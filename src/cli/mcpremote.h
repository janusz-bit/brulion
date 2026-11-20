// `brulion-cli mcp serve`: the MCP tools over Streamable HTTP with OAuth sign-in, for the
// providers' own apps (claude.ai and the Claude app, ChatGPT) — docs/CLI.md "Remote MCP server".
// One endpoint (<public URL>/mcp): POST of one JSON-RPC message answered as JSON (or SSE), the
// handshake era's sessions (Mcp-Session-Id, DELETE ends one) and the per-request era of 2026-07-28
// (its headers checked against the body), GET answered 405 (no server-initiated messages). Every
// request carries a bearer token of the authorization server (authserver.h) bound to this
// endpoint; Origin and Host are checked (DNS rebinding). HTTPS itself with a certificate and key
// (reloaded when they change), or plain HTTP on 127.0.0.1 behind a reverse proxy or tunnel.
#pragma once

#include "authserver.h"
#include "http.h"
#include "mcp_p.h"

#include <QDateTime>
#include <QHash>
#include <QHostAddress>
#include <QObject>

#include <memory>

class QTimer;

namespace brulion::cli {

struct RemoteOptions {
    QHostAddress address = QHostAddress(QHostAddress::LocalHost);
    quint16 port = 8780;
    QString publicUrl;      ///< "" = http(s)://<address>:<port>
    QString authFile;       ///< the state file (oauth::Store)
    QString passwordHash;   ///< from --password-file (else the state file's)
    bool readOnly = false;
    bool allowDestructive = false;
    QString tlsCertificate; ///< PEM, the certificate and its chain
    QString tlsKey;         ///< PEM
    QStringList allowedOrigins; ///< browser-based clients (CORS)
    QStringList allowedHosts;   ///< more names in Host (a proxy that rewrites it)
    qint64 maxBodyBytes = 16 * 1024 * 1024;
    int maxSessions = 256;
    int maxSessionsPerGrant = 32;
    qint64 sessionIdleSeconds = 3600;
    int maintenanceIntervalMs = 30000; ///< write last use, drop idle sessions, reload the certificate
    http::ServerLimits limits;
    oauth::ServerConfig auth; ///< token lifetimes (issuer, resource and the rest are filled in)
};

class RemoteMcpServer : public QObject {
    Q_DECLARE_TR_FUNCTIONS(brulion::cli)
public:
    RemoteMcpServer(Context &context, RemoteOptions options, QObject *parent = nullptr);
    ~RemoteMcpServer() override;

    /// Check the options, read the state, load the certificate and listen.
    bool start(QString *error);
    quint16 port() const;
    /// The MCP endpoint to paste into the app (<public URL>/mcp).
    QString endpoint() const;
    QString issuer() const;
    oauth::AuthServer *auth() { return m_auth.get(); }
    http::Server *server() { return m_http.get(); }
    oauth::Store *store() { return m_store.get(); }
    int sessionCount() const { return int(m_sessions.size()); }
    std::function<void(const QString &line)> log;

private:
    struct Session {
        QString grantId;
        QString version;
        QDateTime lastUsed;
    };

    void say(const QString &line) const;
    bool loadTls(QString *error);
    void maintain();
    std::optional<http::Response> onHeaders(const http::Request &head, qint64 *maxBody);
    void onRequest(const http::Request &request, const http::Reply &reply);
    void handleMcp(const http::Request &request, const http::Reply &reply);
    QString route(const QByteArray &path) const;
    bool hostAllowed(const QByteArray &host) const;
    bool originAllowed(const QByteArray &origin) const;
    http::Headers corsHeaders(const QByteArray &origin) const;
    void pruneSessions();

    Context &m_ctx;
    RemoteOptions m_options;
    QString m_issuer;
    QString m_basePath;
    QString m_endpoint;
    QStringList m_hosts;   ///< allowed Host values, lower case
    QStringList m_origins; ///< allowed Origin values
    std::unique_ptr<oauth::Store> m_store;
    std::unique_ptr<oauth::AuthServer> m_auth;
    std::unique_ptr<http::Server> m_http;
    std::unique_ptr<McpDispatcher> m_dispatcher;
    QHash<QString, Session> m_sessions;
    QTimer *m_maintenance = nullptr;
    QDateTime m_certModified;
    QDateTime m_keyModified;
};

/// The state file of `mcp serve`, `mcp password`, `mcp clients` and `mcp revoke`: --auth-file,
/// else mcp-remote.json in the notebook's device state; "" with `error`.
QString remoteAuthFile(Context &context, const QJsonObject &args, std::optional<Error> *error);

} // namespace brulion::cli
