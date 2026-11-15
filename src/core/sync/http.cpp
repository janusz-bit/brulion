#include "sync/http.h"

#include "sync/util_p.h"

#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QTimer>

#include <algorithm>
#include <memory>
#include <utility>

using namespace Qt::StringLiterals;

namespace brulion::sync {

namespace {

using Header = std::pair<QByteArray, QByteArray>;

/// The first request header called `name` (any case), or nullptr.
const Header *findHeader(const QList<Header> &headers, QByteArrayView name)
{
    const auto it = std::ranges::find_if(
            headers, [name](const Header &h) { return h.first.compare(name, Qt::CaseInsensitive) == 0; });
    return it == headers.cend() ? nullptr : &*it;
}

/// Per-request state shared by the reply's handlers.
struct ReplyState {
    bool timedOut = false;
    bool tooLarge = false;   ///< aborted: the body is over the request's limit
    QString refusedRedirect; ///< target of a redirect we refused to follow
    qint64 sent = 0;
    qint64 received = 0;
};

/// The port a URL leads to: its own, or the scheme's.
int effectivePort(const QUrl &url)
{
    return url.port(url.scheme().compare(u"https"_s, Qt::CaseInsensitive) == 0 ? 443 : 80);
}

} // namespace

bool isSameServer(const QUrl &origin, const QUrl &target)
{
    const bool originSecure = origin.scheme().compare(u"https"_s, Qt::CaseInsensitive) == 0;
    const bool targetSecure = target.scheme().compare(u"https"_s, Qt::CaseInsensitive) == 0;
    if (!targetSecure && target.scheme().compare(u"http"_s, Qt::CaseInsensitive) != 0)
        return false;
    if (target.host().compare(origin.host(), Qt::CaseInsensitive) != 0 || (originSecure && !targetSecure))
        return false;
    if (effectivePort(target) == effectivePort(origin))
        return true;
    return !originSecure && targetSecure && effectivePort(origin) == 80 && effectivePort(target) == 443;
}

QByteArray HttpRequest::header(QByteArrayView name) const
{
    const Header *found = findHeader(headers, name);
    return found ? found->second : QByteArray();
}

bool HttpRequest::hasHeader(QByteArrayView name) const
{
    return findHeader(headers, name) != nullptr;
}

HttpResponse HttpResponse::oversized(const HttpRequest &request)
{
    HttpResponse response;
    response.tooLarge = true;
    // One multi-argument arg(): percent escapes in the URL ("%20") must not count as placeholders.
    response.networkError = u"The answer to %1 %2 is larger than %3 bytes"_s.arg(
            QString::fromLatin1(request.method), request.url.toString(QUrl::RemoveUserInfo),
            QString::number(request.bodyLimit()));
    return response;
}

QByteArray HttpResponse::header(QByteArrayView name) const
{
    const QByteArray lower = name.toByteArray().toLower();
    if (const auto it = headers.constFind(lower); it != headers.cend())
        return *it;
    for (auto it = headers.cbegin(); it != headers.cend(); ++it) {
        if (it.key().compare(name, Qt::CaseInsensitive) == 0)
            return it.value();
    }
    return {};
}

NetworkHttpTransport::NetworkHttpTransport(QObject *parent)
    : QObject(parent)
    , m_manager(new QNetworkAccessManager(this))
{
}

NetworkHttpTransport::NetworkHttpTransport(QNetworkAccessManager *manager, QObject *parent)
    : QObject(parent)
    , m_manager(manager)
{
}

NetworkHttpTransport::~NetworkHttpTransport() = default;

QNetworkAccessManager *NetworkHttpTransport::manager() const
{
    return m_manager;
}

void NetworkHttpTransport::setMaxRedirects(int count)
{
    m_maxRedirects = std::max(0, count);
}

void NetworkHttpTransport::send(const HttpRequest &request, HttpCallback done)
{
    if (!m_manager) {
        detail::post(this, [done = std::move(done)] {
            HttpResponse response;
            response.networkError = u"No network access manager"_s;
            done(response);
        });
        return;
    }
    QNetworkRequest req(request.url);
    for (const auto &[name, value] : request.headers)
        req.setRawHeader(name, value);
    req.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::UserVerifiedRedirectPolicy);
    req.setMaximumRedirectsAllowed(m_maxRedirects);
    req.setAttribute(QNetworkRequest::CacheLoadControlAttribute, QNetworkRequest::AlwaysNetwork);
    req.setAttribute(QNetworkRequest::CacheSaveControlAttribute, false);
    if (request.timeoutMs > 0)
        req.setTransferTimeout(request.timeoutMs);

    QNetworkReply *reply = m_manager->sendCustomRequest(req, request.method, request.body);
    auto state = std::make_shared<ReplyState>();
    const QUrl origin = request.url;
    connect(reply, &QNetworkReply::redirected, reply, [reply, origin, state](const QUrl &target) {
        // Credentials travel in the Authorization header: never follow to another server or
        // from https down to http.
        if (isSameServer(origin, target)) {
            Q_EMIT reply->redirectAllowed();
        } else {
            state->refusedRedirect = target.toString(QUrl::RemoveUserInfo);
            reply->abort();
        }
    });
    // The reply keeps what arrives until it is read at the end: stop at the limit, both when the
    // server announces more and when it simply goes on sending (the bytes counted are the ones
    // kept, after decompression).
    const qint64 limit = request.bodyLimit();
    const auto stopAbove = [reply, state, limit](qint64 bytes) {
        if (bytes <= limit || state->tooLarge)
            return;
        state->tooLarge = true;
        reply->abort();
    };
    connect(reply, &QNetworkReply::metaDataChanged, reply,
            [reply, stopAbove] { stopAbove(reply->header(QNetworkRequest::ContentLengthHeader).toLongLong()); });
    connect(reply, &QNetworkReply::readyRead, reply, [reply, stopAbove] { stopAbove(reply->bytesAvailable()); });
    if (request.onProgress) {
        // Progress counts as activity (stall detection): a big transfer is not a stalled one.
        connect(reply, &QNetworkReply::uploadProgress, reply,
                [state, onProgress = request.onProgress](qint64 sent, qint64) {
                    state->sent = sent;
                    onProgress(state->sent, state->received);
                });
        connect(reply, &QNetworkReply::downloadProgress, reply,
                [state, onProgress = request.onProgress](qint64 received, qint64) {
                    state->received = received;
                    onProgress(state->sent, state->received);
                });
    }
    if (request.timeoutMs > 0) {
        QTimer::singleShot(request.timeoutMs, reply, [reply, state] {
            state->timedOut = true;
            reply->abort();
        });
    }
    HttpRequest asked; // what the request was, without what it carried
    asked.method = request.method;
    asked.url = request.url;
    asked.maxBodyBytes = request.maxBodyBytes;
    connect(reply, &QNetworkReply::finished, this, [this, reply, state, asked, done = std::move(done)] {
        if (state->tooLarge) {
            reply->deleteLater();
            done(HttpResponse::oversized(asked));
            return;
        }
        if (!state->refusedRedirect.isEmpty()) {
            reply->deleteLater();
            HttpResponse response;
            response.networkError = u"Refused to follow a redirect to %1"_s.arg(state->refusedRedirect);
            done(response);
            return;
        }
        finish(reply, state->timedOut, done);
    });
}

void NetworkHttpTransport::finish(QNetworkReply *reply, bool timedOut, const HttpCallback &done)
{
    reply->deleteLater();
    HttpResponse response;
    const QVariant status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute);
    const QNetworkReply::NetworkError error = reply->error();
    // Errors below 200 are network / proxy layer failures (refused, host not found, TLS,
    // closed mid-transfer, timeout); 2xx-4xx codes mirror HTTP statuses, which are answers.
    const bool transportFailure = error != QNetworkReply::NoError && static_cast<int>(error) < 200;
    if (timedOut || error == QNetworkReply::TimeoutError) {
        response.timedOut = true;
        response.networkError = u"%1 %2 timed out"_s.arg(QString::fromLatin1(reply->request().attribute(
                                                               QNetworkRequest::CustomVerbAttribute).toByteArray()),
                                                           reply->request().url().toString(QUrl::RemoveUserInfo));
    } else if (transportFailure || !status.isValid()) {
        response.networkError = reply->errorString();
    } else {
        response.status = status.toInt();
        response.protocol = reply->attribute(QNetworkRequest::Http2WasUsedAttribute).toBool() ? "h2" : "http/1.1";
        for (const auto &[name, value] : reply->rawHeaderPairs())
            response.headers.insert(name.toLower(), value);
        response.body = reply->readAll();
    }
    done(response);
}

} // namespace brulion::sync
