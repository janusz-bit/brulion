// Link resolution (docs/SYNTAX.md §2) for the vault index: target -> vault file. Internal.
//
// Order: 1) exact vault path (with or without `.md`, case-insensitive, NFC; among case variants
// the exact spelling wins); 2) path relative to the linking file's folder; 3) base-name match
// (case-insensitive, NFC; for targets with folders a path-suffix match) — several candidates:
// shortest path, then alphabetical (code units); 4) frontmatter `aliases`. Explicitly relative
// targets (`./x`, `../x`) try the relative path first; a leading `/` anchors the target at the
// vault root. A target with a real file extension (`photo.png`, see isAttachmentExtension())
// prefers attachments, anything else (`Dom`, `Dr. Smith`, `Spotkanie 14.10`) prefers notes.
//
// NameTable keeps the lookup maps incrementally. Every file a target can resolve to shares the
// target's linkKey() (lower-cased base name without `.md`) as its note/file name key or as the
// linkKey() of one of its aliases, which lets the index re-resolve only the links that a file
// change can affect.
#pragma once

#include "index/smallsets_p.h"
#include "types.h"

#include <QHash>
#include <QSet>
#include <QString>
#include <QStringList>

#include <optional>

namespace brulion::index_detail {

/// Case-insensitive key of a vault path: NFC, lower-cased ("Projekty/Dom.md" -> "projekty/dom.md").
QString pathKey(const QString &path);
/// Name key of a note: lower-cased title ("Projekty/Dom.md" -> "dom").
QString noteNameKey(const VaultPath &path);
/// Name key of an attachment: lower-cased file name ("assets/Foto.PNG" -> "foto.png").
QString fileNameKey(const VaultPath &path);
/// Key of an alias: NFC, trimmed, lower-cased.
QString aliasKey(const QString &alias);
/// How a link target is written.
enum class TargetForm {
    /// As typed by a user or API caller: may carry `[[ ]]`, a `|display` part and a `#fragment`.
    Raw,
    /// Link::target from the Markdown parser: display text and fragment are already split off,
    /// and a Markdown link target is percent-decoded — so '#' and '|' in it are part of the
    /// file name (`[C#](C%23.md)` -> "C#.md").
    Parsed,
};

/// Strip what is not part of the file name: for Raw targets surrounding `[[ ]]`, a `|display`
/// part (also the table-escaped `\|`) and a `#heading` / `#^block` fragment; trim; backslashes
/// become slashes.
QString cleanTarget(const QString &target, TargetForm form = TargetForm::Raw);
/// Invalidation key of a link target: its base name without `.md`, NFC, lower-cased.
QString linkKey(const QString &target, TargetForm form = TargetForm::Raw);
/// Invalidation keys under which links that may resolve through `alias` are filed (the alias as
/// a Raw and as a Parsed target).
QStringList aliasLinkKeys(const QString &alias);
/// Does the target carry a URL scheme (`https:`, `mailto:`, …)? Windows drives excluded.
bool isExternalTarget(QStringView target);
/// paths::isAttachmentExtension() (shared with the markdown module's path parts of a link).
bool isAttachmentExtension(QStringView ext);
/// Is an unresolved target a page (no real extension, or `.md`) rather than a missing attachment?
bool isPageTarget(const QString &target, TargetForm form = TargetForm::Raw);

/// Shortest path, then smallest by code units; empty for an empty set.
VaultPath pickBest(const PathSet &paths);

/// What NameTable::resolve() looks a link target up by: the keys derived from the target and the
/// linking file's path alone (NFC, lower case, the paths to try, the name and alias keys) — pure,
/// so any thread can derive them. NameTable::resolve(target, from, form) is resolve() of such a
/// plan whose keys are derived when a lookup first needs them; the index loader's workers derive
/// every key of a note's links beforehand (complete(), VaultIndex::prepare()), so merging the note
/// on the index's thread only looks the keys up. Not thread-safe while incomplete.
class ResolvePlan {
public:
    ResolvePlan() = default;
    ResolvePlan(const QString &target, const VaultPath &from, TargetForm form);

    /// Derive every key now (the lookups then change nothing in the plan: another thread may
    /// read it).
    void complete() const;

private:
    friend class NameTable;
    /// One path tried (NameTable::lookupPath()): its NFC spelling and lower-cased key.
    struct Probe {
        QString spelled;
        QString key;
        bool markdown = false; ///< the key ends with .md / .markdown: only that file
    };
    /// The base-name lookup (NameTable::lookupName()) of the lower-cased path.
    struct Name {
        QString path;            ///< lower-cased NFC target (normalised when it has folders)
        QString stem;            ///< `path` without .md / .markdown
        QString noteBase;        ///< basename(stem): the notes' name key
        QString fileBase;        ///< basename(path): the attachments' name key
        bool markdown = false;   ///< `path` ends with .md / .markdown
        bool attachment = false; ///< `path` has a real file extension: attachments first
    };
    enum class Kind : quint8 {
        Nothing, ///< resolves to nothing (a path that normalises to "")
        From,    ///< an empty target (`[[#^id]]`): the linking file
        Bare,    ///< a name without folders: the vault root, the linking file's folder, names
        Path,    ///< a path: relative, exact, under the linking file's folder, names
    };

    int probeCount() const { return m_probeCount; }
    const Probe &probe(int i) const;
    const Name &name() const;
    const QString &aliasKey() const;
    /// The path probe `i` tries.
    QString probePath(int i) const;

    Kind m_kind = Kind::Nothing;
    bool m_attachmentFirst = false; ///< a file with the target's extension before a note
    bool m_explicitRelative = false;
    quint8 m_probeCount = 0;
    QString m_target;  ///< cleanTarget()
    VaultPath m_from;  ///< the linking file
    QString m_lowered; ///< Bare: lower(nfc(target)); Path: normalize(target)
    QString m_fromDir; ///< the linking file's folder (as the kind joins it)
    mutable std::optional<Probe> m_probes[2];
    mutable std::optional<Name> m_name;
    mutable std::optional<QString> m_aliasKey;
};

/// Incrementally maintained lookup tables for link resolution.
class NameTable {
public:
    /// The keys a note is filed under (addNote()): derived from its path and aliases alone.
    struct NoteNames {
        QString pathKey;       ///< pathKey(path)
        QString nameKey;       ///< noteNameKey(path)
        QStringList aliasKeys; ///< aliasKey() of each alias that has one
        static NoteNames of(const VaultPath &path, const QStringList &aliases);
    };

    void addNote(const VaultPath &path, const QStringList &aliases);
    void addNote(const VaultPath &path, const NoteNames &names);
    void removeNote(const VaultPath &path, const QStringList &aliases);
    void addAttachment(const VaultPath &path);
    void removeAttachment(const VaultPath &path);
    void clear();
    /// Make this an empty table that reads `base` wherever it was not changed itself — the names
    /// after a move (VaultIndex::planRelocation(): a few names change; a changed copy of the whole
    /// table would cost O(notes)). `base` must outlive this table and stay unchanged.
    void overlay(const NameTable *base);

    /// Resolve `target` written in `from` (see the file comment). An empty target (`[[#^id]]`)
    /// resolves to `from`. Returns an empty path when nothing matches.
    VaultPath resolve(const QString &target, const VaultPath &from = {}, TargetForm form = TargetForm::Raw) const;
    /// resolve() of a planned target (the same result as resolve() of the plan's target and file).
    VaultPath resolve(const ResolvePlan &plan) const;
    /// resolve() of `target` written at the vault root, among notes only (a tag names a note,
    /// docs/SYNTAX.md §3): exact path ± `.md` / `.markdown`, base name or path suffix (shortest
    /// path, then alphabetical), aliases. An attachment of that name (a `TODO` file next to
    /// Projekty/TODO.md) never hides a note. Empty when no note matches.
    VaultPath resolveNote(const QString &target, TargetForm form = TargetForm::Raw) const;
    /// The note at exactly vault path `path` with `.md` / `.markdown` added (case-insensitive,
    /// NFC; among case variants the exact spelling wins), never an attachment; empty when none.
    /// The page beside a folder (VaultIndex::resolveLinkPart()).
    VaultPath notePath(const QString &path) const;
    /// Every key of every map with its files, sorted (tests: two tables hold the same names).
    QStringList describe() const;

private:
    /// File at exactly the probe's path (case-insensitive, NFC), also with `.md` / `.markdown`
    /// added; notes first unless `attachmentFirst`. Among case variants the exact spelling wins.
    VaultPath lookupPath(const ResolvePlan::Probe &probe, bool attachmentFirst) const;
    /// The file at `key` + `suffix` (lower-cased NFC); `spelled` + `suffix` is the NFC path as
    /// written, which wins among case variants.
    VaultPath lookupExact(QStringView key, QStringView spelled, QLatin1StringView suffix = {}) const;
    /// lookupExact() of the note at `key` + `.md`, else at `key` + `.markdown`.
    VaultPath lookupNote(QStringView key, QStringView spelled) const;
    VaultPath lookupName(const ResolvePlan::Name &name) const;
    VaultPath lookupAlias(const QString &target) const;
    /// lookupAlias() given the plan (its alias key derived only when there are aliases).
    VaultPath lookupAlias(const ResolvePlan &plan) const;

    using Map = QHash<QString, PathSet>;
    /// The files under `key` in the map `field`, the base's when this overlay did not change
    /// them; nullptr when there are none.
    const PathSet *find(Map NameTable::*field, QStringView key) const;
    /// Has the map `field` (or the base's) any key? (A fast path: may say yes for an overlay
    /// that hides every one of them.)
    bool hasAny(Map NameTable::*field) const;
    void add(Map NameTable::*field, const QString &key, const VaultPath &path);
    void remove(Map NameTable::*field, const QString &key, const VaultPath &path);

    Map m_byPath;    ///< pathKey -> notes and attachments
    Map m_noteNames; ///< noteNameKey -> notes
    Map m_fileNames; ///< fileNameKey -> attachments
    Map m_aliases;   ///< aliasKey -> notes
    /// overlay(): the table read where this one has no entry (an entry with no files hides the
    /// base's).
    const NameTable *m_base = nullptr;
};

} // namespace brulion::index_detail
