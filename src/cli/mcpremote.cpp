// `brulion-cli mcp serve` and its companions `mcp password`, `mcp clients`, `mcp revoke`
// (mcpremote.h, docs/CLI.md "Remote MCP server").
#include "mcpremote.h"

#include "commands_p.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QTimer>

#if QT_CONFIG(ssl)
#include <QSslCertificate>
#include <QSslKey>
#include <QSslSocket>
#endif

#include <algorithm>
#include <cstdio>

#ifdef Q_OS_WIN
#include <io.h>
#include <windows.h>
#else
#include <termios.h>
#include <unistd.h>
#endif

using namespace Qt::StringLiterals;

namespace brulion::cli {

namespace {

constexpr auto kAuthFileName = "mcp-remote.json";

/// One line on standard error, with the time (the server's log).
void logLine(const QString &line)
{
    const QByteArray bytes =
            (QDateTime::currentDateTime().toString(u"yyyy-MM-dd HH:mm:ss"_s) + u' ' + line + u'\n').toUtf8();
    std::fwrite(bytes.constData(), 1, size_t(bytes.size()), stderr);
    std::fflush(stderr);
}

QStringList listArgument(const QJsonObject &args, const QString &name)
{
    QStringList out;
    for (const QString &part : args.value(name).toString().split(u',', Qt::SkipEmptyParts)) {
        if (const QString t = part.trimmed(); !t.isEmpty())
            out.append(t);
    }
    return out;
}

/// The value of a Host header or an authority without the scheme's default port, lower case.
QString normalizedHost(const QString &host, bool https)
{
    QString h = host.trimmed().toLower();
    const QString defaultPort = https ? u":443"_s : u":80"_s;
    if (h.endsWith(defaultPort))
        h.chop(defaultPort.size());
    return h;
}

http::Response jsonRpcError(int status, const QJsonValue &id, int code, const QString &message,
                            const QJsonValue &data = QJsonValue())
{
    return http::Response::json(status, McpDispatcher::errorResponse(id, code, message, data));
}

/// The value of an Mcp-Name or Mcp-Param header: "=?base64?…?=" decoded (UTF-8), else as sent.
std::optional<QString> headerValue(const QByteArray &raw)
{
    if (raw.startsWith("=?base64?") && raw.endsWith("?=") && raw.size() >= 11) {
        const auto decoded = QByteArray::fromBase64Encoding(raw.mid(9, raw.size() - 11),
                                                            QByteArray::AbortOnBase64DecodingErrors);
        if (!decoded)
            return std::nullopt;
        return QString::fromUtf8(decoded.decoded);
    }
    return QString::fromLatin1(raw);
}

} // namespace

// ---- The server

RemoteMcpServer::RemoteMcpServer(Context &context, RemoteOptions options, QObject *parent)
    : QObject(parent), m_ctx(context), m_options(std::move(options))
{
}

RemoteMcpServer::~RemoteMcpServer()
{
    if (m_auth)
        m_auth->flushLastUsed();
}

void RemoteMcpServer::say(const QString &line) const
{
    if (log)
        log(line);
}

quint16 RemoteMcpServer::port() const
{
    return m_http ? m_http->port() : 0;
}

QString RemoteMcpServer::endpoint() const
{
    return m_endpoint;
}

QString RemoteMcpServer::issuer() const
{
    return m_issuer;
}

QString RemoteMcpServer::route(const QByteArray &path) const
{
    const QString p = QString::fromLatin1(path);
    if (!m_basePath.isEmpty() && p.startsWith(m_basePath + u'/'))
        return p.mid(m_basePath.size());
    if (!m_basePath.isEmpty() && p == m_basePath)
        return u"/"_s;
    return p;
}

bool RemoteMcpServer::loadTls(QString *error)
{
#if QT_CONFIG(ssl)
    if (!QSslSocket::supportsSsl()) {
        *error = tr("This build of Qt has no TLS backend: serve plain HTTP behind a reverse proxy");
        return false;
    }
    QFile certFile(m_options.tlsCertificate);
    QFile keyFile(m_options.tlsKey);
    if (!certFile.open(QIODevice::ReadOnly) || !keyFile.open(QIODevice::ReadOnly)) {
        *error = tr("Cannot read the certificate or its key: %1")
                         .arg(certFile.isOpen() ? keyFile.errorString() : certFile.errorString());
        return false;
    }
    const QList<QSslCertificate> chain = QSslCertificate::fromData(certFile.readAll(), QSsl::Pem);
    const QByteArray keyBytes = keyFile.readAll();
    QSslKey key;
    for (const QSsl::KeyAlgorithm algorithm : {QSsl::Rsa, QSsl::Ec, QSsl::Dsa}) {
        key = QSslKey(keyBytes, algorithm, QSsl::Pem, QSsl::PrivateKey);
        if (!key.isNull())
            break;
    }
    if (chain.isEmpty() || chain.first().isNull()) {
        *error = tr("%1 holds no PEM certificate").arg(m_options.tlsCertificate);
        return false;
    }
    if (key.isNull()) {
        *error = tr("%1 holds no PEM private key (RSA or EC) without a passphrase").arg(m_options.tlsKey);
        return false;
    }
    QSslConfiguration configuration = QSslConfiguration::defaultConfiguration();
    configuration.setLocalCertificateChain(chain);
    configuration.setPrivateKey(key);
    configuration.setProtocol(QSsl::SecureProtocols);
    configuration.setPeerVerifyMode(QSslSocket::VerifyNone);
    configuration.setAllowedNextProtocols({QByteArrayLiteral("http/1.1")});
    m_http->setTlsConfiguration(configuration);
    m_certModified = QFileInfo(m_options.tlsCertificate).lastModified(QTimeZone::UTC);
    m_keyModified = QFileInfo(m_options.tlsKey).lastModified(QTimeZone::UTC);
    return true;
#else
    *error = tr("This build has no TLS: serve plain HTTP behind a reverse proxy");
    return false;
#endif
}

bool RemoteMcpServer::start(QString *error)
{
    // The public URL: the issuer, and the MCP endpoint under it.
    const bool tls = !m_options.tlsCertificate.isEmpty() || !m_options.tlsKey.isEmpty();
    if (tls && (m_options.tlsCertificate.isEmpty() || m_options.tlsKey.isEmpty())) {
        *error = tr("--tls-cert and --tls-key go together");
        return false;
    }
    QString publicUrl = m_options.publicUrl.trimmed();
    const bool anyAddress = m_options.address == QHostAddress::Any || m_options.address == QHostAddress::AnyIPv4
                            || m_options.address == QHostAddress::AnyIPv6;
    if (publicUrl.isEmpty()) {
        if (anyAddress) {
            *error = tr("--public-url is needed when the server listens on all addresses");
            return false;
        }
    }
    m_http = std::make_unique<http::Server>(m_options.limits);
    m_http->onLog = [this](const QString &line) { say(line); };
    m_http->onHeaders = [this](const http::Request &head, qint64 *maxBody) { return onHeaders(head, maxBody); };
    m_http->onRequest = [this](const http::Request &request, const http::Reply &reply) { onRequest(request, reply); };
    if (tls && !loadTls(error))
        return false;
    if (!m_http->listen(m_options.address, m_options.port, error)) {
        *error = tr("Cannot listen on %1 port %2: %3")
                         .arg(m_options.address.toString(), QString::number(m_options.port), *error);
        return false;
    }
    const quint16 port = m_http->port();
    if (publicUrl.isEmpty()) {
        const QString host = m_options.address.protocol() == QAbstractSocket::IPv6Protocol
                                     ? u'[' + m_options.address.toString() + u']'
                                     : m_options.address.toString();
        publicUrl = (tls ? u"https://"_s : u"http://"_s) + host + u':' + QString::number(port);
    }
    const QUrl url(publicUrl, QUrl::StrictMode);
    const bool loopbackHost = url.host() == u"localhost" || QHostAddress(url.host()).isLoopback();
    if (!url.isValid() || url.host().isEmpty() || !url.userInfo().isEmpty() || url.hasQuery() || url.hasFragment()
        || (url.scheme() != u"https" && !(url.scheme() == u"http" && loopbackHost))) {
        *error = tr("--public-url must be an https:// address (http:// only on this computer): %1").arg(publicUrl);
        return false;
    }
    QString path = url.path(QUrl::FullyEncoded);
    while (path.endsWith(u'/'))
        path.chop(1);
    if (path.endsWith(u"/mcp"))
        path.chop(4);
    m_basePath = path;
    QUrl base = url;
    base.setPath(path.isEmpty() ? QString() : path);
    m_issuer = base.toString(QUrl::FullyEncoded);
    while (m_issuer.endsWith(u'/'))
        m_issuer.chop(1);
    m_endpoint = m_issuer + u"/mcp"_s;

    // Who may call: the Host names (DNS rebinding) and the browser origins (CORS).
    const bool https = url.scheme() == u"https";
    m_hosts = {normalizedHost(url.authority(QUrl::FullyEncoded), https)};
    for (const QString &loopback : {u"localhost"_s, u"127.0.0.1"_s, u"[::1]"_s})
        m_hosts.append(loopback + u':' + QString::number(port));
    if (!anyAddress) {
        const QString a = m_options.address.protocol() == QAbstractSocket::IPv6Protocol
                                  ? u'[' + m_options.address.toString() + u']'
                                  : m_options.address.toString();
        m_hosts.append(a.toLower() + u':' + QString::number(port));
    }
    for (const QString &h : std::as_const(m_options.allowedHosts))
        m_hosts.append(normalizedHost(h, https));
    m_origins = {http::originOf(url)};
    for (const QString &o : std::as_const(m_options.allowedOrigins))
        m_origins.append(http::originOf(QUrl(o)));

    m_store = std::make_unique<oauth::Store>(m_options.authFile);
    QString readError;
    if (!m_store->refresh(&readError)) {
        *error = tr("Cannot read %1: %2").arg(m_options.authFile, readError);
        return false;
    }
    if (m_options.passwordHash.isEmpty() && m_store->state().password.isEmpty()) {
        *error = tr("No password is set: run %1 mcp password first (or give --password-file)").arg(m_ctx.program);
        return false;
    }
    oauth::ServerConfig config = m_options.auth;
    config.issuer = m_issuer;
    config.basePath = m_basePath;
    config.resource = m_endpoint;
    config.notebookName = QFileInfo(m_ctx.vaultRoot()).fileName();
    config.allowWrite = !m_options.readOnly;
    config.allowDestructive = m_options.allowDestructive && !m_options.readOnly;
    config.passwordHash = m_options.passwordHash;
    m_auth = std::make_unique<oauth::AuthServer>(config, m_store.get());
    m_auth->log = [this](const QString &line) { say(line); };
    m_dispatcher = std::make_unique<McpDispatcher>(m_ctx);

    m_maintenance = new QTimer(this);
    connect(m_maintenance, &QTimer::timeout, this, &RemoteMcpServer::maintain);
    m_maintenance->start(m_options.maintenanceIntervalMs);
    return true;
}

void RemoteMcpServer::maintain()
{
    if (m_auth)
        m_auth->flushLastUsed();
    pruneSessions();
    if (!m_http || !m_http->tls())
        return;
    // A renewed certificate (certbot, ACME): new connections get it; on a broken pair (one file
    // written, the other not yet) the old one stays until the next look.
    const QDateTime cert = QFileInfo(m_options.tlsCertificate).lastModified(QTimeZone::UTC);
    const QDateTime key = QFileInfo(m_options.tlsKey).lastModified(QTimeZone::UTC);
    if (cert == m_certModified && key == m_keyModified)
        return;
    QString error;
    if (loadTls(&error))
        say(u"TLS certificate reloaded"_s);
    else
        say(u"TLS certificate not reloaded: %1"_s.arg(error));
}

void RemoteMcpServer::pruneSessions()
{
    const QDateTime now = QDateTime::currentDateTimeUtc();
    for (auto it = m_sessions.begin(); it != m_sessions.end();) {
        if (it->lastUsed.secsTo(now) > m_options.sessionIdleSeconds)
            it = m_sessions.erase(it);
        else
            ++it;
    }
}

bool RemoteMcpServer::hostAllowed(const QByteArray &host) const
{
    if (host.isEmpty())
        return true; // HTTP/1.0 (the parser requires Host for 1.1)
    const bool https = m_issuer.startsWith(u"https://");
    return m_hosts.contains(normalizedHost(QString::fromLatin1(host), https));
}

bool RemoteMcpServer::originAllowed(const QByteArray &origin) const
{
    if (origin.isEmpty())
        return true;
    if (origin == "null")
        return false;
    return m_origins.contains(http::originOf(QUrl(QString::fromLatin1(origin))));
}

http::Headers RemoteMcpServer::corsHeaders(const QByteArray &origin) const
{
    http::Headers h;
    if (origin.isEmpty() || !originAllowed(origin))
        return h;
    h.add("Access-Control-Allow-Origin"_ba, origin);
    h.add("Access-Control-Expose-Headers"_ba, "Mcp-Session-Id, MCP-Protocol-Version, WWW-Authenticate"_ba);
    h.add("Vary"_ba, "Origin"_ba);
    return h;
}

std::optional<http::Response> RemoteMcpServer::onHeaders(const http::Request &head, qint64 *maxBody)
{
    *maxBody = 0;
    if (!hostAllowed(head.headers.value("Host"))) {
        say(u"%1 refused: Host %2"_s.arg(head.peer.toString(), QString::fromLatin1(head.headers.value("Host").left(100))));
        return http::Response::text(421, "This server does not answer for that host name"_ba);
    }
    const QByteArray origin = head.headers.value("Origin");
    const QString r = route(head.path);
    const bool metadata = m_auth->owns(head.path) && head.path.contains("/.well-known/");
    if (head.method == "OPTIONS") {
        // A CORS preflight of a browser-based client.
        if (!originAllowed(origin) && !metadata)
            return http::Response::text(403, "This origin is not allowed"_ba);
        http::Response r204 = http::Response::empty(204);
        r204.headers.set("Access-Control-Allow-Origin"_ba, metadata && !originAllowed(origin) ? "*"_ba : origin);
        r204.headers.set("Access-Control-Allow-Methods"_ba, "GET, POST, DELETE, OPTIONS"_ba);
        r204.headers.set("Access-Control-Allow-Headers"_ba,
                         "Authorization, Content-Type, Accept, Mcp-Session-Id, MCP-Protocol-Version, Mcp-Method, "
                         "Mcp-Name, Last-Event-ID"_ba);
        r204.headers.set("Access-Control-Max-Age"_ba, "600"_ba);
        r204.headers.set("Vary"_ba, "Origin"_ba);
        return r204;
    }
    if (!metadata && r != u"/authorize" && !originAllowed(origin)) {
        // DNS rebinding or another site's page: refused before anything is read (the MCP
        // specification asks for 403, with a JSON-RPC error of no id).
        say(u"%1 refused: Origin %2"_s.arg(head.peer.toString(), QString::fromLatin1(origin.left(100))));
        return jsonRpcError(403, QJsonValue(), McpInvalidRequest, tr("This origin is not allowed"));
    }
    if (m_auth->owns(head.path)) {
        *maxBody = m_auth->bodyLimit(head.path);
        return std::nullopt;
    }
    if (r == u"/mcp") {
        // The token first: no body of a request without one is ever read.
        http::Response refusal;
        if (!m_auth->authenticate(head, &refusal)) {
            // A local: the temporary's list would end before the loop (C++20's range-for).
            const http::Headers cors = corsHeaders(origin);
            for (const auto &f : cors.fields())
                refusal.headers.add(f.name, f.value);
            return refusal;
        }
        if (head.method == "POST")
            *maxBody = m_options.maxBodyBytes;
        return std::nullopt;
    }
    if (r == u"/" && (head.method == "GET" || head.method == "HEAD"))
        return m_auth->homePage(head);
    return http::Response::text(404, "Not found"_ba);
}

void RemoteMcpServer::onRequest(const http::Request &request, const http::Reply &reply)
{
    const http::Reply corsReply = reply.with(corsHeaders(request.headers.value("Origin")));
    if (m_auth->owns(request.path)) {
        m_auth->handle(request, corsReply);
        return;
    }
    if (route(request.path) == u"/mcp") {
        handleMcp(request, corsReply);
        return;
    }
    reply.send(http::Response::text(404, "Not found"_ba));
}

void RemoteMcpServer::handleMcp(const http::Request &request, const http::Reply &reply)
{
    http::Response refusal;
    const auto access = m_auth->authenticate(request, &refusal);
    if (!access) {
        reply.send(refusal);
        return;
    }
    const QByteArray sessionHeader = request.headers.value("Mcp-Session-Id");
    if (request.method == "GET" || request.method == "HEAD") {
        // No stream of server-initiated messages: this server sends none (2025-06-18: 405).
        http::Response r = http::Response::text(405, "This server sends no messages of its own: no SSE stream"_ba);
        r.headers.set("Allow"_ba, "POST, DELETE"_ba);
        reply.send(r);
        return;
    }
    if (request.method == "DELETE") {
        const QString id = QString::fromLatin1(sessionHeader);
        const auto it = m_sessions.constFind(id);
        if (sessionHeader.isEmpty()) {
            reply.send(http::Response::text(400, "Mcp-Session-Id is missing"_ba));
        } else if (it == m_sessions.cend() || it->grantId != access->grantId) {
            reply.send(http::Response::text(404, "No such session"_ba));
        } else {
            m_sessions.remove(id);
            reply.send(http::Response::empty(204));
        }
        return;
    }
    if (request.method != "POST") {
        http::Response r = http::Response::text(405, "Method not allowed"_ba);
        r.headers.set("Allow"_ba, "POST, DELETE"_ba);
        reply.send(r);
        return;
    }
    const QByteArray contentType = request.headers.value("Content-Type").split(';').value(0).trimmed().toLower();
    if (contentType != "application/json") {
        reply.send(jsonRpcError(415, QJsonValue(), McpInvalidRequest, tr("The body must be application/json")));
        return;
    }
    bool json = true;
    if (request.headers.has("Accept")) {
        bool acceptsJson = false;
        bool acceptsSse = false;
        for (const QByteArray &item : request.headers.tokens("Accept")) {
            const QByteArray type = item.split(';').value(0).trimmed().toLower();
            acceptsJson = acceptsJson || type == "application/json" || type == "application/*" || type == "*/*";
            acceptsSse = acceptsSse || type == "text/event-stream";
        }
        if (!acceptsJson && !acceptsSse) {
            reply.send(jsonRpcError(406, QJsonValue(), McpInvalidRequest,
                                    tr("Accept must allow application/json or text/event-stream")));
            return;
        }
        json = acceptsJson;
    }
    QJsonParseError parseError{};
    const QJsonDocument doc = QJsonDocument::fromJson(request.body, &parseError);
    if (parseError.error != QJsonParseError::NoError) {
        reply.send(jsonRpcError(400, QJsonValue(), McpParseError, tr("Parse error: %1").arg(parseError.errorString())));
        return;
    }
    if (!doc.isObject()) {
        reply.send(jsonRpcError(400, QJsonValue(), McpInvalidRequest, tr("Expected one JSON-RPC message (no batches)")));
        return;
    }
    const QJsonObject message = doc.object();
    const QJsonValue id = message.value(u"id"_s);
    const QString method = message.value(u"method"_s).toString();
    const QJsonObject params = message.value(u"params"_s).toObject();
    const bool modern = method != u"initialize" && McpDispatcher::isModern(message);
    Session *session = nullptr;
    if (modern) {
        // The headers mirror the body (2026-07-28 "Server Validation"): a gateway that routes by
        // them must not see another request than this server runs.
        const QString version = params.value(u"_meta"_s)
                                        .toObject()
                                        .value(QString::fromLatin1(McpDispatcher::kProtocolVersionKey))
                                        .toString();
        const auto mismatch = [&](const QString &what) {
            reply.send(jsonRpcError(400, id, McpHeaderMismatch, tr("Header mismatch: %1").arg(what)));
        };
        if (QString::fromLatin1(request.headers.value("MCP-Protocol-Version")) != version) {
            mismatch(u"MCP-Protocol-Version"_s);
            return;
        }
        if (!message.contains(u"id"_s) && !method.isEmpty()) {
            // A notification: nothing of this server's needs one.
        } else if (QString::fromLatin1(request.headers.value("Mcp-Method")) != method) {
            mismatch(u"Mcp-Method"_s);
            return;
        }
        if (method == u"tools/call" || method == u"resources/read" || method == u"prompts/get") {
            const QString bodyName =
                    params.value(method == u"resources/read" ? u"uri"_s : u"name"_s).toString();
            const auto headerName = headerValue(request.headers.value("Mcp-Name"));
            if (!request.headers.has("Mcp-Name") || !headerName || *headerName != bodyName) {
                mismatch(u"Mcp-Name"_s);
                return;
            }
        }
    } else if (method != u"initialize" && message.contains(u"id"_s)) {
        // The handshake era: a session from initialize.
        if (sessionHeader.isEmpty()) {
            reply.send(jsonRpcError(400, id, McpInvalidRequest,
                                    tr("Mcp-Session-Id is missing: send initialize first")));
            return;
        }
        const auto it = m_sessions.find(QString::fromLatin1(sessionHeader));
        if (it == m_sessions.end() || it->grantId != access->grantId) {
            reply.send(jsonRpcError(404, id, McpInvalidRequest, tr("The session ended: initialize again")));
            return;
        }
        session = &*it;
        const QByteArray version = request.headers.value("MCP-Protocol-Version");
        if (!version.isEmpty() && QString::fromLatin1(version) != session->version) {
            reply.send(jsonRpcError(400, id, McpInvalidRequest,
                                    tr("MCP-Protocol-Version %1 is not the session's %2")
                                            .arg(QString::fromLatin1(version.left(40)), session->version)));
            return;
        }
        session->lastUsed = QDateTime::currentDateTimeUtc();
    }
    // What this token allows: writing only with notes:write (on a server that is not read-only).
    McpPolicy policy;
    policy.remote = true;
    policy.readOnly = m_options.readOnly || !access->write;
    policy.allowDestructive = m_options.allowDestructive && !policy.readOnly;
    if (method == u"tools/call" && !m_options.readOnly && !access->write) {
        McpPolicy server = policy;
        server.readOnly = false;
        server.allowDestructive = m_options.allowDestructive;
        if (const Command *c = m_dispatcher->tool(params.value(u"name"_s).toString(), server); c && c->writes) {
            reply.send(m_auth->insufficientScope(tr("This connection may only read: %1 changes notes")
                                                         .arg(toolName(*c))));
            return;
        }
    }
    McpDispatcher::Outcome outcome = m_dispatcher->handle(message, policy);
    if (!outcome.response) {
        reply.send(http::Response::empty(202));
        return;
    }
    http::Response r;
    r.status = outcome.httpStatus;
    if (method == u"initialize" && outcome.response->contains(u"result"_s)) {
        pruneSessions();
        // Room for one more: the least recently used session of this connection (or of all) goes.
        const auto evict = [this](const QString &grant) {
            auto oldest = m_sessions.end();
            for (auto it = m_sessions.begin(); it != m_sessions.end(); ++it) {
                if ((grant.isEmpty() || it->grantId == grant)
                    && (oldest == m_sessions.end() || it->lastUsed < oldest->lastUsed))
                    oldest = it;
            }
            if (oldest != m_sessions.end())
                m_sessions.erase(oldest);
        };
        while (std::ranges::count_if(m_sessions, [&](const Session &s) { return s.grantId == access->grantId; })
               >= m_options.maxSessionsPerGrant)
            evict(access->grantId);
        while (m_sessions.size() >= m_options.maxSessions)
            evict(QString());
        const QString sessionId = oauth::randomToken(QString(), 24);
        m_sessions.insert(sessionId, {access->grantId, outcome.negotiated, QDateTime::currentDateTimeUtc()});
        r.headers.set("Mcp-Session-Id"_ba, sessionId.toLatin1());
    }
    const QByteArray body = QJsonDocument(*outcome.response).toJson(QJsonDocument::Compact);
    if (json) {
        r.headers.set("Content-Type"_ba, "application/json"_ba);
        r.body = body;
    } else {
        r.headers.set("Content-Type"_ba, "text/event-stream"_ba);
        r.headers.set("Cache-Control"_ba, "no-cache"_ba);
        r.headers.set("X-Accel-Buffering"_ba, "no"_ba);
        r.body = "event: message\ndata: "_ba + body + "\n\n";
    }
    reply.send(r);
}

// ---- The commands

QString remoteAuthFile(Context &context, const QJsonObject &args, std::optional<Error> *error)
{
    const QString given = args.value(u"authFile"_s).toString().trimmed();
    if (!given.isEmpty()) {
        const QString expanded = given.startsWith(u"~/") ? QDir::homePath() + given.mid(1) : given;
        return QDir::cleanPath(QFileInfo(expanded).absoluteFilePath());
    }
    if (auto e = context.resolveVault()) {
        *error = e;
        return {};
    }
    const QString state = context.stateDir();
    if (state.isEmpty()) {
        *error = Error{ExitUsage, u"auth_file_required"_s,
                       QCoreApplication::translate("brulion::cli",
                                                   "Without device state (--no-history) give the state file with "
                                                   "--auth-file")};
        return {};
    }
    return QDir(state).filePath(QString::fromLatin1(kAuthFileName));
}

namespace {

bool stdinIsTerminal()
{
#ifdef Q_OS_WIN
    return _isatty(_fileno(stdin)) != 0;
#else
    return isatty(fileno(stdin)) != 0;
#endif
}

/// A line typed on the terminal without echo ("" when it could not be read).
QString readHidden(const QString &prompt)
{
    const QByteArray p = prompt.toUtf8();
    std::fwrite(p.constData(), 1, size_t(p.size()), stderr);
    std::fflush(stderr);
    QString line;
#ifdef Q_OS_WIN
    HANDLE input = ::GetStdHandle(STD_INPUT_HANDLE);
    DWORD mode = 0;
    const bool console = ::GetConsoleMode(input, &mode);
    if (console)
        ::SetConsoleMode(input, mode & ~DWORD(ENABLE_ECHO_INPUT));
    wchar_t buffer[1100];
    DWORD read = 0;
    if (console && ::ReadConsoleW(input, buffer, 1024, &read, nullptr))
        line = QString::fromWCharArray(buffer, int(read));
    if (console)
        ::SetConsoleMode(input, mode);
#else
    termios old{};
    const bool terminal = tcgetattr(STDIN_FILENO, &old) == 0;
    if (terminal) {
        termios quiet = old;
        quiet.c_lflag &= ~tcflag_t(ECHO);
        tcsetattr(STDIN_FILENO, TCSAFLUSH, &quiet);
    }
    char buffer[1100];
    if (std::fgets(buffer, sizeof buffer, stdin))
        line = QString::fromUtf8(buffer);
    if (terminal)
        tcsetattr(STDIN_FILENO, TCSAFLUSH, &old);
#endif
    std::fwrite("\n", 1, 1, stderr);
    while (line.endsWith(u'\n') || line.endsWith(u'\r'))
        line.chop(1);
    return line;
}

Result mcpServeCommand(Call &call)
{
    // run() starts the server itself (it serves until stopped).
    return call.fail(ExitUsage, u"not_a_tool"_s, tr("mcp serve starts the server; it is no tool of its own"));
}

Result mcpPassword(Call &call)
{
    std::optional<Error> error;
    const QString file = remoteAuthFile(call.ctx, call.args, &error);
    if (error)
        return failWith(*error);
    QString password;
    if (call.ctx.globals.stdinText) {
        password = call.ctx.readStdin();
        while (password.endsWith(u'\n') || password.endsWith(u'\r'))
            password.chop(1);
    } else if (stdinIsTerminal()) {
        password = readHidden(tr("New password for the MCP server: "));
        if (readHidden(tr("Again: ")) != password)
            return call.fail(ExitUsage, u"password_mismatch"_s, tr("The two passwords differ"));
    } else {
        return call.fail(ExitUsage, u"password_needs_stdin"_s,
                         tr("Type the password on a terminal, or give it on standard input with --stdin"));
    }
    if (password.size() < oauth::kMinPasswordLength)
        return call.fail(ExitUsage, u"password_too_short"_s,
                         tr("The password needs at least %1 characters").arg(oauth::kMinPasswordLength));
    if (password.size() > 1024)
        return call.fail(ExitUsage, u"password_too_long"_s, tr("The password is longer than 1024 characters"));
    const QString hash = oauth::hashPassword(password);
    if (call.flag(u"printHash"_s)) {
        Result r = call.ok(QJsonObject{{u"hash"_s, hash}}, hash + u'\n');
        return r;
    }
    if (call.dryRun())
        return call.ok(QJsonObject{{u"authFile"_s, file}, {u"dryRun"_s, true}});
    oauth::Store store(file);
    QString writeError;
    if (!store.update(
                [&hash](oauth::State &s) {
                    s.password = hash;
                    return true;
                },
                &writeError))
        return call.fail(ExitUnavailable, u"cannot_write"_s, tr("Cannot write %1: %2").arg(file, writeError),
                         {{u"path"_s, file}});
    return call.ok(QJsonObject{{u"authFile"_s, file}, {u"changed"_s, true},
                               {u"connections"_s, store.state().grants.size()}},
                   tr("Password set (%1). Connections made before keep working; end them with mcp revoke.")
                           .arg(file));
}

QJsonObject grantJson(const oauth::Grant &g)
{
    return {{u"id"_s, g.id},
            {u"client"_s, g.clientName},
            {u"clientId"_s, g.clientId},
            {u"kind"_s, g.clientKind},
            {u"returnsTo"_s, QUrl(g.redirectUri).host()},
            {u"scopes"_s, QJsonArray::fromStringList(g.scopes)},
            {u"write"_s, g.canWrite()},
            {u"connected"_s, oauth::timeText(g.created)},
            {u"lastUsed"_s, oauth::timeText(g.lastUsed)},
            {u"expires"_s, oauth::timeText(g.refresh.expires)}};
}

Result mcpClients(Call &call)
{
    std::optional<Error> error;
    const QString file = remoteAuthFile(call.ctx, call.args, &error);
    if (error)
        return failWith(*error);
    oauth::Store store(file);
    QString readError;
    if (!store.refresh(&readError))
        return call.fail(ExitUnavailable, u"cannot_read"_s, tr("Cannot read %1: %2").arg(file, readError),
                         {{u"path"_s, file}});
    oauth::State state = store.state();
    state.prune(QDateTime::currentDateTimeUtc());
    QJsonArray connections;
    QString text;
    for (const oauth::Grant &g : std::as_const(state.grants)) {
        connections.append(grantJson(g));
        text += u"%1  %2  %3  %4  %5\n"_s.arg(
                g.id, g.clientName.isEmpty() ? g.clientId : g.clientName,
                g.canWrite() ? tr("reads and writes") : tr("reads"),
                tr("connected %1").arg(QLocale().toString(g.created.toLocalTime(), QLocale::ShortFormat)),
                tr("last used %1").arg(QLocale().toString(g.lastUsed.toLocalTime(), QLocale::ShortFormat)));
    }
    QJsonArray clients;
    for (const oauth::Client &c : std::as_const(state.clients)) {
        clients.append(QJsonObject{{u"id"_s, c.id},
                                   {u"name"_s, c.name},
                                   {u"redirectUris"_s, QJsonArray::fromStringList(c.redirectUris)},
                                   {u"registered"_s, oauth::timeText(c.created)}});
    }
    if (connections.isEmpty())
        text = tr("No connections.") + u'\n';
    return call.ok(QJsonObject{{u"authFile"_s, file},
                               {u"passwordSet"_s, !state.password.isEmpty()},
                               {u"connections"_s, connections},
                               {u"clients"_s, clients}},
                   text);
}

Result mcpRevoke(Call &call)
{
    std::optional<Error> error;
    const QString file = remoteAuthFile(call.ctx, call.args, &error);
    if (error)
        return failWith(*error);
    const QString target = call.str(u"connection"_s).trimmed();
    const bool all = call.flag(u"all"_s);
    if (target.isEmpty() == !all)
        return call.fail(ExitUsage, u"revoke_what"_s, tr("Name a connection or client (mcp clients), or give --all"));
    oauth::Store store(file);
    QStringList revoked;
    bool clientRemoved = false;
    QString writeError;
    const bool written = store.update(
            [&](oauth::State &s) {
                const qsizetype before = s.grants.size();
                s.grants.removeIf([&](const oauth::Grant &g) {
                    const bool match = all || g.id == target || g.clientId == target;
                    if (match)
                        revoked.append(g.id);
                    return match;
                });
                const qsizetype clientsBefore = s.clients.size();
                s.clients.removeIf([&](const oauth::Client &c) { return all || c.id == target; });
                clientRemoved = s.clients.size() != clientsBefore;
                return s.grants.size() != before || clientRemoved;
            },
            &writeError);
    if (!written)
        return call.fail(ExitUnavailable, u"cannot_write"_s, tr("Cannot write %1: %2").arg(file, writeError),
                         {{u"path"_s, file}});
    if (!all && revoked.isEmpty() && !clientRemoved)
        return call.fail(ExitNotFound, u"not_found"_s, tr("No connection or client %1").arg(target),
                         {{u"connection"_s, target}});
    return call.ok(QJsonObject{{u"authFile"_s, file}, {u"revoked"_s, QJsonArray::fromStringList(revoked)}},
                   tr("Connections ended: %1").arg(revoked.size()) + u'\n');
}

Command make(const QString &id, const char *summary, Handler run, QList<Arg> args, const char *help)
{
    Command c;
    c.id = id;
    c.summary = cliText(summary);
    c.help = cliText(help);
    c.run = std::move(run);
    c.args = std::move(args);
    c.needsVault = false; // the notebook gives the state file's default place only
    c.mcp = false;
    return c;
}

Arg authFileOption()
{
    return option(u"authFile"_s, ArgType::String,
                  QT_TRANSLATE_NOOP("brulion::cli",
                                    "The server's state file (password hash, connections); default: mcp-remote.json "
                                    "in the notebook's device state"));
}

} // namespace

void addRemoteMcpCommands(QList<Command> &out)
{
    Command c = make(
            u"mcp.serve"_s,
            QT_TRANSLATE_NOOP("brulion::cli",
                              "Serve the MCP tools over HTTPS with sign-in, for claude.ai, the Claude app and ChatGPT"),
            mcpServeCommand,
            {option(u"listen"_s, ArgType::String,
                    QT_TRANSLATE_NOOP("brulion::cli", "The address to listen on (default 127.0.0.1)")),
             option(u"port"_s, ArgType::Int, QT_TRANSLATE_NOOP("brulion::cli", "The port (default 8780; 0: any free one)")),
             option(u"publicUrl"_s, ArgType::String,
                    QT_TRANSLATE_NOOP("brulion::cli",
                                      "The address the apps reach it at (https://notes.example.com, behind a proxy "
                                      "or tunnel); the MCP endpoint is this + /mcp")),
             authFileOption(),
             option(u"passwordFile"_s, ArgType::String,
                    QT_TRANSLATE_NOOP("brulion::cli",
                                      "A file with the password or its hash (mcp password --print-hash) instead of "
                                      "the state file's")),
             option(u"tlsCert"_s, ArgType::String,
                    QT_TRANSLATE_NOOP("brulion::cli", "HTTPS: the certificate chain (PEM), reloaded when it changes")),
             option(u"tlsKey"_s, ArgType::String, QT_TRANSLATE_NOOP("brulion::cli", "HTTPS: its private key (PEM)")),
             option(u"allowedOrigin"_s, ArgType::String,
                    QT_TRANSLATE_NOOP("brulion::cli",
                                      "Browser-based clients allowed to call it (origins, comma-separated)")),
             option(u"allowedHost"_s, ArgType::String,
                    QT_TRANSLATE_NOOP("brulion::cli",
                                      "More host names it answers to (a proxy that rewrites Host; comma-separated)")),
             flag(u"readOnly"_s, QT_TRANSLATE_NOOP("brulion::cli", "No client may change notes")),
             flag(u"allowDestructive"_s,
                  QT_TRANSLATE_NOOP("brulion::cli",
                                    "Also offer the destructive tools to clients allowed to write"))},
            QT_TRANSLATE_NOOP("brulion::cli",
                              "MCP Streamable HTTP at <public URL>/mcp with OAuth 2.1 (dynamic client registration, "
                              "client ID metadata documents, PKCE): the owner signs in with the password of mcp "
                              "password and allows each app, reading only or also writing. Prints one JSON line "
                              "when it listens, logs requests on standard error and serves until stopped."));
    c.needsVault = true;
    out.append(c);
    out.append(make(u"mcp.password"_s,
                    QT_TRANSLATE_NOOP("brulion::cli", "Set the password of the remote MCP server (mcp serve)"),
                    mcpPassword,
                    {authFileOption(),
                     flag(u"printHash"_s,
                          QT_TRANSLATE_NOOP("brulion::cli",
                                            "Print its hash instead of storing it (for --password-file)"))},
                    QT_TRANSLATE_NOOP("brulion::cli",
                                      "Typed twice on the terminal, or one line of standard input with --stdin; kept "
                                      "only as a salted PBKDF2-SHA256 hash (600 000 rounds). At least 8 characters.")));
    out.append(make(u"mcp.clients"_s,
                    QT_TRANSLATE_NOOP("brulion::cli", "The apps connected to the remote MCP server"), mcpClients,
                    {authFileOption()},
                    QT_TRANSLATE_NOOP("brulion::cli",
                                      "Each connection: the app, whether it may write, when it connected, was last "
                                      "used and expires unless used.")));
    out.append(make(u"mcp.revoke"_s,
                    QT_TRANSLATE_NOOP("brulion::cli", "End connections of the remote MCP server"), mcpRevoke,
                    {positional(u"connection"_s, ArgType::String,
                                QT_TRANSLATE_NOOP("brulion::cli",
                                                  "A connection (bgr_…) or a client (bcl_…, an https:// client ID) "
                                                  "from mcp clients"),
                                false),
                     flag(u"all"_s, QT_TRANSLATE_NOOP("brulion::cli", "End every connection")), authFileOption()},
                    QT_TRANSLATE_NOOP("brulion::cli",
                                      "Its tokens stop working at once, also in a running server; the app has to "
                                      "sign in again.")));
}

int serveRemoteMcp(Context &context, const Command &command, const QJsonObject &args,
                   const std::function<int(const Command *, const Result &)> &output)
{
    const auto fail = [&](int exit, const QString &code, const QString &message, const QJsonObject &details = {}) {
        Result r;
        r.error = Error{exit, code, message, details};
        return output(&command, r);
    };
    if (auto e = context.resolveVault()) {
        Result r;
        r.error = e;
        return output(&command, r);
    }
    RemoteOptions options;
    const QString listen = args.value(u"listen"_s).toString(u"127.0.0.1"_s).trimmed();
    if (listen == u"localhost")
        options.address = QHostAddress(QHostAddress::LocalHost);
    else if (listen == u"*" || listen == u"0.0.0.0")
        options.address = QHostAddress(QHostAddress::AnyIPv4);
    else if (listen == u"::")
        options.address = QHostAddress(QHostAddress::Any);
    else
        options.address = QHostAddress(listen);
    if (options.address.isNull())
        return fail(ExitUsage, u"bad_listen"_s, QCoreApplication::translate("brulion::cli", "--listen takes an IP address"),
                    {{u"listen"_s, listen}});
    // From the command line a number comes as text, from JSON as a number.
    const QJsonValue portValue = args.value(u"port"_s);
    bool portOk = true;
    const qint64 port = portValue.isUndefined() ? 8780
                        : portValue.isDouble()  ? portValue.toInteger(-1)
                                                : portValue.toString().toLongLong(&portOk);
    if (!portOk || port < 0 || port > 65535)
        return fail(ExitUsage, u"bad_port"_s, QCoreApplication::translate("brulion::cli", "--port takes 0 … 65535"));
    options.port = quint16(port);
    options.publicUrl = args.value(u"publicUrl"_s).toString();
    options.readOnly = context.globals.readOnly || args.value(u"readOnly"_s).toBool();
    options.allowDestructive = context.globals.allowDestructive || args.value(u"allowDestructive"_s).toBool();
    options.tlsCertificate = args.value(u"tlsCert"_s).toString();
    options.tlsKey = args.value(u"tlsKey"_s).toString();
    options.allowedOrigins = listArgument(args, u"allowedOrigin"_s);
    options.allowedHosts = listArgument(args, u"allowedHost"_s);
    std::optional<Error> error;
    options.authFile = remoteAuthFile(context, args, &error);
    if (error) {
        Result r;
        r.error = error;
        return output(&command, r);
    }
    if (const QString passwordFile = args.value(u"passwordFile"_s).toString(); !passwordFile.isEmpty()) {
        QFile file(passwordFile);
        if (!file.open(QIODevice::ReadOnly))
            return fail(ExitUnavailable, u"cannot_read"_s,
                        QCoreApplication::translate("brulion::cli", "Cannot read %1: %2").arg(passwordFile, file.errorString()));
        QString secret = QString::fromUtf8(file.read(4096));
        while (secret.endsWith(u'\n') || secret.endsWith(u'\r'))
            secret.chop(1);
        if (oauth::isPasswordHash(secret))
            options.passwordHash = secret.trimmed();
        else if (secret.size() >= oauth::kMinPasswordLength)
            options.passwordHash = oauth::hashPassword(secret);
        else
            return fail(ExitUsage, u"password_too_short"_s,
                        QCoreApplication::translate("brulion::cli", "The password needs at least %1 characters")
                                .arg(oauth::kMinPasswordLength));
    }
    // The notebook opened and indexed before the first request (as `mcp`).
    std::optional<Error> openError;
    if (notebook::Notebook *nb = context.notebook(&openError))
        nb->index();
    RemoteMcpServer server(context, options);
    server.log = logLine;
    QString startError;
    if (!server.start(&startError))
        return fail(ExitUnavailable, u"cannot_serve"_s, startError);
    const QJsonObject started{{u"ok"_s, true},
                              {u"command"_s, command.id},
                              {u"vault"_s, context.vaultRoot()},
                              {u"result"_s, QJsonObject{{u"endpoint"_s, server.endpoint()},
                                                        {u"port"_s, server.port()},
                                                        {u"address"_s, options.address.toString()},
                                                        {u"tls"_s, server.server()->tls()},
                                                        {u"readOnly"_s, options.readOnly},
                                                        {u"allowDestructive"_s, options.allowDestructive},
                                                        {u"authFile"_s, options.authFile}}},
                              {u"warnings"_s, QJsonArray()}};
    const QByteArray line = QJsonDocument(started).toJson(QJsonDocument::Compact) + '\n';
    std::fwrite(line.constData(), 1, size_t(line.size()), stdout);
    std::fflush(stdout);
    logLine(u"serving %1 at %2 (listening on %3 port %4%5%6)"_s.arg(
            context.vaultRoot(), server.endpoint(), options.address.toString(), QString::number(server.port()),
            server.server()->tls() ? u", HTTPS"_s : QString(), options.readOnly ? u", read-only"_s : QString()));
    return QCoreApplication::exec();
}

} // namespace brulion::cli
