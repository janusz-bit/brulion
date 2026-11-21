// `brulion-cli acp serve` in process over a real local socket (src/cli/acphost.h), with the app's side
// as it talks to it — core acp/remote.h's RemoteStream under acp::Client — and an agent that speaks
// the protocol as a program of its own (tests/app/shell/fakeacpagent.cpp): what is rewritten on the
// way (the device's cwd and MCP servers replaced, files and terminals offered), a conversation
// answered, the agent's questions relayed and answered, files of the notebook read and written here
// (with the version history; outside the notebook refused), a command run in a terminal here, the
// agent's end, pairing (a wrong code, a used one, too many tries), tokens refused, unpairing a device
// that has an agent running, an idle stream given up, sends that come twice, Host and Origin
// refused, and the notebook synced with Nextcloud (an in-memory server) before a turn and after it.
#include "acphost.h"
#include "cli_p.h"
#include "hostsync.h"

#include "acp/acp.h"
#include "acp/remote.h"
#include "automation/uri.h"
#include "fakewebdavserver.h"
#include "qr/qrcode.h"
#include "notebook/session.h"
#include "sync/http.h"

#include <QDir>
#include <QDirIterator>
#include <QElapsedTimer>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QNetworkAccessManager>
#include <QNetworkReply>
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
};

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

    std::unique_ptr<Talk> talk(const QString &token, const QString &agent = u"fake"_s)
    {
        auto t = std::make_unique<Talk>();
        t->stream = std::make_unique<acp::RemoteStream>(m_transport.get(), QUrl(m_base), token, agent);
        t->stream->setRetryDelays({50});
        Talk *raw = t.get();
        acp::RemoteStream *stream = t->stream.get();
        t->client = std::make_unique<acp::Client>([stream](const QByteArray &line) { stream->write(line); }, u"test"_s);
        acp::Client *client = t->client.get();
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
        t->stream->start();
        return t;
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

    QString ask(Talk &t, const QString &key, const QString &text)
    {
        t.answers.remove(key);
        t.failures.remove(key);
        if (!t.client->prompt(key, turn(text)))
            return u"(not started)"_s;
        if (!QTest::qWaitFor([&] { return t.answers.contains(key) || t.failures.contains(key); }, kTimeoutMs))
            return u"(no answer)"_s;
        return t.answers.contains(key) ? t.answers.value(key) : u"(failed) "_s + t.failures.value(key);
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
        QCOMPARE(QFileInfo(m_stateFile).permissions() & (QFileDevice::ReadGroup | QFileDevice::ReadOther),
                 QFileDevice::Permissions());
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
        QVERIFY(t->client->prompt(key, turn(u"powoli, proszę"_s)));
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
    }

    void turnQuestionsCancelled()
    {
        // A question of the agent while the device is gone: answered "cancelled" once the device
        // says it lost the stream; a turn the device stops from afar.
        startHost(*m_ctx, options());
        QFile::remove(m_vault + u"/Zgoda.md"_s);
        const auto [token, device] = pairDevice();
        std::unique_ptr<Talk> t = talk(token);
        QVERIFY(t->client->prompt(u"AI/Pytanie.md"_s, turn(u"pozwolenie"_s)));
        QTRY_VERIFY_WITH_TIMEOUT(!t->questions.isEmpty(), kTimeoutMs);
        const QString session = t->client->sessionId(u"AI/Pytanie.md"_s);
        QVERIFY(m_host->turn(device, session)->asking);
        t.reset();
        const auto [first, e1] = fetchTurn(token, session, 0);
        QVERIFY(!e1.failed());
        QCOMPARE(first.state, acp::RemoteTurn::State::Running);
        QVERIFY(first.asking);
        const auto [done, e2] = fetchTurn(token, session, 25);
        QVERIFY(!e2.failed());
        QCOMPARE(done.state, acp::RemoteTurn::State::Done);
        QCOMPARE(done.stopReason, u"cancelled"_s);
        QVERIFY(!QFile::exists(m_vault + u"/Zgoda.md"_s)); // (not allowed: not written)

        std::unique_ptr<Talk> u = talk(token);
        QVERIFY(u->client->prompt(u"AI/Czekam.md"_s, turn(u"czekaj"_s)));
        QTRY_VERIFY_WITH_TIMEOUT(!u->client->sessionId(u"AI/Czekam.md"_s).isEmpty(), kTimeoutMs);
        const QString waiting = u->client->sessionId(u"AI/Czekam.md"_s);
        QTRY_VERIFY_WITH_TIMEOUT(m_host->turn(device, waiting), kTimeoutMs);
        acp::cancelRemoteTurn(m_transport.get(), QUrl(m_base), token, waiting);
        QTRY_COMPARE_WITH_TIMEOUT(m_host->turn(device, waiting)->state, HostTurn::State::Done, kTimeoutMs);
        QCOMPARE(m_host->turn(device, waiting)->stopReason, u"cancelled"_s);
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
};

QTEST_GUILESS_MAIN(TestCliAcpHost)
#include "tst_cli_acp_host.moc"
