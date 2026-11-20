// `brulion-cli mcp serve` in process over a real local socket (src/cli/mcpremote.h): the whole
// OAuth flow with QNetworkAccessManager as the client — protected resource and authorization
// server metadata, dynamic client registration, the authorization request and its errors, the
// sign-in page (wrong and right password, the form's token, another site's form, the delay after
// wrong passwords), consent (allow, deny, reading only), the token endpoint with PKCE (a wrong
// verifier refused, a code used twice revokes), MCP over Streamable HTTP in both eras (sessions
// and DELETE; the per-request headers checked against the body; SSE answers), tools/list and
// tools/call (search finds Polish text, writes refused for a reading connection and on a
// read-only server), refresh token rotation with its grace period and reuse detection, expired and
// revoked tokens (by another process), client ID metadata documents (a fake fetcher), a
// confidential client, token revocation, a restart that keeps connections, Origin and Host
// refusals, malformed, oversized and slow requests, too many connections, concurrent clients and
// HTTPS with a certificate that is reloaded.
//
// The certificates in tests/data/mcp-remote are self-signed for localhost and 127.0.0.1 for 100
// years: openssl req -x509 -newkey ec -pkeyopt ec_paramgen_curve:prime256v1 -nodes -days 36500
// -subj /CN=localhost -addext subjectAltName=DNS:localhost,IP:127.0.0.1.
#include "mcpremote.h"
#include "oauth.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QTcpSocket>
#include <QTemporaryDir>
#include <QTest>
#include <QUrlQuery>

#if QT_CONFIG(ssl)
#include <QSslConfiguration>
#include <QSslSocket>
#endif

#include <memory>

using namespace Qt::StringLiterals;
using namespace brulion::cli;

namespace {

constexpr int kTimeoutMs = 30000 * BRULION_TEST_SLOWDOWN;
const QString kPassword = u"Hasło właściciela 1"_s;
const QString kClaudeRedirect = u"https://claude.ai/api/mcp/auth_callback"_s;

struct HttpResult {
    int status = 0;
    QByteArray body;
    QHash<QByteArray, QByteArray> headers; ///< names in lower case
    QJsonObject json;
    QByteArray header(const char *name) const { return headers.value(QByteArray(name).toLower()); }
};

struct Pkce {
    QString verifier;
    QString challenge;
};

Pkce makePkce()
{
    Pkce p;
    p.verifier = oauth::randomToken(QString(), 40);
    p.challenge = QString::fromLatin1(QCryptographicHash::hash(p.verifier.toLatin1(), QCryptographicHash::Sha256)
                                              .toBase64(QByteArray::Base64UrlEncoding | QByteArray::OmitTrailingEquals));
    return p;
}

struct Tokens {
    QString clientId;
    QString access;
    QString refresh;
    QString scope;
};

QByteArray formBody(const QList<std::pair<QString, QString>> &fields)
{
    QByteArray out;
    for (const auto &[name, value] : fields) {
        if (!out.isEmpty())
            out += '&';
        out += QUrl::toPercentEncoding(name) + '=' + QUrl::toPercentEncoding(value);
    }
    return out;
}

QString hidden(const QByteArray &html, const char *name)
{
    const QRegularExpression re(u"name=\"%1\" value=\"([^\"]*)\""_s.arg(QString::fromLatin1(name)));
    return re.match(QString::fromUtf8(html)).captured(1);
}

QHash<QString, QString> locationQuery(const HttpResult &r)
{
    QHash<QString, QString> out;
    const QUrl location(QString::fromUtf8(r.header("Location")));
    for (const auto &[k, v] : QUrlQuery(location).queryItems(QUrl::FullyDecoded))
        out.insert(k, v);
    return out;
}

} // namespace

class TestCliRemoteMcp : public QObject {
    Q_OBJECT
    QTemporaryDir m_tmp;
    QString m_vault;
    std::unique_ptr<Context> m_ctx;
    std::unique_ptr<RemoteMcpServer> m_server;
    QNetworkAccessManager *m_nam = nullptr;
    QString m_base;
    QString m_authFile;
    QDateTime m_now;
    int m_servers = 0;
    QStringList m_log;

    void startServer(RemoteOptions options = {}, bool freshState = true)
    {
        m_server.reset();
        if (freshState) {
            m_authFile = m_tmp.filePath(u"auth-%1/mcp-remote.json"_s.arg(++m_servers));
            oauth::Store store(m_authFile);
            QString error;
            QVERIFY(store.update(
                    [](oauth::State &s) {
                        s.password = oauth::hashPassword(kPassword, 1000);
                        return true;
                    },
                    &error));
        }
        options.authFile = m_authFile;
        options.port = 0;
        m_server = std::make_unique<RemoteMcpServer>(*m_ctx, options);
        m_server->log = [this](const QString &line) { m_log.append(line); };
        QString error;
        QVERIFY2(m_server->start(&error), qPrintable(error));
        m_now = QDateTime::currentDateTimeUtc();
        m_server->auth()->clock = [this] { return m_now; };
        m_base = m_server->issuer();
        // A fresh client for every server: no connection is reused across them.
        delete m_nam;
        m_nam = new QNetworkAccessManager(this);
    }

    HttpResult send(const QByteArray &method, const QString &path, const QByteArray &body = {},
                    const QList<std::pair<QByteArray, QByteArray>> &headers = {})
    {
        QNetworkRequest request(QUrl(path.startsWith(u"http") ? path : m_base + path));
        request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::ManualRedirectPolicy);
        request.setAttribute(QNetworkRequest::CacheLoadControlAttribute, QNetworkRequest::AlwaysNetwork);
        for (const auto &[name, value] : headers)
            request.setRawHeader(name, value);
        QNetworkReply *reply =
                method == "HEAD" ? m_nam->head(request) : m_nam->sendCustomRequest(request, method, body);
        HttpResult out;
        if (!QTest::qWaitFor([reply] { return reply->isFinished(); }, kTimeoutMs)) {
            reply->abort();
            reply->deleteLater();
            return out;
        }
        out.status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        out.body = reply->readAll();
        for (const auto &[name, value] : reply->rawHeaderPairs())
            out.headers.insert(name.toLower(), value);
        out.json = QJsonDocument::fromJson(out.body).object();
        reply->deleteLater();
        return out;
    }

    HttpResult postForm(const QString &path, const QList<std::pair<QString, QString>> &fields,
                        QList<std::pair<QByteArray, QByteArray>> headers = {})
    {
        headers.append({"Content-Type"_ba, "application/x-www-form-urlencoded"_ba});
        return send("POST"_ba, path, formBody(fields), headers);
    }

    HttpResult postJson(const QString &path, const QJsonObject &body, QList<std::pair<QByteArray, QByteArray>> headers = {})
    {
        headers.append({"Content-Type"_ba, "application/json"_ba});
        return send("POST"_ba, path, QJsonDocument(body).toJson(QJsonDocument::Compact), headers);
    }

    QString registerClient(const QString &redirect = kClaudeRedirect, const QString &name = u"Claude"_s)
    {
        const HttpResult r = postJson(u"/register"_s, {{u"redirect_uris"_s, QJsonArray{redirect}},
                                                       {u"client_name"_s, name},
                                                       {u"token_endpoint_auth_method"_s, u"none"_s},
                                                       {u"grant_types"_s,
                                                        QJsonArray{u"authorization_code"_s, u"refresh_token"_s}}});
        if (r.status != 201)
            qWarning() << "register" << r.status << r.body;
        return r.json.value(u"client_id"_s).toString();
    }

    HttpResult authorize(const QString &clientId, const Pkce &pkce, const QString &redirect = kClaudeRedirect,
                         QList<std::pair<QString, QString>> extra = {})
    {
        QList<std::pair<QString, QString>> params{{u"response_type"_s, u"code"_s},
                                                  {u"client_id"_s, clientId},
                                                  {u"redirect_uri"_s, redirect},
                                                  {u"code_challenge"_s, pkce.challenge},
                                                  {u"code_challenge_method"_s, u"S256"_s},
                                                  {u"state"_s, u"stan-żółw"_s},
                                                  {u"scope"_s, u"notes:read notes:write"_s},
                                                  {u"resource"_s, m_server->endpoint()}};
        for (const auto &e : extra) {
            params.removeIf([&](const auto &p) { return p.first == e.first; });
            if (!e.second.isNull())
                params.append(e);
        }
        return send("GET"_ba, u"/authorize?"_s + QString::fromLatin1(formBody(params)));
    }

    /// Sign in on the page of `page` (the sign-in page) and answer the consent page.
    HttpResult signInAndDecide(const HttpResult &page, const QString &action = u"allow"_s, bool write = true)
    {
        const QString request = hidden(page.body, "request");
        HttpResult consent = postForm(u"/authorize"_s, {{u"request"_s, request},
                                                        {u"csrf"_s, hidden(page.body, "csrf")},
                                                        {u"action"_s, u"signin"_s},
                                                        {u"password"_s, kPassword}});
        if (!consent.body.contains("name=\"action\" value=\"allow\""))
            return consent;
        QList<std::pair<QString, QString>> fields{
                {u"request"_s, request}, {u"csrf"_s, hidden(consent.body, "csrf")}, {u"action"_s, action}};
        if (write)
            fields.append({u"write"_s, u"on"_s});
        return postForm(u"/authorize"_s, fields);
    }

    HttpResult exchange(const QString &clientId, const QString &code, const Pkce &pkce,
                        const QString &redirect = kClaudeRedirect)
    {
        return postForm(u"/token"_s, {{u"grant_type"_s, u"authorization_code"_s},
                                      {u"code"_s, code},
                                      {u"redirect_uri"_s, redirect},
                                      {u"client_id"_s, clientId},
                                      {u"code_verifier"_s, pkce.verifier},
                                      {u"resource"_s, m_server->endpoint()}});
    }

    /// A whole connection: registration, sign-in, consent, tokens.
    Tokens connectClient(bool write = true)
    {
        Tokens t;
        t.clientId = registerClient();
        const Pkce pkce = makePkce();
        const HttpResult page = authorize(t.clientId, pkce);
        const HttpResult allowed = signInAndDecide(page, u"allow"_s, write);
        const QString code = locationQuery(allowed).value(u"code"_s);
        const HttpResult token = exchange(t.clientId, code, pkce);
        t.access = token.json.value(u"access_token"_s).toString();
        t.refresh = token.json.value(u"refresh_token"_s).toString();
        t.scope = token.json.value(u"scope"_s).toString();
        return t;
    }

    HttpResult mcp(const QString &access, const QJsonObject &message,
                   QList<std::pair<QByteArray, QByteArray>> headers = {})
    {
        headers.prepend({"Accept"_ba, "application/json, text/event-stream"_ba});
        headers.prepend({"Authorization"_ba, "Bearer "_ba + access.toLatin1()});
        return postJson(u"/mcp"_s, message, headers);
    }

    static QJsonObject rpc(int id, const QString &method, const QJsonObject &params = {})
    {
        QJsonObject o{{u"jsonrpc"_s, u"2.0"_s}, {u"id"_s, id}, {u"method"_s, method}};
        if (!params.isEmpty())
            o.insert(u"params"_s, params);
        return o;
    }

    /// initialize: the session id.
    QByteArray initialize(const QString &access, const QString &version = u"2025-06-18"_s)
    {
        const HttpResult r = mcp(access, rpc(1, u"initialize"_s,
                                             {{u"protocolVersion"_s, version},
                                              {u"capabilities"_s, QJsonObject()},
                                              {u"clientInfo"_s, QJsonObject{{u"name"_s, u"test"_s}, {u"version"_s, u"1"_s}}}}));
        return r.header("Mcp-Session-Id");
    }

    QStringList toolNames(const HttpResult &r)
    {
        QStringList names;
        for (const QJsonValue &t : r.json.value(u"result"_s).toObject().value(u"tools"_s).toArray())
            names.append(t.toObject().value(u"name"_s).toString());
        return names;
    }

    /// Raw bytes over a socket of our own: what came back once the server closed or `until` showed.
    QByteArray raw(const QByteArray &bytes, const QByteArray &until = {}, bool *closed = nullptr, int port = 0)
    {
        QTcpSocket socket;
        socket.connectToHost(QHostAddress::LocalHost, port ? port : m_server->port());
        if (!QTest::qWaitFor([&] { return socket.state() == QAbstractSocket::ConnectedState; }, kTimeoutMs))
            return {};
        socket.write(bytes);
        QByteArray got;
        (void)QTest::qWaitFor(
                [&] {
                    got += socket.readAll();
                    return socket.state() == QAbstractSocket::UnconnectedState
                           || (!until.isEmpty() && got.contains(until));
                },
                kTimeoutMs);
        got += socket.readAll();
        if (closed)
            *closed = socket.state() == QAbstractSocket::UnconnectedState;
        return got;
    }

private Q_SLOTS:
    void initTestCase()
    {
        QVERIFY(m_tmp.isValid());
        // The app's settings (read only) and data of this test's own.
        for (const char *name : {"HOME", "XDG_CONFIG_HOME", "XDG_DATA_HOME", "XDG_CACHE_HOME", "XDG_STATE_HOME"})
            qputenv(name, m_tmp.filePath(QString::fromLatin1(name)).toLocal8Bit());
        QStandardPaths::setTestModeEnabled(true);
        m_vault = m_tmp.filePath(u"Notatki"_s);
        QVERIFY(QDir().mkpath(m_vault + u"/Zwierzęta"_s));
        QFile note(m_vault + u"/Zwierzęta/Żółw.md"_s);
        QVERIFY(note.open(QIODevice::WriteOnly));
        note.write("- żółw błotny śpi w ogrodzie\n");
        note.close();
        m_ctx = std::make_unique<Context>();
        m_ctx->globals.vault = m_vault;
        m_ctx->globals.stateDir = m_tmp.filePath(u"state"_s);
        m_ctx->globals.now = u"2026-10-14 10:30"_s;
        QVERIFY(!m_ctx->resolveVault());
    }

    void cleanup()
    {
        m_server.reset();
    }

    void metadata()
    {
        startServer();
        QVERIFY(m_server->endpoint().endsWith(u"/mcp"_s));
        for (const QString &path : {u"/.well-known/oauth-protected-resource"_s,
                                    u"/.well-known/oauth-protected-resource/mcp"_s}) {
            const HttpResult r = send("GET"_ba, path);
            QCOMPARE(r.status, 200);
            QCOMPARE(r.json.value(u"resource"_s).toString(), m_server->endpoint());
            QCOMPARE(r.json.value(u"authorization_servers"_s).toArray(), QJsonArray{m_base});
            QCOMPARE(r.json.value(u"scopes_supported"_s).toArray(), (QJsonArray{u"notes:read"_s, u"notes:write"_s}));
        }
        for (const QString &path : {u"/.well-known/oauth-authorization-server"_s, u"/.well-known/openid-configuration"_s}) {
            const HttpResult r = send("GET"_ba, path);
            QCOMPARE(r.status, 200);
            QCOMPARE(r.json.value(u"issuer"_s).toString(), m_base);
            QCOMPARE(r.json.value(u"code_challenge_methods_supported"_s).toArray(), QJsonArray{u"S256"_s});
            QVERIFY(r.json.value(u"token_endpoint_auth_methods_supported"_s).toArray().contains(u"none"_s));
            QVERIFY(r.json.value(u"client_id_metadata_document_supported"_s).toBool());
            QVERIFY(r.json.value(u"authorization_response_iss_parameter_supported"_s).toBool());
            QCOMPARE(r.json.value(u"registration_endpoint"_s).toString(), m_base + u"/register"_s);
        }
        QCOMPARE(send("POST"_ba, u"/.well-known/oauth-authorization-server"_s).status, 405);
        QCOMPARE(send("HEAD"_ba, u"/.well-known/oauth-protected-resource"_s).status, 200);
        // The root page tells what to paste into the app.
        const HttpResult home = send("GET"_ba, u"/"_s);
        QCOMPARE(home.status, 200);
        QVERIFY(home.body.contains(m_server->endpoint().toUtf8()));
        QCOMPARE(send("GET"_ba, u"/nic-tu-nie-ma"_s).status, 404);
    }

    void unauthenticated()
    {
        startServer();
        HttpResult r = mcp(QString(), rpc(1, u"tools/list"_s));
        QCOMPARE(r.status, 401);
        r = send("POST"_ba, u"/mcp"_s, "{}"_ba, {{"Content-Type"_ba, "application/json"_ba}});
        QCOMPARE(r.status, 401);
        const QByteArray challenge = r.header("WWW-Authenticate");
        QVERIFY(challenge.startsWith("Bearer "));
        QVERIFY(challenge.contains(
                QByteArray("resource_metadata=\""_ba + m_base.toUtf8() + "/.well-known/oauth-protected-resource\"")));
        QVERIFY(challenge.contains("scope=\"notes:read notes:write\""));
        QVERIFY(!challenge.contains("error="));
        r = mcp(u"bat_nieprawdziwy"_s, rpc(1, u"tools/list"_s));
        QCOMPARE(r.status, 401);
        QVERIFY(r.header("WWW-Authenticate").contains("error=\"invalid_token\""));
        QCOMPARE(send("POST"_ba, u"/mcp"_s, "{}"_ba, {{"Authorization"_ba, "Basic eDp5"_ba}}).status, 401);
        // A large body without a token is never read: the answer comes at once and the connection
        // closes.
        bool closed = false;
        const QByteArray answer = raw("POST /mcp HTTP/1.1\r\nHost: 127.0.0.1:"_ba + QByteArray::number(m_server->port())
                                              + "\r\nContent-Type: application/json\r\nContent-Length: 10000000\r\n\r\n{",
                                      {}, &closed);
        QVERIFY(answer.startsWith("HTTP/1.1 401 "));
        QVERIFY(closed);
    }

    void registration()
    {
        startServer();
        HttpResult r = postJson(u"/register"_s, {{u"redirect_uris"_s, QJsonArray{kClaudeRedirect}},
                                                 {u"client_name"_s, u"Claude‮\nżółw"_s}});
        QCOMPARE(r.status, 201);
        QVERIFY(r.json.value(u"client_id"_s).toString().startsWith(u"bcl_"_s));
        QCOMPARE(r.json.value(u"token_endpoint_auth_method"_s).toString(), u"none"_s);
        QCOMPARE(r.json.value(u"client_name"_s).toString(), u"Claude żółw"_s); // controls dropped
        QVERIFY(!r.json.contains(u"client_secret"_s));
        // Nothing an unauthenticated request sends is written down.
        {
            QFile f(m_authFile);
            QVERIFY(f.open(QIODevice::ReadOnly));
            QVERIFY(!f.readAll().contains("bcl_"));
        }
        const QList<std::pair<QJsonObject, QString>> bad{
                {{{u"redirect_uris"_s, QJsonArray{u"http://evil.example/cb"_s}}}, u"invalid_redirect_uri"_s},
                {{{u"redirect_uris"_s, QJsonArray{u"javascript:alert(1)"_s}}}, u"invalid_redirect_uri"_s},
                {{{u"redirect_uris"_s, QJsonArray{u"https://a.example/cb#x"_s}}}, u"invalid_redirect_uri"_s},
                {{{u"redirect_uris"_s, QJsonArray()}}, u"invalid_redirect_uri"_s},
                {{{u"redirect_uris"_s, u"https://a.example/cb"_s}}, u"invalid_redirect_uri"_s},
                {{{u"client_name"_s, u"x"_s}}, u"invalid_redirect_uri"_s},
                {{{u"redirect_uris"_s, QJsonArray{kClaudeRedirect}}, {u"token_endpoint_auth_method"_s, u"private_key_jwt"_s}},
                 u"invalid_client_metadata"_s},
                {{{u"redirect_uris"_s, QJsonArray{kClaudeRedirect}}, {u"grant_types"_s, QJsonArray{u"implicit"_s}}},
                 u"invalid_client_metadata"_s},
                {{{u"redirect_uris"_s, QJsonArray{kClaudeRedirect}}, {u"response_types"_s, QJsonArray{u"token"_s}}},
                 u"invalid_client_metadata"_s}};
        for (const auto &[body, error] : bad) {
            r = postJson(u"/register"_s, body);
            QCOMPARE(r.status, 400);
            QCOMPARE(r.json.value(u"error"_s).toString(), error);
        }
        r = send("POST"_ba, u"/register"_s, "{nie json"_ba, {{"Content-Type"_ba, "application/json"_ba}});
        QCOMPARE(r.json.value(u"error"_s).toString(), u"invalid_client_metadata"_s);
        r = send("POST"_ba, u"/register"_s, "{}"_ba, {{"Content-Type"_ba, "text/plain"_ba}});
        QCOMPARE(r.status, 400);
        // Registrations are limited: 30 in ten minutes.
        int created = 1;
        for (int i = 0; i < 35; ++i) {
            r = postJson(u"/register"_s, {{u"redirect_uris"_s, QJsonArray{kClaudeRedirect}}});
            if (r.status == 201)
                ++created;
            else
                QCOMPARE(r.status, 429);
        }
        QCOMPARE(created, 30);
        m_now = m_now.addSecs(601);
        QCOMPARE(postJson(u"/register"_s, {{u"redirect_uris"_s, QJsonArray{kClaudeRedirect}}}).status, 201);
    }

    void authorizeErrors()
    {
        startServer();
        const QString client = registerClient();
        const Pkce pkce = makePkce();
        // The client or its return address unknown: a page, never a redirect.
        HttpResult r = authorize(u"bcl_nieznany"_s, pkce);
        QCOMPARE(r.status, 400);
        QVERIFY(!r.headers.contains("location"));
        QVERIFY(r.body.contains("not registered"));
        r = authorize(client, pkce, u"https://evil.example/cb"_s);
        QCOMPARE(r.status, 400);
        QVERIFY(!r.headers.contains("location"));
        r = authorize(client, pkce, kClaudeRedirect, {{u"client_id"_s, QString()}});
        QCOMPARE(r.status, 400);
        r = send("GET"_ba, u"/authorize?client_id=a&client_id=b"_s);
        QCOMPARE(r.status, 400);
        r = send("GET"_ba, u"/authorize?client_id=%ZZ"_s);
        QCOMPARE(r.status, 400);
        // Other errors go back to the client, with the state and the issuer (RFC 9207).
        const auto redirected = [&](const QList<std::pair<QString, QString>> &extra, const QString &error) {
            const HttpResult res = authorize(client, pkce, kClaudeRedirect, extra);
            QCOMPARE(res.status, 303);
            QVERIFY(res.header("Location").startsWith(QByteArray(kClaudeRedirect.toUtf8() + '?')));
            const auto q = locationQuery(res);
            QCOMPARE(q.value(u"error"_s), error);
            QCOMPARE(q.value(u"state"_s), u"stan-żółw"_s);
            QCOMPARE(q.value(u"iss"_s), m_base);
        };
        redirected({{u"code_challenge"_s, QString()}}, u"invalid_request"_s);
        redirected({{u"code_challenge_method"_s, u"plain"_s}}, u"invalid_request"_s);
        redirected({{u"code_challenge_method"_s, QString()}}, u"invalid_request"_s);
        redirected({{u"response_type"_s, u"token"_s}}, u"unsupported_response_type"_s);
        redirected({{u"resource"_s, u"https://evil.example/mcp"_s}}, u"invalid_target"_s);
        // A loopback client (Claude Code) on any port.
        const QString local = registerClient(u"http://127.0.0.1/callback"_s, u"Claude Code"_s);
        r = authorize(local, pkce, u"http://127.0.0.1:41234/callback"_s);
        QCOMPARE(r.status, 200);
        QVERIFY(r.body.contains("Sign in"));
    }

    void signInAndConsent()
    {
        startServer();
        const QString client = registerClient();
        const Pkce pkce = makePkce();
        const HttpResult page = authorize(client, pkce);
        QCOMPARE(page.status, 200);
        QVERIFY(page.body.contains("<strong>Claude</strong>"));
        QVERIFY(page.body.contains("Notatki"));
        const QByteArray csp = page.header("Content-Security-Policy");
        QVERIFY(csp.contains("default-src 'none'"));
        QVERIFY(csp.contains("frame-ancestors 'none'"));
        QCOMPARE(page.header("X-Frame-Options"), "DENY"_ba);
        QCOMPARE(page.header("Cache-Control"), "no-store"_ba);
        const QString request = hidden(page.body, "request");
        const QString csrf = hidden(page.body, "csrf");
        QVERIFY(!request.isEmpty() && !csrf.isEmpty());
        const auto signIn = [&](const QString &password, const QString &token,
                                QList<std::pair<QByteArray, QByteArray>> headers = {}) {
            return postForm(u"/authorize"_s, {{u"request"_s, request}, {u"csrf"_s, token}, {u"action"_s, u"signin"_s},
                                              {u"password"_s, password}},
                            headers);
        };
        HttpResult r = signIn(u"złe hasło"_s, csrf);
        QCOMPARE(r.status, 200);
        QVERIFY(r.body.contains("Wrong password."));
        QVERIFY(!r.body.contains("value=\"allow\""));
        // The form's token: another one is refused; so is another site's form.
        r = signIn(kPassword, u"podrobiony"_s);
        QCOMPARE(r.status, 403);
        r = signIn(kPassword, csrf, {{"Origin"_ba, "https://evil.example"_ba}});
        QCOMPARE(r.status, 403);
        r = postForm(u"/authorize"_s, {{u"request"_s, u"nieznane"_s}, {u"csrf"_s, csrf}, {u"action"_s, u"signin"_s}});
        QCOMPARE(r.status, 400);
        // Consent before sign-in is not possible.
        r = postForm(u"/authorize"_s, {{u"request"_s, request}, {u"csrf"_s, csrf}, {u"action"_s, u"allow"_s}});
        QVERIFY(!r.headers.contains("location"));
        r = signIn(kPassword, csrf, {{"Origin"_ba, http::originOf(QUrl(m_base)).toLatin1()}});
        QCOMPARE(r.status, 200);
        QVERIFY(r.body.contains("Allow access to your notes?"));
        QVERIFY(r.body.contains("claude.ai")); // where it returns, prominently
        QVERIFY(r.body.contains("name=\"write\""));
        QVERIFY(r.body.contains("a name the app gave itself"));
        QVERIFY(r.header("Content-Security-Policy").contains("form-action 'self' https://claude.ai"));
        const QString consentCsrf = hidden(r.body, "csrf");
        QVERIFY(consentCsrf != csrf); // a new one after sign-in
        r = postForm(u"/authorize"_s, {{u"request"_s, request}, {u"csrf"_s, consentCsrf}, {u"action"_s, u"deny"_s}});
        QCOMPARE(r.status, 303);
        auto q = locationQuery(r);
        QCOMPARE(q.value(u"error"_s), u"access_denied"_s);
        QCOMPARE(q.value(u"state"_s), u"stan-żółw"_s);
        QCOMPARE(q.value(u"iss"_s), m_base);
        // The request is gone once answered.
        r = postForm(u"/authorize"_s, {{u"request"_s, request}, {u"csrf"_s, consentCsrf}, {u"action"_s, u"allow"_s}});
        QCOMPARE(r.status, 400);
        // Allowed: a code for the client, with the state and the issuer.
        r = signInAndDecide(authorize(client, pkce));
        QCOMPARE(r.status, 303);
        q = locationQuery(r);
        QVERIFY(q.value(u"code"_s).startsWith(u"bac_"_s));
        QCOMPARE(q.value(u"iss"_s), m_base);
        QCOMPARE(q.value(u"state"_s), u"stan-żółw"_s);
        QVERIFY(!m_log.filter(u"wrong password"_s).isEmpty());
        QVERIFY(m_log.filter(kPassword).isEmpty()); // no secrets in the log
    }

    void signInDelay()
    {
        startServer();
        const QString client = registerClient();
        const Pkce pkce = makePkce();
        HttpResult page = authorize(client, pkce);
        const auto wrong = [&](HttpResult &p) {
            return postForm(u"/authorize"_s, {{u"request"_s, hidden(p.body, "request")},
                                              {u"csrf"_s, hidden(p.body, "csrf")},
                                              {u"action"_s, u"signin"_s},
                                              {u"password"_s, u"zgadywanie"_s}});
        };
        for (int i = 0; i < 4; ++i)
            QVERIFY(wrong(page).body.contains("Wrong password."));
        // The fifth wrong one ends this sign-in.
        QCOMPARE(wrong(page).status, 403);
        // From now on each attempt waits longer, whatever sign-in it comes from — even the right
        // password is not checked while the delay lasts.
        page = authorize(client, pkce);
        HttpResult r = postForm(u"/authorize"_s, {{u"request"_s, hidden(page.body, "request")},
                                                  {u"csrf"_s, hidden(page.body, "csrf")},
                                                  {u"action"_s, u"signin"_s},
                                                  {u"password"_s, kPassword}});
        QVERIFY(r.body.contains("Too many wrong passwords"));
        m_now = m_now.addSecs(2);
        r = postForm(u"/authorize"_s, {{u"request"_s, hidden(page.body, "request")},
                                       {u"csrf"_s, hidden(page.body, "csrf")},
                                       {u"action"_s, u"signin"_s},
                                       {u"password"_s, kPassword}});
        QVERIFY2(r.body.contains("value=\"allow\""), r.body.constData());
    }

    void tokenExchange()
    {
        startServer();
        const QString client = registerClient();
        const Pkce pkce = makePkce();
        HttpResult allowed = signInAndDecide(authorize(client, pkce));
        const QString code = locationQuery(allowed).value(u"code"_s);
        QVERIFY(!code.isEmpty());
        HttpResult r = exchange(client, code, makePkce()); // another verifier
        QCOMPARE(r.status, 400);
        QCOMPARE(r.json.value(u"error"_s).toString(), u"invalid_grant"_s);
        r = exchange(client, code, pkce, u"https://claude.ai/other"_s);
        QCOMPARE(r.json.value(u"error"_s).toString(), u"invalid_grant"_s);
        r = exchange(registerClient(), code, pkce);
        QCOMPARE(r.json.value(u"error"_s).toString(), u"invalid_grant"_s);
        r = postForm(u"/token"_s, {{u"grant_type"_s, u"password"_s}, {u"client_id"_s, client}});
        QCOMPARE(r.json.value(u"error"_s).toString(), u"unsupported_grant_type"_s);
        r = postForm(u"/token"_s, {{u"grant_type"_s, u"authorization_code"_s}, {u"client_id"_s, u"bcl_x"_s}});
        QCOMPARE(r.status, 401);
        QCOMPARE(r.json.value(u"error"_s).toString(), u"invalid_client"_s);
        r = send("POST"_ba, u"/token"_s, "grant_type=authorization_code&code=a&code=b"_ba,
                 {{"Content-Type"_ba, "application/x-www-form-urlencoded"_ba}});
        QCOMPARE(r.json.value(u"error"_s).toString(), u"invalid_request"_s);
        r = send("POST"_ba, u"/token"_s, "{}"_ba, {{"Content-Type"_ba, "application/json"_ba}});
        QCOMPARE(r.json.value(u"error"_s).toString(), u"invalid_request"_s);
        // The right verifier (the failed attempts did not use the code up).
        r = exchange(client, code, pkce);
        QCOMPARE(r.status, 200);
        QCOMPARE(r.json.value(u"token_type"_s).toString(), u"Bearer"_s);
        QCOMPARE(r.json.value(u"expires_in"_s).toInt(), 3600);
        QCOMPARE(r.json.value(u"scope"_s).toString(), u"notes:read notes:write"_s);
        QCOMPARE(r.header("Cache-Control"), "no-store"_ba);
        const QString access = r.json.value(u"access_token"_s).toString();
        QVERIFY(access.startsWith(u"bat_"_s));
        QVERIFY(r.json.value(u"refresh_token"_s).toString().startsWith(u"brt_"_s));
        QVERIFY(!initialize(access).isEmpty());
        // The connection is written down, by hash only.
        QFile f(m_authFile);
        QVERIFY(f.open(QIODevice::ReadOnly));
        const QByteArray state = f.readAll();
        QVERIFY(state.contains("Claude"));
        QVERIFY(!state.contains(access.toLatin1()));
        // The code again: refused, and what it gave is revoked.
        r = exchange(client, code, pkce);
        QCOMPARE(r.json.value(u"error"_s).toString(), u"invalid_grant"_s);
        QCOMPARE(mcp(access, rpc(2, u"tools/list"_s)).status, 401);
        // An expired code.
        const Pkce second = makePkce();
        allowed = signInAndDecide(authorize(client, second));
        m_now = m_now.addSecs(61);
        r = exchange(client, locationQuery(allowed).value(u"code"_s), second);
        QCOMPARE(r.json.value(u"error"_s).toString(), u"invalid_grant"_s);
    }

    void legacySession()
    {
        startServer();
        const Tokens t = connectClient();
        QVERIFY(!t.access.isEmpty());
        HttpResult r = mcp(t.access, rpc(1, u"initialize"_s, {{u"protocolVersion"_s, u"2025-11-25"_s}}));
        QCOMPARE(r.status, 200);
        QCOMPARE(r.json.value(u"result"_s).toObject().value(u"protocolVersion"_s).toString(), u"2025-11-25"_s);
        QVERIFY(r.json.value(u"result"_s).toObject().value(u"instructions"_s).toString().contains(u"\"Notatki\""_s));
        QVERIFY(!r.json.value(u"result"_s).toObject().value(u"instructions"_s).toString().contains(m_vault));
        const QByteArray session = r.header("Mcp-Session-Id");
        QVERIFY(session.size() >= 32);
        const QList<std::pair<QByteArray, QByteArray>> inSession{{"Mcp-Session-Id"_ba, session},
                                                                 {"MCP-Protocol-Version"_ba, "2025-11-25"_ba}};
        r = mcp(t.access, {{u"jsonrpc"_s, u"2.0"_s}, {u"method"_s, u"notifications/initialized"_s}}, inSession);
        QCOMPARE(r.status, 202);
        QVERIFY(r.body.isEmpty());
        // Without the session, or with an unknown one.
        r = mcp(t.access, rpc(2, u"tools/list"_s));
        QCOMPARE(r.status, 400);
        r = mcp(t.access, rpc(2, u"tools/list"_s), {{"Mcp-Session-Id"_ba, "nieznana"_ba}});
        QCOMPARE(r.status, 404);
        r = mcp(t.access, rpc(2, u"tools/list"_s), {{"Mcp-Session-Id"_ba, session}, {"MCP-Protocol-Version"_ba, "2025-03-26"_ba}});
        QCOMPARE(r.status, 400);
        r = mcp(t.access, rpc(2, u"tools/list"_s), inSession);
        QCOMPARE(r.status, 200);
        const QStringList tools = toolNames(r);
        QVERIFY(tools.contains(u"brulion_search"_s));
        QVERIFY(tools.contains(u"brulion_capture"_s));
        QVERIFY(!tools.contains(u"brulion_page_delete"_s)); // destructive: not without --allow-destructive
        QVERIFY(!tools.contains(u"brulion_app_open"_s));    // the app's tools never over the network
        QVERIFY(!tools.contains(u"brulion_app_screenshot"_s));
        r = mcp(t.access, rpc(3, u"tools/call"_s, {{u"name"_s, u"brulion_search"_s},
                                                   {u"arguments"_s, QJsonObject{{u"text"_s, u"zolw blotny"_s}}}}),
                inSession);
        QCOMPARE(r.status, 200);
        const QJsonObject call = r.json.value(u"result"_s).toObject();
        QVERIFY(!call.value(u"isError"_s).toBool());
        const QJsonArray hits =
                call.value(u"structuredContent"_s).toObject().value(u"result"_s).toObject().value(u"hits"_s).toArray();
        QCOMPARE(hits.size(), 1);
        QVERIFY(hits.first().toObject().value(u"excerpt"_s).toString().contains(u"żółw błotny"_s));
        r = mcp(t.access, rpc(4, u"tools/call"_s, {{u"name"_s, u"brulion_capture"_s},
                                                   {u"arguments"_s, QJsonObject{{u"text"_s, u"Nakarmić żółwia"_s}}}}),
                inSession);
        QVERIFY(!r.json.value(u"result"_s).toObject().value(u"isError"_s).toBool());
        QFile journal(m_vault + u"/journals/2026-10-14.md"_s);
        QVERIFY(journal.open(QIODevice::ReadOnly));
        QVERIFY(journal.readAll().contains("Nakarmić żółwia"));
        r = mcp(t.access, rpc(5, u"tools/call"_s, {{u"name"_s, u"brulion_app_open"_s}}), inSession);
        QCOMPARE(r.json.value(u"error"_s).toObject().value(u"code"_s).toInt(), -32602);
        r = mcp(t.access, rpc(6, u"resources/list"_s), inSession);
        QCOMPARE(r.json.value(u"error"_s).toObject().value(u"code"_s).toInt(), -32601);
        // An SSE answer for a client that takes only that.
        auto sse = inSession;
        r = postJson(u"/mcp"_s, rpc(7, u"ping"_s),
                     {{"Authorization"_ba, "Bearer "_ba + t.access.toLatin1()}, {"Accept"_ba, "text/event-stream"_ba},
                      {"Mcp-Session-Id"_ba, session}});
        QCOMPARE(r.status, 200);
        QCOMPARE(r.header("Content-Type"), "text/event-stream"_ba);
        QVERIFY(r.body.startsWith("event: message\ndata: {"));
        QVERIFY(r.body.endsWith("}\n\n"));
        // No stream of the server's own.
        QCOMPARE(send("GET"_ba, u"/mcp"_s, {}, {{"Authorization"_ba, "Bearer "_ba + t.access.toLatin1()}}).status, 405);
        // Malformed bodies.
        r = send("POST"_ba, u"/mcp"_s, "{nie json"_ba,
                 {{"Authorization"_ba, "Bearer "_ba + t.access.toLatin1()}, {"Content-Type"_ba, "application/json"_ba}});
        QCOMPARE(r.status, 400);
        QCOMPARE(r.json.value(u"error"_s).toObject().value(u"code"_s).toInt(), -32700);
        r = send("POST"_ba, u"/mcp"_s, "[]"_ba,
                 {{"Authorization"_ba, "Bearer "_ba + t.access.toLatin1()}, {"Content-Type"_ba, "application/json"_ba}});
        QCOMPARE(r.status, 400);
        r = send("POST"_ba, u"/mcp"_s, "{}"_ba,
                 {{"Authorization"_ba, "Bearer "_ba + t.access.toLatin1()}, {"Content-Type"_ba, "text/plain"_ba}});
        QCOMPARE(r.status, 415);
        r = postJson(u"/mcp"_s, rpc(8, u"ping"_s),
                     {{"Authorization"_ba, "Bearer "_ba + t.access.toLatin1()}, {"Accept"_ba, "text/html"_ba}});
        QCOMPARE(r.status, 406);
        // DELETE ends the session.
        QCOMPARE(send("DELETE"_ba, u"/mcp"_s, {}, {{"Authorization"_ba, "Bearer "_ba + t.access.toLatin1()},
                                                   {"Mcp-Session-Id"_ba, session}})
                         .status,
                 204);
        QCOMPARE(mcp(t.access, rpc(9, u"tools/list"_s), inSession).status, 404);
        // Another connection cannot use this one's session.
        const QByteArray mine = initialize(t.access);
        const Tokens other = connectClient();
        QCOMPARE(mcp(other.access, rpc(10, u"tools/list"_s), {{"Mcp-Session-Id"_ba, mine}}).status, 404);
        QCOMPARE(send("DELETE"_ba, u"/mcp"_s, {}, {{"Authorization"_ba, "Bearer "_ba + other.access.toLatin1()},
                                                   {"Mcp-Session-Id"_ba, mine}})
                         .status,
                 404);
    }

    void readingConnection()
    {
        startServer();
        const Tokens t = connectClient(false);
        QCOMPARE(t.scope, u"notes:read"_s);
        const QByteArray session = initialize(t.access);
        HttpResult r = mcp(t.access, rpc(2, u"tools/list"_s), {{"Mcp-Session-Id"_ba, session}});
        const QStringList tools = toolNames(r);
        QVERIFY(tools.contains(u"brulion_search"_s));
        QVERIFY(!tools.contains(u"brulion_capture"_s));
        QVERIFY(!tools.contains(u"brulion_task_add"_s));
        // A tool that writes: 403 with the scope it needs (the client may ask the owner again).
        r = mcp(t.access, rpc(3, u"tools/call"_s, {{u"name"_s, u"brulion_capture"_s},
                                                   {u"arguments"_s, QJsonObject{{u"text"_s, u"x"_s}}}}),
                {{"Mcp-Session-Id"_ba, session}});
        QCOMPARE(r.status, 403);
        QVERIFY(r.header("WWW-Authenticate").contains("error=\"insufficient_scope\""));
        QVERIFY(r.header("WWW-Authenticate").contains("scope=\"notes:read notes:write\""));
        // A refresh cannot widen it.
        r = postForm(u"/token"_s, {{u"grant_type"_s, u"refresh_token"_s},
                                   {u"refresh_token"_s, t.refresh},
                                   {u"client_id"_s, t.clientId},
                                   {u"scope"_s, u"notes:read notes:write"_s}});
        QCOMPARE(r.json.value(u"error"_s).toString(), u"invalid_scope"_s);
    }

    void readOnlyServer()
    {
        RemoteOptions options;
        options.readOnly = true;
        startServer(options);
        HttpResult r = send("GET"_ba, u"/.well-known/oauth-protected-resource"_s);
        QCOMPARE(r.json.value(u"scopes_supported"_s).toArray(), QJsonArray{u"notes:read"_s});
        const QString client = registerClient();
        const Pkce pkce = makePkce();
        const HttpResult page = authorize(client, pkce);
        r = postForm(u"/authorize"_s, {{u"request"_s, hidden(page.body, "request")},
                                       {u"csrf"_s, hidden(page.body, "csrf")},
                                       {u"action"_s, u"signin"_s},
                                       {u"password"_s, kPassword}});
        QVERIFY(r.body.contains("read-only"));
        QVERIFY(!r.body.contains("name=\"write\""));
        r = postForm(u"/authorize"_s, {{u"request"_s, hidden(page.body, "request")},
                                       {u"csrf"_s, hidden(r.body, "csrf")},
                                       {u"action"_s, u"allow"_s},
                                       {u"write"_s, u"on"_s}}); // asked for anyway
        r = exchange(client, locationQuery(r).value(u"code"_s), pkce);
        QCOMPARE(r.json.value(u"scope"_s).toString(), u"notes:read"_s);
        const QString access = r.json.value(u"access_token"_s).toString();
        const QByteArray session = initialize(access);
        QVERIFY(!toolNames(mcp(access, rpc(2, u"tools/list"_s), {{"Mcp-Session-Id"_ba, session}}))
                         .contains(u"brulion_capture"_s));
        r = mcp(access, rpc(3, u"tools/call"_s, {{u"name"_s, u"brulion_capture"_s},
                                                 {u"arguments"_s, QJsonObject{{u"text"_s, u"x"_s}}}}),
                {{"Mcp-Session-Id"_ba, session}});
        QCOMPARE(r.status, 200);
        QCOMPARE(r.json.value(u"error"_s).toObject().value(u"code"_s).toInt(), -32602); // no such tool here
    }

    void modernRequests()
    {
        startServer();
        const Tokens t = connectClient();
        const QJsonObject meta{{u"io.modelcontextprotocol/protocolVersion"_s, u"2026-07-28"_s},
                               {u"io.modelcontextprotocol/clientCapabilities"_s, QJsonObject()},
                               {u"io.modelcontextprotocol/clientInfo"_s, QJsonObject{{u"name"_s, u"test"_s}}}};
        const auto request = [&](int id, const QString &method, QJsonObject params,
                                 QList<std::pair<QByteArray, QByteArray>> headers) {
            params.insert(u"_meta"_s, meta);
            return mcp(t.access, rpc(id, method, params), headers);
        };
        const QList<std::pair<QByteArray, QByteArray>> discover{{"MCP-Protocol-Version"_ba, "2026-07-28"_ba},
                                                                {"Mcp-Method"_ba, "server/discover"_ba}};
        HttpResult r = request(1, u"server/discover"_s, {}, discover);
        QCOMPARE(r.status, 200);
        QJsonObject result = r.json.value(u"result"_s).toObject();
        QCOMPARE(result.value(u"resultType"_s).toString(), u"complete"_s);
        QVERIFY(result.value(u"supportedVersions"_s).toArray().contains(u"2026-07-28"_s));
        QVERIFY(result.value(u"ttlMs"_s).toInteger() > 0);
        QCOMPARE(result.value(u"cacheScope"_s).toString(), u"private"_s);
        QVERIFY(result.value(u"_meta"_s).toObject().contains(u"io.modelcontextprotocol/serverInfo"_s));
        QVERIFY(r.header("Mcp-Session-Id").isEmpty()); // no sessions in this era
        r = request(2, u"tools/list"_s, {},
                    {{"MCP-Protocol-Version"_ba, "2026-07-28"_ba}, {"Mcp-Method"_ba, "tools/list"_ba},
                     {"Mcp-Session-Id"_ba, "ignored"_ba}});
        QCOMPARE(r.status, 200);
        QVERIFY(toolNames(r).contains(u"brulion_search"_s));
        // The tool's name may come Base64-encoded (=?base64?…?=).
        r = request(3, u"tools/call"_s,
                    {{u"name"_s, u"brulion_search"_s}, {u"arguments"_s, QJsonObject{{u"text"_s, u"żółw"_s}}}},
                    {{"MCP-Protocol-Version"_ba, "2026-07-28"_ba}, {"Mcp-Method"_ba, "tools/call"_ba},
                     {"Mcp-Name"_ba, "=?base64?" + "brulion_search"_ba.toBase64() + "?="}});
        QCOMPARE(r.status, 200);
        QCOMPARE(r.json.value(u"result"_s).toObject().value(u"resultType"_s).toString(), u"complete"_s);
        // Headers that differ from the body, or are missing.
        const auto mismatch = [&](const QString &method, const QJsonObject &params,
                                  const QList<std::pair<QByteArray, QByteArray>> &headers) {
            const HttpResult res = request(4, method, params, headers);
            QCOMPARE(res.status, 400);
            QCOMPARE(res.json.value(u"error"_s).toObject().value(u"code"_s).toInt(), -32020);
        };
        mismatch(u"tools/list"_s, {}, {{"Mcp-Method"_ba, "tools/list"_ba}});
        mismatch(u"tools/list"_s, {}, {{"MCP-Protocol-Version"_ba, "2025-11-25"_ba}, {"Mcp-Method"_ba, "tools/list"_ba}});
        mismatch(u"tools/list"_s, {}, {{"MCP-Protocol-Version"_ba, "2026-07-28"_ba}});
        mismatch(u"tools/list"_s, {}, {{"MCP-Protocol-Version"_ba, "2026-07-28"_ba}, {"Mcp-Method"_ba, "tools/call"_ba}});
        mismatch(u"tools/call"_s, {{u"name"_s, u"brulion_search"_s}},
                 {{"MCP-Protocol-Version"_ba, "2026-07-28"_ba}, {"Mcp-Method"_ba, "tools/call"_ba}});
        mismatch(u"tools/call"_s, {{u"name"_s, u"brulion_search"_s}},
                 {{"MCP-Protocol-Version"_ba, "2026-07-28"_ba}, {"Mcp-Method"_ba, "tools/call"_ba},
                  {"Mcp-Name"_ba, "brulion_capture"_ba}});
        // A version this server does not speak, a missing capability object, an unknown method.
        QJsonObject old = meta;
        old.insert(u"io.modelcontextprotocol/protocolVersion"_s, u"1900-01-01"_s);
        r = mcp(t.access, rpc(5, u"tools/list"_s, {{u"_meta"_s, old}}),
                {{"MCP-Protocol-Version"_ba, "1900-01-01"_ba}, {"Mcp-Method"_ba, "tools/list"_ba}});
        QCOMPARE(r.status, 400);
        QCOMPARE(r.json.value(u"error"_s).toObject().value(u"code"_s).toInt(), -32022);
        QVERIFY(r.json.value(u"error"_s).toObject().value(u"data"_s).toObject().value(u"supported"_s).toArray().contains(
                u"2026-07-28"_s));
        QJsonObject noCapabilities = meta;
        noCapabilities.remove(u"io.modelcontextprotocol/clientCapabilities"_s);
        r = mcp(t.access, rpc(6, u"tools/list"_s, {{u"_meta"_s, noCapabilities}}),
                {{"MCP-Protocol-Version"_ba, "2026-07-28"_ba}, {"Mcp-Method"_ba, "tools/list"_ba}});
        QCOMPARE(r.status, 400);
        QCOMPARE(r.json.value(u"error"_s).toObject().value(u"code"_s).toInt(), -32602);
        r = request(7, u"prompts/list"_s, {}, {{"MCP-Protocol-Version"_ba, "2026-07-28"_ba}, {"Mcp-Method"_ba, "prompts/list"_ba}});
        QCOMPARE(r.status, 404);
        QCOMPARE(r.json.value(u"error"_s).toObject().value(u"code"_s).toInt(), -32601);
        // A client trying the new era first gets no modern error from a legacy-looking request
        // without a session: 400 that is no -32020/-32022, so it falls back to initialize.
        r = mcp(t.access, rpc(8, u"tools/list"_s));
        QCOMPARE(r.status, 400);
        QCOMPARE(r.json.value(u"error"_s).toObject().value(u"code"_s).toInt(), -32600);
    }

    void refreshRotation()
    {
        startServer();
        const Tokens t = connectClient();
        const auto refresh = [&](const QString &token) {
            return postForm(u"/token"_s, {{u"grant_type"_s, u"refresh_token"_s},
                                          {u"refresh_token"_s, token},
                                          {u"client_id"_s, t.clientId}});
        };
        HttpResult r = refresh(t.refresh);
        QCOMPARE(r.status, 200);
        const QString access2 = r.json.value(u"access_token"_s).toString();
        const QString refresh2 = r.json.value(u"refresh_token"_s).toString();
        QVERIFY(refresh2 != t.refresh);
        QCOMPARE(r.json.value(u"scope"_s).toString(), u"notes:read notes:write"_s);
        // The old access token works until it expires; so does the new one.
        QVERIFY(!initialize(t.access).isEmpty());
        QVERIFY(!initialize(access2).isEmpty());
        // Another client cannot use it.
        r = postForm(u"/token"_s, {{u"grant_type"_s, u"refresh_token"_s},
                                   {u"refresh_token"_s, refresh2},
                                   {u"client_id"_s, registerClient()}});
        QCOMPARE(r.json.value(u"error"_s).toString(), u"invalid_grant"_s);
        // The previous one again within seconds (the answer was lost): a new pair, once more.
        m_now = m_now.addSecs(5);
        r = refresh(t.refresh);
        QCOMPARE(r.status, 200);
        const QString refresh3 = r.json.value(u"refresh_token"_s).toString();
        // refresh2 was never used and is replaced; refresh3 works.
        r = refresh(refresh3);
        QCOMPARE(r.status, 200);
        const QString refresh4 = r.json.value(u"refresh_token"_s).toString();
        const QString access4 = r.json.value(u"access_token"_s).toString();
        // A replaced token after the grace period: stolen — the whole connection is revoked.
        m_now = m_now.addSecs(60);
        r = refresh(t.refresh);
        QCOMPARE(r.status, 400);
        QCOMPARE(r.json.value(u"error"_s).toString(), u"invalid_grant"_s);
        QVERIFY(!m_log.filter(u"used again"_s).isEmpty());
        QCOMPARE(refresh(refresh4).json.value(u"error"_s).toString(), u"invalid_grant"_s);
        QCOMPARE(mcp(access4, rpc(1, u"tools/list"_s)).status, 401);
        QCOMPARE(refresh(u"brt_nieznany"_s).json.value(u"error"_s).toString(), u"invalid_grant"_s);
    }

    void expiryAndRevocation()
    {
        startServer();
        const Tokens t = connectClient();
        QVERIFY(!initialize(t.access).isEmpty());
        m_now = m_now.addSecs(3601);
        HttpResult r = mcp(t.access, rpc(1, u"tools/list"_s));
        QCOMPARE(r.status, 401);
        QVERIFY(r.header("WWW-Authenticate").contains("error=\"invalid_token\""));
        r = postForm(u"/token"_s, {{u"grant_type"_s, u"refresh_token"_s},
                                   {u"refresh_token"_s, t.refresh},
                                   {u"client_id"_s, t.clientId}});
        QCOMPARE(r.status, 200);
        const QString access = r.json.value(u"access_token"_s).toString();
        QVERIFY(!initialize(access).isEmpty());
        // `mcp revoke` (another process changing the state file) works at once.
        oauth::Store other(m_authFile);
        QString error;
        QVERIFY(other.update(
                [](oauth::State &s) {
                    s.grants.clear();
                    return true;
                },
                &error));
        QCOMPARE(mcp(access, rpc(2, u"tools/list"_s)).status, 401);
        // An unused connection ends after the refresh token's lifetime.
        const Tokens second = connectClient();
        m_now = m_now.addDays(61);
        r = postForm(u"/token"_s, {{u"grant_type"_s, u"refresh_token"_s},
                                   {u"refresh_token"_s, second.refresh},
                                   {u"client_id"_s, second.clientId}});
        QCOMPARE(r.json.value(u"error"_s).toString(), u"invalid_grant"_s);
    }

    void metadataDocumentClients()
    {
        startServer();
        const QString clientId = u"https://claude.ai/oauth/mcp-oauth-client-metadata"_s;
        QJsonObject document{{u"client_id"_s, clientId},
                             {u"client_name"_s, u"Claude"_s},
                             {u"redirect_uris"_s, QJsonArray{kClaudeRedirect}},
                             {u"token_endpoint_auth_method"_s, u"none"_s}};
        int fetches = 0;
        QString fetched;
        m_server->auth()->fetchMetadata = [&](const QUrl &url, oauth::MetadataDone done) {
            ++fetches;
            fetched = url.toString();
            if (document.isEmpty())
                done(std::nullopt, u"HTTP 404"_s, 0);
            else
                done(document, QString(), 300);
        };
        const Pkce pkce = makePkce();
        HttpResult page = authorize(clientId, pkce);
        QCOMPARE(page.status, 200);
        QVERIFY(page.body.contains("<strong>claude.ai</strong>")); // its host until the document is read
        QCOMPARE(fetches, 0);                                       // nothing fetched before sign-in
        HttpResult r = signInAndDecide(page);
        QCOMPARE(fetches, 1);
        QCOMPARE(fetched, clientId);
        QCOMPARE(r.status, 303);
        const QString code = locationQuery(r).value(u"code"_s);
        r = exchange(clientId, code, pkce);
        QCOMPARE(r.status, 200);
        QVERIFY(!initialize(r.json.value(u"access_token"_s).toString()).isEmpty());
        // The consent page names it by its document, and the document is kept a while.
        page = authorize(clientId, pkce);
        r = postForm(u"/authorize"_s, {{u"request"_s, hidden(page.body, "request")},
                                       {u"csrf"_s, hidden(page.body, "csrf")},
                                       {u"action"_s, u"signin"_s},
                                       {u"password"_s, kPassword}});
        QVERIFY(r.body.contains("<strong>Claude</strong>"));
        QVERIFY(r.body.contains("described at claude.ai"));
        QCOMPARE(fetches, 1);
        // A return address the document does not list, another client_id in it, a failed fetch.
        m_now = m_now.addSecs(3600);
        r = signInAndDecide(authorize(clientId, pkce, u"https://claude.ai/inny"_s));
        QCOMPARE(r.status, 400);
        QVERIFY(r.body.contains("redirect_uris"));
        QVERIFY(!r.headers.contains("location"));
        document.insert(u"client_id"_s, u"https://evil.example/c.json"_s);
        m_now = m_now.addSecs(3600);
        r = signInAndDecide(authorize(clientId, pkce));
        QCOMPARE(r.status, 400);
        document = {};
        m_now = m_now.addSecs(3600);
        r = signInAndDecide(authorize(clientId, pkce));
        QCOMPARE(r.status, 400);
        QVERIFY(r.body.contains("HTTP 404"));
        // An address that is no document.
        r = authorize(u"https://10.0.0.1/c.json"_s, pkce);
        QCOMPARE(r.status, 400);
    }

    void confidentialClient()
    {
        startServer();
        HttpResult r = postJson(u"/register"_s, {{u"redirect_uris"_s, QJsonArray{kClaudeRedirect}},
                                                 {u"client_name"_s, u"Poufny"_s},
                                                 {u"token_endpoint_auth_method"_s, u"client_secret_basic"_s}});
        QCOMPARE(r.status, 201);
        const QString id = r.json.value(u"client_id"_s).toString();
        const QString secret = r.json.value(u"client_secret"_s).toString();
        QVERIFY(secret.startsWith(u"bcs_"_s));
        const Pkce pkce = makePkce();
        const QString code = locationQuery(signInAndDecide(authorize(id, pkce))).value(u"code"_s);
        const QList<std::pair<QString, QString>> fields{{u"grant_type"_s, u"authorization_code"_s},
                                                        {u"code"_s, code},
                                                        {u"redirect_uri"_s, kClaudeRedirect},
                                                        {u"code_verifier"_s, pkce.verifier}};
        auto withId = fields;
        withId.append({u"client_id"_s, id});
        r = postForm(u"/token"_s, withId);
        QCOMPARE(r.status, 401); // no secret
        r = postForm(u"/token"_s, fields, {{"Authorization"_ba, "Basic "_ba + (id + u":bcs_zly"_s).toLatin1().toBase64()}});
        QCOMPARE(r.status, 401);
        QVERIFY(r.header("WWW-Authenticate").startsWith("Basic"));
        r = postForm(u"/token"_s, fields, {{"Authorization"_ba, "Basic "_ba + (id + u':' + secret).toLatin1().toBase64()}});
        QCOMPARE(r.status, 200);
        QVERIFY(!r.json.value(u"access_token"_s).toString().isEmpty());
    }

    void revocationEndpoint()
    {
        startServer();
        const Tokens t = connectClient();
        QVERIFY(!initialize(t.access).isEmpty());
        // Another client cannot revoke it (200 all the same, RFC 7009).
        HttpResult r = postForm(u"/revoke"_s, {{u"token"_s, t.refresh}, {u"client_id"_s, registerClient()}});
        QCOMPARE(r.status, 200);
        QVERIFY(!initialize(t.access).isEmpty());
        r = postForm(u"/revoke"_s, {{u"token"_s, t.refresh}, {u"client_id"_s, t.clientId}});
        QCOMPARE(r.status, 200);
        QCOMPARE(mcp(t.access, rpc(1, u"tools/list"_s)).status, 401);
    }

    void restartKeepsConnections()
    {
        // The same public URL (the tokens' audience) on another port.
        RemoteOptions options;
        options.publicUrl = u"http://localhost:9"_s;
        startServer(options);
        m_base = u"http://127.0.0.1:%1"_s.arg(m_server->port());
        const Tokens t = connectClient();
        startServer(options, false); // the same state file
        m_base = u"http://127.0.0.1:%1"_s.arg(m_server->port());
        QVERIFY(!initialize(t.access).isEmpty());
        const HttpResult r = postForm(u"/token"_s, {{u"grant_type"_s, u"refresh_token"_s},
                                                    {u"refresh_token"_s, t.refresh},
                                                    {u"client_id"_s, t.clientId}});
        QCOMPARE(r.status, 200);
    }

    void originAndHost()
    {
        RemoteOptions options;
        options.allowedOrigins = {u"https://inspector.example"_s};
        startServer(options);
        const Tokens t = connectClient();
        const QByteArray bearer = "Bearer "_ba + t.access.toLatin1();
        HttpResult r = mcp(t.access, rpc(1, u"initialize"_s), {{"Origin"_ba, "https://evil.example"_ba}});
        QCOMPARE(r.status, 403);
        QVERIFY(r.json.contains(u"error"_s));
        QCOMPARE(mcp(t.access, rpc(1, u"initialize"_s), {{"Origin"_ba, "null"_ba}}).status, 403);
        r = mcp(t.access, rpc(1, u"initialize"_s), {{"Origin"_ba, "https://inspector.example"_ba}});
        QCOMPARE(r.status, 200);
        QCOMPARE(r.header("Access-Control-Allow-Origin"), "https://inspector.example"_ba);
        QVERIFY(r.header("Access-Control-Expose-Headers").contains("Mcp-Session-Id"));
        r = send("OPTIONS"_ba, u"/mcp"_s, {},
                 {{"Origin"_ba, "https://inspector.example"_ba}, {"Access-Control-Request-Method"_ba, "POST"_ba}});
        QCOMPARE(r.status, 204);
        QVERIFY(r.header("Access-Control-Allow-Headers").contains("Authorization"));
        QCOMPARE(send("OPTIONS"_ba, u"/mcp"_s, {}, {{"Origin"_ba, "https://evil.example"_ba}}).status, 403);
        // Metadata is public: any origin may read it.
        r = send("GET"_ba, u"/.well-known/oauth-protected-resource"_s, {}, {{"Origin"_ba, "https://evil.example"_ba}});
        QCOMPARE(r.status, 200);
        QCOMPARE(r.header("Access-Control-Allow-Origin"), "*"_ba);
        // A host name it does not answer for (DNS rebinding).
        const QByteArray port = QByteArray::number(m_server->port());
        QByteArray answer = raw("GET / HTTP/1.1\r\nHost: evil.example:" + port + "\r\nConnection: close\r\n\r\n");
        QVERIFY2(answer.startsWith("HTTP/1.1 421 "), answer.constData());
        const QByteArray list = R"({"jsonrpc":"2.0","id":1,"method":"tools/list"})";
        answer = raw("POST /mcp HTTP/1.1\r\nHost: localhost:" + port + "\r\nAuthorization: " + bearer
                     + "\r\nContent-Type: application/json\r\nContent-Length: " + QByteArray::number(list.size())
                     + "\r\nConnection: close\r\n\r\n" + list);
        QVERIFY2(answer.startsWith("HTTP/1.1 400 "), answer.constData()); // past the checks: no session
    }

    void malformedAndLimits()
    {
        RemoteOptions options;
        options.maxBodyBytes = 64 * 1024;
        options.limits.headerTimeoutMs = 300;
        options.limits.maxConnectionsPerPeer = 4;
        startServer(options);
        const Tokens t = connectClient();
        // The client's own connections closed: the counts below are the raw sockets' alone.
        delete m_nam;
        m_nam = new QNetworkAccessManager(this);
        QVERIFY(QTest::qWaitFor([&] { return m_server->server()->connectionCount() == 0; }, kTimeoutMs));
        const QByteArray host = "Host: 127.0.0.1:"_ba + QByteArray::number(m_server->port()) + "\r\n";
        bool closed = false;
        QByteArray answer = raw("\x16\x03\x01\x00\xa5\x01\x00\x00\xa1\x03\x03garbage\r\n\r\n"_ba, {}, &closed);
        QVERIFY(answer.startsWith("HTTP/1.1 400 "));
        QVERIFY(closed);
        answer = raw("GET / HTTP/1.1\r\n" + host + "X: " + QByteArray(40000, 'a') + "\r\n\r\n", {}, &closed);
        QVERIFY(answer.startsWith("HTTP/1.1 431 "));
        answer = raw("POST /mcp HTTP/1.1\r\n" + host + "Authorization: Bearer " + t.access.toLatin1()
                             + "\r\nContent-Type: application/json\r\nContent-Length: 70000\r\n\r\n",
                     {}, &closed);
        QVERIFY2(answer.startsWith("HTTP/1.1 413 "), answer.constData());
        QVERIFY(closed);
        answer = raw("POST /mcp HTTP/1.1\r\n" + host + "Authorization: Bearer " + t.access.toLatin1()
                             + "\r\nContent-Type: application/json\r\nTransfer-Encoding: chunked\r\n\r\n10000\r\n"
                             + QByteArray(0x10000, ' ') + "\r\n10000\r\n",
                     {}, &closed);
        QVERIFY2(answer.startsWith("HTTP/1.1 413 "), answer.left(200).constData());
        // A request that never finishes its header fields (slowloris): 408 and closed.
        answer = raw("GET / HTTP/1.1\r\n" + host + "X-Slow: 1\r\n", {}, &closed);
        QVERIFY2(answer.startsWith("HTTP/1.1 408 "), answer.constData());
        QVERIFY(closed);
        // Keep-alive and pipelining: two requests in one write, two answers in order.
        answer = raw("GET /.well-known/oauth-protected-resource HTTP/1.1\r\n" + host
                             + "\r\nGET /nie HTTP/1.1\r\n" + host + "Connection: close\r\n\r\n",
                     {}, &closed);
        QVERIFY(answer.startsWith("HTTP/1.1 200 "));
        QVERIFY(answer.contains("\r\n\r\n{\"authorization_servers\""));
        QVERIFY(answer.contains("HTTP/1.1 404 "));
        QVERIFY(answer.indexOf("HTTP/1.1 200") < answer.indexOf("HTTP/1.1 404"));
        // A small body refused from its header fields is read and dropped: the connection goes on.
        answer = raw("POST /mcp HTTP/1.1\r\n" + host + "Content-Length: 2\r\n\r\n{}GET /nie HTTP/1.1\r\n" + host
                             + "Connection: close\r\n\r\n",
                     {}, &closed);
        QVERIFY2(answer.startsWith("HTTP/1.1 401 "), answer.constData());
        QVERIFY(answer.contains("HTTP/1.1 404 "));
        // Expect: 100-continue.
        answer = raw("POST /token HTTP/1.1\r\n" + host
                             + "Content-Type: application/x-www-form-urlencoded\r\nContent-Length: 3\r\nExpect: "
                               "100-continue\r\nConnection: close\r\n\r\na=1",
                     {}, &closed);
        QVERIFY2(answer.startsWith("HTTP/1.1 100 Continue\r\n\r\nHTTP/1.1 4"), answer.constData());
        // More connections from one address than allowed: the extra ones get 503.
        QList<QTcpSocket *> sockets;
        for (int i = 0; i < 6; ++i) {
            auto *s = new QTcpSocket(this);
            s->connectToHost(QHostAddress::LocalHost, m_server->port());
            sockets.append(s);
        }
        QVERIFY(QTest::qWaitFor(
                [&] {
                    return std::ranges::all_of(sockets, [](QTcpSocket *s) {
                        return s->state() == QAbstractSocket::ConnectedState
                               || s->state() == QAbstractSocket::UnconnectedState;
                    });
                },
                kTimeoutMs));
        QVERIFY(QTest::qWaitFor([&] { return m_server->server()->connectionCount() >= 4; }, kTimeoutMs));
        QByteArray refused;
        (void)QTest::qWaitFor(
                [&] {
                    refused.clear();
                    for (QTcpSocket *s : std::as_const(sockets))
                        refused += s->peek(64);
                    return refused.count("HTTP/1.1 503 ") >= 2;
                },
                kTimeoutMs);
        QCOMPARE(refused.count("HTTP/1.1 503 "), 2);
        qDeleteAll(sockets);
        // Closed by the client: the server lets go of them.
        QVERIFY(QTest::qWaitFor([&] { return m_server->server()->connectionCount() == 0; }, kTimeoutMs));
    }

    void concurrentClients()
    {
        startServer();
        const Tokens t = connectClient();
        const QByteArray session = initialize(t.access);
        QList<QNetworkReply *> replies;
        for (int i = 0; i < 12; ++i) {
            QNetworkRequest request(QUrl(m_server->endpoint()));
            request.setRawHeader("Authorization"_ba, "Bearer "_ba + t.access.toLatin1());
            request.setRawHeader("Content-Type"_ba, "application/json"_ba);
            request.setRawHeader("Mcp-Session-Id"_ba, session);
            const QJsonObject call = rpc(100 + i, u"tools/call"_s,
                                         {{u"name"_s, u"brulion_search"_s},
                                          {u"arguments"_s, QJsonObject{{u"text"_s, u"błotny"_s}}}});
            replies.append(m_nam->post(request, QJsonDocument(call).toJson(QJsonDocument::Compact)));
        }
        QVERIFY(QTest::qWaitFor([&] { return std::ranges::all_of(replies, &QNetworkReply::isFinished); }, kTimeoutMs));
        QSet<int> ids;
        for (QNetworkReply *reply : std::as_const(replies)) {
            QCOMPARE(reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt(), 200);
            const QJsonObject o = QJsonDocument::fromJson(reply->readAll()).object();
            ids.insert(o.value(u"id"_s).toInt());
            QCOMPARE(o.value(u"result"_s)
                             .toObject()
                             .value(u"structuredContent"_s)
                             .toObject()
                             .value(u"result"_s)
                             .toObject()
                             .value(u"hits"_s)
                             .toArray()
                             .size(),
                     1);
            reply->deleteLater();
        }
        QCOMPARE(ids.size(), 12); // each answer to its own request
    }

    void https()
    {
#if QT_CONFIG(ssl)
        if (!QSslSocket::supportsSsl() || QSslSocket::activeBackend() != u"openssl")
            QSKIP("HTTPS is tested with Qt's OpenSSL backend");
        const QString data = QStringLiteral(BRULION_TEST_DATA) + u"/mcp-remote/"_s;
        const QString cert = m_tmp.filePath(u"cert.pem"_s);
        const QString key = m_tmp.filePath(u"key.pem"_s);
        QFile::remove(cert);
        QFile::remove(key);
        QVERIFY(QFile::copy(data + u"cert1.pem"_s, cert));
        QVERIFY(QFile::copy(data + u"key1.pem"_s, key));
        RemoteOptions options;
        options.tlsCertificate = cert;
        options.tlsKey = key;
        options.maintenanceIntervalMs = 100;
        startServer(options);
        QVERIFY(m_base.startsWith(u"https://127.0.0.1:"_s));
        QVERIFY(m_server->server()->tls());
        const auto fetchCertificate = [&](const QString &trusted) -> QSslCertificate {
            QSslConfiguration tls = QSslConfiguration::defaultConfiguration();
            QFile f(trusted);
            if (!f.open(QIODevice::ReadOnly))
                return QSslCertificate();
            tls.setCaCertificates(QSslCertificate::fromData(f.readAll()));
            QNetworkRequest request(QUrl(m_base + u"/.well-known/oauth-protected-resource"_s));
            request.setSslConfiguration(tls);
            QNetworkAccessManager nam;
            QNetworkReply *reply = nam.get(request);
            (void)QTest::qWaitFor([reply] { return reply->isFinished(); }, kTimeoutMs);
            const QSslCertificate peer = reply->sslConfiguration().peerCertificate();
            const bool ok = reply->error() == QNetworkReply::NoError
                            && QJsonDocument::fromJson(reply->readAll()).object().contains(u"resource"_s);
            delete reply;
            return ok ? peer : QSslCertificate();
        };
        const QSslCertificate first = fetchCertificate(data + u"cert1.pem"_s);
        QVERIFY(!first.isNull());
        // A plain-HTTP request to the HTTPS port gets nowhere.
        bool closed = false;
        const QByteArray answer = raw("GET / HTTP/1.1\r\nHost: 127.0.0.1\r\n\r\n"_ba, {}, &closed);
        QVERIFY(!answer.startsWith("HTTP/1.1 200"));
        // A renewed certificate is taken for new connections.
        QFile::remove(cert);
        QFile::remove(key);
        QTest::qWait(20); // another modification time
        QVERIFY(QFile::copy(data + u"cert2.pem"_s, cert));
        QVERIFY(QFile::copy(data + u"key2.pem"_s, key));
        QFile(cert).setFileTime(QDateTime::currentDateTime().addSecs(5), QFileDevice::FileModificationTime);
        QVERIFY(QTest::qWaitFor([&] { return !m_log.filter(u"TLS certificate reloaded"_s).isEmpty(); }, kTimeoutMs));
        const QSslCertificate second = fetchCertificate(data + u"cert2.pem"_s);
        QVERIFY(!second.isNull());
        QVERIFY(second != first);
#else
        QSKIP("no TLS in this Qt");
#endif
    }

    void startChecks()
    {
        startServer(); // a state file with a password
        m_server.reset();
        QString error;
        {
            RemoteOptions options;
            options.authFile = m_tmp.filePath(u"bez-hasła/mcp-remote.json"_s);
            options.port = 0;
            RemoteMcpServer server(*m_ctx, options);
            QVERIFY(!server.start(&error));
            QVERIFY(error.contains(u"password"_s));
        }
        for (const QString &url : {u"http://notes.example.com"_s, u"ftp://a.example"_s, u"https://a.example/?x=1"_s}) {
            RemoteOptions options;
            options.authFile = m_authFile;
            options.port = 0;
            options.publicUrl = url;
            RemoteMcpServer server(*m_ctx, options);
            QVERIFY2(!server.start(&error), qPrintable(url));
        }
        {
            RemoteOptions options;
            options.authFile = m_authFile;
            options.port = 0;
            options.address = QHostAddress(QHostAddress::AnyIPv4);
            RemoteMcpServer server(*m_ctx, options);
            QVERIFY(!server.start(&error));
            QVERIFY(error.contains(u"--public-url"_s));
        }
        {
            // Behind a proxy under a path: the endpoint and the metadata follow it.
            RemoteOptions options;
            options.publicUrl = u"https://notes.example.com/brulion/mcp"_s;
            options.allowedHosts = {u"127.0.0.1"_s};
            startServer(options);
            QCOMPARE(m_server->endpoint(), u"https://notes.example.com/brulion/mcp"_s);
            QCOMPARE(m_server->issuer(), u"https://notes.example.com/brulion"_s);
            m_base = u"http://127.0.0.1:%1"_s.arg(m_server->port());
            HttpResult r = send("GET"_ba, u"/.well-known/oauth-protected-resource/brulion/mcp"_s);
            QCOMPARE(r.json.value(u"resource"_s).toString(), u"https://notes.example.com/brulion/mcp"_s);
            r = send("GET"_ba, u"/brulion/.well-known/openid-configuration"_s);
            QCOMPARE(r.json.value(u"issuer"_s).toString(), u"https://notes.example.com/brulion"_s);
            QCOMPARE(r.json.value(u"token_endpoint"_s).toString(), u"https://notes.example.com/brulion/token"_s);
            r = send("GET"_ba, u"/.well-known/oauth-authorization-server/brulion"_s);
            QCOMPARE(r.status, 200);
            r = send("POST"_ba, u"/brulion/mcp"_s, "{}"_ba, {{"Content-Type"_ba, "application/json"_ba}});
            QCOMPARE(r.status, 401);
            QVERIFY(r.header("WWW-Authenticate").contains(
                    "resource_metadata=\"https://notes.example.com/brulion/.well-known/oauth-protected-resource\""));
            // A proxy that strips the path prefix reaches the same endpoint.
            QCOMPARE(send("POST"_ba, u"/mcp"_s, "{}"_ba, {{"Content-Type"_ba, "application/json"_ba}}).status, 401);
        }
    }
};

QTEST_GUILESS_MAIN(TestCliRemoteMcp)
#include "tst_cli_remote_mcp.moc"
