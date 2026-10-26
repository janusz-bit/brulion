#include "singleinstance.h"

#include "controlprotocol.h"

#include <QCryptographicHash>
#include <QDir>
#include <QLocalServer>
#include <QLocalSocket>
#include <QLockFile>
#include <QStandardPaths>

using namespace Qt::StringLiterals;

namespace brulion::ipc {

SingleInstance::SingleInstance(const QString &key, QObject *parent) : QObject(parent), m_key(key) { }

SingleInstance::~SingleInstance() = default;

QString SingleInstance::keyFor(const QString &appDataDir)
{
    const QByteArray hash = QCryptographicHash::hash(QDir::cleanPath(appDataDir).toUtf8(), QCryptographicHash::Sha1);
    const QString name = u"brulion-"_s + QString::fromLatin1(hash.toHex().left(16));
    // The socket goes into the user's private runtime folder ($XDG_RUNTIME_DIR, mode 0700) when
    // there is one. Without one the key is a bare name, which QLocalServer would put into the
    // shared temp folder: listen() refuses it (another user could create the name first and keep
    // Brulion from starting).
#ifdef Q_OS_WIN
    // Windows: the socket is a named pipe (\\.\pipe\<key>, machine-wide names: the key carries
    // the user's own path) and only its lock file is in the folder — the user's temp folder
    // (%TEMP%, inside the profile, private to the user). RuntimeLocation would be the profile
    // folder itself, where the lock file would show.
    const QString runtime = QDir::tempPath();
#else
    const QString runtime = QStandardPaths::writableLocation(QStandardPaths::RuntimeLocation);
#endif
    return runtime.isEmpty() ? name : QDir(runtime).absoluteFilePath(name);
}

std::unique_ptr<QLocalSocket> SingleInstance::connectToPrimary(int timeoutMs) const
{
    m_error.clear();
    auto socket = std::make_unique<QLocalSocket>();
    socket->connectToServer(m_key);
    if (!socket->waitForConnected(timeoutMs))
        return nullptr;
    // Only this user's Brulion: a socket another user created at that name (the shared temp folder
    // when there is no runtime folder) is told nothing, not even that this one starts.
    m_error = listenerProblem(peerUid(socket->socketDescriptor()), m_key);
    if (!m_error.isEmpty()) {
        socket->abort();
        return nullptr;
    }
    return socket;
}

bool SingleInstance::sendToPrimary(const QByteArray &message, int timeoutMs)
{
    const std::unique_ptr<QLocalSocket> socket = connectToPrimary(timeoutMs);
    if (!socket)
        return false;
    socket->write(message + '\n');
    socket->waitForBytesWritten(timeoutMs);
    socket->disconnectFromServer();
    if (socket->state() != QLocalSocket::UnconnectedState)
        socket->waitForDisconnected(timeoutMs);
    return true;
}

bool SingleInstance::isPrimaryRunning(int timeoutMs) const
{
    const std::unique_ptr<QLocalSocket> socket = connectToPrimary(timeoutMs);
    if (!socket)
        return false;
    socket->disconnectFromServer();
    if (socket->state() != QLocalSocket::UnconnectedState)
        socket->waitForDisconnected(timeoutMs);
    return true;
}

bool SingleInstance::listen()
{
    if (m_server)
        return m_server->isListening();
    m_error.clear();
    // Only in a folder of this user that nobody else can change: in the shared temp folder (a bare
    // key) another user could take the socket's or the lock's name first — Brulion would not start
    // — or listen there and receive what a second launch and brulion-cli send.
    if (const QString problem = socketFolderProblem(m_key, u"the single-instance socket"_s); !problem.isEmpty()) {
        m_error = problem;
        return false;
    }
    // The lock decides who is primary. The socket alone cannot: QLocalServer with
    // UserAccessOption renames its socket into place, replacing a live one, so two launches at
    // the same moment would both "win". A lock left by a crashed instance is stale (its process
    // is gone) and taken over; a running primary's lock never is (no age limit).
    m_lock = std::make_unique<QLockFile>(m_key + u".lock"_s);
    m_lock->setStaleLockTime(0);
    if (!m_lock->tryLock(0)) {
        if (m_lock->error() != QLockFile::LockFailedError)
            m_error = u"Cannot create the lock file %1.lock"_s.arg(m_key);
        m_lock.reset();
        return false; // another instance is the primary (or becoming it): send it the message
    }
    QLocalServer::removeServer(m_key); // a socket file left behind by a crashed instance
    auto *server = new QLocalServer(this);
    server->setSocketOptions(QLocalServer::UserAccessOption);
    if (!server->listen(m_key)) {
        m_error = u"Cannot listen at %1: %2"_s.arg(m_key, server->errorString());
        delete server;
        m_lock.reset();
        return false;
    }
    m_server = server;
    connect(m_server, &QLocalServer::newConnection, this, [this] {
        while (QLocalSocket *socket = m_server->nextPendingConnection()) {
            // Linux and Windows tell who connected: another user, or one it cannot tell, is dropped
            // unheard. (Elsewhere the socket file's permissions decide.)
            if (const qint64 uid = peerUid(socket->socketDescriptor()); !trustedPeer(uid, ownUid())) {
                Q_EMIT refused(uid);
                socket->abort();
                socket->deleteLater();
                continue;
            }
            const auto drain = [this, socket] {
                while (socket->canReadLine()) {
                    const QByteArray line = socket->readLine().trimmed();
                    if (!line.isEmpty())
                        Q_EMIT messageReceived(line);
                }
            };
            connect(socket, &QLocalSocket::readyRead, this, drain);
            // The sender disconnects right after writing: read what is left first.
            connect(socket, &QLocalSocket::disconnected, this, [socket, drain] {
                drain();
                socket->deleteLater();
            });
            drain();
        }
    });
    return true;
}

bool SingleInstance::isListening() const
{
    return m_server && m_server->isListening();
}

} // namespace brulion::ipc
