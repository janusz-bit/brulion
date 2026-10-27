#include "picturefetcher.h"

#include <QMetaObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QSet>

using namespace Qt::StringLiterals;

namespace brulion::webpicture {

PictureFetcher::PictureFetcher(QNetworkAccessManager *manager, QObject *parent)
    : QObject(parent), m_manager(manager ? manager : new QNetworkAccessManager(this))
{ }

PictureFetcher::~PictureFetcher()
{
    cancelAll();
}

void PictureFetcher::fetch(const QUrl &url)
{
    if (isFetching(url))
        return;
    const QString scheme = url.scheme().toLower();
    if (!url.isValid() || url.host().isEmpty() || (scheme != u"http" && scheme != u"https")) {
        QMetaObject::invokeMethod(
                this, [this, url] { fail(url, FetchError::Network, u"not an http(s) URL"_s); }, Qt::QueuedConnection);
        return;
    }
    m_queue.append(url);
    startNext();
}

bool PictureFetcher::isFetching(const QUrl &url) const
{
    if (m_queue.contains(url))
        return true;
    for (auto it = m_running.cbegin(); it != m_running.cend(); ++it)
        if (it.value() == url)
            return true;
    return false;
}

void PictureFetcher::cancel(const QUrl &url)
{
    cancel(QList<QUrl>{url});
}

void PictureFetcher::cancel(const QList<QUrl> &urls)
{
    if (urls.isEmpty())
        return;
    const QSet<QUrl> gone(urls.cbegin(), urls.cend());
    m_queue.removeIf([&gone](const QUrl &url) { return gone.contains(url); });
    QList<QNetworkReply *> replies;
    for (auto it = m_running.cbegin(); it != m_running.cend(); ++it)
        if (gone.contains(it.value()))
            replies.append(it.key());
    for (QNetworkReply *reply : std::as_const(replies)) {
        m_running.remove(reply);
        m_aborted.remove(reply);
        reply->disconnect(this);
        reply->abort();
        reply->deleteLater();
    }
    if (!replies.isEmpty())
        startNext();
}

void PictureFetcher::cancelAll()
{
    m_queue.clear();
    const auto replies = m_running.keys();
    m_running.clear();
    m_aborted.clear();
    for (QNetworkReply *reply : replies) {
        reply->disconnect(this);
        reply->abort();
        reply->deleteLater();
    }
}

void PictureFetcher::startNext()
{
    while (m_running.size() < MaxParallel && !m_queue.isEmpty() && m_manager)
        start(m_queue.takeFirst());
}

void PictureFetcher::start(const QUrl &url)
{
    QNetworkRequest request(url);
    // Every redirect is checked by redirectAllowed() (redirected() below).
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::UserVerifiedRedirectPolicy);
    request.setMaximumRedirectsAllowed(5);
    // No cookies either way, a generic agent, no language: a picture must not tell a server more
    // than its own address.
    request.setAttribute(QNetworkRequest::CookieLoadControlAttribute, QNetworkRequest::Manual);
    request.setAttribute(QNetworkRequest::CookieSaveControlAttribute, QNetworkRequest::Manual);
    request.setRawHeader("Accept", "image/avif,image/webp,image/png,image/svg+xml,image/*;q=0.8");
    if (!m_simpleRequests) {
        request.setHeader(QNetworkRequest::UserAgentHeader, u"Mozilla/5.0"_s);
        request.setRawHeader("Accept-Language", "*");
    }
    request.setTransferTimeout(m_timeoutMs);
    QNetworkReply *reply = m_manager->get(request);
    m_running.insert(reply, url);
    const auto abortWith = [this, reply](FetchError error, const QString &detail) {
        if (!m_running.contains(reply) || m_aborted.contains(reply))
            return;
        m_aborted.insert(reply, {error, detail});
        reply->abort();
    };
    const auto tooLarge = [this, abortWith] { abortWith(FetchError::TooLarge, QString::number(m_maxBytes)); };
    connect(reply, &QNetworkReply::redirected, this, [this, reply, url, abortWith](const QUrl &target) {
        if (redirectAllowed(url, target, m_isPrivate))
            Q_EMIT reply->redirectAllowed();
        else
            abortWith(FetchError::Redirect, u"redirect to %1 refused"_s.arg(target.toDisplayString()));
    });
    connect(reply, &QNetworkReply::metaDataChanged, this, [this, reply, tooLarge] {
        const qint64 announced = reply->header(QNetworkRequest::ContentLengthHeader).toLongLong();
        if (announced > m_maxBytes)
            tooLarge();
    });
    connect(reply, &QNetworkReply::downloadProgress, this, [this, tooLarge](qint64 received, qint64) {
        if (received > m_maxBytes)
            tooLarge();
    });
    connect(reply, &QNetworkReply::finished, this, [this, reply] { finish(reply); });
}

void PictureFetcher::finish(QNetworkReply *reply)
{
    reply->deleteLater();
    if (!m_running.contains(reply))
        return; // cancelled
    const QUrl url = m_running.take(reply);
    const auto aborted = m_aborted.constFind(reply);
    const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    if (aborted != m_aborted.constEnd()) {
        const Abort why = *aborted;
        m_aborted.erase(aborted);
        fail(url, why.error, why.detail);
    } else if (reply->error() == QNetworkReply::TimeoutError
               || reply->error() == QNetworkReply::OperationCanceledError) {
        fail(url, FetchError::Timeout, reply->errorString());
    } else if (reply->error() == QNetworkReply::InsecureRedirectError
               || reply->error() == QNetworkReply::TooManyRedirectsError) {
        // The status is the last redirect's (301): no error of the server's.
        fail(url, FetchError::Redirect, reply->errorString());
    } else if (status >= 400 || (status > 0 && (status < 200 || status >= 300))) {
        fail(url, FetchError::Http, QString::number(status));
    } else if (reply->error() != QNetworkReply::NoError) {
        fail(url, FetchError::Network, reply->errorString());
    } else {
        const QByteArray data = reply->readAll();
        const QString format = sniffFormat(data);
        if (data.size() > m_maxBytes)
            fail(url, FetchError::TooLarge, QString::number(m_maxBytes));
        else if (format.isEmpty())
            fail(url, FetchError::NotPicture, reply->header(QNetworkRequest::ContentTypeHeader).toString());
        else
            Q_EMIT fetched(url, data, format);
    }
    startNext();
}

void PictureFetcher::fail(const QUrl &url, FetchError error, const QString &detail)
{
    Q_EMIT failed(url, error, detail);
}

QString errorName(FetchError error)
{
    switch (error) {
    case FetchError::Network: return u"network error"_s;
    case FetchError::Redirect: return u"redirect refused"_s;
    case FetchError::Timeout: return u"timed out"_s;
    case FetchError::Http: return u"HTTP error"_s;
    case FetchError::TooLarge: return u"too large"_s;
    case FetchError::NotPicture: return u"not a picture"_s;
    }
    return {};
}

} // namespace brulion::webpicture
