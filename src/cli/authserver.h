// The OAuth 2.1 authorization server and protected resource metadata of `brulion-cli mcp serve`
// (docs/CLI.md "Remote MCP server", the MCP authorization specification): protected resource
// metadata (RFC 9728), authorization server metadata (RFC 8414), dynamic client registration
// (RFC 7591) and client ID metadata documents, the authorization code flow with PKCE S256 only —
// a sign-in page for the owner's password and a consent page that names the client, where it
// returns and whether it may change notes — issuer identification (RFC 9207), resource
// indicators (RFC 8707), access tokens and rotating refresh tokens with reuse detection, token
// revocation (RFC 7009) and the bearer check of the MCP endpoint.
#pragma once

#include "http.h"
#include "oauth.h"

#include <QCoreApplication>
#include <QHash>
#include <QObject>
#include <QUrl>

#include <functional>
#include <memory>
#include <optional>

class QHostAddress;
class QNetworkAccessManager;

namespace brulion::cli::oauth {

inline constexpr auto kScopeRead = "notes:read";
inline constexpr auto kScopeWrite = "notes:write";

struct ServerConfig {
    QString issuer;   ///< the public base URL without a trailing slash ("https://notes.example.com")
    QString basePath; ///< its path ("" or "/brulion")
    QString resource; ///< the MCP endpoint, issuer + "/mcp" (the tokens' audience)
    QString notebookName;
    bool allowWrite = true; ///< the server is not --read-only: a client may be allowed to write
    bool allowDestructive = false; ///< (for the consent page's text)
    QString passwordHash;   ///< from --password-file; else the state file's
    qint64 accessTtlSeconds = 3600;
    qint64 refreshTtlSeconds = 60 * 86400;
    qint64 codeTtlSeconds = 60;
    qint64 refreshGraceSeconds = 30;
    qint64 pendingTtlSeconds = 600;
    int maxGrants = 100;
};

/// A client ID metadata document fetched: the JSON (nullopt with `error`) and how long it may be
/// kept (seconds).
using MetadataDone = std::function<void(std::optional<QJsonObject> document, QString error, qint64 maxAge)>;
using MetadataFetcher = std::function<void(const QUrl &url, MetadataDone done)>;

/// Is `address` one a metadata document may be fetched from: global unicast, not private use,
/// loopback, link-local, CGNAT (100.64/10, Tailscale), documentation or reserved?
bool isPublicAddress(const QHostAddress &address);
/// A client_id that names a metadata document: https, a path, no fragment, user or dot segments,
/// the default port, a host name (no IP literal).
bool isMetadataClientId(const QString &clientId);
/// A redirect URI a client may register: https, or http on a loopback host; absolute, no fragment
/// or user information.
bool isAllowedRedirectUri(const QString &uri);
/// Does `presented` match the registered `registered` exactly — or, both on a loopback host over
/// http, apart from the port (RFC 8252 7.3)?
bool redirectMatches(const QString &registered, const QString &presented);
/// The resource indicator names this server's MCP endpoint (scheme and host in any case, a
/// trailing slash or not).
bool sameResource(const QString &a, const QString &b);

/// Fetches metadata documents over HTTPS with the checks above: no redirects, 10 s, 64 KiB.
class MetadataClient : public QObject {
public:
    explicit MetadataClient(QObject *parent = nullptr);
    void fetch(const QUrl &url, MetadataDone done);

private:
    QNetworkAccessManager *m_network;
};

class AuthServer : public QObject {
    Q_DECLARE_TR_FUNCTIONS(brulion::cli)
public:
    AuthServer(ServerConfig config, Store *store, QObject *parent = nullptr);
    ~AuthServer() override;

    /// The clock (tests set one).
    std::function<QDateTime()> clock;
    MetadataFetcher fetchMetadata;
    std::function<void(const QString &line)> log;

    const ServerConfig &config() const { return m_config; }
    /// The path is one of the metadata documents or OAuth endpoints.
    bool owns(const QByteArray &path) const;
    /// The largest body accepted at that path.
    qint64 bodyLimit(const QByteArray &path) const;
    void handle(const http::Request &request, const http::Reply &reply);

    struct Access {
        QString grantId;
        QString clientName;
        bool write = false;
    };
    /// The request's bearer token: its connection, or nullopt with the 401 to send.
    std::optional<Access> authenticate(const http::Request &request, http::Response *refusal);
    /// 403 insufficient_scope for a tool that writes.
    http::Response insufficientScope(const QString &description) const;
    /// The 401 answer (`error`: "" when no token was sent, else invalid_token).
    http::Response unauthorized(const QString &error, const QString &description) const;
    QString resourceMetadataUrl() const;
    /// The scopes a client should ask for (WWW-Authenticate, metadata).
    QStringList resourceScopes() const;
    /// Write the connections' last use (kept in memory between writes).
    void flushLastUsed();
    /// The page at the server's root: what to paste into the app.
    http::Response homePage(const http::Request &request) const;

private:
    struct Pending;
    struct Code;
    struct CachedMetadata;

    QDateTime now() const;
    void say(const QString &line) const;
    QString route(const QByteArray &path) const;
    http::Response protectedResourceMetadata() const;
    http::Response authorizationServerMetadata() const;
    http::Response registerClient(const http::Request &request);
    void authorizeGet(const http::Request &request, const http::Reply &reply);
    void authorizePost(const http::Request &request, const http::Reply &reply);
    void signIn(const std::shared_ptr<Pending> &pending, const QString &password, const QString &lang,
                const http::Reply &reply);
    void passwordChecked(const QString &pendingId, bool right, const QString &lang, const http::Reply &reply);
    void afterSignIn(const QString &pendingId, const QString &lang, const http::Reply &reply);
    void metadataFetched(const QString &pendingId, const std::optional<QJsonObject> &document, const QString &error,
                         const QString &lang, const http::Reply &reply);
    http::Response decide(const std::shared_ptr<Pending> &pending, bool allow, bool write);
    http::Response token(const http::Request &request);
    http::Response tokenFromCode(const QHash<QString, QString> &form, const QString &clientId);
    http::Response tokenFromRefresh(const QHash<QString, QString> &form, const QString &clientId);
    http::Response revoke(const http::Request &request);
    /// The client of a token or revocation request (client_secret_basic, _post or none):
    /// its id, or nullopt with the error answer.
    std::optional<QString> authenticateClient(const http::Request &request, const QHash<QString, QString> &form,
                                              http::Response *refusal);
    const Client *registeredClient(const QString &id) const;
    QString passwordHash() const;
    /// Seconds until the next password attempt is allowed (0: now).
    qint64 signInDelay() const;
    void prunePending();

    // Pages
    http::Response page(const QString &lang, const QString &title, const QString &body,
                        const QString &formTarget = QString(), int status = 200) const;
    http::Response errorPage(const QString &lang, const QString &message, int status = 400) const;
    http::Response signInPage(const Pending &pending, const QString &lang, const QString &error) const;
    http::Response consentPage(const Pending &pending, const QString &lang) const;
    http::Response redirect(const QString &redirectUri, const QList<std::pair<QString, QString>> &params) const;
    http::Response redirectError(const Pending &pending, const QString &error, const QString &description) const;
    QString clientIdentity(const Pending &pending, const QString &lang) const;

    ServerConfig m_config;
    Store *m_store;
    MetadataClient *m_metadataClient = nullptr;
    QHash<QString, std::shared_ptr<Pending>> m_pending;
    QHash<QString, std::shared_ptr<Code>> m_codes; ///< by code hash
    QList<Client> m_newClients;     ///< registered, not yet allowed by the owner (in memory only)
    QList<QDateTime> m_registrations;
    QList<QDateTime> m_failures;
    bool m_verifying = false;
    QHash<QString, QDateTime> m_lastUsed; ///< grant id -> last use not yet written
    QHash<QString, std::shared_ptr<CachedMetadata>> m_metadataCache;
};

} // namespace brulion::cli::oauth
