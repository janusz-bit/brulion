// `brulion-cli acp agent` (src/cli/acpagent.h): Brulion's assistant as an ACP agent, talked to by the
// app's own ACP client (core acp/acp.h) as an editor would, its b.ai answered by an OpenAI-compatible
// fake (tests/app/fakeaiserver.h). In process: a conversation (the history sent again), what the
// assistant does shown as tool calls, the model's thoughts as thought chunks, a change of a note
// asked as session/request_permission — allowed (written, with the command line's history reason),
// declined (nothing written, the model told), every change of a question allowed —, a turn
// cancelled while the model writes, a prompt without a model (no key, the model on the device), no
// notebook, the prompt's embedded files and links. As a process: brulion-cli reading the app's AI
// settings and key from a private home, over standard input and output.
#include "acpagent.h"
#include "cli_p.h"

#include "acp/acp.h"
#include "fakeaiserver.h"
#include "notebook/session.h"
#include "sync/http.h"

#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QProcess>
#include <QTemporaryDir>
#include <QTest>

#include <algorithm>
#include <memory>

using namespace Qt::StringLiterals;
using namespace brulion;
using namespace brulion::cli;

namespace {

constexpr int kTimeoutMs = 20000 * BRULION_TEST_SLOWDOWN;

void writeFile(const QString &path, const QByteArray &bytes)
{
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile f(path);
    QVERIFY(f.open(QIODevice::WriteOnly));
    f.write(bytes);
}

/// The text of every note under `root` (not .brulion/) that contains `text`.
QStringList filesWith(const QString &root, const QString &text)
{
    QStringList out;
    QDirIterator it(root, {u"*.md"_s}, QDir::Files, QDirIterator::Subdirectories);
    while (it.hasNext()) {
        const QString path = it.next();
        if (path.contains(u"/.brulion/"_s))
            continue;
        QFile f(path);
        if (f.open(QIODevice::ReadOnly) && QString::fromUtf8(f.readAll()).contains(text))
            out << QDir(root).relativeFilePath(path);
    }
    return out;
}

/// What the editor's side heard of one conversation.
struct Heard {
    QString answer;
    QString stopReason;
    std::optional<acp::Failure> failure;
    QList<acp::Permission> questions;
    QStringList statuses;
};

/// The app's ACP client and the agent, each one's lines delivered to the other from the event loop
/// (as two processes would).
struct Pair {
    std::unique_ptr<AcpAgent> agent;
    std::unique_ptr<acp::Client> client;
    QList<QJsonObject> fromAgent; ///< every message the agent wrote
    QHash<QString, Heard> heard;
    /// An answer to a question of the agent: the option of this kind ("" : none, the question waits).
    QString answerKind = u"allow_once"_s;

    Pair(notebook::Notebook *nb, const QString &problem, AcpAgentOptions options, sync::HttpTransport *transport)
    {
        agent = std::make_unique<AcpAgent>(nb, problem, std::move(options), transport, [this](const QByteArray &line) {
            fromAgent.append(QJsonDocument::fromJson(line).object());
            QMetaObject::invokeMethod(
                    client.get(), [this, line] { client->received(line); }, Qt::QueuedConnection);
        });
        client = std::make_unique<acp::Client>(
                [this](const QByteArray &line) {
                    QMetaObject::invokeMethod(
                            agent.get(), [this, line] { agent->received(line); }, Qt::QueuedConnection);
                },
                u"0-test"_s);
        acp::Client *c = client.get();
        QObject::connect(c, &acp::Client::finished, c, [this](const QString &key, const QString &text, const QString &reason) {
            heard[key].answer = text;
            heard[key].stopReason = reason;
        });
        QObject::connect(c, &acp::Client::failed, c, [this](const QString &key, const acp::Failure &failure) {
            heard[key].failure = failure;
        });
        QObject::connect(c, &acp::Client::status, c, [this](const QString &key, const QString &text) {
            if (!text.isEmpty())
                heard[key].statuses << text;
        });
        QObject::connect(c, &acp::Client::permissionAsked, c, [this, c](const QString &key, const acp::Permission &p) {
            heard[key].questions << p;
            for (const acp::PermissionOption &o : p.options) {
                if (o.kind == answerKind) {
                    c->answer(key, o.id);
                    return;
                }
            }
        });
    }

    bool prompt(const QString &key, const QList<acp::Block> &blocks)
    {
        acp::TurnInput input;
        input.cwd = QDir::tempPath();
        input.prompt = blocks;
        return client->prompt(key, input);
    }
    bool prompt(const QString &key, const QString &text) { return prompt(key, {acp::Block::textBlock(text)}); }
    bool done(const QString &key) const
    {
        return heard.contains(key) && (!heard[key].stopReason.isEmpty() || heard[key].failure.has_value());
    }
    /// The updates of this kind the agent sent (sessionUpdate).
    QList<QJsonObject> updates(const QString &kind) const
    {
        QList<QJsonObject> out;
        for (const QJsonObject &m : fromAgent) {
            const QJsonObject u = m.value(u"params"_s).toObject().value(u"update"_s).toObject();
            if (m.value(u"method"_s).toString() == u"session/update" && u.value(u"sessionUpdate"_s).toString() == kind)
                out << u;
        }
        return out;
    }
};

} // namespace

class TestCliAcpAgent : public QObject {
    Q_OBJECT

    QTemporaryDir m_dir;
    QString m_root;
    std::unique_ptr<notebook::Notebook> m_nb;
    std::unique_ptr<FakeAiServer> m_server;
    std::unique_ptr<sync::NetworkHttpTransport> m_transport;

    AcpAgentOptions options() const
    {
        AcpAgentOptions o;
        o.ai.provider = u"openai"_s;
        o.ai.baseUrl = m_server->url();
        o.ai.model = u"fake"_s;
        o.language = u"pl"_s;
        o.now = [] { return QDateTime(QDate(2026, 11, 22), QTime(10, 0)); };
        o.version = u"0-test"_s;
        return o;
    }
    std::unique_ptr<Pair> pair(const AcpAgentOptions &o) const
    {
        return std::make_unique<Pair>(m_nb.get(), QString(), o, m_transport.get());
    }
    /// The texts of the user's messages and the tools' results of the fake's request `i`.
    QString requestText(int i) const
    {
        return QString::fromUtf8(QJsonDocument(m_server->requests.at(i)).toJson(QJsonDocument::Compact));
    }

private Q_SLOTS:
    void initTestCase()
    {
        QVERIFY(m_dir.isValid());
        m_root = m_dir.filePath(u"Zeszyt"_s);
        writeFile(m_root + u"/Zakupy.md"_s, "- chleb\n- masło\n");
        writeFile(m_root + u"/Projekty/Dom.md"_s, "# Dom\n\n- [ ] Pomalować płot\n");
        m_nb = std::make_unique<notebook::Notebook>();
        notebook::Notebook::Options o;
        o.stateDir = m_dir.filePath(u"state"_s);
        QString error;
        QVERIFY2(m_nb->open(m_root, o, &error), qPrintable(error));
        m_nb->index();
        m_transport = std::make_unique<sync::NetworkHttpTransport>();
    }

    void init() { m_server = std::make_unique<FakeAiServer>(); }

    void conversation()
    {
        auto p = pair(options());
        m_server->answers = {u"think:Użytkownik pyta o pogodę.\nNie wiem, jaka jest pogoda."_s,
                             u"Wcześniej pytałeś o pogodę."_s};
        QVERIFY(p->prompt(u"a"_s, u"Jaka jest dziś pogoda?"_s));
        QTRY_VERIFY_WITH_TIMEOUT(p->done(u"a"_s), kTimeoutMs);
        QVERIFY2(!p->heard[u"a"_s].failure, qPrintable(p->heard[u"a"_s].failure ? p->heard[u"a"_s].failure->detail : QString()));
        QCOMPARE(p->heard[u"a"_s].answer, u"Nie wiem, jaka jest pogoda."_s);
        QCOMPARE(p->heard[u"a"_s].stopReason, u"end_turn"_s);
        // The model's thoughts, as they were written.
        QString thought;
        for (const QJsonObject &u : p->updates(u"agent_thought_chunk"_s))
            thought += u.value(u"content"_s).toObject().value(u"text"_s).toString();
        QCOMPARE(thought.trimmed(), u"Użytkownik pyta o pogodę."_s);
        QCOMPARE(p->client->agentName(), u"Brulion"_s);
        // The assistant's own instructions, in the notebook's language, the question with today.
        QCOMPARE(m_server->requests.size(), 1);
        QVERIFY(requestText(0).contains(u"Brulion"_s));
        QVERIFY(FakeAiServer::lastUserText(m_server->requests.at(0)).contains(u"Jaka jest dziś pogoda?"_s));
        QVERIFY(FakeAiServer::lastUserText(m_server->requests.at(0)).contains(u"2026-11-22"_s));
        // The next turn sends the conversation so far.
        p->heard.remove(u"a"_s);
        QVERIFY(p->prompt(u"a"_s, u"O co pytałem?"_s));
        QTRY_VERIFY_WITH_TIMEOUT(p->done(u"a"_s), kTimeoutMs);
        QCOMPARE(p->heard[u"a"_s].answer, u"Wcześniej pytałeś o pogodę."_s);
        QCOMPARE(m_server->requests.size(), 2);
        const QString second = requestText(1);
        QVERIFY(second.contains(u"Jaka jest dziś pogoda?"_s));
        QVERIFY(second.contains(u"Nie wiem, jaka jest pogoda."_s));
        QVERIFY(second.contains(u"O co pytałem?"_s));
        QCOMPARE(p->agent->sessions().size(), 1);
    }

    void searchIsAToolCall()
    {
        auto p = pair(options());
        m_server->answers = {u"tool:search_notes {\"query\": \"masło\"}"_s, u"Masło jest na liście [[Zakupy]]."_s};
        QVERIFY(p->prompt(u"s"_s, u"Gdzie zapisałem masło?"_s));
        QTRY_VERIFY_WITH_TIMEOUT(p->done(u"s"_s), kTimeoutMs);
        QVERIFY(!p->heard[u"s"_s].failure);
        QCOMPARE(p->heard[u"s"_s].answer, u"Masło jest na liście [[Zakupy]]."_s);
        // The assistant's b.status as a tool call of the turn, in the app's language, completed.
        QVERIFY2(p->heard[u"s"_s].statuses.contains(u"Szukam w notatkach: „masło”…"_s),
                 qPrintable(p->heard[u"s"_s].statuses.join(u" | "_s)));
        const QList<QJsonObject> calls = p->updates(u"tool_call"_s);
        QCOMPARE(calls.size(), 1);
        QCOMPARE(calls.first().value(u"kind"_s).toString(), u"read"_s);
        QCOMPARE(calls.first().value(u"status"_s).toString(), u"in_progress"_s);
        const QList<QJsonObject> ends = p->updates(u"tool_call_update"_s);
        QVERIFY(!ends.isEmpty());
        QCOMPARE(ends.last().value(u"toolCallId"_s), calls.first().value(u"toolCallId"_s));
        QCOMPARE(ends.last().value(u"status"_s).toString(), u"completed"_s);
        // The search's result went to the model.
        QVERIFY(requestText(1).contains(u"Zakupy"_s));
    }

    void changeAllowed()
    {
        auto p = pair(options());
        m_server->answers = {u"tool:append_to_journal {\"markdown\": \"- Kupić mleko dla żółwia\"}"_s,
                             u"Dopisałem mleko."_s};
        QVERIFY(p->prompt(u"w"_s, u"Zanotuj: kupić mleko dla żółwia"_s));
        QTRY_VERIFY_WITH_TIMEOUT(p->done(u"w"_s), kTimeoutMs);
        const Heard &h = p->heard[u"w"_s];
        QVERIFY2(!h.failure, qPrintable(h.failure ? h.failure->detail : QString()));
        // The question: what it would write, with the assistant's three answers as the protocol's kinds.
        QCOMPARE(h.questions.size(), 1);
        const acp::Permission &q = h.questions.first();
        QVERIFY2(q.title.contains(u"2026-11-22"_s), qPrintable(q.title));
        QVERIFY(q.details.contains(u"Kupić mleko dla żółwia"_s));
        QCOMPARE(q.options.size(), 3);
        QCOMPARE(q.options.at(0).name, u"Zgoda"_s);
        QCOMPARE(q.options.at(0).kind, u"allow_once"_s);
        QCOMPARE(q.options.at(1).kind, u"allow_always"_s);
        QCOMPARE(q.options.at(2).name, u"Nie"_s);
        QCOMPARE(q.options.at(2).kind, u"reject_once"_s);
        // Written into the day's note, the answer naming it.
        const QStringList written = filesWith(m_root, u"Kupić mleko dla żółwia"_s);
        QCOMPARE(written.size(), 1);
        QVERIFY2(written.first().contains(u"2026"_s), qPrintable(written.first()));
        QVERIFY(h.answer.startsWith(u"Dopisałem mleko."_s));
        QVERIFY2(h.answer.contains(u"✏️"_s), qPrintable(h.answer));
        QVERIFY(requestText(1).contains(u"Added to the journal of 2026-11-22."_s));
        // The change as a tool call of kind edit, completed once allowed.
        bool edit = false;
        for (const QJsonObject &u : p->updates(u"tool_call"_s))
            edit = edit || u.value(u"kind"_s).toString() == u"edit";
        QVERIFY(edit);
    }

    void changeDeclined()
    {
        auto p = pair(options());
        p->answerKind = u"reject_once"_s;
        m_server->answers = {u"tool:append_to_page {\"name\": \"Zakupy\", \"markdown\": \"- ser\"}"_s,
                             u"Dobrze, nie dopisuję."_s};
        QVERIFY(p->prompt(u"d"_s, u"Dopisz ser do zakupów"_s));
        QTRY_VERIFY_WITH_TIMEOUT(p->done(u"d"_s), kTimeoutMs);
        const Heard &h = p->heard[u"d"_s];
        QVERIFY(!h.failure);
        QCOMPARE(h.questions.size(), 1);
        QVERIFY(h.questions.first().title.contains(u"Zakupy"_s));
        QCOMPARE(h.answer, u"Dobrze, nie dopisuję."_s);
        QVERIFY(filesWith(m_root, u"- ser"_s).isEmpty());
        QVERIFY(requestText(1).contains(u"The user declined this change."_s));
        // The change's tool call failed (declined).
        QCOMPARE(p->updates(u"tool_call_update"_s).last().value(u"status"_s).toString(), u"failed"_s);
    }

    void everyChangeAllowed()
    {
        // "Allow every change of this question": the second change of the turn is not asked.
        auto p = pair(options());
        p->answerKind = u"allow_always"_s;
        m_server->answers = {u"tool:append_to_page {\"name\": \"Zakupy\", \"markdown\": \"- jabłka\"}"_s,
                             u"tool:append_to_page {\"name\": \"Projekty/Dom\", \"markdown\": \"- [ ] Kupić farbę\"}"_s,
                             u"Dopisane."_s};
        QVERIFY(p->prompt(u"e"_s, u"Dopisz jabłka do zakupów i farbę do domu"_s));
        QTRY_VERIFY_WITH_TIMEOUT(p->done(u"e"_s), kTimeoutMs);
        const Heard &h = p->heard[u"e"_s];
        QVERIFY2(!h.failure, qPrintable(h.failure ? h.failure->detail : QString()));
        QCOMPARE(h.questions.size(), 1);
        QCOMPARE(filesWith(m_root, u"- jabłka"_s), QStringList{u"Zakupy.md"_s});
        QCOMPARE(filesWith(m_root, u"Kupić farbę"_s), QStringList{u"Projekty/Dom.md"_s});
    }

    void cancelled()
    {
        auto p = pair(options());
        m_server->hold = true;
        m_server->answers = {u"Za późno."_s, u"Teraz tak."_s};
        QVERIFY(p->prompt(u"c"_s, u"Napisz długi wiersz"_s));
        QTRY_COMPARE_WITH_TIMEOUT(m_server->waiting(), 1, kTimeoutMs);
        const QString sid = p->client->sessionId(u"c"_s);
        QVERIFY(p->agent->running(sid));
        p->client->cancel(u"c"_s);
        QTRY_VERIFY_WITH_TIMEOUT(!p->agent->running(sid), kTimeoutMs);
        QTRY_VERIFY_WITH_TIMEOUT(std::ranges::any_of(p->fromAgent,
                                                     [](const QJsonObject &m) {
                                                         return m.value(u"result"_s).toObject().value(u"stopReason"_s)
                                                                == u"cancelled"_s;
                                                     }),
                                 kTimeoutMs);
        // The model's answer that comes after all is no answer of anything.
        m_server->release();
        m_server->hold = false;
        QTest::qWait(200);
        QVERIFY(!p->done(u"c"_s));
        // The conversation goes on.
        QVERIFY(p->prompt(u"c"_s, u"A teraz?"_s));
        QTRY_VERIFY_WITH_TIMEOUT(p->done(u"c"_s), kTimeoutMs);
        QCOMPARE(p->heard[u"c"_s].answer, u"Teraz tak."_s);
    }

    void noKey()
    {
        // Claude without a key: the prompt fails with what is missing, and nothing is asked.
        AcpAgentOptions o = options();
        o.ai = ai::Settings{};
        o.aiProblem = aiProblemForCli(o.ai);
        QVERIFY(o.aiProblem.contains(u"no API key"_s));
        auto p = pair(o);
        QVERIFY(p->prompt(u"k"_s, u"Cześć"_s));
        QTRY_VERIFY_WITH_TIMEOUT(p->done(u"k"_s), kTimeoutMs);
        QVERIFY(p->heard[u"k"_s].failure);
        QCOMPARE(p->heard[u"k"_s].failure->kind, acp::Failure::Kind::Error);
        QCOMPARE(p->heard[u"k"_s].failure->detail, o.aiProblem);
        QVERIFY(m_server->requests.isEmpty());
        // The model on the device runs only in the app; an OpenAI-compatible API needs a model.
        ai::Settings local;
        local.provider = u"local"_s;
        QVERIFY(aiProblemForCli(local).contains(u"only in the Brulion app"_s));
        ai::Settings server;
        server.provider = u"openai"_s;
        server.baseUrl = m_server->url();
        QVERIFY(aiProblemForCli(server).contains(u"no model"_s));
        server.model = u"fake"_s;
        QVERIFY(aiProblemForCli(server).isEmpty());
    }

    void modelFails()
    {
        // The provider's own words, without the script's line.
        AcpAgentOptions o = options();
        o.ai.baseUrl = u"http://127.0.0.1:1/v1"_s; // (nothing listens there)
        auto p = pair(o);
        QVERIFY(p->prompt(u"f"_s, u"Cześć"_s));
        QTRY_VERIFY_WITH_TIMEOUT(p->done(u"f"_s), kTimeoutMs);
        QVERIFY(p->heard[u"f"_s].failure);
        QVERIFY2(p->heard[u"f"_s].failure->detail.startsWith(u"AI: "_s), qPrintable(p->heard[u"f"_s].failure->detail));
        QVERIFY(!p->heard[u"f"_s].failure->detail.contains(u".luau"_s));
    }

    void noNotebook()
    {
        auto p = std::make_unique<Pair>(nullptr, u"No notebook: give --vault <folder>"_s, options(), m_transport.get());
        QVERIFY(p->prompt(u"n"_s, u"Cześć"_s));
        QTRY_VERIFY_WITH_TIMEOUT(p->done(u"n"_s), kTimeoutMs);
        QVERIFY(p->heard[u"n"_s].failure);
        QCOMPARE(p->heard[u"n"_s].failure->detail, u"No notebook: give --vault <folder>"_s);
        QVERIFY(p->agent->sessions().isEmpty());
    }

    void promptBlocks()
    {
        // A file of the notebook linked is its page's link, one embedded comes as its text.
        auto p = pair(options());
        m_server->answers = {u"Widzę."_s};
        const QString dom = QUrl::fromLocalFile(m_root + u"/Projekty/Dom.md"_s).toString();
        QVERIFY(p->prompt(u"b"_s, {acp::Block::textBlock(u"Co jest w "_s), acp::Block::link(dom, u"Dom.md"_s),
                                   acp::Block::textBlock(u" i tu?"_s),
                                   acp::Block::resource(u"file:///home/ala/kod/main.cpp"_s, u"main.cpp"_s,
                                                        u"int main() { return 0; }"_s)}));
        QTRY_VERIFY_WITH_TIMEOUT(p->done(u"b"_s), kTimeoutMs);
        const QString asked = FakeAiServer::lastUserText(m_server->requests.at(0));
        QVERIFY2(asked.contains(u"Co jest w [[Projekty/Dom]] i tu?"_s), qPrintable(asked));
        QVERIFY(asked.contains(u"[main.cpp](file:///home/ala/kod/main.cpp):"_s));
        QVERIFY(asked.contains(u"int main() { return 0; }"_s));
    }

    void overStandardStreams()
    {
#ifdef Q_OS_WIN
        QSKIP("the private home is XDG folders");
#else
        // brulion-cli as an editor starts it: the app's settings and key from its home.
        QTemporaryDir home;
        QVERIFY(home.isValid());
        writeFile(home.filePath(u"config/Brulion/Brulion.ini"_s),
                  "[ai]\nprovider=openai\nbaseUrl=" + m_server->url().toUtf8() + "\nmodel=fake\n\n[ui]\nlanguage=pl\n");
        writeFile(home.filePath(u"data/Brulion/Brulion/secrets/ai-key"_s), "sk-test-ąę\n");
        QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
        env.insert(u"HOME"_s, home.path());
        env.insert(u"XDG_CONFIG_HOME"_s, home.filePath(u"config"_s));
        env.insert(u"XDG_DATA_HOME"_s, home.filePath(u"data"_s));
        env.insert(u"XDG_STATE_HOME"_s, home.filePath(u"state"_s));
        env.insert(u"XDG_CACHE_HOME"_s, home.filePath(u"cache"_s));
        env.remove(u"BRULION_PROFILE"_s);
        env.remove(u"BRULION_VAULT"_s);
        env.insert(u"LANG"_s, u"C.UTF-8"_s);
        QProcess process;
        process.setProcessEnvironment(env);
        acp::Client client([&process](const QByteArray &line) { process.write(line); }, u"0-test"_s);
        QObject::connect(&process, &QProcess::readyReadStandardOutput, &client,
                         [&] { client.received(process.readAllStandardOutput()); });
        QString answer, failure;
        QList<acp::Permission> questions;
        QObject::connect(&client, &acp::Client::finished, &client,
                         [&answer](const QString &, const QString &text, const QString &) { answer = text; });
        QObject::connect(&client, &acp::Client::failed, &client,
                         [&failure](const QString &, const acp::Failure &f) { failure = f.detail; });
        QObject::connect(&client, &acp::Client::permissionAsked, &client,
                         [&](const QString &key, const acp::Permission &p) {
                             questions << p;
                             client.answer(key, p.options.first().id);
                         });
        process.start(QStringLiteral(BRULION_CLI), {u"--vault"_s, m_root, u"--now"_s, u"2026-11-22 10:00"_s,
                                                    u"acp"_s, u"agent"_s});
        QVERIFY(process.waitForStarted());
        m_server->answers = {u"tool:append_to_journal {\"markdown\": \"- Zadzwonić do mamy\"}"_s, u"Zanotowane."_s};
        acp::TurnInput input;
        input.cwd = m_root;
        input.prompt = {acp::Block::textBlock(u"Zanotuj: zadzwonić do mamy"_s)};
        QVERIFY(client.prompt(u"p"_s, input));
        QTRY_VERIFY_WITH_TIMEOUT(!answer.isEmpty() || !failure.isEmpty(), kTimeoutMs);
        QVERIFY2(failure.isEmpty(), qPrintable(failure));
        QVERIFY(answer.startsWith(u"Zanotowane."_s));
        QCOMPARE(questions.size(), 1);
        QCOMPARE(questions.first().options.first().name, u"Zgoda"_s); // (the app's language)
        QCOMPARE(filesWith(m_root, u"Zadzwonić do mamy"_s).size(), 1);
        // The app's key went to the model's server.
        QCOMPARE(m_server->requests.size(), 2);
        QCOMPARE(m_server->authorizations.first(), QByteArray("Bearer ") + u"sk-test-ąę"_s.toUtf8());
        // The editor closes it: its input ends, and so does it.
        process.closeWriteChannel();
        QVERIFY(process.waitForFinished(kTimeoutMs));
        QCOMPARE(process.exitCode(), 0);
        const QString log = QString::fromUtf8(process.readAllStandardError());
        QVERIFY2(log.contains(u"asking openai (fake)"_s), qPrintable(log));
        QVERIFY(!log.contains(u"sk-test"_s));
        QVERIFY(!log.contains(u"mamy"_s)); // (no note's text in the log)
#endif
    }

    void cleanupTestCase()
    {
        m_server.reset();
        m_nb.reset();
    }
};

QTEST_GUILESS_MAIN(TestCliAcpAgent)
#include "tst_cli_acp_agent.moc"
