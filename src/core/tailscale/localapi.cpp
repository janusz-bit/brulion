#include "tailscale/localapi.h"

#include <QFileInfo>
#include <QStandardPaths>
#include <QSysInfo>
#include <QTimer>
#include <QUrlQuery>

#if QT_CONFIG(localserver)
#include <QLocalSocket>
#endif
#if QT_CONFIG(process)
#include <QProcess>
#endif

using namespace Qt::StringLiterals;

namespace brulion::tailscale {

namespace {

/// The Host tailscaled's LocalAPI answers to (any other: 403 "invalid localapi request").
constexpr auto kLocalApiHost = "local-tailscaled.sock";

QString envPath(const char *name)
{
    return qEnvironmentVariable(name).trimmed();
}

QString peerText(const QHostAddress &address, quint16 port)
{
    bool v4 = false;
    const quint32 ipv4 = address.toIPv4Address(&v4);
    if (v4)
        return QHostAddress(ipv4).toString() + u':' + QString::number(port);
    return u'[' + address.toString() + u"]:"_s + QString::number(port);
}

} // namespace

LocalApi::Answer parseHttpAnswer(const QByteArray &bytes)
{
    LocalApi::Answer a;
    const qsizetype headEnd = bytes.indexOf("\r\n\r\n");
    if (!bytes.startsWith("HTTP/1.") || headEnd < 0) {
        a.error = bytes.isEmpty() ? u"tailscaled closed the connection without an answer"_s
                                  : u"tailscaled's answer is no HTTP"_s;
        return a;
    }
    const QList<QByteArray> lines = bytes.left(headEnd).split('\n');
    const QList<QByteArray> statusLine = lines.value(0).trimmed().split(' ');
    bool ok = false;
    const int status = statusLine.value(1).toInt(&ok);
    if (!ok || status < 100 || status > 599) {
        a.error = u"tailscaled's answer has no status"_s;
        return a;
    }
    qint64 length = -1;
    for (qsizetype i = 1; i < lines.size(); ++i) {
        const QByteArray line = lines.at(i).trimmed();
        const qsizetype colon = line.indexOf(':');
        if (colon > 0 && line.left(colon).trimmed().compare("Content-Length", Qt::CaseInsensitive) == 0)
            length = line.mid(colon + 1).trimmed().toLongLong(&ok);
        if (colon > 0 && line.left(colon).trimmed().compare("Transfer-Encoding", Qt::CaseInsensitive) == 0) {
            // (HTTP/1.0 is asked for: tailscaled never chunks its answer then.)
            a.error = u"tailscaled's answer is chunked"_s;
            return a;
        }
    }
    a.status = status;
    a.body = bytes.mid(headEnd + 4);
    if (length >= 0 && length < a.body.size())
        a.body.truncate(length);
    if (length >= 0 && a.body.size() < length) {
        a.status = 0;
        a.error = u"tailscaled's answer was cut off"_s;
    }
    return a;
}

LocalApi::LocalApi(QObject *parent)
    : QObject(parent), m_socket(defaultSocketPath()), m_program(defaultProgram())
{ }

LocalApi::~LocalApi()
{
    *m_alive = false;
}

QString LocalApi::defaultSocketPath()
{
    if (const QString given = envPath("BRULION_TAILSCALE_SOCKET"); !given.isEmpty())
        return given;
    if (QSysInfo::kernelType() == u"winnt")
        return u"\\\\.\\pipe\\ProtectedPrefix\\Administrators\\Tailscale\\tailscaled"_s;
    for (const QString &path : {u"/run/tailscale/tailscaled.sock"_s, u"/var/run/tailscale/tailscaled.sock"_s,
                                u"/var/run/tailscaled.socket"_s}) {
        if (QFileInfo::exists(path))
            return path;
    }
    return {};
}

QString LocalApi::defaultProgram()
{
    if (qEnvironmentVariableIsSet("BRULION_TAILSCALE_PROGRAM"))
        return envPath("BRULION_TAILSCALE_PROGRAM");
    if (const QString onPath = QStandardPaths::findExecutable(u"tailscale"_s); !onPath.isEmpty())
        return onPath;
    const QString mac = u"/Applications/Tailscale.app/Contents/MacOS/Tailscale"_s;
    if (QFileInfo(mac).isExecutable())
        return mac;
    return {};
}

void LocalApi::get(const QString &pathAndQuery, Done done)
{
    viaSocket(pathAndQuery, std::move(done));
}

void LocalApi::viaSocket(const QString &pathAndQuery, Done done)
{
#if QT_CONFIG(localserver)
    if (m_socket.isEmpty()) {
        done(Answer{0, {}, u"tailscaled's socket is not there (is Tailscale installed and running?)"_s});
        return;
    }
    auto *socket = new QLocalSocket(this);
    auto *timer = new QTimer(socket);
    timer->setSingleShot(true);
    auto buffer = std::make_shared<QByteArray>();
    auto finished = std::make_shared<bool>(false);
    auto shared = std::make_shared<Done>(std::move(done));
    const std::weak_ptr<bool> alive = m_alive;
    const auto finish = [socket, finished, shared, alive](const Answer &answer) {
        if (*finished)
            return;
        *finished = true;
        socket->disconnect();
        socket->abort();
        socket->deleteLater();
        if (alive.lock() && *alive.lock())
            (*shared)(answer);
    };
    connect(timer, &QTimer::timeout, socket, [finish] {
        finish(Answer{0, {}, u"tailscaled did not answer in time"_s});
    });
    connect(socket, &QLocalSocket::connected, socket, [socket, pathAndQuery] {
        const QByteArray request = "GET " + pathAndQuery.toUtf8() + " HTTP/1.0\r\nHost: " + kLocalApiHost
                                   + "\r\nAccept: application/json\r\nUser-Agent: brulion\r\n\r\n";
        socket->write(request);
    });
    connect(socket, &QLocalSocket::readyRead, socket, [socket, buffer, finish] {
        *buffer += socket->readAll();
        if (buffer->size() > MaxAnswerBytes)
            finish(Answer{0, {}, u"tailscaled's answer is too large"_s});
    });
    connect(socket, &QLocalSocket::disconnected, socket, [socket, buffer, finish] {
        *buffer += socket->readAll();
        finish(parseHttpAnswer(*buffer));
    });
    connect(socket, &QLocalSocket::errorOccurred, socket, [socket, buffer, finish](QLocalSocket::LocalSocketError e) {
        if (e == QLocalSocket::PeerClosedError) {
            *buffer += socket->readAll();
            finish(parseHttpAnswer(*buffer));
            return;
        }
        finish(Answer{0, {}, u"tailscaled not reached: %1"_s.arg(socket->errorString())});
    });
    timer->start(m_timeoutMs);
    socket->connectToServer(m_socket);
#else
    Q_UNUSED(pathAndQuery)
    done(Answer{0, {}, u"no local sockets in this build"_s});
#endif
}

void LocalApi::viaProgram(const QStringList &arguments, Done done)
{
#if QT_CONFIG(process)
    if (m_program.isEmpty()) {
        done(Answer{0, {}, QString()});
        return;
    }
    auto *process = new QProcess(this);
    auto *timer = new QTimer(process);
    timer->setSingleShot(true);
    auto finished = std::make_shared<bool>(false);
    auto shared = std::make_shared<Done>(std::move(done));
    const std::weak_ptr<bool> alive = m_alive;
    const auto finish = [process, finished, shared, alive](const Answer &answer) {
        if (*finished)
            return;
        *finished = true;
        process->disconnect();
        if (process->state() != QProcess::NotRunning)
            process->kill();
        process->deleteLater();
        if (alive.lock() && *alive.lock())
            (*shared)(answer);
    };
    connect(timer, &QTimer::timeout, process, [finish] {
        finish(Answer{0, {}, u"tailscale did not answer in time"_s});
    });
    connect(process, &QProcess::finished, process, [process, finish](int code, QProcess::ExitStatus exit) {
        const QByteArray out = process->readAllStandardOutput().left(MaxAnswerBytes);
        const QString err = QString::fromUtf8(process->readAllStandardError()).trimmed().left(300);
        if (exit == QProcess::NormalExit && code == 0) {
            finish(Answer{200, out, QString()});
            return;
        }
        // (`tailscale whois` of an address it does not know: "no match for IP:port".)
        if (err.contains(u"no match"_s, Qt::CaseInsensitive)) {
            finish(Answer{404, err.toUtf8(), QString()});
            return;
        }
        finish(Answer{0, {}, err.isEmpty() ? u"tailscale exited with %1"_s.arg(code) : err});
    });
    connect(process, &QProcess::errorOccurred, process, [process, finish](QProcess::ProcessError e) {
        if (e == QProcess::FailedToStart)
            finish(Answer{0, {}, u"tailscale did not start: %1"_s.arg(process->errorString())});
    });
    timer->start(m_timeoutMs);
    process->start(m_program, arguments, QIODevice::ReadOnly);
#else
    Q_UNUSED(arguments)
    done(Answer{0, {}, QString()});
#endif
}

void LocalApi::status(std::function<void(const std::optional<Status> &, const QString &)> done)
{
    auto shared = std::make_shared<decltype(done)>(std::move(done));
    const auto read = [shared](const Answer &a) {
        if (a.status != 200) {
            (*shared)(std::nullopt, a.status == 0 ? a.error : u"tailscaled answered %1"_s.arg(a.status));
            return;
        }
        QString error;
        std::optional<Status> s = parseStatus(a.body, &error);
        (*shared)(s, error);
    };
    viaSocket(u"/localapi/v0/status"_s, [this, read](const Answer &a) {
        if (a.status == 0 && !m_program.isEmpty()) {
            // (Not reached: the program, which finds its daemon however this platform does it.)
            const QString socketError = a.error;
            viaProgram({u"status"_s, u"--json"_s}, [read, socketError](const Answer &b) {
                read(b.status == 0 && b.error.isEmpty() ? Answer{0, {}, socketError} : b);
            });
            return;
        }
        read(a);
    });
}

void LocalApi::whois(const QHostAddress &address, quint16 port,
                     std::function<void(const std::optional<Identity> &, const QString &)> done)
{
    auto shared = std::make_shared<decltype(done)>(std::move(done));
    const QString addr = peerText(address, port);
    const auto read = [shared](const Answer &a) {
        if (a.status == 404) {
            (*shared)(std::nullopt, QString());
            return;
        }
        if (a.status != 200) {
            (*shared)(std::nullopt, a.status == 0 ? a.error : u"tailscaled answered %1"_s.arg(a.status));
            return;
        }
        QString error;
        std::optional<Identity> who = parseWhois(a.body, &error);
        (*shared)(who, error);
    };
    QUrlQuery query;
    query.addQueryItem(u"addr"_s, addr);
    viaSocket(u"/localapi/v0/whois?"_s + query.toString(QUrl::FullyEncoded), [this, read, addr](const Answer &a) {
        if (a.status == 0 && !m_program.isEmpty()) {
            const QString socketError = a.error;
            viaProgram({u"whois"_s, u"--json"_s, addr}, [read, socketError](const Answer &b) {
                read(b.status == 0 && b.error.isEmpty() ? Answer{0, {}, socketError} : b);
            });
            return;
        }
        read(a);
    });
}

} // namespace brulion::tailscale
