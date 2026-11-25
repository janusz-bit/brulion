// `brulion-cli app hooks serve` (docs/CLI.md "Webhooks"): the webhooks of the running app's
// scheduled conversations (docs/UI.md "Scheduled conversations" → "Webhooks") over HTTP, on the
// command line's hardened server (http.h). The desktop app starts it itself while Settings → Scheduled
// conversations → Webhooks is on (shell/hookserver.h, a supervised ServerProcess) and gives it a
// grant of its own (BRULION_APP_HOOK_TOKEN: the remote control's GrantScope::Hook — hello and chatHook,
// nothing else); each request goes on to the app over its remote control socket, which checks the
// hook's token among the open notebook's schedules (constant time), the rate limits and the
// conversation, and starts the run.
//
//   POST <address>/…/hooks/<token>   (a body of JSON, text or a form: UTF-8, at most 1 MiB)
//   202 {"conversation": "<name>"}   started (a new conversation, or the next message of one)
//   404 {"error": "not_found"}       no such hook — also one that is off, or of another notebook
//   429 {"error": "too_many_requests"} + Retry-After   one run a minute, its conversation still answers
//   400 / 413 / 405 / 503            a body not UTF-8 or of another charset, too large, not POST, no app
//
// Why a process of its own, not a server inside the app: the parser of the network's requests is
// the one `mcp serve` and `acp serve` use (no second HTTP server to keep hardened), it never runs in
// the process that holds the notes on screen, and it ends with the app (`--stop-on-eof`). Its log
// (standard error, English) has one line per request — never a token: every one after "/hooks/" is
// masked (webhook::maskPaths).
#include "cli_p.h"

#include "controlclient.h"
#include "controlprotocol.h"
#include "http.h"
#include "singleinstance.h"
#include "webhook/webhook.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QElapsedTimer>
#include <QHostAddress>
#include <QJsonArray>
#include <QJsonDocument>
#include <QUrl>

#include <cstdio>
#include <memory>
#include <optional>
#include <thread>

using namespace Qt::StringLiterals;

namespace brulion::cli {

namespace {

/// The variable that carries the app's hook grant.
constexpr auto HookTokenVariable = "BRULION_APP_HOOK_TOKEN";
/// The port by default.
constexpr quint16 DefaultPort = 8791;
/// Requests to hooks one address may send in a minute (behind a proxy: all of them).
constexpr int MaxRequestsPerMinute = 60;

void logLine(const QString &line)
{
    const QByteArray bytes =
            (QDateTime::currentDateTime().toString(u"yyyy-MM-dd HH:mm:ss"_s) + u' ' + webhook::maskPaths(line) + u'\n')
                    .toUtf8();
    std::fwrite(bytes.constData(), 1, size_t(bytes.size()), stderr);
    std::fflush(stderr);
}

http::Response answer(int status, const QJsonObject &body)
{
    http::Response r = http::Response::json(status, body);
    r.headers.set("Cache-Control"_ba, "no-store"_ba);
    r.headers.set("X-Content-Type-Options"_ba, "nosniff"_ba);
    return r;
}

http::Response refusal(int status, const QString &code)
{
    return answer(status, {{u"error"_s, code}});
}

http::Response retryLater(int status, const QString &code, qint64 ms)
{
    http::Response r = refusal(status, code);
    r.headers.set("Retry-After"_ba, QByteArray::number(std::max<qint64>(1, (ms + 999) / 1000)));
    return r;
}

class HookRelay {
public:
    HookRelay(const QString &key, const QString &token) : m_key(key), m_token(token)
    {
        http::ServerLimits limits;
        limits.maxConnections = 32;
        limits.maxConnectionsPerPeer = 8;
        limits.bodyTimeoutMs = 30000;
        m_http = std::make_unique<http::Server>(limits);
        m_http->onLog = logLine;
        m_http->onHeaders = [this](const http::Request &head, qint64 *maxBody) { return onHeaders(head, maxBody); };
        m_http->onRequest = [this](const http::Request &request, const http::Reply &reply) {
            onRequest(request, reply);
        };
    }

    bool listen(const QHostAddress &address, quint16 port, QString *error)
    {
        return m_http->listen(address, port, error);
    }
    quint16 port() const { return m_http->port(); }

    /// The app's answer to `method` with the hook grant ({} and `*why` when it did not answer).
    std::optional<QJsonObject> askApp(const QString &method, const QJsonObject &params, QString *why) const
    {
        ipc::ControlClient client(m_key);
        client.setIdentity(m_token, u"webhooks"_s);
        if (!client.connect(2000)) {
            *why = client.errorString().isEmpty() ? u"the app does not run"_s : client.errorString();
            return std::nullopt;
        }
        const ipc::ControlClient::Response r = client.call(method, params, 10000);
        if (!r.ok) {
            *why = r.code + u": "_s + r.message;
            return std::nullopt;
        }
        return r.result.toObject();
    }

private:
    std::optional<http::Response> onHeaders(const http::Request &head, qint64 *maxBody)
    {
        *maxBody = 0;
        const QString token = webhook::tokenOfPath(head.path);
        if (token.isEmpty()) {
            // (The root says what answers here — nothing of the notebook —, for "is it reachable?".)
            if ((head.path == "/" || head.path.isEmpty()) && (head.method == "GET" || head.method == "HEAD"))
                return http::Response::text(
                        200, "Brulion: webhooks of scheduled conversations (POST <address>/hooks/<token>)\n"_ba);
            return refusal(404, u"not_found"_s);
        }
        if (head.method != "POST") {
            // Never GET: a link preview or a browser's prefetch would start a run.
            http::Response r = refusal(405, u"method_not_allowed"_s);
            r.headers.set("Allow"_ba, "POST"_ba);
            return r;
        }
        if (!m_perPeer.allow(head.peer.toString(), m_clock.elapsed()))
            return retryLater(429, u"too_many_requests"_s, 60000);
        // A larger body ends in 413 before it is kept.
        *maxBody = webhook::MaxBodyBytes;
        return std::nullopt;
    }

    void onRequest(const http::Request &request, const http::Reply &reply)
    {
        webhook::BodyError problem = webhook::BodyError::None;
        const std::optional<webhook::Body> body =
                webhook::readBody(request.body, request.headers.value("Content-Type"), &problem);
        if (!body) {
            using E = webhook::BodyError;
            const QString code = problem == E::TooLarge  ? u"too_large"_s
                                 : problem == E::Charset ? u"charset_not_utf8"_s
                                 : problem == E::Binary  ? u"not_text"_s
                                                         : u"not_utf8"_s;
            return reply.send(refusal(problem == E::TooLarge ? 413 : 400, code));
        }
        QString why;
        const std::optional<QJsonObject> result = askApp(u"chatHook"_s,
                                                         {{u"token"_s, webhook::tokenOfPath(request.path)},
                                                          {u"text"_s, body->text},
                                                          {u"kind"_s, webhook::kindName(body->kind)},
                                                          {u"bytes"_s, double(body->bytes)},
                                                          {u"cut"_s, body->cut}},
                                                         &why);
        if (!result) {
            logLine(u"the app did not take it: %1"_s.arg(why));
            return reply.send(retryLater(503, u"unavailable"_s, 60000));
        }
        const QString status = result->value(u"status"_s).toString();
        if (status == u"started")
            return reply.send(answer(202, {{u"conversation"_s, result->value(u"conversation"_s).toString()}}));
        if (status == u"busy")
            return reply.send(
                    retryLater(429, u"too_many_requests"_s, qint64(result->value(u"retryAfterMs"_s).toDouble(60000))));
        if (status == u"unavailable")
            return reply.send(retryLater(503, u"unavailable"_s, 60000));
        return reply.send(refusal(404, u"not_found"_s));
    }

    QString m_key;
    QString m_token;
    std::unique_ptr<http::Server> m_http;
    webhook::Window m_perPeer{MaxRequestsPerMinute, 60000};
    QElapsedTimer m_clock = [] {
        QElapsedTimer t;
        t.start();
        return t;
    }();
};

} // namespace

int serveAppHooks(Context &context, const Command &command, const QJsonObject &args,
                  const std::function<int(const Command *, const Result &)> &output)
{
    const auto fail = [&](int exit, const QString &code, const QString &message, const QJsonObject &details = {}) {
        Result r;
        r.error = Error{exit, code, message, details};
        return output(&command, r);
    };
    const QString token = qEnvironmentVariable(HookTokenVariable).trimmed();
    if (token.isEmpty())
        return fail(ExitUnavailable, u"no_grant"_s,
                    QCoreApplication::translate(
                            "brulion::cli", "The app starts this server itself, with a grant of its own: Settings → "
                                            "Extensions → Scheduled conversations → Webhooks"));
    const QString listen = args.value(u"listen"_s).toString(u"127.0.0.1"_s).trimmed();
    const QHostAddress address(listen == u"localhost" ? u"127.0.0.1"_s : listen);
    if (address.isNull())
        return fail(ExitUsage, u"bad_listen"_s,
                    QCoreApplication::translate("brulion::cli", "--listen is an IP address of this computer"));
    const QJsonValue portValue = args.value(u"port"_s);
    const int port = portValue.isUndefined()
                             ? DefaultPort
                             : (portValue.isDouble() ? portValue.toInt() : portValue.toString().toInt());
    if (port < 0 || port > 65535)
        return fail(ExitUsage, u"bad_port"_s, QCoreApplication::translate("brulion::cli", "--port is 0 … 65535"));
    const QString publicUrl = args.value(u"publicUrl"_s).toString().trimmed();
    if (!publicUrl.isEmpty()) {
        const QUrl url(publicUrl, QUrl::StrictMode);
        if (!url.isValid() || (url.scheme() != u"https" && url.scheme() != u"http") || url.host().isEmpty())
            return fail(ExitUsage, u"bad_public_url"_s,
                        QCoreApplication::translate("brulion::cli", "--public-url is an http or https address"));
    }
    // The app first: it must run and take the grant.
    const QString key = ipc::controlKeyFor(ipc::SingleInstance::keyFor(context.appDataDir()));
    HookRelay server(key, token);
    QString why;
    if (!server.askApp(u"hello"_s, {}, &why))
        return fail(ExitUnavailable, u"app_not_reached"_s,
                    QCoreApplication::translate("brulion::cli", "The running app did not take the grant: %1").arg(why));
    QString listenError;
    if (!server.listen(address, quint16(port), &listenError))
        return fail(ExitUnavailable, u"cannot_serve"_s, listenError);
    QString host = address.toString();
    if (address.protocol() == QAbstractSocket::IPv6Protocol)
        host = u"["_s + host + u']';
    const QString local = u"http://"_s + host + u':' + QString::number(server.port());
    QString url = publicUrl.isEmpty() ? local : publicUrl;
    while (url.endsWith(u'/'))
        url.chop(1);
    const QJsonObject started{{u"ok"_s, true},
                              {u"command"_s, command.id},
                              {u"result"_s, QJsonObject{{u"url"_s, url},
                                                        {u"local"_s, local},
                                                        {u"address"_s, address.toString()},
                                                        {u"port"_s, server.port()}}},
                              {u"warnings"_s, QJsonArray()}};
    const QByteArray line = QJsonDocument(started).toJson(QJsonDocument::Compact) + '\n';
    std::fwrite(line.constData(), 1, size_t(line.size()), stdout);
    std::fflush(stdout);
    logLine(u"webhooks at %1/hooks/… (listening on %2 port %3)"_s.arg(url, address.toString(),
                                                                      QString::number(server.port())));
    if (args.value(u"stopOnEof"_s).toBool()) {
        // (The app closes its standard input to stop it, and so does its own end, a crash too.)
        QCoreApplication *app = QCoreApplication::instance();
        std::thread([app] {
            char buffer[256];
            while (std::fread(buffer, 1, sizeof buffer, stdin) > 0) { }
            QMetaObject::invokeMethod(app, [] { QCoreApplication::quit(); }, Qt::QueuedConnection);
        }).detach();
    }
    return QCoreApplication::exec();
}

} // namespace brulion::cli
