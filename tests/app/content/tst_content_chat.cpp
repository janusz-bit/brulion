// The AI chat as the app runs it, on a real Workspace and its notebook, the model on the device
// played by the test (ai::LocalBackend): a new conversation (its page in AI/ named after the
// question, the page it is about, the journal's line that links to it), the assistant's turn (the
// conversation's messages and the page's text in the request, what it says it does, the answer
// written into the page), the next message with the history, a change asked a yes for in the
// conversation (questionChat) and written, Stop (nothing written for that turn), a failure and Try
// again, the extensions loaded again during a turn, the assistant switched off; the messages as the view shows them (ChatMessageModel: roles,
// rendered text — links, lists, numbers, tasks, headings, code —, the page it is about) and the
// list of conversations (ChatListModel: only conversations of AI/, newest first, their names
// without the date, previews).
#include "chatmodels.h"

#include "workspace.h"
#include "workspacechats.h"
#include "workspaceextensions.h"

#include "ai/local.h"
#include "vault/vault.h"

#include <QDir>
#include <QFile>
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
    }

    void aConversation()
    {
        auto ws = open(notes());
        shell::WorkspaceChats *chats = ws->chats();
        QSignalSpy written(chats, &shell::WorkspaceChats::written);
        QStringList statuses;
        connect(chats, &shell::WorkspaceChats::stateChanged, this, [&] {
            if (!chats->status().isEmpty() && (statuses.isEmpty() || statuses.last() != chats->status()))
                statuses << chats->status();
        });
        m_model.replies = {calling(u"search_notes"_s, {{u"query"_s, u"jutro"_s}}),
                           answer(u"Jutro masz jedno zadanie:\n\n- zapłacić czynsz — [[Dom]] ⏰ 2026-11-16 09:00"_s)};

        // A new conversation: its page, named after the question, about the page; the day's line.
        const QString path = chats->send(QString(), u"  Co mam jutro do zrobienia?  "_s, u"Dom"_s);
        QCOMPARE(path, u"AI/2026-11-15 Co mam jutro do zrobienia.md"_s);
        QCOMPARE(chats->running(), path);
        QCOMPARE(ws->read(path), u"---\nzadania: ignoruj\n---\n- 🧑 Co mam jutro do zrobienia? ([[Dom]])\n"_s);
        QVERIFY2(ws->read(ws->journalPath(u"2026-11-15"_s)).contains(u"- 🤖 [[AI/2026-11-15 Co mam jutro do zrobienia]]"_s),
                 qPrintable(ws->read(ws->journalPath(u"2026-11-15"_s))));
        QVERIFY(chats->isConversation(path));
        QVERIFY(!chats->isConversation(u"Dom.md"_s));
        // A second message waits for the turn.
        QCOMPARE(chats->send(path, u"A pojutrze?"_s), QString());

        // The answer lands in the page; the page's text went along; what it did was said.
        QTRY_COMPARE(chats->running(), QString());
        QCOMPARE(ws->read(path), u"---\nzadania: ignoruj\n---\n- 🧑 Co mam jutro do zrobienia? ([[Dom]])\n"
                                 "- 🤖 Jutro masz jedno zadanie:\n\n  - zapłacić czynsz — [[Dom]] `⏰ 2026-11-16 09:00`\n"_s);
        QCOMPARE(written.size(), 2); // the question, the answer
        QCOMPARE(m_model.requests.size(), 2);
        const ai::Message &first = m_model.requests[0].messages.first();
        QVERIFY2(first.content.startsWith(u"Dziś jest 2026-11-15.\n\nRozmowa dotyczy strony [[Dom]]:\n\n- [ ] Zapłacić czynsz"_s),
                 qPrintable(first.content));
        QVERIFY(first.content.endsWith(u"Co mam jutro do zrobienia? ([[Dom]])"_s));
        QVERIFY(!m_model.requests[0].tools.isEmpty());
        QCOMPARE(statuses, QStringList{u"Szukam w notatkach: „jutro”…"_s});
        QVERIFY(chats->status().isEmpty());

        // The next message: the history goes with it.
        QCOMPARE(chats->send(path, u"A pojutrze?"_s), path);
        QTRY_COMPARE(chats->running(), QString());
        const QList<ai::Message> history = m_model.requests[2].messages;
        QCOMPARE(history.size(), 3);
        QCOMPARE(history[1].role, u"assistant"_s);
        QVERIFY(history[1].content.startsWith(u"Jutro masz jedno zadanie:"_s));
        QCOMPARE(history[2].content, u"A pojutrze?"_s);
        QVERIFY(ws->read(path).endsWith(u"- 🧑 A pojutrze?\n- 🤖 Dobrze.\n"_s));

        // A second conversation with the same question: a name of its own.
        m_model.replies = {answer(u"Nic."_s)};
        QCOMPARE(chats->send(QString(), u"Co mam jutro do zrobienia?"_s), u"AI/2026-11-15 Co mam jutro do zrobienia 2.md"_s);
        QTRY_COMPARE(chats->running(), QString());

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
        QCOMPARE(chats->running(), path);
        chats->showing(path, true);
        QVERIFY(chats->isShown(path));
        QVERIFY(chats->isShown(path.toUpper()));
        ext->answerChoice(ext->questionOptions().value(0));
        QCOMPARE(ext->questionChat(), QString());
        QTRY_COMPARE(chats->running(), QString());
        chats->showing(path, false);
        QVERIFY(!chats->isShown(path));
        QCOMPARE(ws->read(u"Zakupy.md"_s), u"- chleb\n- mleko\n"_s);
        QVERIFY2(ws->read(path).endsWith(u"- 🤖 Dopisałem mleko.\n\n  ✏️ Zmienione: [[Zakupy]]\n"_s), qPrintable(ws->read(path)));
    }

    void stopFailAndTryAgain()
    {
        auto ws = open(notes());
        shell::WorkspaceChats *chats = ws->chats();
        // Stop: the answer on its way is dropped, the question stays for Try again.
        m_model.hold = true;
        const QString path = chats->send(QString(), u"Długie pytanie"_s);
        QTRY_COMPARE(m_model.requests.size(), 1);
        chats->stop();
        QCOMPARE(chats->running(), QString());
        m_model.release();
        QCoreApplication::processEvents();
        QCOMPARE(chats->running(), QString());
        QVERIFY(ws->read(path).endsWith(u"- 🧑 Długie pytanie\n"_s));
        QCOMPARE(ws->extensions()->aiPending(), 0);

        // A failure: said in the conversation, Try again runs the turn on the page as it is.
        m_model.hold = false;
        ai::Reply broken;
        broken.error = u"the model could not be loaded"_s;
        m_model.replies = {broken, answer(u"Jednak tak."_s)};
        QVERIFY(chats->retry(path));
        QTRY_COMPARE(chats->failedPath(), path);
        QVERIFY2(chats->failure().startsWith(u"AI: "_s) && chats->failure().contains(u"could not be loaded"_s),
                 qPrintable(chats->failure()));
        QVERIFY(ws->read(path).endsWith(u"- 🧑 Długie pytanie\n"_s));
        QVERIFY(chats->retry(path));
        QCOMPARE(chats->failure(), QString());
        QTRY_COMPARE(chats->running(), QString());
        QVERIFY(ws->read(path).endsWith(u"- 🤖 Jednak tak.\n"_s));
        // Nothing to answer: the last message is the AI's.
        QVERIFY(!chats->retry(path));
        QVERIFY(!chats->failure().isEmpty());
        chats->dismissFailure();
        QCOMPARE(chats->failure(), QString());
        // Nothing to send.
        QCOMPARE(chats->send(path, u"   "_s), QString());

        // The extensions loaded again while the model answers: the turn ends, said so, and its
        // answer, when it comes, is written nowhere.
        m_model.hold = true;
        const qsizetype before = m_model.requests.size();
        QCOMPARE(chats->send(path, u"Jeszcze raz?"_s), path);
        QTRY_COMPARE(m_model.requests.size(), before + 1);
        ws->extensions()->reloadAll();
        QCOMPARE(chats->running(), QString());
        QCOMPARE(chats->failedPath(), path);
        QVERIFY2(chats->failure().contains(u"loaded again"_s), qPrintable(chats->failure()));
        m_model.release();
        QCoreApplication::processEvents();
        QVERIFY(ws->read(path).endsWith(u"- 🧑 Jeszcze raz?\n"_s));
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
        QVERIFY(m_model.partial); // (the chat watches its requests)
        QTRY_COMPARE(chats->partial(), u"Jutro masz"_s);
        QCOMPARE(chats->thoughts(), u"Użytkownik pyta o jutro."_s);
        QCOMPARE(chats->speed(), QLocale().toString(20.0, 'f', 1) + u" tok/s"_s); // 40 tokens in the last 2 s
        QCOMPARE(chats->partialSettled(), QString()); // (one block, being written)
        QCOMPARE(chats->partialTail(), u"Jutro masz"_s);
        QCOMPARE(chats->running(), path);
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
                bothThere = chats->partial() == u"Jutro masz" && chats->running() == path;
        });
        m_model.release();
        QTRY_COMPARE(chats->running(), QString());
        QVERIFY(bothThere);
        QCOMPARE(chats->partial(), QString());
        QCOMPARE(chats->thoughts(), QString());
        QCOMPARE(chats->speed(), QString());
        QVERIFY(!chats->runningShown());
        chats->showing(path, false);
        QVERIFY(ws->read(path).endsWith(u"- 🤖 Jutro masz jogę.\n"_s));

        // Each request of the turn starts anew: what the first wrote goes when a tool has run.
        m_model.writing = {{u"Sprawdzę."_s, QString()}};
        m_model.replies = {calling(u"search_notes"_s, {{u"query"_s, u"joga"_s}}), answer(u"Joga jest jutro."_s)};
        const qsizetype before = m_model.requests.size();
        QCOMPARE(chats->send(path, u"Na pewno?"_s), path);
        QTRY_COMPARE(chats->partial(), u"Sprawdzę."_s);
        QCOMPARE(chats->speed(), QString()); // (no counts: a provider on the network)
        m_model.release(); // the tool runs, the next request goes out (at once)
        QCOMPARE(m_model.requests.size(), before + 2);
        QCOMPARE(chats->partial(), QString());
        QTRY_COMPARE(chats->partial(), u"Sprawdzę."_s);
        m_model.release();
        QTRY_COMPARE(chats->running(), QString());
        QCOMPARE(chats->partial(), QString());
        QVERIFY(ws->read(path).endsWith(u"- 🤖 Joga jest jutro.\n"_s));
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
        chats->stop();
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
        QVERIFY(chats->failure().contains(u"Settings"_s) || chats->failure().contains(u"Ustawieniach"_s));
        QVERIFY(!ws->exists(u"AI/2026-11-15 Halo.md"_s));
        QVERIFY(m_model.requests.isEmpty());
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
