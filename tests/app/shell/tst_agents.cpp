// AI agents of the user's own in the AI chat (workspaceagents.h) on a real Workspace and its
// notebook, the agent a real program that speaks the Agent Client Protocol (fakeacpagent.cpp): a
// new conversation answered by the agent (its page, the answer written with its time; the agent
// started in the notebook's folder, given the conversation's context, the page it is about as a
// file, Brulion's tools or none), the next message in the same session, a question of the agent in
// the conversation (a file it may write) allowed and rejected, Stop (session/cancel, nothing
// written, the next message answered after it), an agent that dies (the turn's failure with its
// last words, the next turn starting it again with the conversation so far), signing in, an agent
// not set up here, the partners the chat offers, the settings' agents stored and read back
// (malformed entries left out, what the chat learnt kept); the notebook's files read and written
// through the app (none outside it), a terminal's output with its tool call and its exit code, a
// plan, a turn of tool calls only, usage and slash commands; a session's options learnt, chosen in
// the chat, remembered for the agent's next sessions and set before their first prompt; a session
// resumed after a restart, a new one when the conversation changed meanwhile; Check; a question no
// chat view shows; Windows' batch files, fs/read_text_file's lines, a terminal's output kept. An
// agent on another computer: the real brulion-cli acp serve with the same fake agent — paired with
// a code of its acp pair, a conversation answered there (its folder as the agent's cwd and in the
// context, that computer's files read), what Android's notification says meanwhile, a turn the app
// lost fetched from there afterwards — also one whose question waits there, answered from the app
// started again —, unpaired there (the turn says so); the settings' remote
// agents (their token in the app's secrets, never in the settings; paired again: the same agent);
// the words for a stream's ends and a pairing's errors. An agent on another computer let change the
// app: its tools there (the real brulion-cli mcp the agent starts) reach this app through acp serve
// and the stream, answered in process (setAppControl()) — a grant named "<agent> on <computer>" and
// a chat grant of its conversation, each call with its own, the session's request naming a handle
// and never a token; not let change it: the chat's tools alone; the grants end with its process. "Let it change the
// app": its tools get
// --app-control and a grant's token in their environment only (BRULION_APP_TOKEN, the agent's name
// as BRULION_APP_CLIENT), one grant per process, the context says so; the grant ends when the
// process stops (its settings changed, it died), and without a grant provider the tools are plain.
// Delegation (chatcontrol.h, chatlineage.h): every agent's session gets --app-chat and its
// conversation's chat grant (BRULION_APP_CHAT_TOKEN), the context names the chat tools; an agent's
// chatStart makes a sub-conversation of its own (`parent:` in its page, listed under it), whose
// agent gets no live tools even when let change the app, and whose answer chatWait brings back; it
// sends to and stops only its own sub-conversations, is refused another parent, a turn that runs
// (busy), a fifth sub-conversation running at once and one deeper than three levels (whose agent
// gets no chat tools either); a grant follows a renamed conversation and ends with the agent.
// The chat's methods for a script (any parent, a timeout of chatWait, a question it waits on,
// chatRead's pages of long texts, partners). Scheduled conversations (chatschedules.h): a rule due
// runs once (also one missed while the app was closed), a new conversation each run or the same
// one going on (skipped while it answers), one of a partner that does not answer here skipped with
// why, Run now, an agent's schedule a sub-conversation of its conversation, kept as JSON; webhooks
// (a schedule's own address: its token kept with it, never in the remote control's answers, masked in
// the log; a run with the request's body as data, at most one a minute, busy while its one
// conversation answers, none for an unknown token or one that is off; a rule left empty with one).
// A secret an agent asks for (chatsecrets.h): only in its own conversation while its turn runs, the
// card's fields, the conversation asking meanwhile, an owner-only file holding the value (never in
// the page) deleted when the turn ends, the value itself when asked so, masked in the log while it
// lives, Cancel, Stop and the time ending the card, a second request busy, a script's request.
// A thread of another computer's project (the real acp serve --projects): its projects listed here, a
// thread answered there in the project or a worktree made there, its checkpoint known here and in
// its page, the turn's changes and a diff from there, a folder that is no worktree refused.
// The app's conversations followed and steered from the phone (ChatControl's chatLive, chatAnswer):
// the revision and its long poll (at once, on a change, on time out, dropped when the client left),
// the rows (running, asking with its question, the messages waiting, ended lately and gone after
// acp::RecentChatMinutes), one in full (its answer's end cut, tools, plan, question with options, diff
// and plan, the messages waiting, a conversation that is none); chatAnswer (answered, no_question,
// bad_option, an agent refused); chatSend's steer and queue; chatStart in a worktree of a project (one
// under the worktrees root, one git lists, a folder that is none).
#include "appsettings.h"
#include "chatcontrol.h"
#include "chatlineage.h"
#include "chatschedules.h"
#include "chatsecrets.h"
#include "codeprojects.h"
#include "remoteprojects.h"
#include "workspace.h"
#include "workspaceagents.h"
#include "workspacechats.h"

#include "acp/appchats.h"
#include "agentusage.h"
#include "chat/chat.h"
#include "log/redact.h"
#include "webhook/webhook.h"

#ifdef BRULION_TEST_IPC
#include "controlserver.h"
#endif

#include <QBuffer>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLocale>
#include <QProcess>
#include <QScopeGuard>
#include <QSettings>
#include <QSignalSpy>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTest>
#include <QTimer>
#include <QUrl>

#include <algorithm>
#include <memory>

using namespace Qt::StringLiterals;
using namespace brulion;
using brulion::shell::AgentConfig;
using brulion::shell::ChatControl;
using brulion::shell::ChatLineage;
using brulion::shell::ChatSchedules;
using brulion::shell::ChatSecrets;
using brulion::shell::Workspace;
using brulion::shell::WorkspaceAgents;
using brulion::shell::WorkspaceChats;

namespace {

bool writeRaw(const QString &file, const QByteArray &bytes)
{
    QDir().mkpath(QFileInfo(file).absolutePath());
    QFile f(file);
    if (!f.open(QIODevice::WriteOnly))
        return false;
    f.write(bytes);
    return true;
}

/// The agent's command line: the fake agent (its path quoted) and `args`.
QString command(const QString &args = QString())
{
    return u"\"%1\""_s.arg(QStringLiteral(FAKE_ACP_AGENT)) + (args.isEmpty() ? QString() : u' ' + args);
}

#if defined(BRULION_CLI) && defined(BRULION_TEST_IPC)
/// The real brulion-cli first on PATH (real agents' tools: toolsProgram()), while the guard lives.
[[nodiscard]] auto cliFirstOnPath()
{
    const QByteArray path = qgetenv("PATH");
    qputenv("PATH", QByteArray(QFileInfo(QStringLiteral(BRULION_CLI)).absolutePath().toLocal8Bit()
                                         + QDir::listSeparator().toLatin1() + path));
    return qScopeGuard([path] { qputenv("PATH", path); });
}

/// The app's control socket for real agents' tools in a private runtime folder, its chat answered
/// as RemoteControl answers it — only the grants' connections (the remote control's setting off);
/// the agents get the chat's grants and the app's instance while it lives.
class RealAgentsControl {
public:
    explicit RealAgentsControl(Workspace *ws) : m_chatControl(ws), m_chats(ws->chats()) { }
    ~RealAgentsControl() { m_chats->agents()->setChatGrants({}, {}); }
    bool listen(const QString &runtime)
    {
        if (!QDir().mkpath(runtime))
            return false;
        QFile::setPermissions(runtime, QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner);
        const QString instance = runtime + u"/brulion-prawdziwe"_s;
        WorkspaceChats *chats = m_chats;
        ChatControl *chatControl = &m_chatControl;
        m_control.setHandler([chatControl, chats](const ipc::ControlRequest &request, const ipc::ControlReply &reply) {
            if (request.method == u"hello") {
                reply.ok(QJsonObject{{u"protocol"_s, ipc::ControlProtocolVersion},
                                     {u"app"_s, u"Brulion"_s},
                                     {u"granted"_s, request.granted}});
                return;
            }
            if (!ChatControl::handles(request.method)) {
                reply.fail(u"unknown_method"_s, request.method);
                return;
            }
            const ChatControl::Caller caller{
                    request.granted, request.granted ? chats->agents()->grantConversation(request.grant) : QString(),
                    request.client};
            ChatControl::Reply answer;
            answer.ok = [reply](const QJsonObject &result) { reply.ok(result); };
            answer.fail = [reply](const QString &code, const QString &message, const QJsonObject &details) {
                reply.fail(code, message, details);
            };
            answer.connected = [reply] { return reply.connected(); };
            chatControl->handle(request.method, request.params, caller, answer);
        });
        m_control.setOpen(false);
        if (!m_control.listen(ipc::controlKeyFor(instance)))
            return false;
        ipc::ControlServer *control = &m_control;
        m_chats->agents()->setChatGrants(
                [control](const QString &client) { return control->addGrant(client, ipc::GrantScope::Chat); },
                [control](const QString &token) { control->revokeGrant(token); });
        m_chats->agents()->setAppInstance(instance);
        return true;
    }
    QString error() const { return m_control.errorString(); }

private:
    ChatControl m_chatControl;
    WorkspaceChats *m_chats;
    ipc::ControlServer m_control;
};
#endif

} // namespace

class TestAgents : public QObject {
    Q_OBJECT

    QTemporaryDir m_tmp;
    int m_n = 0;
    QString m_log;

    std::unique_ptr<Workspace> open(const QList<AgentConfig> &agents, const QString &existing = QString())
    {
        const QString root = existing.isEmpty() ? m_tmp.filePath(u"vault-%1"_s.arg(++m_n)) : existing;
        if (existing.isEmpty())
            writeRaw(root + u"/Dom.md"_s, "- [ ] Zapłacić czynsz 📅 2026-11-16\n");
        auto ws = std::make_unique<Workspace>();
        ws->setClock([] { return QDateTime(QDate(2026, 11, 20), QTime(9, 15)); });
        ws->setUiLanguage(u"pl"_s);
        QString error;
        if (!ws->open(root, root + u"-state"_s, &error))
            qWarning() << error;
        ws->finishIndexing();
        ws->chats()->setAgents(agents);
        // What the agents read, line by line (their environment comes from the test's).
        if (existing.isEmpty())
            m_log = m_tmp.filePath(u"agent-%1.log"_s.arg(m_n));
        qputenv("FAKE_ACP_LOG", m_log.toLocal8Bit());
        return ws;
    }

    /// The messages the agent read with `method`, in order.
    QList<QJsonObject> logged(const QString &method) const
    {
        QFile f(m_log);
        QList<QJsonObject> out;
        if (!f.open(QIODevice::ReadOnly))
            return out;
        for (const QByteArray &line : f.readAll().split('\n')) {
            const QJsonObject o = QJsonDocument::fromJson(line).object();
            if (o.value(u"method"_s).toString() == method)
                out << o;
        }
        return out;
    }

    static QJsonArray promptOf(const QJsonObject &request)
    {
        return request.value(u"params"_s).toObject().value(u"prompt"_s).toArray();
    }

    static AgentConfig fake(bool tools = false, const QString &args = QString())
    {
        return AgentConfig{u"fake"_s, u"Fałszywy"_s, command(args), tools};
    }

    /// The answer of a chat method (ChatControl::handle()): ok and its result, or its error.
    struct Answer {
        bool done = false;
        bool ok = false;
        QJsonObject result;
        QString code;
        QString message;
        QJsonObject details;
    };
    static std::shared_ptr<Answer> ask(ChatControl &control, const QString &method, const QJsonObject &params,
                                       const ChatControl::Caller &caller = {})
    {
        auto a = std::make_shared<Answer>();
        ChatControl::Reply reply;
        reply.ok = [a](const QJsonObject &result) {
            a->done = a->ok = true;
            a->result = result;
        };
        reply.fail = [a](const QString &code, const QString &message, const QJsonObject &details) {
            a->done = true;
            a->code = code;
            a->message = message;
            a->details = details;
        };
        control.handle(method, params, caller, reply);
        return a;
    }
    /// The env of the MCP server of the session/new `n` as {name → value}.
    QHash<QString, QString> serverEnv(qsizetype n) const
    {
        QHash<QString, QString> out;
        const QJsonArray servers =
                logged(u"session/new"_s).at(n).value(u"params"_s).toObject().value(u"mcpServers"_s).toArray();
        for (const QJsonValue &e : servers.at(0).toObject().value(u"env"_s).toArray())
            out.insert(e.toObject().value(u"name"_s).toString(), e.toObject().value(u"value"_s).toString());
        return out;
    }
    QStringList serverArgs(qsizetype n) const
    {
        QStringList out;
        const QJsonArray servers =
                logged(u"session/new"_s).at(n).value(u"params"_s).toObject().value(u"mcpServers"_s).toArray();
        for (const QJsonValue &a : servers.at(0).toObject().value(u"args"_s).toArray())
            out << a.toString();
        return out;
    }

private Q_SLOTS:
    void initTestCase()
    {
        QVERIFY(m_tmp.isValid());
        QLocale::setDefault(QLocale::c());
        if (!WorkspaceAgents::available())
            QSKIP("AI agents run only in the desktop app");
    }

    /// An option's row for the chat's chips: a select of just "Off" and "On" (Claude Code's "Fast
    /// mode") is a switch — its values and state —, other selects and bools are not.
    void optionRows_data()
    {
        QTest::addColumn<QJsonObject>("option");
        QTest::addColumn<bool>("onOff");
        QTest::addColumn<QString>("onValue");
        QTest::addColumn<QString>("offValue");
        QTest::addColumn<bool>("isOn");
        const auto select = [](const QString &current, const QJsonArray &choices) {
            return QJsonObject{{u"id"_s, u"fast"_s}, {u"name"_s, u"Fast mode"_s}, {u"category"_s, u"speed"_s},
                               {u"type"_s, u"select"_s}, {u"currentValue"_s, current}, {u"options"_s, choices}};
        };
        const auto choice = [](const QString &value, const QString &name) {
            return QJsonObject{{u"value"_s, value}, {u"name"_s, name}};
        };
        QTest::newRow("off, on (off)") << select(u"off"_s, {choice(u"off"_s, u"Off"_s), choice(u"on"_s, u"On"_s)})
                                       << true << u"on"_s << u"off"_s << false;
        QTest::newRow("on first (on)") << select(u"fast-on"_s, {choice(u"fast-on"_s, u"On"_s), choice(u"fast-off"_s, u"Off"_s)})
                                       << true << u"fast-on"_s << u"fast-off"_s << true;
        QTest::newRow("words in the values") << select(u"disabled"_s, {choice(u"enabled"_s, u"Szybko"_s),
                                                                       choice(u"disabled"_s, u"Wolno"_s)})
                                             << true << u"enabled"_s << u"disabled"_s << false;
        QTest::newRow("two models") << select(u"opus"_s, {choice(u"opus"_s, u"Opus"_s), choice(u"sonnet"_s, u"Sonnet"_s)})
                                    << false << QString() << QString() << false;
        QTest::newRow("on, on") << select(u"a"_s, {choice(u"a"_s, u"On"_s), choice(u"b"_s, u"On"_s)})
                                << false << QString() << QString() << false;
        QTest::newRow("three choices") << select(u"off"_s, {choice(u"off"_s, u"Off"_s), choice(u"on"_s, u"On"_s),
                                                             choice(u"max"_s, u"Max"_s)})
                                       << false << QString() << QString() << false;
        QTest::newRow("a bool") << QJsonObject{{u"id"_s, u"fast"_s}, {u"name"_s, u"Fast"_s}, {u"type"_s, u"boolean"_s},
                                               {u"currentValue"_s, true}}
                                << false << QString() << QString() << false;
    }
    void optionRows()
    {
        QFETCH(QJsonObject, option);
        QFETCH(bool, onOff);
        QFETCH(QString, onValue);
        QFETCH(QString, offValue);
        QFETCH(bool, isOn);
        const std::optional<acp::ConfigOption> o = acp::ConfigOption::fromJson(option);
        QVERIFY(o);
        const QVariantMap row = shell::agentOptionRow(*o);
        QCOMPARE(row.value(u"id"_s).toString(), option.value(u"id"_s).toString());
        QCOMPARE(row.value(u"onOff"_s).toBool(), onOff);
        QCOMPARE(row.value(u"onValue"_s).toString(), onValue);
        QCOMPARE(row.value(u"offValue"_s).toString(), offValue);
        QCOMPARE(row.value(u"isOn"_s).toBool(), isOn);
    }

    void aConversation()
    {
        auto ws = open({fake()});
        WorkspaceChats *chats = ws->chats();
        const QString path = chats->send(QString(), u"Co mam jutro?"_s, u"Dom"_s, {}, u"acp:fake"_s);
        QCOMPARE(path, u"AI/2026-11-20 Co mam jutro.md"_s);
        QCOMPARE(chats->partnerOf(path), u"acp:fake"_s);
        shell::ChatTurn *turn = chats->turn(path);
        QVERIFY(turn->running());
        QTRY_VERIFY_WITH_TIMEOUT(!turn->running(), 20000);
        QCOMPARE(turn->failure(), QString());
        QVERIFY2(ws->read(path).endsWith(u"- 🤖 Odpowiedź na: Co mam jutro? ([[Dom]])\n  czas:: 2026-11-20 09:15\n"_s),
                 qPrintable(ws->read(path)));

        // Started in the notebook's folder, with the conversation's context, the page as a file.
        const QList<QJsonObject> created = logged(u"session/new"_s);
        QCOMPARE(created.size(), 1);
        const QJsonObject params = created.first().value(u"params"_s).toObject();
        QCOMPARE(params.value(u"cwd"_s).toString(), QDir(ws->rootPath()).absolutePath());
        QCOMPARE(params.value(u"mcpServers"_s).toArray().size(), 0); // (no tools asked for)
        const QJsonArray first = promptOf(logged(u"session/prompt"_s).first());
        QVERIFY(first.size() >= 3);
        const QString context = first.at(0).toObject().value(u"text"_s).toString();
        QVERIFY2(context.contains(u"\"AI/2026-11-20 Co mam jutro\""_s), qPrintable(context));
        QVERIFY(context.contains(u"about the note \"Dom\""_s));
        QVERIFY(!context.contains(u"MCP"_s));
        const QJsonObject page = first.at(1).toObject();
        QCOMPARE(page.value(u"type"_s).toString(), u"resource_link"_s);
        QCOMPARE(page.value(u"uri"_s).toString(),
                 QUrl::fromLocalFile(QDir(ws->rootPath()).filePath(u"Dom.md"_s)).toString(QUrl::FullyEncoded));
        QCOMPARE(first.last().toObject().value(u"text"_s).toString(), u"Co mam jutro? ([[Dom]])"_s);
        const QJsonObject init = logged(u"initialize"_s).first().value(u"params"_s).toObject();
        QCOMPARE(init.value(u"clientInfo"_s).toObject().value(u"name"_s).toString(), u"brulion"_s);

        // The next message: the same session, only the message.
        QCOMPARE(chats->send(path, u"A pojutrze?"_s), path);
        QTRY_VERIFY_WITH_TIMEOUT(!turn->running(), 20000);
        QVERIFY2(ws->read(path).endsWith(u"- 🤖 Odpowiedź na: A pojutrze?\n  czas:: 2026-11-20 09:15\n"_s),
                 qPrintable(ws->read(path)));
        QCOMPARE(logged(u"session/new"_s).size(), 1);
        QCOMPARE(logged(u"initialize"_s).size(), 1);
        const QJsonArray next = promptOf(logged(u"session/prompt"_s).last());
        QCOMPARE(next.size(), 1);
        QCOMPARE(next.first().toObject().value(u"text"_s).toString(), u"A pojutrze?"_s);
    }

    /// Brulion's tools found (a stand-in brulion-cli on PATH where the real one is not): the fake
    /// agent never starts them.
    bool toolsThere()
    {
        if (!WorkspaceAgents::toolsProgram().isEmpty())
            return true;
#ifdef Q_OS_WIN
        return false;
#else
        const QString bin = m_tmp.filePath(u"bin"_s);
        if (!writeRaw(bin + u"/brulion-cli"_s, "#!/bin/sh\nexit 0\n"))
            return false;
        QFile::setPermissions(bin + u"/brulion-cli"_s,
                              QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner);
        qputenv("PATH", (bin + u':' + qEnvironmentVariable("PATH")).toLocal8Bit());
        return !WorkspaceAgents::toolsProgram().isEmpty();
#endif
    }

    void appAccess()
    {
        if (!toolsThere())
            QSKIP("no brulion-cli to name");
        auto ws = open({});
        WorkspaceChats *chats = ws->chats();
        QStringList granted;
        QStringList revoked;
        chats->agents()->setAppGrants(
                [&granted](const QString &client) {
                    granted << client;
                    return u"token-%1"_s.arg(granted.size());
                },
                [&revoked](const QString &token) { revoked << token; });
        // (Before the lists the functions use are gone, whatever way the test ends.)
        auto unset = qScopeGuard([chats] { chats->agents()->setAppGrants({}, {}); });
        AgentConfig agent = fake(true);
        agent.appAccess = true;
        chats->setAgents({agent});
        const QString instance = m_tmp.filePath(u"run/brulion-0123456789abcdef"_s);
        chats->agents()->setAppInstance(instance);
        const auto serverOf = [this](qsizetype session) {
            return logged(u"session/new"_s)
                    .at(session)
                    .value(u"params"_s)
                    .toObject()
                    .value(u"mcpServers"_s)
                    .toArray()
                    .at(0)
                    .toObject();
        };
        const QString path = chats->send(QString(), u"Zmień motyw na zielony"_s, QString(), {}, u"acp:fake"_s);
        QTRY_VERIFY_WITH_TIMEOUT(!chats->turn(path)->running(), 20000);
        QCOMPARE(chats->turn(path)->failure(), QString());
        QCOMPARE(granted, QStringList{u"Fałszywy"_s});
        QJsonObject server = serverOf(0);
        const QJsonArray args = server.value(u"args"_s).toArray();
        QCOMPARE(args.first().toString(), u"mcp"_s);
        QCOMPARE(args.last().toString(), u"--app-control"_s);
        QVERIFY(!QJsonDocument(args).toJson().contains("token-1")); // (others can list a program's arguments)
        QCOMPARE(server.value(u"env"_s).toArray(),
                 (QJsonArray{QJsonObject{{u"name"_s, u"BRULION_APP_TOKEN"_s}, {u"value"_s, u"token-1"_s}},
                             QJsonObject{{u"name"_s, u"BRULION_APP_CLIENT"_s}, {u"value"_s, u"Fałszywy"_s}},
                             QJsonObject{{u"name"_s, u"BRULION_APP_INSTANCE"_s}, {u"value"_s, instance}}}));
        const QString context =
                promptOf(logged(u"session/prompt"_s).first()).at(0).toObject().value(u"text"_s).toString();
        QVERIFY2(context.contains(u"brulion_app_eval"_s) && context.contains(u"undo"_s), qPrintable(context));
        QVERIFY(revoked.isEmpty());

        // Another conversation: the same process, the same grant.
        const QString second = chats->send(QString(), u"A teraz czcionkę"_s, QString(), {}, u"acp:fake"_s);
        QTRY_VERIFY_WITH_TIMEOUT(!chats->turn(second)->running(), 20000);
        QCOMPARE(granted.size(), 1);
        QCOMPARE(serverOf(1).value(u"env"_s).toArray().at(0).toObject().value(u"value"_s).toString(), u"token-1"_s);

        // The agent dies: its grant ends; the next turn starts it with a new one.
        QCOMPARE(chats->send(path, u"To koniec"_s), path);
        QTRY_VERIFY_WITH_TIMEOUT(!chats->turn(path)->running(), 20000);
        QTRY_COMPARE(revoked, QStringList{u"token-1"_s});
        QCOMPARE(chats->send(path, u"Jesteś?"_s), path);
        QTRY_VERIFY_WITH_TIMEOUT(!chats->turn(path)->running(), 20000);
        QCOMPARE(granted.size(), 2);

        // Not allowed any more (another program): stopped, its grant ended, the tools plain.
        agent.appAccess = false;
        chats->setAgents({agent});
        QCOMPARE(revoked, (QStringList{u"token-1"_s, u"token-2"_s}));
        const QString third = chats->send(QString(), u"Bez aplikacji"_s, QString(), {}, u"acp:fake"_s);
        QTRY_VERIFY_WITH_TIMEOUT(!chats->turn(third)->running(), 20000);
        QCOMPARE(granted.size(), 2);
        server = serverOf(logged(u"session/new"_s).size() - 1);
        QVERIFY(!server.value(u"args"_s).toArray().contains(u"--app-control"_s));
        QCOMPARE(server.value(u"env"_s).toArray(), QJsonArray());
        QVERIFY(!promptOf(logged(u"session/prompt"_s).last())
                         .at(0)
                         .toObject()
                         .value(u"text"_s)
                         .toString()
                         .contains(u"brulion_app_"_s));

        // Without a grant provider (no remote control here): plain tools, whatever the setting.
        chats->agents()->setAppGrants({}, {});
        agent.appAccess = true;
        chats->setAgents({agent});
        const QString fourth = chats->send(QString(), u"Bez pozwolenia"_s, QString(), {}, u"acp:fake"_s);
        QTRY_VERIFY_WITH_TIMEOUT(!chats->turn(fourth)->running(), 20000);
        QVERIFY(!serverOf(logged(u"session/new"_s).size() - 1).value(u"args"_s).toArray().contains(u"--app-control"_s));
        // The notebook closes: nothing more to revoke (no grant was made).
        unset.dismiss(); // (it would reach the workspace after it is gone)
        chats->agents()->setAppGrants({}, {});
        ws.reset();
        QCOMPARE(revoked.size(), 2);
    }

    void delegation()
    {
        if (!toolsThere())
            QSKIP("no brulion-cli to name");
        auto ws = open({});
        WorkspaceChats *chats = ws->chats();
        QStringList chatGranted, chatRevoked, fullGranted;
        chats->agents()->setChatGrants(
                [&chatGranted](const QString &client) {
                    chatGranted << client;
                    return u"chat-%1"_s.arg(chatGranted.size());
                },
                [&chatRevoked](const QString &token) { chatRevoked << token; });
        chats->agents()->setAppGrants(
                [&fullGranted](const QString &client) {
                    fullGranted << client;
                    return u"full-%1"_s.arg(fullGranted.size());
                },
                [](const QString &) {});
        auto unset = qScopeGuard([chats] {
            chats->agents()->setChatGrants({}, {});
            chats->agents()->setAppGrants({}, {});
        });
        AgentConfig codex{u"codex"_s, u"Kodeks"_s, command(), true};
        codex.appAccess = true; // (let change the app — never in a sub-conversation)
        chats->setAgents({fake(true), codex});
        // The app names itself to the tools: an agent may not pass on the environment its socket is
        // found from (Codex gives its MCP servers HOME, PATH and a few more only).
        const QString instance = m_tmp.filePath(u"run/brulion-0123456789abcdef"_s);
        chats->agents()->setAppInstance(instance);

        // The user's conversation with an agent: its tools get the chat's grant for it.
        const QString path = chats->send(QString(), u"Zaplanuj wyjazd do Zakopanego"_s, QString(), {}, u"acp:fake"_s);
        QTRY_VERIFY_WITH_TIMEOUT(!chats->turn(path)->running(), 20000);
        QCOMPARE(chatGranted, QStringList{u"Fałszywy"_s});
        QCOMPARE(serverArgs(0).last(), u"--app-chat"_s);
        QCOMPARE(serverEnv(0).value(u"BRULION_APP_CHAT_TOKEN"_s), u"chat-1"_s);
        QCOMPARE(serverEnv(0).value(u"BRULION_APP_CLIENT"_s), u"Fałszywy"_s);
        QCOMPARE(serverEnv(0).value(u"BRULION_APP_INSTANCE"_s), instance);
        QVERIFY(!serverEnv(0).contains(u"BRULION_APP_TOKEN"_s));
        const QString context =
                promptOf(logged(u"session/prompt"_s).first()).at(0).toObject().value(u"text"_s).toString();
        QVERIFY2(context.contains(u"brulion_app_chat_start"_s), qPrintable(context));
        QVERIFY(!context.contains(u"brulion_app_eval"_s));
        QCOMPARE(chats->agents()->grantConversation(u"chat-1"_s), path);
        QCOMPARE(chats->agents()->grantConversation(u"nic"_s), QString());

        // The agent asks Codex for a second opinion: a sub-conversation of its own.
        ChatControl control(ws.get());
        const ChatControl::Caller agent{true, chats->agents()->grantConversation(u"chat-1"_s), u"Fałszywy"_s};
        auto a = ask(control, u"chatStart"_s,
                     {{u"text"_s, u"Druga opinia: czy ten wyjazd ma sens?"_s}, {u"partner"_s, u"acp:codex"_s}},
                     agent);
        QVERIFY2(a->ok, qPrintable(a->message));
        const QString child = a->result.value(u"path"_s).toString();
        QVERIFY(child.startsWith(u"AI/2026-11-20 Druga opinia"_s));
        QCOMPARE(a->result.value(u"parent"_s).toString(), path);
        QCOMPARE(a->result.value(u"partnerName"_s).toString(), u"Kodeks"_s);
        QVERIFY2(ws->read(child).contains(u"parent: \"[[%1]]\""_s.arg(path.chopped(3))), qPrintable(ws->read(child)));
        QCOMPARE(chats->lineage()->parentOf(child), path);
        QCOMPARE(chats->lineage()->childrenOf(path), QStringList{child});
        QCOMPARE(chats->lineage()->depthOf(child), 1);
        // Its answer comes back.
        a = ask(control, u"chatWait"_s, {{u"path"_s, child}, {u"ms"_s, 20000}}, agent);
        QTRY_VERIFY_WITH_TIMEOUT(a->done, 25000);
        QVERIFY2(a->ok, qPrintable(a->message));
        QCOMPARE(a->result.value(u"state"_s).toString(), u"idle"_s);
        QVERIFY2(a->result.value(u"answer"_s).toObject().value(u"text"_s).toString().startsWith(u"Odpowiedź na:"_s),
                 QJsonDocument(a->result).toJson().constData());
        // Codex's session: the chat's grant for the sub-conversation, never the live tools.
        QCOMPARE(logged(u"session/new"_s).size(), 2);
        QVERIFY(!serverArgs(1).contains(u"--app-control"_s));
        QCOMPARE(serverArgs(1).last(), u"--app-chat"_s);
        QCOMPARE(serverEnv(1).value(u"BRULION_APP_CHAT_TOKEN"_s), u"chat-2"_s);
        QCOMPARE(serverEnv(1).value(u"BRULION_APP_INSTANCE"_s), instance);
        QVERIFY(!serverEnv(1).contains(u"BRULION_APP_TOKEN"_s));
        QCOMPARE(chats->agents()->grantConversation(u"chat-2"_s), child);
        // The parent's view lists it.
        const QVariantList rows = chats->lineage()->children(path);
        QCOMPARE(rows.size(), 1);
        QCOMPARE(rows.first().toMap().value(u"partner"_s).toString(), u"Kodeks"_s);
        QCOMPARE(rows.first().toMap().value(u"state"_s).toString(), u"idle"_s);
        a = ask(control, u"chats"_s, {{u"parent"_s, path}});
        QCOMPARE(a->result.value(u"total"_s).toInt(), 1);
        QCOMPARE(a->result.value(u"conversations"_s).toArray().at(0).toObject().value(u"path"_s).toString(), child);

        // Its own sub-conversation only: another parent, the user's other conversations are refused.
        const QString other = chats->send(QString(), u"Inna rozmowa"_s, QString(), {}, u"acp:fake"_s);
        QTRY_VERIFY_WITH_TIMEOUT(!chats->turn(other)->running(), 20000);
        a = ask(control, u"chatStart"_s, {{u"text"_s, u"Ą"_s}, {u"parent"_s, other}}, agent);
        QCOMPARE(a->code, u"refused"_s);
        a = ask(control, u"chatSend"_s, {{u"path"_s, other}, {u"text"_s, u"Zmień to"_s}}, agent);
        QCOMPARE(a->code, u"refused"_s);
        a = ask(control, u"chatStop"_s, {{u"path"_s, other}}, agent);
        QCOMPARE(a->code, u"refused"_s);
        a = ask(control, u"chatStart"_s, {{u"text"_s, u"Ą"_s}, {u"partner"_s, u"acp:nieznany"_s}}, agent);
        QCOMPARE(a->code, u"not_found"_s);
        // A follow-up: while its turn runs, busy; a wait runs out; Stop.
        a = ask(control, u"chatSend"_s, {{u"path"_s, child}, {u"text"_s, u"czekaj na mnie"_s}}, agent);
        QVERIFY2(a->ok, qPrintable(a->message));
        QCOMPARE(a->result.value(u"state"_s).toString(), u"running"_s);
        a = ask(control, u"chatSend"_s, {{u"path"_s, child}, {u"text"_s, u"I jeszcze"_s}}, agent);
        QCOMPARE(a->code, u"busy"_s);
        // … or in line after the answer, when asked to (then out of line again here).
        a = ask(control, u"chatSend"_s, {{u"path"_s, child}, {u"text"_s, u"Po odpowiedzi"_s}, {u"queue"_s, true}}, agent);
        QVERIFY2(a->ok, qPrintable(a->message));
        QVERIFY(a->result.value(u"queued"_s).toBool());
        QCOMPARE(chats->queued(child).size(), 1);
        chats->unqueue(child, chats->queued(child).first().toMap().value(u"id"_s).toInt());
        a = ask(control, u"chatWait"_s, {{u"path"_s, child}, {u"ms"_s, 300}}, agent);
        QTRY_VERIFY_WITH_TIMEOUT(a->done, 5000);
        QCOMPARE(a->code, u"timeout"_s);
        QCOMPARE(a->details.value(u"state"_s).toString(), u"running"_s);
        a = ask(control, u"chatStop"_s, {{u"path"_s, child}}, agent);
        QVERIFY(a->ok);
        QVERIFY(a->result.value(u"stopped"_s).toBool());
        QTRY_VERIFY_WITH_TIMEOUT(!chats->isRunning(child), 10000);

        // At most four sub-conversations run at once.
        QStringList waiting;
        for (int i = 0; i < ChatLineage::MaxRunningChildren; ++i) {
            a = ask(control, u"chatStart"_s, {{u"text"_s, u"czekaj %1"_s.arg(i)}, {u"partner"_s, u"acp:codex"_s}}, agent);
            QVERIFY2(a->ok, qPrintable(a->message));
            waiting << a->result.value(u"path"_s).toString();
        }
        a = ask(control, u"chatStart"_s, {{u"text"_s, u"Piąta"_s}, {u"partner"_s, u"acp:codex"_s}}, agent);
        QCOMPARE(a->code, u"refused"_s);
        QCOMPARE(a->details.value(u"reason"_s).toString(), u"children"_s);
        a = ask(control, u"chatSend"_s, {{u"path"_s, child}, {u"text"_s, u"Szósta"_s}}, agent);
        QCOMPARE(a->code, u"refused"_s);
        // (Every session so far: the conversation, Codex's answer, the other conversation, the four
        // that wait. They are stopped once their agent has them: a slow start stopped sooner opens none.)
        QTRY_COMPARE_WITH_TIMEOUT(logged(u"session/new"_s).size(), 7, 20000);
        for (const QString &w : std::as_const(waiting))
            QVERIFY(ask(control, u"chatStop"_s, {{u"path"_s, w}}, agent)->ok);
        QTRY_VERIFY_WITH_TIMEOUT(chats->lineage()->runningChildren(path) == 0, 10000);

        // Three levels deep at most: such a conversation's agent gets no chat tools.
        QCOMPARE(logged(u"session/new"_s).size(), 7);
        ws->write(u"AI/b.md"_s, u"---\nparent: \"[[%1]]\"\n---\n- 🧑 b\n"_s.arg(child.chopped(3)));
        ws->write(u"AI/c.md"_s, u"---\npartner: acp:fake\nparent: \"[[AI/b]]\"\n---\n- 🧑 c\n"_s);
        QCOMPARE(chats->lineage()->depthOf(u"AI/c.md"_s), 3);
        a = ask(control, u"chatStart"_s, {{u"text"_s, u"Jeszcze głębiej"_s}}, ChatControl::Caller{true, u"AI/c.md"_s, u"x"_s});
        QCOMPARE(a->code, u"refused"_s);
        QCOMPARE(a->details.value(u"reason"_s).toString(), u"depth"_s);
        const qsizetype before = logged(u"session/new"_s).size();
        QVERIFY(chats->retry(u"AI/c.md"_s));
        QTRY_VERIFY_WITH_TIMEOUT(!chats->turn(u"AI/c.md"_s)->running(), 20000);
        QCOMPARE(logged(u"session/new"_s).size(), before + 1);
        QVERIFY(!serverArgs(before).contains(u"--app-chat"_s));
        QVERIFY(!serverEnv(before).contains(u"BRULION_APP_CHAT_TOKEN"_s));

        // A renamed conversation takes its grant along; the agent's end ends its grants.
        const QString renamed = chats->rename(path, u"Wyjazd w góry"_s);
        QVERIFY(!renamed.isEmpty());
        QCOMPARE(chats->agents()->grantConversation(u"chat-1"_s), renamed);
        QTRY_COMPARE(chats->lineage()->parentOf(child), renamed); // (the child's link follows the rename)
        QVERIFY(chatRevoked.isEmpty());
        QCOMPARE(chats->send(renamed, u"koniec"_s), renamed);
        QTRY_VERIFY_WITH_TIMEOUT(chatRevoked.contains(u"chat-1"_s), 20000);
        QCOMPARE(chats->agents()->grantConversation(u"chat-1"_s), QString());
        QVERIFY(fullGranted.size() <= 1); // (Codex's process has its own; its sub-conversations never use it)
        unset.dismiss();
        chats->agents()->setChatGrants({}, {});
        chats->agents()->setAppGrants({}, {});
    }

    void chatMethods()
    {
        auto ws = open({fake()});
        WorkspaceChats *chats = ws->chats();
        ChatControl control(ws.get());
        // Partners: the assistant and the agent.
        auto a = ask(control, u"chatPartners"_s, {});
        QVERIFY(a->ok);
        const QJsonArray partners = a->result.value(u"partners"_s).toArray();
        QCOMPARE(partners.first().toObject().value(u"kind"_s).toString(), u"assistant"_s);
        QVERIFY(std::ranges::any_of(partners, [](const QJsonValue &p) {
            return p.toObject().value(u"id"_s).toString() == u"acp:fake"
                   && p.toObject().value(u"kind"_s).toString() == u"agent";
        }));
        // A script starts a conversation (no parent needed) and waits for it.
        a = ask(control, u"chatStart"_s, {{u"text"_s, u"Co słychać?"_s}, {u"partner"_s, u"acp:fake"_s}, {u"page"_s, u"Dom"_s}});
        QVERIFY2(a->ok, qPrintable(a->message));
        const QString path = a->result.value(u"path"_s).toString();
        QVERIFY(!a->result.contains(u"parent"_s));
        QCOMPARE(a->result.value(u"page"_s).toString(), u"Dom"_s);
        a = ask(control, u"chatWait"_s, {{u"path"_s, path.chopped(3)}}); // (".md" optional)
        QTRY_VERIFY_WITH_TIMEOUT(a->done, 20000);
        QVERIFY(a->ok);
        QCOMPARE(a->result.value(u"answer"_s).toObject().value(u"position"_s).toInt(), 1);
        // Waiting on a turn that does not run answers at once.
        a = ask(control, u"chatWait"_s, {{u"path"_s, path}});
        QVERIFY(a->done && a->ok);
        // Its name is enough; one not there is not found.
        QCOMPARE(control.resolve(u"Co słychać"_s), path);
        QCOMPARE(control.resolve(path.mid(3)), path);
        QCOMPARE(ask(control, u"chatRead"_s, {{u"path"_s, u"Nie ma takiej"_s}})->code, u"not_found"_s);
        // A question it waits on ends a wait.
        QCOMPARE(chats->send(path, u"Daj pozwolenie"_s), path);
        QTRY_VERIFY_WITH_TIMEOUT(chats->asking(path), 20000);
        a = ask(control, u"chatWait"_s, {{u"path"_s, path}});
        QVERIFY(a->done && a->ok);
        QCOMPARE(a->result.value(u"state"_s).toString(), u"asking"_s);
        QVERIFY(a->result.value(u"question"_s).toObject().value(u"text"_s).toString().contains(u"Zgoda.md"_s));
        QVERIFY(!a->result.value(u"question"_s).toObject().value(u"options"_s).toArray().isEmpty());
        a = ask(control, u"chats"_s, {});
        QCOMPARE(a->result.value(u"conversations"_s).toArray().at(0).toObject().value(u"state"_s).toString(), u"asking"_s);
        chats->stop(path);
        QTRY_VERIFY_WITH_TIMEOUT(!chats->isRunning(path), 10000);
        // A long message comes in pages: cut, with where to go on.
        const QString longText = QString(u"żółw "_s).repeated(8000).trimmed(); // (39 999 characters)
        ws->write(u"AI/długa.md"_s, u"- 🧑 Krótko\n- 🤖 "_s + longText + u"\n- 🧑 Koniec\n"_s);
        a = ask(control, u"chatRead"_s, {{u"path"_s, u"AI/długa.md"_s}});
        QVERIFY(a->ok);
        QCOMPARE(a->result.value(u"total"_s).toInt(), 3);
        QJsonArray messages = a->result.value(u"messages"_s).toArray();
        QCOMPARE(messages.size(), 2);
        QVERIFY(messages.at(1).toObject().value(u"cut"_s).toBool());
        const int offset = a->result.value(u"next"_s).toObject().value(u"offset"_s).toInt();
        QCOMPARE(offset, ChatControl::MaxReadChars - 6);
        a = ask(control, u"chatRead"_s, {{u"path"_s, u"AI/długa.md"_s}, {u"from"_s, 1}, {u"offset"_s, offset}});
        messages = a->result.value(u"messages"_s).toArray();
        QCOMPARE(messages.size(), 2);
        QCOMPARE(messages.at(0).toObject().value(u"offset"_s).toInt(), offset);
        QCOMPARE(messages.at(0).toObject().value(u"text"_s).toString(), longText.mid(offset));
        QCOMPARE(messages.at(1).toObject().value(u"text"_s).toString(), u"Koniec"_s);
        QVERIFY(!a->result.contains(u"next"_s));
        // A script may name any parent; a missing one is not found.
        a = ask(control, u"chatStart"_s, {{u"text"_s, u"Podrzędna"_s}, {u"partner"_s, u"acp:fake"_s}, {u"parent"_s, path}});
        QVERIFY2(a->ok, qPrintable(a->message));
        QCOMPARE(a->result.value(u"parent"_s).toString(), path);
        QCOMPARE(ask(control, u"chatStart"_s, {{u"text"_s, u"Ą"_s}, {u"parent"_s, u"AI/brak"_s}})->code, u"not_found"_s);
        QTRY_VERIFY_WITH_TIMEOUT(!chats->isRunning(a->result.value(u"path"_s).toString()), 20000);
        // No notebook: not ready.
        ws->close();
        QCOMPARE(ask(control, u"chats"_s, {})->code, u"not_ready"_s);
    }

    // An agent's own notification (chatNotify): from its own conversation, who sends it in the
    // title, a click opening the conversation or a page; cleaned, cut, limited, switched off.
    void agentNotifications()
    {
        auto ws = open({fake()});
        shell::NotificationCenter *center = ws->notifications();
        bool active = false;
        center->setAppActive([&active] { return active; });
        QDateTime now(QDate(2026, 11, 20), QTime(9, 15));
        ws->setClock([&now] { return now; });
        ws->write(u"AI/2026-11-20 Budowanie.md"_s, u"---\npartner: acp:fake\n---\n- 🧑 Daj znać, gdy skończysz\n"_s);
        ws->write(u"AI/2026-11-20 Inna.md"_s, u"---\npartner: acp:fake\n---\n- 🧑 Inna\n"_s);
        const QString path = u"AI/2026-11-20 Budowanie.md"_s;
        ChatControl control(ws.get());
        const ChatControl::Caller agent{true, path, u"Fałszywy"_s};
        QSignalSpy shown(center, &shell::NotificationCenter::shown);

        auto a = ask(
                control, u"chatNotify"_s,
                {{u"text"_s, u"  Zbudowane: 3 ostrzeżenia\r\n\n\n\n\nżółwie\x1b[0m  "_s}, {u"title"_s, u"Budowanie"_s}},
                agent);
        QVERIFY2(a->ok, qPrintable(a->message));
        QCOMPARE(a->result.value(u"conversation"_s).toString(), path);
        QCOMPARE(a->result.value(u"opens"_s).toString(), path);
        QCOMPARE(shown.size(), 1);
        QCOMPARE(shown.at(0).at(1).toString(), path);
        QCOMPARE(shown.at(0).at(2).toString(), u"Fałszywy: Budowanie"_s);
        QCOMPARE(shown.at(0).at(3).toString(), u"Zbudowane: 3 ostrzeżenia\n\nżółwie[0m"_s);
        QVERIFY(!shown.at(0).at(4).toBool());
        // No title: who sends it; a page opens instead of the conversation; long texts cut.
        active = true;
        a = ask(control, u"chatNotify"_s,
                {{u"text"_s, QString(u"ą"_s).repeated(1500)},
                 {u"title"_s, QString(u"ę"_s).repeated(200)},
                 {u"page"_s, u"Dom"_s}},
                agent);
        QVERIFY2(a->ok, qPrintable(a->message));
        QCOMPARE(a->result.value(u"opens"_s).toString(), u"Dom.md"_s);
        QCOMPARE(shown.last().at(1).toString(), u"Dom.md"_s);
        QCOMPARE(shown.last().at(3).toString().size(), shell::NotificationCenter::MaxText);
        QVERIFY(shown.last().at(3).toString().endsWith(u'…'));
        QCOMPARE(shown.last().at(2).toString(),
                 u"Fałszywy: "_s + QString(u"ę"_s).repeated(shell::NotificationCenter::MaxTitle - 1) + u'…');
        QVERIFY(shown.last().at(4).toBool()); // (the app in front: a toast)
        a = ask(control, u"chatNotify"_s, {{u"text"_s, u"Gotowe"_s}}, agent);
        QCOMPARE(shown.last().at(2).toString(), u"Fałszywy"_s);
        QCOMPARE(shown.size(), 3);
        // Its own conversation only; a page that is not there; no text.
        QCOMPARE(ask(control, u"chatNotify"_s, {{u"text"_s, u"x"_s}, {u"path"_s, u"Inna"_s}}, agent)->code,
                 u"refused"_s);
        QCOMPARE(ask(control, u"chatNotify"_s, {{u"text"_s, u"x"_s}, {u"page"_s, u"Nie ma"_s}}, agent)->code,
                 u"not_found"_s);
        QCOMPARE(ask(control, u"chatNotify"_s, {{u"text"_s, u" \n "_s}}, agent)->code, u"bad_params"_s);
        QCOMPARE(ask(control, u"chatNotify"_s, {{u"text"_s, u"x"_s}}, ChatControl::Caller{true, QString(), u"x"_s})
                         ->code,
                 u"refused"_s);
        QCOMPARE(shown.size(), 3);
        // At most ten of a conversation an hour: then refused with when to try again.
        for (int i = shown.size(); i < shell::NotificationCenter::MaxPerHour; ++i)
            QVERIFY(ask(control, u"chatNotify"_s, {{u"text"_s, u"Krok %1"_s.arg(i)}}, agent)->ok);
        now = now.addSecs(600);
        a = ask(control, u"chatNotify"_s, {{u"text"_s, u"Jedenasty"_s}}, agent);
        QCOMPARE(a->code, u"refused"_s);
        QCOMPARE(a->details.value(u"reason"_s).toString(), u"limit"_s);
        QCOMPARE(a->details.value(u"retryAfterMs"_s).toInteger(), 50 * 60 * 1000);
        QCOMPARE(shown.size(), shell::NotificationCenter::MaxPerHour);
        // (Another conversation has its own; a script's without one too — titled with its name.)
        const ChatControl::Caller other{true, u"AI/2026-11-20 Inna.md"_s, u"Fałszywy"_s};
        QVERIFY(ask(control, u"chatNotify"_s, {{u"text"_s, u"Z innej"_s}}, other)->ok);
        a = ask(control, u"chatNotify"_s, {{u"text"_s, u"Kopia gotowa"_s}},
                ChatControl::Caller{false, QString(), u"kopia.sh"_s});
        QVERIFY2(a->ok, qPrintable(a->message));
        QVERIFY(!a->result.contains(u"conversation"_s));
        QCOMPARE(shown.last().at(1).toString(), QString());
        QCOMPARE(shown.last().at(2).toString(), u"kopia.sh"_s);
        // A script may name the conversation (its partner sends it).
        a = ask(control, u"chatNotify"_s, {{u"text"_s, u"x"_s}, {u"path"_s, u"Inna"_s}}, ChatControl::Caller{});
        QVERIFY2(a->ok, qPrintable(a->message));
        QCOMPARE(shown.last().at(2).toString(), u"Fałszywy"_s);
        // An hour after the first: it goes again.
        now = now.addSecs(3000);
        QVERIFY(ask(control, u"chatNotify"_s, {{u"text"_s, u"Po godzinie"_s}}, agent)->ok);
        // Switched off by the user: refused, so the agent says it in its answer.
        center->setAllowed(shell::NotificationCenter::Kind::Agent, false);
        a = ask(control, u"chatNotify"_s, {{u"text"_s, u"Wyłączone"_s}}, other);
        QCOMPARE(a->code, u"refused"_s);
        QCOMPARE(a->details.value(u"reason"_s).toString(), u"off"_s);
        const qsizetype count = shown.size();
        QCOMPARE(
                center->post({shell::NotificationCenter::Kind::Agent, QString(), u"x"_s, QString(), u"y"_s, QString()}),
                u"off"_s);
        QCOMPARE(shown.size(), count);
        // (A script's still shows: only agents were turned off.)
        QVERIFY(ask(control, u"chatNotify"_s, {{u"text"_s, u"Skrypt"_s}}, ChatControl::Caller{false, QString(), u"s"_s})
                        ->ok);
        QCOMPARE(shown.last().at(0).toString(), u"script"_s);
    }

    // Code mode (docs/CODE.md): a thread of a project — its page names the project, no journal line,
    // its session in the project's folder with the app's tools but not the notebook's, its own
    // context; the project's files read and written there; a worktree that is gone fails the turn.
    void codeThreads()
    {
        if (!toolsThere())
            QSKIP("no brulion-cli to name");
        auto ws = open({fake(true)});
        WorkspaceChats *chats = ws->chats();
        // (The chat's grant: a thread's agent gets the app's chat tools, as any conversation's.)
        int grants = 0;
        chats->agents()->setChatGrants([&grants](const QString &) { return u"chat-%1"_s.arg(++grants); },
                                       [](const QString &) {});
        auto unset = qScopeGuard([chats] { chats->agents()->setChatGrants({}, {}); });
        shell::CodeProjects *projects = ws->projects();
        QByteArray kept;
        projects->setStorage([&kept] { return kept; }, [&kept](const QByteArray &json) { kept = json; });
        const QString folder = m_tmp.filePath(u"Projekt żółwia %1"_s.arg(m_n));
        QVERIFY(writeRaw(folder + u"/Dom.md"_s, "# README żółwia\n"));
        // (A git repository where git is: each turn of a thread gets a checkpoint of it.)
        const bool git = !QStandardPaths::findExecutable(u"git"_s).isEmpty();
        if (git) {
            for (const QStringList &args : {QStringList{u"init"_s, u"-q"_s}, QStringList{u"add"_s, u"-A"_s},
                                            QStringList{u"-c"_s, u"user.name=Ala"_s, u"-c"_s, u"user.email=a@b.c"_s,
                                                        u"commit"_s, u"-qm"_s, u"start"_s}})
                QCOMPARE(QProcess::execute(u"git"_s, QStringList{u"-C"_s, folder} + args), 0);
        }
        const QString id = projects->add(folder);
        QCOMPARE(id, u"projekt-zolwia-%1"_s.arg(m_n));
        QCOMPARE(projects->add(folder + u"/"_s), id); // (the same folder: the same project)
        QVERIFY(projects->add(m_tmp.filePath(u"nie ma"_s)).isEmpty());
        QVERIFY(kept.contains(id.toUtf8()));
        // Answered by an agent only; an unknown project starts nothing.
        QVERIFY(chats->startThread(u"brak"_s, u"x"_s).isEmpty());
        chats->setAgents({});
        QVERIFY(chats->startThread(id, u"Hej"_s).isEmpty());
        QVERIFY2(chats->turn(QString())->failure().contains(u"AI agent"_s),
                 qPrintable(chats->turn(QString())->failure()));
        chats->setAgents({fake(true)});
        QCOMPARE(chats->threadPartner(), u"acp:fake"_s);
        QCOMPARE(chats->agentPartners().size(), 1);

        const QString path = chats->startThread(id, u"Wyjaśnij projekt"_s);
        QVERIFY(!path.isEmpty());
        QTRY_VERIFY_WITH_TIMEOUT(!chats->turn(path)->running(), 20000);
        QVERIFY2(chats->turn(path)->failure().isEmpty(), qPrintable(chats->turn(path)->failure()));
        if (git) {
            // Its checkpoint: a git snapshot before the turn, for the Changes panel.
            QCOMPARE(chats->checkpoints(path).size(), 1);
            QCOMPARE(chats->checkpoints(path).first().toMap().value(u"message"_s).toInt(), 1);
            const QString checkpoint = chats->lastCheckpoint(path);
            QCOMPARE(QProcess::execute(u"git"_s, {u"-C"_s, folder, u"rev-parse"_s, u"-q"_s, u"--verify"_s,
                                                  u"refs/brulion/checkpoints/"_s + checkpoint}),
                     0);
            // (… and its page names it, for the notebook's other devices.)
            QVERIFY2(ws->read(path).contains(u"checkpoint: "_s + checkpoint), qPrintable(ws->read(path)));
            // A snapshot where the turn ended too: what the user changes after it is no turn's.
            QTRY_VERIFY_WITH_TIMEOUT(!chats->checkpoints(path).first().toMap().value(u"end"_s).toString().isEmpty(), 20000);
            const QString end = chats->checkpoints(path).first().toMap().value(u"end"_s).toString();
            QVERIFY(writeRaw(folder + u"/Dom.md"_s, "# README żółwia, poprawione\n"));
            QProcess diff;
            diff.start(u"git"_s, {u"-C"_s, folder, u"diff"_s, u"--name-only"_s, u"refs/brulion/checkpoints/"_s + checkpoint,
                                  u"refs/brulion/checkpoints/"_s + end});
            QVERIFY(diff.waitForFinished(30000));
            QCOMPARE(diff.readAllStandardOutput(), QByteArray());
            QVERIFY(writeRaw(folder + u"/Dom.md"_s, "# README żółwia\n"));
        }
        const QString page = ws->read(path);
        QVERIFY2(page.contains(u"project: %1"_s.arg(id)) && page.contains(u"partner: acp:fake"_s), qPrintable(page));
        QVERIFY(!ws->read(ws->journalFileFor(u"2026-11-20"_s)).contains(path.chopped(3)));
        // Its session: the project's folder; Brulion's tools without the notebook's.
        const QJsonObject params = logged(u"session/new"_s).last().value(u"params"_s).toObject();
        QCOMPARE(QDir::cleanPath(params.value(u"cwd"_s).toString()), QDir::cleanPath(folder));
        const QStringList args = serverArgs(logged(u"session/new"_s).size() - 1);
        QVERIFY2(args.contains(u"--no-notes"_s) && !args.contains(u"--vault"_s), qPrintable(args.join(u' ')));
        QVERIFY(args.contains(u"--app-chat"_s));
        const QString context =
                promptOf(logged(u"session/prompt"_s).last()).at(0).toObject().value(u"text"_s).toString();
        QVERIFY2(context.contains(u"software project"_s) && context.contains(QDir::cleanPath(folder)),
                 qPrintable(context));
        QVERIFY(!context.contains(u"notebook is the folder"_s));
        QVERIFY(context.contains(u"brulion_app_chat_notify"_s));
        // Where it works.
        QVariantMap place = chats->threadPlace(path);
        QCOMPARE(place.value(u"project"_s).toString(), id);
        QCOMPARE(place.value(u"projectName"_s).toString(), u"Projekt żółwia %1"_s.arg(m_n));
        QVERIFY(place.value(u"exists"_s).toBool());
        QCOMPARE(chats->threadFolder(path), QDir::cleanPath(folder));
        QVERIFY(chats->threadPlace(u"Dom.md"_s).isEmpty());
        // The threads of a folder (a deleted thread's worktree: another one uses it?); unread again.
        QCOMPARE(chats->threadsIn(folder + u"/"_s), QStringList{path});
        QVERIFY(chats->threadsIn(m_tmp.filePath(u"inny"_s)).isEmpty());
        QVERIFY(!chats->runningIn(folder));
        chats->showing(path, true); // (read: a view shows it)
        QVERIFY(!chats->isUnread(path));
        chats->showing(path, false);
        chats->markUnread(path);
        QVERIFY(chats->isUnread(path));
        chats->markUnread(u"Dom.md"_s);
        QVERIFY(!chats->isUnread(u"Dom.md"_s));
        // A thread on a pull request's branch: its page and the agent's first context name it.
        const QString onPr = chats->startThread(id, u"Popraw testy"_s, {}, {}, {}, {}, u"#7 Popraw \"kalkulator\""_s);
        QVERIFY(!onPr.isEmpty());
        QVERIFY(chats->runningIn(folder));
        QTRY_VERIFY_WITH_TIMEOUT(!chats->turn(onPr)->running(), 20000);
        QVERIFY2(ws->read(onPr).contains(u"pr: \"#7 Popraw 'kalkulator'\""_s), qPrintable(ws->read(onPr)));
        const QString prContext =
                promptOf(logged(u"session/prompt"_s).last()).at(0).toObject().value(u"text"_s).toString();
        QVERIFY2(prContext.contains(u"pull request #7 Popraw 'kalkulator'"_s), qPrintable(prContext));
        QCOMPARE(chats->threadsIn(folder).size(), 2);
        // The project's files, through the client: read and written in its folder.
        QCOMPARE(chats->send(path, u"czytaj"_s), path);
        QTRY_VERIFY_WITH_TIMEOUT(!chats->turn(path)->running(), 20000);
        QVERIFY2(ws->read(path).contains(u"Przeczytano: # README żółwia"_s), qPrintable(ws->read(path)));
        QCOMPARE(chats->send(path, u"zapisz"_s), path);
        QTRY_VERIFY_WITH_TIMEOUT(!chats->turn(path)->running(), 20000);
        QFile written(folder + u"/Od agenta.md"_s);
        QVERIFY(written.open(QIODevice::ReadOnly));
        QCOMPARE(written.readAll(), QByteArray("- od agenta\n"));
        QVERIFY(!ws->exists(u"Od agenta.md"_s)); // (not in the notebook)
        if (git) {
            // "Edit from here" with the files back: the checkpoint before that message's turn.
            written.close();
            QVERIFY(chats->checkpointOf(path, 2).isEmpty()); // (an answer has none)
            const QString before = chats->checkpointOf(path, 5);
            QVERIFY(!before.isEmpty());
            QVERIFY(!chats->restoreCheckpoint(path, u"nie-ma/../x"_s));
            QSignalSpy restored(chats, &WorkspaceChats::checkpointRestored);
            QVERIFY(chats->restoreCheckpoint(path, before));
            QTRY_COMPARE_WITH_TIMEOUT(restored.size(), 1, 20000);
            QVERIFY2(restored.first().at(1).toBool(), qPrintable(restored.first().at(2).toString()));
            QVERIFY(!QFile::exists(folder + u"/Od agenta.md"_s));
            QVERIFY(QFile::exists(folder + u"/Dom.md"_s));
        }
        // Its files named with "@" (docs/CODE-FILES.md): each one there also goes as a link to it —
        // not a folder, not one that is not there, not one outside the folder.
        QVERIFY(QDir(folder).mkpath(u"src"_s));
        QCOMPARE(chats->send(path, u"Popraw @Dom.md, @src/ i @brak.md oraz @../poza.md"_s), path);
        QTRY_VERIFY_WITH_TIMEOUT(!chats->turn(path)->running(), 20000);
        const QJsonArray mentioned = promptOf(logged(u"session/prompt"_s).last());
        QStringList links;
        for (const QJsonValue &block : mentioned) {
            if (block.toObject().value(u"type"_s).toString() == u"resource_link"_s)
                links << block.toObject().value(u"uri"_s).toString();
        }
        QCOMPARE(links, QStringList{QUrl::fromLocalFile(QDir(folder).filePath(u"Dom.md"_s)).toString(QUrl::FullyEncoded)});
        QCOMPARE(mentioned.first().toObject().value(u"text"_s).toString(),
                 u"Popraw @Dom.md, @src/ i @brak.md oraz @../poza.md"_s);
        // A worktree of its own: set, and gone — the turn says so.
        QVERIFY(chats->setThreadWorktree(path, m_tmp.filePath(u"drzewo"_s), u"brulion/drzewo"_s));
        place = chats->threadPlace(path);
        QCOMPARE(place.value(u"branch"_s).toString(), u"brulion/drzewo"_s);
        QVERIFY(!place.value(u"exists"_s).toBool());
        QCOMPARE(chats->send(path, u"Dalej"_s), path);
        QTRY_VERIFY_WITH_TIMEOUT(!chats->turn(path)->running(), 20000);
        QVERIFY2(chats->turn(path)->failure().contains(u"not there any more"_s),
                 qPrintable(chats->turn(path)->failure()));
        QVERIFY(chats->setThreadWorktree(path, QString(), QString()));
        QVERIFY(!ws->read(path).contains(u"worktree:"_s));
        // An agent's task in a project (chatStart's project): a thread, a sub-conversation of its own.
        ChatControl control(ws.get());
        auto a = ask(control, u"chatPartners"_s, {});
        QCOMPARE(a->result.value(u"projects"_s).toArray().at(0).toObject().value(u"id"_s).toString(), id);
        a = ask(control, u"chatStart"_s,
                {{u"text"_s, u"Przejrzyj kod"_s}, {u"partner"_s, u"acp:fake"_s}, {u"project"_s, id}},
                ChatControl::Caller{true, path, u"Fałszywy"_s});
        QVERIFY2(a->ok, qPrintable(a->message));
        QCOMPARE(a->result.value(u"project"_s).toString(), id);
        QCOMPARE(a->result.value(u"parent"_s).toString(), path);
        QTRY_VERIFY_WITH_TIMEOUT(!chats->isRunning(a->result.value(u"path"_s).toString()), 20000);
        QCOMPARE(ask(control, u"chatStart"_s, {{u"text"_s, u"x"_s}, {u"project"_s, u"brak"_s}})->code, u"not_found"_s);
        QCOMPARE(ask(control, u"chatStart"_s, {{u"text"_s, u"x"_s}, {u"partner"_s, u""_s}, {u"project"_s, id}})->code,
                 u"bad_params"_s);
        // The project gone from this device: its threads say so.
        projects->remove(id);
        QVERIFY(!chats->threadPlace(path).value(u"known"_s).toBool());
        QCOMPARE(chats->send(path, u"Jeszcze"_s), path);
        QTRY_VERIFY_WITH_TIMEOUT(!chats->turn(path)->running(), 20000);
        QVERIFY2(chats->turn(path)->failure().contains(u"not on this device"_s),
                 qPrintable(chats->turn(path)->failure()));
    }

    // The projects of Code mode: ids, names, scripts, worktrees, what is kept.
    void codeProjects()
    {
        QCOMPARE(shell::CodeProjects::slug(u"Mój Projekt (2)!"_s), u"moj-projekt-2"_s);
        QCOMPARE(shell::CodeProjects::slug(u"Łódź — żółw"_s), u"lodz-zolw"_s);
        QCOMPARE(shell::CodeProjects::slug(u"…"_s), u"project"_s);
        QCOMPARE(shell::CodeProjects::splitCommand(u"code --new-window \"a b\" c\\ d"_s),
                 (QStringList{u"code"_s, u"--new-window"_s, u"a b"_s, u"c d"_s}));
        // Windows' rules: backslashes are a path's, quotes group, \" is a quote.
        QCOMPARE(shell::CodeProjects::splitWindowsCommand(
                         u"\"C:\\Program Files\\Microsoft VS Code\\Code.exe\" -n C:\\Użytkownicy\\Łucja\\"_s),
                 (QStringList{u"C:\\Program Files\\Microsoft VS Code\\Code.exe"_s, u"-n"_s,
                              u"C:\\Użytkownicy\\Łucja\\"_s}));
        QCOMPARE(shell::CodeProjects::splitWindowsCommand(u"zed  \"a \\\"b\\\" c\" \"d\"\"e\" \"\" f\\\\\"g h\""_s),
                 (QStringList{u"zed"_s, u"a \"b\" c"_s, u"d\"e"_s, QString(), u"f\\g h"_s}));
        QCOMPARE(shell::CodeProjects::splitWindowsCommand(u"  "_s), QStringList());
        // A batch file (code.cmd) through cmd.exe: everything quoted, a trailing backslash doubled,
        // what cmd.exe would read inside quotes refused.
        QCOMPARE(shell::CodeProjects::batchCommandLine(u"C:\\VS Code\\bin\\code.cmd"_s,
                                                       {u"-n"_s, u"C:\\Projekty\\R&D (żółw)"_s, u"D:\\"_s}),
                 u"/d /v:off /s /c \"\"C:\\VS Code\\bin\\code.cmd\" \"-n\" \"C:\\Projekty\\R&D (żółw)\" \"D:\\\\\"\""_s);
        QCOMPARE(shell::CodeProjects::batchCommandLine(u"code.cmd"_s, {u"C:\\100%PATH%"_s}), QString());
        QCOMPARE(shell::CodeProjects::batchCommandLine(u"code.cmd"_s, {u"a\"b"_s}), QString());
        QCOMPARE(shell::CodeProjects::batchCommandLine(u"code.cmd"_s, {u"a\nb"_s}), QString());

        QByteArray kept;
        {
            shell::CodeProjects projects;
            projects.setStorage([&kept] { return kept; }, [&kept](const QByteArray &json) { kept = json; });
            const QString a = m_tmp.filePath(u"proj/Ala"_s);
            const QString b = m_tmp.filePath(u"proj2/Ala"_s);
            QVERIFY(QDir().mkpath(a) && QDir().mkpath(b));
            QCOMPARE(projects.add(QUrl::fromLocalFile(a).toString()), u"ala"_s);
            QCOMPARE(projects.add(b), u"ala-2"_s);
            projects.rename(u"ala-2"_s, u"  Druga   Ala "_s);
            QCOMPARE(projects.nameOf(u"ala-2"_s), u"Druga Ala"_s);
            projects.move(u"ala-2"_s, 0);
            QCOMPARE(projects.list().first().toMap().value(u"id"_s).toString(), u"ala-2"_s);
            projects.setScripts(u"ala"_s, {QVariantMap{{u"name"_s, u"dev"_s}, {u"command"_s, u"make run"_s}},
                                           QVariantMap{{u"name"_s, u"Testy żółwia"_s}, {u"command"_s, u"ctest"_s},
                                                       {u"icon"_s, u"test"_s}, {u"shortcut"_s, u"Ctrl+Alt+T"_s}}});
            QCOMPARE(projects.scripts(u"ala"_s).size(), 2);
            const QVariantMap tree = projects.worktreeFor(u"ala"_s, u"Napraw błąd #12"_s);
            QVERIFY(tree.value(u"folder"_s).toString().endsWith(u"/ala/napraw-blad-12"_s));
            QCOMPARE(tree.value(u"branch"_s).toString(), u"brulion/napraw-blad-12"_s);
            projects.setEditorCommand(u" zed "_s);
            projects.setWorktreesRoot(m_tmp.filePath(u"drzewa/"_s));
        }
        shell::CodeProjects again;
        again.setStorage([&kept] { return kept; }, [](const QByteArray &) {});
        QCOMPARE(again.count(), 2);
        QCOMPARE(again.list().first().toMap().value(u"name"_s).toString(), u"Druga Ala"_s);
        QCOMPARE(again.editorCommand(), u"zed"_s);
        QCOMPARE(again.worktreesRoot(), QDir::cleanPath(m_tmp.filePath(u"drzewa"_s)));
        const QVariantList scripts = again.scripts(u"ala"_s);
        QCOMPARE(scripts.size(), 2);
        QCOMPARE(scripts.at(1).toMap().value(u"name"_s).toString(), u"Testy żółwia"_s);
        QCOMPARE(scripts.at(1).toMap().value(u"id"_s).toString(), u"testy-zolwia"_s);
        QCOMPARE(scripts.at(1).toMap().value(u"iconName"_s).toString(), u"circle-check"_s);
        QCOMPARE(scripts.at(1).toMap().value(u"shortcut"_s).toString(), u"Ctrl+Alt+T"_s);
        // Broken or doubled entries are left out.
        again.setStorage(
                [] {
                    return QByteArray(
                            R"({"projects":[{"id":"x","path":"/a"},{"id":"x","path":"/b"},{"name":"bez"},7]})");
                },
                [](const QByteArray &) {});
        QCOMPARE(again.count(), 1);

        // Open in editor: a folder, or a file (the terminal's Ctrl+click on a path) — a file only
        // through the editor's command, never the desktop's handler (it might run it).
        QVERIFY(!again.openInEditor(m_tmp.filePath(u"brak.cpp"_s)));
#ifdef Q_OS_UNIX
        const QString file = m_tmp.filePath(u"proj/Ala/zażółć.cpp"_s);
        {
            QFile f(file);
            QVERIFY(f.open(QIODevice::WriteOnly));
        }
        const QString log = m_tmp.filePath(u"editor.log"_s);
        const QString script = m_tmp.filePath(u"editor.sh"_s);
        {
            QFile f(script);
            QVERIFY(f.open(QIODevice::WriteOnly));
            f.write(QByteArray("#!/bin/sh\nprintf '%s|%s' \"$PWD\" \"$1\" > '") + QFile::encodeName(log) + "'\n");
        }
        QFile::setPermissions(script, QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner);
        again.setEditorCommand(script);
        QVERIFY(again.openInEditor(file));
        QTRY_VERIFY(QFile::exists(log) && QFileInfo(log).size() > 0);
        {
            QFile f(log);
            QVERIFY(f.open(QIODevice::ReadOnly));
            QCOMPARE(QString::fromUtf8(f.readAll()), QFileInfo(file).absolutePath() + u'|' + file);
        }
        again.setEditorCommand(QString());
        // (Without an editor a file goes to the file manager as its folder — CodeProjects::openWith —,
        // never to the desktop's handler of the file, which might run it: not started here.)
#endif
    }

    // Who answers a new conversation: the device's default while it answers here.
    void defaultPartner()
    {
        auto ws = open({fake()});
        WorkspaceChats *chats = ws->chats();
        QSignalSpy changed(chats, &WorkspaceChats::defaultPartnerChanged);
        QCOMPARE(chats->defaultPartner(), QString());
        chats->setDefaultPartner(u" acp:fake "_s);
        QCOMPARE(chats->defaultPartner(), u"acp:fake"_s);
        QCOMPARE(changed.size(), 1);
        chats->setDefaultPartner(u"acp:fake"_s);
        QCOMPARE(changed.size(), 1);
        ChatControl control(ws.get());
        auto a = ask(control, u"chatPartners"_s, {});
        QCOMPARE(a->result.value(u"default"_s).toString(), u"acp:fake"_s);
        for (const QJsonValue &p : a->result.value(u"partners"_s).toArray())
            QCOMPARE(p.toObject().value(u"default"_s).toBool(), p.toObject().value(u"id"_s).toString() == u"acp:fake");
        // A conversation started without a partner: the default answers it; "" names the assistant.
        a = ask(control, u"chatStart"_s, {{u"text"_s, u"Kto odpowiada?"_s}});
        QVERIFY2(a->ok, qPrintable(a->message));
        QCOMPARE(a->result.value(u"partner"_s).toString(), u"acp:fake"_s);
        const QString path = a->result.value(u"path"_s).toString();
        QTRY_VERIFY_WITH_TIMEOUT(!chats->isRunning(path), 20000);
        QCOMPARE(chats->partnerOf(path), u"acp:fake"_s);
        // A scheduled conversation set up without one too.
        QByteArray kept;
        chats->schedules()->setStorage([&kept] { return kept; }, [&kept](const QByteArray &json) { kept = json; });
        a = ask(control, u"chatScheduleAdd"_s, {{u"prompt"_s, u"Plan dnia"_s}, {u"when"_s, u"codziennie o 7:30"_s}});
        QVERIFY2(a->ok, qPrintable(a->message));
        QCOMPARE(a->result.value(u"partner"_s).toString(), u"acp:fake"_s);
        a = ask(control, u"chatScheduleAdd"_s,
                {{u"prompt"_s, u"Asystent"_s}, {u"when"_s, u"codziennie o 8"_s}, {u"partner"_s, u""_s}});
        QVERIFY2(a->ok, qPrintable(a->message));
        QCOMPARE(a->result.value(u"partner"_s).toString(), QString());
        // Not set up here any more: the assistant, until it is again.
        chats->setAgents({});
        QCOMPARE(chats->defaultPartner(), QString());
        QVERIFY(changed.size() >= 2);
        chats->setAgents({fake()});
        QCOMPARE(chats->defaultPartner(), u"acp:fake"_s);
        chats->setDefaultPartner(QString());
        QCOMPARE(chats->defaultPartner(), QString());

        // The device's settings keep the choices.
        shell::AppSettings settings(m_tmp.filePath(u"default-partner.ini"_s), m_tmp.filePath(u"data"_s));
        QCOMPARE(settings.aiDefaultPartner(), QString());
        QVERIFY(settings.aiAgentNotify());
        QSignalSpy partnerChanged(&settings, &shell::AppSettings::aiDefaultPartnerChanged);
        settings.setAiDefaultPartner(u" acp:claude "_s);
        QCOMPARE(settings.aiDefaultPartner(), u"acp:claude"_s);
        settings.setAiDefaultPartner(u"acp:claude"_s);
        QCOMPARE(partnerChanged.size(), 1);
        settings.setAiAgentNotify(false);
        QVERIFY(!settings.aiAgentNotify());
    }

    void schedules()
    {
        auto ws = open({fake()});
        WorkspaceChats *chats = ws->chats();
        ChatSchedules *schedules = chats->schedules();
        QDateTime now(QDate(2026, 11, 20), QTime(9, 15));
        schedules->setClock([&now] { return now; });
        QByteArray kept;
        QVERIFY(!schedules->available());
        QString error;
        QVERIFY(schedules->add({}, QString(), QString(), &error).isEmpty());
        QVERIFY(!error.isEmpty());
        schedules->setStorage([&kept] { return kept; }, [&kept](const QByteArray &json) { kept = json; });
        QVERIFY(schedules->available());
        // Checked in words; refused with why.
        QVariantMap checked = schedules->check(u"codziennie o 7:30"_s);
        QVERIFY(checked.value(u"ok"_s).toBool());
        QCOMPARE(checked.value(u"text"_s).toString(), u"codziennie o 7:30"_s);
        QCOMPARE(checked.value(u"next"_s).toString(), u"jutro 07:30"_s);
        checked = schedules->check(u"co 5 minut"_s);
        QVERIFY(!checked.value(u"ok"_s).toBool());
        QVERIFY(checked.value(u"error"_s).toString().contains(u"15"_s));
        QVERIFY(schedules->add(u"x"_s, u""_s, u"acp:fake"_s, u"codziennie"_s, false, true).isEmpty());
        QVERIFY(!schedules->lastError().isEmpty());

        // A new conversation each morning.
        const QString id = schedules->add(u"Plan dnia"_s, u"Co mam dziś do zrobienia?"_s, u"acp:fake"_s,
                                          u"codziennie o 7:30"_s, false, true);
        QVERIFY2(!id.isEmpty(), qPrintable(schedules->lastError()));
        QVERIFY(kept.contains("Plan dnia"));
        QCOMPARE(schedule::parseSchedules(kept).size(), 1);
        schedules->runDue();
        QCOMPARE(chats->runningCount(), 0); // (not yet)
        QSignalSpy ran(schedules, &ChatSchedules::ran);
        now = QDateTime(QDate(2026, 11, 21), QTime(7, 30, 20));
        schedules->runDue();
        QCOMPARE(ran.size(), 1);
        const QString first = ran.first().at(2).toString();
        QVERIFY(first.startsWith(u"AI/"_s));
        QTRY_VERIFY_WITH_TIMEOUT(!chats->isRunning(first), 20000);
        schedules->runDue();
        QCOMPARE(ran.size(), 1); // (once)
        // The app closed for three days: one run at the next start, not three.
        now = QDateTime(QDate(2026, 11, 24), QTime(12, 0));
        schedules->setStorage([&kept] { return kept; }, [&kept](const QByteArray &json) { kept = json; });
        QCOMPARE(ran.size(), 2);
        QVERIFY(ran.last().at(2).toString() != first);
        QTRY_VERIFY_WITH_TIMEOUT(chats->runningCount() == 0, 20000);
        schedules->runDue();
        QCOMPARE(ran.size(), 2);
        QCOMPARE(schedules->find(id)->next()->toString(), u"2026-11-25 07:30"_s);

        // One conversation going on: the next run is its next message; skipped while it answers.
        QVERIFY(schedules->update(id, u"Plan dnia"_s, u"czekaj na plan"_s, u"acp:fake"_s, u"co godzinę"_s, true, true));
        const QString one = schedules->runNow(id);
        QVERIFY(!one.isEmpty());
        QVERIFY(chats->isRunning(one));
        QCOMPARE(schedules->find(id)->conversation, one);
        QVERIFY(schedules->runNow(id).isEmpty());
        QVERIFY(schedules->find(id)->lastError.contains(u"still answering"_s));
        chats->stop(one);
        QTRY_VERIFY_WITH_TIMEOUT(!chats->isRunning(one), 10000);
        QCOMPARE(schedules->runNow(id), one);
        chats->stop(one);
        QTRY_VERIFY_WITH_TIMEOUT(!chats->isRunning(one), 10000);
        // A partner that does not answer here: skipped, with why.
        const QString away = schedules->add(u"Na telefonie"_s, u"Ą"_s, u"acp:nieznany"_s, u"co godzinę"_s, false, true);
        QVERIFY(!away.isEmpty());
        QVERIFY(schedules->runNow(away).isEmpty());
        QVERIFY(schedules->find(away)->lastError.contains(u"does not answer"_s));
        // Off: never due.
        QVERIFY(schedules->setEnabled(away, false));
        now = now.addDays(1);
        const qsizetype before = ran.size();
        schedules->runDue();
        QCOMPARE(ran.size(), before + 1); // (only the other one: its conversation goes on)
        QCOMPARE(ran.last().at(2).toString(), one);
        chats->stopAll();
        QTRY_VERIFY_WITH_TIMEOUT(chats->runningCount() == 0, 20000);

        // Through the remote control: an agent's schedule runs as a sub-conversation of its own.
        ChatControl control(ws.get());
        const ChatControl::Caller agent{true, first, u"Fałszywy"_s};
        auto a = ask(control, u"chatScheduleAdd"_s,
                     {{u"prompt"_s, u"Sprawdź pogodę"_s}, {u"when"_s, u"jutro o 8"_s}, {u"partner"_s, u"acp:fake"_s}},
                     agent);
        QVERIFY2(a->ok, qPrintable(a->message));
        const QString agentId = a->result.value(u"id"_s).toString();
        QCOMPARE(a->result.value(u"by"_s).toString(), u"Fałszywy"_s);
        QCOMPARE(a->result.value(u"parent"_s).toString(), first);
        QCOMPARE(a->result.value(u"destination"_s).toString(), u"new"_s);
        a = ask(control, u"chatScheduleRun"_s, {{u"id"_s, agentId}}, agent);
        QVERIFY2(a->ok, qPrintable(a->message));
        const QString sub = a->result.value(u"path"_s).toString();
        QCOMPARE(chats->lineage()->parentOf(sub), first);
        // It changes only its own; the user's are not its to change or delete.
        QCOMPARE(ask(control, u"chatScheduleRemove"_s, {{u"id"_s, id}}, agent)->code, u"refused"_s);
        QCOMPARE(ask(control, u"chatScheduleSet"_s, {{u"id"_s, id}, {u"enabled"_s, false}}, agent)->code, u"refused"_s);
        QCOMPARE(ask(control, u"chatScheduleAdd"_s, {{u"prompt"_s, u"Ą"_s}, {u"when"_s, u"kiedyś"_s}}, agent)->code,
                 u"bad_params"_s);
        a = ask(control, u"chatSchedules"_s, {});
        QCOMPARE(a->result.value(u"schedules"_s).toArray().size(), 3);
        QVERIFY(ask(control, u"chatScheduleRemove"_s, {{u"id"_s, agentId}}, agent)->ok);
        QVERIFY(ask(control, u"chatScheduleRemove"_s, {{u"id"_s, away}})->ok); // (a script: any)
        QCOMPARE(schedule::parseSchedules(kept).size(), 1);
        QTRY_VERIFY_WITH_TIMEOUT(chats->runningCount() == 0, 20000);
        // No notebook of the user's: none kept, none run.
        schedules->setStorage({}, {});
        QVERIFY(schedules->rows().isEmpty());
        QCOMPARE(ask(control, u"chatScheduleAdd"_s, {{u"prompt"_s, u"Ą"_s}, {u"when"_s, u"jutro"_s}})->code, u"not_ready"_s);
    }

    void secrets()
    {
        auto ws = open({fake()});
        WorkspaceChats *chats = ws->chats();
        ChatSecrets *secrets = chats->secrets();
        secrets->setBase(m_tmp.filePath(u"secrets-%1"_s.arg(m_n)));
        ChatControl control(ws.get());
        // A conversation whose agent waits (its turn runs until it is stopped).
        auto a = ask(control, u"chatStart"_s, {{u"text"_s, u"czekaj na sekret"_s}, {u"partner"_s, u"acp:fake"_s}});
        QVERIFY2(a->ok, qPrintable(a->message));
        const QString path = a->result.value(u"path"_s).toString();
        QTRY_VERIFY_WITH_TIMEOUT(chats->isRunning(path), 20000);
        const ChatControl::Caller agent{true, path, u"Fałszywy"_s};
        // Its own conversation only; an address that is no http(s) page; a label.
        QCOMPARE(ask(control, u"chatRequestSecret"_s, {{u"label"_s, u"x"_s}, {u"path"_s, u"AI/inna"_s}}, agent)->code,
                 u"refused"_s);
        QCOMPARE(ask(control, u"chatRequestSecret"_s, {{u"label"_s, u"Token"_s}, {u"url"_s, u"javascript:alert(1)"_s}},
                     agent)
                         ->code,
                 u"bad_params"_s);
        QCOMPARE(ask(control, u"chatRequestSecret"_s, {{u"label"_s, u"  "_s}}, agent)->code, u"bad_params"_s);
        QVERIFY(!secrets->pending(path));

        // The card: who asks, the label and the reason as written, the page's host; the
        // conversation asks meanwhile, and a toast says so (no chat view shows it).
        QSignalSpy waiting(chats, &WorkspaceChats::questionWaiting);
        a = ask(control, u"chatRequestSecret"_s,
                {{u"label"_s, u"Token GitHuba"_s},
                 {u"reason"_s, u"Żeby wypchnąć gałąź <b>teraz</b>"_s},
                 {u"placeholder"_s, u"ghp_…"_s},
                 {u"url"_s, u"https://github.com/settings/tokens"_s}},
                agent);
        QVERIFY(!a->done);
        QVERIFY(secrets->pending(path));
        QCOMPARE(chats->stateOf(path), u"asking"_s);
        QCOMPARE(chats->questionText(path), u"Fałszywy asks for a secret: Token GitHuba"_s);
        QCOMPARE(waiting.size(), 1);
        QVERIFY(!waiting.first().at(1).toString().isEmpty());
        const QVariantMap card = secrets->card(path);
        QCOMPARE(card.value(u"agent"_s).toString(), u"Fałszywy"_s);
        QCOMPARE(card.value(u"reason"_s).toString(), u"Żeby wypchnąć gałąź <b>teraz</b>"_s); // (shown as plain text)
        QCOMPARE(card.value(u"placeholder"_s).toString(), u"ghp_…"_s);
        QCOMPARE(card.value(u"host"_s).toString(), u"github.com"_s);
        QVERIFY(!card.value(u"inline"_s).toBool());
        // A second request meanwhile: busy.
        QCOMPARE(ask(control, u"chatRequestSecret"_s, {{u"label"_s, u"Inny"_s}}, agent)->code, u"busy"_s);

        // Sent: an owner-only file holds it, the page does not, the log masks it.
        const QString value = u"ghp_Żółć-1234567890"_s;
        QVERIFY(!secrets->send(path, QString()));
        QVERIFY(secrets->send(path, value));
        QVERIFY(a->done && a->ok);
        QCOMPARE(a->result.value(u"delivery"_s).toString(), u"file"_s);
        const QString file = a->result.value(u"file"_s).toString();
        QFile f(file);
        QVERIFY(f.open(QIODevice::ReadOnly));
        QCOMPARE(f.readAll(), value.toUtf8());
        f.close();
#ifdef Q_OS_UNIX
        QCOMPARE(QFileInfo(file).permissions()
                         & (QFileDevice::ReadGroup | QFileDevice::WriteGroup | QFileDevice::ReadOther
                            | QFileDevice::WriteOther),
                 QFileDevice::Permissions());
#endif
        QVERIFY(a->result.value(u"note"_s).toString().contains(file));
        QVERIFY(!secrets->pending(path));
        QCOMPARE(chats->stateOf(path), u"running"_s);
        bool ok = false;
        QVERIFY(!ws->read(path, &ok).contains(value));
        QVERIFY(ok);
        QVERIFY(!logging::redact(u"echo "_s + value).contains(value));
        // The turn ends (stopped): the file goes, and with it the value's redaction.
        chats->stop(path);
        QTRY_VERIFY_WITH_TIMEOUT(!chats->isRunning(path), 20000);
        QVERIFY(!QFile::exists(file));
        QVERIFY(logging::redact(u"echo "_s + value).contains(value));

        // The next turn: Cancel — the agent hears it was not given.
        QVERIFY(ask(control, u"chatSend"_s, {{u"path"_s, path}, {u"text"_s, u"czekaj dalej"_s}})->ok);
        QTRY_VERIFY_WITH_TIMEOUT(chats->isRunning(path), 20000);
        a = ask(control, u"chatRequestSecret"_s, {{u"label"_s, u"PIN"_s}, {u"delivery"_s, u"value"_s}}, agent);
        QVERIFY(secrets->card(path).value(u"inline"_s).toBool());
        secrets->cancel(path);
        QVERIFY(a->done && !a->ok);
        QCOMPARE(a->code, u"refused"_s);
        QCOMPARE(a->details.value(u"reason"_s).toString(), u"cancelled"_s);
        QCOMPARE(chats->stateOf(path), u"running"_s);
        // The value itself, when asked so: masked in the log while it lives.
        a = ask(control, u"chatRequestSecret"_s, {{u"label"_s, u"PIN"_s}, {u"delivery"_s, u"value"_s}}, agent);
        QVERIFY(secrets->send(path, u"4321-żółw"_s));
        QVERIFY(a->ok);
        QCOMPARE(a->result.value(u"delivery"_s).toString(), u"value"_s);
        QCOMPARE(a->result.value(u"value"_s).toString(), u"4321-żółw"_s);
        QVERIFY(!a->result.contains(u"file"_s));
        QVERIFY(!logging::redact(u"PIN 4321-żółw"_s).contains(u"4321-żółw"_s));
        // Its time up: timeout.
        a = ask(control, u"chatRequestSecret"_s, {{u"label"_s, u"Hasło"_s}, {u"ms"_s, 1000}}, agent);
        QTRY_VERIFY_WITH_TIMEOUT(a->done, 10000);
        QCOMPARE(a->code, u"timeout"_s);
        QVERIFY(!secrets->pending(path));
        // Stop while a card waits: refused, "stopped"; the value handed over goes with the turn.
        a = ask(control, u"chatRequestSecret"_s, {{u"label"_s, u"Hasło"_s}}, agent);
        QVERIFY(secrets->pending(path));
        chats->stop(path);
        QVERIFY(a->done);
        QCOMPARE(a->code, u"refused"_s);
        QCOMPARE(a->details.value(u"reason"_s).toString(), u"stopped"_s);
        QVERIFY(!secrets->pending(path));
        QTRY_VERIFY_WITH_TIMEOUT(!chats->isRunning(path), 20000);
        QVERIFY(logging::redact(u"PIN 4321-żółw"_s).contains(u"4321-żółw"_s));
        // An agent whose turn does not run asks for nothing.
        QCOMPARE(ask(control, u"chatRequestSecret"_s, {{u"label"_s, u"x"_s}}, agent)->code, u"refused"_s);
        // A script of the user's names the conversation.
        a = ask(control, u"chatRequestSecret"_s, {{u"label"_s, u"Klucz API"_s}, {u"path"_s, path}});
        QVERIFY(!a->done);
        QVERIFY(secrets->pending(path));
        QVERIFY(secrets->questionText(path).startsWith(u"An AI agent asks for a secret"_s));
        QCOMPARE(ask(control, u"chatRequestSecret"_s, {{u"label"_s, u"x"_s}, {u"path"_s, u"AI/brak"_s}})->code,
                 u"not_found"_s);
        // The notebook closed: every card goes.
        chats->reset();
        QVERIFY(a->done);
        QCOMPARE(a->code, u"refused"_s);
        QVERIFY(!secrets->pending(path));
    }

    void webhooks()
    {
        auto ws = open({fake()});
        WorkspaceChats *chats = ws->chats();
        ChatSchedules *schedules = chats->schedules();
        QDateTime now(QDate(2026, 11, 20), QTime(9, 15));
        schedules->setClock([&now] { return now; });
        QByteArray kept;
        schedules->setStorage([&kept] { return kept; }, [&kept](const QByteArray &json) { kept = json; });
        // A rule left empty needs a webhook; with one, only the webhook starts it.
        QVERIFY(schedules
                        ->save(QString(), u"Dzwonek"_s, u"Ktoś dzwoni"_s, u"acp:fake"_s, QString(), false, true,
                               QString())
                        .isEmpty());
        QVERIFY(schedules
                        ->save(QString(), u"Dzwonek"_s, u"Ktoś dzwoni"_s, u"acp:fake"_s, QString(), false, true,
                               u"za-krótki"_s)
                        .isEmpty());
        const QString token = schedules->newHookToken();
        QVERIFY(webhook::isToken(token));
        QVERIFY(token != schedules->newHookToken());
        const QString id = schedules->save(QString(), u"Dzwonek"_s, u"Ktoś dzwoni do drzwi: sprawdź, kto to."_s,
                                           u"acp:fake"_s, QString(), false, true, token);
        QVERIFY2(!id.isEmpty(), qPrintable(schedules->lastError()));
        QCOMPARE(schedules->find(id)->when.kind, schedule::When::Kind::None);
        QCOMPARE(schedules->find(id)->hook, token);
        QVERIFY(kept.contains(token.toLatin1())); // (on this device, with the schedules)
        const QVariantMap row = schedules->rows().first().toMap();
        QVERIFY(row.value(u"webhook"_s).toBool());
        QCOMPARE(row.value(u"rule"_s).toString(), u"tylko przez webhook"_s);
        QCOMPARE(row.value(u"rule"_s).toString(), schedules->hookOnlyText());
        QCOMPARE(row.value(u"next"_s).toString(), QString());
        now = now.addDays(3);
        schedules->runDue();
        QCOMPARE(chats->runningCount(), 0);
        // Its address: the server's and the token; masked where it is shown, and in the log.
        QCOMPARE(schedules->hookUrl(token), QString(u"http://127.0.0.1:8791/hooks/"_s + token));
        QCOMPARE(schedules->maskedHookUrl(token), QString(u"http://127.0.0.1:8791/hooks/"_s + token.left(4) + u"…"_s));
        QVERIFY(!logging::redact(u"POST /x/"_s + token).contains(token));
        // The remote control's schedules: that there is one, never the token.
        ChatControl control(ws.get());
        auto a = ask(control, u"chatSchedules"_s, {});
        QVERIFY(a->ok);
        QVERIFY(!QJsonDocument(a->result).toJson().contains(token.toLatin1()));
        QVERIFY(a->result.value(u"schedules"_s).toArray().first().toObject().value(u"webhook"_s).toBool());
        // (Its fields take no token.)
        QVERIFY(ask(control, u"chatScheduleSet"_s, {{u"id"_s, id}, {u"hook"_s, webhook::newToken()}})->ok);
        QCOMPARE(schedules->find(id)->hook, token);

        // Unknown and malformed tokens.
        QCOMPARE(schedules->runHook({webhook::newToken(), {}}).status, u"unknown"_s);
        QCOMPARE(schedules->runHook({u"nie-token"_s, {}}).status, u"unknown"_s);
        // Its own: a run with the body as data in a fence, the conversation's name.
        QSignalSpy ran(schedules, &ChatSchedules::ran);
        webhook::Body body;
        body.kind = webhook::BodyKind::Json;
        body.text = u"{\n    \"kto\": \"listonosz\"\n}"_s;
        body.bytes = 20;
        ChatSchedules::HookOutcome out = schedules->runHook({token, body});
        QCOMPARE(out.status, u"started"_s);
        QCOMPARE(ran.size(), 1);
        const QString path = ran.first().at(2).toString();
        QCOMPARE(out.conversation, chats->nameOf(path));
        bool ok = false;
        const QString page = ws->read(path, &ok);
        QVERIFY(page.contains(u"Ktoś dzwoni do drzwi"_s));
        QVERIFY(page.contains(u"niezaufane dane"_s));
        QVERIFY(page.contains(u"```json"_s));
        QVERIFY(page.contains(u"listonosz"_s));
        QVERIFY(schedules->find(id)->lastHook.isValid());
        QVERIFY(!schedules->find(id)->lastRun.isValid()); // (its timed rule does not move)
        QTRY_VERIFY_WITH_TIMEOUT(!chats->isRunning(path), 20000);
        // At most one a minute.
        out = schedules->runHook({token, {}});
        QCOMPARE(out.status, u"busy"_s);
        QVERIFY(out.retryAfterMs > 0 && out.retryAfterMs <= webhook::MinIntervalMs);
        now = now.addSecs(61);
        QCOMPARE(schedules->runHook({token, {}}).status, u"started"_s);
        QTRY_VERIFY_WITH_TIMEOUT(chats->runningCount() == 0, 20000);
        // Off: as if there were none.
        QVERIFY(schedules->setEnabled(id, false));
        now = now.addSecs(61);
        QCOMPARE(schedules->runHook({token, {}}).status, u"unknown"_s);
        // One conversation going on: busy while it answers.
        QCOMPARE(schedules->save(id, u"Dzwonek"_s, u"czekaj na kamerę"_s, u"acp:fake"_s, QString(), true, true, token),
                 id);
        out = schedules->runHook({token, {}});
        QCOMPARE(out.status, u"started"_s);
        const QString one = schedules->find(id)->conversation;
        QVERIFY(chats->isRunning(one));
        now = now.addSecs(61);
        QCOMPARE(schedules->runHook({token, {}}).status, u"busy"_s);
        chats->stop(one);
        QTRY_VERIFY_WITH_TIMEOUT(!chats->isRunning(one), 20000);
        // A new address: the old one stops working.
        const QString renewed = schedules->newHookToken();
        QCOMPARE(schedules->save(id, u"Dzwonek"_s, u"Ktoś dzwoni"_s, u"acp:fake"_s, QString(), false, true, renewed),
                 id);
        now = now.addSecs(61);
        QCOMPARE(schedules->runHook({token, {}}).status, u"unknown"_s);
        QCOMPARE(schedules->runHook({renewed, {}}).status, u"started"_s);
        QTRY_VERIFY_WITH_TIMEOUT(chats->runningCount() == 0, 20000);
        // Without its webhook it needs a rule; with one the webhook may go.
        QVERIFY(schedules->save(id, u"Dzwonek"_s, u"Ktoś dzwoni"_s, u"acp:fake"_s, QString(), false, true, QString())
                        .isEmpty());
        QVERIFY(!schedules->lastError().isEmpty());
        QCOMPARE(schedules->save(id, u"Dzwonek"_s, u"Ktoś dzwoni"_s, u"acp:fake"_s, u"codziennie o 7:30"_s, false, true,
                                 QString()),
                 id);
        QVERIFY(schedules->find(id)->hook.isEmpty());
        QVERIFY(!kept.contains(renewed.toLatin1()));
        QCOMPARE(schedules->runHook({renewed, {}}).status, u"unknown"_s);
        // No notebook of the user's: no hook known.
        schedules->setStorage({}, {});
        QCOMPARE(schedules->runHook({renewed, {}}).status, u"unknown"_s);
    }

    void tools()
    {
        if (WorkspaceAgents::toolsProgram().isEmpty())
            QSKIP("no brulion-cli next to the test or on PATH");
        auto ws = open({fake(true)});
        WorkspaceChats *chats = ws->chats();
        const QString path = chats->send(QString(), u"Cześć"_s, QString(), {}, u"acp:fake"_s);
        QTRY_VERIFY_WITH_TIMEOUT(!chats->turn(path)->running(), 20000);
        const QJsonObject server = logged(u"session/new"_s).first().value(u"params"_s).toObject()
                                           .value(u"mcpServers"_s).toArray().at(0).toObject();
        QCOMPARE(server.value(u"name"_s).toString(), u"brulion"_s);
        QVERIFY(QFileInfo(server.value(u"command"_s).toString()).isAbsolute());
        QCOMPARE(server.value(u"args"_s).toArray().at(0).toString(), u"mcp"_s);
        QVERIFY(promptOf(logged(u"session/prompt"_s).first()).at(0).toObject().value(u"text"_s).toString().contains(u"MCP"_s));
        // The notebook's tools keep the app's own device state of it (the pages' history, the write
        // lock), named in their arguments: an agent may start them with an environment of its own
        // (Codex: HOME, PATH, LANG … alone), in which brulion-cli would find the default profile's.
        QStringList args;
        for (const QJsonValue &a : server.value(u"args"_s).toArray())
            args << a.toString();
        const qsizetype state = args.indexOf(u"--state-dir"_s);
        QVERIFY2(state > 0 && state + 1 < args.size(), qPrintable(args.join(u' ')));
        QCOMPARE(args.at(state + 1), ws->vault()->deviceStatePath());
        QCOMPARE(args.at(args.indexOf(u"--vault"_s) + 1), ws->vault()->rootPath());
    }

    void permission()
    {
        auto ws = open({fake()});
        WorkspaceChats *chats = ws->chats();
        QSignalSpy questions(chats, &WorkspaceChats::questionsChanged);
        QSignalSpy waiting(chats, &WorkspaceChats::questionWaiting);
        const QString path = chats->send(QString(), u"Daj mi pozwolenie"_s, QString(), {}, u"acp:fake"_s);
        shell::ChatTurn *turn = chats->turn(path);
        QTRY_VERIFY_WITH_TIMEOUT(chats->asking(path), 20000);
        QVERIFY(questions.size() > 0);
        // No chat view shows it: said where the user is.
        QCOMPARE(waiting.size(), 1);
        QCOMPARE(waiting.first().at(0).toString(), path);
        QCOMPARE(waiting.first().at(1).toString(), u"Fałszywy asks: Write Zgoda.md"_s);
        chats->showing(path, true);
        QCOMPARE(chats->questionText(path), u"Fałszywy asks: Write Zgoda.md\nZgoda.md\n- zgoda"_s);
        // The file's change, for the diff the conversation shows.
        const QVariantMap diff = chats->questionDiff(path);
        QCOMPARE(diff.value(u"path"_s).toString(), u"Zgoda.md"_s);
        QCOMPARE(diff.value(u"oldText"_s).toString(), u"- nic\n"_s);
        QCOMPARE(diff.value(u"newText"_s).toString(), u"- zgoda"_s);
        QCOMPARE(chats->questionOptions(path), (QStringList{u"Allow"_s, u"Reject"_s}));
        QVERIFY(turn->running());
        chats->answer(path, u"Allow"_s);
        QVERIFY(!chats->asking(path));
        QTRY_VERIFY_WITH_TIMEOUT(!turn->running(), 20000);
        QVERIFY2(ws->read(path).contains(u"- 🤖 Zapisano."_s), qPrintable(ws->read(path)));
        QTRY_VERIFY(QFile::exists(QDir(ws->rootPath()).filePath(u"Zgoda.md"_s)));
        // Rejected.
        QCOMPARE(chats->send(path, u"Jeszcze raz pozwolenie"_s), path);
        QTRY_VERIFY_WITH_TIMEOUT(chats->asking(path), 20000);
        chats->answer(path, u"Reject"_s);
        QTRY_VERIFY_WITH_TIMEOUT(!turn->running(), 20000);
        QVERIFY2(ws->read(path).endsWith(u"- 🤖 Nie zapisano.\n  czas:: 2026-11-20 09:15\n"_s), qPrintable(ws->read(path)));
        // Stopped while it asks: the question goes, the agent hears "cancelled".
        QCOMPARE(chats->send(path, u"I trzeci raz pozwolenie"_s), path);
        QTRY_VERIFY_WITH_TIMEOUT(chats->asking(path), 20000);
        const QString before = ws->read(path);
        chats->stop(path);
        QVERIFY(!chats->asking(path));
        QVERIFY(!turn->running());
        QTRY_VERIFY(logged(u"session/cancel"_s).size() == 1);
        QCOMPARE(ws->read(path), before);
    }

    /// Messages written while a turn runs wait for it (as T3 Code's queue): they go one by one
    /// when it answers; a stop holds them until Resume; Send now stops the turn and goes first;
    /// they are kept in the notebook's device state over a restart, held then.
    void queue()
    {
        auto ws = open({fake()});
        WorkspaceChats *chats = ws->chats();
        const QString path = chats->send(QString(), u"Proszę czekaj"_s, QString(), {}, u"acp:fake"_s);
        shell::ChatTurn *turn = chats->turn(path);
        QTRY_VERIFY_WITH_TIMEOUT(logged(u"session/prompt"_s).size() == 1, 20000);
        QVERIFY(turn->running());
        // (send() itself waits for no turn: it refuses.)
        QCOMPARE(chats->send(path, u"Nie teraz"_s), QString());
        QVERIFY(chats->queue(path, u"Po pierwsze: zażółć"_s));
        QVERIFY(chats->queue(path, u"Po drugie: gęślą jaźń"_s));
        QCOMPARE(chats->queued(path).size(), 2);
        QCOMPARE(chats->queued(path).first().toMap().value(u"text"_s).toString(), u"Po pierwsze: zażółć"_s);
        QVERIFY(!chats->queueHeld(path));
        // Stopped: they are held.
        chats->stop(path);
        QVERIFY(!turn->running());
        QVERIFY(chats->queueHeld(path));
        QCOMPARE(chats->queued(path).size(), 2);
        // Kept over a restart (held then too).
        const QString root = ws->rootPath();
        ws.reset();
        ws = open({fake()}, root);
        chats = ws->chats();
        turn = chats->turn(path);
        QCOMPARE(chats->queued(path).size(), 2);
        QVERIFY(chats->queueHeld(path));
        // Resumed: the first goes, its answer sends the second, then nothing waits.
        QVERIFY(chats->resumeQueue(path));
        QVERIFY(turn->running());
        QCOMPARE(chats->queued(path).size(), 1);
        QTRY_VERIFY_WITH_TIMEOUT(chats->queued(path).isEmpty() && !turn->running(), 20000);
        const QString page = ws->read(path);
        QVERIFY2(page.contains(u"- 🧑 Po pierwsze: zażółć\n  czas:: 2026-11-20 09:15\n- 🤖 Odpowiedź na: Po pierwsze: zażółć\n"_s), qPrintable(page));
        QVERIFY2(page.endsWith(u"- 🧑 Po drugie: gęślą jaźń\n  czas:: 2026-11-20 09:15\n- 🤖 Odpowiedź na: Po drugie: gęślą jaźń\n  czas:: 2026-11-20 09:15\n"_s),
                 qPrintable(page));

        // Send now: the turn stops, that one is answered first; one taken out goes back with its text.
        QCOMPARE(chats->send(path, u"Znowu czekaj"_s), path);
        QTRY_VERIFY(turn->running());
        QVERIFY(chats->queue(path, u"Pierwsza w kolejce"_s));
        QVERIFY(chats->queue(path, u"Druga w kolejce"_s));
        const int second = chats->queued(path).at(1).toMap().value(u"id"_s).toInt();
        QCOMPARE(chats->unqueue(path, chats->queued(path).first().toMap().value(u"id"_s).toInt(), true),
                 u"Pierwsza w kolejce"_s);
        QVERIFY(chats->sendNow(path, second));
        QTRY_VERIFY_WITH_TIMEOUT(!turn->running() && chats->queued(path).isEmpty(), 20000);
        QVERIFY2(ws->read(path).endsWith(u"- 🧑 Druga w kolejce\n  czas:: 2026-11-20 09:15\n- 🤖 Odpowiedź na: Druga w kolejce\n  czas:: 2026-11-20 09:15\n"_s),
                 qPrintable(ws->read(path)));
        // Steer with nothing running: sent at once.
        QVERIFY(chats->steer(path, u"Od razu"_s));
        QTRY_VERIFY_WITH_TIMEOUT(!turn->running(), 20000);
        QVERIFY(ws->read(path).endsWith(u"- 🤖 Odpowiedź na: Od razu\n  czas:: 2026-11-20 09:15\n"_s));
        // A conversation renamed: its waiting messages go along.
        QCOMPARE(chats->send(path, u"Trzecie czekaj"_s), path);
        QTRY_VERIFY(turn->running());
        QVERIFY(chats->queue(path, u"Za nazwą"_s));
        const int cancels = int(logged(u"session/cancel"_s).size());
        const int sessions = int(logged(u"session/new"_s).size());
        chats->stop(path);
        // Renamed while the cancelled turn's answer is still on its way: the session goes along,
        // and the next turn goes on in it (no new session).
        QTRY_VERIFY(logged(u"session/cancel"_s).size() > cancels);
        const QString renamed = chats->rename(path, u"Nowa nazwa"_s);
        QCOMPARE(renamed, u"AI/2026-11-20 Nowa nazwa.md"_s);
        QCOMPARE(chats->queued(renamed).size(), 1);
        QVERIFY(chats->queued(path).isEmpty());
        QVERIFY(chats->queueHeld(renamed));
        // A new message while they are held: they go on, it after them.
        QVERIFY(chats->queue(renamed, u"I jeszcze to"_s));
        shell::ChatTurn *renamedTurn = chats->turn(renamed);
        QVERIFY(renamedTurn->running());
        QVERIFY(!chats->queueHeld(renamed));
        QTRY_VERIFY_WITH_TIMEOUT(!renamedTurn->running(), 20000);
        QVERIFY2(chats->queued(renamed).isEmpty(), qPrintable(ws->read(renamed) + u"\nfailure: "_s + renamedTurn->failure()));
        QVERIFY2(ws->read(renamed).endsWith(u"- 🤖 Odpowiedź na: I jeszcze to\n  czas:: 2026-11-20 09:15\n"_s),
                 qPrintable(ws->read(renamed)));
        QCOMPARE(logged(u"session/new"_s).size(), sessions);
    }

    /// "Edit from here" takes the message and the rest out (an agent's next turn: a new session
    /// with what is left; Undo puts them back while nothing changed since); "Fork from here" copies
    /// the conversation up to a message into one that goes on by itself.
    void rewindAndFork()
    {
        auto ws = open({fake()});
        WorkspaceChats *chats = ws->chats();
        const QString path = chats->send(QString(), u"Co mam jutro?"_s, u"Dom"_s, {}, u"acp:fake"_s);
        shell::ChatTurn *turn = chats->turn(path);
        QTRY_VERIFY_WITH_TIMEOUT(!turn->running(), 20000);
        QCOMPARE(chats->send(path, u"A pojutrze?"_s), path);
        QTRY_VERIFY_WITH_TIMEOUT(!turn->running(), 20000);
        QCOMPARE(logged(u"session/new"_s).size(), 1);
        QCOMPARE(chats->prompts(path), (QStringList{u"A pojutrze?"_s, u"Co mam jutro?"_s}));

        const QString full = ws->read(path);
        const chat::Conversation c = chat::parse(full);
        QCOMPARE(c.messages.size(), 4);
        QSignalSpy rewound(chats, &WorkspaceChats::rewound);
        // Only a user's message: an answer is no place to edit from.
        QCOMPARE(chats->rewind(path, c.messages.at(1).line), QString());
        QCOMPARE(chats->rewind(path, c.messages.at(2).line), u"A pojutrze?"_s);
        QCOMPARE(rewound.size(), 1);
        QCOMPARE(chat::parse(ws->read(path)).messages.size(), 2);
        // Undo while nothing changed: all back.
        QVERIFY(chats->undoRewind(path));
        QCOMPARE(ws->read(path), full);
        QVERIFY(!chats->undoRewind(path)); // (once)
        // Again, and on: the agent gets a new session with what is left.
        QCOMPARE(chats->rewind(path, c.messages.at(2).line), u"A pojutrze?"_s);
        QCOMPARE(chats->send(path, u"A w niedzielę?"_s), path);
        QTRY_VERIFY_WITH_TIMEOUT(!turn->running(), 20000);
        QCOMPARE(logged(u"session/new"_s).size(), 2);
        const QJsonArray prompt = promptOf(logged(u"session/prompt"_s).last());
        QString all;
        for (const QJsonValue &b : prompt)
            all += b.toObject().value(u"text"_s).toString() + u'\n';
        QVERIFY2(all.contains(u"Co mam jutro?"_s) && !all.contains(u"A pojutrze?"_s), qPrintable(all));
        // Not undone once the page changed.
        QVERIFY(!chats->undoRewind(path));

        // Fork from the first answer: a copy up to it, the same partner, the source named.
        const chat::Conversation now = chat::parse(ws->read(path));
        const QString forked = chats->fork(path, now.messages.at(1).line);
        QCOMPARE(forked, u"AI/2026-11-20 Co mam jutro 2.md"_s);
        QCOMPARE(chats->forkedFrom(forked), path);
        QCOMPARE(chats->partnerOf(forked), u"acp:fake"_s);
        QCOMPARE(chat::parse(ws->read(forked)).messages.size(), 2);
        QVERIFY(ws->read(ws->journalPath(u"2026-11-20"_s)).contains(u"[[AI/2026-11-20 Co mam jutro 2]]"_s));
        // It goes on by itself: a session of its own.
        QCOMPARE(chats->send(forked, u"A w poniedziałek?"_s), forked);
        shell::ChatTurn *forkTurn = chats->turn(forked);
        QTRY_VERIFY_WITH_TIMEOUT(!forkTurn->running(), 20000);
        QCOMPARE(logged(u"session/new"_s).size(), 3);
        QCOMPARE(chat::parse(ws->read(path)).messages.size(), 4); // (the source as it was)
        QCOMPARE(chats->quote(u"Pierwsza\n![[a.png]]"_s), u"> Pierwsza\n\n"_s);

        // A draft stays with its conversation, follows a rename, goes with an empty text.
        chats->setDraft(forked, u"Jeszcze nie wysłane: ąę"_s);
        chats->setDraft(QString(), u"Nowa rozmowa"_s);
        QCOMPARE(chats->draft(forked), u"Jeszcze nie wysłane: ąę"_s);
        QCOMPARE(chats->draft(QString()), u"Nowa rozmowa"_s);
        const QString renamed = chats->rename(forked, u"Gałąź"_s);
        QCOMPARE(chats->draft(renamed), u"Jeszcze nie wysłane: ąę"_s);
        QCOMPARE(chats->draft(forked), QString());
        chats->setDraft(renamed, QString());
        QCOMPARE(chats->draft(renamed), QString());
    }

    /// Approvals: "edits" lets a change of a file go by itself, "all" too, never a plan; "ask"
    /// shows each question. A plan to approve is the question's plan, saved as a page on request.
    void approvalsAndPlans()
    {
        auto ws = open({fake()});
        WorkspaceChats *chats = ws->chats();
        QCOMPARE(chats->approvals(QString()), u"ask"_s);
        chats->setApprovals(QString(), u"edits"_s);
        QCOMPARE(chats->approvals(QString()), u"edits"_s);
        const QString path = chats->send(QString(), u"Daj mi pozwolenie"_s, QString(), {}, u"acp:fake"_s);
        shell::ChatTurn *turn = chats->turn(path);
        // (A new conversation has the mode chosen last.)
        QCOMPARE(chats->approvals(path), u"edits"_s);
        QTRY_VERIFY_WITH_TIMEOUT(!turn->running(), 20000);
        QVERIFY2(ws->read(path).contains(u"- 🤖 Zapisano."_s), qPrintable(ws->read(path)));
        QVERIFY(!chats->asking(path));
        // Asked again with "ask".
        chats->setApprovals(path, u"ask"_s);
        QCOMPARE(chats->approvals(path), u"ask"_s);
        QCOMPARE(chats->send(path, u"Jeszcze pozwolenie"_s), path);
        QTRY_VERIFY_WITH_TIMEOUT(chats->asking(path), 20000);
        chats->answer(path, u"Reject"_s);
        QTRY_VERIFY_WITH_TIMEOUT(!turn->running(), 20000);
        // A plan asks whatever the mode: as a plan, saved as a page.
        chats->setApprovals(path, u"all"_s);
        QCOMPARE(chats->send(path, u"Proszę zatwierdź"_s), path);
        QTRY_VERIFY_WITH_TIMEOUT(chats->asking(path), 20000);
        QCOMPARE(chats->questionPlan(path), u"# Porządki\n\n1. Połączyć notatki\n2. Usunąć duplikaty"_s);
        QVERIFY(chats->questionText(path).startsWith(u"Fałszywy asks: Ready to code?\n# Porządki"_s));
        const QString saved = chats->savePlan(path);
        QCOMPARE(saved, u"Plan – Daj mi pozwolenie.md"_s);
        QVERIFY(ws->read(saved).startsWith(u"# Porządki\n\n1. Połączyć notatki\n2. Usunąć duplikaty\n\n"_s));
        QVERIFY(ws->read(saved).contains(u"[[AI/2026-11-20 Daj mi pozwolenie]]"_s));
        chats->answer(path, u"Yes"_s);
        QTRY_VERIFY_WITH_TIMEOUT(!turn->running(), 20000);
        QVERIFY(ws->read(path).contains(u"- 🤖 Plan przyjęty."_s));
        QCOMPARE(chats->questionPlan(path), QString());
        // Kept per conversation in the device state, following a rename.
        const QString renamed = chats->rename(path, u"Zgody"_s);
        QCOMPARE(chats->approvals(renamed), u"all"_s);
    }

    /// The files a turn changed (as T3 Code's turn diff), whichever way the agent changed them —
    /// a change it asked for, its own edit of a file it names, fs/write_text_file —, and Undo:
    /// each back as it was, unless it changed since.
    void turnChanges()
    {
        auto ws = open({fake()});
        WorkspaceChats *chats = ws->chats();
        const QString dom = ws->read(u"Dom.md"_s);
        // Its own edit of a file it names (Claude Code's Edit).
        const QString path = chats->send(QString(), u"Proszę edytuj"_s, QString(), {}, u"acp:fake"_s);
        shell::ChatTurn *turn = chats->turn(path);
        QTRY_VERIFY_WITH_TIMEOUT(!turn->running(), 20000);
        QVERIFY(ws->read(u"Dom.md"_s).contains(u"Zapłacić czynsz i prąd"_s));
        QVariantList changes = chats->changes(path);
        QCOMPARE(changes.size(), 1);
        QVariantMap change = changes.first().toMap();
        QCOMPARE(change.value(u"path"_s).toString(), u"Dom.md"_s);
        QCOMPARE(change.value(u"oldText"_s).toString(), dom);
        QVERIFY(change.value(u"known"_s).toBool());
        QVERIFY(change.value(u"existed"_s).toBool());
        QVERIFY(!chats->undoChanges(path).isEmpty());
        QCOMPARE(ws->read(u"Dom.md"_s), dom);
        QVERIFY(chats->changes(path).isEmpty());

        // A change it asked for (a new file): undone to the trash.
        chats->setApprovals(path, u"edits"_s);
        QCOMPARE(chats->send(path, u"Daj pozwolenie"_s), path);
        QTRY_VERIFY_WITH_TIMEOUT(!turn->running(), 20000);
        changes = chats->changes(path);
        QCOMPARE(changes.size(), 1);
        change = changes.first().toMap();
        QCOMPARE(change.value(u"path"_s).toString(), u"Zgoda.md"_s);
        QVERIFY(!change.value(u"existed"_s).toBool());
        QVERIFY(change.value(u"exists"_s).toBool());
        QCOMPARE(change.value(u"newText"_s).toString(), u"- zgoda\n"_s);
        // Changed since by the user: left as it is.
        QVERIFY(ws->write(u"Zgoda.md"_s, u"- moja zmiana\n"_s));
        chats->undoChanges(path);
        QCOMPARE(ws->read(u"Zgoda.md"_s), u"- moja zmiana\n"_s);

        // Written through the client; a new turn forgets the last one's.
        QCOMPARE(chats->send(path, u"Proszę zapisz"_s), path);
        QTRY_VERIFY_WITH_TIMEOUT(!turn->running(), 20000);
        changes = chats->changes(path);
        QCOMPARE(changes.size(), 1);
        QCOMPARE(changes.first().toMap().value(u"path"_s).toString(), u"Od agenta.md"_s);
        QCOMPARE(chats->send(path, u"Coś innego"_s), path);
        QVERIFY(chats->changes(path).isEmpty() || turn->running());
        QTRY_VERIFY_WITH_TIMEOUT(!turn->running(), 20000);
        QVERIFY(chats->changes(path).isEmpty());
    }

    /// Files an agent's shell command changed (it names none: Claude Code's Bash) are the turn's
    /// changes too — their text before from the index, as the notebook saw it — and Undo puts them
    /// back; another program's change while a turn runs no command is not the turn's.
    void shellChangesUndone()
    {
        auto ws = open({fake()});
        WorkspaceChats *chats = ws->chats();
        const QString dom = ws->read(u"Dom.md"_s);
        const QString old = u"- stara notatka z ąę\n"_s;
        QVERIFY(ws->write(u"Stara.md"_s, old));
        QTRY_VERIFY(ws->index()->content(u"Stara.md"_s) == old);
        const QString path = chats->send(QString(), u"Proszę powłoką"_s, QString(), {}, u"acp:fake"_s);
        shell::ChatTurn *turn = chats->turn(path);
        QTRY_VERIFY_WITH_TIMEOUT(!turn->running(), 20000);
        QCOMPARE(turn->failure(), QString());
        QCOMPARE(ws->read(u"Dom.md"_s), dom + u"- [ ] Kupić mleko\n"_s);
        QVariantMap byPath;
        for (const QVariant &c : chats->changes(path))
            byPath.insert(c.toMap().value(u"path"_s).toString(), c);
        QCOMPARE(byPath.keys(), (QStringList{u"Dom.md"_s, u"Nowa.md"_s, u"Przeniesiona.md"_s, u"Stara.md"_s}));
        // Renamed: the old name had its text, the new one was not there.
        const QVariantMap renamedFrom = byPath.value(u"Stara.md"_s).toMap();
        QCOMPARE(renamedFrom.value(u"oldText"_s).toString(), old);
        QVERIFY(renamedFrom.value(u"existed"_s).toBool() && !renamedFrom.value(u"exists"_s).toBool());
        QVERIFY(!byPath.value(u"Przeniesiona.md"_s).toMap().value(u"existed"_s).toBool());
        const QVariantMap changed = byPath.value(u"Dom.md"_s).toMap();
        QCOMPARE(changed.value(u"oldText"_s).toString(), dom);
        QVERIFY(changed.value(u"known"_s).toBool() && changed.value(u"existed"_s).toBool());
        const QVariantMap made = byPath.value(u"Nowa.md"_s).toMap();
        QVERIFY(!made.value(u"existed"_s).toBool() && made.value(u"exists"_s).toBool());
        QVERIFY(!chats->undoChanges(path).isEmpty());
        QCOMPARE(ws->read(u"Dom.md"_s), dom);
        QTRY_VERIFY(!QFileInfo::exists(ws->absoluteFilePath(u"Nowa.md"_s))); // (to the trash)
        QCOMPARE(ws->read(u"Stara.md"_s), old);
        QTRY_VERIFY(!QFileInfo::exists(ws->absoluteFilePath(u"Przeniesiona.md"_s)));

        // A file a call named but did not change in the end (the edit failed): not among them.
        QCOMPARE(chats->send(path, u"Proszę nieudana edycja"_s), path);
        QTRY_VERIFY_WITH_TIMEOUT(!turn->running(), 20000);
        QVERIFY(ws->read(path).contains(u"Nie wyszło."_s));
        QVERIFY(chats->changes(path).isEmpty());

        // Another program's change while the turn runs no command: not the turn's.
        QCOMPARE(chats->send(path, u"Proszę powoli"_s), path);
        QVERIFY(turn->running());
        QVERIFY(writeRaw(ws->absoluteFilePath(u"Dom.md"_s), "- [ ] Zmiana z innego programu\n"));
        QTRY_VERIFY_WITH_TIMEOUT(!turn->running(), 20000);
        QTRY_VERIFY(ws->read(u"Dom.md"_s).contains(u"innego programu"_s));
        QVERIFY(chats->changes(path).isEmpty());
    }

    void stop()
    {
        auto ws = open({fake()});
        WorkspaceChats *chats = ws->chats();
        const QString path = chats->send(QString(), u"Proszę czekaj"_s, QString(), {}, u"acp:fake"_s);
        shell::ChatTurn *turn = chats->turn(path);
        QTRY_VERIFY_WITH_TIMEOUT(logged(u"session/prompt"_s).size() == 1, 20000);
        QVERIFY(turn->running());
        const QString before = ws->read(path);
        chats->stop(path);
        QVERIFY(!turn->running());
        QTRY_VERIFY(logged(u"session/cancel"_s).size() == 1);
        // The next message is answered once the agent ended the cancelled turn; nothing for that one.
        QCOMPARE(chats->send(path, u"Dalej"_s), path);
        QTRY_VERIFY_WITH_TIMEOUT(!turn->running(), 20000);
        QCOMPARE(turn->failure(), QString());
        QCOMPARE(ws->read(path), before + u"- 🧑 Dalej\n  czas:: 2026-11-20 09:15\n- 🤖 Odpowiedź na: Dalej\n  czas:: 2026-11-20 09:15\n"_s);
        // (The agent got both messages: the one it never answered goes with the newest.)
        QCOMPARE(promptOf(logged(u"session/prompt"_s).last()).first().toObject().value(u"text"_s).toString(),
                 u"Proszę czekaj\n\nDalej"_s);
        QCOMPARE(logged(u"session/prompt"_s).size(), 2);
    }

    void agentDies()
    {
        auto ws = open({fake()});
        WorkspaceChats *chats = ws->chats();
        const QString path = chats->send(QString(), u"Pierwsze"_s, QString(), {}, u"acp:fake"_s);
        shell::ChatTurn *turn = chats->turn(path);
        QTRY_VERIFY_WITH_TIMEOUT(!turn->running(), 20000);
        QCOMPARE(chats->send(path, u"To koniec"_s), path);
        QTRY_VERIFY_WITH_TIMEOUT(!turn->running(), 20000);
        QVERIFY2(turn->failure().contains(u"Fałszywy"_s) && turn->failure().contains(u"3"_s)
                         && turn->failure().contains(u"fake agent: koniec"_s),
                 qPrintable(turn->failure()));
        // Started again: a new session that gets the conversation so far.
        QCOMPARE(chats->send(path, u"Jesteś?"_s), path);
        QTRY_VERIFY_WITH_TIMEOUT(!turn->running(), 20000);
        QCOMPARE(turn->failure(), QString());
        QVERIFY2(ws->read(path).endsWith(u"- 🤖 Odpowiedź na: Jesteś?\n  czas:: 2026-11-20 09:15\n"_s), qPrintable(ws->read(path)));
        const QJsonArray first = promptOf(logged(u"session/prompt"_s).last());
        QString transcript;
        for (const QJsonValue &b : first)
            transcript += b.toObject().value(u"text"_s).toString() + u'\n';
        QVERIFY2(transcript.contains(u"🧑 Pierwsze\n\n🤖 Odpowiedź na: Pierwsze"_s), qPrintable(transcript));
        // (The message it never answered goes with the newest one.)
        QVERIFY(transcript.contains(u"To koniec\n\nJesteś?"_s));
    }

    void signIn()
    {
        auto ws = open({fake(false, u"--auth"_s)});
        WorkspaceChats *chats = ws->chats();
        const QString path = chats->send(QString(), u"Witaj"_s, QString(), {}, u"acp:fake"_s);
        shell::ChatTurn *turn = chats->turn(path);
        QTRY_VERIFY_WITH_TIMEOUT(chats->asking(path), 20000);
        QCOMPARE(chats->questionText(path), u"Fałszywy asks you to sign in.\nAuthentication required\nLog in: Opens the browser"_s);
        QCOMPARE(chats->questionOptions(path).first(), u"Log in"_s);
        QCOMPARE(chats->questionOptions(path).size(), 2);
        chats->answer(path, u"Log in"_s);
        QTRY_VERIFY_WITH_TIMEOUT(!turn->running(), 20000);
        QCOMPARE(turn->failure(), QString());
        QCOMPARE(logged(u"authenticate"_s).size(), 1);
        QCOMPARE(logged(u"authenticate"_s).first().value(u"params"_s).toObject().value(u"methodId"_s).toString(), u"login"_s);
        QVERIFY(ws->read(path).contains(u"- 🤖 Odpowiedź na: Witaj"_s));

        // Cancel: the turn fails, nothing more is asked.
        auto other = open({fake(false, u"--auth"_s)});
        const QString second = other->chats()->send(QString(), u"Witaj"_s, QString(), {}, u"acp:fake"_s);
        QTRY_VERIFY_WITH_TIMEOUT(other->chats()->asking(second), 20000);
        other->chats()->answer(second, other->chats()->questionOptions(second).last());
        QVERIFY(!other->chats()->turn(second)->running());
        QVERIFY(!other->chats()->turn(second)->failure().isEmpty());
    }

    void filesThroughTheApp()
    {
        auto ws = open({fake()});
        WorkspaceChats *chats = ws->chats();
        const QString path = chats->send(QString(), u"Przeczytaj, czytaj"_s, QString(), {}, u"acp:fake"_s);
        shell::ChatTurn *turn = chats->turn(path);
        QTRY_VERIFY_WITH_TIMEOUT(!turn->running(), 20000);
        QVERIFY2(ws->read(path).contains(u"- 🤖 Przeczytano: - [ ] Zapłacić czynsz 📅 2026-11-16"_s), qPrintable(ws->read(path)));
        // Written through the Workspace: the notebook sees it at once; a file outside it is refused.
        QCOMPARE(chats->send(path, u"Teraz zapisz"_s), path);
        QTRY_VERIFY_WITH_TIMEOUT(!turn->running(), 20000);
        QVERIFY2(ws->read(path).contains(u"- 🤖 Zapisano przez klienta; poza notatnikiem: -32602"_s), qPrintable(ws->read(path)));
        QVERIFY(ws->exists(u"Od agenta.md"_s));
        QCOMPARE(ws->read(u"Od agenta.md"_s), u"- od agenta\n"_s);
        QVERIFY(!QFile::exists(QDir::cleanPath(QDir(ws->rootPath()).absolutePath()) + u"-outside.txt"_s));
        const QJsonObject caps = logged(u"initialize"_s).first().value(u"params"_s).toObject().value(u"clientCapabilities"_s).toObject();
        QVERIFY(caps.value(u"fs"_s).toObject().value(u"writeTextFile"_s).toBool());
        QVERIFY(caps.value(u"terminal"_s).toBool());
    }

    void activity()
    {
        auto ws = open({fake()});
        WorkspaceChats *chats = ws->chats();
        // A terminal: its output with its tool call while it runs, its exit code to the agent.
        QVariantList seenTools, seenPlan;
        const QString path = chats->send(QString(), u"Uruchom terminal"_s, QString(), {}, u"acp:fake"_s);
        shell::ChatTurn *turn = chats->turn(path);
        QObject watching; // (its connections go with this function's locals)
        connect(turn, &shell::ChatTurn::activityChanged, &watching, [&] {
            if (!turn->tools().isEmpty())
                seenTools = turn->tools();
            if (!turn->plan().isEmpty())
                seenPlan = turn->plan();
        });
        QTRY_VERIFY_WITH_TIMEOUT(!turn->running(), 20000);
        QVERIFY2(ws->read(path).contains(u"- 🤖 Wyjście (4): ąę|drugi"_s), qPrintable(ws->read(path)));
        QVERIFY(!seenTools.isEmpty());
        const QVariantMap call = seenTools.last().toMap();
        QCOMPARE(call.value(u"title"_s).toString(), u"Run sh"_s);
        QCOMPARE(call.value(u"kind"_s).toString(), u"execute"_s);
        QVERIFY2(call.value(u"output"_s).toString().contains(u"drugi"_s), qPrintable(call.value(u"output"_s).toString()));
        QVERIFY(turn->tools().isEmpty()); // (the turn ended)
        // … but its work log stays with the answer (message 2): the call as it ended, how long.
        QVariantMap log = chats->workLog(path, 2);
        QVERIFY2(!log.isEmpty(), "the work log of the answer");
        QCOMPARE(log.value(u"tools"_s).toList().last().toMap().value(u"title"_s).toString(), u"Run sh"_s);
        QVERIFY(log.value(u"tools"_s).toList().last().toMap().value(u"output"_s).toString().contains(u"drugi"_s));
        QVERIFY(log.value(u"seconds"_s).toInt() >= 0);
        QVERIFY(chats->workLog(path, 1).isEmpty()); // (the user's question)
        QVERIFY(chats->workLog(u"Inna.md"_s, 2).isEmpty());
        // A plan.
        QSignalSpy kept(chats, &WorkspaceChats::workLogChanged);
        QCOMPARE(chats->send(path, u"Zrób plan"_s), path);
        QTRY_VERIFY_WITH_TIMEOUT(!turn->running(), 20000);
        QCOMPARE(kept.size(), 1);
        QCOMPARE(chats->workLog(path, 4).value(u"plan"_s).toList().size(), 2);
        QCOMPARE(seenPlan.size(), 2);
        QCOMPARE(seenPlan.at(1).toMap().value(u"content"_s).toString(), u"Napisać plan"_s);
        QCOMPARE(seenPlan.at(1).toMap().value(u"status"_s).toString(), u"in_progress"_s);
        // Tool calls without words: what it did is the answer — the calls in words (chattools.h;
        // untranslated here: "file(s)"), then their titles.
        QCOMPARE(chats->send(path, u"Same narzędzia"_s), path);
        QTRY_VERIFY_WITH_TIMEOUT(!turn->running(), 20000);
        QCOMPARE(turn->failure(), QString());
        QVERIFY2(ws->read(path).contains(u"- 🤖 Edited 1 file(s)\n  czas:: 2026-11-20 09:15\n\n  - Edit Dom.md"_s),
                 qPrintable(ws->read(path)));
        // Its usage and its slash commands.
        QCOMPARE(chats->agentUsage(path), u"20 % of the context · 0.25 USD"_s);
        // (The app's own /btw first, never the agent's: a side question.)
        const QVariantList all = chats->agentCommands(path, u"acp:fake"_s, u"/"_s);
        QCOMPARE(all.size(), 3);
        QCOMPARE(all.first().toMap().value(u"name"_s).toString(), u"btw"_s);
        QVERIFY(all.first().toMap().value(u"app"_s).toBool());
        const QVariantList review = chats->agentCommands(path, u"acp:fake"_s, u"/RE"_s);
        QCOMPARE(review.size(), 1);
        QCOMPARE(review.first().toMap().value(u"description"_s).toString(), u"Przejrzyj zmiany"_s);
        QVERIFY(chats->agentCommands(path, u"acp:fake"_s, u"/review coś"_s).isEmpty());
        QVERIFY(chats->agentCommands(path, u"acp:fake"_s, u"review"_s).isEmpty());
        QCOMPARE(chats->agentCommands(path, QString(), u"/"_s).size(), 1); // (the assistant's: /btw alone)
        // (A new conversation offers the agent's last commands.)
        QCOMPARE(chats->agentCommands(QString(), u"acp:fake"_s, u"/p"_s).size(), 1);

        // A change of a file as a diff with its call; the conversation deleted: its session closed.
        QVariantList shown;
        connect(turn, &shell::ChatTurn::activityChanged, &watching, [&] {
            if (turn->tools().size() >= 2)
                shown = turn->tools();
        });
        QCOMPARE(chats->send(path, u"Teraz pokaż, co robisz"_s), path);
        QTRY_VERIFY_WITH_TIMEOUT(shown.size() >= 2, 20000);
        const QVariantMap edit = shown.at(1).toMap();
        QCOMPARE(edit.value(u"title"_s).toString(), u"Edit Dom.md"_s);
        QCOMPARE(edit.value(u"diffOld"_s).toString(), u"- [ ] Zapłacić czynsz\n"_s);
        QVERIFY(edit.value(u"diffNew"_s).toString().contains(u"Kupić mleko"_s));
        QVERIFY(!chats->remove(path).isEmpty());
        QVERIFY(!turn->running());
        QTRY_VERIFY(logged(u"session/cancel"_s).size() == 1);
    }

    void options()
    {
        auto ws = open({fake(false, u"--config"_s)});
        WorkspaceChats *chats = ws->chats();
        QSignalSpy remembered(chats, &WorkspaceChats::agentRemembered);
        QVERIFY(chats->agentOptions(QString(), u"acp:fake"_s).isEmpty()); // (no session yet: nothing known)
        const QString path = chats->send(QString(), u"Jaki model?"_s, QString(), {}, u"acp:fake"_s);
        shell::ChatTurn *turn = chats->turn(path);
        QTRY_VERIFY_WITH_TIMEOUT(!turn->running(), 20000);
        QVERIFY2(ws->read(path).contains(u"- 🤖 Model: sonnet (fast), tryb: ask"_s), qPrintable(ws->read(path)));
        // Its options: the configOptions and the modes; learnt for the agent.
        QVariantList options = chats->agentOptions(path, u"acp:fake"_s);
        QCOMPARE(options.size(), 3);
        QCOMPARE(options.at(0).toMap().value(u"currentName"_s).toString(), u"Sonnet"_s);
        QCOMPARE(options.at(1).toMap().value(u"boolean"_s).toBool(), true);
        QCOMPARE(options.at(1).toMap().value(u"current"_s).toString(), u"true"_s);
        QCOMPARE(options.at(2).toMap().value(u"id"_s).toString(), u"mode"_s);
        const QVariantList choices = chats->agentChoices(path, u"acp:fake"_s, u"model"_s);
        QCOMPARE(choices.size(), 2);
        QCOMPARE(choices.at(1).toMap().value(u"description"_s).toString(), u"Największy"_s);
        QVERIFY(!remembered.isEmpty());
        QCOMPARE(remembered.last().at(2).toJsonArray().size(), 3);

        // Chosen in the chat: set on the session, remembered for the agent.
        chats->setAgentOption(path, u"acp:fake"_s, u"model"_s, u"opus"_s);
        chats->setAgentOption(path, u"acp:fake"_s, u"fast"_s, u"false"_s);
        chats->setAgentOption(path, u"acp:fake"_s, u"mode"_s, u"code"_s);
        QTRY_COMPARE(chats->agentOptions(path, u"acp:fake"_s).at(0).toMap().value(u"current"_s).toString(), u"opus"_s);
        QTRY_COMPARE(chats->agentOptions(path, u"acp:fake"_s).at(2).toMap().value(u"current"_s).toString(), u"code"_s);
        const QJsonObject chosen = remembered.last().at(1).toJsonObject();
        QCOMPARE(chosen, (QJsonObject{{u"model"_s, u"opus"_s}, {u"fast"_s, false}, {u"mode"_s, u"code"_s}}));
        QCOMPARE(chats->send(path, u"A teraz jaki model?"_s), path);
        QTRY_VERIFY_WITH_TIMEOUT(!turn->running(), 20000);
        QVERIFY2(ws->read(path).contains(u"- 🤖 Model: opus, tryb: code"_s), qPrintable(ws->read(path)));

        // A new conversation: the options as they will be set, then set before its first prompt.
        const QVariantList before = chats->agentOptions(QString(), u"acp:fake"_s);
        QCOMPARE(before.size(), 3);
        QCOMPARE(before.at(0).toMap().value(u"current"_s).toString(), u"opus"_s);
        // (The agent restarted: its own defaults again, the remembered ones set on the new session.)
        chats->setAgents({});
        AgentConfig again = fake(false, u"--config"_s);
        again.options = chosen;
        again.known = remembered.last().at(2).toJsonArray();
        chats->setAgents({again});
        const QString second = chats->send(QString(), u"Jaki model w nowej?"_s, QString(), {}, u"acp:fake"_s);
        QTRY_VERIFY_WITH_TIMEOUT(!chats->turn(second)->running(), 20000);
        QVERIFY2(ws->read(second).contains(u"- 🤖 Model: opus, tryb: code"_s), qPrintable(ws->read(second)));
    }

    void resumeAfterRestart()
    {
        QString root, path;
        {
            auto ws = open({fake(false, u"--resume"_s)});
            root = ws->rootPath();
            path = ws->chats()->send(QString(), u"Witaj"_s, QString(), {}, u"acp:fake"_s);
            QTRY_VERIFY_WITH_TIMEOUT(!ws->chats()->turn(path)->running(), 20000);
            QCOMPARE(logged(u"session/new"_s).size(), 1);
        }
        // The app again: the conversation's session resumed, only the new message sent.
        {
            auto ws = open({fake(false, u"--resume"_s)}, root);
            QCOMPARE(ws->chats()->send(path, u"Która sesja?"_s), path);
            QTRY_VERIFY_WITH_TIMEOUT(!ws->chats()->turn(path)->running(), 20000);
            QVERIFY2(ws->read(path).contains(u"- 🤖 Sesja: s1"_s), qPrintable(ws->read(path)));
            QCOMPARE(logged(u"session/resume"_s).size(), 1);
            QCOMPARE(logged(u"session/new"_s).size(), 1);
            QCOMPARE(promptOf(logged(u"session/prompt"_s).last()).size(), 1);
        }
        // Changed meanwhile (another device answered): a new session with the conversation so far.
        {
            auto ws = open({fake(false, u"--resume"_s)}, root);
            const QDateTime now(QDate(2026, 11, 20), QTime(9, 20));
            QString content = ws->read(path);
            content = chat::appendMessage(content, chat::Role::User, u"Pytanie z telefonu"_s, now, u"pl"_s);
            content = chat::appendMessage(content, chat::Role::Assistant, u"Odpowiedź z telefonu"_s, now, u"pl"_s);
            QVERIFY(ws->write(path, content));
            QCOMPARE(ws->chats()->send(path, u"I co dalej?"_s), path);
            QTRY_VERIFY_WITH_TIMEOUT(!ws->chats()->turn(path)->running(), 20000);
            QCOMPARE(logged(u"session/resume"_s).size(), 1);
            QCOMPARE(logged(u"session/new"_s).size(), 2);
            QString first;
            for (const QJsonValue &b : promptOf(logged(u"session/prompt"_s).last()))
                first += b.toObject().value(u"text"_s).toString() + u'\n';
            QVERIFY2(first.contains(u"🤖 Odpowiedź z telefonu"_s), qPrintable(first));
        }
    }

    void check()
    {
        auto ws = open({fake(false, u"--config"_s), AgentConfig{u"zly"_s, u"Zły"_s, u"/nonexistent/agent"_s, false}});
        WorkspaceChats *chats = ws->chats();
        QSignalSpy checked(chats, &WorkspaceChats::agentChecked);
        chats->checkAgent(u"fake"_s);
        QTRY_COMPARE_WITH_TIMEOUT(checked.size(), 1, 20000);
        QCOMPARE(checked.first().at(0).toString(), u"fake"_s);
        QVERIFY(checked.first().at(1).toBool());
        const QString text = checked.first().at(2).toString();
        QVERIFY2(text.startsWith(u"Fałszywy works (Fake Agent 1.2)."_s) && text.contains(u"Model: Sonnet"_s), qPrintable(text));
        QCOMPARE(logged(u"session/prompt"_s).size(), 0);
        // Learnt: the chat shows its options before a first message.
        QCOMPARE(chats->agentOptions(QString(), u"acp:fake"_s).size(), 3);
        chats->checkAgent(u"zly"_s);
        QTRY_COMPARE_WITH_TIMEOUT(checked.size(), 2, 20000);
        QVERIFY(!checked.last().at(1).toBool());
        QVERIFY2(checked.last().at(2).toString().contains(u"Zły"_s), qPrintable(checked.last().at(2).toString()));
    }

    void realAgent()
    {
        // A real agent, when one is named: BRULION_REAL_ACP_AGENT="npx -y @agentclientprotocol/claude-agent-acp"
        // (it signs in by itself; a question costs a fraction of a cent of its subscription).
        const QString command = qEnvironmentVariable("BRULION_REAL_ACP_AGENT");
        if (command.isEmpty())
            QSKIP("no real agent named (BRULION_REAL_ACP_AGENT)");
        auto ws = open({AgentConfig{u"real"_s, u"Real"_s, command, true}});
        WorkspaceChats *chats = ws->chats();
        QSignalSpy checked(chats, &WorkspaceChats::agentChecked);
        chats->checkAgent(u"real"_s);
        QTRY_COMPARE_WITH_TIMEOUT(checked.size(), 1, 240000);
        qInfo() << "check:" << checked.first().at(1).toBool() << checked.first().at(2).toString();
        QVERIFY(checked.first().at(1).toBool());
        qInfo() << "options:" << chats->agentOptions(QString(), u"acp:real"_s).size();
        // An option chosen before the first message (BRULION_REAL_ACP_OPTION="model=gpt-6-luna"):
        // set on the new session before its prompt.
        if (const QString option = qEnvironmentVariable("BRULION_REAL_ACP_OPTION"); option.contains(u'='))
            chats->setAgentOption(QString(), u"acp:real"_s, option.section(u'=', 0, 0), option.section(u'=', 1));
        const QString path = chats->send(QString(), u"Jakie zadanie jest w notatce Dom? Odpowiedz krótko, jednym zdaniem."_s,
                                         u"Dom"_s, {}, u"acp:real"_s);
        shell::ChatTurn *turn = chats->turn(path);
        QTRY_VERIFY_WITH_TIMEOUT(!turn->running(), 300000);
        qInfo().noquote() << ws->read(path);
        QCOMPARE(turn->failure(), QString());
        QVERIFY(ws->read(path).contains(u"czynsz"_s, Qt::CaseInsensitive));
        qInfo() << "usage:" << chats->agentUsage(path);
    }

    // The chat's T3 Code features with a real agent, when one is named (BRULION_REAL_ACP_AGENT and
    // BRULION_REAL_ACP_OPTION as in realAgent(); docs/UI.md "AI chat"): a message queued while a
    // turn runs goes after it; one steered in stops the turn and is answered; a side chat (/btw)
    // answers beside the conversation without touching it; "Fork from here" goes on in a session
    // of its own; "Edit from here" gives the next turn a new session with what is left.
    void realAgentChatFeatures()
    {
        const QString command = qEnvironmentVariable("BRULION_REAL_ACP_AGENT");
        if (command.isEmpty())
            QSKIP("no real agent named (BRULION_REAL_ACP_AGENT)");
        auto ws = open({AgentConfig{u"real"_s, u"Real"_s, command, true}});
        WorkspaceChats *chats = ws->chats();
        if (const QString option = qEnvironmentVariable("BRULION_REAL_ACP_OPTION"); option.contains(u'=')) {
            QSignalSpy checked(chats, &WorkspaceChats::agentChecked);
            chats->checkAgent(u"real"_s);
            QTRY_COMPARE_WITH_TIMEOUT(checked.size(), 1, 240000);
            chats->setAgentOption(QString(), u"acp:real"_s, option.section(u'=', 0, 0), option.section(u'=', 1));
        }
        // Its questions (may it edit Dom.md?) allowed as they come, in every conversation.
        QStringList conversations;
        QTimer answers;
        answers.setInterval(200);
        QObject::connect(&answers, &QTimer::timeout, chats, [&] {
            for (const QString &p : std::as_const(conversations)) {
                if (!chats->asking(p))
                    continue;
                const QStringList options = chats->questionOptions(p);
                QString allow = options.value(0);
                for (const QString &o : options)
                    if (o.contains(u"allow"_s, Qt::CaseInsensitive) && !o.contains(u"always"_s, Qt::CaseInsensitive)) {
                        allow = o;
                        break;
                    }
                qInfo() << "asked:" << chats->questionText(p) << "->" << allow;
                chats->answer(p, allow);
            }
        });
        answers.start();
        const auto settled = [chats](const QString &p) {
            return QTest::qWaitFor([&] { return !chats->turn(p)->running() && chats->queued(p).isEmpty(); }, 600000);
        };
        const auto said = [chats](const QString &p, const QString &word) {
            return chats->lastAnswer(p).contains(word, Qt::CaseInsensitive);
        };

        // A message queued while the first turn runs: answered after it, in a turn of its own.
        const QString path = chats->send(QString(), u"Wymień po kolei dni tygodnia po polsku, każdy w osobnej linii."_s,
                                         QString(), {}, u"acp:real"_s);
        QVERIFY(!path.isEmpty());
        conversations << path;
        QVERIFY(chats->turn(path)->running());
        QVERIFY(chats->queue(path, u"Teraz odpowiedz tylko jednym słowem, wielkimi literami: KOLEJKA"_s));
        QCOMPARE(chats->queued(path).size(), 1);
        QVERIFY(settled(path));
        qInfo().noquote() << ws->read(path);
        QCOMPARE(chats->turn(path)->failure(), QString());
        QVERIFY(ws->read(path).contains(u"środa"_s, Qt::CaseInsensitive));
        QVERIFY2(said(path, u"KOLEJKA"_s), qPrintable(chats->lastAnswer(path)));
        QCOMPARE(chat::parse(ws->read(path)).messages.size(), 4);

        // Steered in while a long answer is being written: the turn stops, the new message is answered.
        QCOMPARE(chats->send(path, u"Napisz długi wiersz o Bałtyku, dwanaście zwrotek po cztery wersy."_s), path);
        QVERIFY(QTest::qWaitFor(
                [&] { return !chats->turn(path)->partial().isEmpty() || !chats->turn(path)->running(); }, 300000));
        QVERIFY(chats->steer(path, u"Przestań pisać wiersz. Odpowiedz tylko jednym słowem, wielkimi literami: STER"_s));
        QVERIFY(settled(path));
        qInfo().noquote() << ws->read(path).section(u"Bałtyku"_s, 1);
        QCOMPARE(chats->turn(path)->failure(), QString());
        QVERIFY2(said(path, u"STER"_s), qPrintable(chats->lastAnswer(path)));

        // A side question (/btw): its own page, answered there; the conversation untouched.
        const QString before = ws->read(path);
        const QString side = chats->askAside(path, u"Ile to jest 17 + 25? Odpowiedz samą liczbą."_s);
        QVERIFY(!side.isEmpty());
        conversations << side;
        QCOMPARE(chats->sideOf(side), path);
        QVERIFY(settled(side));
        QCOMPARE(chats->turn(side)->failure(), QString());
        QVERIFY2(said(side, u"42"_s), qPrintable(chats->lastAnswer(side)));
        QCOMPARE(ws->read(path), before);

        // Fork from the first answer: a copy up to it that goes on in a session of its own.
        const chat::Conversation c = chat::parse(ws->read(path));
        const QString forked = chats->fork(path, c.messages.at(1).line);
        QVERIFY(!forked.isEmpty());
        conversations << forked;
        QCOMPARE(chats->forkedFrom(forked), path);
        QCOMPARE(chats->send(forked, u"Który dzień wymieniłeś jako trzeci? Odpowiedz samą nazwą dnia."_s), forked);
        QVERIFY(settled(forked));
        QCOMPARE(chats->turn(forked)->failure(), QString());
        QVERIFY2(said(forked, u"środa"_s), qPrintable(chats->lastAnswer(forked)));
        QCOMPARE(ws->read(path), before);

        // Edit from here: the steered message and what came after it out; a new session goes on.
        const chat::Conversation now = chat::parse(ws->read(path));
        int steered = -1;
        for (const chat::Message &m : now.messages)
            if (m.role == chat::Role::User && m.text.contains(u"STER"_s))
                steered = m.line;
        QVERIFY(steered >= 0);
        QVERIFY(chats->rewind(path, steered).contains(u"STER"_s));
        QVERIFY(!ws->read(path).contains(u"STER"_s));
        QCOMPARE(chats->send(path, u"Odpowiedz tylko jednym słowem, wielkimi literami: EDYCJA"_s), path);
        QVERIFY(settled(path));
        QCOMPARE(chats->turn(path)->failure(), QString());
        QVERIFY2(said(path, u"EDYCJA"_s), qPrintable(chats->lastAnswer(path)));

        qInfo() << "usage:" << chats->agentUsage(path);
    }

    // A file a real agent's turn changed in the notebook is put back by Undo (docs/UI.md "AI agents
    // (ACP)": a turn's changed files), when one is named (BRULION_REAL_ACP_AGENT, _OPTION): `how`
    // says how the agent is asked to write — with its own editing tool, or a shell command.
    void realAgentUndoesItsChanges_data()
    {
        QTest::addColumn<QString>("how");
        QTest::newRow("its editing tool") << u"Użyj swojego narzędzia do edycji plików (nie powłoki)."_s;
        QTest::newRow("a shell command") << u"Użyj jednego polecenia powłoki (echo z >>), nie narzędzia do edycji."_s;
    }
    void realAgentUndoesItsChanges()
    {
        QFETCH(QString, how);
        const QString command = qEnvironmentVariable("BRULION_REAL_ACP_AGENT");
        if (command.isEmpty())
            QSKIP("no real agent named (BRULION_REAL_ACP_AGENT)");
        auto ws = open({AgentConfig{u"real"_s, u"Real"_s, command, true}});
        WorkspaceChats *chats = ws->chats();
        if (const QString option = qEnvironmentVariable("BRULION_REAL_ACP_OPTION"); option.contains(u'=')) {
            QSignalSpy checked(chats, &WorkspaceChats::agentChecked);
            chats->checkAgent(u"real"_s);
            QTRY_COMPARE_WITH_TIMEOUT(checked.size(), 1, 240000);
            chats->setAgentOption(QString(), u"acp:real"_s, option.section(u'=', 0, 0), option.section(u'=', 1));
        }
        QString path;
        QTimer answers;
        answers.setInterval(200);
        QObject::connect(&answers, &QTimer::timeout, chats, [&] {
            if (path.isEmpty() || !chats->asking(path))
                return;
            const QStringList options = chats->questionOptions(path);
            QString allow = options.value(0);
            for (const QString &o : options)
                if (o.contains(u"allow"_s, Qt::CaseInsensitive) && !o.contains(u"always"_s, Qt::CaseInsensitive)) {
                    allow = o;
                    break;
                }
            qInfo() << "asked:" << chats->questionText(path) << "->" << allow;
            chats->answer(path, allow);
        });
        answers.start();
        const QString dom = ws->read(u"Dom.md"_s);
        path = chats->send(QString(),
                           u"Dopisz na końcu pliku Dom.md w folderze, w którym pracujesz, jedną linię: "
                           u"\"- [ ] Kupić mleko\". "_s
                                   + how + u" Nie zmieniaj niczego innego i odpowiedz krótko."_s,
                           QString(), {}, u"acp:real"_s);
        QVERIFY(!path.isEmpty());
        QTRY_VERIFY_WITH_TIMEOUT(!chats->turn(path)->running(), 600000);
        qInfo().noquote() << ws->read(path);
        QCOMPARE(chats->turn(path)->failure(), QString());
        QTRY_VERIFY_WITH_TIMEOUT(ws->read(u"Dom.md"_s).contains(u"Kupić mleko"_s), 10000);
        const QVariantList changed = chats->changes(path);
        qInfo() << "changes:" << changed;
        QVERIFY(std::ranges::any_of(changed, [](const QVariant &f) {
            return f.toMap().value(u"path"_s).toString().endsWith(u"Dom.md"_s);
        }));
        qInfo() << "undo:" << chats->undoChanges(path);
        QTRY_COMPARE_WITH_TIMEOUT(ws->read(u"Dom.md"_s), dom, 10000);
    }

    // A plan to approve from a real agent in its plan mode (Claude Code: BRULION_REAL_ACP_AGENT,
    // _OPTION as in realAgent(); docs/UI.md "AI chat" — plans to approve): the turn asks with the
    // plan, which is saved as a page, and once approved the agent carries it out.
    void realAgentPlanApproved()
    {
        const QString command = qEnvironmentVariable("BRULION_REAL_ACP_AGENT");
        if (command.isEmpty())
            QSKIP("no real agent named (BRULION_REAL_ACP_AGENT)");
        auto ws = open({AgentConfig{u"real"_s, u"Real"_s, command, true}});
        WorkspaceChats *chats = ws->chats();
        QSignalSpy checked(chats, &WorkspaceChats::agentChecked);
        chats->checkAgent(u"real"_s);
        QTRY_COMPARE_WITH_TIMEOUT(checked.size(), 1, 240000);
        if (const QString option = qEnvironmentVariable("BRULION_REAL_ACP_OPTION"); option.contains(u'='))
            chats->setAgentOption(QString(), u"acp:real"_s, option.section(u'=', 0, 0), option.section(u'=', 1));
        QString planMode;
        for (const QVariant &c : chats->agentChoices(QString(), u"acp:real"_s, u"mode"_s)) {
            const QString value = c.toMap().value(u"value"_s).toString();
            if (value.contains(u"plan"_s, Qt::CaseInsensitive))
                planMode = value;
        }
        if (planMode.isEmpty())
            QSKIP("the agent has no plan mode");
        chats->setAgentOption(QString(), u"acp:real"_s, u"mode"_s, planMode);
        const QString path = chats->send(QString(),
                                         u"Zaplanuj w dwóch krokach dopisanie na końcu pliku Dom.md linii "
                                         u"\"- [ ] Kupić mleko\", a po zatwierdzeniu planu wykonaj go."_s,
                                         QString(), {}, u"acp:real"_s);
        QVERIFY(!path.isEmpty());
        QVERIFY2(QTest::qWaitFor([&] { return !chats->questionPlan(path).isEmpty() || !chats->turn(path)->running(); },
                                 600000),
                 "no plan asked for in time");
        qInfo().noquote() << ws->read(path);
        const QString plan = chats->questionPlan(path);
        QVERIFY2(!plan.isEmpty(), "the turn ended without a plan to approve");
        const QString saved = chats->savePlan(path);
        QVERIFY(!saved.isEmpty());
        QVERIFY(ws->read(saved).contains(plan.left(20)));
        const QStringList options = chats->questionOptions(path);
        qInfo() << "plan options:" << options;
        QString approve;
        for (const QString &o : options)
            if (!o.contains(u"keep"_s, Qt::CaseInsensitive) && !o.contains(u"no"_s, Qt::CaseInsensitive)) {
                approve = o;
                break;
            }
        QVERIFY(!approve.isEmpty());
        // Its edits after the plan, allowed as they come.
        QTimer answers;
        answers.setInterval(200);
        QObject::connect(&answers, &QTimer::timeout, chats, [&] {
            if (!chats->asking(path))
                return;
            const QStringList more = chats->questionOptions(path);
            QString allow = more.value(0);
            for (const QString &o : more)
                if (o.contains(u"allow"_s, Qt::CaseInsensitive) && !o.contains(u"always"_s, Qt::CaseInsensitive)) {
                    allow = o;
                    break;
                }
            chats->answer(path, allow);
        });
        chats->answer(path, approve);
        answers.start();
        QTRY_VERIFY_WITH_TIMEOUT(!chats->turn(path)->running(), 600000);
        qInfo().noquote() << ws->read(path).section(u"🧑"_s, -1);
        QCOMPARE(chats->turn(path)->failure(), QString());
        QCOMPARE(chats->questionPlan(path), QString());
        QTRY_VERIFY_WITH_TIMEOUT(ws->read(u"Dom.md"_s).contains(u"Kupić mleko"_s), 10000);
    }

    // A real agent's thread in a real git project (docs/CODE.md), when one is named:
    // BRULION_REAL_ACP_AGENT="npx -y @agentclientprotocol/claude-agent-acp" (or codex-acp). The agent fixes a bug in
    // the project's file — its questions (may it edit?) allowed —, the turn's checkpoint is taken before it, and git
    // shows exactly that file changed since the checkpoint; a second turn in the same session knows the first.
    void realAgentThread()
    {
        const QString command = qEnvironmentVariable("BRULION_REAL_ACP_AGENT");
        if (command.isEmpty())
            QSKIP("no real agent named (BRULION_REAL_ACP_AGENT)");
        if (QStandardPaths::findExecutable(u"git"_s).isEmpty())
            QSKIP("git is not installed");
        auto ws = open({AgentConfig{u"real"_s, u"Real"_s, command, true}});
        WorkspaceChats *chats = ws->chats();
        shell::CodeProjects *projects = ws->projects();
        QByteArray kept;
        projects->setStorage([&kept] { return kept; }, [&kept](const QByteArray &json) { kept = json; });
        const QString folder = m_tmp.filePath(u"Kalkulator %1"_s.arg(m_n));
        QVERIFY(writeRaw(folder + u"/calc.py"_s, "def add(a, b):\n    return a - b\n\n\ndef mul(a, b):\n    return a * b\n"));
        QVERIFY(writeRaw(folder + u"/README.md"_s, "# Kalkulator\n\nDodawanie i mnożenie.\n"));
        for (const QStringList &args :
             {QStringList{u"init"_s, u"-q"_s, u"-b"_s, u"main"_s}, QStringList{u"add"_s, u"-A"_s},
              QStringList{u"-c"_s, u"user.name=Ala"_s, u"-c"_s, u"user.email=a@b.c"_s, u"commit"_s, u"-qm"_s, u"start"_s}})
            QCOMPARE(QProcess::execute(u"git"_s, QStringList{u"-C"_s, folder} + args), 0);
        const QString id = projects->add(folder);
        QVERIFY(!id.isEmpty());
        // An option chosen before the first message (BRULION_REAL_ACP_OPTION="model=…"), as in realAgent().
        if (const QString option = qEnvironmentVariable("BRULION_REAL_ACP_OPTION"); option.contains(u'=')) {
            QSignalSpy checked(chats, &WorkspaceChats::agentChecked);
            chats->checkAgent(u"real"_s);
            QTRY_COMPARE_WITH_TIMEOUT(checked.size(), 1, 240000);
            const QString name = option.section(u'=', 0, 0);
            qInfo() << "choices of" << name << chats->agentChoices(QString(), u"acp:real"_s, name);
            chats->setAgentOption(QString(), u"acp:real"_s, name, option.section(u'=', 1));
        }

        // Its questions (may it edit calc.py?) allowed as they come.
        QString path;
        QStringList asked;
        QTimer answers;
        answers.setInterval(200);
        QObject::connect(&answers, &QTimer::timeout, chats, [&] {
            if (path.isEmpty() || !chats->asking(path))
                return;
            const QStringList options = chats->questionOptions(path);
            asked << chats->questionText(path);
            QString allow = options.value(0);
            for (const QString &o : options) {
                if (o.contains(u"allow"_s, Qt::CaseInsensitive) && !o.contains(u"always"_s, Qt::CaseInsensitive)) {
                    allow = o;
                    break;
                }
            }
            chats->answer(path, allow);
        });
        answers.start();
        path = chats->startThread(id,
                                  u"W pliku calc.py funkcja add odejmuje zamiast dodawać. Popraw tylko ją — nie "
                                  u"zmieniaj innych plików, niczego nie uruchamiaj, nie commituj — i odpowiedz "
                                  u"jednym krótkim zdaniem po polsku."_s,
                                  {}, u"acp:real"_s);
        QVERIFY(!path.isEmpty());
        QTRY_VERIFY_WITH_TIMEOUT(!chats->turn(path)->running(), 600000);
        qInfo().noquote() << ws->read(path);
        qInfo() << "asked:" << asked;
        QCOMPARE(chats->turn(path)->failure(), QString());
        QFile calc(folder + u"/calc.py"_s);
        QVERIFY(calc.open(QIODevice::ReadOnly));
        const QByteArray fixed = calc.readAll();
        QVERIFY2(fixed.contains("a + b") && fixed.contains("a * b"), fixed.constData());
        // The turn's checkpoint, and git's view of what the turn changed: calc.py alone.
        QCOMPARE(chats->checkpoints(path).size(), 1);
        const QString checkpoint = chats->lastCheckpoint(path);
        QProcess diff;
        diff.start(u"git"_s, {u"-C"_s, folder, u"diff"_s, u"--name-only"_s, u"refs/brulion/checkpoints/"_s + checkpoint});
        QVERIFY(diff.waitForFinished(30000));
        QCOMPARE(QString::fromUtf8(diff.readAllStandardOutput()).trimmed(), u"calc.py"_s);
        QVERIFY(!ws->read(path).contains(u"🤖 \n"_s));
        qInfo() << "usage:" << chats->agentUsage(path);

        // A second turn: the same session remembers the first; a second checkpoint.
        QCOMPARE(chats->send(path, u"Którą funkcję poprawiłeś? Odpowiedz samą nazwą funkcji."_s), path);
        QTRY_VERIFY_WITH_TIMEOUT(!chats->turn(path)->running(), 600000);
        QCOMPARE(chats->turn(path)->failure(), QString());
        const QString page = ws->read(path);
        qInfo().noquote() << page;
        QVERIFY(page.section(u"🤖"_s, -1).contains(u"add"_s));
        QCOMPARE(chats->checkpoints(path).size(), 2);
    }

    // Scheduled conversations and their webhooks with a real agent, when one is named
    // (BRULION_REAL_ACP_AGENT, _OPTION; docs/UI.md "Scheduled conversations"): a rule's time comes
    // and the agent answers in a new conversation; a webhook's body reaches it as data.
    void realAgentScheduledAndHooked()
    {
        const QString command = qEnvironmentVariable("BRULION_REAL_ACP_AGENT");
        if (command.isEmpty())
            QSKIP("no real agent named (BRULION_REAL_ACP_AGENT)");
        AgentConfig agent{u"real"_s, u"Real"_s, command, true};
        if (const QString o = qEnvironmentVariable("BRULION_REAL_ACP_OPTION"); o.contains(u'='))
            agent.options = QJsonObject{{o.section(u'=', 0, 0), o.section(u'=', 1)}};
        auto ws = open({agent});
        WorkspaceChats *chats = ws->chats();
        ChatSchedules *schedules = chats->schedules();
        QDateTime now(QDate(2026, 11, 20), QTime(9, 15));
        schedules->setClock([&now] { return now; });
        QByteArray kept;
        schedules->setStorage([&kept] { return kept; }, [&kept](const QByteArray &json) { kept = json; });
        QSignalSpy ran(schedules, &ChatSchedules::ran);

        // Its time comes: a new conversation, answered.
        const QString id =
                schedules->add(u"Plan dnia"_s, u"Jakie zadanie jest w notatce Dom? Odpowiedz krótko, jednym zdaniem."_s,
                               u"acp:real"_s, u"codziennie o 7:30"_s, false, true);
        QVERIFY2(!id.isEmpty(), qPrintable(schedules->lastError()));
        now = QDateTime(QDate(2026, 11, 21), QTime(7, 30, 20));
        schedules->runDue();
        QCOMPARE(ran.size(), 1);
        const QString morning = ran.first().at(2).toString();
        QTRY_VERIFY_WITH_TIMEOUT(!chats->isRunning(morning), 600000);
        qInfo().noquote() << ws->read(morning);
        QCOMPARE(chats->turn(morning)->failure(), QString());
        QVERIFY2(chats->lastAnswer(morning).contains(u"czynsz"_s, Qt::CaseInsensitive),
                 qPrintable(chats->lastAnswer(morning)));

        // A webhook's body: data in the conversation, read by the agent.
        const QString token = schedules->newHookToken();
        const QString hooked = schedules->save(QString(), u"Dzwonek"_s,
                                               u"Ktoś dzwoni do drzwi. Podaj samo imię osoby z pola „kto” danych."_s,
                                               u"acp:real"_s, QString(), false, true, token);
        QVERIFY2(!hooked.isEmpty(), qPrintable(schedules->lastError()));
        webhook::Body body;
        body.kind = webhook::BodyKind::Json;
        body.text = u"{\n    \"kto\": \"Zenon Żółkiewski\"\n}"_s;
        body.bytes = int(body.text.toUtf8().size());
        const ChatSchedules::HookOutcome out = schedules->runHook({token, body});
        QCOMPARE(out.status, u"started"_s);
        QCOMPARE(ran.size(), 2);
        const QString door = ran.last().at(2).toString();
        QTRY_VERIFY_WITH_TIMEOUT(!chats->isRunning(door), 600000);
        qInfo().noquote() << ws->read(door);
        QCOMPARE(chats->turn(door)->failure(), QString());
        QVERIFY2(chats->lastAnswer(door).contains(u"Zenon"_s), qPrintable(chats->lastAnswer(door)));
    }

    // The usage of a real agent read from its own session logs on this computer (docs/UI.md "Its
    // usage"), when one is named (BRULION_REAL_ACP_AGENT, _OPTION: Claude Code or Codex): after a
    // turn in this notebook, today's usage of this notebook is not empty.
    void realAgentUsageCounted()
    {
        const QString command = qEnvironmentVariable("BRULION_REAL_ACP_AGENT");
        if (command.isEmpty())
            QSKIP("no real agent named (BRULION_REAL_ACP_AGENT)");
        AgentConfig agent{u"real"_s, u"Real"_s, command, true};
        if (const QString o = qEnvironmentVariable("BRULION_REAL_ACP_OPTION"); o.contains(u'='))
            agent.options = QJsonObject{{o.section(u'=', 0, 0), o.section(u'=', 1)}};
        auto ws = open({agent});
        WorkspaceChats *chats = ws->chats();
        shell::AgentUsage *usage = chats->usage();
        if (!usage->applies(u"acp:real"_s))
            QSKIP("not Claude Code or Codex: no logs to read");
        usage->setCacheDir(m_tmp.filePath(u"usage-%1"_s.arg(m_n)));
        const QString path =
                chats->send(QString(), u"Ile to jest 2 + 2? Odpowiedz samą liczbą."_s, QString(), {}, u"acp:real"_s);
        QVERIFY(!path.isEmpty());
        QTRY_VERIFY_WITH_TIMEOUT(!chats->turn(path)->running(), 600000);
        QCOMPARE(chats->turn(path)->failure(), QString());
        usage->refresh(u"acp:real"_s);
        QTRY_VERIFY_WITH_TIMEOUT(!usage->state(u"acp:real"_s).value(u"busy"_s).toBool()
                                         && usage->state(u"acp:real"_s).value(u"read"_s).toBool(),
                                 300000);
        const QVariantMap state = usage->state(u"acp:real"_s);
        qInfo() << "usage:" << state.value(u"summary"_s).toString() << state.value(u"failure"_s).toString();
        QCOMPARE(state.value(u"failure"_s).toString(), QString());
        const QVariantMap today = usage->details(u"acp:real"_s, 1, true);
        qInfo() << "this notebook today:" << today.value(u"total"_s) << today.value(u"models"_s);
        QVERIFY2(!today.value(u"empty"_s).toBool(), qPrintable(today.value(u"emptyText"_s).toString()));
    }

    // A secret a real agent asks for (docs/UI.md "A secret the agent asks for"), when one is named
    // (BRULION_REAL_ACP_AGENT, _OPTION): it calls brulion_app_chat_request_secret through the real
    // brulion-cli, the card is answered, the agent reads the file in a command — and the value is
    // never in the conversation's page.
    void realAgentAsksForASecret()
    {
#if !defined(BRULION_CLI) || !defined(BRULION_TEST_IPC)
        QSKIP("brulion-cli or the control socket is not built here");
#else
        const QString command = qEnvironmentVariable("BRULION_REAL_ACP_AGENT");
        if (command.isEmpty())
            QSKIP("no real agent named (BRULION_REAL_ACP_AGENT)");
        auto restorePath = cliFirstOnPath();
        AgentConfig agent{u"real"_s, u"Real"_s, command, true};
        if (const QString o = qEnvironmentVariable("BRULION_REAL_ACP_OPTION"); o.contains(u'='))
            agent.options = QJsonObject{{o.section(u'=', 0, 0), o.section(u'=', 1)}};
        auto ws = open({agent});
        WorkspaceChats *chats = ws->chats();
        ChatSecrets *secrets = chats->secrets();
        secrets->setBase(m_tmp.filePath(u"secrets-%1"_s.arg(m_n)));
        RealAgentsControl control(ws.get());
        QVERIFY2(control.listen(m_tmp.filePath(u"run-%1"_s.arg(m_n))), qPrintable(control.error()));
        QString path;
        QTimer answers; // its questions (may it use the tool, run the command?) allowed
        answers.setInterval(200);
        QObject::connect(&answers, &QTimer::timeout, chats, [&] {
            if (path.isEmpty() || !chats->asking(path) || secrets->pending(path))
                return;
            const QStringList options = chats->questionOptions(path);
            QString allow = options.value(0);
            for (const QString &o : options)
                if (o.contains(u"allow"_s, Qt::CaseInsensitive) && !o.contains(u"always"_s, Qt::CaseInsensitive)) {
                    allow = o;
                    break;
                }
            qInfo() << "asked:" << chats->questionText(path) << "->" << allow;
            chats->answer(path, allow);
        });
        answers.start();
        const QString value = u"kod-7731-abc"_s; // 12 characters
        path = chats->send(QString(),
                           u"Potrzebujesz kodu testowego, którego nie znasz. Poproś mnie o niego narzędziem Brulionu "
                           u"do sekretów (brulion_app_chat_request_secret, etykieta „Kod testowy”). Gdy go dostaniesz, "
                           u"policz jego znaki jednym poleceniem powłoki na pliku, który dostaniesz, i odpowiedz samą "
                           u"liczbą. Nie pokazuj samego kodu."_s,
                           QString(), {}, u"acp:real"_s);
        QVERIFY(!path.isEmpty());
        QVERIFY2(QTest::qWaitFor([&] { return secrets->pending(path) || !chats->turn(path)->running(); }, 600000),
                 "no secret asked for in time");
        qInfo() << "card:" << secrets->card(path);
        QVERIFY2(secrets->pending(path), qPrintable(ws->read(path)));
        QVERIFY(secrets->send(path, value));
        QTRY_VERIFY_WITH_TIMEOUT(!chats->turn(path)->running(), 600000);
        const QString page = ws->read(path);
        qInfo().noquote() << page;
        QCOMPARE(chats->turn(path)->failure(), QString());
        QVERIFY2(chats->lastAnswer(path).contains(u"12"_s), qPrintable(chats->lastAnswer(path)));
        QVERIFY(!page.contains(value));
#endif
    }

    // A sub-conversation that wakes its parent with real agents (docs/UI.md "Agents delegate"), when
    // both are named: BRULION_REAL_ACP_AGENT the parent's agent, BRULION_REAL_ACP_CHILD the one it
    // gives the task (Claude Code and Codex — "npx -y @agentclientprotocol/claude-agent-acp",
    // "npx -y @agentclientprotocol/codex-acp" — either way round), BRULION_REAL_ACP_OPTION and
    // BRULION_REAL_ACP_CHILD_OPTION an option of each ("model=haiku", "model=gpt-6-luna": as the
    // agent's remembered options). Their tools are the real brulion-cli, reaching this test's control
    // socket with the chat's grants — answered by ChatControl as the app's remote control answers
    // them, the app naming itself to them (setAppInstance()): the parent starts a sub-conversation
    // with wake and ends its turn, the child answers, the parent gets the answer quoted and a turn of
    // its own, and answers with it.
    void realAgentWakesParent()
    {
#if !defined(BRULION_CLI) || !defined(BRULION_TEST_IPC)
        QSKIP("brulion-cli or the control socket is not built here");
#else
        const QString parentCommand = qEnvironmentVariable("BRULION_REAL_ACP_AGENT");
        const QString childCommand = qEnvironmentVariable("BRULION_REAL_ACP_CHILD");
        if (parentCommand.isEmpty() || childCommand.isEmpty())
            QSKIP("no real agents named (BRULION_REAL_ACP_AGENT and BRULION_REAL_ACP_CHILD)");
        auto restorePath = cliFirstOnPath();
        const auto optionOf = [](const char *variable) {
            const QString o = qEnvironmentVariable(variable);
            return o.contains(u'=') ? QJsonObject{{o.section(u'=', 0, 0), o.section(u'=', 1)}} : QJsonObject{};
        };
        AgentConfig parentAgent{u"rodzic"_s, u"Rodzic"_s, parentCommand, true};
        parentAgent.options = optionOf("BRULION_REAL_ACP_OPTION");
        AgentConfig childAgent{u"dziecko"_s, u"Dziecko"_s, childCommand, true};
        childAgent.options = optionOf("BRULION_REAL_ACP_CHILD_OPTION");
        auto ws = open({parentAgent, childAgent});
        WorkspaceChats *chats = ws->chats();

        RealAgentsControl control(ws.get());
        QVERIFY2(control.listen(m_tmp.filePath(u"run"_s)), qPrintable(control.error()));

        // Their questions (may it use the tool?) allowed as they come, in either conversation.
        QString parent;
        QStringList asked;
        const auto allow = [&](const QString &conversation) {
            if (!chats->asking(conversation))
                return;
            const QStringList options = chats->questionOptions(conversation);
            asked << conversation + u": "_s + chats->questionText(conversation);
            QString choice = options.value(0);
            for (const QString &o : options) {
                if (o.contains(u"allow"_s, Qt::CaseInsensitive) && !o.contains(u"always"_s, Qt::CaseInsensitive)) {
                    choice = o;
                    break;
                }
            }
            chats->answer(conversation, choice);
        };
        QTimer answers;
        answers.setInterval(200);
        QObject::connect(&answers, &QTimer::timeout, chats, [&] {
            if (parent.isEmpty())
                return;
            allow(parent);
            for (const QString &child : chats->lineage()->childrenOf(parent))
                allow(child);
        });
        answers.start();
        parent = chats->send(QString(),
                             u"Przekaż zadanie partnerowi czatu: wywołaj narzędzie brulion_app_chat_start z partner "
                             u"\"acp:dziecko\", wake true i tekstem \"Jaka jest stolica Polski? Odpowiedz jednym "
                             u"słowem.\". Nie czekaj na odpowiedź i nie odpowiadaj sam na to pytanie: zakończ turę "
                             u"słowem \"Zlecone\". Gdy przyjdzie wiadomość z odpowiedzią podrozmowy, odpowiedz "
                             u"jednym zdaniem, co odpowiedziała."_s,
                             QString(), {}, u"acp:rodzic"_s);
        QVERIFY(!parent.isEmpty());

        // The parent's turn: a sub-conversation of its own, with wake, and no wait for it.
        QTRY_VERIFY_WITH_TIMEOUT(chats->lineage()->childrenOf(parent).size() == 1, 600000);
        const QString child = chats->lineage()->childrenOf(parent).first();
        QCOMPARE(chats->lineage()->children(parent).first().toMap().value(u"partner"_s).toString(), u"Dziecko"_s);
        // The child answers; the parent is woken with its answer and answers with it.
        const auto answeredAfterWake = [&] {
            const QString page = ws->read(parent);
            const qsizetype wake = page.lastIndexOf(u"↩ Podrozmowa"_s);
            return wake >= 0 && page.indexOf(u"- 🤖"_s, wake) > wake && !chats->isRunning(parent)
                   && chats->queued(parent).isEmpty();
        };
        QTRY_VERIFY_WITH_TIMEOUT(answeredAfterWake(), 900000);
        const QString childPage = ws->read(child);
        const QString parentPage = ws->read(parent);
        qInfo().noquote() << "child:" << child << '\n' << childPage;
        qInfo().noquote() << "parent:" << parent << '\n' << parentPage;
        qInfo() << "asked:" << asked;
        qInfo() << "usage:" << chats->agentUsage(parent) << chats->agentUsage(child);
        QCOMPARE(chats->turn(child)->failure(), QString());
        QCOMPARE(chats->turn(parent)->failure(), QString());
        QVERIFY2(childPage.section(u"🤖"_s, -1).contains(u"Warszaw"_s), qPrintable(childPage));
        const QString wake = parentPage.mid(parentPage.lastIndexOf(u"↩ Podrozmowa"_s));
        QVERIFY2(wake.startsWith(u"↩ Podrozmowa [["_s) && wake.contains(u"odpowiedziała:"_s), qPrintable(wake));
        QVERIFY2(wake.section(u"- 🤖"_s, -1).contains(u"Warszaw"_s), qPrintable(parentPage));
        QVERIFY(!chats->wakesParent(child));
#endif
    }

    void realRemoteAgent()
    {
#ifndef BRULION_CLI
        QSKIP("brulion-cli is not built here");
#else
        // A real agent on "another computer" (brulion-cli acp serve here, with the user's own sign-in:
        // HOME as it is), when one is named: BRULION_REAL_ACP_AGENT="<…>/bin/claude-agent-acp". Paired,
        // asked about the computer's copy of the notebook, then the app ended while it answers a
        // second question and started again: the answer comes from the computer.
        const QString command = qEnvironmentVariable("BRULION_REAL_ACP_AGENT");
        if (command.isEmpty())
            QSKIP("no real agent named (BRULION_REAL_ACP_AGENT)");
        const QString computer = m_tmp.filePath(u"prawdziwy-komputer"_s);
        writeRaw(computer + u"/Dom.md"_s, "- [ ] Naprawić kran w łazience 📅 2026-11-22\n");
        const QString stateFile = m_tmp.filePath(u"prawdziwy-stan/acp-remote.json"_s);
        const QStringList common{u"--vault"_s, computer, u"--state-dir"_s, m_tmp.filePath(u"prawdziwy-urzadzenie"_s)};
        QProcess host;
        host.setProcessChannelMode(QProcess::ForwardedErrorChannel);
        host.start(QStringLiteral(BRULION_CLI), common
                                                        + QStringList{u"acp"_s, u"serve"_s, u"--agent"_s,
                                                                      u"real:Prawdziwy="_s + command, u"--port"_s,
                                                                      u"0"_s, u"--state-file"_s, stateFile});
        QVERIFY(host.waitForStarted(10000));
        QTRY_VERIFY_WITH_TIMEOUT(host.canReadLine(), 30000);
        const QString url = QJsonDocument::fromJson(host.readLine())
                                    .object()
                                    .value(u"result"_s)
                                    .toObject()
                                    .value(u"url"_s)
                                    .toString();
        QProcess pair;
        pair.start(QStringLiteral(BRULION_CLI),
                                  common + QStringList{u"acp"_s, u"pair"_s, u"--state-file"_s, stateFile});
        QVERIFY(pair.waitForFinished(20000));
        const QString code = QJsonDocument::fromJson(pair.readAllStandardOutput())
                                     .object()
                                     .value(u"result"_s)
                                     .toObject()
                                     .value(u"code"_s)
                                     .toString();
        auto ws = open({});
        WorkspaceChats *chats = ws->chats();
        QList<AgentConfig> paired;
        connect(chats, &WorkspaceChats::agentsPaired, this,
                [&paired](const QList<AgentConfig> &agents) { paired = agents; });
        chats->pairAgentHost(url, code);
        QTRY_COMPARE_WITH_TIMEOUT(paired.size(), 1, 30000);
        AgentConfig agent = paired.first();
        agent.id = u"komputer-real"_s;
        chats->setAgents({agent});
        const QString path =
                chats->send(QString(), u"Jakie zadanie jest w notatce Dom? Odpowiedz krótko, jednym zdaniem."_s,
                            QString(), {}, u"acp:komputer-real"_s);
        QTRY_VERIFY_WITH_TIMEOUT(!chats->turn(path)->running(), 300000);
        qInfo().noquote() << ws->read(path);
        QCOMPARE(chats->turn(path)->failure(), QString());
        QVERIFY(ws->read(path).contains(u"kran"_s, Qt::CaseInsensitive));
        // The app ended while the agent answers; started again, it fetches the answer.
        QCOMPARE(chats->send(path, u"Przeczytaj notatkę Dom jeszcze raz i napisz o tym zadaniu wiersz o ośmiu wersach; "
                                   u"na końcu napisz słowo: gotowe"_s),
                 path);
        QTest::qWait(2000);
        if (!chats->turn(path)->running())
            QSKIP("the agent answered before the app could be ended");
        const QString root = ws->rootPath();
        chats->agents()->abandon();
        ws.reset();
        ws = open({agent}, root);
        chats = ws->chats();
        QVERIFY(chats->turn(path)->running());
        QTRY_VERIFY_WITH_TIMEOUT(!chats->turn(path)->running(), 300000);
        qInfo().noquote() << ws->read(path);
        QCOMPARE(chats->turn(path)->failure(), QString());
        QVERIFY(ws->read(path).contains(u"gotowe"_s, Qt::CaseInsensitive));
        ws.reset();
        host.kill(); // (terminate() does not end a console program on Windows)
        QVERIFY(host.waitForFinished(10000));
#endif
    }

    void notSetUp()
    {
        auto ws = open({fake()});
        WorkspaceChats *chats = ws->chats();
        const QString path = chats->send(QString(), u"Halo"_s, QString(), {}, u"acp:nieznany"_s);
        QVERIFY(!path.isEmpty());
        shell::ChatTurn *turn = chats->turn(path);
        QVERIFY(!turn->running());
        QVERIFY2(turn->failure().contains(u"nieznany"_s), qPrintable(turn->failure()));
        QCOMPARE(chats->partnerLabel(u"acp:nieznany"_s), QString());
        // A command that does not run.
        chats->setAgents({AgentConfig{u"zly"_s, u"Zły"_s, u"/nonexistent/agent --acp"_s, false}});
        QVERIFY(chats->setPartner(path, u"acp:zly"_s));
        QVERIFY(chats->retry(path));
        QTRY_VERIFY_WITH_TIMEOUT(!turn->running(), 20000);
        QVERIFY2(turn->failure().contains(u"Zły"_s), qPrintable(turn->failure()));
    }

    void partners()
    {
        auto ws = open({fake(), AgentConfig{u"claude"_s, u"Claude Code"_s, u"npx -y x"_s, true}});
        WorkspaceChats *chats = ws->chats();
        const QVariantList partners = chats->partners();
        QVERIFY(partners.size() >= 3); // (the assistant first)
        QCOMPARE(partners.at(0).toMap().value(u"id"_s).toString(), QString());
        QCOMPARE(partners.at(partners.size() - 2).toMap().value(u"id"_s).toString(), u"acp:fake"_s);
        QCOMPARE(partners.last().toMap().value(u"label"_s).toString(), u"Claude Code"_s);
        QCOMPARE(chats->partnerLabel(u"acp:claude"_s), u"Claude Code"_s);
        QSignalSpy changed(chats, &WorkspaceChats::partnersChanged);
        chats->setAgents({fake()});
        QCOMPARE(changed.size(), 1);
        chats->setAgents({fake()});
        QCOMPARE(changed.size(), 1);
    }

    void textBefore_data()
    {
        QTest::addColumn<QString>("snapshot");
        QTest::addColumn<QString>("oldText");
        QTest::addColumn<QString>("newText");
        QTest::addColumn<bool>("hasDiff");
        QTest::addColumn<bool>("done");
        QTest::addColumn<QString>("before");
        QTest::addColumn<bool>("known");
        QTest::newRow("no diff") << u"- a\n"_s << QString() << QString() << false << false << u"- a\n"_s << true;
        QTest::newRow("before the change") << u"- zapłacić czynsz\n"_s << u"czynsz"_s << u"czynsz i prąd"_s << true << false
                                           << u"- zapłacić czynsz\n"_s << true;
        QTest::newRow("after it: turned back") << u"- zapłacić prąd\n"_s << u"czynsz"_s << u"prąd"_s << true << true
                                               << u"- zapłacić czynsz\n"_s << true;
        QTest::newRow("a whole file written") << u"- nowe\n"_s << u"- stare\n"_s << u"- nowe\n"_s << true << true
                                              << u"- stare\n"_s << true;
        QTest::newRow("the new text twice: not sure") << u"ż ż"_s << u"a"_s << u"ż"_s << true << true << u"ż ż"_s << false;
        QTest::newRow("done, neither text there") << u"inne"_s << u"a"_s << u"b"_s << true << true << u"inne"_s << false;
        QTest::newRow("pending, neither there yet") << u"inne"_s << u"a"_s << u"b"_s << true << false << u"inne"_s << true;
    }
    void textBefore()
    {
        QFETCH(QString, snapshot);
        QFETCH(QString, oldText);
        QFETCH(QString, newText);
        QFETCH(bool, hasDiff);
        QFETCH(bool, done);
        QFETCH(QString, before);
        QFETCH(bool, known);
        bool isKnown = false;
        const std::optional<acp::Diff> diff = hasDiff ? std::optional<acp::Diff>(acp::Diff{u"x.md"_s, oldText, newText})
                                                      : std::nullopt;
        QCOMPARE(WorkspaceAgents::textBefore(snapshot, diff, done, &isKnown), before);
        QCOMPARE(isKnown, known);
    }

    void mentionAt_data()
    {
        QTest::addColumn<QString>("text");
        QTest::addColumn<int>("caret");
        QTest::addColumn<int>("start");
        QTest::addColumn<QString>("query");
        QTest::newRow("@ at the start") << u"@Do"_s << 3 << 0 << u"Do"_s;
        QTest::newRow("@ after a space") << u"Zobacz @Zakupy"_s << 14 << 7 << u"Zakupy"_s;
        QTest::newRow("@ alone") << u"Zobacz @"_s << 8 << 7 << QString();
        QTest::newRow("@ with diacritics") << u"@Łódź"_s << 5 << 0 << u"Łódź"_s;
        QTest::newRow("an address is none") << u"ja@poczta.pl"_s << 12 << -1 << QString();
        QTest::newRow("after the word: none") << u"@Dom i"_s << 6 << -1 << QString();
        QTest::newRow("[[ with spaces") << u"Patrz [[Moje zak"_s << 16 << 6 << u"Moje zak"_s;
        QTest::newRow("[[ empty") << u"[["_s << 2 << 0 << QString();
        QTest::newRow("a closed link: none") << u"[[Dom]] i"_s << 9 << -1 << QString();
        QTest::newRow("an embed: none") << u"![[foto"_s << 7 << -1 << QString();
        QTest::newRow("caret inside") << u"@Dom dalej"_s << 2 << 0 << u"D"_s;
        QTest::newRow("caret out of range") << u"@Dom"_s << 9 << -1 << QString();
    }
    void mentionAt()
    {
        QFETCH(QString, text);
        QFETCH(int, caret);
        QFETCH(int, start);
        QFETCH(QString, query);
        const auto [at, typed] = WorkspaceChats::mentionAt(text, caret);
        QCOMPARE(at, start);
        QCOMPARE(typed, query);
    }

    /// The pages a message names go to the agent with it (their text), the page names offered
    /// while one is written.
    void mentionedPages()
    {
        auto ws = open({fake()});
        WorkspaceChats *chats = ws->chats();
        QVERIFY(ws->write(u"Zakupy.md"_s, u"- mleko\n- chleb\n"_s));
        ws->finishIndexing();
        const QVariantList offered = chats->mentions(QString(), u"Spójrz na @Zak"_s, 14);
        QCOMPARE(offered.size(), 1);
        QCOMPARE(offered.first().toMap().value(u"title"_s).toString(), u"Zakupy"_s);
        QCOMPARE(offered.first().toMap().value(u"start"_s).toInt(), 10);
        QCOMPARE(offered.first().toMap().value(u"end"_s).toInt(), 14);
        QCOMPARE(chats->withMention(u"Spójrz na @Zak dalej"_s, 14, u"Zakupy"_s), u"Spójrz na [[Zakupy]]  dalej"_s);
        QCOMPARE(chats->mentionEnd(u"Spójrz na @Zak dalej"_s, 14, u"Zakupy"_s), 21);
        QCOMPARE(chats->withMention(u"nic"_s, 3, u"Zakupy"_s), QString());
        const QString path = chats->send(QString(), u"Porównaj [[Zakupy]] i [[Dom]] i [[Brak]]"_s, QString(), {}, u"acp:fake"_s);
        shell::ChatTurn *turn = chats->turn(path);
        QTRY_VERIFY_WITH_TIMEOUT(!turn->running(), 20000);
        const QJsonArray prompt = promptOf(logged(u"session/prompt"_s).last());
        QStringList resources;
        for (const QJsonValue &b : prompt) {
            const QJsonObject o = b.toObject();
            if (o.value(u"type"_s).toString() == u"resource_link"_s)
                resources << QFileInfo(QUrl(o.value(u"uri"_s).toString()).toLocalFile()).fileName();
        }
        // (The fake agent takes no embedded context: links, the missing page none.)
        QCOMPARE(resources, (QStringList{u"Zakupy.md"_s, u"Dom.md"_s}));
    }

    // A work log's bounds (WorkspaceChats::workLogEntry): the newest calls, an output's end, a change's
    // texts only while small, a plan's steps.
    void workLogBounds()
    {
        QVariantList tools;
        for (int i = 0; i < WorkspaceChats::MaxWorkLogCalls + 5; ++i)
            tools << QVariantMap{{u"title"_s, u"Krok %1 — zażółć"_s.arg(i)}, {u"kind"_s, u"read"_s},
                                 {u"status"_s, u"completed"_s}};
        QVariantMap last{{u"title"_s, u"Uruchom testy"_s},
                         {u"kind"_s, u"execute"_s},
                         {u"status"_s, u"failed"_s},
                         {u"output"_s, QString(QString(10000, u'x') + u"KONIEC"_s)},
                         {u"diffPath"_s, u"src/żółw.py"_s},
                         {u"diffOld"_s, u"a\n"_s},
                         {u"diffNew"_s, u"b\n"_s}};
        tools << last;
        QVariantMap huge = last;
        huge.insert(u"diffNew"_s, QString(WorkspaceChats::MaxWorkLogDiff, u'y'));
        tools << huge;
        const QJsonObject entry = WorkspaceChats::workLogEntry(4, 75, tools, {QVariantMap{{u"content"_s, u"Napisać testy"_s},
                                                                                          {u"status"_s, u"completed"_s}}});
        QCOMPARE(entry.value(u"n"_s).toInt(), 4);
        QCOMPARE(entry.value(u"seconds"_s).toInt(), 75);
        const QJsonArray calls = entry.value(u"tools"_s).toArray();
        QCOMPARE(calls.size(), WorkspaceChats::MaxWorkLogCalls);
        const QJsonObject kept = calls.at(calls.size() - 2).toObject();
        QCOMPARE(kept.value(u"title"_s).toString(), u"Uruchom testy"_s);
        QVERIFY(kept.value(u"output"_s).toString().endsWith(u"KONIEC"_s));
        QCOMPARE(kept.value(u"output"_s).toString().size(), WorkspaceChats::MaxWorkLogOutput + 1); // (…)
        QCOMPARE(kept.value(u"diffPath"_s).toString(), u"src/żółw.py"_s);
        // (A change too large to keep: its call without it.)
        QVERIFY(!calls.last().toObject().contains(u"diffPath"_s));
        QCOMPARE(entry.value(u"plan"_s).toArray().at(0).toObject().value(u"content"_s).toString(), u"Napisać testy"_s);
    }

    /// The app's conversations as the phone follows them (chatLive) and steers them (chatAnswer,
    /// chatSend's modes, chatStart in a worktree).
    void liveConversations()
    {
        auto ws = open({fake()});
        QDateTime now(QDate(2026, 11, 20), QTime(9, 15));
        ws->setClock([&now] { return now; });
        WorkspaceChats *chats = ws->chats();
        ChatControl control(ws.get());
        const auto live = [](const std::shared_ptr<Answer> &a) {
            std::optional<acp::RemoteChats> out = acp::RemoteChats::fromJson(a->result);
            if (!out)
                qWarning() << "no RemoteChats:" << a->result;
            return out.value_or(acp::RemoteChats{});
        };
        const auto rowOf = [](const acp::RemoteChats &c, const QString &path) {
            for (const acp::RemoteChat &row : c.chats) {
                if (row.path == path)
                    return std::optional<acp::RemoteChat>(row);
            }
            return std::optional<acp::RemoteChat>();
        };
        // Nothing runs: no conversation, a revision (never below the time: it never goes back).
        auto a = ask(control, u"chatLive"_s, {});
        QVERIFY2(a->done && a->ok, qPrintable(a->message));
        QVERIFY(live(a).chats.isEmpty());
        QVERIFY(!live(a).detail);
        const qint64 first = live(a).revision;
        QVERIFY(first >= QDateTime::currentMSecsSinceEpoch() - 600000);
        QCOMPARE(control.revision(), first);
        // A long poll waits for a newer revision; timeout 0 or since -1 answers at once; one past
        // the revision waits for its time.
        QVERIFY(ask(control, u"chatLive"_s, {{u"since"_s, double(first)}, {u"timeout"_s, 0}})->done);
        QVERIFY(ask(control, u"chatLive"_s, {{u"since"_s, -1}, {u"timeout"_s, 30000}})->done);
        QVERIFY(ask(control, u"chatLive"_s, {{u"since"_s, double(first - 1)}})->done);
        auto timed = ask(control, u"chatLive"_s, {{u"since"_s, double(first)}, {u"timeout"_s, 300}});
        QVERIFY(!timed->done);
        QTRY_VERIFY_WITH_TIMEOUT(timed->done, 5000);
        QVERIFY(timed->ok);
        QVERIFY(live(timed).revision >= first);
        // (A wait whose client left is dropped: never answered.)
        bool connected = true;
        auto left = std::make_shared<Answer>();
        ChatControl::Reply leftReply;
        leftReply.ok = [left](const QJsonObject &) { left->done = true; };
        leftReply.fail = [left](const QString &, const QString &, const QJsonObject &) { left->done = true; };
        leftReply.connected = [&connected] { return connected; };
        control.handle(u"chatLive"_s, {{u"since"_s, double(first)}, {u"timeout"_s, 60000}}, {}, leftReply);
        connected = false;
        auto poll = ask(control, u"chatLive"_s, {{u"since"_s, double(first)}, {u"timeout"_s, 30000}});
        QVERIFY(!poll->done);

        // A turn starts: the poll is answered with it, newer.
        const QString path = chats->send(QString(), u"Teraz pokaż, co robisz"_s, QString(), {}, u"acp:fake"_s);
        QTRY_VERIFY_WITH_TIMEOUT(poll->done, 20000);
        QVERIFY(poll->ok);
        QVERIFY(live(poll).revision > first);
        QTest::qWait(1500); // (the dropped wait's client check ran)
        QVERIFY(!left->done);
        // Its plan and tool calls, in full with `path`.
        acp::RemoteChats seen;
        QTRY_VERIFY_WITH_TIMEOUT((seen = live(ask(control, u"chatLive"_s, {{u"path"_s, path}}))).detail
                                         && seen.detail->tools.size() >= 3,
                                 20000);
        const acp::RemoteChatDetail &d = *seen.detail;
        QCOMPARE(d.path, path);
        QCOMPARE(d.state, u"running"_s);
        QCOMPARE(d.messages, 1);
        QCOMPARE(d.tools.at(0), (acp::RemoteChatTool{u"Read Dom.md"_s, u"completed"_s, u"read"_s}));
        QCOMPARE(d.tools.at(1).title, u"Edit Dom.md"_s);
        QCOMPARE(d.tools.at(2).kind, u"execute"_s);
        QCOMPARE(d.plan.size(), 3);
        QCOMPARE(d.plan.at(1), (acp::RemoteChatStep{u"Policzyć zadania na ten tydzień"_s, u"in_progress"_s}));
        QVERIFY(d.partial.contains(u"Sprawdzam notatki"_s));
        QVERIFY(!d.partialCut);
        QVERIFY(d.question.isEmpty());
        std::optional<acp::RemoteChat> row = rowOf(seen, path);
        QVERIFY(row);
        QCOMPARE(row->state, u"running"_s);
        QCOMPARE(row->partner, u"acp:fake"_s);
        QCOMPARE(row->partnerName, u"Fałszywy"_s);
        QCOMPARE(row->name, u"Teraz pokaż, co robisz"_s);
        QCOMPARE(row->messages, 1);
        QVERIFY(row->ended.isEmpty());
        // A message written meanwhile waits: counted in its row, its text in full.
        QVERIFY(chats->queue(path, u"Po pierwsze: zażółć"_s));
        QTRY_VERIFY(rowOf(live(ask(control, u"chatLive"_s, {})), path).value_or(acp::RemoteChat{}).waiting == 1);
        seen = live(ask(control, u"chatLive"_s, {{u"path"_s, path}}));
        QCOMPARE(seen.detail->waiting, QStringList{u"Po pierwsze: zażółć"_s});
        // Sent while it runs: refused, in line (mode or the old flag) — or now, steered in.
        a = ask(control, u"chatSend"_s, {{u"path"_s, path}, {u"text"_s, u"Nie teraz"_s}, {u"mode"_s, u"now"_s}});
        QCOMPARE(a->code, u"busy"_s);
        a = ask(control, u"chatSend"_s,
                {{u"path"_s, path}, {u"text"_s, u"Po drugie: gęślą"_s}, {u"mode"_s, u"queue"_s}});
        QVERIFY2(a->ok, qPrintable(a->message));
        QVERIFY(a->result.value(u"queued"_s).toBool());
        QCOMPARE(chats->queued(path).size(), 2);
        a = ask(control, u"chatSend"_s, {{u"path"_s, path}, {u"text"_s, u"Skręć: jaźń"_s}, {u"mode"_s, u"steer"_s}});
        QVERIFY2(a->ok, qPrintable(a->message));
        QVERIFY(a->result.value(u"steered"_s).toBool());
        // (The steered one first, the waiting ones after it.)
        QTRY_VERIFY_WITH_TIMEOUT(!chats->isRunning(path) && chats->queued(path).isEmpty(), 30000);
        const QString page = ws->read(path);
        const qsizetype steered = page.indexOf(u"- 🤖 Odpowiedź na: Skręć: jaźń"_s);
        const qsizetype queuedFirst = page.indexOf(u"- 🤖 Odpowiedź na: Po pierwsze: zażółć"_s);
        QVERIFY2(steered > 0 && queuedFirst > steered && page.contains(u"- 🤖 Odpowiedź na: Po drugie: gęślą"_s),
                 qPrintable(page));
        // Ended: listed a while yet (idle, when), then gone.
        QTRY_VERIFY((row = rowOf(live(ask(control, u"chatLive"_s, {})), path)) && row->state == u"idle");
        const QDateTime ended = QDateTime::fromString(row->ended, Qt::ISODate);
        QVERIFY2(ended.isValid(), qPrintable(row->ended));
        QCOMPARE(ended.toLocalTime(), now);
        QVERIFY2(row->ended.startsWith(u"2026-11-20T09:15:00"_s), qPrintable(row->ended));
        const int messages = int(chat::parse(ws->read(path)).messages.size());
        QCOMPARE(messages, 7); // (the first, then the steered one and the two waiting, each answered)
        QCOMPARE(row->messages, messages);
        now = now.addSecs(60LL * acp::RecentChatMinutes - 30);
        QVERIFY(rowOf(live(ask(control, u"chatLive"_s, {})), path));
        const qint64 before = control.revision();
        now = now.addSecs(60);
        a = ask(control, u"chatLive"_s, {});
        QVERIFY(!rowOf(live(a), path));
        QTRY_VERIFY(control.revision() > before); // (its going is a change too)
        // … but read in full by its path any time; a page that is no conversation has no detail.
        seen = live(ask(control, u"chatLive"_s, {{u"path"_s, path}}));
        QCOMPARE(seen.detail->state, u"idle"_s);
        QCOMPARE(seen.detail->messages, messages);
        QVERIFY(seen.detail->tools.isEmpty());
        QVERIFY(!live(ask(control, u"chatLive"_s, {{u"path"_s, u"Dom.md"_s}})).detail);
        QVERIFY(!live(ask(control, u"chatLive"_s, {{u"path"_s, u"AI/Nie ma.md"_s}})).detail);

        // A question: in its row, in full with its options and diff; answered from here.
        const QString asking = chats->send(QString(), u"Daj mi pozwolenie"_s, QString(), {}, u"acp:fake"_s);
        QTRY_VERIFY_WITH_TIMEOUT(chats->asking(asking), 20000);
        QTRY_VERIFY((seen = live(ask(control, u"chatLive"_s, {{u"path"_s, asking}}))).detail
                    && !seen.detail->question.isEmpty());
        QCOMPARE(seen.detail->state, u"asking"_s);
        QCOMPARE(seen.detail->question.text, u"Fałszywy asks: Write Zgoda.md"_s);
        QCOMPARE(seen.detail->question.details, u"Zgoda.md\n- zgoda"_s);
        QCOMPARE(seen.detail->question.options, (QStringList{u"Allow"_s, u"Reject"_s}));
        QCOMPARE(seen.detail->question.file, u"Zgoda.md"_s);
        QCOMPARE(seen.detail->question.oldText, u"- nic\n"_s);
        QCOMPARE(seen.detail->question.newText, u"- zgoda"_s);
        row = rowOf(seen, asking);
        QVERIFY(row);
        QCOMPARE(row->state, u"asking"_s);
        QCOMPARE(row->question, u"Fałszywy asks: Write Zgoda.md"_s);
        // Not an option, an agent's answer: refused; the user's: answered; then nothing is asked.
        a = ask(control, u"chatAnswer"_s, {{u"path"_s, asking}, {u"option"_s, u"Może później"_s}});
        QCOMPARE(a->code, u"bad_option"_s);
        QCOMPARE(a->details.value(u"options"_s).toArray(), (QJsonArray{u"Allow"_s, u"Reject"_s}));
        a = ask(control, u"chatAnswer"_s, {{u"path"_s, asking}, {u"option"_s, u"Allow"_s}},
                ChatControl::Caller{true, asking, u"Fałszywy"_s});
        QCOMPARE(a->code, u"refused"_s);
        QVERIFY(chats->asking(asking));
        a = ask(control, u"chatAnswer"_s, {{u"path"_s, u"Nie ma takiej"_s}, {u"option"_s, u"Allow"_s}});
        QCOMPARE(a->code, u"not_found"_s);
        a = ask(control, u"chatAnswer"_s, {{u"path"_s, asking}, {u"option"_s, u" Allow "_s}});
        QVERIFY2(a->ok, qPrintable(a->message));
        QVERIFY(a->result.value(u"answered"_s).toBool());
        QCOMPARE(a->result.value(u"option"_s).toString(), u"Allow"_s);
        QTRY_VERIFY_WITH_TIMEOUT(!chats->isRunning(asking), 20000);
        QVERIFY(ws->read(asking).contains(u"- 🤖 Zapisano."_s));
        a = ask(control, u"chatAnswer"_s, {{u"path"_s, asking}, {u"option"_s, u"Allow"_s}});
        QCOMPARE(a->code, u"no_question"_s);
        // A plan to approve: the question's plan.
        QCOMPARE(chats->send(asking, u"Proszę zatwierdź"_s), asking);
        QTRY_VERIFY_WITH_TIMEOUT(chats->asking(asking), 20000);
        QTRY_VERIFY((seen = live(ask(control, u"chatLive"_s, {{u"path"_s, asking}}))).detail
                    && !seen.detail->question.isEmpty());
        QCOMPARE(seen.detail->question.plan, u"# Porządki\n\n1. Połączyć notatki\n2. Usunąć duplikaty"_s);
        QVERIFY(seen.detail->question.file.isEmpty());
        QVERIFY2(ask(control, u"chatAnswer"_s, {{u"path"_s, asking}, {u"option"_s, u"Yes"_s}})->ok,
                 "the plan approved");
        QTRY_VERIFY_WITH_TIMEOUT(!chats->isRunning(asking), 20000);

        // An answer longer than a device is given: its end, cut.
        const QString longer = chats->send(QString(), u"Odpowiadaj długo"_s, QString(), {}, u"acp:fake"_s);
        QTRY_VERIFY_WITH_TIMEOUT((seen = live(ask(control, u"chatLive"_s, {{u"path"_s, longer}}))).detail
                                         && seen.detail->partialCut,
                                 20000);
        QCOMPARE(seen.detail->partial.size(), acp::MaxLiveTextChars);
        QVERIFY(seen.detail->partial.endsWith(u"żżż"_s));
        QVERIFY(!seen.detail->partial.contains(u"Długo"_s));
        // The newest activity first.
        QCOMPARE(seen.chats.first().path, longer);
        a = ask(control, u"chatStop"_s, {{u"path"_s, longer}});
        QVERIFY(a->ok);
        QVERIFY(a->result.value(u"stopped"_s).toBool());
    }

    /// What else the phone does with the app's conversations (acp/appchats.h): the agent's options
    /// of a conversation (chatLive's detail, chatOption), the messages held after Stop (held,
    /// chatResume), pictures sent with a message (stored in the notebook, given to the agent), and the
    /// turns that ended before the app shared its conversations — or before it started again.
    void liveOptionsHeldPictures()
    {
        auto ws = open({fake(false, u"--config"_s)});
        QDateTime now(QDate(2026, 11, 20), QTime(9, 15));
        ws->setClock([&now] { return now; });
        WorkspaceChats *chats = ws->chats();
        const auto live = [](const std::shared_ptr<Answer> &a) {
            return acp::RemoteChats::fromJson(a->result).value_or(acp::RemoteChats{});
        };
        const auto rowOf = [](const acp::RemoteChats &c, const QString &path) {
            for (const acp::RemoteChat &row : c.chats) {
                if (row.path == path)
                    return std::optional<acp::RemoteChat>(row);
            }
            return std::optional<acp::RemoteChat>();
        };
        // A turn that ends before anything shares the app's conversations (no ChatControl yet) …
        const QString path = chats->send(QString(), u"Jaki model?"_s, QString(), {}, u"acp:fake"_s);
        QTRY_VERIFY_WITH_TIMEOUT(!chats->isRunning(path), 20000);
        auto control = std::make_unique<ChatControl>(ws.get());
        // … is listed once it does: ended, when.
        std::optional<acp::RemoteChat> row = rowOf(live(ask(*control, u"chatLive"_s, {})), path);
        QVERIFY(row);
        QCOMPARE(row->state, u"idle"_s);
        QVERIFY2(row->ended.startsWith(u"2026-11-20T09:15:00"_s), qPrintable(row->ended));
        // (The app started again: still.)
        control = std::make_unique<ChatControl>(ws.get());
        QVERIFY(rowOf(live(ask(*control, u"chatLive"_s, {})), path));
        // (Its time over: gone.)
        now = now.addSecs(60LL * acp::RecentChatMinutes + 1);
        QVERIFY(!rowOf(live(ask(*control, u"chatLive"_s, {})), path));
        now = now.addSecs(-60LL * acp::RecentChatMinutes - 1);

        // Its agent's options, as the app has them.
        acp::RemoteChats seen = live(ask(*control, u"chatLive"_s, {{u"path"_s, path}}));
        QVERIFY(seen.detail);
        QCOMPARE(seen.detail->options.size(), 3);
        const acp::RemoteChatOption &model = seen.detail->options.at(0);
        QCOMPARE(model.id, u"model"_s);
        QCOMPARE(model.current, u"sonnet"_s);
        QCOMPARE(model.currentName, u"Sonnet"_s);
        QCOMPARE(model.choices.size(), 2);
        QVERIFY(seen.detail->options.at(1).boolean);
        // Set from the phone: a choice of its, or on/off; anything else refused, with what it takes.
        auto a = ask(*control, u"chatOption"_s, {{u"path"_s, path}, {u"option"_s, u"kolor"_s}, {u"value"_s, u"x"_s}});
        QCOMPARE(a->code, u"bad_option"_s);
        QCOMPARE(a->details.value(u"options"_s).toArray().size(), 3);
        a = ask(*control, u"chatOption"_s, {{u"path"_s, path}, {u"option"_s, u"model"_s}, {u"value"_s, u"gpt"_s}});
        QCOMPARE(a->code, u"bad_option"_s);
        QCOMPARE(a->details.value(u"values"_s).toArray().size(), 2);
        a = ask(*control, u"chatOption"_s, {{u"path"_s, path}, {u"option"_s, u"fast"_s}, {u"value"_s, u"tak"_s}});
        QCOMPARE(a->code, u"bad_option"_s);
        a = ask(*control, u"chatOption"_s, {{u"path"_s, path}, {u"option"_s, u"model"_s}, {u"value"_s, u"opus"_s}});
        QVERIFY2(a->ok, qPrintable(a->message));
        QCOMPARE(a->result.value(u"value"_s).toString(), u"opus"_s);
        QTRY_COMPARE(live(ask(*control, u"chatLive"_s, {{u"path"_s, path}})).detail->options.at(0).current, u"opus"_s);
        // (An agent the app started changes only its own sub-conversations' options.)
        a = ask(*control, u"chatOption"_s, {{u"path"_s, path}, {u"option"_s, u"model"_s}, {u"value"_s, u"sonnet"_s}},
                ChatControl::Caller{true, u"AI/Inna.md"_s, u"Fałszywy"_s});
        QCOMPARE(a->code, u"refused"_s);
        QVERIFY(ask(*control, u"chatSend"_s, {{u"path"_s, path}, {u"text"_s, u"A teraz jaki model?"_s}})->ok);
        QTRY_VERIFY_WITH_TIMEOUT(!chats->isRunning(path), 20000);
        QVERIFY2(ws->read(path).contains(u"- 🤖 Model: opus"_s), qPrintable(ws->read(path)));

        // A message written meanwhile, held when the turn is stopped: listed held, resumed from here.
        QVERIFY(ask(*control, u"chatSend"_s, {{u"path"_s, path}, {u"text"_s, u"Proszę czekaj"_s}})->ok);
        QTRY_VERIFY_WITH_TIMEOUT(chats->isRunning(path), 20000);
        QVERIFY(ask(*control, u"chatSend"_s,
                    {{u"path"_s, path}, {u"text"_s, u"Potem jaki model?"_s}, {u"mode"_s, u"queue"_s}})
                        ->ok);
        a = ask(*control, u"chatResume"_s, {{u"path"_s, path}});
        QCOMPARE(a->code, u"not_held"_s); // (it waits, not held)
        QVERIFY(ask(*control, u"chatStop"_s, {{u"path"_s, path}})->ok);
        QTRY_VERIFY_WITH_TIMEOUT(!chats->isRunning(path), 20000);
        QTRY_VERIFY((row = rowOf(live(ask(*control, u"chatLive"_s, {})), path)) && row->held);
        QCOMPARE(row->waiting, 1);
        seen = live(ask(*control, u"chatLive"_s, {{u"path"_s, path}}));
        QVERIFY(seen.detail->held);
        QCOMPARE(seen.detail->waiting, QStringList{u"Potem jaki model?"_s});
        a = ask(*control, u"chatResume"_s, {{u"path"_s, path}});
        QVERIFY2(a->ok, qPrintable(a->message));
        QVERIFY(a->result.value(u"resumed"_s).toBool());
        QTRY_VERIFY_WITH_TIMEOUT(!chats->isRunning(path) && chats->queued(path).isEmpty(), 20000);
        QVERIFY(ws->read(path).contains(u"- 🤖 Model: opus"_s));
        QVERIFY(!live(ask(*control, u"chatLive"_s, {{u"path"_s, path}})).detail->held);

        // Pictures with a message: stored in the notebook, named by the page, given to the agent; a
        // message of pictures alone is one too; odd ones refused, nothing stored.
        QImage image(4, 3, QImage::Format_RGB32);
        image.fill(Qt::red);
        QByteArray png;
        QBuffer buffer(&png);
        buffer.open(QIODevice::WriteOnly);
        QVERIFY(image.save(&buffer, "PNG"));
        const QJsonArray pictures = acp::toJson(QList<acp::RemotePicture>{{u"paragon.png"_s, png}});
        a = ask(*control, u"chatSend"_s,
                {{u"path"_s, path}, {u"text"_s, u"Co widać? obrazy"_s}, {u"pictures"_s, pictures}});
        QVERIFY2(a->ok, qPrintable(a->message));
        QTRY_VERIFY_WITH_TIMEOUT(!chats->isRunning(path), 20000);
        QString page = ws->read(path);
        QVERIFY2(page.contains(u"paragon.png"_s) && page.contains(u"- 🤖 Obrazy: paragon.png"_s), qPrintable(page));
        const QString stored = chats->picturePath(u"paragon.png"_s, path);
        QVERIFY(!stored.isEmpty());
        QCOMPARE(QFile(ws->absoluteFilePath(stored)).size(), png.size());
        a = ask(*control, u"chatSend"_s, {{u"path"_s, path}, {u"text"_s, QString()}, {u"pictures"_s, pictures}});
        QVERIFY2(a->ok, qPrintable(a->message));
        QTRY_VERIFY_WITH_TIMEOUT(!chats->isRunning(path), 20000);
        a = ask(*control, u"chatSend"_s, {{u"path"_s, path}, {u"text"_s, QString()}});
        QCOMPARE(a->code, u"bad_params"_s);
        a = ask(*control, u"chatSend"_s,
                {{u"path"_s, path},
                 {u"text"_s, u"x"_s},
                 {u"pictures"_s, QJsonArray{QJsonObject{{u"name"_s, u"skrypt.sh"_s}, {u"data"_s, u"eA=="_s}}}}});
        QCOMPARE(a->code, u"bad_pictures"_s);
        a = ask(*control, u"chatStart"_s,
                {{u"text"_s, u"x"_s}, {u"partner"_s, u"acp:fake"_s}, {u"pictures"_s, u"a.png"_s}});
        QCOMPARE(a->code, u"bad_pictures"_s);
        // A new conversation with a picture.
        a = ask(*control, u"chatStart"_s,
                {{u"text"_s, u"Policz obrazy"_s}, {u"partner"_s, u"acp:fake"_s}, {u"pictures"_s, pictures}});
        QVERIFY2(a->ok, qPrintable(a->message));
        const QString started = a->result.value(u"path"_s).toString();
        QTRY_VERIFY_WITH_TIMEOUT(!chats->isRunning(started), 20000);
        page = ws->read(started);
        QVERIFY2(page.contains(u"- 🤖 Obrazy: paragon.png"_s), qPrintable(page));
    }

    /// chatStart of a thread in a worktree of its project: one under the worktrees root, one `git
    /// worktree list` lists (answered once git has listed it), a folder that is none refused; the
    /// project's own folder is no worktree; worktree without a project, or with a parent, refused.
    void liveThreadInWorktree()
    {
        if (!toolsThere())
            QSKIP("no brulion-cli to name");
        if (QStandardPaths::findExecutable(u"git"_s).isEmpty())
            QSKIP("no git");
        auto ws = open({fake(true)});
        WorkspaceChats *chats = ws->chats();
        shell::CodeProjects *projects = ws->projects();
        QByteArray kept;
        projects->setStorage([&kept] { return kept; }, [&kept](const QByteArray &json) { kept = json; });
        projects->setWorktreesRoot(m_tmp.filePath(u"drzewa-%1"_s.arg(m_n)));
        const QString folder = m_tmp.filePath(u"Projekt gęsi %1"_s.arg(m_n));
        QVERIFY(writeRaw(folder + u"/Dom.md"_s, "# README gęsi\n"));
        const auto git = [](const QString &in, const QStringList &args) {
            return QProcess::execute(u"git"_s, QStringList{u"-C"_s, in, u"-c"_s, u"user.name=Ala"_s, u"-c"_s,
                                                           u"user.email=a@b.c"_s, u"-c"_s, u"commit.gpgsign=false"_s}
                                                       + args);
        };
        QCOMPARE(git(folder, {u"init"_s, u"-q"_s, u"-b"_s, u"main"_s}), 0);
        QCOMPARE(git(folder, {u"add"_s, u"-A"_s}), 0);
        QCOMPARE(git(folder, {u"commit"_s, u"-qm"_s, u"start"_s}), 0);
        const QString id = projects->add(folder);
        QVERIFY(!id.isEmpty());
        // A worktree git lists, outside the worktrees root.
        const QString listed = m_tmp.filePath(u"obok-%1"_s.arg(m_n));
        QCOMPARE(git(folder, {u"worktree"_s, u"add"_s, u"-q"_s, u"-b"_s, u"brulion/obok"_s, listed}), 0);
        // One under the root (its folder there is enough).
        const QString underRoot = QDir(projects->worktreesRoot()).filePath(id + u"/pod-korzeniem"_s);
        QVERIFY(QDir().mkpath(underRoot));
        ChatControl control(ws.get());
        const auto start = [&](const QJsonObject &params) {
            auto a = ask(control, u"chatStart"_s, params);
            if (!QTest::qWaitFor([a] { return a->done; }, 20000))
                qWarning() << "chatStart did not answer";
            return a;
        };
        auto a = start({{u"text"_s, u"Popraw testy"_s},
                        {u"project"_s, id},
                        {u"worktree"_s, listed},
                        {u"branch"_s, u"brulion/obok"_s}});
        QVERIFY2(a->ok, qPrintable(a->message));
        const QString inListed = a->result.value(u"path"_s).toString();
        QCOMPARE(chats->threadPlace(inListed).value(u"worktree"_s).toString(), QDir::cleanPath(listed));
        QCOMPARE(chats->threadPlace(inListed).value(u"branch"_s).toString(), u"brulion/obok"_s);
        QTRY_VERIFY_WITH_TIMEOUT(!chats->isRunning(inListed), 20000);
        QCOMPARE(QDir::cleanPath(
                         logged(u"session/new"_s).last().value(u"params"_s).toObject().value(u"cwd"_s).toString()),
                 QDir::cleanPath(listed));
        // Its row says where it works.
        const std::optional<acp::RemoteChats> seen =
                acp::RemoteChats::fromJson(ask(control, u"chatLive"_s, {})->result);
        QVERIFY(seen && !seen->chats.isEmpty());
        QCOMPARE(seen->chats.first().project, id);
        QCOMPARE(seen->chats.first().worktree, QDir::cleanPath(listed));
        QCOMPARE(seen->chats.first().branch, u"brulion/obok"_s);
        a = start({{u"text"_s, u"Pod korzeniem"_s}, {u"project"_s, id}, {u"worktree"_s, underRoot}});
        QVERIFY2(a->ok, qPrintable(a->message));
        QCOMPARE(chats->threadPlace(a->result.value(u"path"_s).toString()).value(u"worktree"_s).toString(),
                 QDir::cleanPath(underRoot));
        QTRY_VERIFY_WITH_TIMEOUT(!chats->isRunning(a->result.value(u"path"_s).toString()), 20000);
        // The project's own folder: no worktree of its own.
        a = start({{u"text"_s, u"W projekcie"_s}, {u"project"_s, id}, {u"worktree"_s, QString(folder + u"/"_s)}});
        QVERIFY2(a->ok, qPrintable(a->message));
        QCOMPARE(chats->threadPlace(a->result.value(u"path"_s).toString()).value(u"worktree"_s).toString(), QString());
        QTRY_VERIFY_WITH_TIMEOUT(!chats->isRunning(a->result.value(u"path"_s).toString()), 20000);
        // Refused: a folder that is no worktree of it, one that is not there, a relative one, without
        // a project, as a sub-conversation.
        const QString stranger = m_tmp.filePath(u"obcy-%1"_s.arg(m_n));
        QVERIFY(QDir().mkpath(stranger));
        for (const QString &bad : {stranger, m_tmp.filePath(u"nie-ma-%1"_s.arg(m_n)), u"wzgledny/folder"_s}) {
            a = start({{u"text"_s, u"Nie tu"_s}, {u"project"_s, id}, {u"worktree"_s, bad}});
            QCOMPARE(a->code, u"bad_params"_s);
            QCOMPARE(a->details.value(u"param"_s).toString(), u"worktree"_s);
        }
        a = start({{u"text"_s, u"Bez projektu"_s}, {u"worktree"_s, listed}});
        QCOMPARE(a->code, u"bad_params"_s);
        a = start(
                {{u"text"_s, u"Jako dziecko"_s}, {u"project"_s, id}, {u"worktree"_s, listed}, {u"parent"_s, inListed}});
        QCOMPARE(a->code, u"bad_params"_s);
    }

    void autoAnswers()
    {
        acp::Permission edit{u"Edit Dom.md"_s, QString(), {{u"a"_s, u"Allow"_s, u"allow_once"_s}, {u"r"_s, u"Reject"_s, u"reject_once"_s}}};
        edit.kind = u"edit"_s;
        QCOMPARE(WorkspaceAgents::autoAnswer(u"ask"_s, edit), QString());
        QCOMPARE(WorkspaceAgents::autoAnswer(u"edits"_s, edit), u"a"_s);
        QCOMPARE(WorkspaceAgents::autoAnswer(u"all"_s, edit), u"a"_s);
        acp::Permission run = edit;
        run.kind = u"execute"_s;
        QCOMPARE(WorkspaceAgents::autoAnswer(u"edits"_s, run), QString());
        QCOMPARE(WorkspaceAgents::autoAnswer(u"all"_s, run), u"a"_s);
        // Only allow_always offered: that one.
        acp::Permission always{u"Fetch"_s, QString(), {{u"aa"_s, u"Always"_s, u"allow_always"_s}, {u"r"_s, u"No"_s, u"reject_once"_s}}};
        always.kind = u"fetch"_s;
        QCOMPARE(WorkspaceAgents::autoAnswer(u"all"_s, always), u"aa"_s);
        // Never a plan, never without an allow.
        acp::Permission plan = edit;
        plan.kind = u"switch_mode"_s;
        QCOMPARE(WorkspaceAgents::autoAnswer(u"all"_s, plan), QString());
        plan.kind = u"edit"_s;
        plan.plan = u"# Plan"_s;
        QCOMPARE(WorkspaceAgents::autoAnswer(u"all"_s, plan), QString());
        acp::Permission rejectOnly{u"x"_s, QString(), {{u"r"_s, u"No"_s, u"reject_once"_s}}};
        QCOMPARE(WorkspaceAgents::autoAnswer(u"all"_s, rejectOnly), QString());
    }

    void plainOutput_data()
    {
        QTest::addColumn<QString>("text");
        QTest::addColumn<QString>("expected");
        QTest::newRow("console") << u"```console\nok\nżółw\n```"_s << u"ok\nżółw"_s;
        QTest::newRow("spaces") << u"  ```\n$ ls\n```  \n"_s << u"$ ls"_s;
        QTest::newRow("plain") << u"zwykły tekst"_s << u"zwykły tekst"_s;
        QTest::newRow("two blocks") << u"```\na\n```\n```\nb\n```"_s << u"```\na\n```\n```\nb\n```"_s;
        QTest::newRow("text after") << u"```\na\n```\npotem"_s << u"```\na\n```\npotem"_s;
        QTest::newRow("one line") << u"```ok```"_s << u"```ok```"_s;
        QTest::newRow("empty block") << u"```\n```"_s << u"```\n```"_s;
    }
    void plainOutput()
    {
        QFETCH(QString, text);
        QFETCH(QString, expected);
        QCOMPARE(WorkspaceAgents::plainOutput(text), expected);
    }

    void pureParts()
    {
        const QString context = WorkspaceAgents::context(u"/home/ala/Notatki"_s, u"AI/2026-11-20 Pytanie"_s, QString(), true);
        QVERIFY(context.contains(u"/home/ala/Notatki"_s));
        QVERIFY(context.contains(u"MCP server \"brulion\""_s));
        QVERIFY(!context.contains(u"about the note"_s));
        // Answers may draw: the chat shows a chart, a diagram and a formula as the editor does.
        QVERIFY(context.contains(u"```mermaid"_s) && context.contains(u"```chart"_s) && context.contains(u"$$…$$"_s));
        // With the chat's tools: a secret is asked for in a private card, never in the chat.
        QVERIFY(!context.contains(u"brulion_app_chat_request_secret"_s));
        QVERIFY(WorkspaceAgents::context(u"/n"_s, u"AI/x"_s, QString(), true, false, true)
                        .contains(u"brulion_app_chat_request_secret"_s));
        // … and the user told with a notification when they asked to be.
        QVERIFY(!context.contains(u"brulion_app_chat_notify"_s));
        QVERIFY(WorkspaceAgents::context(u"/n"_s, u"AI/x"_s, QString(), true, false, true)
                        .contains(u"brulion_app_chat_notify"_s));
        QCOMPARE(WorkspaceAgents::transcript({QVariantMap{{u"role"_s, u"user"_s}, {u"content"_s, u"Tylko to"_s}}}), QString());
        QCOMPARE(WorkspaceAgents::transcript({QVariantMap{{u"role"_s, u"user"_s}, {u"content"_s, u"Żółw?"_s}},
                                              QVariantMap{{u"role"_s, u"assistant"_s}, {u"content"_s, u" Je sałatę. "_s}},
                                              QVariantMap{{u"role"_s, u"user"_s}, {u"content"_s, u"A teraz?"_s}}}),
                 u"The conversation so far (🧑 the user's messages, 🤖 your answers):\n\n🧑 Żółw?\n\n🤖 Je sałatę."_s);
        QVERIFY(WorkspaceAgents::isAgent(u" acp:claude"_s));
        QVERIFY(!WorkspaceAgents::isAgent(u"pirat/rozmowa"_s));
        QCOMPARE(WorkspaceAgents::presets().size(), 4);
        // Windows: a batch file through cmd.exe, a program found on PATH, nothing changed elsewhere.
        const auto npxCmd = [](const QString &) { return u"C:/nodejs/npx.cmd"_s; };
        QCOMPARE(WorkspaceAgents::commandLine(u"npx -y \"@agentclientprotocol/claude-agent-acp\""_s, true, npxCmd),
                 (QStringList{u"cmd.exe"_s, u"/c"_s, QDir::toNativeSeparators(u"C:/nodejs/npx.cmd"_s), u"-y"_s,
                              u"@agentclientprotocol/claude-agent-acp"_s}));
        QCOMPARE(WorkspaceAgents::commandLine(u"gemini --acp"_s, true, [](const QString &) { return u"C:/bin/gemini.exe"_s; }),
                 (QStringList{u"C:/bin/gemini.exe"_s, u"--acp"_s}));
        QCOMPARE(WorkspaceAgents::commandLine(u"npx -y x"_s, false, npxCmd), (QStringList{u"npx"_s, u"-y"_s, u"x"_s}));
        QCOMPARE(WorkspaceAgents::commandLine(u"nieznany --acp"_s, true, [](const QString &) { return QString(); }),
                 (QStringList{u"nieznany"_s, u"--acp"_s}));
        // fs/read_text_file's lines.
        const QString text = u"pierwsza\nżółta\ntrzecia\n"_s;
        QCOMPARE(WorkspaceAgents::lines(text, 0, 0), text);
        QCOMPARE(WorkspaceAgents::lines(text, 2, 1), u"żółta\n"_s);
        QCOMPARE(WorkspaceAgents::lines(text, 2, 0), u"żółta\ntrzecia\n"_s);
        QCOMPARE(WorkspaceAgents::lines(text, 3, 5), u"trzecia\n"_s);
        QCOMPARE(WorkspaceAgents::lines(text, 9, 1), QString());
        // A terminal's output kept to its end, never half a character.
        bool cut = false;
        QCOMPARE(WorkspaceAgents::keepLast("abc", 10, &cut), QByteArray("abc"));
        QVERIFY(!cut);
        QCOMPARE(WorkspaceAgents::keepLast(u"xąę"_s.toUtf8(), 3, &cut), u"ę"_s.toUtf8()); // ("ą" would be cut)
        QVERIFY(cut);
        // Usage.
        QCOMPARE(WorkspaceAgents::usageText({}), QString());
        QCOMPARE(WorkspaceAgents::usageText({42552, 1000000, 0.2096, u"USD"_s}), u"4.3 % of the context · 0.21 USD"_s);
        QCOMPARE(WorkspaceAgents::usageText({40000, 200000, -1, QString()}), u"20 % of the context"_s);
    }

    void remoteAgentChangesTheApp()
    {
#ifndef BRULION_CLI
        QSKIP("brulion-cli is not built here");
#else
        const QString computer = m_tmp.filePath(u"komputer-aplikacja"_s);
        writeRaw(computer + u"/Dom.md"_s, "- na komputerze\n");
        const QString stateFile = m_tmp.filePath(u"komputer-aplikacja-stan/acp-remote.json"_s);
        auto ws = open({}); // (FAKE_ACP_LOG for the agent the computer starts)
        QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
        for (const char *name : {"HOME", "XDG_CONFIG_HOME", "XDG_DATA_HOME", "XDG_CACHE_HOME"})
            env.insert(QString::fromLatin1(name),
                       m_tmp.filePath(u"komputer-aplikacja-dom/"_s + QString::fromLatin1(name)));
        const QString runtime = m_tmp.filePath(u"run-k"_s);
        QDir().mkpath(runtime);
        QFile::setPermissions(runtime, QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner);
        env.insert(u"XDG_RUNTIME_DIR"_s, runtime);
        for (const char *name : {"BRULION_APP_TOKEN", "BRULION_APP_CHAT_TOKEN", "BRULION_APP_SOCKET"})
            env.remove(QString::fromLatin1(name));
        const QStringList common{u"--vault"_s, computer, u"--state-dir"_s,
                                 m_tmp.filePath(u"komputer-aplikacja-urzadzenie"_s)};
        QProcess host;
        host.setProcessEnvironment(env);
        host.setProcessChannelMode(QProcess::ForwardedErrorChannel);
        host.start(QStringLiteral(BRULION_CLI), common
                                                        + QStringList{u"acp"_s, u"serve"_s, u"--agent"_s,
                                                                      u"fake:Fałszywy="_s + command(), u"--port"_s,
                                                                      u"0"_s, u"--state-file"_s, stateFile});
        QVERIFY(host.waitForStarted(10000));
        const auto stopHost = qScopeGuard([&host] {
            host.kill();
            host.waitForFinished(10000);
        });
        QTRY_VERIFY_WITH_TIMEOUT(host.canReadLine(), 20000);
        const QJsonObject started = QJsonDocument::fromJson(host.readLine()).object().value(u"result"_s).toObject();
        QVERIFY2(started.value(u"appTools"_s).toBool(), qPrintable(QString::fromUtf8(QJsonDocument(started).toJson())));
        QProcess pairing;
        pairing.setProcessEnvironment(env);
        pairing.start(QStringLiteral(BRULION_CLI),
                                     common + QStringList{u"acp"_s, u"pair"_s, u"--state-file"_s, stateFile});
        QVERIFY(pairing.waitForFinished(20000));
        const QString code = QJsonDocument::fromJson(pairing.readAllStandardOutput())
                                     .object()
                                     .value(u"result"_s)
                                     .toObject()
                                     .value(u"code"_s)
                                     .toString();

        // This app, answering in process: what it was granted and asked.
        struct Call {
            QString token;
            QString method;
            QJsonObject params;
        };
        QHash<QString, std::pair<QString, bool>> grants; // token → client, chat only
        QStringList revoked;
        QList<Call> calls;
        WorkspaceAgents::AppControl control;
        control.grant = [&grants](const QString &client, bool chatOnly) {
            const QString token = u"token-%1"_s.arg(grants.size() + 1);
            grants.insert(token, {client, chatOnly});
            return token;
        };
        control.revoke = [&revoked](const QString &token) { revoked << token; };
        control.call = [&calls](const QString &token, const QString &method, const QJsonObject &params,
                                std::function<void(const acp::AppAnswer &)> done, std::function<bool()> waiting) {
            QVERIFY(waiting());
            calls.append({token, method, params});
            if (method == u"hello")
                return done(acp::AppAnswer::success(QJsonObject{{u"granted"_s, true}}));
            if (method == u"theme")
                return done(acp::AppAnswer::success(
                        QJsonObject{{u"live"_s, QJsonArray{QJsonObject{{u"who"_s, u"live:Fałszywy"_s},
                                                                       {u"tokens"_s, params.value(u"tokens"_s)}}}}}));
            done(acp::AppAnswer::success(QJsonObject{{u"method"_s, method}, {u"zażółć"_s, true}}));
        };
        WorkspaceChats *chats = ws->chats();
        chats->agents()->setAppControl(control);
        QList<AgentConfig> paired;
        connect(chats, &WorkspaceChats::agentsPaired, this,
                [&paired](const QList<AgentConfig> &agents) { paired = agents; });
        chats->pairAgentHost(started.value(u"url"_s).toString(), code);
        QTRY_COMPARE_WITH_TIMEOUT(paired.size(), 1, 20000);
        AgentConfig agent = paired.first();
        agent.id = u"komputer-aplikacja"_s;
        agent.appAccess = true;
        QVERIFY(agent.tools);
        chats->setAgents({agent});

        const QString path = chats->send(QString(),
                                         u"aplikacja lista && brulion_app_state {} && brulion_app_theme {\"tokens\": "
                                         u"\"{\\\"accent\\\": \\\"#33aa77\\\"}\"} && "
                                         u"brulion_app_chat_partners {}"_s,
                                         QString(), {}, u"acp:komputer-aplikacja"_s);
        shell::ChatTurn *turn = chats->turn(path);
        QTRY_VERIFY_WITH_TIMEOUT(!turn->running(), 60000);
        QCOMPARE(turn->failure(), QString());
        const QString page = ws->read(path);
        QVERIFY2(page.contains(u"Aplikacja: "_s), qPrintable(page));
        QVERIFY2(page.contains(u"brulion_app_theme"_s) && page.contains(u"brulion_app_chat_start"_s), qPrintable(page));
        QVERIFY2(page.contains(u"zażółć"_s), qPrintable(page));
        // A grant of this app for the agent, named for its computer, and its conversation's chat grant.
        const QString client = WorkspaceAgents::remoteClient(agent);
        QCOMPARE(client, u"Fałszywy on %1"_s.arg(agent.host));
        QString full, chat;
        for (auto it = grants.cbegin(); it != grants.cend(); ++it)
            (it->second ? chat : full) = it.key();
        QCOMPARE(grants.size(), 2);
        QCOMPARE(grants.value(full).first, client);
        // Each call with its grant: the live tools with the agent's, the chat's with the conversation's.
        QStringList methods;
        for (const Call &c : std::as_const(calls))
            methods << c.method;
        QCOMPARE(methods, (QStringList{u"state"_s, u"theme"_s, u"chatPartners"_s}));
        QCOMPARE(calls.at(0).token, full);
        QCOMPARE(calls.at(1).params.value(u"tokens"_s).toObject().value(u"accent"_s).toString(), u"#33aa77"_s);
        QCOMPARE(calls.at(2).token, chat);
        QCOMPARE(chats->agents()->grantConversation(chat), path);
        // The computer's agent got the tools in their tunnel mode; the request named a handle, no token.
        const QJsonObject created = logged(u"session/new"_s).last();
        QVERIFY(!created.value(u"params"_s).toObject().contains(u"_meta"_s));
        QVERIFY(serverArgs(logged(u"session/new"_s).size() - 1).contains(u"--app-control"_s));
        const QHash<QString, QString> serverVars = serverEnv(logged(u"session/new"_s).size() - 1);
        // (Its own socket in the runtime folder; on Windows a pipe named after the temporary one.)
        QVERIFY(QFileInfo(serverVars.value(u"BRULION_APP_SOCKET"_s)).fileName().startsWith(u"brulion-acp-"_s));
#ifndef Q_OS_WIN
        QVERIFY(serverVars.value(u"BRULION_APP_SOCKET"_s).startsWith(runtime));
#endif
        QVERIFY(serverVars.value(u"BRULION_APP_TOKEN"_s) != full); // (the computer's own grant, not this app's)
        QCOMPARE(serverVars.value(u"BRULION_APP_DEVICE"_s), WorkspaceAgents::deviceName());
        // The context tells the agent the app is the user's device's.
        QVERIFY(promptOf(logged(u"session/prompt"_s).last())
                        .at(0)
                        .toObject()
                        .value(u"text"_s)
                        .toString()
                        .contains(u"on the user's device"_s));

        // Not let change the app: its process starts again, with the chat's tools alone.
        agent.appAccess = false;
        chats->setAgents({agent});
        QTRY_VERIFY_WITH_TIMEOUT(revoked.contains(full) && revoked.contains(chat), 20000);
        const QString second = chats->send(QString(), u"aplikacja lista"_s, QString(), {}, u"acp:komputer-aplikacja"_s);
        QTRY_VERIFY_WITH_TIMEOUT(!chats->turn(second)->running(), 60000);
        const QString listed = ws->read(second);
        QVERIFY2(listed.contains(u"brulion_app_chat_start"_s), qPrintable(listed));
        QVERIFY2(!listed.contains(u"brulion_app_theme"_s), qPrintable(listed));
        QVERIFY(serverArgs(logged(u"session/new"_s).size() - 1).contains(u"--app-chat"_s));
        QCOMPARE(grants.size(), 3);
        // The agent stops: its grants end.
        chats->setAgents({});
        QTRY_VERIFY_WITH_TIMEOUT(revoked.size() == 3, 20000);
#endif
    }

    void remoteParts()
    {
        using End = acp::RemoteStream::End;
        QCOMPARE(
                WorkspaceAgents::remoteEndText(u"raspberry-pi-4"_s, End::AgentEnded, u"it ended with code 3: koniec"_s),
                u"it ended with code 3: koniec (on raspberry-pi-4)"_s);
        QVERIFY(WorkspaceAgents::remoteEndText(u"raspberry-pi-4"_s, End::Unauthorized, QString())
                        .contains(u"no longer paired"_s));
        QVERIFY(WorkspaceAgents::remoteEndText(u"pi"_s, End::Unreachable, u"Network unreachable"_s)
                        .contains(u"pi could not be reached for a while (Network unreachable)"_s));
        QVERIFY(WorkspaceAgents::remoteEndText(u"pi"_s, End::Refused, u"This computer has no agent x"_s)
                        .endsWith(u"This computer has no agent x"_s));
        QVERIFY(WorkspaceAgents::remoteEndText(u"pi"_s, End::Gone, QString()).contains(u"try again"_s));
        QVERIFY(WorkspaceAgents::remoteEndText(u"pi"_s, End::Version, QString()).contains(u"update Brulion"_s));
        const QString address = u"https://agent.example.pl"_s;
        QVERIFY(WorkspaceAgents::remoteErrorText(address,
                                                 {acp::RemoteError::Kind::Refused, 403, u"bad_code"_s, QString()})
                        .startsWith(u"That is not the code"_s));
        QVERIFY(WorkspaceAgents::remoteErrorText(address,
                                                 {acp::RemoteError::Kind::Refused, 403, u"no_code"_s, QString()})
                        .contains(u"acp pair"_s));
        QVERIFY(WorkspaceAgents::remoteErrorText(address,
                                                 {acp::RemoteError::Kind::Network, 0, QString(), u"Host not found"_s})
                        .contains(u"Host not found"_s));
        QCOMPARE(WorkspaceAgents::remoteErrorText(address, {}), QString());
        QVERIFY(!WorkspaceAgents::deviceName().isEmpty());
        QVERIFY(WorkspaceAgents::remoteAvailable());
        // An agent on another computer runs wherever that computer can be reached.
        AgentConfig remote{u"pi-claude"_s, u"Claude Code"_s, u"claude-agent-acp"_s, true};
        remote.url = address;
        remote.remote = u"claude"_s;
        QVERIFY(WorkspaceAgents::runs(remote));
        QVERIFY(!remote.sameProgram([&] {
            AgentConfig again = remote;
            again.token = u"bat_nowy"_s;
            return again;
        }()));
    }

    void remoteSettings()
    {
        const QString file = m_tmp.filePath(u"remote-settings.ini"_s);
        const QString data = m_tmp.filePath(u"remote-data"_s);
        {
            QSettings raw(file, QSettings::IniFormat);
            raw.setValue(
                    u"ai/agents"_s,
                    QString::fromUtf8(QJsonDocument(QJsonArray{QJsonObject{{u"id"_s, u"zly-adres"_s},
                                                                           {u"name"_s, u"x"_s},
                                                                           {u"url"_s, u"ftp://agent.example.pl"_s},
                                                                           {u"remote"_s, u"claude"_s}},
                                                               QJsonObject{{u"id"_s, u"bez-agenta"_s},
                                                                           {u"name"_s, u"y"_s},
                                                                           {u"url"_s, u"https://agent.example.pl"_s}}})
                                              .toJson()));
        }
        shell::AppSettings settings(file, data);
        QCOMPARE(settings.acpAgents().size(), 0); // (malformed: left out)
        AgentConfig r;
        r.name = u"Claude Code"_s;
        r.command = u"/run/current-system/sw/bin/claude-agent-acp"_s;
        r.tools = true;
        r.url = u"https://agent.example.pl"_s;
        r.remote = u"claude"_s;
        r.host = u"raspberry-pi-4"_s;
        r.root = u"/var/lib/brulion-acp/notebook"_s;
        r.token = u"bat_sekretny-token-żółwia"_s;
        QSignalSpy changed(&settings, &shell::AppSettings::acpAgentsChanged);
        QCOMPARE(settings.addRemoteAcpAgents({r}), QStringList{u"raspberry-pi-4-claude"_s});
        QCOMPARE(changed.size(), 1);
        // The token is in the app's secrets, never in the settings.
        QVERIFY(!QSettings(file, QSettings::IniFormat).value(u"ai/agents"_s).toString().contains(u"bat_"_s));
        QFile secret(data + u"/secrets/acp-raspberry-pi-4-claude"_s);
        QVERIFY(secret.open(QIODevice::ReadOnly));
        QCOMPARE(QString::fromUtf8(secret.readAll()).trimmed(), r.token);
        secret.close();
        QList<AgentConfig> agents = settings.acpAgents();
        QCOMPARE(agents.size(), 1);
        QCOMPARE(agents.at(0).url, r.url);
        QCOMPARE(agents.at(0).remote, u"claude"_s);
        QCOMPARE(agents.at(0).host, u"raspberry-pi-4"_s);
        QCOMPARE(agents.at(0).root, r.root);
        QCOMPARE(agents.at(0).token, r.token);
        const QVariantMap row = settings.acpAgentRows().at(0).toMap();
        QVERIFY(row.value(u"remote"_s).toBool());
        QCOMPARE(row.value(u"summary"_s).toString(),
                 u"On raspberry-pi-4 (https://agent.example.pl) · with Brulion's tools"_s);
        // "Let it change the app while it runs": off at pairing; on, its tools there reach this app
        // (the row says so), and it stays when paired again.
        QVERIFY(!agents.at(0).appAccess);
        QVERIFY(!row.value(u"app"_s).toBool());
        settings.setAcpAgentApp(u"raspberry-pi-4-claude"_s, true);
        QVERIFY(settings.acpAgents().at(0).appAccess);
        QCOMPARE(settings.acpAgentRows().at(0).toMap().value(u"summary"_s).toString(),
                 u"On raspberry-pi-4 (https://agent.example.pl) · with Brulion's tools · may change the app"_s);
        // Paired again: the same agent, a new token; its own name stays.
        settings.setAcpAgentName(u"raspberry-pi-4-claude"_s, u"Claude na Pi"_s);
        r.token = u"bat_nowy-token"_s;
        QCOMPARE(settings.addRemoteAcpAgents({r}), QStringList{u"raspberry-pi-4-claude"_s});
        agents = settings.acpAgents();
        QCOMPARE(agents.size(), 1);
        QCOMPARE(agents.at(0).name, u"Claude na Pi"_s);
        QCOMPARE(agents.at(0).token, u"bat_nowy-token"_s);
        QVERIFY(agents.at(0).appAccess);
        settings.setAcpAgentApp(u"raspberry-pi-4-claude"_s, false);
        QVERIFY(!settings.acpAgents().at(0).appAccess);
        // Another computer of the same name: an agent of its own.
        AgentConfig other = r;
        other.url = u"https://laptop.example.pl"_s;
        other.host = u"Mój Laptop"_s;
        QCOMPARE(settings.addRemoteAcpAgents({other}), QStringList{u"m-j-laptop-claude"_s});
        // Removed: its token goes too.
        settings.removeAcpAgent(u"raspberry-pi-4-claude"_s);
        QVERIFY(!QFileInfo::exists(data + u"/secrets/acp-raspberry-pi-4-claude"_s));
        QCOMPARE(settings.acpAgents().size(), 1);
        // A token the device no longer has: the row says to pair again.
        QFile::remove(data + u"/secrets/acp-m-j-laptop-claude"_s);
        QVERIFY(settings.acpAgentRows().at(0).toMap().value(u"summary"_s).toString().contains(u"not paired"_s));
    }

    void remoteAgent()
    {
#ifndef BRULION_CLI
        QSKIP("brulion-cli is not built here");
#else
        // The computer, with its own copy of the notebook (another Dom.md than this device's).
        const QString computer = m_tmp.filePath(u"komputer"_s);
        writeRaw(computer + u"/Dom.md"_s, "- na komputerze: żółw błotny\n");
        const QString stateFile = m_tmp.filePath(u"komputer-stan/acp-remote.json"_s);
        auto ws = open({}); // (FAKE_ACP_LOG for the agent the computer starts)
        QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
        for (const char *name : {"HOME", "XDG_CONFIG_HOME", "XDG_DATA_HOME", "XDG_CACHE_HOME"})
            env.insert(QString::fromLatin1(name), m_tmp.filePath(u"komputer-dom/"_s + QString::fromLatin1(name)));
        const QStringList common{u"--vault"_s, computer, u"--state-dir"_s, m_tmp.filePath(u"komputer-urzadzenie"_s)};
        QProcess host;
        host.setProcessEnvironment(env);
        host.setProgram(QStringLiteral(BRULION_CLI));
        host.setArguments(common
                          + QStringList{u"acp"_s, u"serve"_s, u"--agent"_s, u"fake:Fałszywy="_s + command(),
                                        u"--port"_s, u"0"_s, u"--state-file"_s, stateFile});
        host.start();
        QVERIFY(host.waitForStarted(10000));
        QTRY_VERIFY_WITH_TIMEOUT(host.canReadLine(), 20000);
        const QJsonObject started = QJsonDocument::fromJson(host.readLine()).object();
        const QString url = started.value(u"result"_s).toObject().value(u"url"_s).toString();
        QVERIFY2(url.startsWith(u"http://127.0.0.1:"_s),
                 qPrintable(QString::fromUtf8(QJsonDocument(started).toJson())));
        const auto cli = [&](const QStringList &args) {
            QProcess p;
            p.setProcessEnvironment(env);
            p.start(QStringLiteral(BRULION_CLI), common + args + QStringList{u"--state-file"_s, stateFile});
            p.waitForFinished(20000);
            return QJsonDocument::fromJson(p.readAllStandardOutput()).object().value(u"result"_s).toObject();
        };
        const QString code = cli({u"acp"_s, u"pair"_s}).value(u"code"_s).toString();
        QCOMPARE(code.size(), 9);

        // Paired from this device, the code typed in lower case.
        WorkspaceChats *chats = ws->chats();
        QList<AgentConfig> paired;
        connect(chats, &WorkspaceChats::agentsPaired, this,
                [&paired](const QList<AgentConfig> &agents) { paired = agents; });
        QSignalSpy pairedSpy(chats, &WorkspaceChats::agentHostPaired);
        chats->pairAgentHost(url, code.toLower());
        QVERIFY(chats->agentPairing());
        QTRY_COMPARE_WITH_TIMEOUT(pairedSpy.size(), 1, 20000);
        QVERIFY2(pairedSpy.first().at(0).toBool(), qPrintable(pairedSpy.first().at(1).toString()));
        QVERIFY(!chats->agentPairing());
        QCOMPARE(paired.size(), 1);
        AgentConfig agent = paired.first();
        QCOMPARE(agent.remote, u"fake"_s);
        QCOMPARE(agent.name, u"Fałszywy"_s);
        QCOMPARE(agent.root, QDir::cleanPath(computer));
        QVERIFY(agent.token.startsWith(u"bat_"_s));
        // The code is no good twice.
        chats->pairAgentHost(url, code);
        QTRY_COMPARE_WITH_TIMEOUT(pairedSpy.size(), 2, 20000);
        QVERIFY(!pairedSpy.last().at(0).toBool());

        agent.id = u"komputer-fake"_s;
        chats->setAgents({agent});
        QCOMPARE(chats->partnerLabel(u"acp:komputer-fake"_s), u"Fałszywy"_s);
        QSignalSpy working(chats->agents(), &WorkspaceAgents::remoteWorkingChanged);
        const QString path = chats->send(QString(), u"Cześć z telefonu"_s, QString(), {}, u"acp:komputer-fake"_s);
        shell::ChatTurn *turn = chats->turn(path);
        QVERIFY(chats->agents()->remoteWorking()); // (Android's foreground service runs meanwhile)
        QCOMPARE(chats->agents()->remoteTurns(), QStringList{path});
        const WorkspaceAgents::RemoteWork work = chats->agents()->remoteWork();
        QCOMPARE(work.title, u"Fałszywy is answering…"_s);
        QCOMPARE(work.text, u"On %1 · %2"_s.arg(agent.host, QFileInfo(path).completeBaseName()));
        QVERIFY(!work.waiting);
        QTRY_VERIFY_WITH_TIMEOUT(!turn->running(), 30000);
        QCOMPARE(turn->failure(), QString());
        QVERIFY(!chats->agents()->remoteWorking());
        QCOMPARE(working.size(), 2);
        QVERIFY2(ws->read(path).contains(u"- 🤖 Odpowiedź na: Cześć z telefonu"_s), qPrintable(ws->read(path)));
        // The agent works on that computer: its folder as the cwd and in the context, its files.
        const QList<QJsonObject> created = logged(u"session/new"_s);
        QCOMPARE(created.size(), 1);
        QCOMPARE(created.first().value(u"params"_s).toObject().value(u"cwd"_s).toString(), QDir::cleanPath(computer));
        const QString context =
                promptOf(logged(u"session/prompt"_s).first()).at(0).toObject().value(u"text"_s).toString();
        QVERIFY2(context.contains(QDir::cleanPath(computer)), qPrintable(context));
        QVERIFY(!context.contains(ws->rootPath()));
        QCOMPARE(chats->send(path, u"czytaj"_s), path);
        QTRY_VERIFY_WITH_TIMEOUT(!turn->running(), 30000);
        QVERIFY2(ws->read(path).contains(u"Przeczytano: - na komputerze: żółw błotny"_s), qPrintable(ws->read(path)));

        // The app is ended by the system while the agent works there: started again, it fetches
        // the answer that computer kept, and the conversation goes on in that session.
        const QString slow = chats->send(QString(), u"powoli"_s, QString(), {}, u"acp:komputer-fake"_s);
        QTRY_VERIFY_WITH_TIMEOUT(logged(u"session/prompt"_s).size() == 3, 30000);
        QVERIFY(chats->turn(slow)->running());
        const QString root = ws->rootPath();
        chats->agents()->abandon();
        ws.reset();
        ws = open({agent}, root);
        chats = ws->chats();
        shell::ChatTurn *again = chats->turn(slow);
        QVERIFY(again->running());
        QVERIFY2(again->status().contains(u"still working"_s), qPrintable(again->status()));
        QVERIFY(chats->agents()->remoteWorking());
        QTRY_VERIFY_WITH_TIMEOUT(!again->running(), 30000);
        QCOMPARE(again->failure(), QString());
        QVERIFY2(ws->read(slow).contains(u"- 🤖 Powoli: gotowe."_s), qPrintable(ws->read(slow)));
        QVERIFY(chats->agents()->recoverable().isEmpty());
        QCOMPARE(chats->send(slow, u"sesja"_s), slow);
        QTRY_VERIFY_WITH_TIMEOUT(!again->running(), 30000);
        QVERIFY2(ws->read(slow).contains(u"- 🤖 Sesja: s"_s), qPrintable(ws->read(slow)));
        QVERIFY(chats->agents()->remoteWork().title.isEmpty());

        // Ended while a question of the agent is on screen: started again, the app gets it from that
        // computer (where it waited), and the answer goes there — the agent writes its copy.
        QFile::remove(computer + u"/Zgoda.md"_s);
        const QString asking = chats->send(QString(), u"pozwolenie"_s, QString(), {}, u"acp:komputer-fake"_s);
        QTRY_VERIFY_WITH_TIMEOUT(!chats->questionOptions(asking).isEmpty(), 30000);
        QCOMPARE(chats->agents()->remoteWork().title, u"Fałszywy asks: Write Zgoda.md"_s);
        QVERIFY(chats->agents()->remoteWork().waiting);
        chats->agents()->abandon();
        ws.reset();
        ws = open({agent}, root);
        chats = ws->chats();
        shell::ChatTurn *asked = chats->turn(asking);
        QVERIFY(asked->running());
        QTRY_VERIFY_WITH_TIMEOUT(!chats->questionOptions(asking).isEmpty(), 30000);
        QCOMPARE(chats->questionOptions(asking), (QStringList{u"Allow"_s, u"Reject"_s}));
        QVERIFY2(chats->questionText(asking).startsWith(u"Fałszywy asks: Write Zgoda.md"_s),
                 qPrintable(chats->questionText(asking)));
        QCOMPARE(chats->questionDiff(asking).value(u"newText"_s).toString(), u"- zgoda"_s);
        QVERIFY(chats->agents()->remoteWork().waiting);
        QVERIFY(!QFile::exists(computer + u"/Zgoda.md"_s));
        chats->answer(asking, u"Allow"_s);
        QVERIFY(chats->questionOptions(asking).isEmpty());
        QTRY_VERIFY_WITH_TIMEOUT(!asked->running(), 30000);
        QCOMPARE(asked->failure(), QString());
        QVERIFY2(ws->read(asking).contains(u"- 🤖 Zapisano."_s), qPrintable(ws->read(asking)));
        QVERIFY(QFile::exists(computer + u"/Zgoda.md"_s));
        QVERIFY(!chats->agents()->remoteWorking());
        turn = chats->turn(path); // (the first conversation's, in the app started again)

        // Unpaired there: the next turn says so.
        QVERIFY(!cli({u"acp"_s, u"revoke"_s, u"--all"_s}).isEmpty());
        QCOMPARE(chats->send(path, u"jeszcze raz"_s), path);
        QTRY_VERIFY_WITH_TIMEOUT(!turn->running(), 30000);
        QVERIFY2(turn->failure().contains(u"no longer paired"_s), qPrintable(turn->failure()));
        ws.reset();
        host.kill(); // (terminate() does not end a console program on Windows)
        QVERIFY(host.waitForFinished(10000));
#endif
    }

    // A thread of a project of another computer (docs/CODE.md "Threads on the phone"): the real
    // brulion-cli acp serve with --projects and the fake agent — the computer's projects listed here
    // with its name, a new thread answered by its agent in the project's folder there (the session's
    // request naming the project, never reaching the agent), the checkpoint that computer took known
    // here (the thread's list, its page), the turn's changes and a file's diff from there, the turn
    // reverted there, a commit there pushed to its remote (an empty message never sent), a worktree
    // made there for the next thread and its turns in it, a folder that is no worktree refused there.
    void remoteThread()
    {
#ifndef BRULION_CLI
        QSKIP("brulion-cli is not built here");
#else
        if (QStandardPaths::findExecutable(u"git"_s).isEmpty())
            QSKIP("no git here");
        const QString computer = m_tmp.filePath(u"komputer-kodu"_s);
        writeRaw(computer + u"/Dom.md"_s, "- na komputerze\n");
        const QString repo = m_tmp.filePath(u"komputer-projekt/Sklep żółwia"_s);
        QVERIFY(writeRaw(repo + u"/Dom.md"_s, "# Sklep żółwia\n"));
        const QString bare = m_tmp.filePath(u"komputer-zdalne.git"_s);
        QCOMPARE(QProcess::execute(u"git"_s, {u"init"_s, u"-q"_s, u"--bare"_s, bare}), 0);
        for (const QStringList &args :
             {QStringList{u"init"_s, u"-q"_s, u"-b"_s, u"main"_s}, QStringList{u"add"_s, u"-A"_s},
              QStringList{u"config"_s, u"user.name"_s, u"Ala"_s}, QStringList{u"config"_s, u"user.email"_s, u"a@b.c"_s},
              QStringList{u"config"_s, u"commit.gpgsign"_s, u"false"_s}, QStringList{u"commit"_s, u"-qm"_s, u"start"_s},
              QStringList{u"remote"_s, u"add"_s, u"origin"_s, bare}})
            QCOMPARE(QProcess::execute(u"git"_s, QStringList{u"-C"_s, repo} + args), 0);
        const QString root = m_tmp.filePath(u"komputer-drzewa"_s);
        const QString projectsFile = m_tmp.filePath(u"komputer-projekty.json"_s);
        QVERIFY(writeRaw(projectsFile, QJsonDocument(QJsonArray{QJsonObject{{u"id"_s, u"sklep"_s},
                                                                             {u"name"_s, u"Sklep żółwia"_s},
                                                                             {u"path"_s, repo},
                                                                             {u"worktreesRoot"_s, root}}})
                                               .toJson()));
        const QString stateFile = m_tmp.filePath(u"komputer-kodu-stan/acp-remote.json"_s);
        auto ws = open({}); // (FAKE_ACP_LOG for the agent the computer starts)
        QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
        for (const char *name : {"HOME", "XDG_CONFIG_HOME", "XDG_DATA_HOME", "XDG_CACHE_HOME"})
            env.insert(QString::fromLatin1(name), m_tmp.filePath(u"komputer-kodu-dom/"_s + QString::fromLatin1(name)));
        const QStringList common{u"--vault"_s, computer, u"--state-dir"_s, m_tmp.filePath(u"komputer-kodu-urzadzenie"_s)};
        QProcess host;
        host.setProcessEnvironment(env);
        host.setProgram(QStringLiteral(BRULION_CLI));
        host.setArguments(common
                          + QStringList{u"acp"_s, u"serve"_s, u"--agent"_s, u"fake:Fałszywy="_s + command(), u"--port"_s,
                                        u"0"_s, u"--state-file"_s, stateFile, u"--projects"_s, projectsFile});
        host.start();
        QVERIFY(host.waitForStarted(10000));
        auto stopHost = qScopeGuard([&host] {
            host.kill();
            host.waitForFinished(10000);
        });
        QTRY_VERIFY_WITH_TIMEOUT(host.canReadLine(), 20000);
        const QJsonObject started = QJsonDocument::fromJson(host.readLine()).object();
        const QString url = started.value(u"result"_s).toObject().value(u"url"_s).toString();
        QCOMPARE(started.value(u"result"_s).toObject().value(u"projects"_s).toString(), QDir::cleanPath(projectsFile));
        QProcess pairing;
        pairing.setProcessEnvironment(env);
        pairing.start(QStringLiteral(BRULION_CLI), common + QStringList{u"acp"_s, u"pair"_s, u"--state-file"_s, stateFile});
        QVERIFY(pairing.waitForFinished(20000));
        const QString code =
                QJsonDocument::fromJson(pairing.readAllStandardOutput()).object().value(u"result"_s).toObject().value(u"code"_s).toString();
        WorkspaceChats *chats = ws->chats();
        QList<AgentConfig> paired;
        connect(chats, &WorkspaceChats::agentsPaired, this, [&paired](const QList<AgentConfig> &agents) { paired = agents; });
        chats->pairAgentHost(url, code);
        QTRY_COMPARE_WITH_TIMEOUT(paired.size(), 1, 20000);
        AgentConfig agent = paired.first();
        agent.id = u"komputer-fake"_s;
        chats->setAgents({agent});

        // Its projects: listed here (this device has none), with the computer's name.
        shell::CodeProjects *projects = ws->projects();
        QTRY_VERIFY_WITH_TIMEOUT(projects->contains(u"sklep"_s), 20000);
        QVERIFY(projects->isRemote(u"sklep"_s));
        QCOMPARE(projects->count(), 1);
        const QVariantMap entry = projects->project(u"sklep"_s);
        QVERIFY(entry.value(u"remote"_s).toBool());
        QCOMPARE(entry.value(u"host"_s).toString(), agent.host);
        QCOMPARE(entry.value(u"remotePath"_s).toString(), QDir::cleanPath(repo));
        QCOMPARE(projects->nameOf(u"sklep"_s), u"Sklep żółwia"_s);
        QVERIFY(projects->folderOf(u"sklep"_s).isEmpty());
        QCOMPARE(projects->list().size(), 2); // (and "No project", last)
        // A new thread there: answered by that computer's agent (another one asked for: the agent
        // that computer has), in the project's folder there.
        const QString path = chats->startThread(u"sklep"_s, u"katalog"_s, {}, u"acp:claude"_s);
        QVERIFY2(!path.isEmpty(), qPrintable(chats->turn(QString())->failure()));
        QVERIFY2(ws->read(path).contains(u"partner: acp:komputer-fake"_s), qPrintable(ws->read(path)));
        QTRY_VERIFY_WITH_TIMEOUT(!chats->turn(path)->running(), 30000);
        QCOMPARE(chats->turn(path)->failure(), QString());
        const QString folder = QFileInfo(repo).canonicalFilePath();
        QVERIFY2(ws->read(path).contains(u"- 🤖 Katalog: "_s + folder), qPrintable(ws->read(path)));
        const QJsonObject params = logged(u"session/new"_s).last().value(u"params"_s).toObject();
        QCOMPARE(params.value(u"cwd"_s).toString(), folder);
        QVERIFY(!params.contains(u"_meta"_s));
        const QString context = promptOf(logged(u"session/prompt"_s).last()).at(0).toObject().value(u"text"_s).toString();
        QVERIFY2(context.contains(u"software project"_s) && context.contains(QDir::cleanPath(repo)), qPrintable(context));
        QVERIFY(!context.contains(QDir::cleanPath(computer)));
        // The checkpoint that computer took: this thread's, in its page too; a ref in that repository.
        QTRY_COMPARE_WITH_TIMEOUT(chats->checkpoints(path).size(), 1, 20000);
        const QString checkpoint = chats->lastCheckpoint(path);
        QCOMPARE(QProcess::execute(u"git"_s, {u"-C"_s, repo, u"rev-parse"_s, u"-q"_s, u"--verify"_s,
                                              u"refs/brulion/checkpoints/"_s + checkpoint}),
                 0);
        QVERIFY2(ws->read(path).contains(u"checkpoint: "_s + checkpoint), qPrintable(ws->read(path)));
        // Where it works: there, nothing of it here.
        QVariantMap place = chats->threadPlace(path);
        QVERIFY(place.value(u"remote"_s).toBool());
        QCOMPARE(place.value(u"host"_s).toString(), agent.host);
        QCOMPARE(place.value(u"remoteFolder"_s).toString(), QDir::cleanPath(repo));
        QVERIFY(place.value(u"folder"_s).toString().isEmpty());
        QVERIFY(!place.value(u"exists"_s).toBool());
        QVERIFY(chats->threadFolder(path).isEmpty());

        // A file written there; the turn's changes and the file's diff from there.
        QCOMPARE(chats->send(path, u"zapisz"_s), path);
        QTRY_VERIFY_WITH_TIMEOUT(!chats->turn(path)->running(), 30000);
        QFile written(repo + u"/Od agenta.md"_s);
        QVERIFY(written.open(QIODevice::ReadOnly));
        QCOMPARE(written.readAll(), QByteArray("- od agenta\n"));
        QTRY_COMPARE_WITH_TIMEOUT(chats->checkpoints(path).size(), 2, 20000);
        shell::RemoteProjects *remote = projects->remote();
        QSignalSpy ready(remote, &shell::RemoteProjects::changesReady);
        const int serial = remote->requestChanges(u"sklep"_s, QString(), chats->lastCheckpoint(path));
        QTRY_COMPARE_WITH_TIMEOUT(ready.size(), 1, 20000);
        QCOMPARE(ready.first().at(0).toInt(), serial);
        QCOMPARE(ready.first().at(3).toString(), QString());
        const QVariantList changes = ready.first().at(1).toList();
        QCOMPARE(changes.size(), 1);
        QCOMPARE(changes.first().toMap().value(u"path"_s).toString(), u"Od agenta.md"_s);
        QCOMPARE(changes.first().toMap().value(u"letter"_s).toString(), u"A"_s);
        QCOMPARE(changes.first().toMap().value(u"name"_s).toString(), u"Od agenta.md"_s);
        QSignalSpy diffed(remote, &shell::RemoteProjects::diffReady);
        remote->requestDiff(u"sklep"_s, QString(), chats->lastCheckpoint(path), u"Od agenta.md"_s, QString());
        QTRY_COMPARE_WITH_TIMEOUT(diffed.size(), 1, 20000);
        QCOMPARE(diffed.first().at(3).toString(), u"- od agenta\n"_s);
        QCOMPARE(diffed.first().at(6).toString(), QString());
        // A project no computer has: said so.
        remote->requestChanges(u"brak"_s, QString(), QString());
        QTRY_COMPARE_WITH_TIMEOUT(ready.size(), 2, 20000);
        QVERIFY(!ready.last().at(3).toString().isEmpty());

        // Git there from here: the turn reverted (its file gone there), written again, committed
        // and pushed (to a bare repository there); a message or a title that is none never sent.
        QSignalSpy git(remote, &shell::RemoteProjects::gitDone);
        int asked = remote->revertTurn(u"sklep"_s, QString(), chats->lastCheckpoint(path));
        QTRY_COMPARE_WITH_TIMEOUT(git.size(), 1, 30000);
        QCOMPARE(git.last().at(0).toInt(), asked);
        QCOMPARE(git.last().at(1).toString(), u"revert"_s);
        QVERIFY2(git.last().at(2).toBool(), qPrintable(git.last().at(3).toString()));
        QVERIFY(!QFile::exists(repo + u"/Od agenta.md"_s));
        QCOMPARE(chats->send(path, u"zapisz"_s), path);
        QTRY_VERIFY_WITH_TIMEOUT(!chats->turn(path)->running(), 30000);
        QVERIFY(QFile::exists(repo + u"/Od agenta.md"_s));
        remote->commit(u"sklep"_s, QString(), u"  \n "_s, {}, true, false, QString(), QString(), false);
        QTRY_COMPARE_WITH_TIMEOUT(git.size(), 2, 20000);
        QVERIFY(!git.last().at(2).toBool());
        QCOMPARE(git.last().at(3).toString(), u"Write a commit message first."_s);
        asked = remote->commit(u"sklep"_s, QString(), u"Od agenta\n\nZ telefonu"_s, {}, true, false, QString(),
                               QString(), false);
        QTRY_COMPARE_WITH_TIMEOUT(git.size(), 3, 60000);
        QCOMPARE(git.last().at(0).toInt(), asked);
        QVERIFY2(git.last().at(2).toBool(), qPrintable(git.last().at(3).toString()));
        QCOMPARE(git.last().at(3).toString(), u"Committed and pushed to origin"_s);
        QProcess log;
        log.start(u"git"_s, {u"-C"_s, bare, u"log"_s, u"-1"_s, u"--format=%s"_s, u"main"_s});
        QVERIFY(log.waitForFinished(20000));
        QCOMPARE(log.readAllStandardOutput().trimmed(), "Od agenta"_ba);
        remote->createPullRequest(u"sklep"_s, QString(), u"   "_s, QString(), QString(), false);
        QTRY_COMPARE_WITH_TIMEOUT(git.size(), 4, 20000);
        QCOMPARE(git.last().at(3).toString(), u"A pull request needs a title."_s);
        // Nothing to commit there now: its words.
        remote->commit(u"sklep"_s, QString(), u"Znowu"_s, {}, false, false, QString(), QString(), false);
        QTRY_COMPARE_WITH_TIMEOUT(git.size(), 5, 30000);
        QVERIFY(!git.last().at(2).toBool());
        QVERIFY2(git.last().at(3).toString().contains(u"Nothing to commit"_s), qPrintable(git.last().at(3).toString()));
        remote->push(u"brak"_s, QString());
        QTRY_COMPARE_WITH_TIMEOUT(git.size(), 6, 20000);
        QCOMPARE(git.last().at(3).toString(), u"No computer of yours has this project."_s);

        // Its branches there, for a new worktree's base (the picker asks for its folder there).
        QCOMPARE(QProcess::execute(u"git"_s, {u"-C"_s, repo, u"branch"_s, u"rozwój"_s}), 0);
        QSignalSpy listed(projects, &shell::CodeProjects::branchesListed);
        QVERIFY(projects->listBranches(QDir::cleanPath(repo)));
        QTRY_COMPARE_WITH_TIMEOUT(listed.size(), 1, 20000);
        QCOMPARE(listed.first().at(0).toString(), QDir::cleanPath(repo));
        QCOMPARE(listed.first().at(2).toString(), QString());
        QStringList names;
        for (const QVariant &row : listed.first().at(1).toList())
            names << row.toMap().value(u"name"_s).toString();
        QVERIFY2(names.contains(u"rozwój"_s), qPrintable(names.join(u", "_s)));
        QVERIFY(!projects->listBranches(m_tmp.filePath(u"nie-ma-tu-nic"_s))); // (no folder here nor there)
        // A worktree there from that base: its branch starts at the base's commit.
        {
            QFile side(repo + u"/Na boku.md"_s);
            QVERIFY(side.open(QIODevice::WriteOnly));
            side.write("- w main\n");
        }
        QCOMPARE(QProcess::execute(u"git"_s, {u"-C"_s, repo, u"add"_s, u"-A"_s}), 0);
        QCOMPARE(QProcess::execute(u"git"_s,
                                   {u"-C"_s, repo, u"-c"_s, u"user.name=Ala"_s, u"-c"_s, u"user.email=a@b.c"_s, u"-c"_s,
                                    u"commit.gpgsign=false"_s, u"commit"_s, u"-q"_s, u"-m"_s, u"Na boku"_s}),
                 0);
        QSignalSpy fromBase(projects, &shell::CodeProjects::worktreeCreated);
        QVERIFY(projects->createWorktree(u"sklep"_s, u"Z rozwoju"_s, u"rozwój"_s));
        QTRY_COMPARE_WITH_TIMEOUT(fromBase.size(), 1, 30000);
        QCOMPARE(fromBase.first().at(3).toString(), QString());
        const QString based = fromBase.first().at(1).toString();
        QVERIFY(!QFileInfo::exists(based + u"/Na boku.md"_s)); // (rozwój has not got main's newest)
        QProcess head;
        head.start(u"git"_s, {u"-C"_s, repo, u"rev-parse"_s, u"rozwój"_s, fromBase.first().at(2).toString()});
        QVERIFY(head.waitForFinished(20000));
        const QList<QByteArray> heads = head.readAllStandardOutput().trimmed().split('\n');
        QCOMPARE(heads.size(), 2);
        QCOMPARE(heads.at(0), heads.at(1));

        // A worktree made there for the next thread, and its turns in it.
        QSignalSpy created(projects, &shell::CodeProjects::worktreeCreated);
        QVERIFY(projects->createWorktree(u"sklep"_s, u"Dodaj rabat 10%"_s));
        QTRY_COMPARE_WITH_TIMEOUT(created.size(), 1, 30000);
        QCOMPARE(created.first().at(0).toString(), u"sklep"_s);
        QCOMPARE(created.first().at(3).toString(), QString());
        const QString tree = created.first().at(1).toString();
        QCOMPARE(tree, QDir::cleanPath(root + u"/sklep/dodaj-rabat-10"_s));
        QCOMPARE(created.first().at(2).toString(), u"brulion/dodaj-rabat-10"_s);
        const QString threaded = chats->startThread(u"sklep"_s, u"katalog"_s, {}, QString(), tree, u"brulion/dodaj-rabat-10"_s);
        QVERIFY(!threaded.isEmpty());
        QTRY_VERIFY_WITH_TIMEOUT(!chats->turn(threaded)->running(), 30000);
        QVERIFY2(ws->read(threaded).contains(u"- 🤖 Katalog: "_s + QFileInfo(tree).canonicalFilePath()),
                 qPrintable(ws->read(threaded)));
        QCOMPARE(chats->threadPlace(threaded).value(u"remoteFolder"_s).toString(), tree);
        QCOMPARE(chats->threadPlace(threaded).value(u"branch"_s).toString(), u"brulion/dodaj-rabat-10"_s);
        // A folder that is no worktree of the project: refused there, the turn says why.
        const QString stranger = m_tmp.filePath(u"obcy-folder"_s);
        QVERIFY(QDir().mkpath(stranger));
        QVERIFY(chats->setThreadWorktree(threaded, stranger, u"obca"_s));
        QCOMPARE(chats->send(threaded, u"katalog"_s), threaded);
        QTRY_VERIFY_WITH_TIMEOUT(!chats->turn(threaded)->running(), 30000);
        QVERIFY2(chats->turn(threaded)->failure().contains(u"no worktree"_s), qPrintable(chats->turn(threaded)->failure()));

        // The computer's agents gone from this device: its projects too.
        chats->setAgents({});
        QVERIFY(!projects->contains(u"sklep"_s));
        QCOMPARE(projects->count(), 0);
        ws.reset();
#endif
    }

    void settings()
    {
        const QString file = m_tmp.filePath(u"settings.ini"_s);
        {
            QSettings raw(file, QSettings::IniFormat);
            raw.setValue(u"ai/agents"_s,
                         uR"([{"id":"claude","name":"Claude Code","command":"npx a"},{"id":"Zły id!","name":"x","command":"y"},
                             {"id":"claude","name":"drugi","command":"z"},{"id":"own","name":"","command":"moj-agent --acp","tools":false},
                             "nie obiekt"])"_s);
        }
        shell::AppSettings settings(file, m_tmp.filePath(u"data"_s));
        QList<AgentConfig> agents = settings.acpAgents();
        QCOMPARE(agents.size(), 2);
        QCOMPARE(agents.at(0), (AgentConfig{u"claude"_s, u"Claude Code"_s, u"npx a"_s, true}));
        QCOMPARE(agents.at(1), (AgentConfig{u"own"_s, u"own"_s, u"moj-agent --acp"_s, false}));
        QSignalSpy changed(&settings, &shell::AppSettings::acpAgentsChanged);
        QCOMPARE(settings.addAcpAgent(u"claude"_s), u"claude-2"_s);
        QCOMPARE(settings.addAcpAgent(u"gemini"_s), u"gemini"_s);
        QCOMPARE(settings.addAcpAgent(QString()), u"agent"_s);
        QCOMPARE(changed.size(), 3);
        settings.setAcpAgentName(u"agent"_s, u"  Mój   agent "_s);
        settings.setAcpAgentCommand(u"agent"_s, u" ./agent --acp "_s);
        settings.setAcpAgentTools(u"agent"_s, false);
        settings.setAcpAgentName(u"agent"_s, u"   "_s); // (an empty name: kept)
        settings.removeAcpAgent(u"claude"_s);
        agents = settings.acpAgents();
        QCOMPARE(agents.size(), 4);
        QCOMPARE(agents.at(0).id, u"own"_s);
        QCOMPARE(agents.at(1).command, WorkspaceAgents::presets().at(0).command);
        QCOMPARE(agents.last(), (AgentConfig{u"agent"_s, u"Mój agent"_s, u"./agent --acp"_s, false}));
        QCOMPARE(settings.acpAgentRows().size(), 4);
        // What the row says: the command (a long one without its middle) and the tools.
        QCOMPARE(settings.acpAgentRows().last().toMap().value(u"summary"_s).toString(),
                 u"./agent --acp · without Brulion's tools"_s);
        settings.setAcpAgentCommand(u"agent"_s, u"/home/ala/.local/share/agenci/bardzo-dlugi-katalog/moj-agent --acp --verbose"_s);
        QCOMPARE(settings.acpAgentRows().last().toMap().value(u"summary"_s).toString(),
                 u"/home/ala/.local/share/a…talog/moj-agent --acp --verbose · without Brulion's tools"_s);
        QCOMPARE(settings.acpPresets().size(), 4);
        // What the chat learnt of an agent: kept with it, read back.
        const QJsonArray known{QJsonObject{{u"id"_s, u"model"_s}, {u"type"_s, u"select"_s}, {u"currentValue"_s, u"opus"_s}}};
        settings.setAcpAgentMemory(u"agent"_s, QJsonObject{{u"model"_s, u"opus"_s}}, known);
        QCOMPARE(settings.acpAgents().last().options.value(u"model"_s).toString(), u"opus"_s);
        QCOMPARE(settings.acpAgents().last().known, known);
        // "Let it change the app": kept as `app`, in the row and its summary.
        QVERIFY(!settings.acpAgents().last().appAccess);
        settings.setAcpAgentTools(u"agent"_s, true);
        settings.setAcpAgentApp(u"agent"_s, true);
        QVERIFY(settings.acpAgents().last().appAccess);
        QVERIFY(settings.acpAgentRows().last().toMap().value(u"app"_s).toBool());
        QVERIFY(settings.acpAgentRows()
                        .last()
                        .toMap()
                        .value(u"summary"_s)
                        .toString()
                        .endsWith(u" · may change the app"_s));
        settings.setAcpAgentApp(u"agent"_s, false);
        QVERIFY(!settings.acpAgents().last().appAccess);
        QVERIFY(!settings.acpAgentRows().last().toMap().value(u"app"_s).toBool());
    }
};

QTEST_GUILESS_MAIN(TestAgents)
#include "tst_agents.moc"
