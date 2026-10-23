// Value types of the sync module: remote entries, per-path sync state, run reports and
// progress. Kept out of src/core/types.h because only the sync module and its UI use them.
#pragma once

#include "sync/errors.h"
#include "types.h"

#include <QByteArray>
#include <QDateTime>
#include <QJsonObject>
#include <QList>
#include <QMap>
#include <QMetaType>
#include <QString>
#include <QStringList>

#include <functional>
#include <optional>

namespace brulion::sync {

/// A file or folder on the WebDAV server, relative to the client's base URL.
struct RemoteEntry {
    VaultPath path;     ///< NFC, no leading or trailing slash
    bool isDir = false;
    QString etag;       ///< normalised ETag; empty when the server sent none
    qint64 mtime = 0;   ///< ms since epoch; 0 when unknown
    qint64 size = 0;    ///< bytes; 0 for folders or when unknown

    friend bool operator==(const RemoteEntry &, const RemoteEntry &) = default;
};

/// Version string of a remote file: its ETag, or for servers without ETags "m<mtime>-s<size>".
/// Stored as SyncFileState::remoteEtag.
QString remoteVersion(const RemoteEntry &entry);

/// What both sides looked like after the last successful sync of one path. Persisted in the
/// LocalStore under "sync/state/<path>".
struct SyncFileState {
    QString localHash;           ///< content hash of the local file (text: of its decoded text)
    QString remoteEtag;          ///< remote version (remoteVersion()); "" = unknown
    qint64 size = 0;             ///< local size in bytes
    std::optional<qint64> mtime; ///< local mtime in ms (quick change check for binary files)
    qint64 syncedAt = 0;         ///< when the path was synced, ms since epoch

    QJsonObject toJson() const;
    /// std::nullopt when `json` lacks localHash / remoteEtag / size.
    static std::optional<SyncFileState> fromJson(const QJsonObject &json);
    friend bool operator==(const SyncFileState &, const SyncFileState &) = default;
};

/// What the engine did with one path (a file, or a folder for the folder actions).
enum class SyncAction {
    Upload,
    Download,
    Merge,
    Conflict,
    DeleteLocal,
    DeleteRemote,
    CreateFolderLocal,  ///< a folder of the server (empty there) created in the vault
    CreateFolderRemote, ///< a folder of the vault (empty here) created on the server (MKCOL)
    DeleteFolderLocal,  ///< a folder deleted on the server and empty here: removed here
    DeleteFolderRemote, ///< a folder deleted here and empty on the server: deleted there
    MoveRemote,         ///< a folder renamed or moved here, moved on the server (WebDAV MOVE)
};

/// "upload", "download", "merge", "conflict", "delete-local", "delete-remote",
/// "folder-create-local", "folder-create-remote", "folder-delete-local", "folder-delete-remote",
/// "move-remote".
QString syncActionName(SyncAction action);
/// The folder actions (CreateFolderLocal … MoveRemote).
bool isFolderAction(SyncAction action);

struct SyncChange {
    VaultPath path; ///< MoveRemote: the folder's new path (SyncReport::folderMoves has both)
    SyncAction action = SyncAction::Upload;
    friend bool operator==(const SyncChange &, const SyncChange &) = default;
};

/// A folder renamed or moved in the vault and moved on the server (WebDAV MOVE).
struct SyncFolderMove {
    VaultPath from;
    VaultPath to;
    friend bool operator==(const SyncFolderMove &, const SyncFolderMove &) = default;
};

/// Why a run started (SyncScheduler; SyncRunOptions::trigger).
enum class SyncTrigger {
    Unknown,
    Start,        ///< the first run after the scheduler started (app start, settings saved)
    Timer,        ///< the periodic interval
    Retry,        ///< a retry after a failed run (back-off)
    Manual,       ///< "Sync now"
    LocalChange,  ///< after edits (debounced), or a change during the previous run
    NetworkBack,  ///< the network came back
    Foreground,   ///< the app came back to the foreground
    Background,   ///< the app went to the background with pending edits
    Confirmation, ///< the user confirmed held-back deletions
};
/// "unknown" "start" "timer" "retry" "manual" "edit" "network" "foreground" "background" "confirm"
QString syncTriggerName(SyncTrigger trigger);
std::optional<SyncTrigger> syncTriggerFromName(QStringView name);

/// Steps of one run, in order. Without a compare-then-transfer split the per-path reading,
/// comparing and transferring happen together in Transfer; Compare covers loading the sync
/// states, recognising folder renames and the empty-side guard. Transfer: folder moves on the
/// server, the paths without deletions, the mass-deletion guard, the paths with deletions,
/// then the folders.
enum class SyncPhase { Connect, ListRemote, ListLocal, Compare, Transfer, SaveState };
/// "connect" "list-remote" "list-local" "compare" "transfer" "save-state"
QString syncPhaseName(SyncPhase phase);
std::optional<SyncPhase> syncPhaseFromName(QStringView name);

/// How long one phase of a run took.
struct SyncPhaseTime {
    SyncPhase phase = SyncPhase::Connect;
    qint64 startMs = 0; ///< since the run started (monotonic)
    qint64 durationMs = 0;
    int items = 0;    ///< connect: 0; list-remote: folders listed; list-local: local files;
                      ///< compare: tracked paths; transfer: paths processed; save-state: 0
    qint64 bytes = 0; ///< list-remote: listing answers; transfer: payload sent + received;
                      ///< save-state: 0
    friend bool operator==(const SyncPhaseTime &, const SyncPhaseTime &) = default;
};

/// One HTTP request of a run (the slowest are kept).
struct SyncRequestTime {
    QByteArray method;
    VaultPath path; ///< relative to the WebDAV base ("" = the base folder)
    int status = 0; ///< 0: no HTTP answer
    qint64 ms = 0;
    qint64 bytes = 0; ///< sent + received payload
    friend bool operator==(const SyncRequestTime &, const SyncRequestTime &) = default;
};

/// Counters of one run.
struct SyncStats {
    int requests = 0;
    int failedRequests = 0; ///< no answer or an unexpected status (>= 400 except 404/412)
    int timeouts = 0;
    int preconditionRetries = 0; ///< 412: re-fetched and merged again
    QMap<QByteArray, int> requestsByMethod;
    qint64 requestMs = 0; ///< sum of request durations (÷ requests = average)
    qint64 bytesSent = 0;
    qint64 bytesReceived = 0;
    qint64 listingBytes = 0; ///< PROPFIND answers
    int remoteFolders = 0;
    int remoteFiles = 0;
    int localFiles = 0;
    int trackedFiles = 0; ///< sync states loaded
    int pathsChecked = 0;
    qint64 longestStallMs = 0;      ///< longest time without any answer while requests were open
    QByteArray protocol;            ///< "h2" / "http/1.1" of the last answer ("" unknown)
    QList<SyncRequestTime> slowest; ///< the 5 slowest requests, slowest first
    friend bool operator==(const SyncStats &, const SyncStats &) = default;
};

/// A file that could not be synced in this run (the rest of the run went on; retried next time).
struct SyncFileError {
    VaultPath path;
    QString message; ///< technical (English), redacted (logging::redact)
    /// errorCodeName(WebDavErrorCode) of the failed request ("locked", "forbidden", "too-large",
    /// "insufficient-storage", "server", "conflict", "http", "parse", …) or a local code:
    /// "read-failed", "write-failed", "case-clash", "unseen-local-file", "remote-busy",
    /// "conflict-copy-failed".
    QString code;
    int status = 0; ///< HTTP status when there was one
    friend bool operator==(const SyncFileError &, const SyncFileError &) = default;
};

/// Summary of one SyncEngine run.
struct SyncReport {
    int uploaded = 0;              ///< new or changed locally (or restored on the server)
    int downloaded = 0;            ///< new or changed remotely (or restored locally)
    int deletedLocal = 0;          ///< deleted on the server -> moved to the local trash
    int deletedRemote = 0;         ///< deleted locally -> deleted on the server
    int merged = 0;                ///< changed on both sides and merged automatically
    int foldersCreated = 0;        ///< empty folders created here or on the server
    int foldersDeleted = 0;        ///< emptied folders deleted here or on the server
    int foldersMoved = 0;          ///< folders renamed here and moved on the server (MOVE)
    QList<SyncFolderMove> folderMoves; ///< those moves, in order
    QList<VaultPath> conflicts;    ///< real conflicts: local kept, remote saved as a copy
    QList<VaultPath> conflictCopies; ///< conflict copies created in this run
    QList<SyncFileError> errors;   ///< per-file failures
    QList<VaultPath> skipped;      ///< skipped on purpose (larger than the size limit)
    QList<SyncChange> changes;     ///< every change made (files and folders), in completion order
    QDateTime started;
    QDateTime finished;

    bool cancelled = false;        ///< stopped by SyncEngine::cancel(); finished files stay synced
    WebDavError remoteError;       ///< the run stopped on this (auth, network, listing, …)
    SyncError syncError;           ///< the engine refused to run (local-empty, remote-empty, …)

    /// The mass-deletion guard held back every deletion of this run (see SyncEngine): nothing
    /// was deleted on either side, everything else was synced. Show pendingDeletions to the
    /// user and sync again with SyncRunOptions::confirmDeletions to apply them.
    bool needsConfirmation = false;
    /// The deletions held back (action DeleteLocal / DeleteRemote), sorted by path.
    QList<SyncChange> pendingDeletions;

    int runNumber = 0; ///< SyncEngine's counter ("#42" in log lines)
    SyncTrigger trigger = SyncTrigger::Unknown;
    QList<SyncPhaseTime> phases; ///< in order; a failed run has the phases it reached
    SyncStats stats;
    qint64 durationMs = 0; ///< monotonic, start to finish

    /// The run did not complete: cancelled, or stopped by remoteError / syncError.
    bool failed() const { return remoteError.isError() || syncError.isError(); }
    /// Message of remoteError or syncError ("" when the run did not fail).
    QString failureMessage() const;
    /// errorCodeName() of remoteError or syncError ("" when the run did not fail).
    QString failureCode() const;
    /// Number of changes made (uploads, downloads, deletions, merges, conflicts, folders).
    int changeCount() const;
};

/// Progress of a running sync.
struct SyncProgress {
    enum class Phase {
        Connecting,   ///< identity check
        Listing,      ///< the server's folders (done = folders answered, total = folders found so far)
        ListingLocal, ///< this device's files (rescan)
        Comparing,    ///< sync states, guards
        Syncing,      ///< per path: compare, transfer, merge (done / total = paths)
        Saving,       ///< the sync state is written
        Done,
    };
    Phase phase = Phase::Connecting;
    int done = 0;            ///< Listing: folders answered; Syncing, Done: paths processed so far
    int total = 0;           ///< Listing: folders found so far (grows); Syncing, Done: paths in total
    VaultPath path;          ///< the path just processed
    QList<VaultPath> active; ///< Syncing: paths being processed now (≤ concurrency), oldest first
    int remoteEntries = 0;   ///< Listing: files and folders found so far
    qint64 bytesDone = 0;    ///< payload bytes sent + received so far
    int changes = 0;         ///< uploads, downloads, merges, deletions, conflicts, folders so far
    int errors = 0;          ///< per-file errors so far
    qint64 elapsedMs = 0;    ///< since the run started

    friend bool operator==(const SyncProgress &, const SyncProgress &) = default;
};

/// Options of one sync run.
struct SyncRunOptions {
    /// Allow a run in which one side is empty although files were synced before (normally
    /// refused with SyncErrorCode::LocalEmpty / RemoteEmpty to protect against accidents).
    bool allowEmptySide = false;
    /// Apply deletions even when they trip the mass-deletion guard (the user confirmed the list
    /// from SyncReport::pendingDeletions). allowEmptySide implies it.
    bool confirmDeletions = false;
    /// Called with every progress update (in addition to SyncEngine::progress).
    std::function<void(const SyncProgress &)> onProgress;
    /// Why this run starts (reported back in SyncReport::trigger and the log).
    SyncTrigger trigger = SyncTrigger::Unknown;
};

/// What a running sync is waiting for (stall detection, SyncScheduler).
struct SyncActivity {
    bool running = false;
    int runNumber = 0; ///< SyncReport::runNumber of the running run (log lines)
    qint64 idleMs = 0; ///< since the last answer, transfer progress or finished path
    int requestsInFlight = 0;
    QStringList waitingFor; ///< "PROPFIND Dziennik/", "GET Projekty/Plan.md" (≤ 4, oldest first)
    friend bool operator==(const SyncActivity &, const SyncActivity &) = default;
};

using SyncDoneCallback = std::function<void(const SyncReport &)>;

/// What SyncScheduler drives: implemented by SyncEngine; tests use a fake.
class SyncRunner {
public:
    virtual ~SyncRunner() = default;
    /// Start a run, or join the running one. `done` is called exactly once with the report,
    /// also when the run was cancelled or failed (SyncEngine calls it from the event loop;
    /// SyncScheduler also copes with runners that call it synchronously).
    virtual void startSync(const SyncRunOptions &options, SyncDoneCallback done) = 0;
    /// Ask a running run to stop; it finishes with report.cancelled.
    virtual void cancelSync() = 0;
    /// What the running run waits for (default: nothing known — fake runners in tests).
    virtual SyncActivity activity() const { return {}; }
};

} // namespace brulion::sync

Q_DECLARE_METATYPE(brulion::sync::SyncReport)
Q_DECLARE_METATYPE(brulion::sync::SyncProgress)
Q_DECLARE_METATYPE(brulion::sync::SyncTrigger)
