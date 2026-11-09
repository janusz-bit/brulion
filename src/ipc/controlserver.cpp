#include "controlserver.h"

#include <QFile>
#include <QLocalServer>
#include <QLocalSocket>

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
        const std::optional<ControlRequest> request = parseRequest(line, &failure);
        if (!request) {
            writeLine(socket, encodeError(failure));
            continue;
        }
        if (const auto problem = checkRequest(*request)) {
            writeLine(socket, encodeError(*problem));
            continue;
        }
        const ControlReply reply(socket, request->id);
        if (m_handler)
            m_handler(*request, reply);
        else
            reply.fail(QString::fromLatin1(ControlError::NotReady), u"The app is not ready for requests"_s);
    }
}

} // namespace brulion::ipc
