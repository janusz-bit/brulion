// PictureFetcher — downloads pictures from the internet for the editor (webpicture.h).
//
// Only http(s); redirects followed only as safe as the request (an https picture never comes
// over http) and at most 5 times; no cookies sent or kept, no referrer; at most MaxBytes (also
// when the server announces more, it is aborted at once); a stall of TimeoutMs fails; the bytes
// must be a picture by their signature (an HTML page that answers with 200 is no picture). One
// request per URL at a time (a second fetch() of a URL in flight waits for the first), at most
// MaxParallel at once, the rest queued.
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
    /// Abort everything (no signals for the aborted fetches).
    void cancelAll();

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
    /// Replies aborted by us for their size (finished() then reports TooLarge, not Network).
    QHash<QNetworkReply *, bool> m_tooLarge;
    qint64 m_maxBytes = MaxBytes;
    int m_timeoutMs = TimeoutMs;
};

/// A short English reason of `error` for the log.
QString errorName(FetchError error);

} // namespace brulion::webpicture
