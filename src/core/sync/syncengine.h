// Bidirectional sync of a vault with a WebDAV folder (Nextcloud, ownCloud, any WebDAV server).
#pragma once

#include "sync/synctypes.h"

#include <QByteArray>
#include <QDateTime>
#include <QHash>
#include <QList>
#include <QObject>
#include <QPointer>
#include <QString>

#include <functional>
#include <memory>
#include <optional>

namespace brulion {
class LocalStore;
class Vault;
} // namespace brulion

namespace brulion::sync {

class SyncRun;
class WebDavClient;

/// Files the engine merges as text, by extension: md markdown txt json csv canvas css ics
/// opml html svg. Everything else is transferred as opaque bytes.
bool isSyncTextPath(const VaultPath &path);

/// Paths the engine syncs: everything the vault tracks except ignored folders (.trash, .git,
/// .obsidian, logseq, …; hidden ones in any letter case — on case-insensitive storage a remote
/// ".GIT/hooks/x" would land in the real .git) and temp files, plus ".brulion/config.json".
/// Conflict copies are synced like normal files.
bool isSyncablePath(const VaultPath &path);

/// Strict UTF-8 decoding for sync (a leading BOM is dropped); std::nullopt when the bytes are
/// not valid UTF-8 (e.g. a Windows-1250 CSV) — such files travel as raw bytes, never re-encoded.
std::optional<QString> decodeSyncText(const QByteArray &bytes);

/// Facts about one path that decide what to do (planSync()).
struct SyncFacts {
    bool local = false;
    bool remote = false;
    bool known = false;  ///< a sync state exists (the path was synced before)
    bool localChanged = false;
    bool remoteChanged = false;
};

enum class SyncPlan { Noop, Upload, Download, Reconcile, DeleteRemote, DeleteLocal, Forget };

/// The decision table (pure). Reconcile = compare contents and merge (both sides changed, or
/// both exist without history). A deletion on one side loses against a modification on the
/// other side (the modified file is restored).
SyncPlan planSync(const SyncFacts &facts);

/// Group sorted paths whose names differ only in letter case (one file on case-insensitive
/// storage such as Android shared storage). Inside a group, names that exist locally but not on
/// the server come first: after a case-only rename on another device the old local name is
/// trashed before the new one is downloaded, not the other way round.
QList<QStringList> groupByCase(const QStringList &sortedPaths, const std::function<bool(const VaultPath &)> &isLocal,
                               const std::function<bool(const VaultPath &)> &isRemote);

/// Bidirectional vault <-> WebDAV sync with three-way merge.
///
/// For every path the engine compares the local file, the remote file and what both looked like
/// after the last sync (SyncFileState + merge base, kept per path in the device-local
/// LocalStore under "sync/state/", "sync/base/"; "sync/meta" remembers the account + folder),
/// then uploads, downloads, deletes or merges:
///  * new / changed on one side -> transferred; deleted on one side and unchanged on the other
///    -> deleted there too (locally into the vault trash); deleted vs. changed -> restored;
///  * changed on both sides: text files (<= 2 MB keep a merge base) are merged with merge3();
///    the vault config key by key (mergeJson3()); a real conflict keeps the local file and saves
///    the remote one as "Name.conflict-<device>-<yyyyMMdd-HHmmss>.ext" next to it (uploaded too),
///    binary files likewise keep both;
///  * 412 Precondition Failed (changed on the server meanwhile) -> fetched again and re-merged;
///  * the first sync with content on both sides keeps everything (identical files are only
///    recorded, text files are unioned against an empty base).
/// Every file travels byte for byte (a BOM, CRLF or a legacy encoding is kept; a merge result is
/// uploaded exactly as the vault wrote it locally). Excluded: .trash,
/// ignored folders and temp files (isSyncablePath()); files larger than maxFileSize are skipped.
/// One run at a time (sync() while running joins it); cancel() stops between files; a failing
/// file does not abort the others (its error is reported, it is retried next time), but auth,
/// network and timeout failures stop the run. Up to `concurrency` (4) transfers are in flight.
/// The engine refuses to mirror a side on which every previously synced file has disappeared
/// (SyncErrorCode::LocalEmpty / RemoteEmpty) unless SyncRunOptions::allowEmptySide is set, and
/// refuses to run while the vault folder is missing or on another storage than at open()
/// (SyncErrorCode::VaultUnavailable, see Vault::isStorageAvailable()).
/// Mass-deletion guard (like the Nextcloud client): when a run would delete more than 25 % of
/// the files of one side and at least 10 of them (MassDeletionRatio, MassDeletionMinFiles), or
/// every file of a side (at least 2), it applies no deletion at all — the other changes are
/// synced — and reports SyncReport::needsConfirmation with the pendingDeletions; a run with
/// SyncRunOptions::confirmDeletions (or allowEmptySide) applies them. The vault config does not
/// count. Held-back paths keep their sync state, so later runs plan the same deletions.
/// When the vault, client or store is destroyed during a run, the run ends with
/// SyncErrorCode::NotReady. Everything runs on the owner's thread; the vault is accessed
/// synchronously, network I/O is asynchronous.
class SyncEngine : public QObject, public SyncRunner {
    Q_OBJECT
public:
    /// Store key prefixes / key (device-local bookkeeping).
    static QString stateKey(const VaultPath &path);
    static QString baseKey(const VaultPath &path);
    /// A folder that existed on both sides after a run ("sync/folder/<path>").
    static QString folderKey(const VaultPath &path);
    static QString metaKey();

    /// Mass-deletion guard: a side loses more than this share of its files ...
    static constexpr double MassDeletionRatio = 0.25;
    /// ... and at least this many files.
    static constexpr int MassDeletionMinFiles = 10;

    /// None of the pointers is owned; `device` names this device in conflict copies.
    SyncEngine(Vault *vault, WebDavClient *client, LocalStore *store, const QString &device,
               QObject *parent = nullptr);
    ~SyncEngine() override;
    Q_DISABLE_COPY_MOVE(SyncEngine)

    QString device() const { return m_device; }
    Vault *vault() const;
    WebDavClient *client() const;
    LocalStore *store() const;

    /// Clock for report times, sync states and conflict copy names (default: current local time).
    void setClock(std::function<QDateTime()> clock);
    QDateTime now() const;
    /// Transfers in flight (default 4, at least 1).
    void setConcurrency(int count);
    int concurrency() const { return m_concurrency; }
    /// Larger files are skipped (default 200 MB).
    void setMaxFileSize(qint64 bytes);
    qint64 maxFileSize() const { return m_maxFileSize; }
    /// Text files up to this size keep a merge base (default 2 MB).
    void setMaxBaseSize(qint64 bytes);
    qint64 maxBaseSize() const { return m_maxBaseSize; }

    bool isSyncing() const;

    /// Number of the next run (the app seeds it from SyncHistory::nextNumber(); default 1).
    void setNextRunNumber(int number);
    int nextRunNumber() const { return m_nextRunNumber; }
    /// What the running run waits for (the client's open requests, the last progress).
    SyncActivity activity() const override;

    /// Start a run, or join the running one (then `options` are ignored). `done` gets the
    /// report — also when cancelled or failed — after finished() is emitted. Returns true when a
    /// new run started.
    bool sync(const SyncRunOptions &options = {}, SyncDoneCallback done = {});
    /// Stop the running sync between files; transfers in flight complete. No-op when idle.
    void cancel();

    void startSync(const SyncRunOptions &options, SyncDoneCallback done) override;
    void cancelSync() override;

    /// Forget all bookkeeping: the next sync behaves like the first one (nothing is deleted).
    void resetState();
    /// Stored state of one path (diagnostics, UI).
    std::optional<SyncFileState> stateOf(const VaultPath &path) const;
    /// Stored merge base (last synced text) of one path.
    std::optional<QString> mergeBaseOf(const VaultPath &path) const;
    /// The folder existed on both sides after a run (so its absence on one side later is a
    /// deletion there).
    bool knowsFolder(const VaultPath &path) const;

Q_SIGNALS:
    void started();
    void progress(const brulion::sync::SyncProgress &progress);
    void finished(const brulion::sync::SyncReport &report);

private:
    friend class SyncRun;
    void runFinished(const SyncReport &report);
    void dependencyDestroyed();

    QPointer<Vault> m_vault;
    QPointer<WebDavClient> m_client;
    QPointer<LocalStore> m_store;
    QString m_device;
    std::function<QDateTime()> m_clock;
    int m_concurrency = 4;
    qint64 m_maxFileSize = 200LL * 1024 * 1024;
    qint64 m_maxBaseSize = 2LL * 1024 * 1024;
    std::shared_ptr<SyncRun> m_run;
    QList<SyncDoneCallback> m_waiters;
    int m_nextRunNumber = 1;
    QString m_loggedIdentity; ///< account + folder announced in the log
};

} // namespace brulion::sync
