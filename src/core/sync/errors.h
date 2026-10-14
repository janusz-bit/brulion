// Errors of the sync module: typed WebDAV failures (by HTTP status or transport failure) and
// engine-level refusals (e.g. a side that suddenly looks empty). Nothing in the module throws;
// failures travel as values in callbacks and reports.
#pragma once

#include <QString>

namespace brulion::sync {

/// Machine-readable category of a failed WebDAV request; the UI translates it.
enum class WebDavErrorCode {
    None,
    Auth,                ///< 401 — wrong user name or (app) password
    Forbidden,           ///< 403
    NotFound,            ///< 404, 410
    NotAllowed,          ///< 405
    Conflict,            ///< 409 — typically a missing parent folder
    Precondition,        ///< 412 — If-Match / If-None-Match failed: changed meanwhile
    TooLarge,            ///< 413
    Locked,              ///< 423
    InsufficientStorage, ///< 507 — quota exceeded
    Server,              ///< any other 5xx
    Http,                ///< any other unexpected status
    Network,             ///< no HTTP answer at all (DNS, refused connection, TLS, offline)
    Timeout,             ///< no answer within the time limit
    Parse,               ///< the answer is not a WebDAV multistatus (e.g. an HTML login page)
    Cancelled,           ///< the caller cancelled the operation
};

/// A failed WebDAV request. `code == None` means success.
struct WebDavError {
    WebDavErrorCode code = WebDavErrorCode::None;
    int status = 0;  ///< HTTP status; 0 when there was no HTTP answer
    QString message; ///< technical details (English) for logs and a "details" view
    QString url;     ///< the request URL, when known

    bool isError() const { return code != WebDavErrorCode::None; }
    bool is(WebDavErrorCode c) const { return code == c; }
    /// Failures after which continuing with other files is pointless: Auth, Network, Timeout.
    bool isFatal() const;

    /// The error for an unexpected HTTP status (`codeForStatus`).
    static WebDavError fromStatus(int status, const QString &message, const QString &url = {});
    static WebDavError make(WebDavErrorCode code, const QString &message, const QString &url = {});
};

/// Category of an HTTP status: 401 Auth, 403 Forbidden, 404/410 NotFound, 405 NotAllowed,
/// 409 Conflict, 412 Precondition, 413 TooLarge, 423 Locked, 507 InsufficientStorage,
/// other >= 500 Server, everything else Http.
WebDavErrorCode codeForStatus(int status);

/// Stable lower-case name ("auth", "not-found", "insufficient-storage", …) for logs, settings
/// and the UI's translation table; "" for None.
QString errorCodeName(WebDavErrorCode code);

/// Why the sync engine refused to run, or why a file could not be synced.
enum class SyncErrorCode {
    None,
    LocalEmpty,   ///< none of the files synced before exist locally (vault storage lost?)
    RemoteEmpty,  ///< none of them exist on the server any more (wrong folder? wiped server?)
    RemoteBusy,   ///< a file kept changing on the server while merging (per file; retried later)
    NotReady,     ///< no open vault, client or store
};

/// An engine-level failure. `code == None` means success.
struct SyncError {
    SyncErrorCode code = SyncErrorCode::None;
    QString message;

    bool isError() const { return code != SyncErrorCode::None; }
    bool is(SyncErrorCode c) const { return code == c; }
};

/// "local-empty", "remote-empty", "remote-busy", "not-ready"; "" for None.
QString errorCodeName(SyncErrorCode code);

} // namespace brulion::sync
