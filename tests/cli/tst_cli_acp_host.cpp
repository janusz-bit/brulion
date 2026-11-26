// `brulion-cli acp serve` in process over a real local socket (src/cli/acphost.h), with the app's side
// as it talks to it — core acp/remote.h's RemoteStream under acp::Client — and an agent that speaks
// the protocol as a program of its own (tests/app/shell/fakeacpagent.cpp): what is rewritten on the
// way (the device's cwd and MCP servers replaced, files and terminals offered), a conversation
// answered, the agent's questions relayed and answered, files of the notebook read and written here
// (with the version history; outside the notebook refused), a command run in a terminal here, the
// agent's end, pairing (a wrong code, a used one, too many tries), tokens refused, unpairing a device
// that has an agent running, an idle stream given up, sends that come twice, Host and Origin
// refused, and the notebook synced with Nextcloud (an in-memory server) before a turn and after it.
// The turns kept for a device that lost its stream: their answers, and the agent's questions that
// wait for the device there (answered from the turn, asked after the device was gone, answered
// "cancelled" once nobody fetched them for --question-wait, or at once with 0).
// The app's live tools for the agent (acp/apptunnel.h): a device that answers them gets them in a
// session that asks — the real brulion-cli mcp started by the agent, its brulion_app_* tools
// (state, a screenshot written here as a PNG, the chat's partners, a refusal) answered by the
// device over its stream, the session's grants only in the server's environment, the request's
// `_meta` never reaching the agent —; the tunnel's socket itself: a chat grant's methods only,
// tokens that are no grant, quit refused, a call the device does not answer failing in time, the
// calls in flight bounded, a late answer dropped, malformed answers and pictures, a path that is
// none, the grants ended with the stream; an older device (no feature: plain tools) and a server
// without the tunnel (--no-app-tools).
// Code mode's threads (--projects, acp/codehost.h) with real git in temporary repositories: the
// projects to a paired device only (the file read again, a broken one none), a thread's session in
// its project (cwd, no notebook tools, the request's project never reaching the agent), files and
// terminals there only (outside, .git and another folder refused), a checkpoint before each turn told
// to the device and kept with the turn, the changes since one and a file's diff (paths that are none
// and links out refused), worktrees made for new threads (a free name, a base, refusals), sessions in a
// worktree under the root or one git lists, refused in anything else, a session resumed in a new
// stream, and a server without projects refusing threads.
#include "acphost.h"
#include "cli_p.h"
#include "hostsync.h"

#include "acp/acp.h"
#include "acp/remote.h"
#include "automation/uri.h"
#include "fakewebdavserver.h"
#include "notebook/session.h"
#include "qr/qrcode.h"
#include "sync/http.h"

#include "controlprotocol.h"

#include "acp/apptunnel.h"
#include "acp/codehost.h"

#include <QDir>
#include <QDirIterator>
#include <QElapsedTimer>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLocalSocket>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QProcess>
#include <QSignalSpy>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTest>

#include <memory>

using namespace Qt::StringLiterals;
using namespace brulion;
using namespace brulion::cli;

namespace {

constexpr int kTimeoutMs = 30000 * BRULION_TEST_SLOWDOWN;

struct HttpResult {
    int status = 0;
    QByteArray body;
    QJsonObject json;
    QHash<QByteArray, QByteArray> headers;
};

QString fileText(const QString &path)
{
    QFile f(path);
    return f.open(QIODevice::ReadOnly) ? QString::fromUtf8(f.readAll()) : QString();
}

void writeFile(const QString &path, const QByteArray &bytes)
{
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile f(path);
    QVERIFY(f.open(QIODevice::WriteOnly));
    f.write(bytes);
}

/// The device's app as it answers the calls of the agent's tools (acp/apptunnel.h): what it heard,
/// how it answers (nullopt: not yet — held), or a raw answer of its own (true: answered so).
struct AppDevice {
    QList<acp::AppCall> calls;
    std::function<std::optional<acp::AppAnswer>(const acp::AppCall &call)> answer;
    std::function<bool(const acp::AppCall &call, acp::Client *client, const QJsonValue &id)> raw;
    QList<QJsonValue> held;
};

/// A client of the tunnel's socket, as the tools' MCP server is (asynchronous: the host runs here).
struct TunnelClient {
    QLocalSocket socket;
    QHash<int, QJsonObject> answers;
    int next = 1;
    bool connect(const QString &key)
    {
        socket.connectToServer(key);
        return socket.waitForConnected(5000);
    }
    int send(const QString &method, const QJsonObject &params = {})
    {
        const int id = next++;
        socket.write(brulion::ipc::encodeRequest(id, method, params) + '\n');
        socket.flush();
        return id;
    }
    /// The answer to `id` ({} none in time).
    QJsonObject answer(int id, int timeoutMs)
    {
        (void)QTest::qWaitFor(
                [&] {
                    while (socket.canReadLine()) {
                        const QJsonObject o = QJsonDocument::fromJson(socket.readLine()).object();
                        answers.insert(o.value(u"id"_s).toInt(-1), o);
                    }
                    return answers.contains(id) || socket.state() != QLocalSocket::ConnectedState;
                },
                timeoutMs);
        return answers.value(id);
    }
    static QString errorCode(const QJsonObject &answer)
    {
        return answer.value(u"error"_s).toObject().value(u"code"_s).toString();
    }
};

/// A 1×1 PNG, base64.
constexpr auto kTinyPng =
        "iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAYAAAAfFcSJAAAADUlEQVR42mNk+M9QDwADhgGAWjR9awAAAABJRU5ErkJggg==";

/// A conversation of the app with an agent on the host: the app's client over a stream.
struct Talk {
    std::unique_ptr<acp::RemoteStream> stream;
    std::unique_ptr<acp::Client> client;
    QString root;
    QString endedWhy;
    acp::RemoteStream::End end{};
    bool ended = false;
    QHash<QString, QString> answers;
    QHash<QString, QString> failures;
    QList<std::pair<QString, acp::Permission>> questions;
    /// The checkpoints the host told of (a thread's turns), with their conversations.
    QList<std::pair<QString, acp::RemoteCheckpoint>> checkpoints;
};

/// git in `folder` (its exit code; -1 not run).
int git(const QString &folder, const QStringList &args)
{
    return QProcess::execute(u"git"_s, QStringList{u"-C"_s, folder, u"-c"_s, u"user.name=Ala"_s, u"-c"_s,
                                                    u"user.email=ala@example.com"_s, u"-c"_s,
                                                    u"commit.gpgsign=false"_s} + args);
}

/// A git repository at `folder` with Dom.md committed.
bool gitRepo(const QString &folder, const QByteArray &readme = "# README żółwia\n")
{
    QDir().mkpath(folder);
    QFile f(folder + u"/Dom.md"_s);
    if (!f.open(QIODevice::WriteOnly))
        return false;
    f.write(readme);
    f.close();
    return git(folder, {u"init"_s, u"-q"_s, u"-b"_s, u"main"_s}) == 0 && git(folder, {u"add"_s, u"-A"_s}) == 0
           && git(folder, {u"commit"_s, u"-qm"_s, u"start"_s}) == 0;
}

QString canonical(const QString &path)
{
    return QFileInfo(path).canonicalFilePath();
}

} // namespace

class TestCliAcpHost : public QObject {
    Q_OBJECT
    QTemporaryDir m_tmp;
    QString m_vault;
    QString m_agentLog;
    std::unique_ptr<Context> m_ctx;
    std::unique_ptr<AcpHost> m_host;
    std::unique_ptr<sync::NetworkHttpTransport> m_transport;
    QNetworkAccessManager *m_nam = nullptr;
    QString m_base;
    QString m_stateFile;
    QStringList m_log;
    int m_hosts = 0;
    QDateTime m_now; ///< the server's clock where a test sets it

    AcpHostOptions options() const
    {
        AcpHostOptions o;
        o.port = 0;
        o.agents = {{u"fake"_s, u"Udawany agent"_s, QString::fromUtf8(FAKE_ACP_AGENT)},
                    {u"fake-config"_s, u"Z opcjami"_s, QString::fromUtf8(FAKE_ACP_AGENT) + u" --config"_s}};
        o.toolsProgram = u"/opt/brulion/bin/brulion-cli"_s;
        o.maintenanceIntervalMs = 100;
        return o;
    }

    void startHost(Context &context, AcpHostOptions o, HostSync *sync = nullptr)
    {
        m_host.reset();
        m_stateFile = m_tmp.filePath(u"host-%1/acp-remote.json"_s.arg(++m_hosts));
        o.stateFile = m_stateFile;
        std::optional<Error> error;
        notebook::Notebook *nb = context.notebook(&error);
        QVERIFY(nb);
        m_host = std::make_unique<AcpHost>(context, nb, o);
        m_host->log = [this](const QString &line) { m_log.append(line); };
        m_host->setSync(sync);
        QString startError;
        QVERIFY2(m_host->start(&startError), qPrintable(startError));
        m_base = m_host->publicUrl();
        delete m_nam;
        m_nam = new QNetworkAccessManager(this);
        m_transport = std::make_unique<sync::NetworkHttpTransport>();
    }

    HttpResult http(const QByteArray &method, const QString &endpoint, const QByteArray &body = {},
                    const QList<std::pair<QByteArray, QByteArray>> &headers = {})
    {
        QNetworkRequest request(QUrl(m_base + u"/acp/v1/"_s + endpoint));
        request.setAttribute(QNetworkRequest::CacheLoadControlAttribute, QNetworkRequest::AlwaysNetwork);
        if (!body.isEmpty())
            request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json"_ba);
        for (const auto &[name, value] : headers)
            request.setRawHeader(name, value);
        QNetworkReply *reply = m_nam->sendCustomRequest(request, method, body);
        HttpResult out;
        if (!QTest::qWaitFor([reply] { return reply->isFinished(); }, kTimeoutMs)) {
            reply->abort();
            reply->deleteLater();
            return out;
        }
        out.status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        out.body = reply->readAll();
        out.json = QJsonDocument::fromJson(out.body).object();
        for (const auto &[name, value] : reply->rawHeaderPairs())
            out.headers.insert(name.toLower(), value);
        reply->deleteLater();
        return out;
    }

    static QList<std::pair<QByteArray, QByteArray>> bearer(const QString &token)
    {
        return {{"Authorization"_ba, "Bearer "_ba + token.toUtf8()}};
    }

    Result run(const QString &command, QJsonObject args = {})
    {
        args.insert(u"stateFile"_s, m_stateFile);
        const Command *c = findCommand(command);
        if (!c)
            return {};
        return execute(*m_ctx, *c, args);
    }

    /// A pairing code from `acp pair`.
    QString pairingCode()
    {
        const Result r = run(u"acp.pair"_s);
        return r.value.toObject().value(u"code"_s).toString();
    }

    QString pair(const QString &device = u"Pixel 9 Pro Jana"_s) { return pairDevice(device).first; }

    /// A device paired: its token and id.
    std::pair<QString, QString> pairDevice(const QString &device = u"Pixel 9 Pro Jana"_s)
    {
        const QString code = pairingCode();
        const HttpResult r =
                http("POST", u"pair"_s, QJsonDocument(QJsonObject{{u"code"_s, code}, {u"device"_s, device}}).toJson());
        return {r.json.value(u"token"_s).toString(), r.json.value(u"device"_s).toObject().value(u"id"_s).toString()};
    }

    /// The turn of `session` as the app asks for it (acp::fetchRemoteTurn).
    std::pair<acp::RemoteTurn, acp::RemoteError> fetchTurn(const QString &token, const QString &session, int wait)
    {
        std::pair<acp::RemoteTurn, acp::RemoteError> out;
        bool done = false;
        acp::fetchRemoteTurn(m_transport.get(), QUrl(m_base), token, session, wait,
                             [&](const acp::RemoteTurn &t, const acp::RemoteError &e) {
                                 out = {t, e};
                                 done = true;
                             });
        if (!QTest::qWaitFor([&done] { return done; }, kTimeoutMs))
            out.second.kind = acp::RemoteError::Kind::Network;
        return out;
    }

    /// The device's answer to the question `question` of its turn (acp::answerRemoteQuestion).
    std::pair<acp::RemoteTurn, acp::RemoteError> answerQuestion(const QString &token, const QString &session,
                                                                const QString &question, const QString &option)
    {
        std::pair<acp::RemoteTurn, acp::RemoteError> out;
        bool done = false;
        acp::answerRemoteQuestion(m_transport.get(), QUrl(m_base), token, session, question, option,
                                  [&](const acp::RemoteTurn &t, const acp::RemoteError &e) {
                                      out = {t, e};
                                      done = true;
                                  });
        if (!QTest::qWaitFor([&done] { return done; }, kTimeoutMs))
            out.second.kind = acp::RemoteError::Kind::Network;
        return out;
    }

    /// A question asked in the turn `key` of `t` (the device had it), then the device gone without a
    /// word (an app Android ended): the session.
    QString askedAndGone(std::unique_ptr<Talk> &t, const QString &key, const QString &text)
    {
        if (!t->client->prompt(key, turn(text)))
            return {};
        if (!QTest::qWaitFor([&t] { return !t->questions.isEmpty(); }, kTimeoutMs))
            return {};
        const QString session = t->client->sessionId(key);
        t.reset();
        return session;
    }

    /// `app`: the device answers the calls of the agent's tools (its stream asks for them).
    std::unique_ptr<Talk> talk(const QString &token, const QString &agent = u"fake"_s, AppDevice *app = nullptr)
    {
        auto t = std::make_unique<Talk>();
        t->stream = std::make_unique<acp::RemoteStream>(m_transport.get(), QUrl(m_base), token, agent);
        t->stream->setRetryDelays({50});
        if (app)
            t->stream->setFeatures({QString(acp::AppFeature)});
        Talk *raw = t.get();
        acp::RemoteStream *stream = t->stream.get();
        t->client = std::make_unique<acp::Client>([stream](const QByteArray &line) { stream->write(line); }, u"test"_s);
        acp::Client *client = t->client.get();
        if (app) {
            client->setExtensionHandler([client, app](const QJsonValue &id, const QString &method,
                                                      const QJsonObject &params) {
                QString error;
                const std::optional<acp::AppCall> call = acp::AppCall::fromMessage(
                        QJsonObject{
                                {u"jsonrpc"_s, u"2.0"_s}, {u"id"_s, id}, {u"method"_s, method}, {u"params"_s, params}},
                        &error);
                if (!call) {
                    client->respondError(id, acp::InvalidParams, error);
                    return;
                }
                app->calls.append(*call);
                if (app->raw && app->raw(*call, client, id))
                    return;
                const std::optional<acp::AppAnswer> answer = app->answer ? app->answer(*call) : std::nullopt;
                if (!answer) {
                    app->held.append(id);
                    return;
                }
                client->respond(id, acp::appAnswerMessage(id, *answer).value(u"result"_s));
            });
        }
        connect(stream, &acp::RemoteStream::started, this, [raw](const QString &root) { raw->root = root; });
        connect(stream, &acp::RemoteStream::received, client, &acp::Client::received);
        connect(stream, &acp::RemoteStream::ended, this,
                [raw, client](acp::RemoteStream::End why, const QString &detail, const QString &) {
                    raw->ended = true;
                    raw->end = why;
                    raw->endedWhy = detail;
                    client->closed(detail);
                });
        connect(client, &acp::Client::finished, this,
                [raw](const QString &key, const QString &text, const QString &) { raw->answers.insert(key, text); });
        connect(client, &acp::Client::failed, this,
                [raw](const QString &key, const acp::Failure &failure) { raw->failures.insert(key, failure.detail); });
        connect(client, &acp::Client::permissionAsked, this,
                [raw](const QString &key, const acp::Permission &p) { raw->questions.append({key, p}); });
        connect(client, &acp::Client::extensionNotified, this,
                [raw](const QString &key, const QString &method, const QJsonObject &params) {
                    if (method == acp::CheckpointMethod)
                        if (const std::optional<acp::RemoteCheckpoint> c = acp::readCheckpoint(params))
                            raw->checkpoints.append({key, *c});
                });
        t->stream->start();
        return t;
    }

    /// Options of a host whose projects are in `file` (Code mode's threads).
    AcpHostOptions projectOptions(const QString &file) const
    {
        AcpHostOptions o = options();
        o.projectsFile = file;
        return o;
    }

    /// A turn of a thread: its session works in `code`'s project there.
    static acp::TurnInput codeTurn(const QString &text, const acp::CodeSession &code)
    {
        acp::TurnInput input = turn(text);
        input.sessionMeta = QJsonObject{{acp::CodeMetaKey, code.toJson()}};
        return input;
    }

    QString askCode(Talk &t, const QString &key, const QString &text, const acp::CodeSession &code)
    {
        t.answers.remove(key);
        t.failures.remove(key);
        if (!t.client->prompt(key, codeTurn(text, code)))
            return u"(not started)"_s;
        if (!QTest::qWaitFor([&] { return t.answers.contains(key) || t.failures.contains(key); }, kTimeoutMs))
            return u"(no answer)"_s;
        return t.answers.contains(key) ? t.answers.value(key) : u"(failed) "_s + t.failures.value(key);
    }

    /// A turn as the app sends it: the phone's own folder and no MCP server, which the host replaces.
    static acp::TurnInput turn(const QString &text)
    {
        acp::TurnInput input;
        input.cwd = u"/data/user/0/io.github.januszbit.brulion/files/Notatki"_s;
        input.firstPrompt = {acp::Block::textBlock(u"Kontekst rozmowy."_s), acp::Block::textBlock(text)};
        input.prompt = {acp::Block::textBlock(text)};
        return input;
    }

    /// A turn whose session asks for the app's tools (`app`: its handle and what it may do).
    static acp::TurnInput appTurn(const QString &text, const acp::AppSession &app)
    {
        acp::TurnInput input = turn(text);
        input.sessionMeta = QJsonObject{{acp::AppMetaKey, app.toJson()}};
        return input;
    }

    QString ask(Talk &t, const QString &key, const QString &text, const std::optional<acp::AppSession> &app = {})
    {
        t.answers.remove(key);
        t.failures.remove(key);
        if (!t.client->prompt(key, app ? appTurn(text, *app) : turn(text)))
            return u"(not started)"_s;
        if (!QTest::qWaitFor([&] { return t.answers.contains(key) || t.failures.contains(key); }, kTimeoutMs))
            return u"(no answer)"_s;
        return t.answers.contains(key) ? t.answers.value(key) : u"(failed) "_s + t.failures.value(key);
    }

    /// The environment of the MCP server of the agent's last session/new ({name → value}), its
    /// arguments.
    QHash<QString, QString> lastServerEnv() const
    {
        QHash<QString, QString> out;
        const QList<QJsonObject> sessions = agentRead(u"session/new"_s);
        if (sessions.isEmpty())
            return out;
        const QJsonObject server =
                sessions.last().value(u"params"_s).toObject().value(u"mcpServers"_s).toArray().at(0).toObject();
        for (const QJsonValue &e : server.value(u"env"_s).toArray())
            out.insert(e.toObject().value(u"name"_s).toString(), e.toObject().value(u"value"_s).toString());
        return out;
    }
    QStringList lastServerArgs() const
    {
        QStringList out;
        const QList<QJsonObject> sessions = agentRead(u"session/new"_s);
        if (sessions.isEmpty())
            return out;
        const QJsonObject server =
                sessions.last().value(u"params"_s).toObject().value(u"mcpServers"_s).toArray().at(0).toObject();
        for (const QJsonValue &a : server.value(u"args"_s).toArray())
            out << a.toString();
        return out;
    }

    /// The messages the agent read, of `method`.
    QList<QJsonObject> agentRead(const QString &method) const
    {
        QList<QJsonObject> out;
        for (const QByteArray &line : QByteArray(fileText(m_agentLog).toUtf8()).split('\n')) {
            const QJsonObject o = QJsonDocument::fromJson(line).object();
            if (o.value(u"method"_s).toString() == method)
                out.append(o);
        }
        return out;
    }

private Q_SLOTS:
    void initTestCase()
    {
        QVERIFY(m_tmp.isValid());
        for (const char *name : {"HOME", "XDG_CONFIG_HOME", "XDG_DATA_HOME", "XDG_CACHE_HOME", "XDG_STATE_HOME"})
            qputenv(name, m_tmp.filePath(QString::fromLatin1(name)).toLocal8Bit());
        QStandardPaths::setTestModeEnabled(true);
        m_agentLog = m_tmp.filePath(u"agent.log"_s);
        qputenv("FAKE_ACP_LOG", m_agentLog.toLocal8Bit());
        m_vault = m_tmp.filePath(u"Notatki na serwerze"_s);
        writeFile(m_vault + u"/Dom.md"_s, "- pierwsza linia: żółta ćma\n- druga\n");
        m_ctx = std::make_unique<Context>();
        m_ctx->globals.vault = m_vault;
        m_ctx->globals.stateDir = m_tmp.filePath(u"state"_s);
        m_ctx->serving = true;
        QVERIFY(!m_ctx->resolveVault());
    }

    void cleanup() { m_host.reset(); }

    void agents_data()
    {
        QTest::addColumn<QString>("spec");
        QTest::addColumn<QString>("id");
        QTest::addColumn<QString>("name");
        QTest::addColumn<QString>("command");
        QTest::newRow("a preset") << u"claude"_s << u"claude"_s << u"Claude Code"_s
                                  << u"npx -y @agentclientprotocol/claude-agent-acp"_s;
        QTest::newRow("a preset's own command")
                << u"claude=/run/current-system/sw/bin/claude-agent-acp"_s << u"claude"_s << u"Claude Code"_s
                << u"/run/current-system/sw/bin/claude-agent-acp"_s;
        QTest::newRow("a name") << u"moj-agent:Mój agent=moj-agent --acp --tryb \"bez pytań\""_s << u"moj-agent"_s
                                << u"Mój agent"_s << u"moj-agent --acp --tryb \"bez pytań\""_s;
        QTest::newRow("no name") << u"x=prog"_s << u"x"_s << u"x"_s << u"prog"_s;
        QTest::newRow("unknown, no command") << u"nieznany"_s << QString() << QString() << QString();
        QTest::newRow("a bad id") << u"zły agent=prog"_s << QString() << QString() << QString();
        QTest::newRow("an empty command") << u"x=   "_s << QString() << QString() << QString();
    }
    void agents()
    {
        QFETCH(QString, spec);
        QFETCH(QString, id);
        QFETCH(QString, name);
        QFETCH(QString, command);
        QString error;
        const std::optional<HostAgent> a = parseHostAgent(spec, &error);
        if (id.isEmpty()) {
            QVERIFY(!a);
            QVERIFY(!error.isEmpty());
            return;
        }
        QVERIFY2(a, qPrintable(error));
        QCOMPARE(a->id, id);
        QCOMPARE(a->name, name);
        QCOMPARE(a->command, command);
    }

    void rewriting()
    {
        const QJsonArray servers{toolsServer(u"/bin/brulion-cli"_s, u"/srv/Notatki"_s)};
        // initialize: files and terminals offered (served here), whatever the device offered.
        const QJsonObject init{
                {u"jsonrpc"_s, u"2.0"_s},
                {u"id"_s, 1},
                {u"method"_s, u"initialize"_s},
                {u"params"_s,
                 QJsonObject{{u"protocolVersion"_s, 1},
                             {u"clientCapabilities"_s, QJsonObject{{u"fs"_s, QJsonObject{{u"readTextFile"_s, false}}},
                                                                   {u"terminal"_s, false},
                                                                   {u"_meta"_s, QJsonObject{{u"x"_s, 1}}}}}}}};
        const QJsonObject caps = rewriteForAgent(init, u"/srv/Notatki"_s, servers)
                                         ->value(u"params"_s)
                                         .toObject()
                                         .value(u"clientCapabilities"_s)
                                         .toObject();
        QVERIFY(caps.value(u"terminal"_s).toBool());
        QVERIFY(caps.value(u"fs"_s).toObject().value(u"readTextFile"_s).toBool());
        QVERIFY(caps.value(u"fs"_s).toObject().value(u"writeTextFile"_s).toBool());
        QCOMPARE(caps.value(u"_meta"_s).toObject().value(u"x"_s).toInt(), 1);
        // A session: this notebook and Brulion's tools here, never the device's own.
        for (const QString &method : {u"session/new"_s, u"session/load"_s, u"session/resume"_s}) {
            const QJsonObject m{
                    {u"jsonrpc"_s, u"2.0"_s},
                    {u"id"_s, 2},
                    {u"method"_s, method},
                    {u"params"_s, QJsonObject{{u"cwd"_s, u"/data/telefon"_s},
                                              {u"sessionId"_s, u"s1"_s},
                                              {u"mcpServers"_s, QJsonArray{QJsonObject{{u"name"_s, u"zły"_s},
                                                                                       {u"command"_s, u"rm"_s}}}}}}};
            const QJsonObject p = rewriteForAgent(m, u"/srv/Notatki"_s, servers)->value(u"params"_s).toObject();
            QCOMPARE(p.value(u"cwd"_s).toString(), u"/srv/Notatki"_s);
            QCOMPARE(p.value(u"mcpServers"_s).toArray(), servers);
            QCOMPARE(p.value(u"sessionId"_s).toString(), u"s1"_s);
        }
        QCOMPARE(servers.at(0).toObject().value(u"args"_s).toArray(),
                 (QJsonArray{u"mcp"_s, u"--vault"_s, u"/srv/Notatki"_s}));
        // Others as they are; no JSON-RPC message dropped.
        const QJsonObject prompt{{u"jsonrpc"_s, u"2.0"_s},
                                 {u"id"_s, 3},
                                 {u"method"_s, u"session/prompt"_s},
                                 {u"params"_s, QJsonObject{{u"cwd"_s, u"/nie/zmieniane"_s}}}};
        QCOMPARE(*rewriteForAgent(prompt, u"/srv"_s, servers), prompt);
        const QJsonObject answer{{u"jsonrpc"_s, u"2.0"_s}, {u"id"_s, u"perm-1"_s}, {u"result"_s, QJsonObject()}};
        QCOMPARE(*rewriteForAgent(answer, u"/srv"_s, servers), answer);
        QVERIFY(!rewriteForAgent(QJsonObject{{u"method"_s, u"initialize"_s}}, u"/srv"_s, servers));
        QVERIFY(!rewriteForAgent(QJsonObject{{u"jsonrpc"_s, u"2.0"_s}}, u"/srv"_s, servers));
        // Served here: the agent's requests for files and terminals, not their notifications.
        QVERIFY(servedHere(QJsonObject{{u"id"_s, 1}, {u"method"_s, u"fs/read_text_file"_s}}));
        QVERIFY(servedHere(QJsonObject{{u"id"_s, 1}, {u"method"_s, u"terminal/create"_s}}));
        QVERIFY(!servedHere(QJsonObject{{u"method"_s, u"terminal/output"_s}}));
        QVERIFY(!servedHere(QJsonObject{{u"id"_s, 1}, {u"method"_s, u"session/request_permission"_s}}));
    }

    void stateFile()
    {
        HostState s;
        s.devices = {{u"dev_A"_s, u"Telefon Żanety"_s, QString(64, u'a'),
                      QDateTime(QDate(2026, 11, 21), QTime(9, 0), QTimeZone::UTC),
                      QDateTime(QDate(2026, 11, 21), QTime(10, 0), QTimeZone::UTC)},
                     {u"no-hash"_s, u"left out"_s, u"short"_s, {}, {}}};
        s.pairingHash = secretHash(u"K7QM2XRP"_s);
        s.pairingExpires = QDateTime(QDate(2026, 11, 21), QTime(9, 10), QTimeZone::UTC);
        s.pairingAttempts = 2;
        s.publicUrl = u"https://agent.example.pl"_s;
        const HostState back = HostState::fromJson(s.toJson());
        QCOMPARE(back.devices.size(), 1);
        QCOMPARE(back.devices.at(0).name, u"Telefon Żanety"_s);
        QCOMPARE(back.devices.at(0).created, s.devices.at(0).created);
        QCOMPARE(back.pairingHash, s.pairingHash);
        QCOMPARE(back.pairingExpires, s.pairingExpires);
        QCOMPARE(back.pairingAttempts, 2);
        QCOMPARE(back.publicUrl, s.publicUrl);
        // Codes: eight of the letters and digits that cannot be mistaken; tokens long and unique.
        for (int i = 0; i < 50; ++i) {
            const QString code = newPairingCode();
            QCOMPARE(code.size(), kPairingCodeLength);
            for (const QChar c : code)
                QVERIFY2(!u"01ILO"_s.contains(c), qPrintable(code));
        }
        QCOMPARE(formatPairingCode(u"K7QM2XRP"_s), u"K7QM-2XRP"_s);
        QVERIFY(newDeviceToken().startsWith(u"bat_"_s));
        QVERIFY(newDeviceToken().size() >= 40);
        QVERIFY(newDeviceToken() != newDeviceToken());
        QCOMPARE(secretHash(u"x"_s).size(), 64);
    }

    void pairing()
    {
        startHost(*m_ctx, options());
        // No code shown: nothing pairs.
        HttpResult r = http("POST", u"pair"_s, R"({"code":"AAAA-BBBB","device":"x"})"_ba);
        QCOMPARE(r.status, 403);
        QCOMPARE(r.json.value(u"error"_s).toObject().value(u"code"_s).toString(), u"no_code"_s);
        // A wrong code, then the right one (typed in lower case).
        const QString code = pairingCode();
        QCOMPARE(code.size(), kPairingCodeLength + 1);
        r = http("POST", u"pair"_s, R"({"code":"ZZZZ-ZZZZ","device":"x"})"_ba);
        QCOMPARE(r.status, 403);
        QCOMPARE(r.json.value(u"error"_s).toObject().value(u"code"_s).toString(), u"bad_code"_s);
        r = http("POST", u"pair"_s,
                 QJsonDocument(QJsonObject{{u"code"_s, code.toLower()}, {u"device"_s, u"Pixel Jana\n\u0007"_s}})
                         .toJson());
        QCOMPARE(r.status, 200);
        const QString token = r.json.value(u"token"_s).toString();
        QVERIFY(token.startsWith(u"bat_"_s));
        QCOMPARE(r.json.value(u"device"_s).toObject().value(u"name"_s).toString(), u"Pixel Jana"_s);
        // Used once: not again. The token is kept only as its hash.
        r = http("POST", u"pair"_s, QJsonDocument(QJsonObject{{u"code"_s, code}, {u"device"_s, u"y"_s}}).toJson());
        QCOMPARE(r.status, 403);
        QVERIFY(!fileText(m_stateFile).contains(token));
        QVERIFY(fileText(m_stateFile).contains(secretHash(token)));
#ifndef Q_OS_WIN // (Windows: the profile's ACLs keep it the user's; Qt reports no such bits there)
        QCOMPARE(QFileInfo(m_stateFile).permissions() & (QFileDevice::ReadGroup | QFileDevice::ReadOther),
                 QFileDevice::Permissions());
#endif
        // `acp clients` lists it.
        const Result clients = run(u"acp.clients"_s);
        QVERIFY(!clients.error);
        const QJsonArray devices = clients.value.toObject().value(u"devices"_s).toArray();
        QCOMPARE(devices.size(), 1);
        QCOMPARE(devices.at(0).toObject().value(u"name"_s).toString(), u"Pixel Jana"_s);
        QCOMPARE(clients.value.toObject().value(u"url"_s).toString(), m_base);
        // Five wrong tries end a code: the right one is no good after them.
        const QString next = pairingCode();
        for (int i = 0; i < kPairingAttempts; ++i)
            QCOMPARE(http("POST", u"pair"_s, R"({"code":"ZZZZ-ZZZZ"})"_ba).status, 403);
        r = http("POST", u"pair"_s, QJsonDocument(QJsonObject{{u"code"_s, next}}).toJson());
        QCOMPARE(r.json.value(u"error"_s).toObject().value(u"code"_s).toString(), u"no_code"_s);
        // An expired code neither.
        const QString late = pairingCode();
        m_host->setClock([] { return QDateTime::currentDateTimeUtc().addSecs(kPairingMinutes * 60 + 1); });
        r = http("POST", u"pair"_s, QJsonDocument(QJsonObject{{u"code"_s, late}}).toJson());
        QCOMPARE(r.json.value(u"error"_s).toObject().value(u"code"_s).toString(), u"no_code"_s);
    }

    void refusals()
    {
        startHost(*m_ctx, options());
        const QString token = pair();
        QVERIFY(!token.isEmpty());
        QCOMPARE(http("GET", u"info"_s).status, 401);
        HttpResult r = http("GET", u"info"_s, {}, bearer(u"bat_nie-ten"_s));
        QCOMPARE(r.status, 401);
        QVERIFY(r.headers.value("www-authenticate").startsWith("Bearer"));
        QCOMPARE(http("GET", u"info"_s, {}, bearer(token)).status, 200);
        // A browser's page, another host name (DNS rebinding).
        auto withOrigin = bearer(token);
        withOrigin.append({"Origin"_ba, "https://zla-strona.example"_ba});
        QCOMPARE(http("GET", u"info"_s, {}, withOrigin).status, 403);
        auto otherHost = bearer(token);
        otherHost.append({"Host"_ba, "zla-strona.example"_ba});
        QCOMPARE(http("GET", u"info"_s, {}, otherHost).status, 421);
        QCOMPARE(http("GET", u"nic"_s, {}, bearer(token)).status, 404);
        QCOMPARE(http("POST", u"info"_s, "{}", bearer(token)).status, 405);
        QCOMPARE(http("GET", u"pair"_s).status, 405);
        // A stream of no agent here; another device's stream.
        r = http("POST", u"streams"_s, R"({"agent":"rm -rf"})"_ba, bearer(token));
        QCOMPARE(r.status, 404);
        QCOMPARE(r.json.value(u"error"_s).toObject().value(u"code"_s).toString(), u"unknown_agent"_s);
        r = http("POST", u"streams"_s, R"({"agent":"fake"})"_ba, bearer(token));
        QCOMPARE(r.status, 201);
        const QString stream = r.json.value(u"stream"_s).toString();
        const QString other = pair(u"Laptop"_s);
        QCOMPARE(http("GET", u"streams/%1/receive?after=0&wait=0"_s.arg(stream), {}, bearer(other)).status, 404);
        QCOMPARE(http("DELETE", u"streams/%1"_s.arg(stream), {}, bearer(token)).status, 204);
        QCOMPARE(http("GET", u"streams/%1/receive?after=0&wait=0"_s.arg(stream), {}, bearer(token)).status, 404);
    }

    void info()
    {
        startHost(*m_ctx, options());
        const QString token = pair();
        const HttpResult r = http("GET", u"info"_s, {}, bearer(token));
        QCOMPARE(r.status, 200);
        const std::optional<acp::RemoteInfo> info = acp::RemoteInfo::fromJson(r.json);
        QVERIFY(info);
        QCOMPARE(info->protocol, acp::RemoteProtocol);
        QCOMPARE(info->root, QDir::cleanPath(m_vault));
        QVERIFY(info->tools);
        QCOMPARE(info->agents.size(), 2);
        QCOMPARE(info->agents.at(0).id, u"fake"_s);
        QCOMPARE(info->agents.at(0).name, u"Udawany agent"_s);
        QVERIFY(!info->sync.value(u"enabled"_s).toBool());
        // The same through the app's side.
        acp::RemoteInfo got;
        acp::RemoteError error;
        bool done = false;
        acp::fetchRemoteInfo(m_transport.get(), QUrl(m_base), token,
                             [&](const acp::RemoteInfo &i, const acp::RemoteError &e) {
                                 got = i;
                                 error = e;
                                 done = true;
                             });
        QTRY_VERIFY_WITH_TIMEOUT(done, kTimeoutMs);
        QVERIFY(!error.failed());
        QCOMPARE(got.agents.at(1).name, u"Z opcjami"_s);
    }

    void conversation()
    {
        QFile::remove(m_agentLog);
        startHost(*m_ctx, options());
        // Paired through the app's side.
        acp::RemotePairing paired;
        bool pairedDone = false;
        acp::pairRemote(m_transport.get(), QUrl(m_base), pairingCode(), u"Telefon"_s,
                        [&](const acp::RemotePairing &p, const acp::RemoteError &e) {
                            QVERIFY(!e.failed());
                            paired = p;
                            pairedDone = true;
                        });
        QTRY_VERIFY_WITH_TIMEOUT(pairedDone, kTimeoutMs);
        std::unique_ptr<Talk> t = talk(paired.token);
        QTRY_VERIFY_WITH_TIMEOUT(!t->root.isEmpty(), kTimeoutMs);
        QCOMPARE(t->root, QDir::cleanPath(m_vault));

        QCOMPARE(ask(*t, u"AI/Rozmowa.md"_s, u"Cześć, co słychać?"_s), u"Odpowiedź na: Cześć, co słychać?"_s);
        // What the agent got: files and terminals offered, this notebook as cwd, Brulion's tools here.
        const QList<QJsonObject> inits = agentRead(u"initialize"_s);
        QCOMPARE(inits.size(), 1);
        const QJsonObject caps = inits.at(0).value(u"params"_s).toObject().value(u"clientCapabilities"_s).toObject();
        QVERIFY(caps.value(u"terminal"_s).toBool());
        QVERIFY(caps.value(u"fs"_s).toObject().value(u"writeTextFile"_s).toBool());
        const QList<QJsonObject> sessions = agentRead(u"session/new"_s);
        QCOMPARE(sessions.size(), 1);
        const QJsonObject params = sessions.at(0).value(u"params"_s).toObject();
        QCOMPARE(params.value(u"cwd"_s).toString(), QDir::cleanPath(m_vault));
        const QJsonArray servers = params.value(u"mcpServers"_s).toArray();
        QCOMPARE(servers.size(), 1);
        QCOMPARE(servers.at(0).toObject().value(u"command"_s).toString(), u"/opt/brulion/bin/brulion-cli"_s);
        QCOMPARE(servers.at(0).toObject().value(u"args"_s).toArray().at(2).toString(), QDir::cleanPath(m_vault));
        // The next turn goes on in the same session.
        QCOMPARE(ask(*t, u"AI/Rozmowa.md"_s, u"A jutro?"_s), u"Odpowiedź na: A jutro?"_s);
        QCOMPARE(agentRead(u"session/new"_s).size(), 1);

        // Files of the notebook here: read, written (outside the notebook refused).
        QCOMPARE(ask(*t, u"AI/Pliki.md"_s, u"czytaj Dom"_s), u"Przeczytano: - pierwsza linia: żółta ćma"_s);
        QCOMPARE(ask(*t, u"AI/Pliki.md"_s, u"zapisz coś"_s), u"Zapisano przez klienta; poza notatnikiem: -32602"_s);
        QCOMPARE(fileText(m_vault + u"/Od agenta.md"_s), u"- od agenta\n"_s);
        QVERIFY(!QFile::exists(u"/etc/brulion-test-outside"_s));
        // A command in a terminal here.
        QCOMPARE(ask(*t, u"AI/Terminal.md"_s, u"terminal"_s), u"Wyjście (4): ąę|drugi"_s);

        // A question of the agent comes to the app; its answer goes back.
        QVERIFY(t->client->prompt(u"AI/Zgoda.md"_s, turn(u"pozwolenie"_s)));
        QTRY_VERIFY_WITH_TIMEOUT(!t->questions.isEmpty(), kTimeoutMs);
        QCOMPARE(t->questions.at(0).first, u"AI/Zgoda.md"_s);
        QCOMPARE(t->questions.at(0).second.options.size(), 2);
        t->client->answer(u"AI/Zgoda.md"_s, u"allow"_s);
        QTRY_VERIFY_WITH_TIMEOUT(t->answers.contains(u"AI/Zgoda.md"_s), kTimeoutMs);
        QCOMPARE(t->answers.value(u"AI/Zgoda.md"_s), u"Zapisano."_s);
        QVERIFY(QFile::exists(m_vault + u"/Zgoda.md"_s));

        // The agent ends: its last words reach the app.
        QCOMPARE(ask(*t, u"AI/Koniec.md"_s, u"koniec"_s).left(9), u"(failed) "_s);
        QTRY_VERIFY_WITH_TIMEOUT(t->ended, kTimeoutMs);
        QCOMPARE(t->end, acp::RemoteStream::End::AgentEnded);
        QVERIFY2(t->endedWhy.contains(u"code 3"_s) && t->endedWhy.contains(u"koniec"_s), qPrintable(t->endedWhy));
        QCOMPARE(t->failures.value(u"AI/Koniec.md"_s), t->endedWhy);
    }

    void history()
    {
        // A page the agent rewrites keeps its earlier text in the device's version history.
        startHost(*m_ctx, options());
        writeFile(m_vault + u"/Od agenta.md"_s, "- przed agentem\n");
        std::unique_ptr<Talk> t = talk(pair());
        QCOMPARE(ask(*t, u"AI/Historia.md"_s, u"zapisz"_s), u"Zapisano przez klienta; poza notatnikiem: -32602"_s);
        QCOMPARE(fileText(m_vault + u"/Od agenta.md"_s), u"- od agenta\n"_s);
        bool found = false;
        QDirIterator it(m_tmp.filePath(u"state"_s), QDir::Files, QDirIterator::Subdirectories);
        while (it.hasNext() && !found) {
            QFile f(it.next());
            found = f.open(QIODevice::ReadOnly) && f.readAll().contains("- przed agentem");
        }
        QVERIFY2(found, "the text before the agent's write is in the history");
    }

    void unpaired()
    {
        startHost(*m_ctx, options());
        const QString token = pair();
        std::unique_ptr<Talk> t = talk(token);
        QCOMPARE(ask(*t, u"AI/A.md"_s, u"raz"_s), u"Odpowiedź na: raz"_s);
        QTRY_COMPARE_WITH_TIMEOUT(m_host->streamCount(), 1, kTimeoutMs);
        const Result revoked = run(u"acp.revoke"_s, QJsonObject{{u"all"_s, true}});
        QVERIFY(!revoked.error);
        QCOMPARE(revoked.value.toObject().value(u"revoked"_s).toArray().size(), 1);
        // The running server takes it at once: the agent stops, the device hears why.
        QTRY_VERIFY_WITH_TIMEOUT(t->ended, kTimeoutMs);
        QCOMPARE(t->end, acp::RemoteStream::End::Unauthorized);
        QTRY_COMPARE_WITH_TIMEOUT(m_host->streamCount(), 0, kTimeoutMs);
        QCOMPARE(http("GET", u"info"_s, {}, bearer(token)).status, 401);
        QCOMPARE(run(u"acp.revoke"_s, QJsonObject{{u"device"_s, u"dev_NIE"_s}}).error->code, u"not_found"_s);
    }

    void oneStreamPerAgent()
    {
        // The app started again: its new stream for the agent ends the old one (and its process).
        startHost(*m_ctx, options());
        const QString token = pair();
        HttpResult r = http("POST", u"streams"_s, R"({"agent":"fake"})"_ba, bearer(token));
        const QString first = r.json.value(u"stream"_s).toString();
        r = http("POST", u"streams"_s, R"({"agent":"fake-config"})"_ba, bearer(token));
        QCOMPARE(r.status, 201);
        r = http("POST", u"streams"_s, R"({"agent":"fake"})"_ba, bearer(token));
        QCOMPARE(r.status, 201);
        QVERIFY(r.json.value(u"stream"_s).toString() != first);
        QCOMPARE(m_host->streamCount(), 2);
        QCOMPARE(http("GET", u"streams/%1/receive?after=0&wait=0"_s.arg(first), {}, bearer(token)).status, 404);
        // Another device's stream of the same agent stays.
        const QString other = pair(u"Tablet"_s);
        QCOMPARE(http("POST", u"streams"_s, R"({"agent":"fake"})"_ba, bearer(other)).status, 201);
        QCOMPARE(m_host->streamCount(), 3);
    }

    void idleStream()
    {
        AcpHostOptions o = options();
        o.streamIdleMs = 300;
        startHost(*m_ctx, o);
        const QString token = pair();
        const HttpResult r = http("POST", u"streams"_s, R"({"agent":"fake"})"_ba, bearer(token));
        QCOMPARE(r.status, 201);
        QCOMPARE(m_host->streamCount(), 1);
        // Nobody fetches from it, no turn runs: given up.
        QTRY_COMPARE_WITH_TIMEOUT(m_host->streamCount(), 0, kTimeoutMs);
        QVERIFY(m_log.join(u'\n').contains(u"no device fetched from it"_s));
    }

    void sendsAndReceives()
    {
        QFile::remove(m_agentLog);
        startHost(*m_ctx, options());
        const QString token = pair();
        HttpResult r = http("POST", u"streams"_s, R"({"agent":"fake"})"_ba, bearer(token));
        const QString stream = r.json.value(u"stream"_s).toString();
        const QString send = u"streams/%1/send"_s.arg(stream);
        const QByteArray init =
                R"({"from":0,"messages":[{"jsonrpc":"2.0","id":1,"method":"initialize","params":{"protocolVersion":1}}]})";
        r = http("POST", send, init, bearer(token));
        QCOMPARE(r.status, 200);
        QCOMPARE(r.json.value(u"received"_s).toInt(), 1);
        // The same batch again (its answer lost): taken, not delivered twice.
        r = http("POST", send, init, bearer(token));
        QCOMPARE(r.json.value(u"received"_s).toInt(), 1);
        // A gap: refused.
        r = http("POST", send, R"({"from":5,"messages":[]})"_ba, bearer(token));
        QCOMPARE(r.status, 409);
        QCOMPARE(http("POST", send, R"({"messages":[]})"_ba, bearer(token)).status, 400);
        // The answer, kept until fetched; fetched again until acknowledged with `after`.
        const QString receive = u"streams/%1/receive?after=%2&wait=%3"_s.arg(stream);
        r = http("GET", receive.arg(0).arg(10), {}, bearer(token));
        QCOMPARE(r.status, 200);
        QJsonArray messages = r.json.value(u"messages"_s).toArray();
        QCOMPARE(messages.size(), 1);
        QCOMPARE(messages.at(0).toObject().value(u"seq"_s).toInt(), 1);
        QCOMPARE(messages.at(0).toObject().value(u"message"_s).toObject().value(u"id"_s).toInt(), 1);
        QCOMPARE(http("GET", receive.arg(0).arg(0), {}, bearer(token)).json.value(u"messages"_s).toArray().size(), 1);
        // Nothing new: the long poll waits, then answers with nothing.
        QElapsedTimer timer;
        timer.start();
        r = http("GET", receive.arg(1).arg(1), {}, bearer(token));
        QCOMPARE(r.status, 200);
        QVERIFY(r.json.value(u"messages"_s).toArray().isEmpty());
        QVERIFY(!r.json.value(u"closed"_s).toBool());
        QVERIFY(timer.elapsed() >= 900);
        QCOMPARE(agentRead(u"initialize"_s).size(), 1);
    }

    void turnKept()
    {
        // The app is ended (Android) while the agent works: the turn goes on here, and the app
        // started again fetches its answer.
        startHost(*m_ctx, options());
        const auto [token, device] = pairDevice();
        std::unique_ptr<Talk> t = talk(token);
        const QString key = u"AI/Długa rozmowa.md"_s;
        acp::TurnInput input = turn(u"powoli, proszę"_s);
        input.turnId = u"tura-żółwia-1"_s; // (its prompt's _meta: the turn kept is named by it)
        QVERIFY(t->client->prompt(key, input));
        QTRY_VERIFY_WITH_TIMEOUT(!t->client->sessionId(key).isEmpty(), kTimeoutMs);
        const QString session = t->client->sessionId(key);
        QTRY_VERIFY_WITH_TIMEOUT(m_host->turn(device, session), kTimeoutMs);
        QCOMPARE(m_host->turn(device, session)->state, HostTurn::State::Running);
        t.reset(); // (gone without a word, as an app Android ended)
        // Another device knows nothing of it.
        QCOMPARE(fetchTurn(pair(u"Tablet"_s), session, 0).second.status, 404);
        // The device asks: the long poll answers once the turn ended.
        const auto [done, error] = fetchTurn(token, session, 25);
        QVERIFY2(!error.failed(), qPrintable(error.detail));
        QCOMPARE(done.state, acp::RemoteTurn::State::Done);
        QCOMPARE(done.id, u"tura-żółwia-1"_s);
        QCOMPARE(done.text, u"Powoli: gotowe."_s);
        QCOMPARE(done.stopReason, u"end_turn"_s);
        QCOMPARE(done.tools, QStringList{u"Read Dom.md"_s});
        // Nobody fetches from that stream now: it ended with the turn.
        QTRY_COMPARE_WITH_TIMEOUT(m_host->streamCount(), 0, kTimeoutMs);
        QCOMPARE(fetchTurn(token, session, 0).first.text, u"Powoli: gotowe."_s);
        // A session the computer knows no turn of.
        const auto [none, lost] = fetchTurn(token, u"nieznana"_s, 0);
        QCOMPARE(lost.kind, acp::RemoteError::Kind::Refused);
        QCOMPARE(lost.status, 404);
        QCOMPARE(lost.code, u"no_turn"_s);
        // The computer started again: it knows nothing.
        startHost(*m_ctx, options());
        QCOMPARE(fetchTurn(pair(), session, 0).second.status, 404);
    }

    void turnOutlivesItsStream()
    {
        // The app started again opens a new stream for the agent while the old one still runs a
        // turn: the old one ends after it, not before.
        startHost(*m_ctx, options());
        const auto [token, device] = pairDevice();
        std::unique_ptr<Talk> t = talk(token);
        const QString key = u"AI/Stara.md"_s;
        QVERIFY(t->client->prompt(key, turn(u"powoli"_s)));
        QTRY_VERIFY_WITH_TIMEOUT(!t->client->sessionId(key).isEmpty(), kTimeoutMs);
        const QString session = t->client->sessionId(key);
        QTRY_VERIFY_WITH_TIMEOUT(m_host->turn(device, session), kTimeoutMs);
        t->stream.reset();
        std::unique_ptr<Talk> fresh = talk(token);
        QTRY_VERIFY_WITH_TIMEOUT(!fresh->root.isEmpty(), kTimeoutMs);
        QCOMPARE(m_host->streamCount(), 2);
        QTRY_COMPARE_WITH_TIMEOUT(m_host->turn(device, session)->state, HostTurn::State::Done, kTimeoutMs);
        QCOMPARE(m_host->turn(device, session)->text, u"Powoli: gotowe."_s);
        QTRY_COMPARE_WITH_TIMEOUT(m_host->streamCount(), 1, kTimeoutMs);
        // (The fake agent numbers its sessions from s1 in every process: the old turn's record
        // is that session's until the new process prompts its own s1.)
        QCOMPARE(ask(*fresh, u"AI/Nowa.md"_s, u"Cześć"_s), u"Odpowiedź na: Cześć"_s);
        // The app closed (the stream ended, DELETE) while a turn runs: the turn ends there all the same.
        QVERIFY(fresh->client->prompt(u"AI/Zamknięta.md"_s, turn(u"powoli"_s)));
        QTRY_VERIFY_WITH_TIMEOUT(!fresh->client->sessionId(u"AI/Zamknięta.md"_s).isEmpty(), kTimeoutMs);
        const QString closed = fresh->client->sessionId(u"AI/Zamknięta.md"_s);
        QTRY_VERIFY_WITH_TIMEOUT(m_host->turn(device, closed), kTimeoutMs);
        fresh->stream->stop();
        QTRY_COMPARE_WITH_TIMEOUT(m_host->turn(device, closed)->state, HostTurn::State::Done, kTimeoutMs);
        QCOMPARE(m_host->turn(device, closed)->text, u"Powoli: gotowe."_s);
        QTRY_COMPARE_WITH_TIMEOUT(m_host->streamCount(), 0, kTimeoutMs);
    }

    void turnQuestionWaits()
    {
        // A question of the agent while the device is gone waits for it with the turn: the device
        // that fetches the turn gets it and answers it there, and the agent goes on.
        startHost(*m_ctx, options());
        QFile::remove(m_vault + u"/Zgoda.md"_s);
        const auto [token, device] = pairDevice();
        std::unique_ptr<Talk> t = talk(token);
        const QString session = askedAndGone(t, u"AI/Pytanie.md"_s, u"pozwolenie"_s);
        QVERIFY(!session.isEmpty());
        QVERIFY(m_host->turn(device, session)->asking);
        const auto [first, e1] = fetchTurn(token, session, 0);
        QVERIFY2(!e1.failed(), qPrintable(e1.detail));
        QCOMPARE(first.state, acp::RemoteTurn::State::Running);
        QVERIFY(first.asking);
        QVERIFY(!first.question.isEmpty());
        const std::optional<acp::Permission> permission = acp::readPermission(first.question.request, {});
        QVERIFY(permission);
        QCOMPARE(permission->title, u"Write Zgoda.md"_s); // (its call's, from the turn: the question names none)
        QCOMPARE(permission->options.size(), 2);
        QCOMPARE(permission->options.at(0).id, u"allow"_s);
        QCOMPARE(permission->diffs.size(), 1);
        QCOMPARE(permission->diffs.at(0).newText, u"- zgoda"_s);
        // (Nobody answers it "cancelled" meanwhile: the device lost the stream, not the question.)
        QTest::qWait(400);
        QVERIFY(m_host->turn(device, session)->asking);
        QVERIFY(!QFile::exists(m_vault + u"/Zgoda.md"_s));
        // Allowed from the turn: the agent writes the file and ends its turn.
        const auto [answered, e2] = answerQuestion(token, session, first.question.id, u"allow"_s);
        QVERIFY2(!e2.failed(), qPrintable(e2.detail));
        QVERIFY(answered.question.isEmpty());
        const auto [done, e3] = fetchTurn(token, session, 25);
        QVERIFY(!e3.failed());
        QCOMPARE(done.state, acp::RemoteTurn::State::Done);
        QCOMPARE(done.stopReason, u"end_turn"_s);
        QCOMPARE(done.text, u"Zapisano."_s);
        QVERIFY(QFile::exists(m_vault + u"/Zgoda.md"_s));
        QTRY_COMPARE_WITH_TIMEOUT(m_host->streamCount(), 0, kTimeoutMs);
        // The same question again, or one that never was: it waits no more.
        auto [none, e4] = answerQuestion(token, session, first.question.id, u"allow"_s);
        QCOMPARE(e4.kind, acp::RemoteError::Kind::Refused);
        QCOMPARE(e4.status, 404);
        QCOMPARE(e4.code, u"no_question"_s);
        QCOMPARE(answerQuestion(token, u"nieznana"_s, first.question.id, u"allow"_s).second.status, 404);

        // Asked after the device lost the stream: it waits for the device as well, and the long
        // poll of the turn hears it at once. Declined there: the agent hears so.
        QFile::remove(m_vault + u"/Zgoda.md"_s);
        std::unique_ptr<Talk> late = talk(token);
        const QString key = u"AI/Później.md"_s;
        QVERIFY(late->client->prompt(key, turn(u"pozwolenie później"_s)));
        QTRY_VERIFY_WITH_TIMEOUT(!late->client->sessionId(key).isEmpty(), kTimeoutMs);
        const QString later = late->client->sessionId(key);
        // (Every process of the fake agent numbers its sessions from s1: the turn of this one.)
        QTRY_VERIFY_WITH_TIMEOUT(m_host->turn(device, later)
                                         && m_host->turn(device, later)->state == HostTurn::State::Running,
                                 kTimeoutMs);
        late.reset();
        acp::RemoteTurn asked;
        QElapsedTimer waited;
        waited.start();
        while (asked.question.isEmpty() && waited.elapsed() < kTimeoutMs) {
            const auto [now, e] = fetchTurn(token, later, 25);
            QVERIFY2(!e.failed(), qPrintable(e.detail));
            QCOMPARE(now.state, acp::RemoteTurn::State::Running);
            asked = now;
        }
        QVERIFY(!asked.question.isEmpty());
        QVERIFY(!answerQuestion(token, later, asked.question.id, u"reject"_s).second.failed());
        const auto [declined, e5] = fetchTurn(token, later, 25);
        QVERIFY(!e5.failed());
        QCOMPARE(declined.state, acp::RemoteTurn::State::Done);
        QCOMPARE(declined.text, u"Nie zapisano."_s);
        QVERIFY(!QFile::exists(m_vault + u"/Zgoda.md"_s));

        // Stopped from afar while its question waits: the question goes too ("cancelled").
        std::unique_ptr<Talk> stopped = talk(token);
        const QString waiting = askedAndGone(stopped, u"AI/Zatrzymana.md"_s, u"pozwolenie"_s);
        QVERIFY(!waiting.isEmpty());
        QVERIFY(!fetchTurn(token, waiting, 0).first.question.isEmpty());
        acp::cancelRemoteTurn(m_transport.get(), QUrl(m_base), token, waiting);
        QTRY_COMPARE_WITH_TIMEOUT(m_host->turn(device, waiting)->state, HostTurn::State::Done, kTimeoutMs);
        QCOMPARE(m_host->turn(device, waiting)->stopReason, u"cancelled"_s);
        QVERIFY(m_host->turn(device, waiting)->question.isEmpty());

        // A turn the device stops from afar.
        std::unique_ptr<Talk> u = talk(token);
        QVERIFY(u->client->prompt(u"AI/Czekam.md"_s, turn(u"czekaj"_s)));
        QTRY_VERIFY_WITH_TIMEOUT(!u->client->sessionId(u"AI/Czekam.md"_s).isEmpty(), kTimeoutMs);
        const QString cancelled = u->client->sessionId(u"AI/Czekam.md"_s);
        QTRY_VERIFY_WITH_TIMEOUT(m_host->turn(device, cancelled)
                                         && m_host->turn(device, cancelled)->state == HostTurn::State::Running,
                                 kTimeoutMs);
        acp::cancelRemoteTurn(m_transport.get(), QUrl(m_base), token, cancelled);
        QTRY_COMPARE_WITH_TIMEOUT(m_host->turn(device, cancelled)->state, HostTurn::State::Done, kTimeoutMs);
        QCOMPARE(m_host->turn(device, cancelled)->stopReason, u"cancelled"_s);
    }

    void turnQuestionExpires()
    {
        // A question nobody fetches — not its stream, not its turn — for --question-wait: answered
        // "cancelled", and the turn ends so. A device that keeps asking for the turn keeps it waiting.
        // (The server's clock is the test's: a stalled machine moves nothing.)
        AcpHostOptions o = options();
        o.questionWaitMs = 10LL * 60 * 1000;
        startHost(*m_ctx, o);
        m_now = QDateTime::currentDateTimeUtc();
        m_host->setClock([this] { return m_now; });
        QFile::remove(m_vault + u"/Zgoda.md"_s);
        const auto [token, device] = pairDevice();
        std::unique_ptr<Talk> t = talk(token);
        t->stream->setWaitSeconds(1); // (its last receive, left behind, ends soon)
        const QString session = askedAndGone(t, u"AI/Bez odpowiedzi.md"_s, u"pozwolenie"_s);
        QVERIFY(!session.isEmpty());
        QTest::qWait(300);
        QVERIFY(m_host->turn(device, session)->asking); // (not yet: the wait is ten minutes)
        // A minute more at each look: answered "cancelled" ten minutes after the last receive.
        const auto aMinuteLater = [this] {
            m_now = m_now.addSecs(60);
            return true;
        };
        QTRY_VERIFY_WITH_TIMEOUT(aMinuteLater() && m_host->turn(device, session)->state == HostTurn::State::Done,
                                 kTimeoutMs);
        QCOMPARE(m_host->turn(device, session)->stopReason, u"cancelled"_s);
        QVERIFY2(m_log.join(u'\n').contains(u"waited"_s), qPrintable(m_log.join(u'\n')));
        QVERIFY(!QFile::exists(m_vault + u"/Zgoda.md"_s));

        std::unique_ptr<Talk> u = talk(token);
        const QString kept = askedAndGone(u, u"AI/Pilnowana.md"_s, u"pozwolenie"_s);
        QVERIFY(!kept.isEmpty());
        // Asked for every six minutes, 24 in all: it waits all along.
        acp::RemoteTurn now;
        for (int i = 0; i < 4; ++i) {
            m_now = m_now.addSecs(6 * 60);
            const auto [turnNow, e] = fetchTurn(token, kept, 0);
            QVERIFY2(!e.failed(), qPrintable(e.detail));
            now = turnNow;
            QCOMPARE(now.state, acp::RemoteTurn::State::Running);
            QVERIFY(!now.question.isEmpty());
            QTest::qWait(150); // (the server looks meanwhile)
        }
        QVERIFY(!answerQuestion(token, kept, now.question.id, u"allow"_s).second.failed());
        QCOMPARE(fetchTurn(token, kept, 25).first.text, u"Zapisano."_s);
        m_host->setClock({});
    }

    void turnQuestionWithoutWait()
    {
        // --question-wait 0: a question of a stream its device lost is answered "cancelled" at once.
        AcpHostOptions o = options();
        o.questionWaitMs = 0;
        startHost(*m_ctx, o);
        QFile::remove(m_vault + u"/Zgoda.md"_s);
        const auto [token, device] = pairDevice();
        std::unique_ptr<Talk> t = talk(token);
        const QString session = askedAndGone(t, u"AI/Pytanie.md"_s, u"pozwolenie"_s);
        QVERIFY(!session.isEmpty());
        // (Not whether it is still asking or running: with no wait the answer may already have
        // gone to the agent, which then ends its turn — on a fast runner before the next line.)
        Q_UNUSED(device)
        const auto [done, e2] = fetchTurn(token, session, 25);
        QVERIFY(!e2.failed());
        QCOMPARE(done.state, acp::RemoteTurn::State::Done);
        QCOMPARE(done.stopReason, u"cancelled"_s);
        QVERIFY(!QFile::exists(m_vault + u"/Zgoda.md"_s)); // (not allowed: not written)
    }

    void browserOrigin()
    {
        // Brulion in a browser at an origin named with --allowed-origin: CORS for it only.
        AcpHostOptions o = options();
        o.allowedOrigins = {u"https://notatki.example.pl/"_s};
        startHost(*m_ctx, o);
        const QString token = pair();
        const QList<std::pair<QByteArray, QByteArray>> preflight{
                {"Origin"_ba, "https://notatki.example.pl"_ba},
                {"Access-Control-Request-Method"_ba, "POST"_ba},
                {"Access-Control-Request-Headers"_ba, "authorization, content-type"_ba},
                {"Access-Control-Request-Private-Network"_ba, "true"_ba}};
        HttpResult r = http("OPTIONS", u"streams"_s, {}, preflight);
        QCOMPARE(r.status, 204);
        QCOMPARE(r.headers.value("access-control-allow-origin"), "https://notatki.example.pl"_ba);
        QVERIFY(r.headers.value("access-control-allow-headers").contains("Authorization"));
        QCOMPARE(r.headers.value("access-control-allow-private-network"), "true"_ba);
        auto allowed = bearer(token);
        allowed.append({"Origin"_ba, "https://notatki.example.pl"_ba});
        r = http("GET", u"info"_s, {}, allowed);
        QCOMPARE(r.status, 200);
        QCOMPARE(r.headers.value("access-control-allow-origin"), "https://notatki.example.pl"_ba);
        // A refusal says why to that page too.
        r = http("GET", u"info"_s, {}, {{"Origin"_ba, "https://notatki.example.pl"_ba}});
        QCOMPARE(r.status, 401);
        QCOMPARE(r.headers.value("access-control-allow-origin"), "https://notatki.example.pl"_ba);
        // Any other page: refused, its preflight too.
        auto other = bearer(token);
        other.append({"Origin"_ba, "https://notatki.example.pl.zla-strona.example"_ba});
        QCOMPARE(http("GET", u"info"_s, {}, other).status, 403);
        QCOMPARE(http("OPTIONS", u"info"_s, {}, {{"Origin"_ba, "null"_ba}}).status, 403);
        // A bad origin given: the server does not start.
        o.allowedOrigins = {u"notatki"_s};
        std::optional<Error> error;
        AcpHost host(*m_ctx, m_ctx->notebook(&error), o);
        QString why;
        QVERIFY(!host.start(&why));
        QVERIFY(why.contains(u"--allowed-origin"_s));
    }

    void pairingByLink()
    {
        startHost(*m_ctx, options());
        const Result r = run(u"acp.pair"_s);
        QVERIFY(!r.error);
        const QString link = r.value.toObject().value(u"link"_s).toString();
        const automation::ParsedUri parsed = automation::parseUri(link);
        QVERIFY2(parsed.request, qPrintable(link));
        QCOMPARE(parsed.request->action, automation::Action::Pair);
        QCOMPARE(parsed.request->value({u"url"}), m_base);
        QCOMPARE(parsed.request->value({u"code"}), r.value.toObject().value(u"code"_s).toString());
        // The text has it as a QR code: block characters, two rows of modules a line.
        QVERIFY(r.text.contains(link));
        const QString qr = terminalQrCode(link);
        const QStringList lines = qr.split(u'\n', Qt::SkipEmptyParts);
        const qr::Code code = qr::encode(link);
        QCOMPARE(lines.size(), (code.size + 2 * qr::QuietZone + 1) / 2);
        for (const QString &line : lines)
            QCOMPARE(line.size(), code.size + 2 * qr::QuietZone);
        QVERIFY(lines.first().count(u'█') == lines.first().size()); // (the quiet zone: light)
        QVERIFY(r.text.contains(lines.at(4)));
        QCOMPARE(pairingLink(QString(), u"K7QM-2XRP"_s), QString());
        QVERIFY(terminalQrCode(QString(5000, u'x')).isEmpty());
    }

    void syncedWithNextcloud()
    {
        // A notebook the devices sync through Nextcloud; this computer's copy starts empty.
        synctest::FakeWebDavServer cloud;
        cloud.putText(u"Brulion/Dom.md"_s, u"- z chmury\n"_s);
        const QString vault = m_tmp.filePath(u"Kopia agenta"_s);
        QVERIFY(QDir().mkpath(vault));
        Context context;
        context.globals.vault = vault;
        context.globals.stateDir = m_tmp.filePath(u"state-sync"_s);
        context.serving = true;
        QVERIFY(!context.resolveVault());
        std::optional<Error> error;
        notebook::Notebook *nb = context.notebook(&error);
        QVERIFY(nb);
        HostSyncOptions s;
        s.server = cloud.options().origin;
        s.user = cloud.options().username;
        s.folder = u"Brulion"_s;
        s.password = cloud.options().password;
        s.device = u"raspberry-pi-4"_s;
        s.afterChangeMs = 50;
        HostSync sync(nb, s, &cloud);
        QStringList syncLog;
        sync.log = [&syncLog](const QString &line) { syncLog.append(line); };
        QString startError;
        QVERIFY2(sync.start(&startError), qPrintable(startError));
        QTRY_COMPARE_WITH_TIMEOUT(fileText(vault + u"/Dom.md"_s), u"- z chmury\n"_s, kTimeoutMs);
        QTRY_VERIFY_WITH_TIMEOUT(!sync.running(), kTimeoutMs);
        QVERIFY(sync.status().value(u"ok"_s).toBool());

        startHost(context, options(), &sync);
        QVERIFY(http("GET", u"info"_s, {}, bearer(pair()))
                        .json.value(u"sync"_s)
                        .toObject()
                        .value(u"enabled"_s)
                        .toBool());
        std::unique_ptr<Talk> t = talk(pair());
        // The phone changed the page a moment ago: the turn waits for it to be here.
        cloud.putText(u"Brulion/Dom.md"_s, u"- zmiana z telefonu: źrebię\n"_s);
        QCOMPARE(ask(*t, u"AI/Sync.md"_s, u"czytaj"_s), u"Przeczytano: - zmiana z telefonu: źrebię"_s);
        // What the agent writes goes to Nextcloud after the turn.
        QCOMPARE(ask(*t, u"AI/Sync.md"_s, u"zapisz"_s), u"Zapisano przez klienta; poza notatnikiem: -32602"_s);
        QTRY_COMPARE_WITH_TIMEOUT(cloud.text(u"Brulion/Od agenta.md"_s).value_or(QString()), u"- od agenta\n"_s,
                                  kTimeoutMs);
        t.reset();
        m_host.reset();
    }

    // The agent's tools reach the app on the device: the real MCP server (brulion-cli mcp), started by
    // the agent with what the session got, calls the tunnel; the device answers over its stream.
    void appToolsThroughTheDevice()
    {
        QFile::remove(m_agentLog);
        AcpHostOptions o = options();
        o.toolsProgram = QString::fromUtf8(BRULION_CLI);
        o.tunnelKey = m_tmp.filePath(u"tunel-1"_s);
        startHost(*m_ctx, o);
        QCOMPARE(m_host->tunnelSocket(), o.tunnelKey);
        const QString token = pair(u"Pixel 9 Pro Jana"_s);
        QCOMPARE(http("GET", u"info"_s, {}, bearer(token)).json.value(u"features"_s).toArray(), QJsonArray{u"app"_s});

        AppDevice device;
        device.answer = [](const acp::AppCall &c) -> std::optional<acp::AppAnswer> {
            if (c.method == u"state")
                return acp::AppAnswer::success(
                        QJsonObject{{u"ready"_s, true}, {u"currentRoute"_s, QJsonObject{{u"view"_s, u"journal"_s}}}});
            if (c.method == u"screenshot")
                return acp::AppAnswer::success(QJsonObject{{u"png"_s, QString::fromLatin1(kTinyPng)},
                                                           {u"width"_s, 1},
                                                           {u"height"_s, 1},
                                                           {u"devicePixelRatio"_s, 2}});
            if (c.method == u"chatPartners")
                return acp::AppAnswer::success(QJsonObject{
                        {u"partners"_s, QJsonArray{QJsonObject{{u"id"_s, u"acp:codex"_s}, {u"name"_s, u"Codex"_s}}}}});
            return acp::AppAnswer::failure(u"bad_params"_s, u"Nie ma tokenu „żółtość”"_s, {{u"param"_s, u"tokens"_s}});
        };
        std::unique_ptr<Talk> t = talk(token, u"fake"_s, &device);
        QTRY_VERIFY_WITH_TIMEOUT(!t->root.isEmpty(), kTimeoutMs);
        QCOMPARE(t->stream->hostFeatures(), QStringList{u"app"_s});

        const QString shot = m_tmp.filePath(u"zrzut ekranu.png"_s);
        const acp::AppSession session{u"h_rozmowa-1"_s, true, true};
        const QString answer =
                ask(*t, u"AI/Aplikacja.md"_s,
                    u"aplikacja lista && brulion_app_state {} && brulion_app_screenshot {\"path\": \"%1\"} && "
                    u"brulion_app_chat_partners {} && brulion_app_theme {\"catalog\": true}"_s.arg(shot),
                    session);
        QVERIFY2(answer.startsWith(u"Aplikacja: "_s), qPrintable(answer));
        const QStringList parts = answer.mid(11).split(u" | "_s);
        QCOMPARE(parts.size(), 5);
        // The live tools and the chat's are there (quit is not among the tunnel's methods, but a tool).
        for (const QString &tool : {u"brulion_app_state"_s, u"brulion_app_eval"_s, u"brulion_app_theme"_s,
                                    u"brulion_app_revert"_s, u"brulion_app_chat_start"_s})
            QVERIFY2(parts.at(0).split(u',').contains(tool), qPrintable(parts.at(0)));
        const QJsonObject state = QJsonDocument::fromJson(parts.at(1).toUtf8()).object();
        QVERIFY2(state.value(u"ok"_s).toBool(), qPrintable(parts.at(1)));
        QCOMPARE(state.value(u"result"_s).toObject().value(u"currentRoute"_s).toObject().value(u"view"_s).toString(),
                 u"journal"_s);
        // The screenshot: a PNG written here where asked, handed to the agent as a picture.
        QVERIFY2(parts.at(2).endsWith(u" [obraz]"_s), qPrintable(parts.at(2)));
        const QJsonObject picture = QJsonDocument::fromJson(parts.at(2).chopped(8).toUtf8()).object();
        QCOMPARE(picture.value(u"result"_s).toObject().value(u"path"_s).toString(), shot);
        QCOMPARE(picture.value(u"result"_s).toObject().value(u"device"_s).toString(), u"Pixel 9 Pro Jana"_s);
        QFile file(shot);
        QVERIFY(file.open(QIODevice::ReadOnly));
        QVERIFY(file.readAll().startsWith("\x89PNG"));
        // The chat's partners: through the conversation's chat grant.
        QVERIFY2(parts.at(3).contains(u"acp:codex"_s), qPrintable(parts.at(3)));
        // A refusal of the device's app reaches the agent with its code and words.
        const QJsonObject refused = QJsonDocument::fromJson(parts.at(4).toUtf8()).object();
        QVERIFY(!refused.value(u"ok"_s).toBool());
        QCOMPARE(refused.value(u"error"_s).toObject().value(u"code"_s).toString(), u"bad_params"_s);
        QVERIFY(refused.value(u"error"_s).toObject().value(u"message"_s).toString().contains(u"żółtość"_s));

        // What the device heard: the session's handle, the scope of each grant, no path of its own.
        QCOMPARE(device.calls.size(), 4);
        QCOMPARE(device.calls.at(0).method, u"state"_s);
        QCOMPARE(device.calls.at(0).handle, u"h_rozmowa-1"_s);
        QCOMPARE(device.calls.at(0).scope, u"control"_s);
        QCOMPARE(device.calls.at(1).method, u"screenshot"_s);
        QVERIFY(!device.calls.at(1).params.contains(u"path"_s));
        QCOMPARE(device.calls.at(2).method, u"chatPartners"_s);
        QCOMPARE(device.calls.at(2).scope, u"chat"_s);
        QCOMPARE(device.calls.at(3).method, u"theme"_s);
        // What the agent got: the tools in their tunnel mode, the grants only in their environment,
        // nothing of the request's `_meta`.
        const QJsonObject created = agentRead(u"session/new"_s).last();
        QVERIFY(!created.value(u"params"_s).toObject().value(u"_meta"_s).toObject().contains(acp::AppMetaKey));
        QVERIFY(lastServerArgs().contains(u"--app-control"_s));
        const QHash<QString, QString> env = lastServerEnv();
        QCOMPARE(env.value(u"BRULION_APP_SOCKET"_s), o.tunnelKey);
        QCOMPARE(env.value(u"BRULION_APP_DEVICE"_s), u"Pixel 9 Pro Jana"_s);
        QCOMPARE(env.value(u"BRULION_APP_CLIENT"_s), u"Udawany agent"_s);
        QCOMPARE(env.value(u"BRULION_APP_TOKEN"_s).size(), 64);
        QCOMPARE(env.value(u"BRULION_APP_CHAT_TOKEN"_s).size(), 64);
        for (const QString &arg : lastServerArgs())
            QVERIFY(!arg.contains(env.value(u"BRULION_APP_TOKEN"_s)));
        t.reset();
        m_host.reset();
    }

    void appToolsRefusals()
    {
        QFile::remove(m_agentLog);
        AcpHostOptions o = options();
        o.tunnelKey = m_tmp.filePath(u"tunel-2"_s);
        o.appCallTimeoutMs = 500 * BRULION_TEST_SLOWDOWN;
        o.maxAppCalls = 2;
        startHost(*m_ctx, o);
        const QString token = pair(u"Telefon"_s);
        AppDevice device;
        device.answer = [](const acp::AppCall &c) -> std::optional<acp::AppAnswer> {
            if (c.method == u"find")
                return std::nullopt; // (held: the app does not answer)
            if (c.method == u"screenshot")
                return acp::AppAnswer::success(QJsonObject{{u"png"_s, u"bmllIG9icmF6"_s}});
            return acp::AppAnswer::success(QJsonObject{{u"method"_s, c.method}});
        };
        device.raw = [](const acp::AppCall &c, acp::Client *client, const QJsonValue &id) {
            if (c.method != u"extensions")
                return false;
            client->respond(id, QJsonObject{{u"coś"_s, 1}}); // (an answer that is none)
            return true;
        };
        std::unique_ptr<Talk> t = talk(token, u"fake"_s, &device);
        // A conversation with the chat's tools alone.
        QCOMPARE(ask(*t, u"AI/Czat.md"_s, u"Cześć"_s, acp::AppSession{u"h_czat"_s, false, true}),
                 u"Odpowiedź na: Cześć"_s);
        QVERIFY(lastServerArgs().contains(u"--app-chat"_s));
        QVERIFY(!lastServerArgs().contains(u"--app-control"_s));
        const QString chatToken = lastServerEnv().value(u"BRULION_APP_CHAT_TOKEN"_s);
        QVERIFY(!chatToken.isEmpty());
        QVERIFY(!lastServerEnv().contains(u"BRULION_APP_TOKEN"_s));

        TunnelClient chat;
        QVERIFY(chat.connect(o.tunnelKey));
        QJsonObject a = chat.answer(chat.send(u"hello"_s, {{u"token"_s, chatToken}}), kTimeoutMs);
        QVERIFY2(a.value(u"ok"_s).toBool(), qPrintable(QString::fromUtf8(QJsonDocument(a).toJson())));
        const QJsonArray methods = a.value(u"result"_s).toObject().value(u"methods"_s).toArray();
        QVERIFY(methods.contains(u"chatStart"_s));
        QVERIFY(!methods.contains(u"state"_s));
        QCOMPARE(a.value(u"result"_s).toObject().value(u"device"_s).toString(), u"Telefon"_s);
        // The chat's methods only.
        QCOMPARE(TunnelClient::errorCode(chat.answer(chat.send(u"state"_s), kTimeoutMs)), u"unauthorized"_s);
        a = chat.answer(chat.send(u"chatPartners"_s), kTimeoutMs);
        QVERIFY(a.value(u"ok"_s).toBool());
        QCOMPARE(device.calls.last().scope, u"chat"_s);
        QCOMPARE(device.calls.last().handle, u"h_czat"_s);

        // A token that is no grant, and none at all.
        TunnelClient stranger;
        QVERIFY(stranger.connect(o.tunnelKey));
        QCOMPARE(TunnelClient::errorCode(stranger.answer(stranger.send(u"state"_s), kTimeoutMs)), u"unauthorized"_s);
        QCOMPARE(TunnelClient::errorCode(
                         stranger.answer(stranger.send(u"hello"_s, {{u"token"_s, QString(64, u'a')}}), kTimeoutMs)),
                 u"unauthorized"_s);

        // A conversation with the live tools.
        QCOMPARE(ask(*t, u"AI/Na żywo.md"_s, u"Zmień motyw"_s, acp::AppSession{u"h_zywo"_s, true, false}),
                 u"Odpowiedź na: Zmień motyw"_s);
        const QString liveToken = lastServerEnv().value(u"BRULION_APP_TOKEN"_s);
        QVERIFY(!liveToken.isEmpty());
        TunnelClient live;
        QVERIFY(live.connect(o.tunnelKey));
        a = live.answer(live.send(u"hello"_s, {{u"token"_s, liveToken}}), kTimeoutMs);
        QVERIFY(a.value(u"result"_s).toObject().value(u"methods"_s).toArray().contains(u"theme"_s));
        QVERIFY(!a.value(u"result"_s).toObject().value(u"methods"_s).toArray().contains(u"quit"_s));
        a = live.answer(live.send(u"theme"_s, {{u"tokens"_s, QJsonObject{{u"accent"_s, u"#33aa77"_s}}}}), kTimeoutMs);
        QVERIFY(a.value(u"ok"_s).toBool());
        QCOMPARE(device.calls.last().scope, u"control"_s);
        QCOMPARE(device.calls.last().params.value(u"tokens"_s).toObject().value(u"accent"_s).toString(), u"#33aa77"_s);
        // Never quit; bad parameters refused before the device hears of them.
        QCOMPARE(TunnelClient::errorCode(live.answer(live.send(u"quit"_s), kTimeoutMs)), u"refused"_s);
        const qsizetype heard = device.calls.size();
        QCOMPARE(TunnelClient::errorCode(live.answer(live.send(u"theme"_s, {{u"żółw"_s, 1}}), kTimeoutMs)),
                 u"bad_params"_s);
        QCOMPARE(TunnelClient::errorCode(
                         live.answer(live.send(u"screenshot"_s, {{u"path"_s, u"względna/ścieżka.png"_s}}), kTimeoutMs)),
                 u"bad_params"_s);
        QCOMPARE(device.calls.size(), heard);
        // A picture that is none; an answer that is none.
        a = live.answer(live.send(u"screenshot"_s), kTimeoutMs);
        QCOMPARE(TunnelClient::errorCode(a), u"failed"_s);
        a = live.answer(live.send(u"extensions"_s), kTimeoutMs);
        QCOMPARE(TunnelClient::errorCode(a), u"failed"_s);

        // Calls the device does not answer: two wait at most, then the third is busy; they fail in time.
        QElapsedTimer waited;
        waited.start();
        const int first = live.send(u"find"_s, {{u"text"_s, u"Zakupy"_s}});
        const int second = live.send(u"find"_s, {{u"text"_s, u"Dom"_s}});
        QTRY_COMPARE_WITH_TIMEOUT(device.held.size(), 2, kTimeoutMs);
        QCOMPARE(TunnelClient::errorCode(live.answer(live.send(u"state"_s), kTimeoutMs)), u"busy"_s);
        a = live.answer(first, kTimeoutMs);
        QCOMPARE(TunnelClient::errorCode(a), u"not_ready"_s);
        QCOMPARE(a.value(u"error"_s).toObject().value(u"details"_s).toObject().value(u"reason"_s).toString(),
                 u"no_answer"_s);
        QVERIFY2(a.value(u"error"_s).toObject().value(u"message"_s).toString().contains(u"Telefon"_s),
                 qPrintable(QString::fromUtf8(QJsonDocument(a).toJson())));
        // (A coarse timer may fire up to 5 % early: 600 ms of 12.5 s under the sanitizers' slowdown.)
        QVERIFY2(waited.elapsed() >= o.appCallTimeoutMs * 9 / 10, qPrintable(QString::number(waited.elapsed())));
        QCOMPARE(TunnelClient::errorCode(live.answer(second, kTimeoutMs)), u"not_ready"_s);
        // Their answers come late: dropped (the agent never hears them), and the next call is fine.
        const qsizetype agentLines = fileText(m_agentLog).size();
        for (const QJsonValue &id : std::as_const(device.held))
            t->client->respond(id, acp::AppAnswer::success(QJsonObject()).toJson());
        a = live.answer(live.send(u"state"_s), kTimeoutMs);
        QVERIFY(a.value(u"ok"_s).toBool());
        QTRY_VERIFY_WITH_TIMEOUT(
                std::ranges::any_of(m_log, [](const QString &l) { return l.contains(u"late answer"_s); }), kTimeoutMs);
        QVERIFY(!fileText(m_agentLog).mid(agentLines).contains(u"_brulion/app"_s));

        // The stream ends (the app closed the agent): the grants with it.
        device.held.clear();
        t->stream->stop();
        QTRY_COMPARE_WITH_TIMEOUT(m_host->streamCount(), 0, kTimeoutMs);
        TunnelClient after;
        QVERIFY(after.connect(o.tunnelKey));
        QCOMPARE(TunnelClient::errorCode(after.answer(after.send(u"hello"_s, {{u"token"_s, liveToken}}), kTimeoutMs)),
                 u"unauthorized"_s);
        t.reset();
        m_host.reset();
    }

    void appToolsOldDeviceAndOff()
    {
        // An older device: no feature in its stream's request — the session gets the plain tools,
        // even when its request asks (and the agent never sees what it asked).
        QFile::remove(m_agentLog);
        AcpHostOptions o = options();
        o.tunnelKey = m_tmp.filePath(u"tunel-3"_s);
        startHost(*m_ctx, o);
        std::unique_ptr<Talk> t = talk(pair());
        QTRY_VERIFY_WITH_TIMEOUT(!t->root.isEmpty(), kTimeoutMs);
        QVERIFY(t->stream->hostFeatures().isEmpty());
        QCOMPARE(ask(*t, u"AI/Stary.md"_s, u"Cześć"_s, acp::AppSession{u"h_stary"_s, true, true}),
                 u"Odpowiedź na: Cześć"_s);
        QCOMPARE(lastServerArgs(), (QStringList{u"mcp"_s, u"--vault"_s, QDir::cleanPath(m_vault)}));
        QVERIFY(lastServerEnv().isEmpty());
        QVERIFY(!agentRead(u"session/new"_s).last().value(u"params"_s).toObject().contains(u"_meta"_s));
        t.reset();

        // A server without the tunnel (--no-app-tools): no feature in info, none for a stream.
        o.appTools = false;
        startHost(*m_ctx, o);
        QVERIFY(m_host->tunnelSocket().isEmpty());
        const QString token = pair();
        QVERIFY(http("GET", u"info"_s, {}, bearer(token)).json.value(u"features"_s).toArray().isEmpty());
        AppDevice device;
        t = talk(token, u"fake"_s, &device);
        QTRY_VERIFY_WITH_TIMEOUT(!t->root.isEmpty(), kTimeoutMs);
        QVERIFY(t->stream->hostFeatures().isEmpty());
        QCOMPARE(ask(*t, u"AI/Bez.md"_s, u"Cześć"_s, acp::AppSession{u"h_bez"_s, true, true}),
                 u"Odpowiedź na: Cześć"_s);
        QVERIFY(!lastServerArgs().contains(u"--app-control"_s));
        t.reset();
        m_host.reset();
    }

    // ---- Code mode's threads (acp/codehost.h) -------------------------------------------------------

    void codeFilePaths_data()
    {
        QTest::addColumn<QString>("file");
        QTest::addColumn<bool>("write");
        QTest::addColumn<QString>("expected"); // relative to the folder; "-" refused
        QTest::newRow("relative") << u"src/main.cpp"_s << true << u"src/main.cpp"_s;
        QTest::newRow("absolute inside") << u"%1/Dom.md"_s << true << u"Dom.md"_s;
        QTest::newRow("dot dot inside") << u"src/../Dom.md"_s << false << u"Dom.md"_s;
        QTest::newRow("dot dot out") << u"../obcy/x"_s << false << u"-"_s;
        QTest::newRow("absolute outside") << u"/etc/passwd"_s << false << u"-"_s;
        QTest::newRow("a prefix of its name") << u"%1-inny/x"_s << false << u"-"_s;
        QTest::newRow("a link out") << u"wyjscie/plik"_s << false << u"-"_s;
        QTest::newRow(".git read") << u".git/config"_s << false << u".git/config"_s;
        QTest::newRow(".git written") << u".git/config"_s << true << u"-"_s;
        QTest::newRow(".GIT written") << u".GIT/hooks/x"_s << true << u"-"_s;
        QTest::newRow("the folder written") << u"."_s << true << u"-"_s;
        QTest::newRow("a NUL") << u"a\0b"_s << false << u"-"_s;
        QTest::newRow("empty") << QString() << false << u"-"_s;
    }
    void codeFilePaths()
    {
        QFETCH(QString, file);
        QFETCH(bool, write);
        QFETCH(QString, expected);
        const QString folder = canonical(m_tmp.path()) + u"/kod-sciezki"_s;
        QDir().mkpath(folder + u"/src"_s);
        QDir().mkpath(m_tmp.filePath(u"obcy"_s));
        QFile::link(m_tmp.filePath(u"obcy"_s), folder + u"/wyjscie"_s);
        const QString got = codeFilePath(folder, file.contains(u"%1"_s) ? file.arg(folder) : file, write);
        QCOMPARE(got, expected == u"-" ? QString() : folder + u'/' + expected);
    }

    void codeProjects()
    {
        // The projects of the --projects file, to a paired device only; read again when it changes.
        const QString repo = m_tmp.filePath(u"Projekt żółwia"_s);
        QVERIFY(gitRepo(repo));
        const QString file = m_tmp.filePath(u"projekty.json"_s);
        writeFile(file, acp::hostProjectsJson({{u"zolw"_s, u"Projekt żółwia"_s, repo, m_tmp.filePath(u"drzewa"_s)}}));
        startHost(*m_ctx, projectOptions(file));
        QCOMPARE(http("GET", u"projects"_s).status, 401);
        QCOMPARE(http("GET", u"projects"_s, {}, bearer(u"bat_nieznany"_s)).status, 401);
        QCOMPARE(http("POST", u"projects/zolw/worktrees"_s, R"({"title":"x"})"_ba).status, 401);
        QCOMPARE(http("GET", u"projects/zolw/changes"_s).status, 401);
        const QString token = pair();
        HttpResult r = http("GET", u"projects"_s, {}, bearer(token));
        QCOMPARE(r.status, 200);
        QJsonArray list = r.json.value(u"projects"_s).toArray();
        QCOMPARE(list.size(), 1);
        QCOMPARE(list.at(0).toObject().value(u"id"_s).toString(), u"zolw"_s);
        QCOMPARE(list.at(0).toObject().value(u"name"_s).toString(), u"Projekt żółwia"_s);
        QCOMPARE(list.at(0).toObject().value(u"path"_s).toString(), QDir::cleanPath(repo));
        QVERIFY(!list.at(0).toObject().contains(u"worktreesRoot"_s)); // (nothing a device needs)
        QVERIFY(http("GET", u"info"_s, {}, bearer(token)).json.value(u"features"_s).toArray().contains(u"code"_s));
        QCOMPARE(http("POST", u"projects"_s, {}, bearer(token)).status, 405);
        QCOMPARE(http("GET", u"projects/zolw"_s, {}, bearer(token)).status, 404);
        // A stream that asks for threads hears that this server runs them.
        r = http("POST", u"streams"_s, R"({"agent":"fake","features":["code"]})"_ba, bearer(token));
        QCOMPARE(r.status, 201);
        QCOMPARE(r.json.value(u"features"_s).toArray(), QJsonArray{u"code"_s});
        // Through the app's side.
        QList<acp::RemoteProject> got;
        acp::RemoteError error;
        bool done = false;
        acp::fetchRemoteProjects(m_transport.get(), QUrl(m_base), token,
                                 [&](const QList<acp::RemoteProject> &p, const acp::RemoteError &e) {
                                     got = p;
                                     error = e;
                                     done = true;
                                 });
        QTRY_VERIFY_WITH_TIMEOUT(done, kTimeoutMs);
        QVERIFY(!error.failed());
        QCOMPARE(got, (QList<acp::RemoteProject>{{u"zolw"_s, u"Projekt żółwia"_s, QDir::cleanPath(repo)}}));
        // A project added (the file written again): there at once; a broken file: none.
        writeFile(file, acp::hostProjectsJson({{u"zolw"_s, u"Projekt żółwia"_s, repo, QString()},
                                               {u"drugi"_s, u"Drugi"_s, m_tmp.filePath(u"drugi"_s), QString()}}));
        QCOMPARE(http("GET", u"projects"_s, {}, bearer(token)).json.value(u"projects"_s).toArray().size(), 2);
        writeFile(file, "to nie jest JSON");
        QCOMPARE(http("GET", u"projects"_s, {}, bearer(token)).json.value(u"projects"_s).toArray().size(), 0);
        QFile::remove(file);
        QCOMPARE(http("GET", u"projects"_s, {}, bearer(token)).json.value(u"projects"_s).toArray().size(), 0);
        // A server without --projects runs no threads.
        startHost(*m_ctx, options());
        const QString other = pair();
        r = http("GET", u"projects"_s, {}, bearer(other));
        QCOMPARE(r.status, 404);
        QCOMPARE(r.json.value(u"error"_s).toObject().value(u"code"_s).toString(), u"no_projects"_s);
        QVERIFY(!http("GET", u"info"_s, {}, bearer(other)).json.value(u"features"_s).toArray().contains(u"code"_s));
        r = http("POST", u"streams"_s, R"({"agent":"fake","features":["code"]})"_ba, bearer(other));
        QVERIFY(r.json.value(u"features"_s).toArray().isEmpty());
        QCOMPARE(http("POST", u"projects/zolw/worktrees"_s, R"({"title":"x"})"_ba, bearer(other)).status, 404);
    }

    void codeThread()
    {
        // A thread's session works in its project's folder: its cwd, files and terminals there, a
        // checkpoint before each turn, the changes since one; the notebook's conversations as before.
        QFile::remove(m_agentLog);
        const QString repo = m_tmp.filePath(u"Wątek żółwia"_s);
        QVERIFY(gitRepo(repo));
        const QString file = m_tmp.filePath(u"projekty-watek.json"_s);
        writeFile(file, acp::hostProjectsJson({{u"zolw"_s, u"Wątek żółwia"_s, repo, QString()}}));
        startHost(*m_ctx, projectOptions(file));
        const auto [token, device] = pairDevice();
        std::unique_ptr<Talk> t = talk(token);
        const acp::CodeSession code{u"zolw"_s, QString(), QString()};
        const QString key = u"AI/Wątek.md"_s;
        const QString folder = canonical(repo);
        QCOMPARE(askCode(*t, key, u"katalog"_s, code), u"Katalog: "_s + folder);
        // The session: the project's cwd, no MCP server (no app tools asked: none of the notebook's
        // either), and the request's project never reaching the agent.
        const QJsonObject params = agentRead(u"session/new"_s).last().value(u"params"_s).toObject();
        QCOMPARE(params.value(u"cwd"_s).toString(), folder);
        QVERIFY(params.value(u"mcpServers"_s).toArray().isEmpty());
        QVERIFY(!params.contains(u"_meta"_s));
        // Its checkpoint, told to the device before the answer, kept with the turn, a ref in git.
        QCOMPARE(t->checkpoints.size(), 1);
        QCOMPARE(t->checkpoints.at(0).first, key);
        const QString first = t->checkpoints.at(0).second.id;
        QCOMPARE(git(repo, {u"rev-parse"_s, u"-q"_s, u"--verify"_s, u"refs/brulion/checkpoints/"_s + first}), 0);
        const QString session = t->client->sessionId(key);
        QCOMPARE(t->checkpoints.at(0).second.session, session);
        QVERIFY(m_host->turn(device, session));
        QCOMPARE(m_host->turn(device, session)->checkpoint, first);
        QCOMPARE(fetchTurn(token, session, 0).first.checkpoint, first);
        // Files: the project's read and written, outside the folder and inside .git refused.
        QCOMPARE(askCode(*t, key, u"czytaj"_s, code), u"Przeczytano: # README żółwia"_s);
        QFile::remove(m_vault + u"/Od agenta.md"_s);
        QCOMPARE(askCode(*t, key, u"zapisz"_s, code), u"Zapisano przez klienta; poza notatnikiem: -32602"_s);
        QCOMPARE(fileText(repo + u"/Od agenta.md"_s), u"- od agenta\n"_s);
        QVERIFY(!QFile::exists(m_vault + u"/Od agenta.md"_s)); // (nothing in the notebook)
        QCOMPARE(t->checkpoints.size(), 3);
        const QString beforeWrite = t->checkpoints.at(2).second.id;
        QCOMPARE(askCode(*t, key, u"do gita"_s, code), u"Do gita: -32602"_s);
        QVERIFY(!QFile::exists(repo + u"/.git/brulion-test"_s));
        // A terminal: in the project's folder, never elsewhere.
        QCOMPARE(askCode(*t, key, u"pwd"_s, code), u"Pwd: %1 | /: -32602"_s.arg(folder));
        // The changes since the checkpoint before "zapisz" (and since none: against HEAD).
        HttpResult r = http("GET", u"projects/zolw/changes?since=%1"_s.arg(beforeWrite), {}, bearer(token));
        QCOMPARE(r.status, 200);
        QJsonArray changes = r.json.value(u"changes"_s).toArray();
        QCOMPARE(changes.size(), 1);
        QCOMPARE(changes.at(0).toObject().value(u"path"_s).toString(), u"Od agenta.md"_s);
        QCOMPARE(changes.at(0).toObject().value(u"change"_s).toString(), u"added"_s);
        r = http("GET", u"projects/zolw/changes?since=%1"_s.arg(t->checkpoints.at(3).second.id), {}, bearer(token));
        QVERIFY(r.json.value(u"changes"_s).toArray().isEmpty());
        r = http("GET", u"projects/zolw/changes"_s, {}, bearer(token));
        QCOMPARE(r.json.value(u"changes"_s).toArray().at(0).toObject().value(u"change"_s).toString(), u"untracked"_s);
        // Through the app's side: a file's two texts.
        acp::RemoteDiff diff;
        acp::RemoteError error;
        bool done = false;
        acp::fetchRemoteDiff(m_transport.get(), QUrl(m_base), token, u"zolw"_s, QString(), beforeWrite, u"Od agenta.md"_s,
                             QString(), [&](const acp::RemoteDiff &d, const acp::RemoteError &e) {
                                 diff = d;
                                 error = e;
                                 done = true;
                             });
        QTRY_VERIFY_WITH_TIMEOUT(done, kTimeoutMs);
        QVERIFY2(!error.failed(), qPrintable(error.detail));
        QCOMPARE(diff.newText, u"- od agenta\n"_s);
        QVERIFY(!diff.oldExists);
        QVERIFY(diff.newExists);
        QList<acp::RemoteChange> since;
        done = false;
        acp::fetchRemoteChanges(m_transport.get(), QUrl(m_base), token, u"zolw"_s, QString(), beforeWrite,
                                [&](const QList<acp::RemoteChange> &c, bool, const acp::RemoteError &e) {
                                    since = c;
                                    error = e;
                                    done = true;
                                });
        QTRY_VERIFY_WITH_TIMEOUT(done, kTimeoutMs);
        QCOMPARE(since.size(), 1);
        QCOMPARE(since.at(0).path, u"Od agenta.md"_s);
        // Refused: a checkpoint, a path or a worktree that is none, an unknown project.
        const auto code400 = [&](const QString &endpoint) { return http("GET", endpoint, {}, bearer(token)).status; };
        QCOMPARE(code400(u"projects/zolw/changes?since=..%2Fx"_s), 400);
        QCOMPARE(code400(u"projects/zolw/diff?path=..%2F..%2Fetc%2Fpasswd"_s), 400);
        QCOMPARE(code400(u"projects/zolw/diff?path=.git%2Fconfig"_s), 400);
        QCOMPARE(code400(u"projects/zolw/diff?path=%2Fetc%2Fpasswd"_s), 400);
        // A folder of the project that is a link out of it: nothing read through it.
        QVERIFY(QFile::link(m_tmp.path(), repo + u"/na-zewnatrz"_s));
        QCOMPARE(code400(u"projects/zolw/diff?path=na-zewnatrz%2Fagent.log"_s), 400);
        QVERIFY(QFile::remove(repo + u"/na-zewnatrz"_s));
        QCOMPARE(code400(u"projects/zolw/changes?worktree=wzgledny"_s), 400);
        r = http("GET", u"projects/zolw/changes?worktree=%1"_s.arg(QString::fromLatin1(QUrl::toPercentEncoding(m_vault))),
                 {}, bearer(token));
        QCOMPARE(r.status, 403);
        QCOMPARE(r.json.value(u"error"_s).toObject().value(u"code"_s).toString(), u"not_a_worktree"_s);
        r = http("GET", u"projects/brak/changes"_s, {}, bearer(token));
        QCOMPARE(r.status, 404);
        QCOMPARE(r.json.value(u"error"_s).toObject().value(u"code"_s).toString(), u"unknown_project"_s);
        // The same stream's conversations of the notebook: the notebook's folder, no checkpoint.
        QCOMPARE(ask(*t, u"AI/Notatki.md"_s, u"katalog"_s), u"Katalog: "_s + QDir::cleanPath(m_vault));
        QCOMPARE(t->checkpoints.size(), 5);
        QCOMPARE(ask(*t, u"AI/Notatki.md"_s, u"pwd"_s), u"Pwd: %1 | /: /"_s.arg(canonical(m_vault)));
    }

    void codeWorktrees()
    {
        // A worktree made here for a device's new thread; a session in a worktree of the project
        // (under its root, or one git lists) — and refused in anything else.
        QFile::remove(m_agentLog);
        const QString repo = m_tmp.filePath(u"Drzewa żółwia"_s);
        QVERIFY(gitRepo(repo));
        const QString root = m_tmp.filePath(u"korzeń drzew"_s);
        const QString file = m_tmp.filePath(u"projekty-drzewa.json"_s);
        writeFile(file, acp::hostProjectsJson({{u"zolw"_s, u"Drzewa żółwia"_s, repo, root}}));
        startHost(*m_ctx, projectOptions(file));
        const QString token = pair();
        HttpResult r = http("POST", u"projects/zolw/worktrees"_s, QJsonDocument(QJsonObject{{u"title"_s, u"Napraw błąd #12"_s}}).toJson(), bearer(token));
        QCOMPARE(r.status, 201);
        const QString folder = r.json.value(u"folder"_s).toString();
        QCOMPARE(folder, QDir::cleanPath(root + u"/zolw/napraw-blad-12"_s));
        QCOMPARE(r.json.value(u"branch"_s).toString(), u"brulion/napraw-blad-12"_s);
        QCOMPARE(fileText(folder + u"/Dom.md"_s), u"# README żółwia\n"_s);
        QCOMPARE(git(repo, {u"rev-parse"_s, u"-q"_s, u"--verify"_s, u"refs/heads/brulion/napraw-blad-12"_s}), 0);
        // The same title again: a free name; through the app's side.
        acp::RemoteWorktree made;
        acp::RemoteError error;
        bool done = false;
        acp::createRemoteWorktree(m_transport.get(), QUrl(m_base), token, u"zolw"_s, u"Napraw błąd #12"_s, QString(),
                                  [&](const acp::RemoteWorktree &w, const acp::RemoteError &e) {
                                      made = w;
                                      error = e;
                                      done = true;
                                  });
        QTRY_VERIFY_WITH_TIMEOUT(done, kTimeoutMs);
        QVERIFY2(!error.failed(), qPrintable(error.detail));
        QCOMPARE(made.folder, folder + u"-2"_s);
        QCOMPARE(made.branch, u"brulion/napraw-blad-12-2"_s);
        // From a branch: a base that is none refused; refusals of what a device sends.
        const auto post = [&](const QByteArray &body, const QString &project = u"zolw"_s) {
            return http("POST", u"projects/%1/worktrees"_s.arg(project), body, bearer(token));
        };
        QCOMPARE(post(R"({"title":"Z gałęzi","base":"main"})").status, 201);
        QCOMPARE(post(R"({"title":"Zła","base":"a..b"})").json.value(u"error"_s).toObject().value(u"code"_s).toString(),
                 u"bad_base"_s);
        QCOMPARE(post(R"({"title":"Zła","base":"--upload-pack=x"})").status, 400);
        QCOMPARE(post(R"({"title":"   "})").json.value(u"error"_s).toObject().value(u"code"_s).toString(), u"bad_title"_s);
        QCOMPARE(post(QJsonDocument(QJsonObject{{u"title"_s, QString(201, u'x')}}).toJson()).status, 400);
        QCOMPARE(post("[]").status, 400);
        QCOMPARE(post(R"({"title":"x"})", u"brak"_s).status, 404);
        QCOMPARE(http("GET", u"projects/zolw/worktrees"_s, {}, bearer(token)).status, 405);

        std::unique_ptr<Talk> t = talk(token);
        // A session in the worktree made: its cwd.
        QCOMPARE(askCode(*t, u"AI/Drzewo.md"_s, u"katalog"_s, {u"zolw"_s, folder, u"brulion/napraw-blad-12"_s}),
                 u"Katalog: "_s + canonical(folder));
        QCOMPARE(t->checkpoints.size(), 1);
        QCOMPARE(git(folder, {u"rev-parse"_s, u"-q"_s, u"--verify"_s, u"refs/brulion/checkpoints/"_s
                                                                           + t->checkpoints.at(0).second.id}),
                 0);
        // One git lists outside the root: taken too.
        const QString elsewhere = m_tmp.filePath(u"inne drzewo"_s);
        QCOMPARE(git(repo, {u"worktree"_s, u"add"_s, u"-q"_s, u"-b"_s, u"inna"_s, elsewhere, u"HEAD"_s}), 0);
        QCOMPARE(askCode(*t, u"AI/Inne.md"_s, u"katalog"_s, {u"zolw"_s, elsewhere, u"inna"_s}),
                 u"Katalog: "_s + canonical(elsewhere));
        // Refused, the agent never asked: a folder that is no worktree of the project, one reached
        // through "..", a link out of the root, a folder that is not there, an unknown project, a
        // project that is none.
        const qsizetype sessions = agentRead(u"session/new"_s).size();
        const QString stranger = m_tmp.filePath(u"obcy folder"_s);
        QDir().mkpath(stranger);
        QDir().mkpath(root + u"/zolw"_s);
        QFile::link(stranger, root + u"/zolw/link"_s);
        const QList<acp::CodeSession> refused{{u"zolw"_s, stranger, QString()},
                                              {u"zolw"_s, root + u"/zolw/../../obcy folder"_s, QString()},
                                              {u"zolw"_s, root + u"/zolw/link"_s, QString()},
                                              {u"zolw"_s, root + u"/zolw/nie-ma"_s, QString()},
                                              {u"zolw"_s, m_vault, QString()},
                                              {u"brak"_s, QString(), QString()}};
        for (qsizetype i = 0; i < refused.size(); ++i) {
            const QString answer = askCode(*t, u"AI/Odmowa %1.md"_s.arg(i), u"katalog"_s, refused.at(i));
            QVERIFY2(answer.startsWith(u"(failed) "_s), qPrintable(answer));
        }
        QVERIFY2(askCode(*t, u"AI/Zła.md"_s, u"katalog"_s, {u"../x"_s, QString(), QString()}).startsWith(u"(failed) "_s),
                 "a project that is none");
        QCOMPARE(agentRead(u"session/new"_s).size(), sessions);
        QVERIFY(m_log.join(u'\n').contains(u"is no worktree of the project"_s));
        // The stream goes on: the next thread's turn is answered.
        QCOMPARE(askCode(*t, u"AI/Drzewo.md"_s, u"Dalej"_s, {u"zolw"_s, folder, u"brulion/napraw-blad-12"_s}),
                 u"Odpowiedź na: Dalej"_s);
        // A server without projects refuses a thread's session the same way.
        startHost(*m_ctx, options());
        std::unique_ptr<Talk> plain = talk(pair());
        const QString answer = askCode(*plain, u"AI/Bez.md"_s, u"katalog"_s, {u"zolw"_s, QString(), QString()});
        QVERIFY2(answer.startsWith(u"(failed) "_s) && answer.contains(u"--projects"_s), qPrintable(answer));
    }

    void codeThreadResumed()
    {
        // The app started again: the thread's session resumed in a new stream works in its project
        // too (session/resume names it), with its checkpoint; resumed elsewhere, refused.
        QFile::remove(m_agentLog);
        const QString repo = m_tmp.filePath(u"Wznowiony"_s);
        QVERIFY(gitRepo(repo));
        const QString file = m_tmp.filePath(u"projekty-wznowione.json"_s);
        writeFile(file, acp::hostProjectsJson({{u"wznow"_s, u"Wznowiony"_s, repo, QString()}}));
        AcpHostOptions o = projectOptions(file);
        o.agents.append({u"fake-resume"_s, u"Wznawia"_s, QString::fromUtf8(FAKE_ACP_AGENT) + u" --resume"_s});
        startHost(*m_ctx, o);
        const QString token = pair();
        const acp::CodeSession code{u"wznow"_s, QString(), QString()};
        const QString folder = canonical(repo);
        const QString key = u"AI/Wznowiony wątek.md"_s;
        std::unique_ptr<Talk> t = talk(token, u"fake-resume"_s);
        QCOMPARE(askCode(*t, key, u"katalog"_s, code), u"Katalog: "_s + folder);
        const QString session = t->client->sessionId(key);
        QVERIFY(!session.isEmpty());
        t.reset();
        std::unique_ptr<Talk> again = talk(token, u"fake-resume"_s);
        acp::TurnInput input = codeTurn(u"katalog"_s, code);
        input.resume = session;
        QVERIFY(again->client->prompt(key, input));
        QTRY_VERIFY_WITH_TIMEOUT(again->answers.contains(key) || again->failures.contains(key), kTimeoutMs);
        QCOMPARE(again->answers.value(key), u"Katalog: "_s + folder);
        const QList<QJsonObject> resumed = agentRead(u"session/resume"_s);
        QCOMPARE(resumed.size(), 1);
        QCOMPARE(resumed.first().value(u"params"_s).toObject().value(u"cwd"_s).toString(), folder);
        QCOMPARE(resumed.first().value(u"params"_s).toObject().value(u"sessionId"_s).toString(), session);
        QCOMPARE(again->checkpoints.size(), 1);
        QCOMPARE(again->checkpoints.first().second.session, session);
        // Resumed naming a folder that is no worktree of the project: refused, the agent never asked.
        std::unique_ptr<Talk> third = talk(token, u"fake-resume"_s);
        acp::TurnInput foreign = codeTurn(u"katalog"_s, {u"wznow"_s, m_vault, QString()});
        foreign.resume = session;
        QVERIFY(third->client->prompt(key, foreign));
        QTRY_VERIFY_WITH_TIMEOUT(third->answers.contains(key) || third->failures.contains(key), kTimeoutMs);
        QVERIFY2(third->failures.contains(key), qPrintable(third->answers.value(key)));
        QCOMPARE(agentRead(u"session/resume"_s).size(), 1);
    }

    void codeThreadWithTheApp()
    {
        // A thread whose session asks for the app's tools: Brulion's tools without the notebook's.
        QFile::remove(m_agentLog);
        const QString repo = m_tmp.filePath(u"Z aplikacją"_s);
        QVERIFY(gitRepo(repo));
        const QString file = m_tmp.filePath(u"projekty-app.json"_s);
        writeFile(file, acp::hostProjectsJson({{u"apka"_s, u"Z aplikacją"_s, repo, QString()}}));
        AcpHostOptions o = projectOptions(file);
        o.tunnelKey = m_tmp.filePath(u"tunel-kod"_s);
        startHost(*m_ctx, o);
        AppDevice device;
        std::unique_ptr<Talk> t = talk(pair(), u"fake"_s, &device);
        acp::TurnInput input = codeTurn(u"Cześć"_s, {u"apka"_s, QString(), QString()});
        input.sessionMeta.insert(acp::AppMetaKey, acp::AppSession{u"h_kod"_s, false, true}.toJson());
        QVERIFY(t->client->prompt(u"AI/Apka.md"_s, input));
        QTRY_VERIFY_WITH_TIMEOUT(t->answers.contains(u"AI/Apka.md"_s), kTimeoutMs);
        QCOMPARE(lastServerArgs(), (QStringList{u"mcp"_s, u"--no-notes"_s, u"--app-chat"_s}));
        QVERIFY(lastServerEnv().contains(u"BRULION_APP_CHAT_TOKEN"_s));
        QVERIFY(!agentRead(u"session/new"_s).last().value(u"params"_s).toObject().contains(u"_meta"_s));
    }
};

QTEST_GUILESS_MAIN(TestCliAcpHost)
#include "tst_cli_acp_host.moc"
