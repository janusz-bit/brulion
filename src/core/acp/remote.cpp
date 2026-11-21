#include "acp/remote.h"

#include <QElapsedTimer>
#include <QJsonArray>
#include <QJsonDocument>
#include <QTimer>

#include <algorithm>

using namespace Qt::StringLiterals;

namespace brulion::acp {

namespace {

/// The answer a request gets at most (a receive: the agent's messages since the last one — a
/// session's replayed history stays far below).
constexpr qint64 MaxAnswerBytes = 64LL * 1024 * 1024;
/// The time a request may take besides a receive's wait.
constexpr int RequestTimeoutMs = 30000;

QByteArray jsonBody(const QJsonObject &o)
{
    return QJsonDocument(o).toJson(QJsonDocument::Compact);
}

/// One line without its line end.
QByteArray trimmedLine(QByteArray line)
{
    while (line.endsWith('\n') || line.endsWith('\r'))
        line.chop(1);
    return line;
}

} // namespace

QUrl remoteUrl(const QUrl &base, const QString &endpoint)
{
    QUrl url = base;
    QString path = url.path();
    while (path.endsWith(u'/'))
        path.chop(1);
    const qsizetype query = endpoint.indexOf(u'?');
    url.setPath(path + u'/' + RemoteApiPrefix + (query < 0 ? endpoint : endpoint.left(query)));
    if (query >= 0)
        url.setQuery(endpoint.mid(query + 1));
    return url;
}

QUrl normalizeRemoteBase(const QString &typed)
{
    QString text = typed.trimmed();
    if (text.isEmpty())
        return {};
    if (!text.contains(u"://"_s))
        text.prepend(u"https://"_s);
    QUrl url(text, QUrl::StrictMode);
    if (!url.isValid() || url.host().isEmpty() || (url.scheme() != u"https" && url.scheme() != u"http")
        || !url.userInfo().isEmpty() || url.hasQuery() || url.hasFragment())
        return {};
    QString path = url.path();
    if (const qsizetype api = path.indexOf(u"/acp/v1"_s); api >= 0)
        path.truncate(api);
    while (path.endsWith(u'/'))
        path.chop(1);
    url.setPath(path);
    return url;
}

QString normalizePairingCode(const QString &typed)
{
    QString out;
    for (const QChar c : typed) {
        if (c.isSpace() || c == u'-' || c == u'–' || c == u'—')
            continue;
        out += c.toUpper();
    }
    return out;
}

std::optional<RemoteInfo> RemoteInfo::fromJson(const QJsonObject &o)
{
    if (!o.value(u"protocol"_s).isDouble())
        return std::nullopt;
    RemoteInfo info;
    info.protocol = o.value(u"protocol"_s).toInt();
    info.version = o.value(u"version"_s).toString();
    info.host = o.value(u"host"_s).toString();
    info.root = o.value(u"root"_s).toString();
    info.tools = o.value(u"tools"_s).toBool();
    info.sync = o.value(u"sync"_s).toObject();
    for (const QJsonValue &v : o.value(u"agents"_s).toArray()) {
        const QJsonObject a = v.toObject();
        const QString id = a.value(u"id"_s).toString();
        if (id.isEmpty())
            continue;
        const QString name = a.value(u"name"_s).toString();
        info.agents.append({id, name.isEmpty() ? id : name, a.value(u"command"_s).toString()});
    }
    return info;
}

RemoteError remoteErrorOf(const sync::HttpResponse &response, QJsonObject *object)
{
    RemoteError e;
    e.status = response.status;
    if (response.status == 0) {
        e.kind = RemoteError::Kind::Network;
        e.detail = response.timedOut                 ? u"no answer in time"_s
                   : response.networkError.isEmpty() ? u"no answer"_s
                                                     : response.networkError;
        return e;
    }
    QJsonParseError parse;
    const QJsonDocument doc = QJsonDocument::fromJson(response.body, &parse);
    const QJsonObject body = doc.isObject() ? doc.object() : QJsonObject();
    if (object)
        *object = body;
    const QJsonObject error = body.value(u"error"_s).toObject();
    e.code = error.value(u"code"_s).toString();
    e.detail = error.value(u"message"_s).toString();
    if (response.status == 401) {
        e.kind = RemoteError::Kind::Unauthorized;
    } else if (response.status >= 500) {
        e.kind = RemoteError::Kind::Server;
    } else if (response.status >= 400) {
        e.kind = RemoteError::Kind::Refused;
    } else if (response.status < 200 || response.status >= 300) {
        e.kind = RemoteError::Kind::Malformed;
        e.detail = u"HTTP %1"_s.arg(response.status);
    } else if (response.status != 204 && !doc.isObject()) {
        e.kind = RemoteError::Kind::Malformed;
        e.detail = u"the answer is no JSON object"_s;
    }
    if (e.kind != RemoteError::Kind::None && e.kind != RemoteError::Kind::Malformed && e.detail.isEmpty())
        e.detail = u"HTTP %1"_s.arg(response.status);
    return e;
}

namespace {

sync::HttpRequest apiRequest(const QByteArray &method, const QUrl &base, const QString &endpoint, const QString &token,
                             const QJsonObject *body)
{
    sync::HttpRequest r;
    r.method = method;
    r.url = remoteUrl(base, endpoint);
    r.headers.append({"Accept"_ba, "application/json"_ba});
    if (!token.isEmpty())
        r.headers.append({"Authorization"_ba, "Bearer "_ba + token.toUtf8()});
    if (body) {
        r.headers.append({"Content-Type"_ba, "application/json"_ba});
        r.body = jsonBody(*body);
    }
    r.timeoutMs = RequestTimeoutMs;
    r.maxBodyBytes = MaxAnswerBytes;
    return r;
}

} // namespace

void pairRemote(sync::HttpTransport *transport, const QUrl &base, const QString &code, const QString &device,
                std::function<void(const RemotePairing &, const RemoteError &)> done)
{
    const QJsonObject body{{u"code"_s, normalizePairingCode(code)}, {u"device"_s, device}};
    transport->send(apiRequest("POST", base, u"pair"_s, QString(), &body), [done](const sync::HttpResponse &response) {
        QJsonObject o;
        RemoteError e = remoteErrorOf(response, &o);
        RemotePairing p;
        if (!e.failed()) {
            p.token = o.value(u"token"_s).toString();
            const QJsonObject d = o.value(u"device"_s).toObject();
            p.deviceId = d.value(u"id"_s).toString();
            p.deviceName = d.value(u"name"_s).toString();
            if (p.token.isEmpty())
                e = RemoteError{RemoteError::Kind::Malformed, response.status, QString(), u"no token in the answer"_s};
        }
        done(p, e);
    });
}

void fetchRemoteInfo(sync::HttpTransport *transport, const QUrl &base, const QString &token,
                     std::function<void(const RemoteInfo &, const RemoteError &)> done)
{
    transport->send(apiRequest("GET", base, u"info"_s, token, nullptr), [done](const sync::HttpResponse &response) {
        QJsonObject o;
        RemoteError e = remoteErrorOf(response, &o);
        RemoteInfo info;
        if (!e.failed()) {
            if (std::optional<RemoteInfo> read = RemoteInfo::fromJson(o)) {
                info = *read;
                if (info.protocol != RemoteProtocol)
                    e = RemoteError{RemoteError::Kind::Version, response.status, QString(),
                                    QString::number(info.protocol)};
            } else {
                e = RemoteError{RemoteError::Kind::Malformed, response.status, QString(),
                                u"no protocol in the answer"_s};
            }
        }
        done(info, e);
    });
}

// ---- RemoteStream ---------------------------------------------------------------------------------------

RemoteStream::RemoteStream(sync::HttpTransport *transport, const QUrl &base, const QString &token, const QString &agent,
                           QObject *parent)
    : QObject(parent), m_transport(transport), m_base(base), m_token(token), m_agent(agent)
{
    static const int registered = qRegisterMetaType<brulion::acp::RemoteStream::End>();
    Q_UNUSED(registered)
}

RemoteStream::~RemoteStream()
{
    *m_alive = false;
}

qint64 RemoteStream::now() const
{
    if (m_clock)
        return m_clock();
    static QElapsedTimer timer = [] {
        QElapsedTimer t;
        t.start();
        return t;
    }();
    return timer.elapsed();
}

sync::HttpRequest RemoteStream::request(const QByteArray &method, const QString &endpoint,
                                        const QJsonObject *body) const
{
    return apiRequest(method, m_base, endpoint, m_token, body);
}

int RemoteStream::retryDelay(int failures) const
{
    if (m_retryDelays.isEmpty())
        return 1000;
    return m_retryDelays.at(std::clamp(failures - 1, 0, int(m_retryDelays.size()) - 1));
}

void RemoteStream::start()
{
    if (m_ended || m_opening || isStarted())
        return;
    open();
}

void RemoteStream::open()
{
    m_opening = true;
    const QJsonObject body{{u"agent"_s, m_agent}};
    const std::weak_ptr<bool> alive = m_alive;
    m_transport->send(request("POST", u"streams"_s, &body), [this, alive](const sync::HttpResponse &response) {
        if (alive.expired() || m_ended)
            return;
        QJsonObject o;
        const RemoteError e = remoteErrorOf(response, &o);
        if (e.failed()) {
            if (e.kind == RemoteError::Kind::Refused) {
                m_opening = false;
                end(End::Refused, e.detail, e.code);
                return;
            }
            bool retrying = false;
            failedTry(e, [this] { open(); }, &retrying);
            if (!retrying)
                m_opening = false;
            return;
        }
        m_opening = false;
        m_failures = 0;
        m_failingSince = -1;
        m_id = o.value(u"stream"_s).toString();
        m_root = o.value(u"root"_s).toString();
        if (m_id.isEmpty()) {
            end(End::Refused, u"no stream in the answer"_s);
            return;
        }
        Q_EMIT started(m_root);
        if (alive.expired() || m_ended)
            return;
        flush();
        poll();
    });
}

void RemoteStream::write(const QByteArray &line)
{
    if (m_ended)
        return;
    const QByteArray message = trimmedLine(line);
    if (message.isEmpty())
        return;
    m_queue.append(message);
    if (!isStarted() || m_flushDue)
        return;
    // (The lines the client writes in one go — a turn's requests — go in one batch.)
    m_flushDue = true;
    QTimer::singleShot(0, this, [this] {
        m_flushDue = false;
        flush();
    });
}

void RemoteStream::flush()
{
    if (m_ended || m_sending || m_queue.isEmpty() || !isStarted())
        return;
    QJsonArray messages;
    const qsizetype count = std::min<qsizetype>(m_queue.size(), RemoteMaxBatch);
    for (qsizetype i = 0; i < count; ++i) {
        const QJsonDocument doc = QJsonDocument::fromJson(m_queue.at(i));
        // (A line that is no JSON object goes as text: the host drops it and says so in its log.)
        messages.append(doc.isObject() ? QJsonValue(doc.object()) : QJsonValue(QString::fromUtf8(m_queue.at(i))));
    }
    const QJsonObject body{{u"from"_s, m_sent}, {u"messages"_s, messages}};
    m_sending = true;
    const std::weak_ptr<bool> alive = m_alive;
    m_transport->send(request("POST", u"streams/%1/send"_s.arg(m_id), &body),
                      [this, alive](const sync::HttpResponse &response) {
                          if (alive.expired() || m_ended)
                              return;
                          QJsonObject o;
                          const RemoteError e = remoteErrorOf(response, &o);
                          if (e.failed()) {
                              bool retrying = false;
                              failedTry(
                                      e,
                                      [this] {
                                          m_sending = false;
                                          flush();
                                      },
                                      &retrying);
                              if (!retrying)
                                  m_sending = false;
                              return;
                          }
                          m_sending = false;
                          m_failures = 0;
                          m_failingSince = -1;
                          // The host says how many it has of all sent: those are dropped here.
                          const qint64 received = o.value(u"received"_s).toInteger(m_sent);
                          const qint64 taken = std::clamp<qint64>(received - m_sent, 0, m_queue.size());
                          m_queue.remove(0, taken);
                          m_sent += taken;
                          flush();
                      });
}

void RemoteStream::poll()
{
    if (m_ended || m_polling || !isStarted())
        return;
    m_polling = true;
    sync::HttpRequest r =
            request("GET", u"streams/%1/receive?after=%2&wait=%3"_s.arg(m_id).arg(m_after).arg(m_waitSeconds), nullptr);
    r.timeoutMs = RequestTimeoutMs + m_waitSeconds * 1000;
    const std::weak_ptr<bool> alive = m_alive;
    m_transport->send(r, [this, alive](const sync::HttpResponse &response) {
        if (alive.expired() || m_ended)
            return;
        QJsonObject o;
        const RemoteError e = remoteErrorOf(response, &o);
        if (e.failed()) {
            bool retrying = false;
            failedTry(
                    e,
                    [this] {
                        m_polling = false;
                        poll();
                    },
                    &retrying);
            if (!retrying)
                m_polling = false;
            return;
        }
        m_polling = false;
        m_failures = 0;
        m_failingSince = -1;
        QByteArray bytes;
        for (const QJsonValue &v : o.value(u"messages"_s).toArray()) {
            const QJsonObject entry = v.toObject();
            const qint64 seq = entry.value(u"seq"_s).toInteger(-1);
            if (seq <= m_after)
                continue; // (already heard: an answer that came twice)
            m_after = seq;
            const QJsonValue message = entry.value(u"message"_s);
            if (message.isObject())
                bytes += QJsonDocument(message.toObject()).toJson(QJsonDocument::Compact) + '\n';
        }
        if (!bytes.isEmpty()) {
            Q_EMIT received(bytes);
            if (alive.expired() || m_ended)
                return;
        }
        if (o.value(u"closed"_s).toBool()) {
            end(End::AgentEnded, o.value(u"why"_s).toString());
            return;
        }
        poll();
    });
}

void RemoteStream::failedTry(const RemoteError &error, const std::function<void()> &again, bool *retrying)
{
    *retrying = false;
    switch (error.kind) {
    case RemoteError::Kind::Unauthorized: end(End::Unauthorized, error.detail, error.code); return;
    case RemoteError::Kind::Version: end(End::Version, error.detail, error.code); return;
    case RemoteError::Kind::Refused:
        if (error.status == 404 || error.status == 410) {
            end(End::Gone, error.detail, error.code);
            return;
        }
        // (Another refusal of a request that was fine before — 409, 429 —: tried again.)
        break;
    default: break;
    }
    if (m_failingSince < 0)
        m_failingSince = now();
    if (now() - m_failingSince >= m_unreachableMs) {
        end(End::Unreachable, error.detail, error.code);
        return;
    }
    *retrying = true;
    QTimer::singleShot(retryDelay(++m_failures), this, [this, again] {
        if (!m_ended)
            again();
    });
}

void RemoteStream::stop()
{
    if (m_ended)
        return;
    m_ended = true;
    m_queue.clear();
    if (!m_id.isEmpty())
        m_transport->send(request("DELETE", u"streams/%1"_s.arg(m_id), nullptr), [](const sync::HttpResponse &) { });
}

void RemoteStream::end(End why, const QString &detail, const QString &code)
{
    if (m_ended)
        return;
    m_ended = true;
    m_queue.clear();
    Q_EMIT ended(why, detail, code);
}

} // namespace brulion::acp
