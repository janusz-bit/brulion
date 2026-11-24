// The AI chat as the app runs it, on a real Workspace and its notebook, the model on the device
// played by the test (ai::LocalBackend): a new conversation (its page in AI/ named after the
// question, the page it is about, the journal's line that links to it), the assistant's turn (the
// conversation's messages and the page's text in the request, what it says it does, the answer
// written into the page), the next message with the history, a change asked a yes for in the
// conversation (questionChat) and written, Stop (nothing written for that turn), a failure and Try
// again, the extensions loaded again during a turn, the assistant switched off; the messages as the view shows them
// (ChatMessageModel: roles, rendered text — links, lists, numbers, tasks, headings, code —, the page it is about) and
// the list of conversations (ChatListModel: only conversations of AI/, newest first, their names without the date,
// previews); the time of each message written and shown, a day's line above it, pictures sent with a message
// (attachments of the notebook, the model's images, a model that does not see them; the browser picker's files),
// several conversations answering at once (an OpenAI-compatible server played by FakeAiServer) and the model on the
// device taking them one at a time in line, a conversation too long for the model sent from a later turn (the model
// told, the view's line), a partner of the user's own (an extension's chat command), renaming and deleting a
// conversation, Claude's thinking the turn shows, and answers the user reported or hid (Report response: the marks in
// the page's frontmatter, folded rows, the list's preview, an older page, marks made in another app; the report's draft
// and its link).
#include "chatmodels.h"

#include "aireportdraft.h"
#include "pickedfiles.h"

#include "workspace.h"
#include "workspacechats.h"
#include "workspaceextensions.h"

#include "../fakeaiserver.h"

#include "ai/local.h"
#include "aireport/aireport.h"
#include "chat/chat.h"
#include "vault/localstore.h"
#include "vault/vault.h"

#include <QBuffer>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QImage>
#include <QJsonObject>
#include <QLocale>
#include <QScopeGuard>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>
#include <QTimer>
#include <QUrlQuery>

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

    std::unique_ptr<Workspace> open(const QHash<QString, QString> &files, bool assistant = true,
                                    const QHash<QString, QDateTime> &times = {})
    {
        const QString root = m_tmp.filePath(u"vault-%1"_s.arg(++m_n));
        for (auto it = files.cbegin(); it != files.cend(); ++it)
            writeRaw(root + u'/' + it.key(), it.value().toUtf8());
        // (When each page last changed: the list's order.)
        for (auto it = times.cbegin(); it != times.cend(); ++it) {
            QFile f(root + u'/' + it.key());
            if (!f.open(QIODevice::ReadWrite) || !f.setFileTime(it.value(), QFileDevice::FileModificationTime))
                qWarning() << "no time set for" << it.key();
        }
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
        const QMetaObject::Connection watch = connect(turn, &shell::ChatTurn::stateChanged, this, [&] {
            if (!turn->status().isEmpty() && (statuses.isEmpty() || statuses.last() != turn->status()))
                statuses << turn->status();
        });
        // Gone before `statuses` and `turn` are: the workspace's close at the end still signals.
        const auto unwatch = qScopeGuard([&] { disconnect(watch); });
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
        QCOMPARE(ws->read(path),
                 u"---\nzadania: ignoruj\n---\n- 🧑 Co mam jutro do zrobienia? ([[Dom]])\n  czas:: 2026-11-15 10:30\n"
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
        QVERIFY2(ws->read(path).endsWith(
                         u"- 🧑 A pojutrze?\n  czas:: 2026-11-15 10:31\n- 🤖 Dobrze.\n  czas:: 2026-11-15 10:31\n"_s),
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
        QVERIFY2(ws->read(path).endsWith(
                         u"- 🤖 Dopisałem mleko.\n  czas:: 2026-11-15 10:30\n\n  ✏️ Zmienione: [[Zakupy]]\n"_s),
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
        QCOMPARE(turn->partialSettled(), QString());                             // (one block, being written)
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
        QVERIFY2(sent.first().content.contains(
                         u"(Początek tej rozmowy — 6 wcześniejszych wiadomości — nie zmieścił się"_s),
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

    // The browser's picker (pickPicturesInBrowser: the WebAssembly build) hands over files read
    // into memory: stored as a file's pictures are; one that is no picture, too large or over the
    // pick's total refused (picturesRefused), the others kept in the order picked.
    void picturesFromTheBrowsersPicker()
    {
        auto ws = open(notes());
        shell::WorkspaceChats *chats = ws->chats();
        QVERIFY(!chats->browserPicker()); // (this build: the FilePicker)
        QCOMPARE(chats->pickPicturesInBrowser(), 0);
        QImage image(40, 30, QImage::Format_RGB32);
        image.fill(QColor(200, 120, 40));
        QByteArray png;
        QBuffer buffer(&png);
        QVERIFY(buffer.open(QIODevice::WriteOnly) && image.save(&buffer, "PNG"));
        platform::PickedFile first{u"Paragon żółty.png"_s, png, png.size()};
        platform::PickedFile text{u"notatka.txt"_s, QByteArray("x"), 1};
        platform::PickedFile big{u"skan.jpg"_s, QByteArray(), 300LL * 1024 * 1024};
        big.tooLarge = true;
        platform::PickedFile second{u"zdjęcie.PNG"_s, png, png.size()};
        platform::PickedFile over{u"jeszcze.png"_s, QByteArray(), png.size()};
        over.overTotal = true;
        QSignalSpy refused(chats, &shell::WorkspaceChats::picturesRefused);
        const QStringList stored = chats->addPickedPictures({first, text, big, second, over});
        QCOMPARE(stored.size(), 2);
        QCOMPARE(refused.size(), 1);
        QVERIFY(stored.at(0).contains(u"Paragon"_s));
        QVERIFY(stored.at(1).contains(u"zdj"_s));
        for (const QString &path : stored)
            QVERIFY2(ws->exists(path), qPrintable(path));
        // Taken off before sending: deleted again, as a file's.
        chats->dropPicture(stored.at(1));
        QVERIFY(!ws->exists(stored.at(1)));
        // Sent: embedded in the message.
        const QString path = chats->send(QString(), u"Ile kosztował?"_s, QString(), {stored.at(0)});
        QVERIFY2(ws->read(path).contains(u"![["_s + stored.at(0) + u"]]"_s), qPrintable(ws->read(path)));
        QTRY_VERIFY(!chats->turn(path)->running());
        // All of them fine: nothing refused.
        refused.clear();
        QCOMPARE(chats->addPickedPictures({second}).size(), 1);
        QCOMPARE(refused.size(), 0);
        QVERIFY(chats->addPickedPictures({}).isEmpty());
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
        QVERIFY(!value(model, 0, "day").toString().isEmpty());  // (the first day)
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
        QVERIFY(ws->write(
                page, u"---\ntags: [extension]\npermissions: [write]\n---\n```luau\nb.command(\"echo\", function(ctx)\n"
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
        QVERIFY(std::ranges::any_of(
                commands, [](const extensions::CommandInfo &c) { return c.name == u"echo" && c.chat && !c.palette; }));
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

    void reportedAndHiddenAnswers()
    {
        auto ws = open(notes());
        shell::WorkspaceChats *chats = ws->chats();
        m_model.replies = {answer(u"Pierwsza **odpowiedź** <b>"_s), answer(u"Druga — żółć"_s), answer(u"Trzecia"_s)};
        const QString path = chats->send(QString(), u"Co je żółw?"_s, u"Dom"_s);
        QTRY_VERIFY(!chats->turn(path)->running());
        QCOMPARE(chats->send(path, u"A w zimie?"_s), path);
        QTRY_VERIFY(!chats->turn(path)->running());
        const QString before = ws->read(path);
        const QString head = u"---\nzadania: ignoruj\n---\n"_s;
        QVERIFY(before.startsWith(head));

        // Only an AI's message of a conversation that is there.
        QVERIFY(!chats->setFlag(path, 1, u"reported"_s)); // (the user's)
        QVERIFY(!chats->setFlag(path, 0, u"hidden"_s));
        QVERIFY(!chats->setFlag(path, 5, u"hidden"_s));
        QVERIFY(!chats->setFlag(path, 2, u"obraźliwa"_s));
        QVERIFY(!chats->setFlag(u"AI/2026-11-15 Brak.md"_s, 2, u"hidden"_s));
        QVERIFY(!chats->setFlag(QString(), 2, u"hidden"_s));
        QCOMPARE(ws->read(path), before);

        // Marked: the frontmatter lists them, every message as it was.
        QVERIFY(chats->setFlag(path, 2, u"reported"_s));
        QVERIFY(chats->setFlag(path, 4, u"hidden"_s));
        QVERIFY(chats->setFlag(path, 4, u"hidden"_s)); // (again: nothing more)
        const QString marked = ws->read(path);
        QCOMPARE(marked, u"---\nzadania: ignoruj\nzgłoszone: [2]\nukryte: [4]\n---\n"_s + before.mid(head.size()));

        // The view: folded rows, opened again in the view only.
        ChatMessageModel model;
        model.setWorkspace(ws.get());
        model.setPath(path);
        model.refreshNow();
        QCOMPARE(model.rowCount(), 4);
        QCOMPARE(value(model, 0, "flag").toString(), QString());
        QCOMPARE(value(model, 1, "flag").toString(), u"reported"_s);
        QCOMPARE(value(model, 2, "flag").toString(), QString());
        QCOMPARE(value(model, 3, "flag").toString(), u"hidden"_s);
        QCOMPARE(value(model, 1, "markdown").toString(), u"Pierwsza **odpowiedź** <b>"_s);
        QCOMPARE(value(model, 1, "line").toInt(), 7); // (the frontmatter's two lines more)
        QVERIFY(!value(model, 1, "revealed").toBool());
        QSignalSpy changed(&model, &QAbstractItemModel::dataChanged);
        model.reveal(1, true);
        QCOMPARE(changed.size(), 1);
        QVERIFY(value(model, 1, "revealed").toBool());
        model.reveal(1, true);
        QCOMPARE(changed.size(), 1);
        model.reveal(0, true); // (not marked: nothing to open)
        QVERIFY(!value(model, 0, "revealed").toBool());
        model.reveal(9, true);
        // The question each answer answers (without the page's text, which stays out).
        QCOMPARE(model.questionBefore(1), u"Co je żółw? ([[Dom]])"_s);
        QCOMPARE(model.questionBefore(3), u"A w zimie?"_s);
        QCOMPARE(model.questionBefore(0), QString());
        QCOMPARE(model.questionBefore(7), QString());

        // The list does not show a hidden answer's words either.
        ChatListModel list;
        list.setWorkspace(ws.get());
        list.refreshNow();
        QCOMPARE(list.rowCount(), 1);
        QCOMPARE(value(list, 0, "preview").toString(), u"Hidden response"_s);
        QVERIFY(value(list, 0, "fromAi").toBool());

        // The next turn: the marks stay with their messages, the model still gets both answers.
        QCOMPARE(chats->send(path, u"Dzięki!"_s), path);
        QTRY_VERIFY(!chats->turn(path)->running());
        const QList<ai::Message> history = m_model.requests.last().messages;
        QVERIFY(std::ranges::any_of(history, [](const ai::Message &m) { return m.content.contains(u"Druga — żółć"_s); }));
        model.refreshNow();
        QCOMPARE(model.rowCount(), 6);
        QCOMPARE(value(model, 1, "flag").toString(), u"reported"_s);
        QVERIFY(value(model, 1, "revealed").toBool()); // (still open in this view)
        QCOMPARE(value(model, 3, "flag").toString(), u"hidden"_s);
        QCOMPARE(value(model, 5, "flag").toString(), QString());
        list.refreshNow();
        QCOMPARE(value(list, 0, "preview").toString(), u"Trzecia"_s);

        // Kept with the page: read again from the file, as after a restart or a sync.
        const chat::Conversation again = chat::parse(ws->read(path));
        QCOMPARE(again.messages.at(1).flag, chat::Flag::Reported);
        QCOMPARE(again.messages.at(3).flag, chat::Flag::Hidden);

        // Hidden → reported; marks taken away: the page as it would be without them.
        QVERIFY(chats->setFlag(path, 4, u"reported"_s));
        QVERIFY2(ws->read(path).startsWith(u"---\nzadania: ignoruj\nzgłoszone: [2, 4]\n---\n"_s), qPrintable(ws->read(path)));
        QVERIFY(chats->setFlag(path, 2, QString()));
        QVERIFY(chats->setFlag(path, 4, QString()));
        QVERIFY(ws->read(path).startsWith(head + u"- 🧑 Co je żółw?"_s));
        model.refreshNow();
        QCOMPARE(value(model, 1, "flag").toString(), QString());
        QVERIFY(!value(model, 1, "revealed").toBool());

        // Who answered, as a report says it (the settings now).
        QVERIFY(!ai::localModels().isEmpty());
        QCOMPARE(chats->modelName(path), u"Brulion's AI assistant · On this device · "_s + ai::localModels().first().name);
        QVERIFY(chats->setPartner(path, u"pirat/rozmowa"_s));
        QVERIFY(chats->modelName(path).startsWith(u"Chat partner pirat/rozmowa (an extension) · On this device · "_s));
    }

    void anOlderPageAndMarksByHand()
    {
        // A page written before the marks (no frontmatter, Logseq's tabs) and one marked in another
        // app (an English notebook, a block list).
        auto ws = open({{u"AI/2026-11-01 Stara.md"_s, u"- 🧑 Pytanie\n- 🤖 Odpowiedź\n\t- punkt\n"_s},
                        {u"AI/2026-11-02 Ręczna.md"_s,
                         u"---\ntasks: ignore\nhidden:\n  - 2\n---\n- 🧑 Question\n- 🤖 Answer\n"_s}});
        ChatMessageModel model;
        model.setWorkspace(ws.get());
        model.setPath(u"AI/2026-11-01 Stara.md"_s);
        model.refreshNow();
        QCOMPARE(model.rowCount(), 2);
        QCOMPARE(value(model, 1, "flag").toString(), QString());
        QVERIFY(ws->chats()->setFlag(u"AI/2026-11-01 Stara.md"_s, 2, u"hidden"_s));
        // (No frontmatter, no language of its own: the notebook's.)
        QCOMPARE(ws->read(u"AI/2026-11-01 Stara.md"_s), u"---\nukryte: [2]\n---\n- 🧑 Pytanie\n- 🤖 Odpowiedź\n\t- punkt\n"_s);
        model.refreshNow();
        QCOMPARE(value(model, 1, "flag").toString(), u"hidden"_s);
        QCOMPARE(value(model, 1, "markdown").toString(), u"Odpowiedź\n- punkt"_s);

        model.setPath(u"AI/2026-11-02 Ręczna.md"_s);
        model.refreshNow();
        QCOMPARE(value(model, 1, "flag").toString(), u"hidden"_s);
        QVERIFY(ws->chats()->setFlag(u"AI/2026-11-02 Ręczna.md"_s, 2, u"reported"_s));
        QCOMPARE(ws->read(u"AI/2026-11-02 Ręczna.md"_s),
                 u"---\ntasks: ignore\nreported: [2]\n---\n- 🧑 Question\n- 🤖 Answer\n"_s);
    }

    void aReportDraft()
    {
        shell::AiReportDraft draft;
        QSignalSpy changed(&draft, &shell::AiReportDraft::changed);
        QCOMPARE(draft.reasons(), (QStringList{u"offensive"_s, u"sexual"_s, u"violent"_s, u"harassment"_s,
                                               u"misleading"_s, u"other"_s}));
        QCOMPARE(draft.reasonLabel(u"misleading"_s), u"False or misleading"_s);
        QCOMPARE(draft.reasonLabel(u"nieznany"_s), QString());
        QVERIFY(!draft.ready());
        draft.setReason(u"nieznany"_s);
        QVERIFY(!draft.ready());
        // This build names no address: a public issue on GitHub.
        QCOMPARE(shell::AiReportDraft::buildAddress(), QString());
        QVERIFY(!draft.byEmail());
        QCOMPARE(draft.destination(), u"github.com/janusz-bit/brulion"_s);
        QVERIFY(!draft.version().isEmpty());
        QVERIFY(!draft.device().isEmpty());

        draft.setReason(u"Offensive"_s);
        QVERIFY(draft.ready());
        QCOMPARE(draft.reason(), u"offensive"_s);
        draft.setWhere(u"chat"_s);
        draft.setModel(u"Brulion's AI assistant · Anthropic · claude-opus-5-5"_s);
        draft.setResponse(u"Zażółć gęślą jaźń <script>"_s);
        draft.setComment(u"Wulgarne"_s);
        draft.setQuestion(u"Moje prywatne pytanie"_s);
        QVERIFY(changed.size() >= 6);
        QCOMPARE(draft.title(), u"AI response report: Offensive or hateful"_s);
        QVERIFY(draft.body().contains(u"- Where: AI chat\n"_s));
        QVERIFY(draft.body().contains(u"```\nZażółć gęślą jaźń <script>\n```"_s));
        QVERIFY(draft.body().contains(u"```\nWulgarne\n```"_s));
        QVERIFY(draft.body().contains(u"- App: Brulion "_s + draft.version() + u'\n'));
        QVERIFY(draft.body().contains(u"- Device: "_s + draft.device() + u'\n'));
        // The question only when added.
        QVERIFY(!draft.body().contains(u"prywatne"_s));
        draft.setIncludeQuestion(true);
        QVERIFY(draft.body().contains(u"```\nMoje prywatne pytanie\n```"_s));
        draft.setIncludeQuestion(false);
        QVERIFY(!draft.body().contains(u"prywatne"_s));
        // The link: GitHub's new issue with the same text.
        QCOMPARE(draft.url().host(), u"github.com"_s);
        QCOMPARE(draft.url().path(), u"/janusz-bit/brulion/issues/new"_s);
        QCOMPARE(QUrlQuery(draft.url()).queryItemValue(u"body"_s, QUrl::FullyDecoded), draft.body());
        QVERIFY(!draft.cut());
        draft.setResponse(QString(30000, u'ż'));
        QVERIFY(draft.cut());
        QVERIFY(draft.url().toEncoded().size() <= aireport::MaxLinkLength);
    }

    void theMessagesAsShown()
    {
        auto ws = open({{u"Dom.md"_s, u"- dom\n"_s},
                        {u"AI/2026-11-14 Plan.md"_s,
                         u"---\nzadania: ignoruj\n---\n- 🧑 Plan na <dziś>? ([[Dom]])\n- 🤖 Plan:\n\n  ## Rano\n  - [ ] kawa\n"
                         "  - spacer\n    - z psem\n  1. raport\n\n  ```luau\n  print(\"<b>\")\n  ```\n"
                         "- 🤖 Pogoda:\n\n  | Dzień | Min / Max | Opady |\n  |---|:-:|--:|\n  | **Dziś** | 1,1 / 3,1°C | 0,7 mm |\n"
                         "  | Jutro <i> | 0,6 \\| 2,4°C |\n\n  Źródło: Open-Meteo.\n"_s},
                        {u"AI/Notatka.md"_s, u"- zwykła strona\n"_s},
                        {u"Inne/2026-11-14 Plan.md"_s, u"- 🧑 nie tutaj\n"_s}});
        ChatMessageModel model;
        model.setWorkspace(ws.get());
        model.setPath(u"AI/2026-11-14 Plan.md"_s);
        model.refreshNow();
        QCOMPARE(model.rowCount(), 3);
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
        // A table: a rich-text table, not its Markdown — the header bold, the columns aligned,
        // the cells rendered and escaped, a short row filled, an escaped pipe kept in its cell.
        const QString weather = value(model, 2, "messageHtml").toString();
        QVERIFY2(weather.contains(u"<table border=\"1\""_s) && !weather.contains(u"|---"_s), qPrintable(weather));
        QVERIFY2(weather.contains(u"<th align=\"left\"><b>Dzień</b></th><th align=\"center\"><b>Min / Max</b></th>"
                                  "<th align=\"right\"><b>Opady</b></th>"_s),
                 qPrintable(weather));
        QVERIFY2(weather.contains(u"<td align=\"left\"><b>Dziś</b></td>"_s), qPrintable(weather));
        QVERIFY2(weather.contains(u"Jutro &lt;i&gt;"_s) && !weather.contains(u"<i>"_s), qPrintable(weather));
        QVERIFY2(weather.contains(u"0,6 | 2,4°C</td><td align=\"right\"></td></tr></table>"_s), qPrintable(weather));
        QVERIFY2(weather.contains(u"Źródło: Open-Meteo."_s), qPrintable(weather));
        QCOMPARE(model.textAt(1), value(model, 1, "markdown").toString());
        QCOMPARE(model.textAt(9), QString());
        // A message added: the rows before it stay, it comes last.
        QSignalSpy reset(&model, &QAbstractItemModel::modelReset);
        QVERIFY(ws->write(u"AI/2026-11-14 Plan.md"_s, ws->read(u"AI/2026-11-14 Plan.md"_s) + u"- 🧑 Dzięki\n"_s));
        model.refreshNow();
        QCOMPARE(model.rowCount(), 4);
        QCOMPARE(reset.size(), 0);
        QCOMPARE(value(model, 3, "markdown").toString(), u"Dzięki"_s);
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

    /// The list's titles in order, a section's name before its first one ("[pinned] Żółw").
    static QStringList listed(const ChatListModel &list)
    {
        QStringList out;
        QString section;
        for (int i = 0; i < list.rowCount(); ++i) {
            const QString s = value(list, i, "section").toString();
            out << (s != section ? u"[%1] "_s.arg(s) : QString()) + value(list, i, "title").toString();
            section = s;
        }
        return out;
    }

    static QHash<QString, QString> conversations()
    {
        return {
                {u"AI/2026-11-10 Żółw.md"_s,
                 u"---\nzadania: ignoruj\nprzypięta: tak\n---\n- 🧑 Co je żółw?\n- 🤖 Sałatę.\n"_s},
                {u"AI/2026-11-11 Ćma.md"_s,
                 u"---\ntasks: ignore\narchived: true\n---\n- 🧑 Ćma?\n- 🤖 Nocny motyl.\n"_s},
                {u"AI/2026-11-12 Łąka.md"_s, u"---\nzadania: ignoruj\n---\n- 🧑 Łąka\r\n- 🤖 Kwitnie.\r\n"_s},
                {u"AI/2026-11-13 Gęś.md"_s,
                 u"---\nzadania: ignoruj\nprzypięta: tak\nzarchiwizowana: tak\n---\n- 🧑 Gęś\n- 🤖 Gęgała.\n"_s},
                {u"AI/2026-11-14 Źródło.md"_s, u"- 🧑 Źródło?\n- 🤖 W górach.\n"_s},
                {u"Dom.md"_s, u"- przypięta: tak\n"_s},
        };
    }
    static QHash<QString, QDateTime> conversationTimes()
    {
        QHash<QString, QDateTime> times;
        int day = 10;
        for (const QString &name : {u"Żółw"_s, u"Ćma"_s, u"Łąka"_s, u"Gęś"_s, u"Źródło"_s}) {
            times.insert(u"AI/2026-11-%1 %2.md"_s.arg(day).arg(name), QDateTime(QDate(2026, 11, day), QTime(12, 0)));
            ++day;
        }
        return times;
    }

    void pinnedAndArchived()
    {
        auto ws = open(conversations(), true, conversationTimes());
        shell::WorkspaceChats *chats = ws->chats();
        ChatListModel list;
        list.setWorkspace(ws.get());
        list.refreshNow();
        // The pinned on top, the others newest first; the archived folded (counted).
        QCOMPARE(listed(list), (QStringList{u"[pinned] Żółw"_s, u"[recent] Źródło"_s, u"Łąka"_s}));
        QCOMPARE(list.archivedCount(), 2);
        QCOMPARE(list.pinnedCount(), 1);
        QVERIFY(value(list, 0, "pinned").toBool());
        QVERIFY(!value(list, 0, "archived").toBool());
        // Unfolded: at the bottom (archived wins over pinned).
        QSignalSpy shown(&list, &ChatListModel::showArchivedChanged);
        list.setShowArchived(true);
        QCOMPARE(shown.size(), 1);
        QCOMPARE(listed(list),
                 (QStringList{u"[pinned] Żółw"_s, u"[recent] Źródło"_s, u"Łąka"_s, u"[archived] Gęś"_s, u"Ćma"_s}));
        QVERIFY(value(list, 3, "pinned").toBool() && value(list, 3, "archived").toBool());
        list.setShowArchived(false);

        // Pinned from the menu: its frontmatter, the rest byte for byte (CRLF lines too).
        const QString meadow = u"AI/2026-11-12 Łąka.md"_s;
        QVERIFY(chats->setPinned(meadow, true));
        QCOMPARE(ws->read(meadow), u"---\nzadania: ignoruj\nprzypięta: tak\n---\n- 🧑 Łąka\r\n- 🤖 Kwitnie.\r\n"_s);
        list.refreshNow();
        QCOMPARE(listed(list), (QStringList{u"[pinned] Łąka"_s, u"Żółw"_s, u"[recent] Źródło"_s}));
        QVERIFY(chats->setPinned(meadow, true)); // (said already: nothing written)
        QVERIFY(chats->setPinned(meadow, false));
        QCOMPARE(ws->read(meadow), u"---\nzadania: ignoruj\n---\n- 🧑 Łąka\r\n- 🤖 Kwitnie.\r\n"_s);
        // Archived: out of the list; a page without a frontmatter gets one in the notebook's language.
        const QString spring = u"AI/2026-11-14 Źródło.md"_s;
        QVERIFY(chats->setArchived(spring, true));
        QCOMPARE(ws->read(spring), u"---\nzarchiwizowana: tak\n---\n- 🧑 Źródło?\n- 🤖 W górach.\n"_s);
        list.refreshNow();
        QCOMPARE(list.rowOf(spring), -1);
        QCOMPARE(list.archivedCount(), 3);
        QVERIFY(chats->setArchived(spring, false));
        list.refreshNow();
        QVERIFY(list.rowOf(spring) >= 0);
        // An English page: its key.
        const QString moth = u"AI/2026-11-11 Ćma.md"_s;
        QVERIFY(chats->setPinned(moth, true));
        QCOMPARE(ws->read(moth),
                 u"---\ntasks: ignore\narchived: true\npinned: true\n---\n- 🧑 Ćma?\n- 🤖 Nocny motyl.\n"_s);
        QVERIFY(!chats->setPinned(u"AI/nie ma.md"_s, true));
        QVERIFY(!chats->setArchived(QString(), true));
    }

    void whatNeedsTheUserComesFirst()
    {
        auto ws = open(conversations(), true, conversationTimes());
        shell::WorkspaceChats *chats = ws->chats();
        ChatListModel list;
        list.setWorkspace(ws.get());
        list.refreshNow();
        QCOMPARE(listed(list), (QStringList{u"[pinned] Żółw"_s, u"[recent] Źródło"_s, u"Łąka"_s}));

        // An answer nobody saw: unread — in the notebook's device state, never in the page.
        const QString meadow = u"AI/2026-11-12 Łąka.md"_s;
        QSignalSpy attention(chats, &shell::WorkspaceChats::attentionChanged);
        m_model.replies = {answer(u"Pachnie.\n\nI brzęczy."_s)};
        QCOMPARE(chats->send(meadow, u"Czym pachnie łąka?"_s), meadow);
        QTRY_VERIFY(!chats->isRunning(meadow));
        QVERIFY(chats->isUnread(meadow));
        QVERIFY(!attention.isEmpty());
        QVERIFY(chats->cameBack(meadow) > 0);
        QVERIFY(ws->vault()->store()->get(u"chat/unread"_s).toObject().contains(meadow.toLower()));
        QVERIFY(!ws->read(meadow).contains(u"unread"_s));
        // Another conversation changes after it: the unread one stays on top.
        const QString spring = u"AI/2026-11-14 Źródło.md"_s;
        // (Later than the answer by the files' times — on a file system whose clock is coarse
        // two writes can share one, and the list then sorts them by name.)
        const auto mtimeOf = [&](const QString &path) {
            for (const FileStat &f : ws->vault()->markdownFiles()) {
                if (f.path == path)
                    return f.mtime;
            }
            return qint64(0);
        };
        QTRY_VERIFY(ws->read(meadow).contains(u"I brzęczy."_s));
        QTRY_VERIFY(QDateTime::currentMSecsSinceEpoch() > mtimeOf(meadow) + 50);
        QVERIFY(ws->write(spring, ws->read(spring) + u"- 🧑 A w dolinach?\n"_s));
        QTRY_VERIFY(mtimeOf(spring) > mtimeOf(meadow));
        list.refreshNow();
        QCOMPARE(listed(list), (QStringList{u"[pinned] Żółw"_s, u"[recent] Łąka"_s, u"Źródło"_s}));
        QVERIFY(value(list, 1, "unread").toBool());
        QVERIFY(!value(list, 2, "unread").toBool());
        // Read: a view shows it.
        chats->showing(meadow, true);
        QVERIFY(!chats->isUnread(meadow));
        QVERIFY(!ws->vault()->store()->contains(u"chat/unread"_s));
        QTRY_COMPARE(listed(list), (QStringList{u"[pinned] Żółw"_s, u"[recent] Źródło"_s, u"Łąka"_s}));
        // Shown while answered: never unread.
        m_model.replies = {answer(u"Miodem."_s)};
        QCOMPARE(chats->send(meadow, u"A czym jeszcze?"_s), meadow);
        QTRY_VERIFY(!chats->isRunning(meadow));
        QVERIFY(!chats->isUnread(meadow));
        chats->showing(meadow, false);

        // A failure: on top, its reason as the row's line.
        ai::Reply broken;
        broken.error = u"brak połączenia"_s;
        m_model.replies = {broken};
        QCOMPARE(chats->send(spring, u"Halo?"_s), spring);
        QTRY_VERIFY(!chats->failureOf(spring).isEmpty());
        QTRY_COMPARE(listed(list), (QStringList{u"[pinned] Żółw"_s, u"[recent] Źródło"_s, u"Łąka"_s}));
        // (The order was this before the failure too: wait for the row's line itself.)
        QTRY_VERIFY2(value(list, 1, "failure").toString().contains(u"brak połączenia"_s),
                     qPrintable(value(list, 1, "failure").toString()));
        // An answer unread meanwhile comes back later: above the failure.
        m_model.replies = {answer(u"Tak."_s)};
        QCOMPARE(chats->send(meadow, u"Na pewno?"_s), meadow);
        QTRY_VERIFY(chats->isUnread(meadow));
        QTRY_COMPARE(listed(list), (QStringList{u"[pinned] Żółw"_s, u"[recent] Łąka"_s, u"Źródło"_s}));
        chats->dismissFailure(spring);
        QTRY_COMPARE(value(list, list.rowOf(spring), "failure").toString(), QString());
        QVERIFY(chats->cameBack(spring) == 0);

        // A question of the turn: on top, what it asks as the line; the pinned stay above.
        m_model.replies = {calling(u"append_to_page"_s, {{u"name"_s, u"Dom"_s}, {u"markdown"_s, u"- żółty"_s}}),
                           answer(u"Dopisane."_s)};
        QCOMPARE(chats->send(spring, u"Dopisz żółty do domu"_s), spring);
        QTRY_VERIFY(chats->asking(spring));
        QTRY_COMPARE(listed(list), (QStringList{u"[pinned] Żółw"_s, u"[recent] Źródło"_s, u"Łąka"_s}));
        QVERIFY(value(list, 1, "asking").toBool());
        QVERIFY2(value(list, 1, "question").toString().contains(u"Dom"_s),
                 qPrintable(value(list, 1, "question").toString()));
        QVERIFY(!value(list, 1, "question").toString().contains(u'\n'));
        ws->extensions()->answerChoice(ws->extensions()->questionOptions().value(0));
        QTRY_VERIFY(!chats->isRunning(spring));
        QTRY_VERIFY(!value(list, list.rowOf(spring), "asking").toBool());

        // Renamed: the unread answer follows; deleted: it goes.
        const QString renamed = chats->rename(meadow, u"Łąka kwietna"_s);
        QVERIFY(!renamed.isEmpty());
        QVERIFY(chats->isUnread(renamed));
        QVERIFY(!chats->isUnread(meadow));
        QVERIFY(!chats->remove(renamed).isEmpty());
        QVERIFY(!chats->isUnread(renamed));
    }

    void searchingTheConversations_data()
    {
        QTest::addColumn<QString>("query");
        QTest::addColumn<QStringList>("titles");
        QTest::addColumn<QString>("matchText");
        QTest::addColumn<QString>("marked"); // the characters marked in it (or in the title)
        QTest::addColumn<int>("matchLine");

        QTest::newRow("nothing") << QString() << QStringList{u"Wakacje w górach"_s, u"Przepis na żurek"_s} << QString()
                                 << QString() << -1;
        QTest::newRow("a name, without diacritics")
                << u"zurek"_s << QStringList{u"Przepis na żurek"_s} << QString() << u"żurek"_s << -1;
        QTest::newRow("a message") << u"KIEŁBASY"_s << QStringList{u"Przepis na żurek"_s}
                                   << u"Potrzebujesz zakwasu i białej kiełbasy."_s << u"kiełbasy"_s << 4;
        QTest::newRow("archived ones too") << u"kiełbas"_s << QStringList{u"Przepis na żurek"_s, u"Stare"_s}
                                           << u"Potrzebujesz zakwasu i białej kiełbasy."_s << u"kiełbas"_s << 4;
        QTest::newRow("all the words, one message")
                << u"szlakow zakopane"_s << QStringList{u"Wakacje w górach"_s}
                << u"Zakopane albo Szczyrk — w Zakopanem jest więcej szlaków."_s << u"ZakopaneZakopaneszlaków"_s << 4;
        QTest::newRow("words of two messages") << u"zakwas pojechać"_s << QStringList() << QString() << QString() << -1;
        QTest::newRow("not the name: a message")
                << u"góry"_s << QStringList{u"Wakacje w górach"_s} << u"Gdzie pojechać w góry?"_s << u"góry"_s << 3;
        QTest::newRow("its user's") << u"ugotować"_s << QStringList{u"Przepis na żurek"_s} << u"Jak ugotować żurek?"_s
                                    << u"ugotować"_s << 3;
        QTest::newRow("no such words") << u"nic takiego"_s << QStringList() << QString() << QString() << -1;
    }
    void searchingTheConversations()
    {
        QFETCH(QString, query);
        QFETCH(QStringList, titles);
        QFETCH(QString, matchText);
        QFETCH(QString, marked);
        QFETCH(int, matchLine);
        const QHash<QString, QString> files{
                {u"AI/2026-11-10 Przepis na żurek.md"_s,
                 u"---\nzadania: ignoruj\n---\n"
                 "- 🧑 Jak ugotować żurek?\n"
                 "- 🤖 Potrzebujesz zakwasu i białej kiełbasy.\n\n  - zakwas\n  - kiełbasa\n"_s},
                {u"AI/2026-11-11 Wakacje w górach.md"_s,
                 u"---\nzadania: ignoruj\n---\n"
                 "- 🧑 Gdzie pojechać w góry?\n"
                 "- 🤖 Zakopane albo Szczyrk — w Zakopanem jest więcej szlaków.\n"_s},
                {u"AI/2026-11-12 Stare.md"_s,
                 u"---\nzadania: ignoruj\nzarchiwizowana: tak\n---\n- 🧑 Co z kiełbasą?\n- 🤖 Zjedz ją.\n"_s},
        };
        const QHash<QString, QDateTime> times{
                {u"AI/2026-11-10 Przepis na żurek.md"_s, QDateTime(QDate(2026, 11, 10), QTime(9, 0))},
                {u"AI/2026-11-11 Wakacje w górach.md"_s, QDateTime(QDate(2026, 11, 11), QTime(9, 0))},
                {u"AI/2026-11-12 Stare.md"_s, QDateTime(QDate(2026, 11, 12), QTime(9, 0))},
        };
        auto ws = open(files, true, times);
        ChatListModel list;
        list.setWorkspace(ws.get());
        list.refreshNow();
        QSignalSpy queried(&list, &ChatListModel::queryChanged);
        list.setQuery(query);
        QCOMPARE(queried.size(), query.isEmpty() ? 0 : 1);
        QStringList got;
        for (int i = 0; i < list.rowCount(); ++i)
            got << value(list, i, "title").toString();
        QCOMPARE(got, titles);
        if (titles.isEmpty())
            return;
        QCOMPARE(value(list, 0, "matchText").toString(), matchText);
        QCOMPARE(value(list, 0, "matchLine").toInt(), matchLine);
        QCOMPARE(list.matchLineAt(0), matchLine);
        // The marked characters: of the match's line, else of the title.
        const QString text = matchText.isEmpty() ? value(list, 0, "title").toString() : matchText;
        const QVariantList positions = value(list, 0, matchText.isEmpty() ? "titleMatches" : "matchMatches").toList();
        QString marks;
        for (const QVariant &p : positions)
            marks += text.at(p.toInt());
        QCOMPARE(marks, marked);
    }

    void aMatchFarIntoALine()
    {
        const QStringList terms{u"zolw"_s};
        const QString line = u"**Na początku** była długa opowieść o lesie, rzece i górach, a potem [[Zwierzęta|o "
                             "zwierzętach]]: na końcu przyszedł żółw."_s;
        const QString excerpt = ChatListModel::matchExcerpt(line, terms);
        QVERIFY2(excerpt.startsWith(u"…"_s) && excerpt.endsWith(u"przyszedł żółw."_s), qPrintable(excerpt));
        QVERIFY2(!excerpt.contains(u"**"_s) && !excerpt.contains(u"[["_s), qPrintable(excerpt));
        QVERIFY(excerpt.size() < 60);
        // Near the start: all of it.
        QCOMPARE(ChatListModel::matchExcerpt(u"- Żółw je sałatę"_s, terms), u"Żółw je sałatę"_s);
    }

    void notifications()
    {
        auto ws = open(notes());
        shell::WorkspaceChats *chats = ws->chats();
        bool active = true;
        chats->setAppActive([&active] { return active; });
        QSignalSpy wanted(chats, &shell::WorkspaceChats::notificationWanted);
        QCOMPARE(chats->notifyMode(), u"background"_s);

        // In front: no notification (the list says it); behind another app: one.
        m_model.replies = {answer(u"Jutro **czynsz**.\n\nI raport."_s)};
        const QString path = chats->send(QString(), u"Co jutro?"_s);
        QTRY_VERIFY(!chats->isRunning(path));
        QVERIFY(wanted.isEmpty());
        active = false;
        m_model.replies = {answer(u"\n- Pojutrze nic.\n- Naprawdę."_s)};
        QCOMPARE(chats->send(path, u"A pojutrze?"_s), path);
        QTRY_COMPARE(wanted.size(), 1);
        QCOMPARE(wanted.at(0).at(0).toString(), path);
        QCOMPARE(wanted.at(0).at(1).toString(), u"Co jutro"_s);
        QCOMPARE(wanted.at(0).at(2).toString(), u"Pojutrze nic."_s);
        // Shown, but the window not active: the user is not looking.
        chats->showing(path, true);
        m_model.replies = {answer(u"Dobrze."_s)};
        QCOMPARE(chats->send(path, u"Dzięki"_s), path);
        QTRY_COMPARE(wanted.size(), 2);

        // Always: also in front, unless a chat view shows it.
        chats->setNotifyMode(u"always"_s);
        active = true;
        m_model.replies = {answer(u"Widzisz."_s)};
        QCOMPARE(chats->send(path, u"Widzisz?"_s), path);
        QTRY_VERIFY(!chats->isRunning(path));
        QCOMPARE(wanted.size(), 2);
        chats->showing(path, false);
        m_model.replies = {answer(u"Nie widzisz."_s)};
        QCOMPARE(chats->send(path, u"A teraz?"_s), path);
        QTRY_COMPARE(wanted.size(), 3);
        QCOMPARE(wanted.at(2).at(2).toString(), u"Nie widzisz."_s);

        // A failure says why; a question what it asks.
        ai::Reply broken;
        broken.error = u"model się nie wczytał"_s;
        m_model.replies = {broken};
        QCOMPARE(chats->send(path, u"Jeszcze?"_s), path);
        QTRY_COMPARE(wanted.size(), 4);
        QVERIFY2(wanted.at(3).at(2).toString().contains(u"model się nie wczytał"_s),
                 qPrintable(wanted.at(3).at(2).toString()));
        m_model.replies = {calling(u"append_to_page"_s, {{u"name"_s, u"Zakupy"_s}, {u"markdown"_s, u"- mleko"_s}}),
                           answer(u"Dopisałem."_s)};
        QVERIFY(chats->retry(path));
        QTRY_COMPARE(wanted.size(), 5);
        QVERIFY2(wanted.at(4).at(2).toString().contains(u"Zakupy"_s), qPrintable(wanted.at(4).at(2).toString()));
        QVERIFY(!wanted.at(4).at(2).toString().contains(u'\n'));
        ws->extensions()->answerChoice(ws->extensions()->questionOptions().value(0));
        QTRY_COMPARE(wanted.size(), 6); // (and its answer)
        QCOMPARE(wanted.at(5).at(2).toString(), u"Dopisałem."_s);

        // Never: none.
        chats->setNotifyMode(u"never"_s);
        active = false;
        m_model.replies = {answer(u"Cicho."_s)};
        QCOMPARE(chats->send(path, u"Cisza?"_s), path);
        QTRY_VERIFY(!chats->isRunning(path));
        QCOMPARE(wanted.size(), 6);
        chats->setNotifyMode(u"cokolwiek"_s);
        QCOMPARE(chats->notifyMode(), u"background"_s);
        // A stopped turn: nothing to say.
        m_model.hold = true;
        QCOMPARE(chats->send(path, u"Długo?"_s), path);
        QTRY_COMPARE(m_model.held != nullptr, true);
        chats->stop(path);
        m_model.release();
        QCoreApplication::processEvents();
        QCOMPARE(wanted.size(), 6);
    }

    void segments_data()
    {
        QTest::addColumn<QString>("markdown");
        QTest::addColumn<QStringList>("kinds"); // [] = text only: the bubble's one rich text
        const QString table = u"| Miesiąc | Przychód |\n|---|--:|\n| Styczeń | 1 200 zł |\n| Luty | 1 500 zł |\n\n"_s;
        QTest::newRow("plain text") << u"Zwykła odpowiedź z **pogrubieniem** i `kodem`."_s << QStringList();
        QTest::newRow("a fence without a language") << u"```\nflowchart LR\nA-->B\n```"_s << QStringList();
        QTest::newRow("code") << u"```luau\nprint(\"<b>\")\n```"_s << QStringList();
        QTest::newRow("a diagram between words")
                << u"Oto schemat:\n\n```mermaid\nflowchart LR\n  A[Pomysł] --> B{Czy warto?}\n```\n\nI dalej."_s
                << QStringList{u"text"_s, u"diagram"_s, u"text"_s};
        QTest::newRow("Mermaid in capitals") << u"```Mermaid\ngraph TD\nA-->B\n```"_s << QStringList{u"diagram"_s};
        QTest::newRow("a sequence, Polish") << u"Zażółć gęślą jaźń:\n\n```mermaid\nsequenceDiagram\nAla->>Ola: Cześć\n```"_s
                                           << QStringList{u"text"_s, u"diagram"_s};
        QTest::newRow("a pie, tildes") << u"~~~mermaid\npie\n\"Tak\" : 3\n\"Nie\" : 1\n~~~"_s << QStringList{u"diagram"_s};
        QTest::newRow("a type not drawn") << u"```mermaid\nclassDiagram\nA <|-- B\n```"_s << QStringList();
        QTest::newRow("no diagram type") << u"```mermaid\nzażółć gęślą jaźń\n```"_s << QStringList();
        QTest::newRow("an empty diagram") << u"```mermaid\n```"_s << QStringList();
        QTest::newRow("unterminated") << u"Rysuję:\n\n```mermaid\nflowchart LR\nA-->B"_s << QStringList();
        QTest::newRow("nested backticks") << u"````markdown\n```mermaid\nflowchart LR\nA-->B\n```\n````"_s << QStringList();
        QTest::newRow("a longer closing fence") << u"```mermaid\nflowchart LR\nA-->B\n````"_s << QStringList{u"diagram"_s};
        QTest::newRow("a shorter closing fence") << u"````mermaid\nflowchart LR\nA-->B\n```"_s << QStringList();
        QTest::newRow("a chart of the table above") << table + u"```chart\ntype: bar\ntitle: Budżet\n```"_s
                                                    << QStringList{u"text"_s, u"chart"_s};
        QTest::newRow("a chart in Polish keys") << table + u"```chart\ntyp: kołowy\ntytuł: Przychody\n```"_s
                                                << QStringList{u"text"_s, u"chart"_s};
        QTest::newRow("a chart without a table") << u"```chart\ntype: bar\n```"_s << QStringList();
        QTest::newRow("a chart that fails") << table + u"```chart\ntype: bar\nkolor: zielony\n```"_s << QStringList();
        QTest::newRow("a chart of a column not there") << table + u"```chart\ny: Koszt\n```"_s << QStringList();
        QTest::newRow("a chart of a query") << u"```chart\nquery: type:tasks group:status\n```"_s << QStringList();
        QTest::newRow("an unterminated chart") << table + u"```chart\ntype: bar"_s << QStringList();
        QTest::newRow("a math block") << u"```math\n\\frac{a}{b}\n```"_s << QStringList{u"math"_s};
        QTest::newRow("latex, tex") << u"```latex\nE = mc^2\n```\n\n```tex\n\\sqrt{2}\n```"_s
                                    << QStringList{u"math"_s, u"math"_s};
        QTest::newRow("an empty math block") << u"```math\n```"_s << QStringList();
        QTest::newRow("a display formula") << u"Wzór:\n\n$$\\sum_{i=1}^{n} i = \\frac{n(n+1)}{2}$$\n\nKoniec."_s
                                           << QStringList{u"text"_s, u"math"_s, u"text"_s};
        QTest::newRow("a formula in a sentence") << u"Wynik to $$x^2$$ i tyle."_s << QStringList();
        QTest::newRow("an inline formula") << u"Cena $x$ zł."_s << QStringList();
        QTest::newRow("a diagram, then a formula") << u"```mermaid\nflowchart TD\nA-->B\n```\n\n```math\nx^2\n```"_s
                                                   << QStringList{u"diagram"_s, u"math"_s};
    }

    void segments()
    {
        QFETCH(QString, markdown);
        QFETCH(QStringList, kinds);
        markdown::RenderContext context;
        context.locale = dates::UiLocale::Polish;
        const QVariantList segments = ChatMessageModel::renderSegments(markdown, context);
        QStringList got;
        for (const QVariant &segment : segments)
            got << segment.toMap().value(u"kind"_s).toString();
        QCOMPARE(got, kinds);
        // The bubble's rich text stays as it was (the code shown as written).
        QVERIFY(!ChatMessageModel::renderMessage(markdown, context).isEmpty());
        for (const QVariant &v : segments) {
            const QVariantMap segment = v.toMap();
            // Every key of every part (the delegates read them all).
            QCOMPARE(segment.keys(), (QStringList{u"chartData"_s, u"code"_s, u"html"_s, u"kind"_s, u"source"_s}));
            const QString kind = segment.value(u"kind"_s).toString();
            const QString html = segment.value(u"html"_s).toString();
            QVERIFY2(!html.isEmpty(), qPrintable(kind));
            if (kind == u"text") {
                // No room under its last paragraph: the gap between the parts is the room.
                const qsizetype at = html.lastIndexOf(u"margin-bottom:"_s);
                QVERIFY2(at < 0 || QStringView(html).sliced(at).startsWith(u"margin-bottom:0px"), qPrintable(html));
                continue;
            }
            // A drawing: what it draws and the block as written (Copy source).
            const QString code = segment.value(u"code"_s).toString();
            const QString source = segment.value(u"source"_s).toString();
            QVERIFY2(!code.isEmpty() && markdown.contains(code), qPrintable(code));
            QVERIFY2(markdown.contains(source.trimmed()) && source.contains(code), qPrintable(source));
            QCOMPARE(segment.value(u"chartData"_s).toMap().isEmpty(), kind != u"chart");
            if (kind == u"diagram")
                QVERIFY2(html.contains(u"<pre"_s), qPrintable(html)); // (shown when it cannot be drawn)
        }
    }

    void segmentsOfAChart()
    {
        // The numbers come from the message's own table, as the editor reads a page's (Polish numbers).
        markdown::RenderContext context;
        const QString message = u"Przychody:\n\n| Miesiąc | Przychód | Koszt |\n|---|--:|--:|\n| Styczeń | 1 200 zł | 900 |\n"
                                "| Luty | 1 500,50 zł | n/d |\n\n```chart\ntype: line\ntitle: Budżet 2026\n```\n\nTak to wygląda."_s;
        const QVariantList segments = ChatMessageModel::renderSegments(message, context);
        QCOMPARE(segments.size(), 3);
        const QVariantMap chart = segments.at(1).toMap();
        QCOMPARE(chart.value(u"code"_s).toString(), u"type: line\ntitle: Budżet 2026"_s);
        QCOMPARE(chart.value(u"source"_s).toString(), u"```chart\ntype: line\ntitle: Budżet 2026\n```"_s);
        const QVariantMap data = chart.value(u"chartData"_s).toMap();
        QVERIFY(data.value(u"ok"_s).toBool());
        QCOMPARE(data.value(u"labels"_s).toStringList(), (QStringList{u"Styczeń"_s, u"Luty"_s}));
        QCOMPARE(data.value(u"labelsName"_s).toString(), u"Miesiąc"_s);
        const QVariantList series = data.value(u"series"_s).toList();
        QCOMPARE(series.size(), 2);
        QCOMPARE(series.at(0).toMap().value(u"name"_s).toString(), u"Przychód"_s);
        const QVariantList income = series.at(0).toMap().value(u"values"_s).toList();
        QCOMPARE(income.at(0).toDouble(), 1200.0);
        QCOMPARE(income.at(1).toDouble(), 1500.5);
        const QVariantList cost = series.at(1).toMap().value(u"values"_s).toList();
        QCOMPARE(cost.at(0).toDouble(), 900.0);
        QVERIFY(cost.at(1).isNull()); // (a gap)
        // The table stays a rich-text table of the text before it; the words after, their own part.
        QVERIFY(segments.at(0).toMap().value(u"html"_s).toString().contains(u"<table"_s));
        QVERIFY(segments.at(2).toMap().value(u"html"_s).toString().contains(u"Tak to wygląda."_s));
        // English numbers in an English chat.
        context.locale = dates::UiLocale::English;
        const QVariantList english =
                ChatMessageModel::renderSegments(u"| Day | Km |\n|---|--:|\n| Mon | 12.5 |\n\n```chart\ntype: bar\n```"_s, context);
        QCOMPARE(english.size(), 2);
        const QVariantList km = english.at(1).toMap().value(u"chartData"_s).toMap().value(u"series"_s).toList();
        QCOMPARE(km.first().toMap().value(u"values"_s).toList().first().toDouble(), 12.5);
    }

    void segmentsOfAConversation()
    {
        // The model's rows: a message with a drawing in parts, one without none; pictures apart.
        auto ws = open({{u"AI/2026-11-22 Schemat.md"_s,
                         u"---\ntasks: ignore\n---\n- 🧑 Narysuj proces\n- 🤖 Proszę:\n\n  ```mermaid\n  flowchart LR\n"
                         "    A[Zamówienie] --> B[Wysyłka]\n  ```\n\n  Gotowe.\n- 🤖 Bez rysunku.\n"_s}});
        ChatMessageModel model;
        model.setWorkspace(ws.get());
        model.setPath(u"AI/2026-11-22 Schemat.md"_s);
        model.refreshNow();
        QCOMPARE(model.rowCount(), 3);
        QVERIFY(value(model, 0, "segments").toList().isEmpty());
        const QVariantList parts = value(model, 1, "segments").toList();
        QCOMPARE(parts.size(), 3);
        QCOMPARE(parts.at(1).toMap().value(u"kind"_s).toString(), u"diagram"_s);
        QCOMPARE(parts.at(1).toMap().value(u"code"_s).toString(), u"flowchart LR\n  A[Zamówienie] --> B[Wysyłka]"_s);
        QVERIFY(value(model, 1, "messageHtml").toString().contains(u"<pre"_s)); // (as before)
        QVERIFY(value(model, 2, "segments").toList().isEmpty());
        // The answer being written: its finished blocks in parts too.
        QCOMPARE(model.segments(u"```math\nx^2\n```"_s).size(), 1);
        QVERIFY(model.segments(u"Tylko tekst."_s).isEmpty());
        QVERIFY(model.segments(QString()).isEmpty());
    }
};

QTEST_MAIN(TestContentChat)
#include "tst_content_chat.moc"
