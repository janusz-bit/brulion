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
#include <memory>
#include <utility>

class QIODevice;
class QNetworkAccessManager;
class QNetworkReply;

namespace brulion::sync {

/// Stops a request on its way (HttpRequest::abort): abort() — from the thread that sent it — ends
/// it as a broken connection would (`done` gets a networkError), or does nothing when it is over.
/// The transport says how it stops one (onAbort); a transport that cannot lets it run to its end.
class HttpAbort {
public:
    void abort()
    {
        m_aborted = true;
        if (std::function<void()> hook = std::exchange(m_hook, {}))
            hook();
    }
    bool aborted() const { return m_aborted; }
    /// The transport's: how to stop its request — at once when abort() came first.
    void onAbort(std::function<void()> hook)
    {
        if (m_aborted)
            hook();
        else
            m_hook = std::move(hook);
    }

private:
    bool m_aborted = false;
    std::function<void()> m_hook;
};

struct HttpRequest {
    QByteArray method; ///< "GET", "PUT", "PROPFIND", "MKCOL", …
    QUrl url;
    QList<std::pair<QByteArray, QByteArray>> headers;
    QByteArray body;
    /// Time limit in ms for the whole exchange; 0 = none. Transports that support it abort
    /// the request when it expires; WebDavClient enforces it independently as well.
    int timeoutMs = 0;
    /// timeoutMs counts only the time without data: an answer read while it comes (onData) may
    /// take longer as long as it keeps coming.
    bool timeoutWhileIdle = false;
    /// Transfer progress (payload bytes sent, bytes received so far), called by transports that
    /// know it (NetworkHttpTransport: uploadProgress / downloadProgress) from the event loop.
    /// Counts as activity for stall detection; never needed for correctness.
    std::function<void(qint64 sent, qint64 received)> onProgress;
    /// The largest answer body accepted, in bytes; 0 = DefaultMaxBodyBytes. An answer does not end
    /// where the server said it would: whoever reads one from a network stops here
    /// (HttpResponse::tooLarge) instead of keeping in memory whatever a server goes on sending.
    qint64 maxBodyBytes = 0;
    static constexpr qint64 DefaultMaxBodyBytes = 256LL * 1024 * 1024;
    /// The answer's body as it arrives, piece by piece in order (an answer read while it comes: a
    /// model's, as server-sent events), from the event loop — only a 2xx answer's. The response
    /// still carries all of it. A transport that does not know it hands nothing here.
    std::function<void(QByteArrayView piece)> onData;
    /// Lets the sender stop the request on its way (HttpAbort::abort); none: it cannot be stopped.
    std::shared_ptr<HttpAbort> abort;
    /// Only for a transport that streams() (never set otherwise): the request's body is read from
    /// this device (open for reading, at its start; `body` stays empty) piece by piece as it is
    /// sent — an upload never held in memory. Its size() is the Content-Length.
    std::shared_ptr<QIODevice> bodyDevice;
    /// Only for a transport that streams(): a 2xx answer's body goes here piece by piece, in order,
    /// instead of into HttpResponse::body (which stays empty) — a download never held in memory.
    /// False stops the request (networkError: the piece could not be kept, a full disk). The
    /// pieces are at most StreamPieceBytes each, and the transport keeps no more than that unread;
    /// maxBodyBytes still bounds the whole answer. An error's body (not 2xx) stays in the response.
    std::function<bool(QByteArrayView piece)> bodySink;
    static constexpr qint64 StreamPieceBytes = 256 * 1024;

    /// maxBodyBytes, or the default.
    qint64 bodyLimit() const { return maxBodyBytes > 0 ? maxBodyBytes : DefaultMaxBodyBytes; }
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
    /// The body was larger than HttpRequest::bodyLimit(): there is no status and no body, and
    /// networkError says so — who does not ask sees a failed request, never a cut answer.
    bool tooLarge = false;
    QByteArray protocol;                   ///< "h2" / "http/1.1" when the transport knows it
    /// Why a layer before the server (EncryptingTransport) answered in its place (English, no
    /// secret): it goes into the error's message ("… failed: HTTP 400 (the name is too long to
    /// encrypt)"). Empty for a server's answer.
    QString reason;

    /// Value of a response header (any case), empty when absent.
    QByteArray header(QByteArrayView name) const;
    /// The answer to `request` when its body is over the limit (tooLarge).
    static HttpResponse oversized(const HttpRequest &request);
};

using HttpCallback = std::function<void(const HttpResponse &)>;

/// Does a redirect from `origin` to `target` stay with the same server, so that the request's
/// Authorization header may follow it? The same host and port — another port of a host is
/// another program, perhaps another user's — or the usual upgrade from http to https, each on
/// its default port. Never from https down to http, never to another scheme.
bool isSameServer(const QUrl &origin, const QUrl &target);

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
    /// True when the transport sends HttpRequest::bodyDevice and hands an answer to
    /// HttpRequest::bodySink piece by piece. False (the default): the sender never sets them, and
    /// bodies travel whole in HttpRequest::body and HttpResponse::body.
    virtual bool streams() const { return false; }
};

/// HttpTransport over QNetworkAccessManager. Any verb (PROPFIND, MKCOL, …) via
/// sendCustomRequest; request.timeoutMs aborts the reply (timedOut = true) and also serves as
/// the transfer (inactivity) timeout; an answer announced or grown larger than
/// request.bodyLimit() is aborted too (tooLarge = true). Redirects are followed only to the same
/// server (host and port; http's default port may become https's) and never from https to http,
/// so credentials are not sent to other servers. No caching. request.onData gets the body as it
/// arrives; request.abort stops the reply (networkError "Stopped"). It streams(): a bodyDevice is
/// read by Qt as it sends it, and with a bodySink the reply's read buffer is bounded to
/// HttpRequest::StreamPieceBytes, so a download holds no more than that in memory.
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
    bool streams() const override { return true; }

    QNetworkAccessManager *manager() const;
    /// Maximum number of redirects followed per request (default 5).
    void setMaxRedirects(int count);

private:
    /// `read`: the body's start, read while it came (HttpRequest::onData). `sinkFailed`: the
    /// request's bodySink refused a piece.
    void finish(QNetworkReply *reply, bool timedOut, const QByteArray &read, bool sinkFailed, const HttpCallback &done);

    QPointer<QNetworkAccessManager> m_manager;
    int m_maxRedirects = 5;
};

} // namespace brulion::sync

Q_DECLARE_METATYPE(brulion::sync::HttpExchange)
