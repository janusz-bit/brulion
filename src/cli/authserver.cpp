// The authorization server of `brulion-cli mcp serve` (authserver.h). Nothing an unauthenticated
// request sends is written to disk: a client that registers itself lives in memory until the
// owner allows it, and a client ID metadata document is fetched only after the owner signed in.
#include "authserver.h"

#include <QCoreApplication>
#include <QHostAddress>
#include <QHostInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QNetworkAccessManager>
#include <QNetworkCookie>
#include <QNetworkCookieJar>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QPointer>
#include <QThreadPool>
#include <QTranslator>

#include <algorithm>
#include <cctype>

using namespace Qt::StringLiterals;

namespace brulion::cli::oauth {

namespace {

constexpr qint64 kFormBodyLimit = 16 * 1024;
constexpr qint64 kRegistrationBodyLimit = 64 * 1024;
constexpr qint64 kMetadataBytes = 64 * 1024;
constexpr int kMetadataTimeoutMs = 10000;
constexpr int kMaxRedirectUris = 10;
constexpr int kMaxNewClients = 100;
constexpr qint64 kNewClientTtlSeconds = 24 * 3600;
constexpr int kMaxRegistrationsPerWindow = 30;
constexpr qint64 kRegistrationWindowSeconds = 600;
constexpr int kMaxPending = 50;
constexpr int kFreeFailures = 5;
constexpr qint64 kFailureWindowSeconds = 15 * 60;
constexpr qint64 kMaxDelaySeconds = 15 * 60;
constexpr int kMaxFailuresPerSignIn = 5;
constexpr int kMaxAccessTokensPerGrant = 5;
constexpr int kMaxMetadataCache = 32;
const char *const kDocumentation = "https://github.com/janusz-bit/brulion/blob/main/docs/CLI.md#remote-mcp-server";

QString scopeRead()
{
    return QString::fromLatin1(kScopeRead);
}

QString scopeWrite()
{
    return QString::fromLatin1(kScopeWrite);
}

/// The Polish catalogue, for pages shown to people whose browser prefers Polish.
const QTranslator *polishCatalogue()
{
    static QTranslator *catalogue = [] {
        auto *t = new QTranslator(QCoreApplication::instance());
        if (!t->load(u":/i18n/brulion_pl.qm"_s))
            qDebug("brulion-cli mcp serve: no Polish catalogue");
        return t;
    }();
    return catalogue;
}

/// A page text (marked with QT_TRANSLATE_NOOP("brulion::cli", …)) in the page's language.
QString text(const QString &lang, const char *source)
{
    if (lang == u"pl") {
        const QString translated = polishCatalogue()->translate("brulion::cli", source);
        if (!translated.isEmpty())
            return translated;
    }
    return QString::fromUtf8(source);
}

QString esc(const QString &s)
{
    return http::htmlEscape(s);
}

/// A name a client gave itself, safe to show: no controls, one line, at most 80 characters.
QString cleanName(const QString &name)
{
    QString out;
    for (const QChar c : name) {
        // Controls, bidirectional overrides and line breaks become spaces: a name cannot turn
        // itself around or pretend to be two lines.
        if (c.category() == QChar::Other_Control || c.category() == QChar::Other_Format
            || c.category() == QChar::Separator_Line || c.category() == QChar::Separator_Paragraph)
            out += u' ';
        else
            out += c;
    }
    out = out.simplified();
    if (out.size() > 80)
        out = out.left(79) + u'…';
    return out;
}

bool isLoopbackHost(const QString &host)
{
    const QString h = host.toLower();
    return h == u"localhost" || h == u"127.0.0.1" || h == u"::1";
}

http::Response oauthError(int status, const QString &error, const QString &description)
{
    http::Response r = http::Response::json(status, {{u"error"_s, error}, {u"error_description"_s, description}});
    r.headers.set("Pragma"_ba, "no-cache"_ba);
    return r;
}

/// The parameters of a form; nullopt when malformed or a parameter comes twice (RFC 6749 3.1).
std::optional<QHash<QString, QString>> singleValued(QByteArrayView data)
{
    const auto pairs = http::parseForm(data);
    if (!pairs)
        return std::nullopt;
    QHash<QString, QString> out;
    for (const auto &[name, value] : *pairs) {
        if (out.contains(name))
            return std::nullopt;
        out.insert(name, value);
    }
    return out;
}

bool isForm(const http::Request &request)
{
    return request.headers.value("Content-Type")
            .split(';')
            .value(0)
            .trimmed()
            .compare("application/x-www-form-urlencoded", Qt::CaseInsensitive)
           == 0;
}

bool isJson(const http::Request &request)
{
    return request.headers.value("Content-Type").split(';').value(0).trimmed().compare("application/json",
                                                                                        Qt::CaseInsensitive)
           == 0;
}

/// Cache-Control max-age of a response, clamped to a minute … a day (default five minutes).
qint64 maxAgeOf(const QByteArray &cacheControl)
{
    qint64 age = 300;
    for (const QByteArray &part : cacheControl.split(',')) {
        const QByteArray p = part.trimmed().toLower();
        if (p == "no-store" || p == "no-cache")
            return 60;
        if (p.startsWith("max-age=")) {
            bool ok = false;
            const qint64 v = p.mid(8).toLongLong(&ok);
            if (ok)
                age = v;
        }
    }
    return std::clamp<qint64>(age, 60, 86400);
}

/// A cookie jar that keeps nothing: one document's host never gets another's cookies.
class NoCookies : public QNetworkCookieJar {
public:
    using QNetworkCookieJar::QNetworkCookieJar;
    bool setCookiesFromUrl(const QList<QNetworkCookie> &, const QUrl &) override { return false; }
    QList<QNetworkCookie> cookiesForUrl(const QUrl &) const override { return {}; }
};

} // namespace

// ---- Checks

bool isPublicAddress(const QHostAddress &address)
{
    if (address.protocol() == QAbstractSocket::IPv6Protocol) {
        bool mapped = false;
        const quint32 v4 = address.toIPv4Address(&mapped);
        if (mapped)
            return isPublicAddress(QHostAddress(v4));
        // NAT64 (may reach any IPv4 address), documentation, discard.
        for (const auto &[net, bits] : {std::pair{"64:ff9b::", 96}, {"64:ff9b:1::", 48}, {"2001:db8::", 32},
                                        {"100::", 64}, {"2002::", 16}}) {
            if (address.isInSubnet(QHostAddress(QString::fromLatin1(net)), bits))
                return false;
        }
    } else if (address.protocol() == QAbstractSocket::IPv4Protocol) {
        for (const auto &[net, bits] : {std::pair{"0.0.0.0", 8}, {"100.64.0.0", 10}, {"192.0.0.0", 24},
                                        {"192.0.2.0", 24}, {"198.18.0.0", 15}, {"198.51.100.0", 24},
                                        {"203.0.113.0", 24}, {"240.0.0.0", 4}}) {
            if (address.isInSubnet(QHostAddress(QString::fromLatin1(net)), bits))
                return false;
        }
    } else {
        return false;
    }
    return !address.isNull() && address.isGlobal() && !address.isLoopback() && !address.isLinkLocal()
           && !address.isSiteLocal() && !address.isUniqueLocalUnicast() && !address.isMulticast()
           && !address.isBroadcast() && !address.isPrivateUse();
}

bool isMetadataClientId(const QString &clientId)
{
    if (!clientId.startsWith(u"https://") || clientId.size() > 1024)
        return false;
    const QUrl url(clientId, QUrl::StrictMode);
    if (!url.isValid() || url.scheme() != u"https" || url.host().isEmpty() || !url.userInfo().isEmpty()
        || url.hasFragment() || (url.port() != -1 && url.port() != 443))
        return false;
    if (!QHostAddress(url.host()).isNull())
        return false; // an IP literal
    const QString path = url.path(QUrl::FullyEncoded);
    if (path.isEmpty() || path == u"/" || path.contains(u"/./") || path.contains(u"/../") || path.endsWith(u"/.")
        || path.endsWith(u"/.."))
        return false;
    return true;
}

bool isAllowedRedirectUri(const QString &uri)
{
    if (uri.isEmpty() || uri.size() > 2048)
        return false;
    const QUrl url(uri, QUrl::StrictMode);
    if (!url.isValid() || url.isRelative() || url.hasFragment() || !url.userInfo().isEmpty() || url.host().isEmpty())
        return false;
    if (std::ranges::any_of(uri, [](QChar c) { return c.unicode() <= 0x20 || c.unicode() >= 0x7f; }))
        return false;
    if (url.scheme() == u"https")
        return true;
    return url.scheme() == u"http" && isLoopbackHost(url.host());
}

bool redirectMatches(const QString &registered, const QString &presented)
{
    if (registered == presented)
        return true;
    const QUrl a(registered, QUrl::StrictMode);
    const QUrl b(presented, QUrl::StrictMode);
    if (!a.isValid() || !b.isValid() || a.scheme() != u"http" || b.scheme() != u"http" || !isLoopbackHost(a.host())
        || a.host().compare(b.host(), Qt::CaseInsensitive) != 0)
        return false;
    // RFC 8252 7.3: a loopback redirect on any port.
    return a.path(QUrl::FullyEncoded) == b.path(QUrl::FullyEncoded)
           && a.query(QUrl::FullyEncoded) == b.query(QUrl::FullyEncoded) && a.userInfo().isEmpty()
           && b.userInfo().isEmpty() && !b.hasFragment();
}

bool sameResource(const QString &a, const QString &b)
{
    const QUrl x(a, QUrl::StrictMode);
    const QUrl y(b, QUrl::StrictMode);
    if (!x.isValid() || !y.isValid() || x.hasFragment() || y.hasFragment())
        return false;
    const auto path = [](const QUrl &u) {
        QString p = u.path(QUrl::FullyEncoded);
        while (p.endsWith(u'/'))
            p.chop(1);
        return p;
    };
    const auto port = [](const QUrl &u) { return u.port(u.scheme().toLower() == u"https" ? 443 : 80); };
    return x.scheme().compare(y.scheme(), Qt::CaseInsensitive) == 0
           && x.host().compare(y.host(), Qt::CaseInsensitive) == 0 && port(x) == port(y) && path(x) == path(y)
           && x.query(QUrl::FullyEncoded) == y.query(QUrl::FullyEncoded) && x.userInfo().isEmpty()
           && y.userInfo().isEmpty();
}

// ---- Fetching metadata documents

MetadataClient::MetadataClient(QObject *parent) : QObject(parent), m_network(new QNetworkAccessManager(this))
{
    m_network->setCookieJar(new NoCookies(m_network));
    m_network->setRedirectPolicy(QNetworkRequest::ManualRedirectPolicy);
}

void MetadataClient::fetch(const QUrl &url, MetadataDone done)
{
    if (!isMetadataClientId(url.toString(QUrl::FullyEncoded))) {
        done(std::nullopt, u"not an https address of a document"_s, 0);
        return;
    }
    QHostInfo::lookupHost(url.host(), this, [this, url, done](const QHostInfo &info) {
        if (info.error() != QHostInfo::NoError || info.addresses().isEmpty()) {
            done(std::nullopt, u"cannot resolve %1"_s.arg(url.host()), 0);
            return;
        }
        for (const QHostAddress &address : info.addresses()) {
            if (!isPublicAddress(address)) {
                done(std::nullopt, u"%1 is not a public address"_s.arg(url.host()), 0);
                return;
            }
        }
        QNetworkRequest request(url);
        request.setTransferTimeout(kMetadataTimeoutMs);
        request.setRawHeader("Accept"_ba, "application/json"_ba);
        request.setHeader(QNetworkRequest::UserAgentHeader,
                          u"Brulion/%1 (brulion-cli mcp serve)"_s.arg(QCoreApplication::applicationVersion()));
        request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::ManualRedirectPolicy);
        QNetworkReply *reply = m_network->get(request);
        connect(reply, &QNetworkReply::downloadProgress, reply, [reply](qint64 received, qint64 total) {
            if (received > kMetadataBytes || total > kMetadataBytes)
                reply->abort();
        });
        connect(reply, &QNetworkReply::finished, this, [reply, done] {
            reply->deleteLater();
            const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
            if (reply->error() != QNetworkReply::NoError && status == 0) {
                done(std::nullopt, reply->errorString(), 0);
                return;
            }
            if (status != 200) {
                done(std::nullopt, u"HTTP %1"_s.arg(status), 0);
                return;
            }
            const QByteArray body = reply->read(kMetadataBytes + 1);
            if (body.size() > kMetadataBytes) {
                done(std::nullopt, u"the document is too large"_s, 0);
                return;
            }
            QJsonParseError error{};
            const QJsonDocument doc = QJsonDocument::fromJson(body, &error);
            if (error.error != QJsonParseError::NoError || !doc.isObject()) {
                done(std::nullopt, u"not a JSON object"_s, 0);
                return;
            }
            done(doc.object(), QString(), maxAgeOf(reply->rawHeader("Cache-Control"_ba)));
        });
    });
}

// ---- The server

struct AuthServer::Pending {
    QString id;
    QString csrf;
    QDateTime created;
    QString clientId;
    QString redirectUri;
    QString state;
    QString challenge;
    QString resource;
    bool askedWrite = true;
    bool metadataClient = false;
    bool metadataChecked = false;
    QString clientName;
    QStringList clientRedirects;
    bool signedIn = false;
    int failures = 0;
};

struct AuthServer::Code {
    QString clientId;
    QString clientName;
    QString clientKind;
    QString redirectUri;
    QString challenge;
    QString resource;
    QStringList scopes;
    QDateTime expires;
    bool used = false;
    QString grantId;
};

struct AuthServer::CachedMetadata {
    QJsonObject document;
    QDateTime expires;
};

AuthServer::AuthServer(ServerConfig config, Store *store, QObject *parent)
    : QObject(parent), m_config(std::move(config)), m_store(store)
{
    m_metadataClient = new MetadataClient(this);
    fetchMetadata = [this](const QUrl &url, MetadataDone done) { m_metadataClient->fetch(url, std::move(done)); };
}

AuthServer::~AuthServer()
{
    flushLastUsed();
}

QDateTime AuthServer::now() const
{
    return clock ? clock().toUTC() : QDateTime::currentDateTimeUtc();
}

void AuthServer::say(const QString &line) const
{
    if (log)
        log(line);
}

QString AuthServer::route(const QByteArray &path) const
{
    const QString p = QString::fromLatin1(path);
    const QString &base = m_config.basePath;
    if (!base.isEmpty() && p.startsWith(base + u'/'))
        return p.mid(base.size());
    return p;
}

QString AuthServer::resourceMetadataUrl() const
{
    return m_config.issuer + u"/.well-known/oauth-protected-resource"_s;
}

QStringList AuthServer::resourceScopes() const
{
    QStringList scopes{scopeRead()};
    if (m_config.allowWrite)
        scopes.append(scopeWrite());
    return scopes;
}

namespace {

bool isResourceMetadataPath(const QString &path, const QString &base)
{
    const QString name = u"/.well-known/oauth-protected-resource"_s;
    return path == name || path == name + base + u"/mcp" || path == base + name || path == base + name + u"/mcp";
}

bool isServerMetadataPath(const QString &path, const QString &base)
{
    for (const QString &name : {u"/.well-known/oauth-authorization-server"_s, u"/.well-known/openid-configuration"_s}) {
        if (path == name + base || path == base + name)
            return true;
    }
    return false;
}

} // namespace

bool AuthServer::owns(const QByteArray &path) const
{
    const QString p = QString::fromLatin1(path);
    if (isResourceMetadataPath(p, m_config.basePath) || isServerMetadataPath(p, m_config.basePath))
        return true;
    const QString r = route(path);
    return r == u"/authorize" || r == u"/token" || r == u"/register" || r == u"/revoke";
}

qint64 AuthServer::bodyLimit(const QByteArray &path) const
{
    return route(path) == u"/register" ? kRegistrationBodyLimit : kFormBodyLimit;
}

void AuthServer::handle(const http::Request &request, const http::Reply &reply)
{
    const QString p = QString::fromLatin1(request.path);
    const bool get = request.method == "GET" || request.method == "HEAD";
    const auto notAllowed = [&reply](const QByteArray &allow) {
        http::Response r = http::Response::text(405, "Method not allowed"_ba);
        r.headers.set("Allow"_ba, allow);
        reply.send(r);
    };
    if (isResourceMetadataPath(p, m_config.basePath) || isServerMetadataPath(p, m_config.basePath)) {
        if (!get) {
            notAllowed("GET, HEAD"_ba);
            return;
        }
        reply.send(isResourceMetadataPath(p, m_config.basePath) ? protectedResourceMetadata()
                                                                : authorizationServerMetadata());
        return;
    }
    const QString r = route(request.path);
    if (r == u"/authorize") {
        if (get)
            authorizeGet(request, reply);
        else if (request.method == "POST")
            authorizePost(request, reply);
        else
            notAllowed("GET, HEAD, POST"_ba);
        return;
    }
    if (request.method != "POST") {
        notAllowed("POST"_ba);
        return;
    }
    if (r == u"/token")
        reply.send(token(request));
    else if (r == u"/register")
        reply.send(registerClient(request));
    else if (r == u"/revoke")
        reply.send(revoke(request));
    else
        reply.send(http::Response::text(404, "Not found"_ba));
}

http::Response AuthServer::protectedResourceMetadata() const
{
    http::Response r = http::Response::json(
            200, {{u"resource"_s, m_config.resource},
                  {u"authorization_servers"_s, QJsonArray{m_config.issuer}},
                  {u"scopes_supported"_s, QJsonArray::fromStringList(resourceScopes())},
                  {u"bearer_methods_supported"_s, QJsonArray{u"header"_s}},
                  {u"resource_name"_s, u"Brulion"_s},
                  {u"resource_documentation"_s, QString::fromLatin1(kDocumentation)}});
    r.headers.set("Cache-Control"_ba, "public, max-age=300"_ba);
    r.headers.set("Access-Control-Allow-Origin"_ba, "*"_ba);
    return r;
}

http::Response AuthServer::authorizationServerMetadata() const
{
    const QString &base = m_config.issuer;
    QStringList scopes = resourceScopes();
    scopes.append(u"offline_access"_s);
    http::Response r = http::Response::json(
            200,
            {{u"issuer"_s, m_config.issuer},
             {u"authorization_endpoint"_s, QString(base + u"/authorize")},
             {u"token_endpoint"_s, QString(base + u"/token")},
             {u"registration_endpoint"_s, QString(base + u"/register")},
             {u"revocation_endpoint"_s, QString(base + u"/revoke")},
             {u"scopes_supported"_s, QJsonArray::fromStringList(scopes)},
             {u"response_types_supported"_s, QJsonArray{u"code"_s}},
             {u"response_modes_supported"_s, QJsonArray{u"query"_s}},
             {u"grant_types_supported"_s, QJsonArray{u"authorization_code"_s, u"refresh_token"_s}},
             {u"token_endpoint_auth_methods_supported"_s,
              QJsonArray{u"none"_s, u"client_secret_post"_s, u"client_secret_basic"_s}},
             {u"revocation_endpoint_auth_methods_supported"_s,
              QJsonArray{u"none"_s, u"client_secret_post"_s, u"client_secret_basic"_s}},
             {u"code_challenge_methods_supported"_s, QJsonArray{u"S256"_s}},
             {u"client_id_metadata_document_supported"_s, true},
             {u"authorization_response_iss_parameter_supported"_s, true},
             {u"service_documentation"_s, QString::fromLatin1(kDocumentation)}});
    r.headers.set("Cache-Control"_ba, "public, max-age=300"_ba);
    r.headers.set("Access-Control-Allow-Origin"_ba, "*"_ba);
    return r;
}

// ---- Dynamic client registration (RFC 7591)

http::Response AuthServer::registerClient(const http::Request &request)
{
    const QDateTime t = now();
    m_registrations.removeIf([&t](const QDateTime &d) { return d.secsTo(t) > kRegistrationWindowSeconds; });
    if (m_registrations.size() >= kMaxRegistrationsPerWindow) {
        http::Response r = oauthError(429, u"invalid_client_metadata"_s, u"Too many registrations; try again later"_s);
        r.headers.set("Retry-After"_ba, "60"_ba);
        return r;
    }
    if (!isJson(request))
        return oauthError(400, u"invalid_client_metadata"_s, u"The registration must be JSON"_s);
    QJsonParseError parseError{};
    const QJsonDocument doc = QJsonDocument::fromJson(request.body, &parseError);
    if (parseError.error != QJsonParseError::NoError || !doc.isObject())
        return oauthError(400, u"invalid_client_metadata"_s, u"The registration is not a JSON object"_s);
    const QJsonObject meta = doc.object();
    const QJsonValue uris = meta.value(u"redirect_uris"_s);
    if (!uris.isArray() || uris.toArray().isEmpty() || uris.toArray().size() > kMaxRedirectUris)
        return oauthError(400, u"invalid_redirect_uri"_s, u"redirect_uris: one to ten URIs"_s);
    Client client;
    for (const QJsonValue &v : uris.toArray()) {
        if (!v.isString() || !isAllowedRedirectUri(v.toString()))
            return oauthError(400, u"invalid_redirect_uri"_s,
                              u"A redirect URI must be https, or http on a loopback address, with no fragment"_s);
        if (!client.redirectUris.contains(v.toString()))
            client.redirectUris.append(v.toString());
    }
    const QString method = meta.value(u"token_endpoint_auth_method"_s).toString(u"none"_s);
    if (method != u"none" && method != u"client_secret_post" && method != u"client_secret_basic")
        return oauthError(400, u"invalid_client_metadata"_s,
                          u"token_endpoint_auth_method: none, client_secret_post or client_secret_basic"_s);
    if (meta.contains(u"grant_types"_s)) {
        bool code = false;
        for (const QJsonValue &v : meta.value(u"grant_types"_s).toArray())
            code = code || v.toString() == u"authorization_code";
        if (!code)
            return oauthError(400, u"invalid_client_metadata"_s, u"grant_types must include authorization_code"_s);
    }
    if (meta.contains(u"response_types"_s)) {
        for (const QJsonValue &v : meta.value(u"response_types"_s).toArray()) {
            if (v.toString() != u"code")
                return oauthError(400, u"invalid_client_metadata"_s, u"response_types: only code"_s);
        }
    }
    client.id = randomToken(u"bcl_"_s, 16);
    client.name = cleanName(meta.value(u"client_name"_s).toString());
    client.authMethod = method;
    const QString uri = meta.value(u"client_uri"_s).toString();
    if (uri.startsWith(u"https://") && uri.size() <= 512 && QUrl(uri, QUrl::StrictMode).isValid())
        client.uri = uri;
    client.softwareId = cleanName(meta.value(u"software_id"_s).toString());
    client.softwareVersion = cleanName(meta.value(u"software_version"_s).toString());
    client.created = t;
    QString secret;
    if (method != u"none") {
        secret = randomToken(u"bcs_"_s, 32);
        client.secretHash = tokenHash(secret);
    }
    m_registrations.append(t);
    m_newClients.removeIf([&t](const Client &c) { return c.created.secsTo(t) > kNewClientTtlSeconds; });
    while (m_newClients.size() >= kMaxNewClients)
        m_newClients.removeFirst();
    m_newClients.append(client);
    say(u"registered a client: %1"_s.arg(client.name.isEmpty() ? client.id : client.name));
    QJsonObject out{{u"client_id"_s, client.id},
                    {u"client_id_issued_at"_s, t.toSecsSinceEpoch()},
                    {u"redirect_uris"_s, QJsonArray::fromStringList(client.redirectUris)},
                    {u"grant_types"_s, QJsonArray{u"authorization_code"_s, u"refresh_token"_s}},
                    {u"response_types"_s, QJsonArray{u"code"_s}},
                    {u"token_endpoint_auth_method"_s, method}};
    if (!client.name.isEmpty())
        out.insert(u"client_name"_s, client.name);
    if (!client.uri.isEmpty())
        out.insert(u"client_uri"_s, client.uri);
    if (!secret.isEmpty()) {
        out.insert(u"client_secret"_s, secret);
        out.insert(u"client_secret_expires_at"_s, 0);
    }
    http::Response r = http::Response::json(201, out);
    r.headers.set("Pragma"_ba, "no-cache"_ba);
    return r;
}

const Client *AuthServer::registeredClient(const QString &id) const
{
    QString error;
    m_store->refresh(&error);
    if (const Client *c = m_store->state().client(id))
        return c;
    const QDateTime t = now();
    const auto it = std::ranges::find_if(m_newClients, [&](const Client &c) {
        return c.id == id && c.created.secsTo(t) <= kNewClientTtlSeconds;
    });
    return it == m_newClients.end() ? nullptr : &*it;
}

QString AuthServer::passwordHash() const
{
    if (!m_config.passwordHash.isEmpty())
        return m_config.passwordHash;
    QString error;
    m_store->refresh(&error);
    return m_store->state().password;
}

// ---- The authorization endpoint

void AuthServer::prunePending()
{
    const QDateTime t = now();
    for (auto it = m_pending.begin(); it != m_pending.end();) {
        if ((*it)->created.secsTo(t) > m_config.pendingTtlSeconds)
            it = m_pending.erase(it);
        else
            ++it;
    }
    while (m_pending.size() >= kMaxPending) {
        auto oldest = std::ranges::min_element(m_pending, {}, [](const std::shared_ptr<Pending> &p) {
            return p->created;
        });
        m_pending.erase(oldest);
    }
}

void AuthServer::authorizeGet(const http::Request &request, const http::Reply &reply)
{
    const QString lang = http::preferredLanguage(request);
    const auto params = singleValued(request.query);
    if (!params) {
        reply.send(errorPage(lang, text(lang, QT_TRANSLATE_NOOP("brulion::cli",
                                                                 "The sign-in address is malformed or gives a "
                                                                 "parameter twice."))));
        return;
    }
    const auto param = [&params](const char *name) { return params->value(QString::fromLatin1(name)); };
    auto pending = std::make_shared<Pending>();
    pending->clientId = param("client_id");
    pending->redirectUri = param("redirect_uri");
    pending->state = param("state");
    pending->challenge = param("code_challenge");
    pending->resource = param("resource");
    const QStringList scopes = param("scope").split(u' ', Qt::SkipEmptyParts);
    pending->askedWrite = scopes.isEmpty() || scopes.contains(scopeWrite());
    if (pending->clientId.isEmpty()) {
        reply.send(errorPage(lang, text(lang, QT_TRANSLATE_NOOP("brulion::cli", "The sign-in address names no app."))));
        return;
    }
    pending->metadataClient = isMetadataClientId(pending->clientId);
    if (pending->metadataClient) {
        // Its redirect URIs are in its document, read once the owner has signed in.
        if (!isAllowedRedirectUri(pending->redirectUri)) {
            reply.send(errorPage(lang, text(lang, QT_TRANSLATE_NOOP("brulion::cli",
                                                                     "The app's return address is missing or not "
                                                                     "allowed."))));
            return;
        }
        pending->clientName = QUrl(pending->clientId).host();
    } else {
        const Client *client = registeredClient(pending->clientId);
        if (!client) {
            reply.send(errorPage(lang, text(lang, QT_TRANSLATE_NOOP("brulion::cli",
                                                                     "This app is not registered here (any more). "
                                                                     "Remove the connector in the app and add it "
                                                                     "again."))));
            return;
        }
        if (pending->redirectUri.isEmpty() && client->redirectUris.size() == 1)
            pending->redirectUri = client->redirectUris.first();
        if (!std::ranges::any_of(client->redirectUris,
                                 [&](const QString &u) { return redirectMatches(u, pending->redirectUri); })) {
            reply.send(errorPage(lang, text(lang, QT_TRANSLATE_NOOP("brulion::cli",
                                                                     "The app's return address is not the one it "
                                                                     "registered."))));
            return;
        }
        pending->clientName = client->name;
        pending->clientRedirects = client->redirectUris;
    }
    // From here on the return address is the client's own: errors go back to it (RFC 6749
    // 4.1.2.1) — for a client known by its document only once that is read.
    const auto fail = [&](const QString &error, const QString &description) {
        if (pending->metadataClient)
            reply.send(errorPage(lang, description));
        else
            reply.send(redirectError(*pending, error, description));
    };
    if (param("response_type") != u"code") {
        fail(u"unsupported_response_type"_s, u"Only response_type=code is supported"_s);
        return;
    }
    if (!isPkceChallenge(pending->challenge)) {
        fail(u"invalid_request"_s, u"A PKCE code_challenge (S256) is required"_s);
        return;
    }
    if (param("code_challenge_method") != u"S256") {
        fail(u"invalid_request"_s, u"code_challenge_method must be S256"_s);
        return;
    }
    if (!pending->resource.isEmpty() && !sameResource(pending->resource, m_config.resource)) {
        fail(u"invalid_target"_s, u"The resource is not this server's MCP endpoint (%1)"_s.arg(m_config.resource));
        return;
    }
    if (passwordHash().isEmpty()) {
        reply.send(errorPage(lang, text(lang, QT_TRANSLATE_NOOP("brulion::cli",
                                                                 "No password is set for this server: run "
                                                                 "brulion-cli mcp password on it first.")),
                             503));
        return;
    }
    prunePending();
    pending->id = randomToken(QString(), 24);
    pending->csrf = randomToken(QString(), 24);
    pending->created = now();
    m_pending.insert(pending->id, pending);
    reply.send(signInPage(*pending, lang, QString()));
}

void AuthServer::authorizePost(const http::Request &request, const http::Reply &reply)
{
    const QString lang = http::preferredLanguage(request);
    // A form of another site posting here (on top of the form's own token).
    const QByteArray origin = request.headers.value("Origin");
    if (!origin.isEmpty() && origin != "null"
        && QString::fromLatin1(origin).compare(http::originOf(QUrl(m_config.issuer)), Qt::CaseInsensitive) != 0) {
        reply.send(errorPage(lang, text(lang, QT_TRANSLATE_NOOP("brulion::cli", "This form came from another site.")),
                             403));
        return;
    }
    const auto form = isForm(request) ? singleValued(request.body) : std::nullopt;
    if (!form) {
        reply.send(errorPage(lang, text(lang, QT_TRANSLATE_NOOP("brulion::cli", "The form is malformed."))));
        return;
    }
    prunePending();
    const auto it = m_pending.constFind(form->value(u"request"_s));
    if (it == m_pending.cend()) {
        reply.send(errorPage(lang, text(lang, QT_TRANSLATE_NOOP("brulion::cli",
                                                                 "This sign-in has expired. Start again from the "
                                                                 "app.")),
                             400));
        return;
    }
    const std::shared_ptr<Pending> pending = *it;
    if (!constantTimeEquals(form->value(u"csrf"_s).toLatin1(), pending->csrf.toLatin1())) {
        reply.send(errorPage(lang, text(lang, QT_TRANSLATE_NOOP("brulion::cli",
                                                                 "The form is no longer valid. Start again from the "
                                                                 "app.")),
                             403));
        return;
    }
    const QString action = form->value(u"action"_s);
    if (action == u"signin") {
        if (pending->signedIn)
            afterSignIn(pending->id, lang, reply);
        else
            signIn(pending, form->value(u"password"_s), lang, reply);
        return;
    }
    if ((action == u"allow" || action == u"deny") && pending->signedIn && pending->metadataChecked) {
        reply.send(decide(pending, action == u"allow", form->value(u"write"_s) == u"on"));
        return;
    }
    reply.send(signInPage(*pending, lang, QString()));
}

qint64 AuthServer::signInDelay() const
{
    const QDateTime t = now();
    qint64 count = 0;
    QDateTime last;
    for (const QDateTime &d : m_failures) {
        if (d.secsTo(t) <= kFailureWindowSeconds) {
            ++count;
            last = std::max(last, d);
        }
    }
    if (count < kFreeFailures)
        return 0;
    const qint64 delay = std::min<qint64>(qint64(1) << std::min<qint64>(count - kFreeFailures, 12), kMaxDelaySeconds);
    return std::max<qint64>(0, delay - last.secsTo(t));
}

void AuthServer::signIn(const std::shared_ptr<Pending> &pending, const QString &password, const QString &lang,
                        const http::Reply &reply)
{
    if (const qint64 wait = signInDelay(); wait > 0) {
        reply.send(signInPage(*pending, lang,
                              text(lang, QT_TRANSLATE_NOOP("brulion::cli",
                                                           "Too many wrong passwords. Try again in %1 s."))
                                      .arg(wait)));
        return;
    }
    const QString hash = passwordHash();
    if (hash.isEmpty()) {
        reply.send(errorPage(lang, text(lang, QT_TRANSLATE_NOOP("brulion::cli",
                                                                 "No password is set for this server: run "
                                                                 "brulion-cli mcp password on it first.")),
                             503));
        return;
    }
    if (m_verifying || password.isEmpty() || password.size() > 1024) {
        reply.send(signInPage(*pending, lang,
                              password.isEmpty() ? QString()
                                                 : text(lang, QT_TRANSLATE_NOOP("brulion::cli",
                                                                                "Another sign-in is being checked. "
                                                                                "Try again."))));
        return;
    }
    // The slow hash runs on a worker thread: the server keeps answering meanwhile.
    m_verifying = true;
    QPointer<AuthServer> self(this);
    const QString id = pending->id;
    QThreadPool::globalInstance()->start([self, hash, password, id, lang, reply] {
        const bool right = verifyPassword(hash, password);
        QMetaObject::invokeMethod(
                QCoreApplication::instance(),
                [self, right, id, lang, reply] {
                    if (self)
                        self->passwordChecked(id, right, lang, reply);
                },
                Qt::QueuedConnection);
    });
}

void AuthServer::passwordChecked(const QString &pendingId, bool right, const QString &lang, const http::Reply &reply)
{
    m_verifying = false;
    const std::shared_ptr<Pending> pending = m_pending.value(pendingId);
    if (!pending) {
        reply.send(errorPage(lang, text(lang, QT_TRANSLATE_NOOP("brulion::cli",
                                                                 "This sign-in has expired. Start again from the "
                                                                 "app."))));
        return;
    }
    if (!right) {
        m_failures.append(now());
        while (m_failures.size() > 64)
            m_failures.removeFirst();
        say(u"wrong password at sign-in (%1)"_s.arg(pending->clientName));
        if (++pending->failures >= kMaxFailuresPerSignIn) {
            m_pending.remove(pendingId);
            reply.send(errorPage(lang, text(lang, QT_TRANSLATE_NOOP("brulion::cli",
                                                                     "Too many wrong passwords. Start again from "
                                                                     "the app.")),
                                 403));
            return;
        }
        reply.send(signInPage(*pending, lang, text(lang, QT_TRANSLATE_NOOP("brulion::cli", "Wrong password."))));
        return;
    }
    m_failures.clear();
    pending->signedIn = true;
    pending->csrf = randomToken(QString(), 24);
    afterSignIn(pendingId, lang, reply);
}

void AuthServer::afterSignIn(const QString &pendingId, const QString &lang, const http::Reply &reply)
{
    const std::shared_ptr<Pending> pending = m_pending.value(pendingId);
    if (!pending)
        return;
    if (!pending->metadataClient) {
        pending->metadataChecked = true;
        reply.send(consentPage(*pending, lang));
        return;
    }
    if (pending->metadataChecked) {
        reply.send(consentPage(*pending, lang));
        return;
    }
    const QDateTime t = now();
    if (const auto cached = m_metadataCache.value(pending->clientId); cached && cached->expires > t) {
        metadataFetched(pendingId, cached->document, QString(), lang, reply);
        return;
    }
    QPointer<AuthServer> self(this);
    const QString clientId = pending->clientId;
    fetchMetadata(QUrl(clientId, QUrl::StrictMode),
                  [self, pendingId, clientId, lang, reply](std::optional<QJsonObject> doc, QString error, qint64 maxAge) {
                      if (!self)
                          return;
                      if (doc) {
                          auto entry = std::make_shared<CachedMetadata>();
                          entry->document = *doc;
                          entry->expires = self->now().addSecs(std::clamp<qint64>(maxAge, 60, 86400));
                          if (self->m_metadataCache.size() >= kMaxMetadataCache)
                              self->m_metadataCache.clear();
                          self->m_metadataCache.insert(clientId, entry);
                      }
                      self->metadataFetched(pendingId, doc, error, lang, reply);
                  });
}

void AuthServer::metadataFetched(const QString &pendingId, const std::optional<QJsonObject> &document,
                                 const QString &error, const QString &lang, const http::Reply &reply)
{
    const std::shared_ptr<Pending> pending = m_pending.value(pendingId);
    if (!pending)
        return;
    const auto refuse = [&](const QString &why) {
        say(u"client metadata of %1 refused: %2"_s.arg(pending->clientId, why));
        m_pending.remove(pendingId);
        reply.send(errorPage(lang, text(lang, QT_TRANSLATE_NOOP("brulion::cli",
                                                                 "The app's description at %1 does not allow this "
                                                                 "sign-in: %2"))
                                           .arg(pending->clientId, why)));
    };
    if (!document) {
        refuse(error);
        return;
    }
    if (document->value(u"client_id"_s).toString() != pending->clientId) {
        refuse(u"its client_id is not its address"_s);
        return;
    }
    QStringList uris;
    for (const QJsonValue &v : document->value(u"redirect_uris"_s).toArray()) {
        if (v.isString() && isAllowedRedirectUri(v.toString()))
            uris.append(v.toString());
    }
    if (!std::ranges::any_of(uris, [&](const QString &u) { return redirectMatches(u, pending->redirectUri); })) {
        refuse(u"the return address %1 is not among its redirect_uris"_s.arg(pending->redirectUri));
        return;
    }
    const QString name = cleanName(document->value(u"client_name"_s).toString());
    if (!name.isEmpty())
        pending->clientName = name;
    pending->clientRedirects = uris;
    pending->metadataChecked = true;
    reply.send(consentPage(*pending, lang));
}

http::Response AuthServer::decide(const std::shared_ptr<Pending> &pending, bool allow, bool write)
{
    m_pending.remove(pending->id);
    if (!allow) {
        say(u"refused %1"_s.arg(pending->clientName));
        return redirectError(*pending, u"access_denied"_s, u"The owner did not allow the connection"_s);
    }
    QStringList scopes{scopeRead()};
    if (write && m_config.allowWrite && pending->askedWrite)
        scopes.append(scopeWrite());
    const QDateTime t = now();
    for (auto it = m_codes.begin(); it != m_codes.end();) {
        if ((*it)->expires.addSecs(600) <= t)
            it = m_codes.erase(it);
        else
            ++it;
    }
    auto code = std::make_shared<Code>();
    code->clientId = pending->clientId;
    code->clientName = pending->clientName;
    code->clientKind = pending->metadataClient ? u"metadata"_s : u"registered"_s;
    code->redirectUri = pending->redirectUri;
    code->challenge = pending->challenge;
    code->resource = m_config.resource;
    code->scopes = scopes;
    code->expires = t.addSecs(m_config.codeTtlSeconds);
    const QString value = randomToken(u"bac_"_s, 32);
    m_codes.insert(tokenHash(value), code);
    say(u"allowed %1 (%2)"_s.arg(pending->clientName, scopes.join(u' ')));
    QList<std::pair<QString, QString>> params{{u"code"_s, value}};
    if (!pending->state.isEmpty())
        params.append({u"state"_s, pending->state});
    params.append({u"iss"_s, m_config.issuer});
    return redirect(pending->redirectUri, params);
}

http::Response AuthServer::redirect(const QString &redirectUri, const QList<std::pair<QString, QString>> &params) const
{
    QByteArray location = redirectUri.toUtf8();
    location += redirectUri.contains(u'?') ? '&' : '?';
    for (qsizetype i = 0; i < params.size(); ++i) {
        if (i > 0)
            location += '&';
        location += http::formEncode(params.at(i).first) + '=' + http::formEncode(params.at(i).second);
    }
    http::Response r = http::Response::empty(303);
    r.headers.set("Location"_ba, location);
    r.headers.set("Referrer-Policy"_ba, "no-referrer"_ba);
    return r;
}

http::Response AuthServer::redirectError(const Pending &pending, const QString &error,
                                         const QString &description) const
{
    QList<std::pair<QString, QString>> params{{u"error"_s, error}, {u"error_description"_s, description}};
    if (!pending.state.isEmpty())
        params.append({u"state"_s, pending.state});
    params.append({u"iss"_s, m_config.issuer});
    return redirect(pending.redirectUri, params);
}

// ---- The token endpoint

std::optional<QString> AuthServer::authenticateClient(const http::Request &request,
                                                      const QHash<QString, QString> &form, http::Response *refusal)
{
    QString basicId;
    QString basicSecret;
    const QByteArray authorization = request.headers.value("Authorization");
    const bool basic = authorization.left(6).compare("Basic ", Qt::CaseInsensitive) == 0;
    if (basic) {
        const auto decoded = QByteArray::fromBase64Encoding(authorization.mid(6).trimmed(),
                                                            QByteArray::AbortOnBase64DecodingErrors);
        const qsizetype colon = decoded ? decoded.decoded.indexOf(':') : -1;
        const auto id = colon < 0 ? std::nullopt : http::parseForm(QByteArray("x="_ba + decoded.decoded.left(colon)));
        const auto secret =
                colon < 0 ? std::nullopt : http::parseForm(QByteArray("x="_ba + decoded.decoded.mid(colon + 1)));
        if (!id || !secret || id->isEmpty() || secret->isEmpty()) {
            *refusal = oauthError(401, u"invalid_client"_s, u"Malformed Basic authentication"_s);
            refusal->headers.set("WWW-Authenticate"_ba, "Basic realm=\"brulion\""_ba);
            return std::nullopt;
        }
        basicId = id->first().second;
        basicSecret = secret->first().second;
    }
    const QString formId = form.value(u"client_id"_s);
    if (basic && (form.contains(u"client_secret"_s) || (!formId.isEmpty() && formId != basicId))) {
        *refusal = oauthError(400, u"invalid_request"_s, u"More than one way of client authentication"_s);
        return std::nullopt;
    }
    const QString id = basic ? basicId : formId;
    if (id.isEmpty()) {
        *refusal = oauthError(401, u"invalid_client"_s, u"client_id is missing"_s);
        return std::nullopt;
    }
    if (isMetadataClientId(id))
        return id; // a public client
    const Client *client = registeredClient(id);
    if (!client) {
        *refusal = oauthError(401, u"invalid_client"_s, u"Unknown client"_s);
        return std::nullopt;
    }
    if (client->secretHash.isEmpty())
        return id;
    const QString secret = basic ? basicSecret : form.value(u"client_secret"_s);
    if (secret.isEmpty() || !constantTimeEquals(tokenHash(secret).toLatin1(), client->secretHash.toLatin1())) {
        *refusal = oauthError(401, u"invalid_client"_s, u"Wrong client credentials"_s);
        if (basic)
            refusal->headers.set("WWW-Authenticate"_ba, "Basic realm=\"brulion\""_ba);
        return std::nullopt;
    }
    return id;
}

http::Response AuthServer::token(const http::Request &request)
{
    if (!isForm(request))
        return oauthError(400, u"invalid_request"_s, u"The token request must be application/x-www-form-urlencoded"_s);
    const auto form = singleValued(request.body);
    if (!form)
        return oauthError(400, u"invalid_request"_s, u"Malformed form, or a parameter given twice"_s);
    http::Response refusal;
    const auto clientId = authenticateClient(request, *form, &refusal);
    if (!clientId)
        return refusal;
    const QString grantType = form->value(u"grant_type"_s);
    if (grantType == u"authorization_code")
        return tokenFromCode(*form, *clientId);
    if (grantType == u"refresh_token")
        return tokenFromRefresh(*form, *clientId);
    return oauthError(400, u"unsupported_grant_type"_s, u"grant_type: authorization_code or refresh_token"_s);
}

namespace {

http::Response tokenResponse(const QString &access, qint64 expiresIn, const QString &refresh,
                             const QStringList &scopes)
{
    http::Response r = http::Response::json(200, {{u"access_token"_s, access},
                                                  {u"token_type"_s, u"Bearer"_s},
                                                  {u"expires_in"_s, expiresIn},
                                                  {u"refresh_token"_s, refresh},
                                                  {u"scope"_s, scopes.join(u' ')}});
    r.headers.set("Pragma"_ba, "no-cache"_ba);
    return r;
}

} // namespace

http::Response AuthServer::tokenFromCode(const QHash<QString, QString> &form, const QString &clientId)
{
    const QString value = form.value(u"code"_s);
    if (value.isEmpty())
        return oauthError(400, u"invalid_request"_s, u"code is missing"_s);
    const QString hash = tokenHash(value);
    const std::shared_ptr<Code> code = m_codes.value(hash);
    const QDateTime t = now();
    if (!code)
        return oauthError(400, u"invalid_grant"_s, u"Unknown or expired code"_s);
    if (code->used) {
        // A code used twice was intercepted: what it gave is revoked (RFC 6749 4.1.2).
        const QString grantId = code->grantId;
        m_codes.remove(hash);
        QString error;
        m_store->update(
                [&grantId](State &s) { return s.grants.removeIf([&](const Grant &g) { return g.id == grantId; }) > 0; },
                &error);
        say(u"a code was used twice: its connection is revoked"_s);
        return oauthError(400, u"invalid_grant"_s, u"The code was already used"_s);
    }
    if (code->expires <= t) {
        m_codes.remove(hash);
        return oauthError(400, u"invalid_grant"_s, u"The code expired"_s);
    }
    if (code->clientId != clientId)
        return oauthError(400, u"invalid_grant"_s, u"The code was issued to another client"_s);
    const QString redirectUri = form.value(u"redirect_uri"_s);
    if (!redirectUri.isEmpty() && redirectUri != code->redirectUri)
        return oauthError(400, u"invalid_grant"_s, u"redirect_uri differs from the authorization request's"_s);
    if (!pkceMatches(form.value(u"code_verifier"_s), code->challenge))
        return oauthError(400, u"invalid_grant"_s, u"The PKCE code_verifier does not match"_s);
    const QString resource = form.value(u"resource"_s);
    if (!resource.isEmpty() && !sameResource(resource, code->resource))
        return oauthError(400, u"invalid_target"_s, u"The resource is not this server's MCP endpoint"_s);
    code->used = true;

    Grant grant;
    grant.id = randomToken(u"bgr_"_s, 12);
    grant.clientId = clientId;
    grant.clientName = code->clientName;
    grant.clientKind = code->clientKind;
    grant.redirectUri = code->redirectUri;
    grant.scopes = code->scopes;
    grant.resource = code->resource;
    grant.created = t;
    grant.lastUsed = t;
    grant.refreshed = t;
    const QString access = randomToken(u"bat_"_s, 32);
    const QString refresh = randomToken(u"brt_"_s, 32);
    grant.access.append({tokenHash(access), t.addSecs(m_config.accessTtlSeconds)});
    grant.refresh = {tokenHash(refresh), t.addSecs(m_config.refreshTtlSeconds)};
    std::optional<Client> newClient;
    if (code->clientKind == u"registered") {
        const auto it = std::ranges::find_if(m_newClients, [&](const Client &c) { return c.id == clientId; });
        if (it != m_newClients.end())
            newClient = *it;
    }
    QString error;
    const int maxGrants = m_config.maxGrants;
    const bool written = m_store->update(
            [&](State &s) {
                s.prune(t);
                if (newClient && !s.client(newClient->id))
                    s.clients.append(*newClient);
                while (s.grants.size() >= maxGrants) {
                    auto oldest = std::ranges::min_element(s.grants, {}, [](const Grant &g) { return g.lastUsed; });
                    s.grants.erase(oldest);
                }
                s.grants.append(grant);
                return true;
            },
            &error);
    if (!written) {
        say(u"cannot write the state file: %1"_s.arg(error));
        return oauthError(500, u"server_error"_s, u"The server cannot save the connection"_s);
    }
    if (newClient)
        m_newClients.removeIf([&](const Client &c) { return c.id == newClient->id; });
    code->grantId = grant.id;
    say(u"connected %1 (%2)"_s.arg(grant.clientName, grant.scopes.join(u' ')));
    return tokenResponse(access, m_config.accessTtlSeconds, refresh, grant.scopes);
}

http::Response AuthServer::tokenFromRefresh(const QHash<QString, QString> &form, const QString &clientId)
{
    const QString presented = form.value(u"refresh_token"_s);
    if (presented.isEmpty())
        return oauthError(400, u"invalid_request"_s, u"refresh_token is missing"_s);
    const QString hash = tokenHash(presented);
    const QDateTime t = now();
    const QStringList asked = form.value(u"scope"_s).split(u' ', Qt::SkipEmptyParts);
    const QString resource = form.value(u"resource"_s);
    enum class Outcome { NotFound, Reused, Expired, WrongClient, BadScope, BadResource, Rotated };
    Outcome outcome = Outcome::NotFound;
    QString access;
    QString refresh;
    QStringList scopes;
    QString clientName;
    const qint64 accessTtl = m_config.accessTtlSeconds;
    const qint64 refreshTtl = m_config.refreshTtlSeconds;
    const qint64 grace = m_config.refreshGraceSeconds;
    const QString ourResource = m_config.resource;
    QString error;
    const bool ok = m_store->update(
            [&](State &s) {
                for (qsizetype i = 0; i < s.grants.size(); ++i) {
                    Grant &g = s.grants[i];
                    const bool current = constantTimeEquals(g.refresh.hash.toLatin1(), hash.toLatin1());
                    const bool previous = !g.previous.hash.isEmpty()
                                          && constantTimeEquals(g.previous.hash.toLatin1(), hash.toLatin1());
                    const bool retired = std::ranges::any_of(g.retired, [&](const Token &r) {
                        return constantTimeEquals(r.hash.toLatin1(), hash.toLatin1());
                    });
                    if (!current && !previous && !retired)
                        continue;
                    clientName = g.clientName;
                    const bool withinGrace = previous && g.previousRotated.isValid()
                                             && g.previousRotated.secsTo(t) <= grace;
                    if (retired || (previous && !withinGrace)) {
                        // A refresh token used again after it was replaced: someone has a copy.
                        s.grants.removeAt(i);
                        outcome = Outcome::Reused;
                        return true;
                    }
                    if (g.clientId != clientId) {
                        outcome = Outcome::WrongClient;
                        return false;
                    }
                    if (g.refresh.expires <= t && current) {
                        s.grants.removeAt(i);
                        outcome = Outcome::Expired;
                        return true;
                    }
                    for (const QString &scope : asked) {
                        if (scope != u"offline_access" && !g.scopes.contains(scope)) {
                            outcome = Outcome::BadScope;
                            return false;
                        }
                    }
                    if (!resource.isEmpty() && !sameResource(resource, ourResource)) {
                        outcome = Outcome::BadResource;
                        return false;
                    }
                    if (current) {
                        if (!g.previous.hash.isEmpty())
                            g.retired.append(g.previous);
                        g.previous = g.refresh;
                        g.previousRotated = t;
                    } else {
                        // The previous one again, within the grace period: the answer that
                        // carried the current one was lost. That one is never used now.
                        g.retired.append(g.refresh);
                    }
                    access = randomToken(u"bat_"_s, 32);
                    refresh = randomToken(u"brt_"_s, 32);
                    g.refresh = {tokenHash(refresh), t.addSecs(refreshTtl)};
                    g.access.append({tokenHash(access), t.addSecs(accessTtl)});
                    while (g.access.size() > kMaxAccessTokensPerGrant)
                        g.access.removeFirst();
                    g.refreshed = t;
                    g.lastUsed = t;
                    scopes = g.scopes;
                    s.prune(t);
                    outcome = Outcome::Rotated;
                    return true;
                }
                return false;
            },
            &error);
    if (!ok) {
        say(u"cannot write the state file: %1"_s.arg(error));
        return oauthError(500, u"server_error"_s, u"The server cannot save the connection"_s);
    }
    switch (outcome) {
    case Outcome::Rotated:
        return tokenResponse(access, accessTtl, refresh, scopes);
    case Outcome::Reused:
        say(u"a replaced refresh token was used again: the connection of %1 is revoked"_s.arg(clientName));
        return oauthError(400, u"invalid_grant"_s, u"The refresh token was already used: the connection is revoked"_s);
    case Outcome::Expired:
        return oauthError(400, u"invalid_grant"_s, u"The refresh token expired"_s);
    case Outcome::WrongClient:
        return oauthError(400, u"invalid_grant"_s, u"The refresh token belongs to another client"_s);
    case Outcome::BadScope:
        return oauthError(400, u"invalid_scope"_s, u"More scopes than were granted"_s);
    case Outcome::BadResource:
        return oauthError(400, u"invalid_target"_s, u"The resource is not this server's MCP endpoint"_s);
    case Outcome::NotFound:
        break;
    }
    return oauthError(400, u"invalid_grant"_s, u"Unknown, expired or revoked refresh token"_s);
}

http::Response AuthServer::revoke(const http::Request &request)
{
    if (!isForm(request))
        return oauthError(400, u"invalid_request"_s, u"The request must be application/x-www-form-urlencoded"_s);
    const auto form = singleValued(request.body);
    if (!form)
        return oauthError(400, u"invalid_request"_s, u"Malformed form, or a parameter given twice"_s);
    http::Response refusal;
    const auto clientId = authenticateClient(request, *form, &refusal);
    if (!clientId)
        return refusal;
    const QString hash = tokenHash(form->value(u"token"_s));
    QString error;
    QString revoked;
    m_store->update(
            [&](State &s) {
                const qsizetype removed = s.grants.removeIf([&](const Grant &g) {
                    if (g.clientId != *clientId)
                        return false;
                    const auto same = [&](const Token &t) { return t.hash == hash; };
                    const bool match = same(g.refresh) || same(g.previous) || std::ranges::any_of(g.access, same)
                                       || std::ranges::any_of(g.retired, same);
                    if (match)
                        revoked = g.clientName;
                    return match;
                });
                return removed > 0;
            },
            &error);
    if (!revoked.isEmpty())
        say(u"%1 ended its connection"_s.arg(revoked));
    // RFC 7009 2.2: 200 whether or not the token was valid.
    return http::Response::empty(200);
}

// ---- The bearer check

http::Response AuthServer::unauthorized(const QString &error, const QString &description) const
{
    QByteArray challenge = "Bearer resource_metadata=\""_ba + resourceMetadataUrl().toUtf8() + "\", scope=\""
                           + resourceScopes().join(u' ').toUtf8() + '"';
    if (!error.isEmpty()) {
        QString d = description;
        d.remove(u'"').remove(u'\\');
        challenge += ", error=\"" + error.toLatin1() + "\", error_description=\"" + d.toUtf8() + '"';
    }
    http::Response r = http::Response::json(401, {{u"error"_s, error.isEmpty() ? u"unauthorized"_s : error},
                                                  {u"error_description"_s, description.isEmpty()
                                                                                   ? u"Sign in: an access token is required"_s
                                                                                   : description}});
    r.headers.set("WWW-Authenticate"_ba, challenge);
    return r;
}

http::Response AuthServer::insufficientScope(const QString &description) const
{
    QString d = description;
    d.remove(u'"').remove(u'\\');
    http::Response r = http::Response::json(403, {{u"error"_s, u"insufficient_scope"_s}, {u"error_description"_s, d}});
    r.headers.set("WWW-Authenticate"_ba, "Bearer error=\"insufficient_scope\", scope=\""_ba
                                                 + resourceScopes().join(u' ').toUtf8() + "\", resource_metadata=\""
                                                 + resourceMetadataUrl().toUtf8() + "\", error_description=\""
                                                 + d.toUtf8() + '"');
    return r;
}

std::optional<AuthServer::Access> AuthServer::authenticate(const http::Request &request, http::Response *refusal)
{
    const int count = request.headers.count("Authorization");
    if (count == 0) {
        *refusal = unauthorized(QString(), QString());
        return std::nullopt;
    }
    const QByteArray value = request.headers.value("Authorization");
    const qsizetype space = value.indexOf(' ');
    const QByteArray scheme = space < 0 ? value : value.left(space);
    const QByteArray token = space < 0 ? QByteArray() : value.mid(space + 1).trimmed();
    const bool token68 = !token.isEmpty() && std::ranges::all_of(token, [](char c) {
        return std::isalnum(static_cast<unsigned char>(c)) || QByteArrayView("-._~+/=").contains(c);
    });
    if (count > 1 || scheme.compare("Bearer", Qt::CaseInsensitive) != 0 || !token68 || token.size() > 512) {
        *refusal = unauthorized(u"invalid_token"_s, u"Malformed Authorization header"_s);
        return std::nullopt;
    }
    QString error;
    if (!m_store->refresh(&error))
        say(u"cannot read the state file: %1"_s.arg(error));
    const QString presented = QString::fromLatin1(token);
    const Grant *grant = m_store->grantByAccessToken(presented);
    const QDateTime t = now();
    const QString hash = tokenHash(presented);
    const auto live = grant ? std::ranges::find_if(grant->access,
                                                   [&](const Token &a) {
                                                       return constantTimeEquals(a.hash.toLatin1(), hash.toLatin1());
                                                   })
                            : QList<Token>::const_iterator();
    if (!grant || live == grant->access.cend()) {
        *refusal = unauthorized(u"invalid_token"_s, u"The access token is unknown or revoked"_s);
        return std::nullopt;
    }
    if (live->expires <= t) {
        *refusal = unauthorized(u"invalid_token"_s, u"The access token expired"_s);
        return std::nullopt;
    }
    if (!sameResource(grant->resource, m_config.resource)) {
        *refusal = unauthorized(u"invalid_token"_s, u"The access token is for another resource"_s);
        return std::nullopt;
    }
    m_lastUsed.insert(grant->id, t);
    return Access{grant->id, grant->clientName, grant->canWrite() && m_config.allowWrite};
}

void AuthServer::flushLastUsed()
{
    if (m_lastUsed.isEmpty())
        return;
    const QHash<QString, QDateTime> used = std::exchange(m_lastUsed, {});
    QString error;
    m_store->update(
            [&used](State &s) {
                bool changed = false;
                for (Grant &g : s.grants) {
                    const auto it = used.constFind(g.id);
                    if (it != used.cend() && (!g.lastUsed.isValid() || *it > g.lastUsed)) {
                        g.lastUsed = *it;
                        changed = true;
                    }
                }
                return changed;
            },
            &error);
}

// ---- Pages

http::Response AuthServer::page(const QString &lang, const QString &title, const QString &body,
                                const QString &formTarget, int status) const
{
    static const QString style = uR"css(
:root{color-scheme:light dark;--bg:#f4f1ea;--card:#fffdf8;--fg:#1d1b18;--muted:#625d55;--line:#ddd6c8;--accent:#c4422f;--on-accent:#fff;--warn:#7a4f00;--warn-bg:#fdf0cf;--err:#9e1c12;--err-bg:#fbe4e1}
@media (prefers-color-scheme:dark){:root{--bg:#000;--card:#141414;--fg:#ece9e4;--muted:#a8a29a;--line:#2e2c2a;--accent:#ff7a66;--on-accent:#2b0a05;--warn:#f2b33d;--warn-bg:#2b2310;--err:#ff8a7d;--err-bg:#2e1512}}
*{box-sizing:border-box}
body{margin:0;min-height:100vh;display:flex;align-items:center;justify-content:center;padding:24px 16px;background:var(--bg);color:var(--fg);font:16px/1.5 system-ui,-apple-system,"Segoe UI",Roboto,"Noto Sans",sans-serif}
main{width:100%;max-width:27rem;background:var(--card);border:1px solid var(--line);border-radius:14px;padding:28px 24px}
.brand{margin:0 0 6px;font-size:13px;font-weight:700;letter-spacing:.08em;text-transform:uppercase;color:var(--accent)}
h1{margin:0 0 14px;font-size:22px;line-height:1.3}
p{margin:0 0 12px}
.muted{color:var(--muted);font-size:14px}
.box{border:1px solid var(--line);border-radius:10px;padding:12px 14px;margin:0 0 16px}
.box p:last-child{margin:0}
.host{font-size:18px;font-weight:700;word-break:break-all}
.warn,.err{border-radius:10px;padding:10px 12px;font-size:14px;margin:0 0 16px}
.warn{background:var(--warn-bg);color:var(--warn)}
.err{background:var(--err-bg);color:var(--err)}
label{display:block;font-weight:600;margin:0 0 6px}
input[type=password]{width:100%;font:inherit;padding:10px 12px;border-radius:8px;border:1px solid var(--line);background:transparent;color:inherit}
input[type=password]:focus{outline:2px solid var(--accent);outline-offset:1px}
.check{display:flex;gap:10px;align-items:flex-start;font-weight:400;margin:0 0 12px}
.check input{margin-top:5px;accent-color:var(--accent)}
.actions{display:flex;gap:10px;flex-wrap:wrap;margin-top:20px}
button{font:inherit;font-weight:600;padding:10px 18px;border-radius:8px;border:1px solid var(--line);background:transparent;color:inherit;cursor:pointer}
button.primary{background:var(--accent);border-color:var(--accent);color:var(--on-accent)}
code{font-size:13px;word-break:break-all}
)css"_s;
    const QString html = u"<!doctype html><html lang=\""_s + lang
                         + u"\"><head><meta charset=\"utf-8\"><meta name=\"viewport\" "
                           u"content=\"width=device-width,initial-scale=1\"><meta name=\"robots\" content=\"noindex\">"
                           u"<title>"_s
                         + esc(title) + u" · Brulion</title><style>"_s + style
                         + u"</style></head><body><main><p class=\"brand\">Brulion</p><h1>"_s + esc(title)
                         + u"</h1>"_s + body + u"</main></body></html>\n"_s;
    http::Response r;
    r.status = status;
    r.body = html.toUtf8();
    r.headers.set("Content-Type"_ba, "text/html; charset=utf-8"_ba);
    QByteArray formAction = "'self'"_ba;
    if (!formTarget.isEmpty())
        formAction += ' ' + formTarget.toLatin1();
    r.headers.set("Content-Security-Policy"_ba, "default-src 'none'; style-src 'unsafe-inline'; form-action "_ba
                                                        + formAction
                                                        + "; frame-ancestors 'none'; base-uri 'none'"_ba);
    r.headers.set("X-Frame-Options"_ba, "DENY"_ba);
    r.headers.set("Referrer-Policy"_ba, "same-origin"_ba);
    r.headers.set("Pragma"_ba, "no-cache"_ba);
    return r;
}

http::Response AuthServer::errorPage(const QString &lang, const QString &message, int status) const
{
    return page(lang, text(lang, QT_TRANSLATE_NOOP("brulion::cli", "The connection cannot be made")),
                u"<p class=\"err\">"_s + esc(message) + u"</p><p class=\"muted\">"_s
                        + esc(text(lang, QT_TRANSLATE_NOOP("brulion::cli", "You can close this page.")))
                        + u"</p>"_s,
                QString(), status);
}

QString AuthServer::clientIdentity(const Pending &pending, const QString &lang) const
{
    if (pending.metadataClient)
        return text(lang, QT_TRANSLATE_NOOP("brulion::cli", "described at %1")).arg(QUrl(pending.clientId).host());
    return text(lang, QT_TRANSLATE_NOOP("brulion::cli", "a name the app gave itself"));
}

http::Response AuthServer::signInPage(const Pending &pending, const QString &lang, const QString &error) const
{
    const QString name = pending.clientName.isEmpty()
                                 ? text(lang, QT_TRANSLATE_NOOP("brulion::cli", "An app without a name"))
                                 : pending.clientName;
    QString body = u"<p>"_s
                   + text(lang, QT_TRANSLATE_NOOP("brulion::cli",
                                                  "%1 asks for access to the notebook “%2” on %3."))
                             .arg(u"<strong>"_s + esc(name) + u"</strong>"_s, esc(m_config.notebookName),
                                  esc(QUrl(m_config.issuer).host()))
                   + u"</p>"_s;
    if (!error.isEmpty())
        body += u"<p class=\"err\" role=\"alert\">"_s + esc(error) + u"</p>"_s;
    body += u"<form method=\"post\" action=\""_s + esc(m_config.basePath + u"/authorize"_s)
            + u"\"><input type=\"hidden\" name=\"request\" value=\""_s + esc(pending.id)
            + u"\"><input type=\"hidden\" name=\"csrf\" value=\""_s + esc(pending.csrf)
            + u"\"><label for=\"password\">"_s + esc(text(lang, QT_TRANSLATE_NOOP("brulion::cli", "Password")))
            + u"</label><input type=\"password\" id=\"password\" name=\"password\" autocomplete=\"current-password\" "
              u"required autofocus><p class=\"muted\" style=\"margin-top:8px\">"_s
            + text(lang, QT_TRANSLATE_NOOP("brulion::cli", "The password of this server, set with %1."))
                      .arg(u"<code>brulion-cli mcp password</code>"_s)
            + u"</p><div class=\"actions\"><button class=\"primary\" type=\"submit\" name=\"action\" "
              u"value=\"signin\">"_s
            + esc(text(lang, QT_TRANSLATE_NOOP("brulion::cli", "Continue"))) + u"</button></div></form>"_s;
    return page(lang, text(lang, QT_TRANSLATE_NOOP("brulion::cli", "Sign in to Brulion")), body);
}

http::Response AuthServer::consentPage(const Pending &pending, const QString &lang) const
{
    const QString name = pending.clientName.isEmpty()
                                 ? text(lang, QT_TRANSLATE_NOOP("brulion::cli", "An app without a name"))
                                 : pending.clientName;
    const QUrl target(pending.redirectUri);
    QString body = u"<div class=\"box\"><p><strong>"_s + esc(name) + u"</strong> <span class=\"muted\">("_s
                   + esc(clientIdentity(pending, lang)) + u")</span></p><p class=\"muted\">"_s
                   + esc(text(lang, QT_TRANSLATE_NOOP("brulion::cli", "After your answer it continues at")))
                   + u"</p><p class=\"host\">"_s + esc(target.host()) + u"</p><p><code>"_s
                   + esc(pending.redirectUri) + u"</code></p></div>"_s;
    const bool loopback = target.scheme() == u"http" && isLoopbackHost(target.host());
    if (loopback)
        body += u"<p class=\"warn\">"_s
                + esc(text(lang, QT_TRANSLATE_NOOP("brulion::cli",
                                                   "The answer goes to a program on the computer you are using now "
                                                   "(localhost). Allow it only if you are setting one up there "
                                                   "yourself.")))
                + u"</p>"_s;
    else if (!pending.metadataClient)
        body += u"<p class=\"warn\">"_s
                + esc(text(lang, QT_TRANSLATE_NOOP("brulion::cli",
                                                   "Brulion cannot check the name: the app gave it itself. Allow "
                                                   "only if you are adding Brulion in that app right now and the "
                                                   "address above belongs to it.")))
                + u"</p>"_s;
    body += u"<form method=\"post\" action=\""_s + esc(m_config.basePath + u"/authorize"_s)
            + u"\"><input type=\"hidden\" name=\"request\" value=\""_s + esc(pending.id)
            + u"\"><input type=\"hidden\" name=\"csrf\" value=\""_s + esc(pending.csrf) + u"\"><p>"_s
            + esc(text(lang, QT_TRANSLATE_NOOP("brulion::cli",
                                               "It will be able to search and read every note of this notebook.")))
            + u"</p>"_s;
    if (m_config.allowWrite && pending.askedWrite) {
        const char *changes = m_config.allowDestructive
                                      ? QT_TRANSLATE_NOOP("brulion::cli",
                                                          "Also allow it to change notes: write in the journal, add "
                                                          "tasks and reminders, edit, replace, move and delete pages "
                                                          "(deleted pages go to the trash).")
                                      : QT_TRANSLATE_NOOP("brulion::cli",
                                                          "Also allow it to change notes: write in the journal, add "
                                                          "tasks and reminders, edit pages (it can never delete or "
                                                          "replace them).");
        body += u"<label class=\"check\"><input type=\"checkbox\" name=\"write\" value=\"on\" checked><span>"_s
                + esc(text(lang, changes)) + u"</span></label>"_s;
    } else if (!m_config.allowWrite) {
        body += u"<p class=\"muted\">"_s
                + esc(text(lang, QT_TRANSLATE_NOOP("brulion::cli", "This server is read-only: nothing can be "
                                                                   "changed through it.")))
                + u"</p>"_s;
    }
    body += u"<div class=\"actions\"><button class=\"primary\" type=\"submit\" name=\"action\" value=\"allow\">"_s
            + esc(text(lang, QT_TRANSLATE_NOOP("brulion::cli", "Allow")))
            + u"</button><button type=\"submit\" name=\"action\" value=\"deny\">"_s
            + esc(text(lang, QT_TRANSLATE_NOOP("brulion::cli", "Deny"))) + u"</button></div></form><p class=\"muted\" "
                                                                            u"style=\"margin-top:18px\">"_s
            + text(lang, QT_TRANSLATE_NOOP("brulion::cli", "You can end the connection at any time: %1."))
                      .arg(u"<code>brulion-cli mcp revoke</code>"_s)
            + u"</p>"_s;
    return page(lang, text(lang, QT_TRANSLATE_NOOP("brulion::cli", "Allow access to your notes?")), body,
                http::originOf(target));
}

http::Response AuthServer::homePage(const http::Request &request) const
{
    const QString lang = http::preferredLanguage(request);
    const QString body =
            u"<p>"_s
            + esc(text(lang, QT_TRANSLATE_NOOP("brulion::cli",
                                               "This is the MCP server of a Brulion notebook. To use it from "
                                               "claude.ai, the Claude app or ChatGPT, add this address there as a "
                                               "custom connector:")))
            + u"</p><div class=\"box\"><p class=\"host\">"_s + esc(m_config.resource) + u"</p></div><p class=\"muted\">"_s
            + esc(text(lang, QT_TRANSLATE_NOOP("brulion::cli",
                                               "Then sign in with the password set with brulion-cli mcp password.")))
            + u"</p>"_s;
    return page(lang, text(lang, QT_TRANSLATE_NOOP("brulion::cli", "Brulion for AI apps")), body);
}

} // namespace brulion::cli::oauth
