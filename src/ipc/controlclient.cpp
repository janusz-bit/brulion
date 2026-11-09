#include "controlclient.h"

#include "controlprotocol.h"

#include <QDeadlineTimer>
#include <QJsonDocument>
#include <QLocalSocket>

#include <algorithm>

using namespace Qt::StringLiterals;

namespace brulion::ipc {

namespace {

/// The request was not delivered or not answered: `code` "disconnected" or "no_answer".
ControlClient::Response transportFailure(const QString &code, const QString &message)
{
    ControlClient::Response out;
    out.transportError = true;
    out.code = code;
    out.message = message;
    return out;
}

} // namespace

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
    m_socket = connectToTrusted(m_key, timeoutMs, &m_error);
    return m_socket != nullptr;
}

bool ControlClient::isConnected() const
{
    return m_socket && m_socket->state() == QLocalSocket::ConnectedState;
}

ControlClient::Response ControlClient::call(const QString &method, const QJsonObject &params, int timeoutMs)
{
    if (!isConnected())
        return transportFailure(u"disconnected"_s, u"Not connected to the app"_s);
    const qint64 id = m_nextId++;
    m_socket->write(encodeRequest(QJsonValue(id), method, params) + '\n');
    if (!m_socket->waitForBytesWritten(timeoutMs) && m_socket->bytesToWrite() > 0)
        return transportFailure(u"disconnected"_s, u"The request could not be sent"_s);
    const QDeadlineTimer deadline(timeoutMs);
    while (true) {
        while (m_socket->canReadLine()) {
            const QJsonObject response = QJsonDocument::fromJson(m_socket->readLine()).object();
            const QJsonValue rid = response.value(u"id"_s);
            if (!rid.isNull() && rid.toInteger(-1) != id)
                continue; // the answer to another request
            Response out;
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
        if (m_socket->state() != QLocalSocket::ConnectedState)
            return transportFailure(u"disconnected"_s, u"The app closed the connection"_s);
        if (deadline.hasExpired() || !m_socket->waitForReadyRead(int(std::max<qint64>(1, deadline.remainingTime())))) {
            // Out of time, unless a line came or the app left meanwhile (both handled above).
            if (!m_socket->canReadLine() && m_socket->state() == QLocalSocket::ConnectedState)
                return transportFailure(u"no_answer"_s, u"The app did not answer in time"_s);
        }
    }
}

} // namespace brulion::ipc
