// ControlServer — the socket of the remote control (controlprotocol.h): a QLocalServer at
// controlKeyFor(<single-instance key>) in the user's runtime folder — only there, never in the
// shared temp folder (controlFolderProblem()) —, the socket file owner-only (0600), connections
// of other users refused (their credentials are checked: SO_PEERCRED).
// Requests are parsed and checked (checkRequest()); the handler answers each one through its
// ControlReply, now or later (waitFor), or not at all when the client left meanwhile.
//
//   ControlServer server;
//   server.setHandler([](const ControlRequest &request, ControlReply reply) { reply.ok({…}); });
//   server.listen(controlKeyFor(SingleInstance::keyFor(appData)));   // off: server.close()
//
// It never takes over a socket another live server holds (a second Brulion run with
// --no-single-instance): listen() fails then. Library brulion_ipc (desktop).
//
// Grants (docs/CLI.md "Live changes"): addGrant(client) makes a token (256 random bits, hex) for a
// program the app starts — an AI agent the user let change the app — which shows it in its
// `hello {token}`; that connection is then the grant's client (ControlRequest::client, granted).
// While the server is not open (setOpen(false): the remote control's setting off, the socket
// there only for grants) every request of a connection that showed no grant's token is answered
// `unauthorized`; a wrong token is refused also while it is open. revokeGrant() ends the grant
// and drops the connections that showed it. Grants outlive close() and listen(). A grant of the
// chat alone (GrantScope::Chat: every agent the app starts with Brulion's tools, one per
// conversation) lets its connection call hello and the chat's methods (MethodSpec::chat) only:
// anything else is answered `unauthorized`. The handler learns the token a request came with
// (ControlRequest::grant), and whether it is such a grant (chatOnly).
#pragma once

#include "controlprotocol.h"

#include <QHash>
#include <QObject>
#include <QPointer>
#include <QString>

#include <functional>
#include <memory>
#include <optional>

class QLocalServer;
class QLocalSocket;

namespace brulion::ipc {

/// The answer to one request: call ok() or fail() once (later calls and replies to a client that
/// disconnected are dropped). Cheap to copy.
class ControlReply {
public:
    ControlReply() = default;
    ControlReply(QLocalSocket *socket, const QJsonValue &id);

    QJsonValue id() const { return m_id; }
    void ok(const QJsonValue &result = QJsonObject()) const;
    void fail(const QString &code, const QString &message, const QJsonObject &details = {}) const;
    /// The client is still connected.
    bool connected() const;

private:
    QPointer<QLocalSocket> m_socket;
    QJsonValue m_id;
    std::shared_ptr<bool> m_sent = std::make_shared<bool>(false);
};

/// What a grant lets its connection do.
enum class GrantScope {
    Full, ///< every method (an AI agent let change the app)
    Chat, ///< hello and the chat's methods (MethodSpec::chat)
};

class ControlServer : public QObject {
    Q_OBJECT
public:
    using Handler = std::function<void(const ControlRequest &, ControlReply)>;

    explicit ControlServer(QObject *parent = nullptr);
    ~ControlServer() override;

    void setHandler(Handler handler) { m_handler = std::move(handler); }
    /// Listen at `key` (a socket path in a folder only this user can change). False with
    /// errorString() for a bare name or a folder others can write to (controlFolderProblem()),
    /// when another server answers there or the socket cannot be created.
    bool listen(const QString &key);
    /// Stop listening and drop every connection.
    void close();
    bool isListening() const;
    /// The socket file ("" when not listening).
    QString socketPath() const;
    QString errorString() const { return m_error; }

    /// Serve connections without a grant (true, the default) or only those that showed one's
    /// token; turned off, the connections without one are dropped.
    void setOpen(bool open);
    bool isOpen() const { return m_open; }
    /// A new grant for `client` (its name in the app, cleanClientName()): its token.
    QString addGrant(const QString &client, GrantScope scope = GrantScope::Full);
    /// Ends the grant of `token` and drops its connections; false when there is none.
    bool revokeGrant(const QString &token);
    int grantCount() const { return int(m_grants.size()); }

Q_SIGNALS:
    /// A connection of another user was refused (its uid).
    void refused(qint64 uid);

private:
    struct Peer {
        bool granted = false;
        QString client;
        QString token;
        GrantScope scope = GrantScope::Full;
    };
    struct Grant {
        QString client;
        GrantScope scope = GrantScope::Full;
    };
    void accept(QLocalSocket *socket);
    void readLines(QLocalSocket *socket);
    /// The grant of `token` (compared in constant time); nullopt for none.
    std::optional<Grant> grantFor(const QString &token) const;
    /// hello's token and client name taken in by `peer`; false (and answered) when the token is none.
    bool introduce(Peer &peer, const ControlRequest &request, QLocalSocket *socket);

    std::unique_ptr<QLocalServer> m_server;
    Handler m_handler;
    QString m_error;
    bool m_open = true;
    QHash<QString, Grant> m_grants; // by token
    QHash<QLocalSocket *, Peer> m_peers;
};

} // namespace brulion::ipc
