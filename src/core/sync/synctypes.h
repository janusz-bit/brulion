// Value types of the sync module: remote entries, per-path sync state, run reports and
// progress. Kept out of src/core/types.h because only the sync module and its UI use them.
#pragma once

#include "sync/errors.h"
#include "types.h"

#include <QDateTime>
#include <QJsonObject>
#include <QList>
#include <QMetaType>
#include <QString>

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

/// What the engine did with one path.
enum class SyncAction { Upload, Download, Merge, Conflict, DeleteLocal, DeleteRemote };

/// "upload", "download", "merge", "conflict", "delete-local", "delete-remote".
QString syncActionName(SyncAction action);

struct SyncChange {
    VaultPath path;
    SyncAction action = SyncAction::Upload;
    friend bool operator==(const SyncChange &, const SyncChange &) = default;
};

/// A file that could not be synced in this run (the rest of the run went on; retried next time).
struct SyncFileError {
    VaultPath path;
    QString message;
    friend bool operator==(const SyncFileError &, const SyncFileError &) = default;
};

/// Summary of one SyncEngine run.
struct SyncReport {
    int uploaded = 0;              ///< new or changed locally (or restored on the server)
    int downloaded = 0;            ///< new or changed remotely (or restored locally)
    int deletedLocal = 0;          ///< deleted on the server -> moved to the local trash
    int deletedRemote = 0;         ///< deleted locally -> deleted on the server
    int merged = 0;                ///< changed on both sides and merged automatically
    QList<VaultPath> conflicts;    ///< real conflicts: local kept, remote saved as a copy
    QList<VaultPath> conflictCopies; ///< conflict copies created in this run
    QList<SyncFileError> errors;   ///< per-file failures
    QList<VaultPath> skipped;      ///< skipped on purpose (larger than the size limit)
    QList<SyncChange> changes;     ///< every change made, in completion order
    QDateTime started;
    QDateTime finished;

    bool cancelled = false;        ///< stopped by SyncEngine::cancel(); finished files stay synced
    WebDavError remoteError;       ///< the run stopped on this (auth, network, listing, …)
    SyncError syncError;           ///< the engine refused to run (local-empty, remote-empty, …)

    /// The run did not complete: cancelled, or stopped by remoteError / syncError.
    bool failed() const { return remoteError.isError() || syncError.isError(); }
    /// Message of remoteError or syncError ("" when the run did not fail).
    QString failureMessage() const;
    /// errorCodeName() of remoteError or syncError ("" when the run did not fail).
    QString failureCode() const;
    /// Number of changes made (uploads, downloads, deletions, merges, conflicts).
    int changeCount() const;
};

/// Progress of a running sync.
struct SyncProgress {
    enum class Phase { Connecting, Listing, Syncing, Done };
    Phase phase = Phase::Connecting;
    int done = 0;   ///< paths processed so far (Syncing, Done)
    int total = 0;  ///< paths to check in total (Syncing, Done); 0 before
    VaultPath path; ///< the path just processed

    friend bool operator==(const SyncProgress &, const SyncProgress &) = default;
};

/// Options of one sync run.
struct SyncRunOptions {
    /// Allow a run in which one side is empty although files were synced before (normally
    /// refused with SyncErrorCode::LocalEmpty / RemoteEmpty to protect against accidents).
    bool allowEmptySide = false;
    /// Called with every progress update (in addition to SyncEngine::progress).
    std::function<void(const SyncProgress &)> onProgress;
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
};

} // namespace brulion::sync

Q_DECLARE_METATYPE(brulion::sync::SyncReport)
Q_DECLARE_METATYPE(brulion::sync::SyncProgress)
