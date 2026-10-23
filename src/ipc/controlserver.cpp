#include "controlserver.h"

#include <QFile>
#include <QLocalServer>
#include <QLocalSocket>

#ifdef Q_OS_LINUX
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>
#endif

using namespace Qt::StringLiterals;

namespace brulion::ipc {

namespace {

/// The uid of the process at the other end of `socket`; -1 when unknown (not Linux).
qint64 peerUid(QLocalSocket *socket)
{
#ifdef Q_OS_LINUX
    ucred credentials{};
    socklen_t size = sizeof(credentials);
    if (::getsockopt(int(socket->socketDescriptor()), SOL_SOCKET, SO_PEERCRED, &credentials, &size) == 0)
        return qint64(credentials.uid);
#else
    Q_UNUSED(socket);
#endif
    return -1;
}

qint64 ownUid()
{
#ifdef Q_OS_LINUX
    return qint64(::geteuid());
#else
    return -1;
#endif
}

} // namespace

ControlReply::ControlReply(QLocalSocket *socket, const QJsonValue &id) : m_socket(socket), m_id(id) { }

void ControlReply::ok(const QJsonValue &result) const
{
    if (*m_sent)
        return;
    *m_sent = true;
    if (m_socket && m_socket->state() == QLocalSocket::ConnectedState) {
        m_socket->write(encodeResult(m_id, result) + '\n');
        m_socket->flush();
    }
}

void ControlReply::fail(const QString &code, const QString &message, const QJsonObject &details) const
{
    if (*m_sent)
        return;
    *m_sent = true;
    if (m_socket && m_socket->state() == QLocalSocket::ConnectedState) {
        m_socket->write(encodeError(m_id, code, message, details) + '\n');
        m_socket->flush();
    }
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
    m_error.clear();
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
    m_server = new QLocalServer(this);
    m_server->setSocketOptions(QLocalServer::UserAccessOption);
    if (!m_server->listen(key)) {
        m_error = m_server->errorString();
        delete m_server;
        m_server = nullptr;
        return false;
    }
    // Owner only: read and write for the user, nothing for anybody else (UserAccessOption gives
    // 0700; a socket needs no execute bit).
    QFile::setPermissions(m_server->fullServerName(), QFileDevice::ReadOwner | QFileDevice::WriteOwner);
    connect(m_server, &QLocalServer::newConnection, this, [this] {
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
    m_server->close(); // removes the socket file
    delete m_server;
    m_server = nullptr;
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
    const qint64 uid = peerUid(socket);
    const qint64 own = ownUid();
    if (uid >= 0 && own >= 0 && uid != own) {
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
    while (socket->state() == QLocalSocket::ConnectedState) {
        if (!socket->canReadLine()) {
            // A line that never ends: refuse it rather than buffer without limit.
            if (socket->bytesAvailable() > MaxLineBytes) {
                socket->write(encodeError(QJsonValue(), QString::fromLatin1(ControlError::InvalidRequest),
                                          u"The request is longer than %1 bytes"_s.arg(MaxLineBytes))
                              + '\n');
                socket->flush();
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
            socket->write(encodeError(failure) + '\n');
            socket->flush();
            continue;
        }
        if (const auto problem = checkRequest(*request)) {
            socket->write(encodeError(*problem) + '\n');
            socket->flush();
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
