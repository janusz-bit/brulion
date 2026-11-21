#include "controlserver.h"

#include <QFile>
#include <QLocalServer>
#include <QLocalSocket>
#include <QRandomGenerator>

#include <utility>

using namespace Qt::StringLiterals;

namespace brulion::ipc {

namespace {

void writeLine(QLocalSocket *socket, const QByteArray &line)
{
    socket->write(line + '\n');
    socket->flush();
}

} // namespace

ControlReply::ControlReply(QLocalSocket *socket, const QJsonValue &id) : m_socket(socket), m_id(id) { }

void ControlReply::ok(const QJsonValue &result) const
{
    if (!std::exchange(*m_sent, true) && connected())
        writeLine(m_socket, encodeResult(m_id, result));
}

void ControlReply::fail(const QString &code, const QString &message, const QJsonObject &details) const
{
    if (!std::exchange(*m_sent, true) && connected())
        writeLine(m_socket, encodeError(m_id, code, message, details));
}

bool ControlReply::connected() const
{
    return m_socket && m_socket->state() == QLocalSocket::ConnectedState;
}

ControlServer::ControlServer(QObject *parent) : QObject(parent) { }

ControlServer::~ControlServer()
{
    close();
}

bool ControlServer::listen(const QString &key)
{
    close();
    // Only in the user's own folder (controlFolderProblem()): never the shared temp folder.
    m_error = controlFolderProblem(key);
    if (!m_error.isEmpty())
        return false;
    // A live server there (another Brulion run with --no-single-instance) keeps its socket; a
    // file left by a crashed run is replaced.
    {
        QLocalSocket probe;
        probe.connectToServer(key);
        if (probe.waitForConnected(300)) {
            probe.disconnectFromServer();
            m_error = u"Another program already listens at %1"_s.arg(key);
            return false;
        }
    }
    QLocalServer::removeServer(key);
    auto server = std::make_unique<QLocalServer>();
    server->setSocketOptions(QLocalServer::UserAccessOption);
    if (!server->listen(key)) {
        m_error = server->errorString();
        return false;
    }
#ifdef Q_OS_UNIX
    // Owner only: read and write for the user, nothing for anybody else (UserAccessOption gives
    // 0700; a socket needs no execute bit). On Windows the named pipe has no file: there
    // UserAccessOption gives it an access list of this user alone.
    QFile::setPermissions(server->fullServerName(), QFileDevice::ReadOwner | QFileDevice::WriteOwner);
#endif
    m_server = std::move(server);
    connect(m_server.get(), &QLocalServer::newConnection, this, [this] {
        while (QLocalSocket *socket = m_server->nextPendingConnection())
            accept(socket);
    });
    return true;
}

void ControlServer::close()
{
    if (!m_server)
        return;
    const QList<QLocalSocket *> sockets = m_server->findChildren<QLocalSocket *>();
    for (QLocalSocket *socket : sockets)
        socket->abort();
    m_server.reset(); // closes it (the socket file is removed) and deletes its connections
}

void ControlServer::setOpen(bool open)
{
    if (m_open == open)
        return;
    m_open = open;
    if (open || !m_server)
        return;
    // Closed to everyone without a grant: their connections end now.
    const QList<QLocalSocket *> sockets = m_server->findChildren<QLocalSocket *>();
    for (QLocalSocket *socket : sockets) {
        if (!m_peers.value(socket).granted)
            socket->abort();
    }
}

QString ControlServer::addGrant(const QString &client)
{
    quint32 words[8];
    QRandomGenerator::system()->fillRange(words);
    QString token;
    for (const quint32 w : words)
        token += u"%1"_s.arg(w, 8, 16, u'0');
    m_grants.insert(token, cleanClientName(client));
    return token;
}

bool ControlServer::revokeGrant(const QString &token)
{
    if (!m_grants.remove(token))
        return false;
    QList<QLocalSocket *> drop;
    for (auto it = m_peers.cbegin(); it != m_peers.cend(); ++it) {
        if (it->granted && it->token == token)
            drop.append(it.key());
    }
    for (QLocalSocket *socket : std::as_const(drop))
        socket->abort();
    return true;
}

std::optional<QString> ControlServer::grantFor(const QString &token) const
{
    // Every grant compared in full: the time taken tells nothing of how much of a token matched.
    std::optional<QString> found;
    const QByteArray given = token.toLatin1();
    for (auto it = m_grants.cbegin(); it != m_grants.cend(); ++it) {
        const QByteArray known = it.key().toLatin1();
        unsigned char diff = given.size() == known.size() ? 0 : 1;
        for (qsizetype i = 0; i < known.size(); ++i)
            diff |= static_cast<unsigned char>(known.at(i) ^ (i < given.size() ? given.at(i) : 0));
        if (diff == 0)
            found = it.value();
    }
    return found;
}

bool ControlServer::introduce(Peer &peer, const ControlRequest &request, QLocalSocket *socket)
{
    const QJsonValue token = request.params.value(u"token"_s);
    if (token.isString()) {
        const std::optional<QString> client = grantFor(token.toString());
        if (!client) {
            writeLine(socket, encodeError(request.id, QString::fromLatin1(ControlError::Unauthorized),
                                          u"The token is no grant of this app (its agent stopped, or it was "
                                          u"never one)"_s));
            return false;
        }
        peer.granted = true;
        peer.token = token.toString();
        peer.client = *client;
        return true;
    }
    // A name for a connection without a grant (a grant's name stays).
    if (!peer.granted && request.params.contains(u"client"_s))
        peer.client = cleanClientName(request.params.value(u"client"_s).toString());
    return true;
}

bool ControlServer::isListening() const
{
    return m_server && m_server->isListening();
}

QString ControlServer::socketPath() const
{
    return isListening() ? m_server->fullServerName() : QString();
}

void ControlServer::accept(QLocalSocket *socket)
{
    // Linux and Windows tell who connected: anyone else, or someone it cannot tell, is refused.
    // (Elsewhere the socket file's permissions decide.)
    if (const qint64 uid = peerUid(socket->socketDescriptor()); !trustedPeer(uid, ownUid())) {
        Q_EMIT refused(uid);
        socket->abort();
        socket->deleteLater();
        return;
    }
    m_peers.insert(socket, Peer{});
    connect(socket, &QObject::destroyed, this, [this, socket] { m_peers.remove(socket); });
    connect(socket, &QLocalSocket::readyRead, this, [this, socket] { readLines(socket); });
    connect(socket, &QLocalSocket::disconnected, socket, &QObject::deleteLater);
    readLines(socket);
}

void ControlServer::readLines(QLocalSocket *socket)
{
    // The handler may close the server (the remote control turned off), which deletes the socket.
    const QPointer<QLocalSocket> alive(socket);
    while (alive && socket->state() == QLocalSocket::ConnectedState) {
        if (!socket->canReadLine()) {
            // A line that never ends: refuse it rather than buffer without limit.
            if (socket->bytesAvailable() > MaxLineBytes) {
                writeLine(socket, encodeError(QJsonValue(), QString::fromLatin1(ControlError::InvalidRequest),
                                              u"The request is longer than %1 bytes"_s.arg(MaxLineBytes)));
                socket->disconnectFromServer();
            }
            return;
        }
        const QByteArray line = socket->readLine();
        if (line.trimmed().isEmpty())
            continue;
        ControlFailure failure;
        std::optional<ControlRequest> request = parseRequest(line, &failure);
        if (!request) {
            writeLine(socket, encodeError(failure));
            continue;
        }
        Peer &peer = m_peers[socket];
        // Not open (only grants): nothing but hello with a token before it showed one.
        const bool hello = request->method == u"hello";
        if (!m_open && !peer.granted && !(hello && request->params.value(u"token"_s).isString())) {
            writeLine(socket, encodeError(request->id, QString::fromLatin1(ControlError::Unauthorized),
                                          u"The remote control is off: only an agent the app started may connect, "
                                          u"with its token (hello {token})"_s));
            continue;
        }
        if (const auto problem = checkRequest(*request)) {
            writeLine(socket, encodeError(*problem));
            continue;
        }
        if (hello && !introduce(peer, *request, socket))
            continue;
        request->client = peer.client;
        request->granted = peer.granted;
        const ControlReply reply(socket, request->id);
        if (m_handler)
            m_handler(*request, reply);
        else
            reply.fail(QString::fromLatin1(ControlError::NotReady), u"The app is not ready for requests"_s);
    }
}

} // namespace brulion::ipc
