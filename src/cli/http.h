// A small HTTP/1.1 server for `brulion-cli mcp serve` (docs/CLI.md "Remote MCP server"): an
// incremental request parser that refuses malformed and hostile input with the right status,
// responses, form and query decoding, and a server over QTcpServer (QSslServer for HTTPS) with
// limits on connections, sizes and time. Requests are answered one after another per connection
// (keep-alive and pipelining), each by a handler that may answer later (Reply).
//
// Why our own and not Qt HttpServer: it is no module of the Windows and AppImage builds' Qt, and
// the MCP endpoint needs control over when a body is read at all (only after the bearer token is
// checked), over every limit and over the answer to bad input.
#pragma once

#include <QByteArray>
#include <QElapsedTimer>
#include <QHash>
#include <QHostAddress>
#include <QJsonObject>
#include <QList>
#include <QObject>
#include <QPointer>
#include <QString>
#include <QUrl>

#if QT_CONFIG(ssl)
#include <QSslConfiguration>
#endif

#include <functional>
#include <memory>
#include <optional>

class QTcpServer;
class QTcpSocket;
class QTimer;

namespace brulion::cli::http {

/// Header fields in the order received; names compare case-insensitively (RFC 9110 5.1).
class Headers {
public:
    struct Field {
        QByteArray name;
        QByteArray value;
    };
    void add(const QByteArray &name, const QByteArray &value) { m_fields.append({name, value}); }
    /// Replaces every field of that name.
    void set(const QByteArray &name, const QByteArray &value);
    void remove(QByteArrayView name);
    bool has(QByteArrayView name) const;
    int count(QByteArrayView name) const;
    /// The first field of that name ("" when there is none).
    QByteArray value(QByteArrayView name) const;
    /// Every value of that name, comma-separated lists split and trimmed (Accept, Connection …).
    QList<QByteArray> tokens(QByteArrayView name) const;
    const QList<Field> &fields() const { return m_fields; }
    bool isEmpty() const { return m_fields.isEmpty(); }

private:
    QList<Field> m_fields;
};

struct Request {
    QByteArray method;
    QByteArray target; ///< as sent ("/mcp?x=1")
    QByteArray path;   ///< the target's path, not decoded ("/mcp")
    QByteArray query;  ///< after '?', not decoded
    int minorVersion = 1; ///< HTTP/1.<minorVersion>
    Headers headers;
    QByteArray body;
    QHostAddress peer;
    bool secure = false; ///< came over TLS
};

struct Response {
    int status = 200;
    Headers headers;
    QByteArray body;
    bool close = false; ///< close the connection after it

    static Response json(int status, const QJsonObject &object);
    static Response text(int status, const QByteArray &text);
    static Response empty(int status);
    /// "HTTP/1.1 200 OK\r\n…" with Content-Length (and Date, Connection when closing).
    QByteArray serialize(bool head = false) const;
};

/// The reason phrase of a status ("Not Found").
QByteArray reasonPhrase(int status);

/// application/x-www-form-urlencoded (a query or a form body): pairs in order, '+' a space,
/// %XX decoded as UTF-8. nullopt when malformed: a bad escape, invalid UTF-8, a control character.
std::optional<QList<std::pair<QString, QString>>> parseForm(QByteArrayView data);
/// "%XX"-encoding for a query value (RFC 3986 unreserved characters stay).
QByteArray formEncode(const QString &value);

/// Limits of the parser: the request line, all header fields together and their number.
struct ParserLimits {
    int maxRequestLine = 8 * 1024;
    int maxHeaderBytes = 32 * 1024;
    int maxHeaders = 100;
    int maxChunkLine = 1024;
};

/// Reads one request after another from the bytes of a connection. After the header fields it
/// stops (HeadersDone): the server decides how large a body may be (setBodyLimit) — or answers
/// at once — before any of the body is kept.
class RequestParser {
public:
    enum class State {
        Incomplete, ///< more bytes needed
        HeadersDone,
        Done,
        Failed, ///< errorStatus(): 400, 413, 414, 431, 501 or 505; the connection must close
    };

    explicit RequestParser(ParserLimits limits = {});
    /// Append bytes and parse.
    State feed(QByteArrayView data);
    /// Parse what is buffered.
    State parse();
    /// The largest body accepted (after HeadersDone; a larger one fails with 413).
    void setBodyLimit(qint64 bytes);
    /// The request (its body once Done).
    const Request &request() const { return m_request; }
    Request &request() { return m_request; }
    State state() const { return m_state; }
    int errorStatus() const { return m_errorStatus; }
    QByteArray errorText() const { return m_errorText; }
    /// The request has a body (Content-Length > 0 or chunked).
    bool hasBody() const { return m_chunked || m_contentLength > 0; }
    qint64 contentLength() const { return m_contentLength; }
    /// "Expect: 100-continue" was sent.
    bool expectsContinue() const { return m_expectContinue; }
    /// After Done: start reading the next request from the bytes left over.
    void next();
    /// Bytes buffered that belong to no finished request yet.
    bool hasBufferedBytes() const { return m_pos < m_buffer.size(); }

private:
    enum class Phase { RequestLine, Headers, Body, ChunkSize, ChunkData, ChunkDataEnd, Trailers, Finished };
    State fail(int status, const QByteArray &text);
    /// The next line ending at "\n" (a "\r" before it dropped), or nullopt when incomplete
    /// (`tooLong` when more than `limit` bytes came without a line end).
    std::optional<QByteArray> takeLine(int limit, bool *tooLong);
    State afterHeaders();
    void compact();

    ParserLimits m_limits;
    QByteArray m_buffer;
    qsizetype m_pos = 0;
    Phase m_phase = Phase::RequestLine;
    State m_state = State::Incomplete;
    Request m_request;
    int m_headerBytes = 0;
    int m_emptyLines = 0;
    qint64 m_contentLength = 0;
    qint64 m_bodyLimit = -1; ///< -1: not decided yet
    qint64 m_chunkLeft = 0;
    bool m_chunked = false;
    bool m_expectContinue = false;
    int m_errorStatus = 0;
    QByteArray m_errorText;
};

class Connection;

/// The answer to one request: send() once, now or later (after a worker thread or a fetch).
/// Sending to a connection that closed meanwhile does nothing.
class Reply {
public:
    Reply() = default;
    void send(Response response) const;
    bool alive() const;
    /// A copy that adds these header fields to the response it sends (those it does not set
    /// itself): CORS headers around a handler that knows nothing of them.
    Reply with(const Headers &extra) const;

private:
    friend class Connection;
    Reply(Connection *connection, quint64 serial) : m_connection(connection), m_serial(serial) { }
    QPointer<Connection> m_connection;
    quint64 m_serial = 0;
    std::shared_ptr<const Headers> m_extra;
};

struct ServerLimits {
    int maxConnections = 64;
    int maxConnectionsPerPeer = 16;
    int headerTimeoutMs = 15000;    ///< from a request's first byte to its last header field
    int bodyTimeoutMs = 120000;     ///< for the body
    int idleTimeoutMs = 60000;      ///< keep-alive: between requests
    int handshakeTimeoutMs = 15000; ///< TLS
    int maxRequestsPerConnection = 1000;
    ParserLimits parser;
};

class Server : public QObject {
    Q_OBJECT
public:
    explicit Server(ServerLimits limits = {}, QObject *parent = nullptr);
    ~Server() override;

    /// After a request's header fields: nullopt and `*maxBody` set to read its body, or the
    /// answer to send at once (its body is never read; the connection closes if one follows).
    std::function<std::optional<Response>(const Request &head, qint64 *maxBody)> onHeaders;
    /// A whole request.
    std::function<void(const Request &request, const Reply &reply)> onRequest;
    /// A line for the log (never tokens, passwords or query strings).
    std::function<void(const QString &line)> onLog;

    bool listen(const QHostAddress &address, quint16 port, QString *error);
#if QT_CONFIG(ssl)
    /// HTTPS: call before listen(); later calls change the configuration for new connections
    /// (a renewed certificate).
    void setTlsConfiguration(const QSslConfiguration &configuration);
#endif
    bool tls() const { return m_tls; }
    quint16 port() const;
    QHostAddress address() const;
    int connectionCount() const { return int(m_connections.size()); }
    const ServerLimits &limits() const { return m_limits; }

private:
    friend class Connection;
    void accept();
    void dropped(Connection *connection);
    void log(const QString &line) const;

    ServerLimits m_limits;
    QTcpServer *m_server = nullptr;
    bool m_tls = false;
#if QT_CONFIG(ssl)
    QSslConfiguration m_tlsConfiguration;
#endif
    QList<Connection *> m_connections;
    QHash<QString, int> m_perPeer;
};

/// One client connection (internal; Reply holds a QPointer to it).
class Connection : public QObject {
    Q_OBJECT
public:
    Connection(Server *server, QTcpSocket *socket);
    ~Connection() override;
    QString peerKey() const { return m_peerKey; }
    void deliver(quint64 serial, Response response);
    bool waitingFor(quint64 serial) const { return m_inFlight && serial == m_serial; }
    /// Refuse it at once (too many connections) and close.
    void refuse(int status);
    /// Start reading.
    void start();

private:
    void readSome();
    void process();
    void write(const Response &response);
    void closeSoon();
    void shut();
    void armTimer();
    void timedOut();
    void finish();

    Server *m_server;
    QTcpSocket *m_socket;
    QTimer *m_timer;
    RequestParser m_parser;
    QString m_peerKey;
    quint64 m_serial = 0;
    int m_handled = 0;
    bool m_inFlight = false;
    bool m_closing = false;
    bool m_headersSeen = false; ///< the current request's header fields were handed over
    bool m_refusedBody = false; ///< answered after its header fields: its body is never read
    bool m_head = false;        ///< the request in flight is a HEAD request
    /// An answer given after the header fields, sent once the (small) body has been read and
    /// dropped, so that the connection can stay open.
    std::optional<Response> m_early;
    bool m_keepAlive = true;
    QElapsedTimer m_started;
    QByteArray m_logMethod;
    QByteArray m_logPath;
    bool m_finished = false;
    bool m_shut = false;
    qint64 m_drained = 0;
    QTimer *m_lingerQuiet = nullptr;
};

/// The origin of a URL ("https://example.com", "http://127.0.0.1:8780"): scheme and host lower
/// case, the port only when it is not the scheme's default.
QString originOf(const QUrl &url);
/// The language of a page for people: "pl" when the browser prefers Polish to English
/// (Accept-Language), else "en".
QString preferredLanguage(const Request &request);
/// Text for HTML: & < > " ' escaped, control characters dropped.
QString htmlEscape(const QString &text);

} // namespace brulion::cli::http
