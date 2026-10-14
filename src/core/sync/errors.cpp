#include "sync/errors.h"

using namespace Qt::StringLiterals;

namespace brulion::sync {

bool WebDavError::isFatal() const
{
    return code == WebDavErrorCode::Auth || code == WebDavErrorCode::Network || code == WebDavErrorCode::Timeout;
}

WebDavError WebDavError::fromStatus(int status, const QString &message, const QString &url)
{
    return WebDavError{codeForStatus(status), status, message, url};
}

WebDavError WebDavError::make(WebDavErrorCode code, const QString &message, const QString &url)
{
    return WebDavError{code, 0, message, url};
}

WebDavErrorCode codeForStatus(int status)
{
    switch (status) {
    case 401: return WebDavErrorCode::Auth;
    case 403: return WebDavErrorCode::Forbidden;
    case 404:
    case 410: return WebDavErrorCode::NotFound;
    case 405: return WebDavErrorCode::NotAllowed;
    case 409: return WebDavErrorCode::Conflict;
    case 412: return WebDavErrorCode::Precondition;
    case 413: return WebDavErrorCode::TooLarge;
    case 423: return WebDavErrorCode::Locked;
    case 507: return WebDavErrorCode::InsufficientStorage;
    default: return status >= 500 ? WebDavErrorCode::Server : WebDavErrorCode::Http;
    }
}

QString errorCodeName(WebDavErrorCode code)
{
    switch (code) {
    case WebDavErrorCode::None: return {};
    case WebDavErrorCode::Auth: return u"auth"_s;
    case WebDavErrorCode::Forbidden: return u"forbidden"_s;
    case WebDavErrorCode::NotFound: return u"not-found"_s;
    case WebDavErrorCode::NotAllowed: return u"not-allowed"_s;
    case WebDavErrorCode::Conflict: return u"conflict"_s;
    case WebDavErrorCode::Precondition: return u"precondition"_s;
    case WebDavErrorCode::TooLarge: return u"too-large"_s;
    case WebDavErrorCode::Locked: return u"locked"_s;
    case WebDavErrorCode::InsufficientStorage: return u"insufficient-storage"_s;
    case WebDavErrorCode::Server: return u"server"_s;
    case WebDavErrorCode::Http: return u"http"_s;
    case WebDavErrorCode::Network: return u"network"_s;
    case WebDavErrorCode::Timeout: return u"timeout"_s;
    case WebDavErrorCode::Parse: return u"parse"_s;
    case WebDavErrorCode::Cancelled: return u"cancelled"_s;
    }
    return {};
}

QString errorCodeName(SyncErrorCode code)
{
    switch (code) {
    case SyncErrorCode::None: return {};
    case SyncErrorCode::LocalEmpty: return u"local-empty"_s;
    case SyncErrorCode::RemoteEmpty: return u"remote-empty"_s;
    case SyncErrorCode::RemoteBusy: return u"remote-busy"_s;
    case SyncErrorCode::NotReady: return u"not-ready"_s;
    }
    return {};
}

} // namespace brulion::sync
