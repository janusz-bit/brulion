#include "singleinstance.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFileInfo>
#include <QLockFile>
#include <QLocalServer>
#include <QLocalSocket>
#include <QStandardPaths>

using namespace Qt::StringLiterals;

namespace brulion::shell {

SingleInstance::SingleInstance(const QString &key, QObject *parent) : QObject(parent), m_key(key) { }

SingleInstance::~SingleInstance() = default;

QString SingleInstance::keyFor(const QString &appDataDir)
{
    const QByteArray hash = QCryptographicHash::hash(QDir::cleanPath(appDataDir).toUtf8(), QCryptographicHash::Sha1);
    const QString name = u"brulion-"_s + QString::fromLatin1(hash.toHex().left(16));
    // The socket goes into the user's private runtime folder ($XDG_RUNTIME_DIR, mode 0700) when
    // there is one: in the shared /tmp another user could create the name first and keep
    // Brulion from starting.
    const QString runtime = QStandardPaths::writableLocation(QStandardPaths::RuntimeLocation);
    return runtime.isEmpty() ? name : QDir(runtime).absoluteFilePath(name);
}

bool SingleInstance::sendToPrimary(const QByteArray &message, int timeoutMs)
{
    QLocalSocket socket;
    socket.connectToServer(m_key);
    if (!socket.waitForConnected(timeoutMs))
        return false;
    socket.write(message + '\n');
    socket.waitForBytesWritten(timeoutMs);
    socket.disconnectFromServer();
    if (socket.state() != QLocalSocket::UnconnectedState)
        socket.waitForDisconnected(timeoutMs);
    return true;
}

bool SingleInstance::listen()
{
    if (m_server)
        return m_server->isListening();
    // The lock decides who is primary. The socket alone cannot: QLocalServer with
    // UserAccessOption renames its socket into place, replacing a live one, so two launches at
    // the same moment would both "win". A lock left by a crashed instance is stale (its process
    // is gone) and taken over; a running primary's lock never is (no age limit).
    const QFileInfo key(m_key);
    m_lock = std::make_unique<QLockFile>(key.isAbsolute() ? m_key + u".lock"_s
                                                          : QDir::temp().filePath(m_key + u".lock"_s));
    m_lock->setStaleLockTime(0);
    if (!m_lock->tryLock(0)) {
        m_lock.reset();
        return false; // another instance is the primary (or becoming it): send it the message
    }
    QLocalServer::removeServer(m_key); // a socket file left behind by a crashed instance
    m_server = new QLocalServer(this);
    m_server->setSocketOptions(QLocalServer::UserAccessOption);
    if (!m_server->listen(m_key))
        return false;
    connect(m_server, &QLocalServer::newConnection, this, [this] {
        while (QLocalSocket *socket = m_server->nextPendingConnection()) {
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

} // namespace brulion::shell
