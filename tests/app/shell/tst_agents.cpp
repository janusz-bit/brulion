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
// the words for a stream's ends and a pairing's errors. "Let it change the app": its tools get
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
#include "appsettings.h"
#include "chatcontrol.h"
#include "chatlineage.h"
#include "chatschedules.h"
#include "chatsecrets.h"
#include "workspace.h"
#include "workspaceagents.h"
#include "workspacechats.h"

#include "chat/chat.h"
#include "log/redact.h"
#include "webhook/webhook.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLocale>
#include <QProcess>
#include <QScopeGuard>
#include <QSettings>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

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
                             QJsonObject{{u"name"_s, u"BRULION_APP_CLIENT"_s}, {u"value"_s, u"Fałszywy"_s}}}));
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

        // The user's conversation with an agent: its tools get the chat's grant for it.
        const QString path = chats->send(QString(), u"Zaplanuj wyjazd do Zakopanego"_s, QString(), {}, u"acp:fake"_s);
        QTRY_VERIFY_WITH_TIMEOUT(!chats->turn(path)->running(), 20000);
        QCOMPARE(chatGranted, QStringList{u"Fałszywy"_s});
        QCOMPARE(serverArgs(0).last(), u"--app-chat"_s);
        QCOMPARE(serverEnv(0).value(u"BRULION_APP_CHAT_TOKEN"_s), u"chat-1"_s);
        QCOMPARE(serverEnv(0).value(u"BRULION_APP_CLIENT"_s), u"Fałszywy"_s);
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
        chats->stop(path);
        // (Renamed once the agent ended the cancelled turn: a session is not renamed while a turn
        // of it is on its way.)
        QTRY_VERIFY(logged(u"session/cancel"_s).size() > cancels);
        QTest::qWait(300 * BRULION_TEST_SLOWDOWN);
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
        QVERIFY(!QFile::exists(u"/etc/brulion-test-outside"_s));
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
        // A plan.
        QCOMPARE(chats->send(path, u"Zrób plan"_s), path);
        QTRY_VERIFY_WITH_TIMEOUT(!turn->running(), 20000);
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
        QCOMPARE(chats->agentCommands(path, u"acp:fake"_s, u"/"_s).size(), 2);
        const QVariantList review = chats->agentCommands(path, u"acp:fake"_s, u"/RE"_s);
        QCOMPARE(review.size(), 1);
        QCOMPARE(review.first().toMap().value(u"description"_s).toString(), u"Przejrzyj zmiany"_s);
        QVERIFY(chats->agentCommands(path, u"acp:fake"_s, u"/review coś"_s).isEmpty());
        QVERIFY(chats->agentCommands(path, u"acp:fake"_s, u"review"_s).isEmpty());
        QVERIFY(chats->agentCommands(path, QString(), u"/"_s).isEmpty());
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
        // Paired again: the same agent, a new token; its own name stays.
        settings.setAcpAgentName(u"raspberry-pi-4-claude"_s, u"Claude na Pi"_s);
        r.token = u"bat_nowy-token"_s;
        QCOMPARE(settings.addRemoteAcpAgents({r}), QStringList{u"raspberry-pi-4-claude"_s});
        agents = settings.acpAgents();
        QCOMPARE(agents.size(), 1);
        QCOMPARE(agents.at(0).name, u"Claude na Pi"_s);
        QCOMPARE(agents.at(0).token, u"bat_nowy-token"_s);
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
