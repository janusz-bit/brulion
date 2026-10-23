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
#pragma once

#include "controlprotocol.h"

#include <QObject>
#include <QPointer>
#include <QString>

#include <functional>
#include <memory>

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

Q_SIGNALS:
    /// A connection of another user was refused (its uid).
    void refused(qint64 uid);

private:
    void accept(QLocalSocket *socket);
    void readLines(QLocalSocket *socket);

    QLocalServer *m_server = nullptr;
    Handler m_handler;
    QString m_error;
};

} // namespace brulion::ipc
