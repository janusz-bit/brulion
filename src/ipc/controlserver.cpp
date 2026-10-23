#include "controlserver.h"

#include <QFile>
#include <QLocalServer>
#include <QLocalSocket>

using namespace Qt::StringLiterals;

namespace brulion::ipc {

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
    // Only in the user's own folder (controlFolderProblem()): never the shared temp folder.
    if (const QString problem = controlFolderProblem(key); !problem.isEmpty()) {
        m_error = problem;
        return false;
    }
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
    // Linux tells who connected: anyone else, or someone it cannot tell, is refused. (Elsewhere the
    // socket file's permissions decide.)
    const qint64 uid = peerUid(socket->socketDescriptor());
    const qint64 own = ownUid();
#ifdef Q_OS_LINUX
    const bool foreign = uid < 0 || uid != own;
#else
    const bool foreign = uid >= 0 && own >= 0 && uid != own;
#endif
    if (foreign) {
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
