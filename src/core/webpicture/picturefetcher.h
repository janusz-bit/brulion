// PictureFetcher — downloads pictures from the internet for the editor (webpicture.h).
//
// Only http(s); redirects followed at most 5 times and each checked (redirectAllowed(): an https
// picture never comes over http, a picture of a host of the internet never from this device or
// its private network — a refused one fails with Redirect); no cookies sent or kept, no referrer;
// at most MaxBytes (also when the server announces more, it is aborted at once); a stall of
// TimeoutMs fails; the bytes must be a picture by their signature (an HTML page that answers with
// 200 is no picture). One request per URL at a time (a second fetch() of a URL in flight waits
// for the first), at most MaxParallel at once, the rest queued.
//
// A request tells the server no more than needed: `Accept` (picture formats), a generic
// `User-Agent: Mozilla/5.0` (not the app's name) and `Accept-Language: *` (Qt would otherwise
// send the device's language).
#pragma once

#include "webpicture.h"

#include <QByteArray>
#include <QHash>
#include <QList>
#include <QObject>
#include <QPointer>
#include <QUrl>

class QNetworkAccessManager;
class QNetworkReply;

namespace brulion::webpicture {

enum class FetchError {
    Network,    ///< no connection, DNS, TLS, refused
    Redirect,   ///< a redirect refused (redirectAllowed()) or too many: the server did answer
    Timeout,    ///< no progress for TimeoutMs
    Http,       ///< the server answered with an error status (404, 403, …)
    TooLarge,   ///< more than the size limit
    NotPicture, ///< the bytes are no picture (sniffFormat() is "")
};

class PictureFetcher : public QObject {
    Q_OBJECT
public:
    /// Requests at once.
    static constexpr int MaxParallel = 4;

    /// Uses `manager` when given (not owned), else an access manager of its own.
    explicit PictureFetcher(QNetworkAccessManager *manager = nullptr, QObject *parent = nullptr);
    ~PictureFetcher() override;

    /// Fetch `url` (http(s); any other URL fails at once with Network). fetched() or failed()
    /// follows from the event loop.
    void fetch(const QUrl &url);
    /// `url` is being fetched or waits for its turn.
    bool isFetching(const QUrl &url) const;
    /// Forget `url`: taken from the queue or its request aborted (no signal for it).
    void cancel(const QUrl &url);
    /// Forget all of `urls` at once: none of them is started by the requests that free a place
    /// (the queue is cleared of them before any running one is aborted).
    void cancel(const QList<QUrl> &urls);
    /// Abort everything (no signals for the aborted fetches).
    void cancelAll();
    /// Which hosts are private for redirectAllowed() (tests; isPrivateHost() by default).
    void setPrivateHostCheck(HostCheck check) { m_isPrivate = std::move(check); }

    void setMaxBytes(qint64 bytes) { m_maxBytes = bytes; }
    qint64 maxBytes() const { return m_maxBytes; }
    void setTimeout(int ms) { m_timeoutMs = ms; }

Q_SIGNALS:
    /// `data` is a picture in `format` (sniffFormat()).
    void fetched(const QUrl &url, const QByteArray &data, const QString &format);
    /// `detail`: the HTTP status ("404") or the network's error text, for the log.
    void failed(const QUrl &url, brulion::webpicture::FetchError error, const QString &detail);

private:
    void startNext();
    void start(const QUrl &url);
    void finish(QNetworkReply *reply);
    void fail(const QUrl &url, FetchError error, const QString &detail);

    QPointer<QNetworkAccessManager> m_manager;
    QList<QUrl> m_queue;
    QHash<QNetworkReply *, QUrl> m_running;
    /// Replies aborted by us — for their size, or a redirect refused — with what finish() reports
    /// (not the abort's OperationCanceledError).
    struct Abort {
        FetchError error;
        QString detail;
    };
    QHash<QNetworkReply *, Abort> m_aborted;
    HostCheck m_isPrivate = isPrivateHost;
    qint64 m_maxBytes = MaxBytes;
    int m_timeoutMs = TimeoutMs;
};

/// A short English reason of `error` for the log.
QString errorName(FetchError error);

} // namespace brulion::webpicture
