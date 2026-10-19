// Internal: one run of the SyncEngine (listing, per-path decisions and transfers).
#pragma once

#include "sync/syncengine.h"
#include "sync/webdavclient.h"

#include <QElapsedTimer>
#include <QHash>
#include <QMetaObject>
#include <QPointer>

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
    auto guarded(F &&fn);

    // Phases
    void listRemote();
    void onListed(const QList<RemoteEntry> &entries);
    void checkRemoteIdentity();
    void listLocal();
    void loadStates();
    bool guardEmptySides();
    void guardMassDeletion();
    void processAll();
    void runGroup(const QStringList &group, qsizetype index, std::function<void()> groupDone);
    void finish();

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
