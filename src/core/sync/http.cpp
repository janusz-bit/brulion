#include "sync/http.h"

#include "sync/util_p.h"

#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QTimer>

#include <memory>

using namespace Qt::StringLiterals;

namespace brulion::sync {

namespace {

template <typename Headers>
QByteArray findHeader(const Headers &headers, QByteArrayView name)
{
    for (const auto &[key, value] : headers) {
        if (key.compare(name, Qt::CaseInsensitive) == 0)
            return value;
    }
    return {};
}

/// Per-request state shared by the reply's handlers.
struct ReplyState {
    bool timedOut = false;
    QString refusedRedirect; ///< target of a redirect we refused to follow
};

} // namespace

QByteArray HttpRequest::header(QByteArrayView name) const
{
    return findHeader(headers, name);
}

bool HttpRequest::hasHeader(QByteArrayView name) const
{
    for (const auto &entry : headers) {
        if (entry.first.compare(name, Qt::CaseInsensitive) == 0)
            return true;
    }
    return false;
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
    m_maxRedirects = qMax(0, count);
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
        // Credentials travel in the Authorization header: never follow to another host or
        // from https down to http.
        const bool sameHost = target.host().compare(origin.host(), Qt::CaseInsensitive) == 0;
        const bool downgrade = origin.scheme() == u"https"_s && target.scheme() != u"https"_s;
        if (sameHost && !downgrade) {
            Q_EMIT reply->redirectAllowed();
        } else {
            state->refusedRedirect = target.toString(QUrl::RemoveUserInfo);
            reply->abort();
        }
    });
    if (request.timeoutMs > 0) {
        QTimer::singleShot(request.timeoutMs, reply, [reply, state] {
            state->timedOut = true;
            reply->abort();
        });
    }
    connect(reply, &QNetworkReply::finished, this, [this, reply, state, done = std::move(done)] {
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
        for (const auto &[name, value] : reply->rawHeaderPairs())
            response.headers.insert(name.toLower(), value);
        response.body = reply->readAll();
    }
    done(response);
}

} // namespace brulion::sync
