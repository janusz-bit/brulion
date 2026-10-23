// Internal: one run of the SyncEngine (listing, per-path decisions and transfers).
#pragma once

#include "sync/syncengine.h"
#include "sync/webdavclient.h"

#include <QElapsedTimer>
#include <QHash>
#include <QMetaObject>
#include <QPointer>
#include <QSet>

#include <functional>
#include <memory>
#include <optional>

namespace brulion {
class LocalStore;
class Vault;
} // namespace brulion

namespace brulion::sync {

namespace detail {
class JobPool;
}

/// One SyncEngine run. Owned (shared) by the engine while it runs; every asynchronous step
/// keeps it alive (see guarded()) and does nothing once the run finished or the engine called
/// abandon(). Per-path work runs through a JobPool (one job per group of case-variant paths).
///
/// Order: list both sides, load the states, recognise local folder renames, the empty-side
/// guard; then (Transfer) the folder renames are moved on the server (MOVE), the groups without
/// a deletion run first (new, changed and moved files), the mass-deletion guard looks at what
/// they transferred (content that arrived under a new path is moved, not deleted), the groups
/// with a deletion follow, and last the folders (syncfolders.cpp).
class SyncRun : public std::enable_shared_from_this<SyncRun> {
public:
    SyncRun(SyncEngine *engine, SyncRunOptions options, int number);

    void start();
    void cancel();
    /// The engine is going away: drop everything still pending (no report follows).
    void abandon();
    /// The vault, client or store went away: finish now with SyncErrorCode::NotReady (pending
    /// callbacks of a destroyed client would never come).
    void dependencyLost();
    /// What the run waits for (SyncEngine::activity()).
    SyncActivity activity() const;
    int number() const { return m_report.runNumber; }

private:
    /// The local file as read for this run.
    struct LocalSnap {
        bool isText = false;
        QString hash;
        FileStat stat;
        QString text;      ///< isText: the decoded content
        QByteArray bytes;  ///< raw content (empty when not read: unchanged binary file)
        bool hasBytes = false;
    };

    /// What one path's processing ended with.
    struct Outcome {
        enum class Kind { Done, Skipped, Failed, Fatal };
        Kind kind = Kind::Done;
        QString message;
        WebDavError error; ///< Fatal
        QString code;      ///< Failed: SyncFileError::code
        int status = 0;    ///< Failed: HTTP status, when there was one

        static Outcome done() { return {}; }
        static Outcome skipped(const QString &why) { return Outcome{Kind::Skipped, why, {}, {}, 0}; }
        static Outcome failed(const QString &code, const QString &message, int status = 0)
        {
            return Outcome{Kind::Failed, message, {}, code, status};
        }
        static Outcome fromError(const WebDavError &error);
    };
    using Done = std::function<void(const Outcome &)>;
    /// Start a path over after a 412 (the remote changed while we were writing it).
    using Retry = std::function<void()>;

    /// Everything known about one path while processing it.
    struct PathCtx {
        VaultPath path;
        std::optional<LocalSnap> snap;
        std::optional<RemoteEntry> remote; ///< as listed (may be outdated)
        std::optional<SyncFileState> state;
    };

    /// Content of the remote file as fetched by GET.
    struct RemoteContent {
        QByteArray data;
        QString version; ///< stored as SyncFileState::remoteEtag
        QString etag;    ///< usable for If-Match ("" when the server has none)
        WebDavError error;
    };

    /// Decoded text (for text paths that are valid UTF-8) and the content hash of some bytes.
    struct Content {
        std::optional<QString> text;
        QString hash;
    };

    struct ReadResult {
        std::optional<LocalSnap> snap;
        bool vanished = false;
        QString error;
    };

    /// A folder renamed or moved in the vault, recognised by its synced content (MOVE).
    struct FolderMove {
        VaultPath from;
        VaultPath to;
    };

    /// One step of the folder pass.
    struct FolderOp {
        enum class Kind { DeleteLocal, DeleteRemote, CreateLocal, CreateRemote };
        Kind kind = Kind::CreateLocal;
        VaultPath path;
    };

    bool abandoned() const;
    Vault *vault() const;
    WebDavClient *client() const;
    LocalStore *store() const;
    QDateTime now() const;
    void report(const SyncProgress &progress);
    /// A progress update with the run's counters filled in.
    void progress(SyncProgress::Phase phase, int done = 0, int total = 0, const VaultPath &path = {});

    // Instrumentation (phases, request statistics, log lines)
    void beginPhase(SyncPhase phase);
    void endPhase(int items = 0);
    void onExchange(const HttpExchange &exchange);
    void notePreconditionRetry(const VaultPath &path, int attempt);
    QString tag() const;
    QString summary() const;

    /// Wrap a continuation: runs only while the engine lives (keeps this run alive meanwhile).
    template <typename F>
    auto guarded(F &&fn)
    {
        return [self = shared_from_this(), fn = std::forward<F>(fn)](auto &&...args) mutable {
            if (self->m_finished || !self->m_engine)
                return; // abandoned: the engine is gone
            if (!self->vault() || !self->client() || !self->store()) {
                self->dependencyLost();
                return;
            }
            fn(std::forward<decltype(args)>(args)...);
        };
    }

    // Phases
    void listRemote();
    void onListed(const QList<RemoteEntry> &entries);
    void checkRemoteIdentity();
    void listLocal();
    void loadStates();
    bool guardEmptySides();
    /// After the first pass: a server that looked empty only had moved files? (guardEmptySides())
    bool recheckRemoteEmpty();
    void guardMassDeletion();
    void startTransfer();
    void processAll();
    /// Run `groups` through a new JobPool, then `next` (also when cancelled or failed).
    void runPass(const QList<QStringList> &groups, std::function<void()> next);
    void afterFirstPass();
    void afterSecondPass();
    void runGroup(const QStringList &group, qsizetype index, std::function<void()> groupDone);
    void finish();

    // Moves and folders (syncfolders.cpp)
    /// A path synced before that exists on one side only (deleted on the other, or restored).
    bool isDeletionCandidate(const VaultPath &path) const;
    /// Folders known from earlier runs: folder states and the folders of the synced files.
    QSet<VaultPath> knownFolders() const;
    /// Local files never synced, by content hash (one-to-one matching of moved content). Only
    /// files whose size one of `wanted` (sync states) has are read.
    QHash<QString, int> newLocalHashes(const QList<SyncFileState> &wanted);
    /// How many of `paths` (synced before) exist under a new path on the other side, matched
    /// one-to-one by content (`arrived`, consumed), by version (a server move keeps the ETag) or
    /// as the same name in another letter case. `here`: the paths vanished locally.
    int countMoved(const QList<VaultPath> &paths, bool here, QHash<QString, int> &arrived) const;
    /// Folder renames in the vault: a folder of synced files missing here (still on the server)
    /// whose synced files are, at least half of them unchanged, at their places below a new local
    /// folder the server does not have.
    QList<FolderMove> findFolderMoves();
    std::optional<VaultPath> renameTarget(const VaultPath &from, const QSet<VaultPath> &known,
                                          const QSet<VaultPath> &localFolders,
                                          const QHash<QString, QList<VaultPath>> &newByName,
                                          const QSet<VaultPath> &taken);
    bool sameContent(const VaultPath &localPath, const SyncFileState &state);
    void moveFolders(qsizetype index, std::function<void()> next);
    /// The server moved `move.from` to `move.to`: states, merge bases and the listing follow.
    void applyFolderMove(const FolderMove &move);
    /// Files and folders written or deleted on the server in this run (m_remoteAll follows).
    void noteRemoteWritten(const VaultPath &path, bool isDir = false);
    void noteRemoteDeleted(const VaultPath &path);
    QSet<VaultPath> localFolders() const;
    QSet<VaultPath> remoteFolders() const;
    void syncFolders(std::function<void()> done);
    void runFolderOps(std::shared_ptr<const QList<FolderOp>> ops, qsizetype index, std::function<void()> done);
    void folderFailed(const VaultPath &path, const QString &code, const QString &message, int status = 0);
    void recordFolders();

    // One path
    void syncPath(const VaultPath &path, Done done);
    static Content classify(const VaultPath &path, const QByteArray &bytes);
    ReadResult readLocal(const VaultPath &path, const FileStat &stat, const std::optional<SyncFileState> &state);
    std::optional<QByteArray> localBytes(const VaultPath &path, const LocalSnap &snap, QString *error);
    std::optional<Outcome> checkLocalUnchanged(const VaultPath &path, const std::optional<LocalSnap> &snap);
    void fetchRemote(const VaultPath &path, const std::optional<RemoteEntry> &listed,
                     std::function<void(const RemoteContent &)> done);
    void versionAfterPut(const VaultPath &path, const QString &etag, std::function<void(const QString &)> done);
    void saveState(const VaultPath &path, const QString &hash, const QString &version,
                   const std::optional<QString> &baseText, const std::optional<FileStat> &stat);
    void forget(const VaultPath &path);
    void record(const VaultPath &path, SyncAction action);
    void refreshState(const PathCtx &ctx, Done done);
    void upload(const PathCtx &ctx, Done done);
    void download(const PathCtx &ctx, Done done);
    void reconcile(const PathCtx &ctx, int attempt, Done done);
    void reconcileAgain(const PathCtx &ctx, int attempt, Done done);
    void compareAndMerge(const PathCtx &ctx, const RemoteContent &remote, const Retry &retry, Done done);
    void publishMerge(const PathCtx &ctx, const RemoteContent &remote, const QString &merged,
                      const QString &remoteText, const Retry &retry, Done done);
    QByteArray mergedBytes(const PathCtx &ctx, const QString &merged);
    std::optional<VaultPath> keepRemoteCopy(const VaultPath &path, const QByteArray &data, bool *created,
                                            QString *error);
    void conflict(const PathCtx &ctx, const RemoteContent &remote, const Retry &retry, Done done);
    void uploadConflictCopy(const VaultPath &copy, const QByteArray &data, Done done);
    void deleteRemote(const PathCtx &ctx, Done done);
    void deleteLocal(const PathCtx &ctx, Done done);
    std::optional<QString> mergeText(const VaultPath &path, const QString &local, const QString &remote,
                                     const std::optional<SyncFileState> &state) const;
    std::optional<VaultPath> findConflictCopy(const VaultPath &path, const QByteArray &content);

    QPointer<SyncEngine> m_engine;
    SyncRunOptions m_options;
    SyncReport m_report;
    QHash<VaultPath, FileStat> m_local;
    QHash<VaultPath, RemoteEntry> m_remote;
    QHash<VaultPath, SyncFileState> m_states;
    /// Every entry of the server as this run knows it: the listing (folders and names that are
    /// not synced included), then what the run wrote, deleted and moved there.
    QHash<VaultPath, RemoteEntry> m_remoteAll;
    QSet<VaultPath> m_folderStates; ///< folders that existed on both sides after a run
    QList<FolderMove> m_moves;      ///< folder renames recognised in this run
    /// Content hashes (with counts) of files uploaded / downloaded to paths never synced before
    /// in this run's first pass: moved content, not deleted content.
    QHash<QString, int> m_uploadedNew;
    QHash<QString, int> m_downloadedNew;
    QList<QStringList> m_laterGroups; ///< groups with a deletion: after the mass-deletion guard
    bool m_recheckRemoteEmpty = false; ///< guardEmptySides() waits for the first pass's downloads
    std::shared_ptr<detail::JobPool> m_pool;
    int m_done = 0;
    int m_total = 0;
    bool m_cancelled = false;
    bool m_finished = false;
    bool m_holdDeletions = false; ///< the mass-deletion guard tripped: no deletions this run

    // Instrumentation
    QElapsedTimer m_runTimer;
    QElapsedTimer m_activityTimer;        ///< restarted by every answer and progress update
    std::optional<SyncPhaseTime> m_phase; ///< the phase running now
    qint64 m_phaseBytes = 0;              ///< payload bytes when the current phase began
    QMetaObject::Connection m_exchangeConnection;
    QList<VaultPath> m_active; ///< paths being processed
    int m_remoteEntries = 0;
    int m_loggedErrors = 0;
};

} // namespace brulion::sync
