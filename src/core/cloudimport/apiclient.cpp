#include "cloudimport/apiclient.h"

#include <QDateTime>
#include <QLocale>
#include <QPointer>
#include <QTimeZone>
#include <QTimer>

namespace brulion::cloudimport {

using namespace Qt::StringLiterals;

bool isHttpsHost(const QUrl &url, QStringView host)
{
    return url.isValid() && url.scheme() == u"https" && url.host().compare(host, Qt::CaseInsensitive) == 0
           && url.userInfo().isEmpty() && (url.port() == -1 || url.port() == 443);
}

bool isHttpsDomain(const QUrl &url, QStringView domain)
{
    if (!url.isValid() || url.scheme() != u"https" || !url.userInfo().isEmpty()
        || (url.port() != -1 && url.port() != 443))
        return false;
    const QString host = url.host().toLower();
    return host == domain || host.endsWith(u'.' + domain.toString());
}

ApiClient::ApiClient(sync::HttpTransport *transport, QObject *parent) : QObject(parent), m_transport(transport) { }

void ApiClient::setToken(const QString &token)
{
    m_token = token;
    m_secret.reset(token);
}

void ApiClient::setHosts(std::function<bool(const QUrl &)> allowed, std::function<bool(const QUrl &)> authorized)
{
    m_allowed = std::move(allowed);
    m_authorized = std::move(authorized);
}

bool ApiClient::allowed(const QUrl &url) const
{
    return url.isValid() && url.scheme() == u"https" && (!m_allowed || m_allowed(url));
}

void ApiClient::cancel()
{
    ++m_generation;
    m_token.clear();
    m_secret.reset();
}

void ApiClient::send(sync::HttpRequest request, Done done)
{
    if (request.method.isEmpty())
        request.method = "GET";
    if (!allowed(request.url)) {
        sync::HttpResponse refused;
        refused.networkError = u"refused"_s;
        done(refused);
        return;
    }
    if (!m_token.isEmpty() && m_authorized && m_authorized(request.url))
        request.headers.append({"Authorization", "Bearer " + m_token.toUtf8()});
    request.timeoutMs = m_limits.timeoutMs;
    request.maxBodyBytes = m_limits.maxBodyBytes;
    const int generation = m_generation;
    if (m_spacingMs > 0) {
        QTimer::singleShot(qint64(m_spacingMs) * m_unitMs / 1000, this, [this, request, done, generation] {
            if (generation == m_generation)
                attempt(request, done, 0, generation);
        });
        return;
    }
    attempt(request, done, 0, generation);
}

void ApiClient::attempt(const sync::HttpRequest &request, const Done &done, int attemptNumber, int generation)
{
    QPointer<ApiClient> self(this);
    m_transport->send(request, [self, request, done, attemptNumber, generation](const sync::HttpResponse &response) {
        if (!self || self->m_generation != generation)
            return;
        ApiClient *c = self.data();
        const int status = response.status;
        const bool retry = status == 429 || status == 503 || status == 504
                           || (status == 0 && !response.timedOut && !response.tooLarge);
        if (retry && attemptNumber < c->m_limits.maxRetries) {
            const QByteArray header = response.header("retry-after").trimmed();
            bool numeric = false;
            int seconds = header.toInt(&numeric);
            if (!numeric && !header.isEmpty()) {
                // An HTTP date: "Wed, 21 Oct 2026 07:28:00 GMT".
                const QDateTime at =
                        QLocale::c().toDateTime(QString::fromLatin1(header).chopped(4), u"ddd, dd MMM yyyy HH:mm:ss"_s);
                if (at.isValid())
                    seconds = int(std::clamp<qint64>(
                            QDateTime::currentDateTimeUtc().secsTo(QDateTime(at.date(), at.time(), QTimeZone::UTC)), 1,
                            3600));
                numeric = at.isValid();
            }
            if (!numeric || seconds < 0)
                seconds = 1 << std::min(attemptNumber, 30);
            seconds = std::clamp(seconds, 1, c->m_limits.maxRetryAfterSeconds);
            QTimer::singleShot(qint64(seconds) * c->m_unitMs, c, [c, request, done, attemptNumber, generation] {
                if (c->m_generation == generation)
                    c->attempt(request, done, attemptNumber + 1, generation);
            });
            return;
        }
        done(response);
    });
}

} // namespace brulion::cloudimport
