// The index cache (docs/PERFORMANCE.md, L2): a notebook's parsed notes and full-text index kept
// on the device (<device state folder>/index-cache.bin), so opening the notebook again parses only
// what changed. Part of the index module — see index/index.h.
//
// Validity: the file's identity (its format, markdown::ParserVersion, SearchIndex::FormatVersion,
// the Qt and app versions, VaultIndex::parseFingerprint() of the config, the vault id) must equal
// the current one — any update of the app parses everything once —, and every note's record is
// used only for the same path with the same byte size and the same VaultIndex::contentKey() of
// the text just read (the file is always read: a touched file is a hit, a same-size,
// same-modification-time edit a miss). Full-text documents carry their note's content key too
// (VaultIndex::adoptSearch()). Corrupt input never crashes nor allocates more than the file holds:
// a bad header, footer or directory ignores the whole file (Reader::open() says why), a bad record
// only its note (parsed instead). Someone who can write the app's data folder can inject a fake
// parse — the trust level of History and LocalStore in the same folder.
//
// Format (little-endian; varint = unsigned LEB128, zigzag for signed values; str = varint
// (UTF-8 byte length + 1) << 1 | utf16 then the bytes, 0 = null — UTF-16 for strings with lone
// surrogates, which UTF-8 cannot carry):
//   header    "BRULIDX1" | u32 0x01020304 | varint format | varint parser | varint search
//             | str qVersion() | str applicationVersion() | str parse fingerprint | str vault id
//             | u64 fnv1a64(header so far)
//   records   per note: body | u64 fnv1a64(body)  (everything derivable is recomputed: title,
//             children, headings, tasks, reminders, block ids, journal date)
//   search    optional: body | u64 fnv1a64(body)
//   directory varint count | count × {str path, varint size, zigzag mtime, u64 content key,
//             varint body offset, varint body length (0: no record, parse on load)}
//             | u64 fnv1a64(directory)
//   footer    48 bytes: u64 directory offset | u64 directory length | u64 search offset
//             | u64 search length (0: none) | u32 notes | u32 flags | "BRULEND1"
#pragma once

#include "index/searchindex.h"
#include "types.h"

#include <QByteArray>
#include <QHash>
#include <QString>

#include <functional>
#include <memory>
#include <optional>

namespace brulion {
class VaultIndex;
}

namespace brulion::indexcache {

/// Bump when the encoding changes (also when Block, Task, Reminder or Link gain a field).
inline constexpr quint32 FormatVersion = 1;

/// What a cache file must have been written with to be used.
struct Identity {
    quint32 format = FormatVersion;
    quint32 parser = 0; ///< markdown::ParserVersion
    quint32 search = 0; ///< SearchIndex::FormatVersion
    QString qt;         ///< qVersion(): Unicode tables of folding, case and NFC
    QString app;        ///< QCoreApplication::applicationVersion()
    QString parse;      ///< VaultIndex::parseFingerprint(config)
    QString vaultId;    ///< Vault::id()

    /// The identity of this build for `config` and the vault `vaultId`.
    static Identity current(const VaultConfig &config, const QString &vaultId);
    friend bool operator==(const Identity &, const Identity &) = default;
};

/// A note in a cache file's directory.
struct Record {
    VaultPath path;
    qint64 fileSize = 0;    ///< bytes of the file when it was indexed
    qint64 mtime = 0;       ///< ms since the epoch (diagnostics only)
    quint64 contentKey = 0; ///< VaultIndex::contentKey() of its text
    qint64 offset = 0;      ///< of the record's body in the file
    qint64 length = 0;      ///< 0: no record (a value the format cannot hold: parsed on load)
};

/// An opened cache file (immutable: any thread may decode from it).
class Reader {
public:
    /// Validate magic, byte order, `expected` identity, the checksums of header and directory and
    /// every offset; nullptr with the reason in `why` when unusable.
    static std::shared_ptr<const Reader> open(QByteArray file, const Identity &expected, QString *why = nullptr);

    /// The record of `path`, or nullptr.
    const Record *find(const VaultPath &path) const;
    qsizetype noteCount() const;

    /// The note of `record` for `content` (the file's text, whose content key and size the caller
    /// checked): exactly what markdown::parseNote(path, content, config) returns. nullopt when the
    /// record's checksum or structure fails (the caller parses). Any thread.
    std::optional<ParsedNote> decodeNote(const Record &record, const VaultPath &path, const QString &content,
                                         const VaultConfig &config) const;

    bool hasSearch() const;
    /// The full-text index in steps (on the owner's thread without workers) or at once.
    class SearchDecoder {
    public:
        /// Decode for about `budgetMs` (at least one term); true when finished (or failed).
        bool step(int budgetMs);
        bool failed() const;
        /// The decoded index (empty when it failed).
        SearchIndex take();

        SearchDecoder(SearchDecoder &&) noexcept;
        SearchDecoder &operator=(SearchDecoder &&) noexcept;
        ~SearchDecoder();

        struct Private; // opaque (indexcache.cpp)

    private:
        friend class Reader;
        explicit SearchDecoder(std::unique_ptr<Private> d);
        std::unique_ptr<Private> d;
    };
    SearchDecoder searchDecoder() const;

    Reader(const Reader &) = delete;
    Reader &operator=(const Reader &) = delete;
    ~Reader();

private:
    Reader() = default;
    QByteArray m_file;
    QHash<VaultPath, Record> m_records;
    qint64 m_searchOffset = 0;
    qint64 m_searchLength = 0;
};

/// The size and modification time of a file as the vault knows them (Vault::stat()).
using StatFn = std::function<std::optional<FileStat>(const VaultPath &)>;

/// Encodes an index on its thread in slices (a whole file); writes nothing itself
/// (notebook::IndexCacheKeeper does). The full-text section is written only when the index's
/// search is complete when the writer starts.
class Writer {
public:
    Writer(const VaultIndex &index, Identity identity, StatFn stat);
    ~Writer();
    Writer(const Writer &) = delete;
    Writer &operator=(const Writer &) = delete;

    enum class State {
        More,    ///< call step() again
        Done,    ///< `out` holds the whole file
        Aborted, ///< the index changed meanwhile: start again later
    };
    /// Append about `budgetMs` of work to `out` (the same buffer on every call).
    State step(int budgetMs, QByteArray *out);
    /// Notes written so far.
    qsizetype notesWritten() const;
    bool hasSearch() const;

private:
    struct Private;
    std::unique_ptr<Private> d;
};

/// The whole file at once (tests, brulion-cli).
QByteArray encode(const VaultIndex &index, const Identity &identity, const StatFn &stat);

} // namespace brulion::indexcache
