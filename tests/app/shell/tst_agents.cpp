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
// context, that computer's files read), unpaired there (the turn says so); the settings' remote
// agents (their token in the app's secrets, never in the settings; paired again: the same agent);
// the words for a stream's ends and a pairing's errors.
#include "appsettings.h"
#include "workspace.h"
#include "workspaceagents.h"
#include "workspacechats.h"

#include "chat/chat.h"

#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLocale>
#include <QProcess>
#include <QSettings>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

using namespace Qt::StringLiterals;
using namespace brulion;
using brulion::shell::AgentConfig;
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

private Q_SLOTS:
    void initTestCase()
    {
        QVERIFY(m_tmp.isValid());
        QLocale::setDefault(QLocale::c());
        if (!WorkspaceAgents::available())
            QSKIP("AI agents run only in the desktop app");
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
        // Tool calls without words: what it did is the answer.
        QCOMPARE(chats->send(path, u"Same narzędzia"_s), path);
        QTRY_VERIFY_WITH_TIMEOUT(!turn->running(), 20000);
        QCOMPARE(turn->failure(), QString());
        QVERIFY2(ws->read(path).contains(u"- 🤖 Done:\n  czas:: 2026-11-20 09:15\n\n  - Edit Dom.md"_s), qPrintable(ws->read(path)));
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
        host.start(QStringLiteral(BRULION_CLI),
                   common + QStringList{u"acp"_s, u"serve"_s, u"--agent"_s, u"real:Prawdziwy="_s + command, u"--port"_s,
                                        u"0"_s, u"--state-file"_s, stateFile});
        QVERIFY(host.waitForStarted(10000));
        QTRY_VERIFY_WITH_TIMEOUT(host.canReadLine(), 30000);
        const QString url =
                QJsonDocument::fromJson(host.readLine()).object().value(u"result"_s).toObject().value(u"url"_s).toString();
        QProcess pair;
        pair.start(QStringLiteral(BRULION_CLI), common + QStringList{u"acp"_s, u"pair"_s, u"--state-file"_s, stateFile});
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
        const QString path = chats->send(QString(), u"Jakie zadanie jest w notatce Dom? Odpowiedz krótko, jednym zdaniem."_s,
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
        host.terminate();
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

    void pureParts()
    {
        const QString context = WorkspaceAgents::context(u"/home/ala/Notatki"_s, u"AI/2026-11-20 Pytanie"_s, QString(), true);
        QVERIFY(context.contains(u"/home/ala/Notatki"_s));
        QVERIFY(context.contains(u"MCP server \"brulion\""_s));
        QVERIFY(!context.contains(u"about the note"_s));
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
            raw.setValue(u"ai/agents"_s,
                         QString::fromUtf8(QJsonDocument(QJsonArray{
                                 QJsonObject{{u"id"_s, u"zly-adres"_s}, {u"name"_s, u"x"_s},
                                             {u"url"_s, u"ftp://agent.example.pl"_s}, {u"remote"_s, u"claude"_s}},
                                 QJsonObject{{u"id"_s, u"bez-agenta"_s}, {u"name"_s, u"y"_s},
                                             {u"url"_s, u"https://agent.example.pl"_s}}}).toJson()));
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
        QVERIFY(chats->agents()->remoteWorking()); // (the screen stays on meanwhile)
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
        turn = chats->turn(path); // (the first conversation's, in the app started again)

        // Unpaired there: the next turn says so.
        QVERIFY(!cli({u"acp"_s, u"revoke"_s, u"--all"_s}).isEmpty());
        QCOMPARE(chats->send(path, u"jeszcze raz"_s), path);
        QTRY_VERIFY_WITH_TIMEOUT(!turn->running(), 30000);
        QVERIFY2(turn->failure().contains(u"no longer paired"_s), qPrintable(turn->failure()));
        ws.reset();
        host.terminate();
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
    }
};

QTEST_GUILESS_MAIN(TestAgents)
#include "tst_agents.moc"
