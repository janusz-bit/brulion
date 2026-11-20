// The AI chat as the app runs it, on a real Workspace and its notebook, the model on the device
// played by the test (ai::LocalBackend): a new conversation (its page in AI/ named after the
// question, the page it is about, the journal's line that links to it), the assistant's turn (the
// conversation's messages and the page's text in the request, what it says it does, the answer
// written into the page), the next message with the history, a change asked a yes for in the
// conversation (questionChat) and written, Stop (nothing written for that turn), a failure and Try
// again, the extensions loaded again during a turn, the assistant switched off; the messages as the view shows them (ChatMessageModel: roles,
// rendered text — links, lists, numbers, tasks, headings, code —, the page it is about) and the
// list of conversations (ChatListModel: only conversations of AI/, newest first, their names
// without the date, previews); the time of each message written and shown, a day's line above it,
// pictures sent with a message (attachments of the notebook, the model's images, a model that does
// not see them), several conversations answering at once (an OpenAI-compatible server played by
// FakeAiServer) and the model on the device taking them one at a time in line, a conversation too
// long for the model sent from a later turn (the model told, the view's line), a partner of the
// user's own (an extension's chat command), renaming and deleting a conversation, and Claude's
// thinking the turn shows.
#include "chatmodels.h"

#include "workspace.h"
#include "workspacechats.h"
#include "workspaceextensions.h"

#include "../fakeaiserver.h"

#include "ai/local.h"
#include "vault/vault.h"

#include <QDir>
#include <QFile>
#include <QImage>
#include <QLocale>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>
#include <QTimer>

using namespace Qt::StringLiterals;
using namespace brulion;
using namespace brulion::content;
using brulion::shell::Workspace;

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

QVariant value(const QAbstractItemModel &model, int row, const QByteArray &role)
{
    return model.data(model.index(row, 0), model.roleNames().key(role, -1));
}

ai::Reply answer(const QString &text)
{
    ai::Reply r;
    r.ok = true;
    r.text = text;
    r.model = u"lfm3-1.7b"_s;
    r.raw = "[]";
    return r;
}

ai::Reply calling(const QString &name, const QVariantMap &input)
{
    ai::Reply r = answer(QString());
    r.toolCalls.append(ai::ToolCall{u"c1"_s, name, input});
    return r;
}

/// The model on the device, played: its replies in order (a text when none is left); `hold` keeps
/// the next one until release().
struct Model : ai::LocalBackend {
    QList<ai::Request> requests;
    QList<ai::Reply> replies;
    bool hold = false;
    std::function<void()> held;
    /// What the model writes before each answer, in order (shown as partial answers).
    QList<ai::Partial> writing;
    ai::PartialCallback partial; ///< the last request's
    std::shared_ptr<std::atomic<bool>> stopped; ///< the last request's
    void send(const ai::Settings &, const ai::Request &request, std::function<void(const ai::Reply &)> done,
              ai::PartialCallback watch, std::shared_ptr<std::atomic<bool>> stop) override
    {
        requests << request;
        partial = watch;
        stopped = stop;
        for (const ai::Partial &p : std::as_const(writing))
            QTimer::singleShot(0, [watch, p] {
                if (watch)
                    watch(p);
            });
        const ai::Reply reply = replies.isEmpty() ? answer(u"Dobrze."_s) : replies.takeFirst();
        if (hold) {
            held = [done, reply] { done(reply); };
            return;
        }
        QTimer::singleShot(0, [done, reply] { done(reply); });
    }
    void release()
    {
        if (held)
            std::exchange(held, nullptr)();
    }
};

} // namespace

class TestContentChat : public QObject {
    Q_OBJECT

    QTemporaryDir m_tmp;
    int m_n = 0;
    QDateTime m_now{QDate(2026, 11, 15), QTime(10, 30)};
    Model m_model;

    std::unique_ptr<Workspace> open(const QHash<QString, QString> &files, bool assistant = true)
    {
        const QString root = m_tmp.filePath(u"vault-%1"_s.arg(++m_n));
        for (auto it = files.cbegin(); it != files.cend(); ++it)
            writeRaw(root + u'/' + it.key(), it.value().toUtf8());
        auto ws = std::make_unique<Workspace>();
        ws->setClock([this] { return m_now; });
        ws->setUiLanguage(u"pl"_s);
        QString error;
        if (!ws->open(root, root + u"-state"_s, &error))
            qWarning() << error;
        ws->finishIndexing();
        ai::Settings local;
        local.provider = u"local"_s;
        ws->extensions()->setAi(local);
        ws->extensions()->setLocalAi(&m_model);
        if (assistant)
            ws->extensions()->setEnabled({u"asystent"_s});
        QCoreApplication::processEvents();
        return ws;
    }

    static const QHash<QString, QString> &notes()
    {
        static const QHash<QString, QString> n{
                {u"Dom.md"_s, u"- [ ] Zapłacić czynsz 📅 2026-11-16\n- Żółw je sałatę\n"_s},
                {u"Zakupy.md"_s, u"- chleb\n"_s},
        };
        return n;
    }

private Q_SLOTS:
    void initTestCase() { QVERIFY(m_tmp.isValid()); }
    void init()
    {
        m_model = Model();
        m_now = QDateTime(QDate(2026, 11, 15), QTime(10, 30));
    }

    void aConversation()
    {
        auto ws = open(notes());
        shell::WorkspaceChats *chats = ws->chats();
        QSignalSpy written(chats, &shell::WorkspaceChats::written);
        QStringList statuses;
        m_model.replies = {calling(u"search_notes"_s, {{u"query"_s, u"jutro"_s}}),
                           answer(u"Jutro masz jedno zadanie:\n\n- zapłacić czynsz — [[Dom]] ⏰ 2026-11-16 09:00"_s)};

        // A new conversation: its page, named after the question, about the page; the day's line.
        const QString path = chats->send(QString(), u"  Co mam jutro do zrobienia?  "_s, u"Dom"_s);
        QCOMPARE(path, u"AI/2026-11-15 Co mam jutro do zrobienia.md"_s);
        shell::ChatTurn *turn = chats->turn(path);
        connect(turn, &shell::ChatTurn::stateChanged, this, [&] {
            if (!turn->status().isEmpty() && (statuses.isEmpty() || statuses.last() != turn->status()))
                statuses << turn->status();
        });
        QVERIFY(turn->running());
        QCOMPARE(chats->runningCount(), 1);
        QCOMPARE(ws->read(path), u"---\nzadania: ignoruj\n---\n- 🧑 Co mam jutro do zrobienia? ([[Dom]])\n"
                                 "  czas:: 2026-11-15 10:30\n"_s);
        QVERIFY2(ws->read(ws->journalPath(u"2026-11-15"_s)).contains(u"- 🤖 [[AI/2026-11-15 Co mam jutro do zrobienia]]"_s),
                 qPrintable(ws->read(ws->journalPath(u"2026-11-15"_s))));
        QVERIFY(chats->isConversation(path));
        QVERIFY(!chats->isConversation(u"Dom.md"_s));
        // A second message waits for the turn.
        QCOMPARE(chats->send(path, u"A pojutrze?"_s), QString());

        // The answer lands in the page with its time; the page's text went along; what it did was said.
        m_now = m_now.addSecs(60);
        QTRY_VERIFY(!turn->running());
        QCOMPARE(chats->runningCount(), 0);
        QCOMPARE(ws->read(path), u"---\nzadania: ignoruj\n---\n- 🧑 Co mam jutro do zrobienia? ([[Dom]])\n  czas:: 2026-11-15 10:30\n"
                                 "- 🤖 Jutro masz jedno zadanie:\n  czas:: 2026-11-15 10:31\n\n"
                                 "  - zapłacić czynsz — [[Dom]] `⏰ 2026-11-16 09:00`\n"_s);
        QCOMPARE(written.size(), 2); // the question, the answer
        QCOMPARE(m_model.requests.size(), 2);
        const ai::Message &first = m_model.requests[0].messages.first();
        // (The day with its weekday: tst_extensions_assistant.)
        QVERIFY2(first.content.startsWith(
                         u"Dziś jest niedziela, 2026-11-15.\n\nRozmowa dotyczy strony [[Dom]]:\n\n- [ ] Zapłacić czynsz"_s),
                 qPrintable(first.content));
        QVERIFY(first.content.endsWith(u"Co mam jutro do zrobienia? ([[Dom]])"_s));
        QVERIFY(!m_model.requests[0].tools.isEmpty());
        QCOMPARE(statuses, QStringList{u"Szukam w notatkach: „jutro”…"_s});
        QVERIFY(turn->status().isEmpty());
        // (The chat asks Claude to show its thinking: other providers send theirs by themselves.)
        QVERIFY(m_model.requests[0].thoughts);

        // The next message: the history goes with it (no time lines in it).
        QCOMPARE(chats->send(path, u"A pojutrze?"_s), path);
        QTRY_VERIFY(!turn->running());
        const QList<ai::Message> history = m_model.requests[2].messages;
        QCOMPARE(history.size(), 3);
        QCOMPARE(history[1].role, u"assistant"_s);
        QVERIFY(history[1].content.startsWith(u"Jutro masz jedno zadanie:"_s));
        QCOMPARE(history[2].content, u"A pojutrze?"_s);
        QVERIFY2(ws->read(path).endsWith(u"- 🧑 A pojutrze?\n  czas:: 2026-11-15 10:31\n- 🤖 Dobrze.\n  czas:: 2026-11-15 10:31\n"_s),
                 qPrintable(ws->read(path)));

        // A second conversation with the same question: a name of its own.
        m_model.replies = {answer(u"Nic."_s)};
        const QString second = chats->send(QString(), u"Co mam jutro do zrobienia?"_s);
        QCOMPARE(second, u"AI/2026-11-15 Co mam jutro do zrobienia 2.md"_s);
        QTRY_VERIFY(!chats->turn(second)->running());

        // The list: both, the newest change first, names without the date.
        ChatListModel list;
        list.setWorkspace(ws.get());
        list.refreshNow();
        QCOMPARE(list.rowCount(), 2);
        QCOMPARE(value(list, 0, "path").toString(), u"AI/2026-11-15 Co mam jutro do zrobienia 2.md"_s);
        QCOMPARE(value(list, 0, "title").toString(), u"Co mam jutro do zrobienia 2"_s);
        QCOMPARE(value(list, 0, "date").toString(), u"2026-11-15"_s);
        QCOMPARE(value(list, 0, "preview").toString(), u"Nic."_s);
        QVERIFY(value(list, 0, "fromAi").toBool());
        QCOMPARE(value(list, 1, "messages").toInt(), 4);
        QCOMPARE(list.rowOf(path), 1);
    }

    void aChangeIsAskedInTheConversation()
    {
        auto ws = open(notes());
        shell::WorkspaceChats *chats = ws->chats();
        shell::WorkspaceExtensions *ext = ws->extensions();
        m_model.replies = {calling(u"append_to_page"_s, {{u"name"_s, u"Zakupy"_s}, {u"markdown"_s, u"- mleko"_s}}),
                           answer(u"Dopisałem mleko."_s)};
        const QString path = chats->send(QString(), u"Dopisz mleko do zakupów"_s);
        // The question names the conversation it belongs to; the view shows it there.
        QTRY_COMPARE(ext->questionKind(), u"choose"_s);
        QCOMPARE(ext->questionChat(), path);
        QVERIFY(ext->questionTitle().contains(u"- mleko"_s));
        QVERIFY(chats->turn(path)->running());
        QCOMPARE(ext->chatQuestion(path).value(u"kind"_s).toString(), u"choose"_s);
        QVERIFY(ext->chatQuestion(path).value(u"title"_s).toString().contains(u"- mleko"_s));
        QVERIFY(ext->chatQuestion(u"AI/inna.md"_s).isEmpty());
        chats->showing(path, true);
        QVERIFY(chats->isShown(path));
        QVERIFY(chats->isShown(path.toUpper()));
        ext->answerChoice(ext->questionOptions().value(0));
        QCOMPARE(ext->questionChat(), QString());
        QTRY_VERIFY(!chats->turn(path)->running());
        chats->showing(path, false);
        QVERIFY(!chats->isShown(path));
        QCOMPARE(ws->read(u"Zakupy.md"_s), u"- chleb\n- mleko\n"_s);
        QVERIFY2(ws->read(path).endsWith(u"- 🤖 Dopisałem mleko.\n  czas:: 2026-11-15 10:30\n\n  ✏️ Zmienione: [[Zakupy]]\n"_s),
                 qPrintable(ws->read(path)));
    }

    void stopFailAndTryAgain()
    {
        auto ws = open(notes());
        shell::WorkspaceChats *chats = ws->chats();
        // Stop: the answer on its way is dropped, the question stays for Try again.
        m_model.hold = true;
        const QString path = chats->send(QString(), u"Długie pytanie"_s);
        shell::ChatTurn *turn = chats->turn(path);
        QTRY_COMPARE(m_model.requests.size(), 1);
        chats->stop(path);
        QVERIFY(!turn->running());
        m_model.release();
        QCoreApplication::processEvents();
        QVERIFY(!turn->running());
        QVERIFY(ws->read(path).endsWith(u"- 🧑 Długie pytanie\n  czas:: 2026-11-15 10:30\n"_s));
        QCOMPARE(ws->extensions()->aiPending(), 0);

        // A failure: said in the conversation, Try again runs the turn on the page as it is.
        m_model.hold = false;
        ai::Reply broken;
        broken.error = u"the model could not be loaded"_s;
        m_model.replies = {broken, answer(u"Jednak tak."_s)};
        QVERIFY(chats->retry(path));
        QTRY_VERIFY(!turn->failure().isEmpty());
        QVERIFY2(turn->failure().startsWith(u"AI: "_s) && turn->failure().contains(u"could not be loaded"_s),
                 qPrintable(turn->failure()));
        QVERIFY(ws->read(path).endsWith(u"- 🧑 Długie pytanie\n  czas:: 2026-11-15 10:30\n"_s));
        QVERIFY(chats->retry(path));
        QCOMPARE(turn->failure(), QString());
        QTRY_VERIFY(!turn->running());
        QVERIFY(ws->read(path).endsWith(u"- 🤖 Jednak tak.\n  czas:: 2026-11-15 10:30\n"_s));
        // Nothing to answer: the last message is the AI's.
        QVERIFY(!chats->retry(path));
        QVERIFY(!turn->failure().isEmpty());
        chats->dismissFailure(path);
        QCOMPARE(turn->failure(), QString());
        // Nothing to send.
        QCOMPARE(chats->send(path, u"   "_s), QString());

        // The extensions loaded again while the model answers: the turn ends, said so, and its
        // answer, when it comes, is written nowhere.
        m_model.hold = true;
        const qsizetype before = m_model.requests.size();
        QCOMPARE(chats->send(path, u"Jeszcze raz?"_s), path);
        QTRY_COMPARE(m_model.requests.size(), before + 1);
        ws->extensions()->reloadAll();
        QVERIFY(!turn->running());
        QVERIFY2(turn->failure().contains(u"loaded again"_s), qPrintable(turn->failure()));
        m_model.release();
        QCoreApplication::processEvents();
        QVERIFY(ws->read(path).endsWith(u"- 🧑 Jeszcze raz?\n  czas:: 2026-11-15 10:30\n"_s));
    }

    void theAnswerAsItIsWritten()
    {
        auto ws = open(notes());
        shell::WorkspaceChats *chats = ws->chats();
        m_model.hold = true;
        m_model.writing = {{QString(), u"Użytkownik pyta o jutro."_s, 20, 1000},
                           {u"Jutro masz"_s, u"Użytkownik pyta o jutro."_s, 60, 3000}};
        m_model.replies = {answer(u"Jutro masz jogę."_s)};
        const QString path = chats->send(QString(), u"Co jutro?"_s);
        shell::ChatTurn *turn = chats->turn(path);
        QVERIFY(m_model.partial); // (the chat watches its requests)
        QTRY_COMPARE(turn->partial(), u"Jutro masz"_s);
        QCOMPARE(turn->thoughts(), u"Użytkownik pyta o jutro."_s);
        QCOMPARE(turn->allThoughts(), u"Użytkownik pyta o jutro."_s);
        QCOMPARE(turn->speed(), QLocale().toString(20.0, 'f', 1) + u" tok/s"_s); // 40 tokens in the last 2 s
        QCOMPARE(turn->partialSettled(), QString()); // (one block, being written)
        QCOMPARE(turn->partialTail(), u"Jutro masz"_s);
        QVERIFY(turn->running());
        // Shown in a view: the top bar's ✨ need not spin.
        QSignalSpy shownSpy(chats, &shell::WorkspaceChats::runningShownChanged);
        QVERIFY(!chats->runningShown());
        chats->showing(path, true);
        QVERIFY(chats->runningShown());
        chats->showing(path, false);
        QVERIFY(!chats->runningShown());
        QCOMPARE(shownSpy.size(), 2);
        chats->showing(path, true); // (and the end of the turn ends it)
        // The answer's message is written while the answer being written is still there; then
        // the turn ends and it goes: a view never shows neither.
        bool bothThere = false;
        connect(chats, &shell::WorkspaceChats::written, this, [&](const QString &p) {
            if (p == path && ws->read(path).contains(u"- 🤖"_s))
                bothThere = turn->partial() == u"Jutro masz" && turn->running();
        });
        m_model.release();
        QTRY_VERIFY(!turn->running());
        QVERIFY(bothThere);
        QCOMPARE(turn->partial(), QString());
        QCOMPARE(turn->thoughts(), QString());
        QCOMPARE(turn->speed(), QString());
        QVERIFY(!chats->runningShown());
        chats->showing(path, false);
        QVERIFY(ws->read(path).endsWith(u"- 🤖 Jutro masz jogę.\n  czas:: 2026-11-15 10:30\n"_s));

        // Each request of the turn starts anew: what the first wrote goes when a tool has run.
        m_model.writing = {{u"Sprawdzę."_s, QString()}};
        m_model.replies = {calling(u"search_notes"_s, {{u"query"_s, u"joga"_s}}), answer(u"Joga jest jutro."_s)};
        const qsizetype before = m_model.requests.size();
        QCOMPARE(chats->send(path, u"Na pewno?"_s), path);
        QTRY_COMPARE(turn->partial(), u"Sprawdzę."_s);
        QCOMPARE(turn->speed(), QString()); // (no counts: a provider on the network)
        m_model.release(); // the tool runs, the next request goes out (at once)
        QCOMPARE(m_model.requests.size(), before + 2);
        QCOMPARE(turn->partial(), QString());
        QTRY_COMPARE(turn->partial(), u"Sprawdzę."_s);
        m_model.release();
        QTRY_VERIFY(!turn->running());
        QCOMPARE(turn->partial(), QString());
        QVERIFY(ws->read(path).endsWith(u"- 🤖 Joga jest jutro.\n  czas:: 2026-11-15 10:30\n"_s));
    }

    void stopStopsTheModel()
    {
        // Stop: the model on the device stops writing — it holds up no other question.
        auto ws = open(notes());
        shell::WorkspaceChats *chats = ws->chats();
        m_model.hold = true;
        const QString path = chats->send(QString(), u"Długie pytanie"_s);
        QTRY_COMPARE(m_model.requests.size(), 1);
        QVERIFY(m_model.stopped && !m_model.stopped->load());
        chats->stop(path);
        QVERIFY(m_model.stopped->load());
        QTRY_COMPARE(ws->extensions()->aiPending(), 0);
        // The extensions loaded again while it writes: it stops too.
        QVERIFY(chats->retry(path));
        QTRY_COMPARE(m_model.requests.size(), 2);
        QVERIFY(!m_model.stopped->load());
        ws->extensions()->reloadAll();
        QVERIFY(m_model.stopped->load());
        m_model.release();
    }

    void theThoughtsEndAndTheSpeed()
    {
        using shell::WorkspaceChats;
        QCOMPARE(WorkspaceChats::thoughtsTail(u"  Krótko.  "_s), u"Krótko."_s);
        QString words;
        for (int i = 0; i < 80; ++i)
            words += u"słowo%1 "_s.arg(i);
        const QString tail = WorkspaceChats::thoughtsTail(words, 100);
        QVERIFY2(tail.startsWith(u"…słowo"_s) && tail.endsWith(u"słowo79"_s) && tail.size() <= 101, qPrintable(tail));
        // One long word: its end.
        const QString one = WorkspaceChats::thoughtsTail(QString(300, u'ż'), 100);
        QCOMPARE(one, u"…"_s + QString(100, u'ż'));

        // The answer being written, cut where its block being written starts: before the last
        // block at the margin after a blank line, never inside a code block.
        const auto split = [](const QString &markdown) { return WorkspaceChats::splitWritten(markdown); };
        using Parts = std::pair<QString, QString>;
        QCOMPARE(split(u"Akapit."_s), Parts(QString(), u"Akapit."_s));
        QCOMPARE(split(u"Pierwszy.\n\nDrugi"_s), Parts(u"Pierwszy."_s, u"Drugi"_s));
        QCOMPARE(split(u"Lista:\n\n- a\n- b\n\nKoniec"_s), Parts(u"Lista:\n\n- a\n- b"_s, u"Koniec"_s));
        QCOMPARE(split(u"Kod:\n\n```\nx\n\ny\n"_s), Parts(u"Kod:"_s, u"```\nx\n\ny\n"_s));
        QCOMPARE(split(u"```\na\n\nb\n```\n\nPo kodzie"_s), Parts(u"```\na\n\nb\n```"_s, u"Po kodzie"_s));
        QCOMPARE(split(u"~~~~\na\n```\n\nwciąż kod"_s), Parts(QString(), u"~~~~\na\n```\n\nwciąż kod"_s));
        QCOMPARE(split(u"- a\n\n  dalej punktu a"_s), Parts(QString(), u"- a\n\n  dalej punktu a"_s));
        QCOMPARE(split(u"Raz.\n\n"_s), Parts(QString(), u"Raz.\n\n"_s));
        QCOMPARE(split(QString()), Parts(QString(), QString()));

        // The speed now: over the last two seconds, not since the start — a phone that slowed
        // down (hot) says so at once.
        QList<std::pair<qint64, int>> samples;
        for (int t = 1; t <= 80; ++t)
            samples.append({qint64(t) * 50, t}); // 20 a second for 4 s
        QCOMPARE(WorkspaceChats::tokensPerSecond(samples), 20.0);
        for (int t = 81; t <= 90; ++t)
            samples.append({4000 + qint64(t - 80) * 200, t}); // then 5 a second for 2 s
        QCOMPARE(WorkspaceChats::tokensPerSecond(samples), 5.0);
        // At the start (less than a second): since the writing began.
        QCOMPARE(WorkspaceChats::tokensPerSecond({{100, 2}, {300, 6}}), 20.0);
        QCOMPARE(WorkspaceChats::tokensPerSecond({}), 0.0);
    }

    void theAssistantOff()
    {
        auto ws = open(notes(), false);
        shell::WorkspaceChats *chats = ws->chats();
        QCOMPARE(chats->send(QString(), u"Halo?"_s), QString());
        const QString failure = chats->turn(QString())->failure();
        QVERIFY2(failure.contains(u"Settings"_s) || failure.contains(u"Ustawieniach"_s), qPrintable(failure));
        QVERIFY(!ws->exists(u"AI/2026-11-15 Halo.md"_s));
        QVERIFY(m_model.requests.isEmpty());
    }

    void severalConversationsAtOnce()
    {
        // A provider on the network: every conversation's turn at once, each its own.
        FakeAiServer server;
        server.hold = true;
        server.answers = {u"Pierwsza odpowiedź."_s, u"Druga odpowiedź."_s};
        auto ws = open(notes());
        ai::Settings settings;
        settings.provider = u"openai"_s;
        settings.baseUrl = server.url();
        settings.model = u"fake"_s;
        ws->extensions()->setAi(settings);
        shell::WorkspaceChats *chats = ws->chats();
        QVERIFY(!chats->onDevice());
        QCOMPARE(chats->historyBudget(), shell::WorkspaceChats::NetworkBudget);
        const QString a = chats->send(QString(), u"Pierwsze pytanie"_s);
        const QString b = chats->send(QString(), u"Drugie pytanie"_s);
        QVERIFY(!a.isEmpty() && !b.isEmpty() && a != b);
        QTRY_COMPARE(server.waiting(), 2); // both asked, neither answered
        QVERIFY(chats->turn(a)->running() && chats->turn(b)->running());
        QVERIFY(!chats->turn(a)->queued() && !chats->turn(b)->queued());
        QCOMPARE(chats->runningCount(), 2);
        // The list says so.
        ChatListModel list;
        list.setWorkspace(ws.get());
        list.refreshNow();
        QVERIFY(value(list, list.rowOf(a), "running").toBool());
        QVERIFY(!value(list, list.rowOf(a), "queued").toBool());
        // A conversation whose turn runs takes no message meanwhile; the other ones do.
        QCOMPARE(chats->send(a, u"Jeszcze?"_s), QString());
        // Stop one: the other goes on.
        chats->stop(a);
        QVERIFY(!chats->turn(a)->running());
        QVERIFY(chats->turn(b)->running());
        server.release();
        QTRY_VERIFY(!chats->turn(b)->running());
        QVERIFY2(ws->read(b).contains(u"- 🤖 "_s), qPrintable(ws->read(b)));
        QVERIFY(!ws->read(a).contains(u"- 🤖"_s));
        QCOMPARE(chats->runningCount(), 0);
        QCOMPARE(ws->extensions()->aiPending(), 0);
    }

    void twoQuestionsAtOnce()
    {
        // Two turns ask for a yes at once: the second waits behind the first — never cancelled —,
        // and each conversation shows its own; answered in either order.
        FakeAiServer server;
        server.answers = {u"tool:append_to_page {\"name\": \"Zakupy\", \"markdown\": \"- mleko\"}"_s,
                          u"tool:append_to_page {\"name\": \"Zakupy\", \"markdown\": \"- jajka\"}"_s};
        auto ws = open(notes());
        ai::Settings settings;
        settings.provider = u"openai"_s;
        settings.baseUrl = server.url();
        settings.model = u"fake"_s;
        ws->extensions()->setAi(settings);
        shell::WorkspaceChats *chats = ws->chats();
        shell::WorkspaceExtensions *ext = ws->extensions();
        const QString a = chats->send(QString(), u"Dopisz mleko do zakupów"_s);
        QTRY_COMPARE(ext->questionChat(), a);
        const QString b = chats->send(QString(), u"Dopisz jajka do zakupów"_s);
        QTRY_VERIFY(!ext->chatQuestion(b).isEmpty());
        QCOMPARE(ext->questionChat(), a); // (the first one is still the one shown)
        QVERIFY(ext->chatQuestion(b).value(u"title"_s).toString().contains(u"- jajka"_s));
        QVERIFY(ext->chatQuestion(a).value(u"title"_s).toString().contains(u"- mleko"_s));
        // A command's question meanwhile is shown first; the turns' wait.
        const int before = ext->chatQuestionSerial();
        ext->evaluate(u"b.ask(\"Imię?\")"_s);
        QCOMPARE(ext->questionKind(), u"ask"_s);
        QCOMPARE(ext->questionChat(), QString());
        QVERIFY(ext->chatQuestionSerial() > before);
        QVERIFY(!ext->chatQuestion(a).isEmpty());
        ext->cancelQuestion(); // (the command's: the turn's comes back)
        QCOMPARE(ext->questionChat(), a);
        // The waiting one answered from its own conversation.
        server.answers = {u"Dopisałem jajka."_s, u"Dopisałem mleko."_s};
        ext->answerChat(b, ext->chatQuestion(b).value(u"options"_s).toStringList().value(0));
        QTRY_VERIFY(!chats->turn(b)->running());
        QVERIFY2(ws->read(b).contains(u"Dopisałem jajka."_s), qPrintable(ws->read(b)));
        QCOMPARE(ext->questionChat(), a);
        ext->answerChat(a, ext->chatQuestion(a).value(u"options"_s).toStringList().value(0));
        QTRY_VERIFY(!chats->turn(a)->running());
        QVERIFY2(ws->read(a).contains(u"Dopisałem mleko."_s), qPrintable(ws->read(a)));
        QCOMPARE(ws->read(u"Zakupy.md"_s), u"- chleb\n- jajka\n- mleko\n"_s);
        QVERIFY(ext->questionKind().isEmpty());
    }

    void theModelOnTheDeviceTakesOneAtATime()
    {
        auto ws = open(notes());
        shell::WorkspaceChats *chats = ws->chats();
        QVERIFY(chats->onDevice());
        QCOMPARE(chats->historyBudget(), shell::WorkspaceChats::DeviceBudget);
        m_model.hold = true;
        m_model.replies = {answer(u"Raz."_s), answer(u"Dwa."_s)};
        const QString a = chats->send(QString(), u"Pierwsze"_s);
        const QString b = chats->send(QString(), u"Drugie"_s);
        const QString c = chats->send(QString(), u"Trzecie"_s);
        QTRY_COMPARE(m_model.requests.size(), 1);
        shell::ChatTurn *tb = chats->turn(b), *tc = chats->turn(c);
        QVERIFY(chats->turn(a)->running() && !chats->turn(a)->queued());
        QVERIFY(tb->running() && tb->queued());
        QCOMPARE(tb->place(), 1);
        QCOMPARE(tc->place(), 2);
        QCOMPARE(chats->runningCount(), 3);
        ChatListModel list;
        list.setWorkspace(ws.get());
        list.refreshNow();
        QVERIFY(value(list, list.rowOf(b), "queued").toBool());
        QVERIFY(!value(list, list.rowOf(a), "queued").toBool());
        // The third leaves the line: the second is still next.
        chats->stop(c);
        QVERIFY(!tc->running() && !tc->queued());
        QCOMPARE(tb->place(), 1);
        m_model.release(); // the first answers: the second starts
        QTRY_COMPARE(m_model.requests.size(), 2);
        QVERIFY(!tb->queued() && tb->running());
        QVERIFY(ws->read(a).contains(u"- 🤖 Raz."_s));
        QVERIFY(m_model.requests.at(1).messages.last().content.endsWith(u"Drugie"_s));
        m_model.release();
        QTRY_VERIFY(!tb->running());
        QVERIFY(ws->read(b).contains(u"- 🤖 Dwa."_s));
        QCOMPARE(m_model.requests.size(), 2); // (the stopped one never asked)
        QVERIFY(!ws->read(c).contains(u"- 🤖"_s));
    }

    void aLongConversationForASmallModel()
    {
        // Longer than the model on the device holds: whole turns from the oldest left out, the
        // model told how many, the view's line above the first one it gets.
        QString page = u"---\nzadania: ignoruj\n---\n"_s;
        for (int i = 1; i <= 6; ++i) {
            page += u"- 🧑 Pytanie %1 "_s.arg(i) + QString(2000, u'ż') + u'\n';
            page += u"- 🤖 Odpowiedź %1 "_s.arg(i) + QString(2000, u'ę') + u'\n';
        }
        auto ws = open({{u"AI/2026-11-14 Długa.md"_s, page}});
        shell::WorkspaceChats *chats = ws->chats();
        const QString path = u"AI/2026-11-14 Długa.md"_s;
        QCOMPARE(chats->send(path, u"I co dalej?"_s), path);
        QTRY_VERIFY(!chats->turn(path)->running());
        const QList<ai::Message> sent = m_model.requests.first().messages;
        // 6 × 2 messages of 2000 and more: 16 000 characters hold the last three turns and the question.
        QVERIFY2(sent.first().content.contains(u"(Początek tej rozmowy — 6 wcześniejszych wiadomości — nie zmieścił się"_s),
                 qPrintable(sent.first().content.left(300)));
        QVERIFY(sent.first().content.contains(u"Pytanie 4"_s));
        QVERIFY(!sent.first().content.contains(u"Pytanie 3"_s));
        QCOMPARE(sent.size(), 7);
        QVERIFY(sent.last().content.endsWith(u"I co dalej?"_s));
        // As the view shows it.
        ChatMessageModel model;
        model.setWorkspace(ws.get());
        model.setPath(path);
        model.setBudget(chats->historyBudget());
        model.refreshNow();
        QCOMPARE(model.rowCount(), 14);
        QCOMPARE(model.omitted(), 6);
        QVERIFY(!value(model, 5, "sent").toBool());
        QVERIFY(value(model, 6, "sent").toBool());
        QVERIFY(value(model, 6, "firstSent").toBool());
        QVERIFY(!value(model, 7, "firstSent").toBool());
        model.setBudget(0); // (all of it)
        model.refreshNow();
        QCOMPARE(model.omitted(), 0);
        QVERIFY(!value(model, 6, "firstSent").toBool());
    }

    void picturesWithAMessage()
    {
        auto ws = open(notes());
        shell::WorkspaceChats *chats = ws->chats();
        QImage image(64, 48, QImage::Format_RGB32);
        image.fill(QColor(30, 160, 90));
        const QString file = m_tmp.filePath(u"roślina %1.png"_s.arg(m_n));
        QVERIFY(image.save(file));
        // Not a picture: refused.
        const QString text = m_tmp.filePath(u"notatka.txt"_s);
        QVERIFY(writeRaw(text, "x"));
        QCOMPARE(chats->addPicture(QUrl::fromLocalFile(text)), QString());
        // Picked, then taken off before sending: gone again.
        const QString dropped = chats->addPicture(QUrl::fromLocalFile(file));
        QVERIFY(!dropped.isEmpty() && ws->exists(dropped));
        chats->dropPicture(dropped);
        QVERIFY(!ws->exists(dropped));
        // Sent with a message: an attachment of the notebook, embedded in the message.
        const QString picture = chats->addPicture(QUrl::fromLocalFile(file));
        QVERIFY(ws->exists(picture));
        // The model on the device that reads text only gets the embed as text.
        QVERIFY(!chats->seesPictures());
        const QString path = chats->send(QString(), u"Co to za roślina?"_s, QString(), {picture});
        QVERIFY2(ws->read(path).contains(u"  ![["_s + picture + u"]]\n"_s), qPrintable(ws->read(path)));
        QTRY_VERIFY(!chats->turn(path)->running());
        QVERIFY(m_model.requests.last().messages.first().images.isEmpty());
        QVERIFY(m_model.requests.last().messages.first().content.contains(picture));
        chats->dropPicture(picture); // (sent: part of the notebook, never deleted)
        QVERIFY(ws->exists(picture));
        // One that sees: the picture goes as an image, not as text.
        ai::Settings sees;
        sees.provider = u"local"_s;
        sees.model = u"lfm3-vl-2b"_s;
        ws->extensions()->setAi(sees);
        QVERIFY(chats->seesPictures());
        QCOMPARE(chats->send(path, QString(), QString(), {picture}), path); // (a picture alone is a message)
        QTRY_VERIFY(!chats->turn(path)->running());
        const QList<ai::Message> sent = m_model.requests.last().messages;
        QCOMPARE(sent.first().images.size(), 1);
        QVERIFY(!sent.first().content.contains(u"![["_s));
        QVERIFY(!sent.first().images.first().data.isEmpty());
        // As the view shows it: the picture apart, the text without its embed.
        ChatMessageModel model;
        model.setWorkspace(ws.get());
        model.setPath(path);
        model.refreshNow();
        QCOMPARE(value(model, 0, "pictures").value<QList<QUrl>>().size(), 1);
        QVERIFY(!value(model, 0, "messageHtml").toString().contains(u"png"_s));
        QCOMPARE(value(model, 2, "messageHtml").toString(), QString()); // (a picture alone)
        QCOMPARE(value(model, 2, "pictures").value<QList<QUrl>>().size(), 1);
        // Nothing at all: nothing sent.
        QCOMPARE(chats->send(path, u"  "_s, QString(), {}), QString());
    }

    void timesAsShown()
    {
        auto ws = open({{u"AI/2026-11-14 Plan.md"_s,
                         u"- 🧑 Wczoraj\n  czas:: 2026-11-14 22:10\n- 🤖 Tak\n  czas:: 2026-11-14 22:11\n"
                         "- 🧑 Dziś\n  czas:: 2026-11-15 08:05\n- 🤖 Bez czasu\n"_s}});
        ChatMessageModel model;
        model.setWorkspace(ws.get());
        model.setPath(u"AI/2026-11-14 Plan.md"_s);
        model.refreshNow();
        QCOMPARE(model.rowCount(), 4);
        QCOMPARE(value(model, 0, "time").toString(), u"22:10"_s);
        QVERIFY(!value(model, 0, "day").toString().isEmpty()); // (the first day)
        QCOMPARE(value(model, 1, "day").toString(), QString()); // (the same day)
        QCOMPARE(value(model, 2, "time").toString(), u"08:05"_s);
        QVERIFY(!value(model, 2, "day").toString().isEmpty()); // (a new day)
        QVERIFY(value(model, 0, "day").toString() != value(model, 2, "day").toString());
        QCOMPARE(value(model, 3, "time").toString(), QString());
        QCOMPARE(value(model, 1, "markdown").toString(), u"Tak"_s); // (no time line in the text)
    }

    void renameAndDelete()
    {
        auto ws = open(notes());
        shell::WorkspaceChats *chats = ws->chats();
        const QString path = chats->send(QString(), u"Co mam jutro?"_s);
        QTRY_VERIFY(!chats->turn(path)->running());
        QCOMPARE(chats->nameOf(path), u"Co mam jutro"_s);
        shell::ChatTurn *turn = chats->turn(path);
        // A new name: the date stays in front, links to it follow (the day's line).
        const QString renamed = chats->rename(path, u"  Plan na jutro: dom  "_s);
        QCOMPARE(renamed, u"AI/2026-11-15 Plan na jutro dom.md"_s);
        QVERIFY(!ws->exists(path) && ws->exists(renamed));
        QVERIFY2(ws->read(ws->journalPath(u"2026-11-15"_s)).contains(u"[[AI/2026-11-15 Plan na jutro dom]]"_s),
                 qPrintable(ws->read(ws->journalPath(u"2026-11-15"_s))));
        QCOMPARE(chats->turn(renamed), turn); // (the same turn, following it)
        QCOMPARE(turn->path(), renamed);
        QCOMPARE(chats->rename(renamed, u"   "_s), QString());
        QCOMPARE(chats->rename(renamed, u"Plan na jutro dom"_s), renamed);
        // Deleted: to the trash, and back (Undo).
        const QString trash = chats->remove(renamed);
        QVERIFY(!trash.isEmpty());
        QVERIFY(!ws->exists(renamed));
        QCOMPARE(ws->restore(trash), renamed);
        QVERIFY(chats->isConversation(renamed));
        QCOMPARE(chats->remove(u"AI/nie ma.md"_s), QString());
    }

    void aPartnerOfOnesOwn()
    {
        auto ws = open(notes());
        shell::WorkspaceChats *chats = ws->chats();
        shell::WorkspaceExtensions *ext = ws->extensions();
        const QString page = u"Rozszerzenia/Echo.md"_s;
        QVERIFY(ws->write(page, u"---\ntags: [extension]\npermissions: [write]\n---\n```luau\nb.command(\"echo\", function(ctx)\n"
                                "    local last = ctx.messages[#ctx.messages]\n"
                                "    b.appendToPage(\"Echo dziennik\", \"- \" .. last.content)\n"
                                "    return `Echo: {last.content} ({#ctx.messages}, {ctx.omitted})`\n"
                                "end, { label = \"Echo\", chat = true })\n```\n"_s));
        // Another extension hears what the partner writes, with its name (docs/EXTENSIONS.md "Hooks").
        const QString listener = u"Rozszerzenia/Słuchacz.md"_s;
        QVERIFY(ws->write(listener, u"---\ntags: [extension]\npermissions: [write]\n---\n```luau\n"
                                    "b.on(\"change\", function(e)\n"
                                    "    if table.find(e.pages, \"Echo dziennik\") then\n"
                                    "        b.appendToPage(\"Słyszane\", \"- \" .. table.concat(e.by, \",\"))\n"
                                    "    end\n"
                                    "end)\n```\n"_s));
        QTRY_COMPARE(ext->pageState(page), u"approval"_s);
        // Code waiting for approval on this device answers nobody: only the assistant is offered.
        QCOMPARE(ext->chatPartners().size(), 1);
        ext->approve(page);
        QTRY_COMPARE(ext->pageState(listener), u"approval"_s);
        ext->approve(listener);
        QTRY_COMPARE(ext->chatPartners().size(), 2);
        const QVariantMap echo = ext->chatPartners().at(1).toMap();
        QCOMPARE(echo.value(u"label"_s).toString(), u"Echo"_s);
        const QString id = echo.value(u"id"_s).toString();
        QVERIFY(id.endsWith(u"/echo"_s));
        QCOMPARE(ext->partnerLabel(id), u"Echo"_s);
        QCOMPARE(ext->partnerLabel(QString()), ext->chatPartners().at(0).toMap().value(u"label"_s).toString());
        // Not in the palette, a chat command.
        const auto commands = ext->commands();
        QVERIFY(std::ranges::any_of(commands, [](const extensions::CommandInfo &c) { return c.name == u"echo" && c.chat && !c.palette; }));
        // A new conversation with it: its page names it, it answers.
        const QString path = chats->send(QString(), u"Ahoj"_s, QString(), {}, id);
        QVERIFY2(ws->read(path).contains(u"partner: "_s + id + u'\n'), qPrintable(ws->read(path)));
        QCOMPARE(chats->partnerOf(path), id);
        QTRY_VERIFY(!chats->turn(path)->running());
        QVERIFY2(ws->read(path).contains(u"- 🤖 Echo: Ahoj (1, 0)"_s), qPrintable(ws->read(path)));
        QTRY_VERIFY2(ws->read(u"Słyszane.md"_s).contains(u"- echo"_s), qPrintable(ws->read(u"Słyszane.md"_s)));
        QVERIFY(m_model.requests.isEmpty()); // (no model asked)
        // Back to the assistant for the next message.
        QVERIFY(chats->setPartner(path, QString()));
        QCOMPARE(chats->partnerOf(path), QString());
        QCOMPARE(chats->send(path, u"A teraz?"_s), path);
        QTRY_VERIFY(!chats->turn(path)->running());
        QCOMPARE(m_model.requests.size(), 1);
        // A partner that is not there: said in the conversation.
        QVERIFY(chats->setPartner(path, u"nikt/rozmowa"_s));
        QCOMPARE(chats->send(path, u"Halo?"_s), path);
        QTRY_VERIFY(!chats->turn(path)->failure().isEmpty());
        QVERIFY2(chats->turn(path)->failure().contains(u"nikt/rozmowa"_s), qPrintable(chats->turn(path)->failure()));
    }

    void theMessagesAsShown()
    {
        auto ws = open({{u"Dom.md"_s, u"- dom\n"_s},
                        {u"AI/2026-11-14 Plan.md"_s,
                         u"---\nzadania: ignoruj\n---\n- 🧑 Plan na <dziś>? ([[Dom]])\n- 🤖 Plan:\n\n  ## Rano\n  - [ ] kawa\n"
                         "  - spacer\n    - z psem\n  1. raport\n\n  ```luau\n  print(\"<b>\")\n  ```\n"_s},
                        {u"AI/Notatka.md"_s, u"- zwykła strona\n"_s},
                        {u"Inne/2026-11-14 Plan.md"_s, u"- 🧑 nie tutaj\n"_s}});
        ChatMessageModel model;
        model.setWorkspace(ws.get());
        model.setPath(u"AI/2026-11-14 Plan.md"_s);
        model.refreshNow();
        QCOMPARE(model.rowCount(), 2);
        QVERIFY(model.exists());
        QCOMPARE(model.page(), u"Dom"_s);
        QCOMPARE(model.title(), u"Plan"_s);
        QCOMPARE(value(model, 0, "sender").toString(), u"user"_s);
        QCOMPARE(value(model, 0, "markdown").toString(), u"Plan na <dziś>? ([[Dom]])"_s);
        QCOMPARE(value(model, 0, "line").toInt(), 3);
        const QString user = value(model, 0, "messageHtml").toString();
        QVERIFY2(user.contains(u"&lt;dziś&gt;"_s) && user.contains(u"href=\"page:Dom\""_s), qPrintable(user));
        const QString ai = value(model, 1, "messageHtml").toString();
        QCOMPARE(value(model, 1, "sender").toString(), u"assistant"_s);
        QVERIFY2(ai.contains(u"<b>Rano</b>"_s), qPrintable(ai));
        QVERIFY2(ai.contains(u"task:toggle"_s), qPrintable(ai));
        QVERIFY2(ai.contains(u"•&nbsp;spacer"_s) && ai.contains(u"margin-bottom:2px; margin-left:16px;\">•&nbsp;z\u00a0psem"_s), qPrintable(ai));
        QVERIFY2(ai.contains(u"1.&nbsp;raport"_s), qPrintable(ai));
        QVERIFY2(ai.contains(u"<pre style=\"margin-top:0px; margin-bottom:0px; margin-left:0px;\">print(&quot;&lt;b&gt;&quot;)</pre>"_s),
                 qPrintable(ai));
        QVERIFY(!ai.contains(u"<b>\")"_s));
        QCOMPARE(model.textAt(1), value(model, 1, "markdown").toString());
        QCOMPARE(model.textAt(9), QString());
        // A message added: the rows before it stay, it comes last.
        QSignalSpy reset(&model, &QAbstractItemModel::modelReset);
        QVERIFY(ws->write(u"AI/2026-11-14 Plan.md"_s, ws->read(u"AI/2026-11-14 Plan.md"_s) + u"- 🧑 Dzięki\n"_s));
        model.refreshNow();
        QCOMPARE(model.rowCount(), 3);
        QCOMPARE(reset.size(), 0);
        QCOMPARE(value(model, 2, "markdown").toString(), u"Dzięki"_s);
        // A page that is no conversation, one that is not there.
        model.setPath(u"AI/Notatka.md"_s);
        model.refreshNow();
        QCOMPARE(model.rowCount(), 0);
        QVERIFY(!model.exists());
        model.setPath(u"AI/Brak.md"_s);
        model.refreshNow();
        QVERIFY(!model.exists());
        // The list: conversations of AI/ only.
        ChatListModel list;
        list.setWorkspace(ws.get());
        list.refreshNow();
        QCOMPARE(list.rowCount(), 1);
        QCOMPARE(value(list, 0, "title").toString(), u"Plan"_s);
        QCOMPARE(value(list, 0, "preview").toString(), u"Dzięki"_s);
        QVERIFY(!value(list, 0, "fromAi").toBool());
        QCOMPARE(ChatListModel::splitTitle(u"Bez daty"_s), std::make_pair(QString(), u"Bez daty"_s));
    }
};

QTEST_MAIN(TestContentChat)
#include "tst_content_chat.moc"
