#include "controlclient.h"

#include "controlprotocol.h"

#include <QDeadlineTimer>
#include <QJsonDocument>
#include <QLocalSocket>

using namespace Qt::StringLiterals;

namespace brulion::ipc {

ControlClient::ControlClient(const QString &key) : m_key(key) { }

ControlClient::~ControlClient()
{
    if (m_socket && m_socket->state() == QLocalSocket::ConnectedState) {
        m_socket->disconnectFromServer();
        if (m_socket->state() != QLocalSocket::UnconnectedState)
            m_socket->waitForDisconnected(500);
    }
}

bool ControlClient::connect(int timeoutMs)
{
    m_error.clear();
    m_socket = std::make_unique<QLocalSocket>();
    m_socket->connectToServer(m_key);
    if (!m_socket->waitForConnected(timeoutMs)) {
        m_socket.reset();
        return false;
    }
#ifdef Q_OS_LINUX
    // Only the app of this user: a socket another user created at that name (the shared temp
    // folder when there is no runtime folder) gets no request.
    if (const qint64 uid = peerUid(m_socket->socketDescriptor()); uid < 0 || uid != ownUid()) {
        m_error = uid < 0 ? u"Could not tell who listens at %1"_s.arg(m_key)
                          : u"Another user (uid %1) listens at %2: not this user's Brulion"_s.arg(uid).arg(m_key);
        m_socket->abort();
        m_socket.reset();
        return false;
    }
#endif
    return true;
}

bool ControlClient::isConnected() const
{
    return m_socket && m_socket->state() == QLocalSocket::ConnectedState;
}

ControlClient::Response ControlClient::call(const QString &method, const QJsonObject &params, int timeoutMs)
{
    Response out;
    if (!isConnected()) {
        out.transportError = true;
        out.code = u"disconnected"_s;
        out.message = u"Not connected to the app"_s;
        return out;
    }
    const qint64 id = m_nextId++;
    m_socket->write(encodeRequest(QJsonValue(id), method, params) + '\n');
    if (!m_socket->waitForBytesWritten(timeoutMs) && m_socket->bytesToWrite() > 0) {
        out.transportError = true;
        out.code = u"disconnected"_s;
        out.message = u"The request could not be sent"_s;
        return out;
    }
    const QDeadlineTimer deadline(timeoutMs);
    while (true) {
        while (m_socket->canReadLine()) {
            const QJsonObject response = QJsonDocument::fromJson(m_socket->readLine()).object();
            const QJsonValue rid = response.value(u"id"_s);
            if (!rid.isNull() && rid.toInteger(-1) != id)
                continue; // the answer to another request
            out.ok = response.value(u"ok"_s).toBool();
            out.result = response.value(u"result"_s);
            const QJsonObject error = response.value(u"error"_s).toObject();
            out.code = error.value(u"code"_s).toString();
            out.message = error.value(u"message"_s).toString();
            out.details = error.value(u"details"_s).toObject();
            if (!out.ok && out.code.isEmpty()) {
                out.code = QString::fromLatin1(ControlError::Failed);
                out.message = u"An answer without a result"_s;
            }
            return out;
        }
        if (m_socket->state() != QLocalSocket::ConnectedState) {
            out.transportError = true;
            out.code = u"disconnected"_s;
            out.message = u"The app closed the connection"_s;
            return out;
        }
        if (deadline.hasExpired() || !m_socket->waitForReadyRead(int(qMax<qint64>(1, deadline.remainingTime())))) {
            if (m_socket->canReadLine())
                continue;
            out.transportError = true;
            out.code = m_socket->state() == QLocalSocket::ConnectedState ? u"no_answer"_s : u"disconnected"_s;
            out.message =
                    out.code == u"no_answer" ? u"The app did not answer in time"_s : u"The app closed the connection"_s;
            return out;
        }
    }
}

} // namespace brulion::ipc
