// HTTP transport abstraction: everything in the sync module talks HTTP through HttpTransport,
// so WebDavClient, SyncEngine and SyncthingClient are testable without a network (tests use an
// in-memory WebDAV server). NetworkHttpTransport is the real implementation on top of
// QNetworkAccessManager (desktop, Android and WebAssembly).
#pragma once

#include <QByteArray>
#include <QByteArrayView>
#include <QHash>
#include <QList>
#include <QMetaType>
#include <QObject>
#include <QPair>
#include <QPointer>
#include <QString>
#include <QUrl>

#include <functional>

class QNetworkAccessManager;
class QNetworkReply;

namespace brulion::sync {

struct HttpRequest {
    QByteArray method; ///< "GET", "PUT", "PROPFIND", "MKCOL", …
    QUrl url;
    QList<QPair<QByteArray, QByteArray>> headers;
    QByteArray body;
    /// Time limit in ms for the whole exchange; 0 = none. Transports that support it abort
    /// the request when it expires; WebDavClient enforces it independently as well.
    int timeoutMs = 0;
    /// Transfer progress (payload bytes sent, bytes received so far), called by transports that
    /// know it (NetworkHttpTransport: uploadProgress / downloadProgress) from the event loop.
    /// Counts as activity for stall detection; never needed for correctness.
    std::function<void(qint64 sent, qint64 received)> onProgress;

    /// Value of a request header (case-insensitive name), empty when absent.
    QByteArray header(QByteArrayView name) const;
    bool hasHeader(QByteArrayView name) const;
};

struct HttpResponse {
    int status = 0;                        ///< HTTP status; 0 = no HTTP answer (see networkError)
    QHash<QByteArray, QByteArray> headers; ///< names in lower case
    QByteArray body;
    QString networkError;                  ///< transport failure (DNS, refused, TLS, timeout)
    bool timedOut = false;                 ///< the time limit expired
    QByteArray protocol;                   ///< "h2" / "http/1.1" when the transport knows it

    /// Value of a response header (any case), empty when absent.
    QByteArray header(QByteArrayView name) const;
};

using HttpCallback = std::function<void(const HttpResponse &)>;

/// One finished request, for statistics and logs (no headers, no bodies).
struct HttpExchange {
    QByteArray method;
    QUrl url;       ///< user info removed
    int status = 0; ///< 0: no HTTP answer
    bool timedOut = false;
    QString networkError; ///< transport failure text (redacted)
    qint64 elapsedMs = 0;
    qint64 bytesSent = 0;     ///< request body
    qint64 bytesReceived = 0; ///< response body
    QByteArray contentType;   ///< of the answer ("text/html" helps with Parse errors)
    QByteArray protocol;      ///< "h2" / "http/1.1" / ""
};

/// Sends HTTP requests. Implementations must call `done` exactly once, from the event loop of
/// the calling thread (WebDavClient tolerates a synchronous call, but real transports never
/// make one). Single-threaded by design: everything runs on the thread that owns the client.
class HttpTransport {
public:
    virtual ~HttpTransport() = default;
    virtual void send(const HttpRequest &request, HttpCallback done) = 0;
};

/// HttpTransport over QNetworkAccessManager. Any verb (PROPFIND, MKCOL, …) via
/// sendCustomRequest; request.timeoutMs aborts the reply (timedOut = true) and also serves as
/// the transfer (inactivity) timeout. Redirects are followed only within the same host and
/// never from https to http, so credentials are not sent to other servers. No caching.
class NetworkHttpTransport : public QObject, public HttpTransport {
    Q_OBJECT
public:
    /// Uses its own QNetworkAccessManager.
    explicit NetworkHttpTransport(QObject *parent = nullptr);
    /// Uses `manager` (not owned; must outlive this transport's requests).
    explicit NetworkHttpTransport(QNetworkAccessManager *manager, QObject *parent = nullptr);
    ~NetworkHttpTransport() override;
    Q_DISABLE_COPY_MOVE(NetworkHttpTransport)

    void send(const HttpRequest &request, HttpCallback done) override;

    QNetworkAccessManager *manager() const;
    /// Maximum number of redirects followed per request (default 5).
    void setMaxRedirects(int count);

private:
    void finish(QNetworkReply *reply, bool timedOut, const HttpCallback &done);

    QPointer<QNetworkAccessManager> m_manager;
    int m_maxRedirects = 5;
};

} // namespace brulion::sync

Q_DECLARE_METATYPE(brulion::sync::HttpExchange)
