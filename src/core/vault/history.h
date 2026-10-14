// History — device-local version history of vault files (never synced, never in the vault).
//
//   History history(appDataDir + "/history");
//   history.snapshot("Notatka.md", previousContent, "user");  // reason: ChangeSource name
//   history.snapshotBytes(path, fileBytes, "sync")             // exact bytes (any encoding)
//   history.list("Notatka.md")      -> QList<HistoryEntry{id, time, size, reason}>, newest first
//   history.read(id)                -> the snapshot's text (UTF-8, a leading BOM dropped)
//   history.readBytes(id)           -> the snapshot's bytes exactly as stored
//   history.rename(from, to), remove(path), paths()
//   history.prune(maxPerPath = 50, maxAgeDays = 90, keepAtLeast = 5)
//
// Policy (ported from Brulion v1):
//  * identical consecutive content is stored once;
//  * at most one snapshot per path per `throttleSeconds` (5 min) for ordinary reasons ("user",
//    "system", …); "sync", "import" and "external" snapshots are always kept (they precede
//    changes the user did not type), and so is the first ordinary snapshot after one of them;
//  * every snapshot applies the retention rules of the policy (50 per path, 90 days, but always
//    the 5 newest); prune() applies them to every path and removes orphaned content.
// Snapshots are kept in the order they were taken, so a device clock that is set back neither
// reorders them nor makes pruning drop the newest ones, and never suppresses snapshots via the
// throttle. The clock is injectable (setClock) for deterministic tests.
//
// On disk: <dir>/index/<sha1(path)>.json ({"path", "entries": [...]}, oldest first) and the
// content of each snapshot in <dir>/objects/<id>.txt — the file's bytes as they were (normally
// UTF-8; a BOM, CRLF or a legacy encoding such as Windows-1250 is kept byte for byte, so the
// original can always be recovered). Snapshots of the same bytes taken as text or as bytes are
// the same version (the dedupe hash is over the bytes). Content is written before the
// index, so an interrupted snapshot leaves at most an orphan that prune() removes. Index files
// are re-read on every call, so two History objects on one directory never act on stale data.
#pragma once

#include "types.h"

#include <QByteArray>
#include <QByteArrayView>
#include <QDateTime>
#include <QList>
#include <QString>
#include <QStringList>

#include <functional>
#include <optional>

namespace brulion {

/// Snapshot metadata (without the content).
struct HistoryEntry {
    QString id;      ///< opaque, unique; pass to History::read()
    QDateTime time;  ///< when the snapshot was taken (local time)
    qint64 size = 0; ///< size of the recorded bytes
    QString reason;  ///< ChangeSource name of the change that replaced this content, or e.g. "restore"

    friend bool operator==(const HistoryEntry &, const HistoryEntry &) = default;
};

/// Limits applied whenever a snapshot is taken.
struct HistoryPolicy {
    int throttleSeconds = 300; ///< minimum time between ordinary snapshots of one path
    int maxPerPath = 50;       ///< snapshots kept per path
    int maxAgeDays = 90;       ///< older snapshots are dropped …
    int keepAtLeast = 5;       ///< … except for this many newest ones
};

class History {
public:
    explicit History(const QString &dir);

    QString dir() const;

    /// Clock used for snapshot times and age-based pruning (default: QDateTime::currentDateTime;
    /// an empty function restores it).
    void setClock(std::function<QDateTime()> clock);
    void setPolicy(const HistoryPolicy &policy);
    HistoryPolicy policy() const;

    /// Record `content` as a version of `path`. `reason` is the ChangeSource name of the change
    /// about to replace it ("user", "sync", "external", "import", "system") or e.g. "restore".
    /// Returns false only on I/O errors (deduplicated or throttled snapshots return true).
    bool snapshot(const VaultPath &path, const QString &content, const QString &reason);
    /// Record the raw bytes of a version (what the file held, whatever its encoding).
    bool snapshotBytes(const VaultPath &path, QByteArrayView bytes, const QString &reason);

    /// Snapshots of `path`, newest first.
    QList<HistoryEntry> list(const VaultPath &path) const;

    /// Text of snapshot `id` (UTF-8 decoded, a leading byte order mark dropped, like
    /// Vault::readText); `ok` is false (and the result empty) when it does not exist.
    QString read(const QString &id, bool *ok = nullptr) const;
    /// Bytes of snapshot `id` exactly as recorded.
    QByteArray readBytes(const QString &id, bool *ok = nullptr) const;

    /// Paths that have at least one snapshot (also deleted files), sorted.
    QStringList paths() const;

    /// Move the history of `from` to `to` (merged with history already at `to`), so notes keep
    /// their history across renames.
    bool rename(const VaultPath &from, const VaultPath &to);

    /// Delete every snapshot of `path`.
    bool remove(const VaultPath &path);

    /// Apply the retention rules to every path: at most `maxPerPath` per path, none older than
    /// `maxAgeDays`, but always the `keepAtLeast` newest; also removes orphaned content (older
    /// than 10 minutes, so an in-flight snapshot of another instance survives). Returns the
    /// number of snapshots removed from indexes.
    int prune(int maxPerPath = 50, int maxAgeDays = 90, int keepAtLeast = 5);

    /// Description of the last error.
    QString errorString() const;

private:
    struct Stored {
        QString id;
        qint64 time = 0; ///< ms since epoch
        qint64 size = 0;
        QString reason;
        QString hash;
    };
    struct Index {
        VaultPath path;
        QList<Stored> entries;
    };

    QString indexFile(const VaultPath &path) const;
    QString objectFile(const QString &id) const;
    std::optional<Index> loadFile(const QString &file) const;
    QList<Stored> load(const VaultPath &path) const;
    bool save(const VaultPath &path, const QList<Stored> &entries);
    bool writeFile(const QString &file, QByteArrayView bytes);
    void removeObjects(const QList<Stored> &all, const QList<Stored> &kept);
    static QList<Stored> retain(const QList<Stored> &entries, qint64 now, int maxPerPath, int maxAgeDays,
                                int keepAtLeast);
    bool isThrottled(const Stored &newest, const QString &reason, qint64 now) const;
    QString newId(qint64 time) const;
    qint64 now() const;
    bool fail(const QString &message);

    QString m_dir;
    std::function<QDateTime()> m_clock;
    HistoryPolicy m_policy;
    QString m_error;
};

} // namespace brulion
