#include "asynccontrolclient.h"

#include "controlprotocol.h"

#include <QJsonDocument>
#include <QLocalSocket>
#include <QTimer>

#include <algorithm>

using namespace Qt::StringLiterals;

namespace brulion::ipc {

namespace {

/// The longest answer line taken (the app's answers are bounded far below it).
constexpr qint64 kMaxAnswerBytes = 32LL * 1024 * 1024;

AsyncControlClient::Response transportFailure(const QString &code, const QString &message)
{
    AsyncControlClient::Response out;
    out.transportError = true;
    out.code = code;
    out.message = message;
    return out;
}

AsyncControlClient::Response responseOf(const QJsonObject &o)
{
    AsyncControlClient::Response out;
    out.ok = o.value(u"ok"_s).toBool();
    out.result = o.value(u"result"_s);
    const QJsonObject error = o.value(u"error"_s).toObject();
    out.code = error.value(u"code"_s).toString();
    out.message = error.value(u"message"_s).toString();
    out.details = error.value(u"details"_s).toObject();
    if (!out.ok && out.code.isEmpty()) {
        out.code = QString::fromLatin1(ControlError::Failed);
        out.message = u"An answer without a result"_s;
    }
    return out;
}

} // namespace

AsyncControlClient::AsyncControlClient(const QString &key, const QString &token, const QString &client,
                                       QObject *parent)
    : QObject(parent), m_key(key), m_token(token.trimmed()), m_client(cleanClientName(client))
{
    m_retry = new QTimer(this);
    m_retry->setSingleShot(true);
    connect(m_retry, &QTimer::timeout, this, &AsyncControlClient::connectNow);
}

AsyncControlClient::~AsyncControlClient()
{
    m_running = false;
    if (m_socket) {
        m_socket->disconnect(this);
        m_socket->abort();
    }
    // (Nobody to answer: the calls waiting are dropped with the object.)
    for (Pending &p : m_pending)
        delete p.timer;
    m_pending.clear();
}

void AsyncControlClient::start()
{
    if (m_running)
        return;
    m_running = true;
    m_failures = 0;
    connectNow();
}

void AsyncControlClient::stop()
{
    m_running = false;
    m_retry->stop();
    if (m_socket) {
        QLocalSocket *s = m_socket;
        s->disconnect(this);
        s->abort();
        s->deleteLater();
    }
    m_socket = nullptr;
    m_helloId = 0;
    const QHash<qint64, Pending> pending = std::exchange(m_pending, {});
    setReady(false);
    for (const Pending &p : pending) {
        delete p.timer;
        failLater(p.done, u"disconnected"_s, u"The link to the app was ended"_s);
    }
}

void AsyncControlClient::connectNow()
{
    if (!m_running || m_socket)
        return;
    auto *socket = new QLocalSocket(this);
    m_socket = socket;
    connect(socket, &QLocalSocket::connected, this, &AsyncControlClient::connected);
    connect(socket, &QLocalSocket::readyRead, this, &AsyncControlClient::readLines);
    connect(socket, &QLocalSocket::disconnected, this, [this] { dropped(u"The app closed the connection"_s); });
    connect(socket, &QLocalSocket::errorOccurred, this, [this, socket](QLocalSocket::LocalSocketError) {
        if (socket == m_socket)
            dropped(socket->errorString());
    });
    socket->connectToServer(m_key);
}

void AsyncControlClient::connected()
{
    QLocalSocket *socket = m_socket;
    if (!socket)
        return;
    // Only this user's process: another one could answer in the app's place and read what is sent.
    if (const QString problem = listenerProblem(peerUid(socket->socketDescriptor()), m_key); !problem.isEmpty()) {
        dropped(problem);
        return;
    }
    QJsonObject who;
    if (!m_token.isEmpty())
        who.insert(u"token"_s, m_token);
    if (!m_client.isEmpty())
        who.insert(u"client"_s, m_client);
    m_helloId = m_nextId++;
    socket->write(encodeRequest(QJsonValue(m_helloId), u"hello"_s, who) + '\n');
}

void AsyncControlClient::readLines()
{
    // An answer may end the link (a refused hello) or the object's user may stop it meanwhile.
    while (m_socket && m_socket->state() == QLocalSocket::ConnectedState) {
        if (!m_socket->canReadLine()) {
            if (m_socket->bytesAvailable() > kMaxAnswerBytes)
                dropped(u"The app's answer is longer than %1 bytes"_s.arg(kMaxAnswerBytes));
            return;
        }
        const QJsonObject o = QJsonDocument::fromJson(m_socket->readLine()).object();
        const QJsonValue id = o.value(u"id"_s);
        if (!id.isDouble())
            continue; // (no answer of ours: a line that was no request, answered with null)
        const qint64 n = id.toInteger(-1);
        const Response r = responseOf(o);
        if (n == m_helloId && m_helloId != 0) {
            m_helloId = 0;
            if (!r.ok) {
                // The token is no grant (any more): said once, tried again after a while.
                dropped(u"The app refused the grant: %1 (%2)"_s.arg(r.message, r.code));
                return;
            }
            m_hello = r.result.toObject();
            m_error.clear();
            m_failures = 0;
            setReady(true);
            continue;
        }
        answer(n, r);
    }
}

void AsyncControlClient::dropped(const QString &why)
{
    if (!m_socket)
        return;
    QLocalSocket *s = m_socket;
    m_socket = nullptr;
    s->disconnect(this);
    s->abort();
    s->deleteLater();
    m_helloId = 0;
    m_hello = {};
    m_error = why;
    const QHash<qint64, Pending> pending = std::exchange(m_pending, {});
    setReady(false);
    for (const Pending &p : pending) {
        delete p.timer;
        failLater(p.done, u"disconnected"_s, u"The link to the app dropped: %1"_s.arg(why));
    }
    if (!m_running)
        return;
    const int delay = m_delays.isEmpty() ? 1000 : m_delays.at(std::min<qsizetype>(m_failures, m_delays.size() - 1));
    ++m_failures;
    m_retry->start(delay);
}

void AsyncControlClient::setReady(bool ready)
{
    if (m_ready == ready)
        return;
    m_ready = ready;
    Q_EMIT readyChanged(ready);
}

void AsyncControlClient::answer(qint64 id, const Response &response)
{
    const auto it = m_pending.find(id);
    if (it == m_pending.end())
        return; // (one that timed out: its late answer dropped)
    const Pending p = it.value();
    m_pending.erase(it);
    delete p.timer;
    p.done(response);
}

void AsyncControlClient::failLater(Done done, const QString &code, const QString &message)
{
    QTimer::singleShot(0, this, [done = std::move(done), code, message] { done(transportFailure(code, message)); });
}

void AsyncControlClient::call(const QString &method, const QJsonObject &params, int timeoutMs, Done done)
{
    if (!m_ready || !m_socket) {
        failLater(std::move(done), u"disconnected"_s,
                  m_error.isEmpty() ? u"Not connected to the app"_s : u"Not connected to the app: %1"_s.arg(m_error));
        return;
    }
    const qint64 id = m_nextId++;
    auto *timer = new QTimer(this);
    timer->setSingleShot(true);
    connect(timer, &QTimer::timeout, this, [this, id] {
        const auto it = m_pending.find(id);
        if (it == m_pending.end())
            return;
        const Pending p = it.value();
        m_pending.erase(it);
        p.timer->deleteLater();
        p.done(transportFailure(u"no_answer"_s, u"The app did not answer in time"_s));
    });
    m_pending.insert(id, Pending{std::move(done), timer});
    timer->start(std::max(0, timeoutMs));
    m_socket->write(encodeRequest(QJsonValue(id), method, params) + '\n');
}

} // namespace brulion::ipc
