#include "caldav/feed.h"

#include "caldav/messages_p.h"
#include "log/redact.h"

#include <QLoggingCategory>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QPointer>
#include <QTimer>

namespace brulion::caldav {

using namespace Qt::StringLiterals;
using detail::CalDavMessages;

QUrl feedUrl(const QString &typed, QString *error)
{
    const auto fail = [error](const QString &why) {
        if (error)
            *error = why;
        return QUrl();
    };
    QString text = typed.trimmed();
    if (text.isEmpty())
        return fail(CalDavMessages::tr("Enter the address of the calendar."));
    if (text.contains(u' ') || text.contains(u'\n') || text.contains(u'\t'))
        return fail(CalDavMessages::tr("A calendar address has no spaces."));
    if (text.startsWith(u"webcals://"_s, Qt::CaseInsensitive))
        text = u"https://"_s + text.sliced(10);
    else if (text.startsWith(u"webcal://"_s, Qt::CaseInsensitive))
        text = u"https://"_s + text.sliced(9);
    else if (!text.contains(u"://"_s))
        text = u"https://"_s + text;
    QUrl url(text, QUrl::StrictMode);
    if (!url.isValid())
        url = QUrl(text, QUrl::TolerantMode);
    const QString scheme = url.scheme().toLower();
    if (!url.isValid() || (scheme != u"https" && scheme != u"http"))
        return fail(CalDavMessages::tr("Use an address that starts with https://, http:// or webcal://."));
    if (url.host().isEmpty())
        return fail(CalDavMessages::tr("The address names no server."));
    url.setScheme(scheme);
    url.setUserInfo({});
    url.setFragment({});
    if (error)
        error->clear();
    return url;
}

bool looksLikeCalendar(QByteArrayView data)
{
    if (data.startsWith("\xEF\xBB\xBF"))
        data = data.sliced(3);
    qsizetype i = 0;
    while (i < data.size() && (data[i] == ' ' || data[i] == '\t' || data[i] == '\r' || data[i] == '\n'))
        ++i;
    return data.sliced(i).first(std::min<qsizetype>(15, data.size() - i)).compare("BEGIN:VCALENDAR", Qt::CaseInsensitive)
           == 0;
}

FeedFetcher::FeedFetcher(sync::HttpTransport *transport) : m_transport(transport) { }

void FeedFetcher::fetch(const QUrl &url, const QString &etag, const QString &lastModified,
                        std::function<void(const FeedResult &)> done)
{
    step(url, etag, lastModified, 0, std::move(done));
}

void FeedFetcher::step(const QUrl &url, const QString &etag, const QString &lastModified, int redirects,
                       std::function<void(const FeedResult &)> done)
{
    sync::HttpRequest request;
    request.method = "GET";
    request.url = url;
    request.timeoutMs = m_timeoutMs;
    request.timeoutWhileIdle = true;
    request.maxBodyBytes = m_maxBytes;
    request.headers.emplaceBack(QByteArrayLiteral("Accept"), "text/calendar, */*;q=0.5");
    if (!m_browser)
        request.headers.emplaceBack(QByteArrayLiteral("User-Agent"), "Brulion");
    if (!etag.isEmpty())
        request.headers.emplaceBack(QByteArrayLiteral("If-None-Match"), etag.toUtf8());
    if (!lastModified.isEmpty())
        request.headers.emplaceBack(QByteArrayLiteral("If-Modified-Since"), lastModified.toUtf8());
    const QString shown = url.toString(QUrl::RemoveUserInfo | QUrl::RemoveQuery);
    m_transport->send(request, [this, url, etag, lastModified, redirects, shown,
                                done = std::move(done)](const sync::HttpResponse &response) {
        FeedResult result;
        result.url = url;
        if (response.tooLarge) {
            result.error = sync::WebDavError::make(sync::WebDavErrorCode::Oversized,
                                                   u"The calendar is larger than %1 bytes"_s.arg(m_maxBytes), shown);
            done(result);
            return;
        }
        if (response.status == 0) {
            const auto code = response.timedOut ? sync::WebDavErrorCode::Timeout : sync::WebDavErrorCode::Network;
            result.error = sync::WebDavError::make(code, logging::redact(response.networkError), shown);
            result.maybeCors = m_browser && !response.timedOut;
            done(result);
            return;
        }
        if (response.status >= 300 && response.status < 400 && response.status != 304) {
            const QUrl target = url.resolved(QUrl(QString::fromUtf8(response.header("location"))));
            const bool downgrade = url.scheme() == u"https" && target.scheme() != u"https";
            if (!target.isValid() || target.host().isEmpty() || downgrade || redirects >= MaxRedirects
                || (target.scheme() != u"https" && target.scheme() != u"http")) {
                result.error = sync::WebDavError::make(sync::WebDavErrorCode::Http,
                                                       u"Refused a redirect to %1"_s.arg(target.toString(QUrl::RemoveUserInfo)),
                                                       shown);
                result.error.status = response.status;
                done(result);
                return;
            }
            QUrl next = target;
            next.setUserInfo({});
            step(next, etag, lastModified, redirects + 1, std::move(done));
            return;
        }
        if (response.status == 304) {
            result.notModified = true;
            result.etag = QString::fromUtf8(response.header("etag"));
            result.lastModified = QString::fromUtf8(response.header("last-modified"));
            if (result.etag.isEmpty())
                result.etag = etag;
            if (result.lastModified.isEmpty())
                result.lastModified = lastModified;
            done(result);
            return;
        }
        if (response.status < 200 || response.status >= 300) {
            result.error = sync::WebDavError::fromStatus(response.status, u"GET answered %1"_s.arg(response.status), shown);
            done(result);
            return;
        }
        if (!looksLikeCalendar(response.body)) {
            result.error = sync::WebDavError::make(
                    sync::WebDavErrorCode::Parse,
                    u"The answer is no calendar (%1)"_s.arg(QString::fromUtf8(response.header("content-type"))), shown);
            done(result);
            return;
        }
        result.data = response.body;
        result.etag = QString::fromUtf8(response.header("etag"));
        result.lastModified = QString::fromUtf8(response.header("last-modified"));
        done(result);
    });
}

// ---- FeedHttpTransport -----------------------------------------------------------------------

struct FeedHttpTransport::Private {
    QNetworkAccessManager manager;
};

FeedHttpTransport::FeedHttpTransport(QObject *parent) : QObject(parent), d(std::make_unique<Private>())
{
    d->manager.setCookieJar(nullptr);
}

FeedHttpTransport::~FeedHttpTransport() = default;

void FeedHttpTransport::send(const sync::HttpRequest &request, sync::HttpCallback done)
{
    QNetworkRequest req(request.url);
    for (const auto &[name, value] : request.headers)
        req.setRawHeader(name, value);
    // Other servers too (a feed has no credentials), never from https down to http.
    req.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
    req.setMaximumRedirectsAllowed(FeedFetcher::MaxRedirects);
    req.setAttribute(QNetworkRequest::CacheLoadControlAttribute, QNetworkRequest::AlwaysNetwork);
    req.setAttribute(QNetworkRequest::CacheSaveControlAttribute, false);
    req.setAttribute(QNetworkRequest::CookieLoadControlAttribute, QNetworkRequest::Manual);
    req.setAttribute(QNetworkRequest::CookieSaveControlAttribute, QNetworkRequest::Manual);
    if (request.timeoutMs > 0)
        req.setTransferTimeout(request.timeoutMs);
    QNetworkReply *reply = d->manager.sendCustomRequest(req, request.method, request.body);
    const qint64 limit = request.bodyLimit();
    auto tooLarge = std::make_shared<bool>(false);
    const auto stopAbove = [reply, tooLarge, limit](qint64 bytes) {
        if (bytes > limit && !*tooLarge) {
            *tooLarge = true;
            reply->abort();
        }
    };
    connect(reply, &QNetworkReply::metaDataChanged, reply,
            [reply, stopAbove] { stopAbove(reply->header(QNetworkRequest::ContentLengthHeader).toLongLong()); });
    connect(reply, &QNetworkReply::readyRead, reply, [reply, stopAbove] { stopAbove(reply->bytesAvailable()); });
    sync::HttpRequest asked;
    asked.method = request.method;
    asked.url = request.url;
    asked.maxBodyBytes = request.maxBodyBytes;
    connect(reply, &QNetworkReply::finished, this, [reply, tooLarge, asked, done = std::move(done)] {
        reply->deleteLater();
        if (*tooLarge) {
            done(sync::HttpResponse::oversized(asked));
            return;
        }
        sync::HttpResponse response;
        const QVariant status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute);
        const QNetworkReply::NetworkError error = reply->error();
        if (error == QNetworkReply::TimeoutError || error == QNetworkReply::OperationCanceledError) {
            response.timedOut = error == QNetworkReply::TimeoutError;
            response.networkError = reply->errorString();
        } else if ((error != QNetworkReply::NoError && int(error) < 200) || !status.isValid()) {
            response.networkError = reply->errorString();
        } else {
            response.status = status.toInt();
            for (const auto &[name, value] : reply->rawHeaderPairs())
                response.headers.insert(name.toLower(), value);
            response.body = reply->readAll();
        }
        done(response);
    });
}

} // namespace brulion::caldav
