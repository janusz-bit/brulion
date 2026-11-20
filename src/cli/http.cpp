// The HTTP/1.1 server of `brulion-cli mcp serve` (http.h). Parsing follows RFC 9112 strictly
// where leniency would let a request mean two things (request smuggling): Content-Length and
// Transfer-Encoding together, two Content-Lengths, folded header lines, whitespace before a
// header's colon, bare CRs and other control characters are refused with 400.
#include "http.h"

#include <QDateTime>
#include <QJsonDocument>
#include <QLocale>
#include <QStringDecoder>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTimer>

#if QT_CONFIG(ssl)
#include <QSslServer>
#include <QSslSocket>
#endif

#include <algorithm>
#include <cctype>

using namespace Qt::StringLiterals;

namespace brulion::cli::http {

namespace {

constexpr qint64 kReadChunk = 64 * 1024;
constexpr qint64 kDrainLimit = 64 * 1024; // the largest body read only to be dropped
constexpr int kCloseGraceMs = 5000;

/// RFC 9110 tchar: the characters of a token (method, header name).
bool isTchar(char c)
{
    const auto u = static_cast<unsigned char>(c);
    if ((u >= '0' && u <= '9') || (u >= 'a' && u <= 'z') || (u >= 'A' && u <= 'Z'))
        return true;
    return QByteArrayView("!#$%&'*+-.^_`|~").contains(c);
}

bool isToken(QByteArrayView text)
{
    return !text.isEmpty() && std::ranges::all_of(text, isTchar);
}

/// A field value's characters: VCHAR, obs-text, SP and HTAB (no CR, LF, NUL or other controls).
bool isFieldValue(QByteArrayView text)
{
    return std::ranges::all_of(text, [](char c) {
        const auto u = static_cast<unsigned char>(c);
        return u == '\t' || (u >= 0x20 && u != 0x7f);
    });
}

QByteArray trimOws(QByteArrayView text)
{
    qsizetype begin = 0;
    qsizetype end = text.size();
    while (begin < end && (text[begin] == ' ' || text[begin] == '\t'))
        ++begin;
    while (end > begin && (text[end - 1] == ' ' || text[end - 1] == '\t'))
        --end;
    return text.sliced(begin, end - begin).toByteArray();
}

int hexValue(char c)
{
    if (c >= '0' && c <= '9')
        return c - '0';
    if (c >= 'a' && c <= 'f')
        return c - 'a' + 10;
    if (c >= 'A' && c <= 'F')
        return c - 'A' + 10;
    return -1;
}

} // namespace

// ---- Headers

void Headers::set(const QByteArray &name, const QByteArray &value)
{
    remove(name);
    add(name, value);
}

void Headers::remove(QByteArrayView name)
{
    m_fields.removeIf([name](const Field &f) { return f.name.compare(name, Qt::CaseInsensitive) == 0; });
}

bool Headers::has(QByteArrayView name) const
{
    return count(name) > 0;
}

int Headers::count(QByteArrayView name) const
{
    return int(std::ranges::count_if(
            m_fields, [name](const Field &f) { return f.name.compare(name, Qt::CaseInsensitive) == 0; }));
}

QByteArray Headers::value(QByteArrayView name) const
{
    for (const Field &f : m_fields) {
        if (f.name.compare(name, Qt::CaseInsensitive) == 0)
            return f.value;
    }
    return {};
}

QList<QByteArray> Headers::tokens(QByteArrayView name) const
{
    QList<QByteArray> out;
    for (const Field &f : m_fields) {
        if (f.name.compare(name, Qt::CaseInsensitive) != 0)
            continue;
        for (const QByteArray &part : f.value.split(',')) {
            const QByteArray token = trimOws(part);
            if (!token.isEmpty())
                out.append(token);
        }
    }
    return out;
}

// ---- Responses

QByteArray reasonPhrase(int status)
{
    switch (status) {
    case 100: return "Continue"_ba;
    case 200: return "OK"_ba;
    case 201: return "Created"_ba;
    case 202: return "Accepted"_ba;
    case 204: return "No Content"_ba;
    case 302: return "Found"_ba;
    case 303: return "See Other"_ba;
    case 304: return "Not Modified"_ba;
    case 400: return "Bad Request"_ba;
    case 401: return "Unauthorized"_ba;
    case 403: return "Forbidden"_ba;
    case 404: return "Not Found"_ba;
    case 405: return "Method Not Allowed"_ba;
    case 406: return "Not Acceptable"_ba;
    case 408: return "Request Timeout"_ba;
    case 411: return "Length Required"_ba;
    case 413: return "Content Too Large"_ba;
    case 414: return "URI Too Long"_ba;
    case 415: return "Unsupported Media Type"_ba;
    case 417: return "Expectation Failed"_ba;
    case 429: return "Too Many Requests"_ba;
    case 431: return "Request Header Fields Too Large"_ba;
    case 500: return "Internal Server Error"_ba;
    case 501: return "Not Implemented"_ba;
    case 503: return "Service Unavailable"_ba;
    case 505: return "HTTP Version Not Supported"_ba;
    default: return "Unknown"_ba;
    }
}

Response Response::json(int status, const QJsonObject &object)
{
    Response r;
    r.status = status;
    r.headers.set("Content-Type"_ba, "application/json"_ba);
    r.body = QJsonDocument(object).toJson(QJsonDocument::Compact);
    return r;
}

Response Response::text(int status, const QByteArray &text)
{
    Response r;
    r.status = status;
    r.headers.set("Content-Type"_ba, "text/plain; charset=utf-8"_ba);
    r.body = text;
    if (!r.body.endsWith('\n'))
        r.body += '\n';
    return r;
}

Response Response::empty(int status)
{
    Response r;
    r.status = status;
    return r;
}

QByteArray Response::serialize(bool head) const
{
    QByteArray out = "HTTP/1.1 "_ba + QByteArray::number(status) + ' ' + reasonPhrase(status) + "\r\n";
    const bool bodyless = status < 200 || status == 204 || status == 304;
    for (const Headers::Field &f : headers.fields()) {
        if (f.name.compare("Content-Length", Qt::CaseInsensitive) == 0
            || f.name.compare("Connection", Qt::CaseInsensitive) == 0)
            continue;
        // A value never ends the header (response splitting): CR and LF go.
        QByteArray value = f.value;
        value.replace('\r', ' ').replace('\n', ' ');
        out += f.name + ": " + value + "\r\n";
    }
    if (!headers.has("Cache-Control"))
        out += "Cache-Control: no-store\r\n";
    if (!headers.has("X-Content-Type-Options"))
        out += "X-Content-Type-Options: nosniff\r\n";
    if (!bodyless)
        out += "Content-Length: " + QByteArray::number(body.size()) + "\r\n";
    if (close)
        out += "Connection: close\r\n";
    out += "\r\n";
    if (!head && !bodyless)
        out += body;
    return out;
}

// ---- Forms

std::optional<QList<std::pair<QString, QString>>> parseForm(QByteArrayView data)
{
    const auto decode = [](QByteArrayView part) -> std::optional<QString> {
        QByteArray bytes;
        bytes.reserve(part.size());
        for (qsizetype i = 0; i < part.size(); ++i) {
            const char c = part[i];
            if (c == '+') {
                bytes += ' ';
            } else if (c == '%') {
                if (i + 2 >= part.size())
                    return std::nullopt;
                const int hi = hexValue(part[i + 1]);
                const int lo = hexValue(part[i + 2]);
                if (hi < 0 || lo < 0)
                    return std::nullopt;
                bytes += char(hi * 16 + lo);
                i += 2;
            } else {
                bytes += c;
            }
        }
        QStringDecoder decoder(QStringDecoder::Utf8, QStringDecoder::Flag::Stateless);
        QString text = decoder(bytes);
        if (decoder.hasError())
            return std::nullopt;
        if (std::ranges::any_of(text, [](QChar ch) { return ch.unicode() < 0x20 || ch.unicode() == 0x7f; }))
            return std::nullopt;
        return text;
    };
    QList<std::pair<QString, QString>> out;
    const QList<QByteArray> parts = data.toByteArray().split('&');
    for (const QByteArray &part : parts) {
        if (part.isEmpty())
            continue;
        const qsizetype eq = part.indexOf('=');
        const auto name = decode(eq < 0 ? QByteArrayView(part) : QByteArrayView(part).first(eq));
        const auto value = decode(eq < 0 ? QByteArrayView() : QByteArrayView(part).sliced(eq + 1));
        if (!name || !value)
            return std::nullopt;
        out.append({*name, *value});
    }
    return out;
}

QByteArray formEncode(const QString &value)
{
    return QUrl::toPercentEncoding(value);
}

// ---- The parser

RequestParser::RequestParser(ParserLimits limits) : m_limits(limits) { }

RequestParser::State RequestParser::fail(int status, const QByteArray &text)
{
    m_state = State::Failed;
    m_errorStatus = status;
    m_errorText = text;
    return m_state;
}

std::optional<QByteArray> RequestParser::takeLine(int limit, bool *tooLong)
{
    const qsizetype end = m_buffer.indexOf('\n', m_pos);
    if (end < 0) {
        *tooLong = m_buffer.size() - m_pos > limit;
        return std::nullopt;
    }
    qsizetype length = end - m_pos;
    if (length > limit + 1) {
        *tooLong = true;
        return std::nullopt;
    }
    if (length > 0 && m_buffer.at(end - 1) == '\r')
        --length;
    QByteArray line = m_buffer.mid(m_pos, length);
    m_pos = end + 1;
    *tooLong = false;
    return line;
}

RequestParser::State RequestParser::feed(QByteArrayView data)
{
    if (m_state == State::Failed)
        return m_state;
    m_buffer.append(data);
    return parse();
}

void RequestParser::setBodyLimit(qint64 bytes)
{
    m_bodyLimit = std::max<qint64>(0, bytes);
    if (m_state == State::HeadersDone && !m_chunked && m_contentLength > m_bodyLimit)
        fail(413, "The request body is too large"_ba);
}

RequestParser::State RequestParser::afterHeaders()
{
    const Headers &h = m_request.headers;
    const int hosts = h.count("Host");
    if (hosts > 1 || (m_request.minorVersion == 1 && hosts == 0))
        return fail(400, "One Host header field is required"_ba);
    if (h.has("Transfer-Encoding")) {
        if (m_request.minorVersion == 0)
            return fail(400, "Transfer-Encoding in an HTTP/1.0 request"_ba);
        if (h.has("Content-Length"))
            return fail(400, "Both Content-Length and Transfer-Encoding"_ba);
        const QList<QByteArray> codings = h.tokens("Transfer-Encoding");
        if (codings.isEmpty() || codings.last().compare("chunked", Qt::CaseInsensitive) != 0)
            return fail(400, "The last transfer coding must be chunked"_ba);
        if (codings.size() > 1)
            return fail(501, "Only the chunked transfer coding is supported"_ba);
        m_chunked = true;
    } else if (h.has("Content-Length")) {
        if (h.count("Content-Length") > 1)
            return fail(400, "More than one Content-Length"_ba);
        const QByteArray value = h.value("Content-Length");
        if (value.isEmpty() || value.size() > 15
            || !std::ranges::all_of(value, [](char c) { return c >= '0' && c <= '9'; }))
            return fail(400, "Content-Length is not a number"_ba);
        m_contentLength = value.toLongLong();
    }
    if (h.has("Expect")) {
        if (h.value("Expect").compare("100-continue", Qt::CaseInsensitive) != 0)
            return fail(417, "Only Expect: 100-continue is supported"_ba);
        m_expectContinue = true;
    }
    m_phase = m_chunked ? Phase::ChunkSize : (m_contentLength > 0 ? Phase::Body : Phase::Finished);
    m_state = State::HeadersDone;
    return m_state;
}

RequestParser::State RequestParser::parse()
{
    if (m_state == State::Failed || m_state == State::Done)
        return m_state;
    for (;;) {
        bool tooLong = false;
        switch (m_phase) {
        case Phase::RequestLine: {
            const auto line = takeLine(m_limits.maxRequestLine, &tooLong);
            if (!line) {
                if (tooLong)
                    return fail(414, "The request line is too long"_ba);
                return m_state = State::Incomplete;
            }
            if (line->isEmpty()) {
                // RFC 9112 2.2: empty lines before a request line are ignored (a few).
                if (++m_emptyLines > 2)
                    return fail(400, "Empty lines instead of a request"_ba);
                continue;
            }
            const QList<QByteArray> parts = line->split(' ');
            if (parts.size() != 3)
                return fail(400, "Malformed request line"_ba);
            const QByteArray &method = parts.at(0);
            const QByteArray &target = parts.at(1);
            const QByteArray &version = parts.at(2);
            if (!isToken(method) || method.size() > 32)
                return fail(400, "Malformed method"_ba);
            if (target.isEmpty() || !std::ranges::all_of(target, [](char c) {
                    const auto u = static_cast<unsigned char>(c);
                    return u > 0x20 && u < 0x7f;
                }))
                return fail(400, "Malformed request target"_ba);
            if (version == "HTTP/1.1")
                m_request.minorVersion = 1;
            else if (version == "HTTP/1.0")
                m_request.minorVersion = 0;
            else if (version.size() == 8 && version.startsWith("HTTP/") && version.at(6) == '.'
                     && std::isdigit(static_cast<unsigned char>(version.at(5)))
                     && std::isdigit(static_cast<unsigned char>(version.at(7))))
                return fail(505, "Only HTTP/1.0 and HTTP/1.1"_ba);
            else
                return fail(400, "Malformed HTTP version"_ba);
            QByteArray pathAndQuery;
            if (target.startsWith('/')) {
                pathAndQuery = target;
            } else if (target == "*" && method == "OPTIONS") {
                pathAndQuery = "*"_ba;
            } else if (target.startsWith("http://") || target.startsWith("https://")) {
                // The absolute form (RFC 9112 3.2.2): its path counts.
                const qsizetype authority = target.indexOf("://") + 3;
                const qsizetype pathStart = target.indexOf('/', authority);
                const qsizetype queryStart = target.indexOf('?', authority);
                const qsizetype start = pathStart < 0 ? queryStart
                                        : queryStart < 0 ? pathStart
                                                         : std::min(pathStart, queryStart);
                pathAndQuery = start < 0 ? "/"_ba : target.mid(start);
                if (pathAndQuery.startsWith('?'))
                    pathAndQuery.prepend('/');
            } else {
                return fail(400, "Unsupported request target"_ba);
            }
            if (pathAndQuery.contains('#'))
                return fail(400, "A fragment in the request target"_ba);
            const qsizetype q = pathAndQuery.indexOf('?');
            m_request.method = method;
            m_request.target = target;
            m_request.path = q < 0 ? pathAndQuery : pathAndQuery.left(q);
            m_request.query = q < 0 ? QByteArray() : pathAndQuery.mid(q + 1);
            m_phase = Phase::Headers;
            m_state = State::Incomplete;
            continue;
        }
        case Phase::Headers:
        case Phase::Trailers: {
            const int room = m_limits.maxHeaderBytes - m_headerBytes;
            const auto line = takeLine(std::max(room, 0), &tooLong);
            if (!line) {
                if (tooLong)
                    return fail(431, "The header fields are too large"_ba);
                return m_state = State::Incomplete;
            }
            m_headerBytes += int(line->size()) + 2;
            if (m_headerBytes > m_limits.maxHeaderBytes)
                return fail(431, "The header fields are too large"_ba);
            if (line->isEmpty()) {
                if (m_phase == Phase::Trailers) {
                    m_phase = Phase::Finished;
                    continue;
                }
                return afterHeaders();
            }
            if (line->front() == ' ' || line->front() == '\t')
                return fail(400, "Folded header lines are not allowed"_ba);
            const qsizetype colon = line->indexOf(':');
            if (colon <= 0)
                return fail(400, "Malformed header line"_ba);
            const QByteArray name = line->left(colon);
            if (!isToken(name))
                return fail(400, "Malformed header name"_ba);
            const QByteArray value = trimOws(QByteArrayView(*line).sliced(colon + 1));
            if (!isFieldValue(value))
                return fail(400, "Control characters in a header value"_ba);
            if (m_phase == Phase::Headers) {
                if (m_request.headers.fields().size() >= m_limits.maxHeaders)
                    return fail(431, "Too many header fields"_ba);
                m_request.headers.add(name, value);
            }
            continue;
        }
        case Phase::Body: {
            if (m_bodyLimit < 0)
                return m_state; // HeadersDone: the server decides first
            if (m_buffer.size() - m_pos < m_contentLength)
                return m_state = State::Incomplete;
            m_request.body = m_buffer.mid(m_pos, m_contentLength);
            m_pos += m_contentLength;
            m_phase = Phase::Finished;
            continue;
        }
        case Phase::ChunkSize: {
            if (m_bodyLimit < 0)
                return m_state;
            const auto line = takeLine(m_limits.maxChunkLine, &tooLong);
            if (!line) {
                if (tooLong)
                    return fail(400, "Malformed chunk"_ba);
                return m_state = State::Incomplete;
            }
            // chunk-size [ chunk-ext ]: hex digits, then extensions that are ignored.
            qsizetype digits = 0;
            while (digits < line->size() && hexValue(line->at(digits)) >= 0)
                ++digits;
            if (digits == 0 || digits > 15)
                return fail(400, "Malformed chunk size"_ba);
            const QByteArray rest = trimOws(QByteArrayView(*line).sliced(digits));
            if (!rest.isEmpty() && (!rest.startsWith(';') || !isFieldValue(rest)))
                return fail(400, "Malformed chunk extension"_ba);
            bool ok = false;
            const qint64 size = line->left(digits).toLongLong(&ok, 16);
            if (!ok)
                return fail(400, "Malformed chunk size"_ba);
            if (size == 0) {
                m_phase = Phase::Trailers;
                continue;
            }
            if (m_request.body.size() + size > m_bodyLimit)
                return fail(413, "The request body is too large"_ba);
            m_chunkLeft = size;
            m_phase = Phase::ChunkData;
            continue;
        }
        case Phase::ChunkData: {
            const qint64 available = std::min<qint64>(m_buffer.size() - m_pos, m_chunkLeft);
            m_request.body.append(QByteArrayView(m_buffer).sliced(m_pos, available));
            m_pos += available;
            m_chunkLeft -= available;
            if (m_chunkLeft > 0)
                return m_state = State::Incomplete;
            m_phase = Phase::ChunkDataEnd;
            continue;
        }
        case Phase::ChunkDataEnd: {
            const auto line = takeLine(2, &tooLong);
            if (!line) {
                if (tooLong)
                    return fail(400, "Malformed chunk"_ba);
                return m_state = State::Incomplete;
            }
            if (!line->isEmpty())
                return fail(400, "Malformed chunk"_ba);
            m_phase = Phase::ChunkSize;
            continue;
        }
        case Phase::Finished:
            if (m_bodyLimit < 0)
                return m_state; // HeadersDone, no body: the server still decides first
            return m_state = State::Done;
        }
    }
}

void RequestParser::compact()
{
    if (m_pos > 0) {
        m_buffer.remove(0, m_pos);
        m_pos = 0;
    }
}

void RequestParser::next()
{
    compact();
    m_phase = Phase::RequestLine;
    m_state = State::Incomplete;
    m_request = Request{};
    m_headerBytes = 0;
    m_emptyLines = 0;
    m_contentLength = 0;
    m_bodyLimit = -1;
    m_chunkLeft = 0;
    m_chunked = false;
    m_expectContinue = false;
    m_errorStatus = 0;
    m_errorText.clear();
}

// ---- Replies

void Reply::send(Response response) const
{
    if (m_extra) {
        for (const Headers::Field &f : m_extra->fields()) {
            if (!response.headers.has(f.name))
                response.headers.add(f.name, f.value);
        }
    }
    if (m_connection)
        m_connection->deliver(m_serial, std::move(response));
}

Reply Reply::with(const Headers &extra) const
{
    Reply copy = *this;
    auto merged = std::make_shared<Headers>(m_extra ? *m_extra : Headers());
    for (const Headers::Field &f : extra.fields())
        merged->add(f.name, f.value);
    copy.m_extra = merged;
    return copy;
}

bool Reply::alive() const
{
    return m_connection && m_connection->waitingFor(m_serial);
}

// ---- Connections

Connection::Connection(Server *server, QTcpSocket *socket)
    : QObject(server), m_server(server), m_socket(socket), m_timer(new QTimer(this)),
      m_parser(server->limits().parser)
{
    m_socket->setParent(this);
    m_socket->setReadBufferSize(kReadChunk);
    m_peerKey = socket->peerAddress().toString();
    m_timer->setSingleShot(true);
    connect(m_timer, &QTimer::timeout, this, &Connection::timedOut);
    connect(m_socket, &QTcpSocket::readyRead, this, &Connection::readSome);
    connect(m_socket, &QTcpSocket::disconnected, this, &Connection::finish);
    connect(m_socket, &QAbstractSocket::errorOccurred, this, [this](QAbstractSocket::SocketError) {
        if (m_socket->state() != QAbstractSocket::ConnectedState)
            finish();
    });
    armTimer();
}

Connection::~Connection() = default;

void Connection::start()
{
    // Bytes may have come with the TLS handshake already, before readyRead was connected.
    QMetaObject::invokeMethod(this, &Connection::readSome, Qt::QueuedConnection);
}

void Connection::finish()
{
    if (m_finished)
        return;
    m_finished = true;
    m_timer->stop();
    m_server->dropped(this);
    deleteLater();
}

void Connection::armTimer()
{
    if (m_closing)
        return;
    if (m_inFlight) {
        m_timer->stop();
        return;
    }
    const ServerLimits &limits = m_server->limits();
    if (m_headersSeen) {
        m_timer->start(limits.bodyTimeoutMs);
    } else if (m_started.isValid()) {
        m_timer->start(int(std::max<qint64>(0, limits.headerTimeoutMs - m_started.elapsed())));
    } else {
        m_timer->start(limits.idleTimeoutMs);
    }
}

void Connection::timedOut()
{
    if (m_inFlight || m_closing)
        return;
    if (!m_headersSeen && !m_started.isValid()) {
        closeSoon(); // keep-alive ended
        return;
    }
    Response r = Response::text(408, "The request took too long"_ba);
    r.close = true;
    write(r);
    closeSoon();
}

void Connection::readSome()
{
    while (!m_inFlight && !m_closing && m_socket->bytesAvailable() > 0) {
        const QByteArray chunk = m_socket->read(kReadChunk);
        if (chunk.isEmpty())
            break;
        if (!m_started.isValid())
            m_started.start();
        m_parser.feed(chunk);
        process();
    }
    armTimer();
}

void Connection::process()
{
    while (!m_inFlight && !m_closing) {
        const RequestParser::State state = m_parser.parse();
        switch (state) {
        case RequestParser::State::Incomplete:
            armTimer();
            return;
        case RequestParser::State::Failed: {
            Response r = Response::text(m_parser.errorStatus(), m_parser.errorText());
            r.close = true;
            m_logMethod = m_parser.request().method;
            m_logPath = m_parser.request().path;
            write(r);
            closeSoon();
            return;
        }
        case RequestParser::State::HeadersDone: {
            if (m_headersSeen) {
                armTimer();
                return; // waiting for the body
            }
            m_headersSeen = true;
            Request &head = m_parser.request();
            head.peer = m_socket->peerAddress();
            head.secure = m_server->tls();
            m_logMethod = head.method;
            m_logPath = head.path;
            m_head = head.method == "HEAD";
            const QList<QByteArray> connection = head.headers.tokens("Connection");
            m_keepAlive = head.minorVersion == 1 && !std::ranges::any_of(connection, [](const QByteArray &t) {
                return t.compare("close", Qt::CaseInsensitive) == 0;
            });
            qint64 maxBody = 0;
            std::optional<Response> early;
            if (m_server->onHeaders)
                early = m_server->onHeaders(head, &maxBody);
            if (early && !m_parser.expectsContinue() && m_parser.contentLength() > 0
                && m_parser.contentLength() <= kDrainLimit) {
                // Answered from the header fields alone; a small body is read and dropped so that
                // the connection stays usable (a larger one is never read: the connection closes).
                m_early = std::move(early);
                m_parser.setBodyLimit(m_parser.contentLength());
                armTimer();
                continue;
            }
            if (early) {
                if (m_parser.hasBody())
                    early->close = true;
                ++m_handled;
                if (!m_keepAlive || m_handled >= m_server->limits().maxRequestsPerConnection)
                    early->close = true;
                write(*early);
                if (early->close) {
                    closeSoon();
                    return;
                }
                m_parser.next();
                m_headersSeen = false;
                m_started.invalidate();
                if (m_parser.hasBufferedBytes())
                    m_started.start();
                continue;
            }
            if (m_parser.hasBody() && m_parser.expectsContinue())
                m_socket->write("HTTP/1.1 100 Continue\r\n\r\n");
            m_parser.setBodyLimit(maxBody);
            armTimer();
            continue;
        }
        case RequestParser::State::Done: {
            m_inFlight = true;
            m_timer->stop();
            ++m_serial;
            const Reply reply(this, m_serial);
            if (m_early) {
                reply.send(*std::exchange(m_early, std::nullopt));
                return;
            }
            if (m_server->onRequest)
                m_server->onRequest(m_parser.request(), reply);
            else
                reply.send(Response::text(404, "Not found"_ba));
            return;
        }
        }
    }
}

void Connection::deliver(quint64 serial, Response response)
{
    if (!waitingFor(serial) || m_closing)
        return;
    m_inFlight = false;
    ++m_handled;
    if (!m_keepAlive || m_handled >= m_server->limits().maxRequestsPerConnection)
        response.close = true;
    write(response);
    if (response.close) {
        closeSoon();
        return;
    }
    m_parser.next();
    m_headersSeen = false;
    m_started.invalidate();
    if (m_parser.hasBufferedBytes())
        m_started.start();
    process();
    readSome();
}

void Connection::write(const Response &response)
{
    m_socket->write(response.serialize(m_head));
    if (!m_logMethod.isEmpty() || response.status >= 400) {
        m_server->log(u"%1 %2 %3 %4 %5 ms"_s.arg(m_peerKey, QString::fromLatin1(m_logMethod),
                                                 QString::fromLatin1(m_logPath.left(200)),
                                                 QString::number(response.status),
                                                 QString::number(m_started.isValid() ? m_started.elapsed() : 0)));
    }
    m_logMethod.clear();
    m_logPath.clear();
}

void Connection::refuse(int status)
{
    Response r = Response::text(status, "The server is busy: too many connections"_ba);
    r.close = true;
    m_socket->write(r.serialize());
    closeSoon();
}

void Connection::closeSoon()
{
    if (m_closing)
        return;
    m_closing = true;
    m_timer->stop();
    m_socket->disconnectFromHost();
    if (m_socket->state() == QAbstractSocket::UnconnectedState) {
        finish();
        return;
    }
    QTimer::singleShot(kCloseGraceMs, this, [this] {
        m_socket->abort();
        finish();
    });
}

// ---- The server

Server::Server(ServerLimits limits, QObject *parent) : QObject(parent), m_limits(limits) { }

Server::~Server()
{
    // Connections are children; they must not call back into a half-destroyed server.
    for (Connection *c : std::as_const(m_connections))
        c->disconnect();
    m_connections.clear();
}

#if QT_CONFIG(ssl)
void Server::setTlsConfiguration(const QSslConfiguration &configuration)
{
    m_tls = true;
    m_tlsConfiguration = configuration;
    if (auto *ssl = qobject_cast<QSslServer *>(m_server))
        ssl->setSslConfiguration(configuration);
}
#endif

bool Server::listen(const QHostAddress &address, quint16 port, QString *error)
{
#if QT_CONFIG(ssl)
    if (m_tls) {
        auto *ssl = new QSslServer(this);
        ssl->setSslConfiguration(m_tlsConfiguration);
        ssl->setHandshakeTimeout(m_limits.handshakeTimeoutMs);
        connect(ssl, &QSslServer::errorOccurred, this, [this](QSslSocket *socket, QAbstractSocket::SocketError) {
            log(u"%1 TLS: %2"_s.arg(socket->peerAddress().toString(), socket->errorString()));
        });
        m_server = ssl;
    }
#endif
    if (!m_server)
        m_server = new QTcpServer(this);
    m_server->setMaxPendingConnections(m_limits.maxConnections);
    connect(m_server, &QTcpServer::pendingConnectionAvailable, this, &Server::accept);
    if (!m_server->listen(address, port)) {
        if (error)
            *error = m_server->errorString();
        return false;
    }
    return true;
}

quint16 Server::port() const
{
    return m_server ? m_server->serverPort() : 0;
}

QHostAddress Server::address() const
{
    return m_server ? m_server->serverAddress() : QHostAddress();
}

void Server::accept()
{
    while (m_server && m_server->hasPendingConnections()) {
        QTcpSocket *socket = m_server->nextPendingConnection();
        if (!socket)
            break;
        auto *connection = new Connection(this, socket);
        const QString peer = connection->peerKey();
        if (m_connections.size() >= m_limits.maxConnections || m_perPeer.value(peer) >= m_limits.maxConnectionsPerPeer) {
            m_connections.append(connection);
            ++m_perPeer[peer];
            log(u"%1 refused: too many connections"_s.arg(peer));
            connection->refuse(503);
            continue;
        }
        m_connections.append(connection);
        ++m_perPeer[peer];
        connection->start();
    }
}

void Server::dropped(Connection *connection)
{
    if (m_connections.removeOne(connection)) {
        const QString peer = connection->peerKey();
        if (--m_perPeer[peer] <= 0)
            m_perPeer.remove(peer);
    }
}

void Server::log(const QString &line) const
{
    if (onLog)
        onLog(line);
}

// ---- Helpers

QString originOf(const QUrl &url)
{
    const QString scheme = url.scheme().toLower();
    QString origin = scheme + u"://"_s + url.host(QUrl::FullyEncoded).toLower();
    if (url.host().contains(u':'))
        origin = scheme + u"://["_s + url.host(QUrl::FullyEncoded).toLower() + u']';
    const int port = url.port();
    if (port >= 0 && !((scheme == u"https" && port == 443) || (scheme == u"http" && port == 80)))
        origin += u':' + QString::number(port);
    return origin;
}

QString preferredLanguage(const Request &request)
{
    double polish = -1;
    double english = -1;
    int order = 0;
    int polishOrder = 0;
    int englishOrder = 0;
    for (const QByteArray &item : request.headers.tokens("Accept-Language")) {
        ++order;
        const QList<QByteArray> parts = item.split(';');
        const QByteArray tag = parts.value(0).trimmed().toLower();
        double q = 1;
        for (qsizetype i = 1; i < parts.size(); ++i) {
            const QByteArray p = parts.at(i).trimmed();
            if (p.startsWith("q=")) {
                bool ok = false;
                q = p.mid(2).toDouble(&ok);
                if (!ok)
                    q = 0;
            }
        }
        const QByteArray primary = tag.split('-').value(0);
        if (primary == "pl" && q > polish) {
            polish = q;
            polishOrder = order;
        } else if (primary == "en" && q > english) {
            english = q;
            englishOrder = order;
        }
    }
    if (polish > 0 && (polish > english || (polish == english && polishOrder < englishOrder)))
        return u"pl"_s;
    return u"en"_s;
}

QString htmlEscape(const QString &text)
{
    QString out;
    out.reserve(text.size() + 16);
    for (const QChar c : text) {
        switch (c.unicode()) {
        case '&': out += u"&amp;"_s; break;
        case '<': out += u"&lt;"_s; break;
        case '>': out += u"&gt;"_s; break;
        case '"': out += u"&quot;"_s; break;
        case '\'': out += u"&#39;"_s; break;
        default:
            if (c.unicode() >= 0x20 && c.unicode() != 0x7f && !(c.unicode() >= 0x80 && c.unicode() < 0xa0))
                out += c;
        }
    }
    return out;
}

} // namespace brulion::cli::http
