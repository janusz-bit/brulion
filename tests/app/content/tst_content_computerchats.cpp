// The conversations the desktop app on another computer runs, followed and steered from this device
// (shell/computerchats.h, WorkspaceChats' mirror; core acp/appchats.h), on a real Workspace and its
// notebook, the computer's `acp serve` played over a fake transport: its long poll asked while the
// app is in front, a turn that runs there running here (ChatTurn.remoteHost, the list's state, the
// top bar's count), the conversation shown asked for in full (its answer as it is written, tool
// calls, plan), its question with options answered there, Stop and the next message (queued,
// steered in; pictures refused) sent there — never this device's own turn —, the messages waiting
// there shown, the turn's end with the answer read from the computer's page before the notebook's
// sync brings it (the messages' model shows it, a notification says it, the conversation is unread),
// a failure there, a page this device does not have yet, the app gone there (503) and an older
// computer (404), asked again later.
#include "chatmodels.h"
#include "codeprojects.h"
#include "computerchats.h"
#include "workspace.h"
#include "workspaceagents.h"
#include "workspacechats.h"

#include "chat/chat.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>
#include <QUrlQuery>

#include <memory>

using namespace Qt::StringLiterals;
using namespace brulion;
using brulion::content::ChatMessageModel;
using brulion::shell::AgentConfig;
using brulion::shell::ChatTurn;
using brulion::shell::ComputerChats;
using brulion::shell::Workspace;
using brulion::shell::WorkspaceChats;

namespace {

/// The computer's `acp serve`: every request waits in `pending` until the test answers it.
class FakeComputer : public sync::HttpTransport {
public:
    struct Pending {
        sync::HttpRequest request;
        sync::HttpCallback done;
        QString endpoint() const
        {
            const QString path = request.url.path();
            const qsizetype at = path.indexOf(u"/acp/v1/"_s);
            return at < 0 ? path : path.mid(at + 8);
        }
        QJsonObject body() const { return QJsonDocument::fromJson(request.body).object(); }
        QString query(const QString &key) const { return QUrlQuery(request.url).queryItemValue(key, QUrl::FullyDecoded); }
    };
    QList<Pending> pending;

    void send(const sync::HttpRequest &request, sync::HttpCallback done) override
    {
        pending.append({request, std::move(done)});
    }
    /// The first request waiting for `endpoint` (and, for GET chats, whose `wait` is `wait`: -1 any).
    std::optional<Pending> take(const QString &endpoint, int wait = -1)
    {
        for (qsizetype i = 0; i < pending.size(); ++i) {
            const Pending &p = pending.at(i);
            if (p.endpoint() == endpoint && (wait < 0 || p.query(u"wait"_s).toInt() == wait))
                return pending.takeAt(i);
        }
        return std::nullopt;
    }
    bool waiting(const QString &endpoint, int wait = -1) const
    {
        return std::ranges::any_of(pending, [&](const Pending &p) {
            return p.endpoint() == endpoint && (wait < 0 || p.query(u"wait"_s).toInt() == wait);
        });
    }
    static sync::HttpResponse json(int status, const QJsonObject &o)
    {
        sync::HttpResponse r;
        r.status = status;
        r.body = QJsonDocument(o).toJson(QJsonDocument::Compact);
        r.headers.insert("content-type"_ba, "application/json"_ba);
        return r;
    }
    static sync::HttpResponse error(int status, const QString &code, const QString &message)
    {
        return json(status, QJsonObject{{u"error"_s, QJsonObject{{u"code"_s, code}, {u"message"_s, message}}}});
    }
};

const QString Url = u"http://nixos.tail1ea20e.ts.net:8790"_s;
const QString Path = u"AI/2026-11-26 Popraw testy żółwia.md"_s;
const QString Page = u"---\npartner: acp:claude\nproject: brulion\n---\n\n"
                     "- 🧑 Popraw testy żółwia\n  czas: 2026-11-26 09:00\n"_s;

acp::RemoteChat row(const QString &state, int messages = 1)
{
    acp::RemoteChat c;
    c.path = Path;
    c.name = u"Popraw testy żółwia"_s;
    c.partner = u"acp:claude"_s;
    c.partnerName = u"Claude Code"_s;
    c.state = state;
    c.project = u"brulion"_s;
    c.messages = messages;
    return c;
}

QJsonObject chats(qint64 revision, const QList<acp::RemoteChat> &rows,
                  const std::optional<acp::RemoteChatDetail> &detail = std::nullopt)
{
    acp::RemoteChats c;
    c.revision = revision;
    c.chats = rows;
    c.detail = detail;
    return c.toJson();
}

} // namespace

class TestComputerChats : public QObject {
    Q_OBJECT

    QTemporaryDir m_tmp;
    int m_n = 0;
    FakeComputer m_computer;
    bool m_active = true;

    std::unique_ptr<Workspace> open(bool withPage = true)
    {
        const QString root = m_tmp.filePath(u"vault-%1"_s.arg(++m_n));
        QDir().mkpath(root + u"/AI"_s);
        if (withPage) {
            QFile f(root + u"/"_s + Path);
            if (f.open(QIODevice::WriteOnly))
                f.write(Page.toUtf8());
        }
        auto ws = std::make_unique<Workspace>();
        ws->setClock([] { return QDateTime(QDate(2026, 11, 26), QTime(9, 15)); });
        ws->setUiLanguage(u"pl"_s);
        QString error;
        if (!ws->open(root, root + u"-state"_s, &error))
            qWarning() << error;
        ws->finishIndexing();
        m_computer.pending.clear();
        m_active = true;
        WorkspaceChats *c = ws->chats();
        c->agents()->setTransport(&m_computer);
        c->agents()->computerChats()->setRetryDelays(50, 50, 50, 0);
        c->setAppActive([this] { return m_active; });
        AgentConfig remote{u"claude-nixos"_s, u"Claude Code"_s, QString(), true};
        remote.url = Url;
        remote.remote = u"claude"_s;
        remote.host = u"nixos"_s;
        remote.root = u"/home/ala/Notatki"_s;
        remote.token = u"bat_telefon"_s;
        c->setAgents({remote});
        return ws;
    }

    /// Answers the long poll waiting (its `since` checked when given).
    void poll(const QJsonObject &answer, qint64 since = -2)
    {
        QTRY_VERIFY(m_computer.waiting(u"chats"_s, ComputerChats::WaitSeconds));
        const FakeComputer::Pending p = *m_computer.take(u"chats"_s, ComputerChats::WaitSeconds);
        QCOMPARE(p.request.headers.contains(std::pair(QByteArray("Authorization"), QByteArray("Bearer bat_telefon"))),
                 true);
        if (since != -2)
            QCOMPARE(p.query(u"since"_s).toLongLong(), since);
        p.done(FakeComputer::json(200, answer));
    }

private Q_SLOTS:
    void followedAndSteered()
    {
        auto ws = open();
        WorkspaceChats *c = ws->chats();
        ComputerChats *computers = c->agents()->computerChats();
        QVERIFY(computers->active());
        // Nothing runs there yet.
        poll(chats(1, {}), -1);
        QVERIFY(!c->isRunning(Path));
        // A turn the app there started: running here, said to be that computer's.
        QSignalSpy listChanged(c, &WorkspaceChats::computerChatsChanged);
        acp::RemoteChat running = row(u"running"_s);
        running.status = u"Czytam testy…"_s;
        poll(chats(2, {running}), 1);
        QTRY_VERIFY(c->isRunning(Path));
        ChatTurn *turn = c->turn(Path);
        QCOMPARE(turn->remoteHost(), u"nixos"_s);
        QCOMPARE(turn->status(), u"Czytam testy…"_s);
        QCOMPARE(c->stateOf(Path), u"running"_s);
        QCOMPARE(c->runningCount(), 1);
        QVERIFY(listChanged.count() > 0);
        const QVariantList list = c->computerChats();
        QCOMPARE(list.size(), 1);
        QCOMPARE(list.at(0).toMap().value(u"host"_s).toString(), u"nixos"_s);
        QCOMPARE(list.at(0).toMap().value(u"here"_s).toBool(), true);

        // Shown here: its whole turn asked for at once, and in every long poll after.
        c->showing(Path, true);
        QTRY_VERIFY(m_computer.waiting(u"chats"_s, 0));
        FakeComputer::Pending quick = *m_computer.take(u"chats"_s, 0);
        QCOMPARE(quick.query(u"path"_s), Path);
        acp::RemoteChatDetail detail;
        detail.path = Path;
        detail.state = u"running"_s;
        detail.status = u"Uruchamiam ctest"_s;
        detail.partial = u"Zmieniłem **trzy** pliki."_s;
        detail.tools = {{u"Read tests/turtle.cpp"_s, u"completed"_s, u"read"_s}};
        detail.plan = {{u"Poprawić test żółwia"_s, u"in_progress"_s}};
        detail.messages = 1;
        quick.done(FakeComputer::json(200, chats(3, {running}, detail)));
        QTRY_COMPARE(turn->partial(), u"Zmieniłem **trzy** pliki."_s);
        QCOMPARE(turn->status(), u"Uruchamiam ctest"_s);
        QCOMPARE(turn->tools().size(), 1);
        QCOMPARE(turn->tools().at(0).toMap().value(u"title"_s).toString(), u"Read tests/turtle.cpp"_s);
        QCOMPARE(turn->plan().at(0).toMap().value(u"content"_s).toString(), u"Poprawić test żółwia"_s);

        // (The long poll on its way — sent before it was shown, or after —: answered without a detail.)
        {
            QTRY_VERIFY(m_computer.waiting(u"chats"_s, ComputerChats::WaitSeconds));
            const FakeComputer::Pending p = *m_computer.take(u"chats"_s, ComputerChats::WaitSeconds);
            p.done(FakeComputer::json(200, chats(3, {running})));
        }
        QCOMPARE(turn->partial(), u"Zmieniłem **trzy** pliki."_s); // (its detail kept)
        // The agent asks there: asked here, its options; the answer goes there.
        QSignalSpy questions(c, &WorkspaceChats::questionsChanged);
        acp::RemoteChat asking = row(u"asking"_s);
        asking.question = u"Uruchomić ctest?"_s;
        acp::RemoteChatDetail asked = detail;
        asked.state = u"asking"_s;
        asked.question.text = u"Uruchomić ctest?"_s;
        asked.question.details = u"ctest -R żółw"_s;
        asked.question.options = {u"Allow once"_s, u"Reject"_s};
        asked.question.file = u"tests/turtle.cpp"_s;
        asked.question.oldText = u"a\n"_s;
        asked.question.newText = u"b\n"_s;
        {
            QTRY_VERIFY(m_computer.waiting(u"chats"_s, ComputerChats::WaitSeconds));
            const FakeComputer::Pending p = *m_computer.take(u"chats"_s, ComputerChats::WaitSeconds);
            QCOMPARE(p.query(u"path"_s), Path); // (the one shown: in the long poll too)
            p.done(FakeComputer::json(200, chats(4, {asking}, asked)));
        }
        QTRY_VERIFY(c->asking(Path));
        QVERIFY(questions.count() > 0);
        QCOMPARE(c->stateOf(Path), u"asking"_s);
        QCOMPARE(c->questionOptions(Path), (QStringList{u"Allow once"_s, u"Reject"_s}));
        QCOMPARE(c->questionText(Path), u"Uruchomić ctest?\nctest -R żółw"_s);
        QCOMPARE(c->questionDiff(Path).value(u"newText"_s).toString(), u"b\n"_s);
        c->answer(Path, u"Allow once"_s);
        std::optional<FakeComputer::Pending> answered = m_computer.take(u"chats/answer"_s);
        QVERIFY(answered);
        QCOMPARE(answered->body(), (QJsonObject{{u"path"_s, Path}, {u"option"_s, u"Allow once"_s}}));
        answered->done(FakeComputer::json(200, QJsonObject{{u"answered"_s, true}}));

        // The next message while it runs: in line there; steered in; pictures refused.
        QVERIFY(c->queue(Path, u"  A potem zrób commit  "_s));
        std::optional<FakeComputer::Pending> sent = m_computer.take(u"chats/send"_s);
        QVERIFY(sent);
        QCOMPARE(sent->body(), (QJsonObject{{u"path"_s, Path}, {u"text"_s, u"A potem zrób commit"_s}, {u"mode"_s, u"queue"_s}}));
        sent->done(FakeComputer::json(200, QJsonObject{{u"path"_s, Path}}));
        QVERIFY(c->steer(Path, u"Najpierw testy!"_s));
        sent = m_computer.take(u"chats/send"_s);
        QVERIFY(sent);
        QCOMPARE(sent->body().value(u"mode"_s).toString(), u"steer"_s);
        QSignalSpy errors(ws.get(), &Workspace::errorOccurred);
        sent->done(FakeComputer::error(409, u"busy"_s, u"Its turn runs"_s));
        QTRY_COMPARE(errors.count(), 1);
        QVERIFY(errors.at(0).at(0).toString().contains(u"nixos"_s));
        QVERIFY(!c->queue(Path, u"Zdjęcie"_s, {u"attachments/zdjecie.png"_s}));
        QCOMPARE(errors.count(), 2);
        QVERIFY(!m_computer.waiting(u"chats/send"_s));
        // (A plain send() of a running conversation of there: in line there too.)
        QCOMPARE(c->send(Path, u"I jeszcze dokumentacja"_s), Path);
        QVERIFY(m_computer.take(u"chats/send"_s));

        // What waits there shown (not taken back, not sent ahead from here).
        acp::RemoteChatDetail waiting = detail;
        waiting.waiting = {u"A potem zrób commit"_s};
        QTRY_VERIFY(m_computer.waiting(u"chats"_s));
        while (std::optional<FakeComputer::Pending> p = m_computer.take(u"chats"_s, 0))
            p->done(FakeComputer::json(200, chats(5, {running}, waiting)));
        if (std::optional<FakeComputer::Pending> p = m_computer.take(u"chats"_s, ComputerChats::WaitSeconds))
            p->done(FakeComputer::json(200, chats(5, {running}, waiting)));
        QTRY_COMPARE(c->queued(Path).size(), 1);
        QCOMPARE(c->queued(Path).at(0).toMap().value(u"remote"_s).toBool(), true);
        QVERIFY(!c->sendNow(Path, -1));
        QVERIFY(!c->queueHeld(Path));
        QVERIFY(!c->asking(Path)); // (answered there)

        // Stop: there.
        c->stop(Path);
        std::optional<FakeComputer::Pending> stopped = m_computer.take(u"chats/stop"_s);
        QVERIFY(stopped);
        QCOMPARE(stopped->body(), (QJsonObject{{u"path"_s, Path}}));
        stopped->done(FakeComputer::json(200, QJsonObject{{u"stopped"_s, true}}));
        QVERIFY(c->isRunning(Path)); // (until the computer says it ended)
        // (Closing the notebook's turns stops none of that computer's.)
        c->stopAll();
        QVERIFY(!m_computer.waiting(u"chats/stop"_s));
        c->showing(Path, false);
    }

    void endedThereAnswerFromItsPage()
    {
        auto ws = open();
        WorkspaceChats *c = ws->chats();
        poll(chats(1, {row(u"running"_s)}));
        QTRY_VERIFY(c->isRunning(Path));
        // In a pocket now: what comes is a notification.
        m_active = false;
        c->setAppActive([this] { return m_active; });
        QVERIFY(c->agents()->computerChats()->active()); // (its turn runs: followed still)
        QSignalSpy notified(c, &WorkspaceChats::notificationWanted);
        QSignalSpy pageCame(c, &WorkspaceChats::computerPageChanged);
        // Answered there: 3 messages there, 1 here — the page is read from there.
        acp::RemoteChat ended = row(u"idle"_s, 3);
        ended.ended = u"2026-11-26T09:20:00"_s;
        poll(chats(2, {ended}));
        QTRY_VERIFY(!c->isRunning(Path));
        QCOMPARE(c->turn(Path)->remoteHost(), QString());
        QTRY_VERIFY(m_computer.waiting(u"chats/page"_s));
        std::optional<FakeComputer::Pending> page = m_computer.take(u"chats/page"_s);
        QCOMPARE(page->query(u"path"_s), Path);
        const QString theirs = Page + u"- 🤖 Poprawione: trzy pliki.\n  Testy przechodzą.\n  czas: 2026-11-26 09:20\n"_s
                               + u"- 🧑 Dzięki\n  czas: 2026-11-26 09:21\n"_s;
        page->done(FakeComputer::json(200, QJsonObject{{u"path"_s, Path}, {u"text"_s, theirs}}));
        QTRY_COMPARE(pageCame.count(), 1);
        QCOMPARE(c->computerPage(Path), theirs);
        // (The last message is the user's: no answer to say.)
        QCOMPARE(notified.count(), 0);
        QVERIFY(c->isUnread(Path));
        // The messages' model shows the computer's page until this device's copy catches up.
        ChatMessageModel model;
        model.setWorkspace(ws.get());
        model.setPath(Path);
        model.refreshNow();
        QCOMPARE(model.rowCount(), 3);
        // The notebook's sync brings it: this device's own.
        QVERIFY(ws->write(Path, theirs));
        QCOMPARE(c->computerPage(Path), QString());
        // Following stops with the turn (the app not in front).
        QVERIFY(!c->agents()->computerChats()->active());
    }

    void answerNotified()
    {
        auto ws = open();
        WorkspaceChats *c = ws->chats();
        poll(chats(1, {row(u"running"_s)}));
        QTRY_VERIFY(c->isRunning(Path));
        m_active = false;
        c->setAppActive([this] { return m_active; });
        QSignalSpy notified(c, &WorkspaceChats::notificationWanted);
        poll(chats(2, {row(u"idle"_s, 2)}));
        QTRY_VERIFY(m_computer.waiting(u"chats/page"_s));
        m_computer.take(u"chats/page"_s)
                ->done(FakeComputer::json(
                        200, QJsonObject{{u"path"_s, Path},
                                         {u"text"_s, QString(Page + u"- 🤖 Gotowe, testy żółwia przechodzą.\n  Szczegóły niżej.\n"_s)}}));
        QTRY_COMPARE(notified.count(), 1);
        QCOMPARE(notified.at(0).at(0).toString(), Path);
        QCOMPARE(notified.at(0).at(2).toString(), u"Gotowe, testy żółwia przechodzą."_s);
    }

    void failedThere()
    {
        auto ws = open();
        WorkspaceChats *c = ws->chats();
        poll(chats(1, {row(u"running"_s)}));
        QTRY_VERIFY(c->isRunning(Path));
        acp::RemoteChat failed = row(u"failed"_s);
        failed.failure = u"Claude Code ended: out of credits"_s;
        poll(chats(2, {failed}));
        QTRY_VERIFY(!c->isRunning(Path));
        QCOMPARE(c->stateOf(Path), u"failed"_s);
        QVERIFY(c->turn(Path)->failure().contains(u"nixos"_s));
        QVERIFY(c->turn(Path)->failure().contains(u"out of credits"_s));
        // A conversation that failed before this device followed it: nothing said of it.
        const QString other = u"AI/2026-11-25 Inna.md"_s;
        acp::RemoteChat old = row(u"failed"_s);
        old.path = other;
        poll(chats(3, {failed, old}));
        QTRY_COMPARE(c->computerChats().size(), 2);
        QCOMPARE(c->stateOf(other), u"idle"_s);
    }

    void pageNotHereYet()
    {
        // Started there a minute ago: this device has no page of it yet — the computer's shown.
        auto ws = open(false);
        WorkspaceChats *c = ws->chats();
        poll(chats(1, {row(u"running"_s)}));
        QTRY_VERIFY(c->isRunning(Path));
        QCOMPARE(c->computerChats().at(0).toMap().value(u"here"_s).toBool(), false);
        QTRY_VERIFY(m_computer.waiting(u"chats/page"_s));
        m_computer.take(u"chats/page"_s)->done(FakeComputer::json(200, QJsonObject{{u"path"_s, Path}, {u"text"_s, Page}}));
        QTRY_COMPARE(c->computerPage(Path), Page);
        ChatMessageModel model;
        model.setWorkspace(ws.get());
        model.setPath(Path);
        model.refreshNow();
        QCOMPARE(model.rowCount(), 1);
        QVERIFY(model.exists());
    }

    void appGoneThere_data()
    {
        QTest::addColumn<int>("status");
        QTest::addColumn<QString>("code");
        QTest::newRow("app not running") << 503 << u"app_unavailable"_s;
        QTest::newRow("older computer") << 404 << u"not_found"_s;
    }
    void appGoneThere()
    {
        QFETCH(int, status);
        QFETCH(QString, code);
        auto ws = open();
        WorkspaceChats *c = ws->chats();
        poll(chats(1, {row(u"running"_s)}));
        QTRY_VERIFY(c->isRunning(Path));
        QVERIFY(c->agents()->computerChats()->attached(Url));
        QTRY_VERIFY(m_computer.waiting(u"chats"_s, ComputerChats::WaitSeconds));
        m_computer.take(u"chats"_s, ComputerChats::WaitSeconds)->done(FakeComputer::error(status, code, u"no"_s));
        // Its turns followed no more (nothing of a failure: it may well go on there).
        QTRY_VERIFY(!c->isRunning(Path));
        QCOMPARE(c->stateOf(Path), u"idle"_s);
        QVERIFY(!c->agents()->computerChats()->attached(Url));
        // Asked again later, from the start.
        poll(chats(7, {row(u"running"_s)}), -1);
        QTRY_VERIFY(c->isRunning(Path));
    }

    void unpairedThere()
    {
        auto ws = open();
        WorkspaceChats *c = ws->chats();
        QTRY_VERIFY(m_computer.waiting(u"chats"_s, ComputerChats::WaitSeconds));
        m_computer.take(u"chats"_s, ComputerChats::WaitSeconds)->done(FakeComputer::error(401, u"unauthorized"_s, u"no"_s));
        QTest::qWait(200);
        QVERIFY(!m_computer.waiting(u"chats"_s)); // (asked no more)
        QVERIFY(!c->isRunning(Path));
    }

    void idleThreadRunsThere_data()
    {
        QTest::addColumn<int>("status");
        QTest::addColumn<QString>("code");
        QTest::newRow("taken") << 200 << QString();
        QTest::newRow("page not there yet") << 404 << u"not_found"_s;
        QTest::newRow("its turn runs") << 409 << u"busy"_s;
    }
    void idleThreadRunsThere()
    {
        QFETCH(int, status);
        QFETCH(QString, code);
        // A thread of a project of that computer, idle: its next turn runs in the app there.
        auto ws = open();
        WorkspaceChats *c = ws->chats();
        QTRY_VERIFY(m_computer.waiting(u"projects"_s));
        m_computer.take(u"projects"_s)
                ->done(FakeComputer::json(200, QJsonObject{{u"projects"_s, QJsonArray{QJsonObject{
                                                                               {u"id"_s, u"brulion"_s},
                                                                               {u"name"_s, u"Brulion"_s},
                                                                               {u"path"_s, u"/home/ala/brulion"_s}}}}}));
        QTRY_VERIFY(ws->projects()->isRemote(u"brulion"_s));
        QVERIFY(c->threadPlace(Path).value(u"remote"_s).toBool());
        poll(chats(1, {}));
        QTRY_VERIFY(c->agents()->computerChats()->attached(Url));
        QCOMPARE(c->send(Path, u"Dalej: dokumentacja"_s), Path);
        std::optional<FakeComputer::Pending> sent = m_computer.take(u"chats/send"_s);
        QVERIFY(sent);
        QCOMPARE(sent->body(), (QJsonObject{{u"path"_s, Path}, {u"text"_s, u"Dalej: dokumentacja"_s}}));
        // (Nothing written here: the app there writes it.)
        bool ok = false;
        QCOMPARE(chat::parse(ws->read(Path, &ok)).messages.size(), 1);
        // A second one meanwhile: in line there.
        QVERIFY(c->queue(Path, u"I testy"_s));
        std::optional<FakeComputer::Pending> second = m_computer.take(u"chats/send"_s);
        QVERIFY(second);
        QCOMPARE(second->body().value(u"mode"_s).toString(), u"queue"_s);
        second->done(FakeComputer::json(200, QJsonObject{{u"path"_s, Path}}));
        if (status == 200) {
            sent->done(FakeComputer::json(200, QJsonObject{{u"path"_s, Path}}));
            QTRY_VERIFY(m_computer.waiting(u"chats"_s, 0)); // (asked at once how it goes)
            m_computer.take(u"chats"_s, 0)->done(FakeComputer::json(200, chats(2, {row(u"running"_s, 2)})));
            QTRY_VERIFY(c->isRunning(Path));
            QCOMPARE(c->turn(Path)->remoteHost(), u"nixos"_s);
            QVERIFY(!m_computer.waiting(u"streams"_s));
        } else if (code == u"busy") {
            sent->done(FakeComputer::error(409, code, u"Its turn runs"_s));
            std::optional<FakeComputer::Pending> again = m_computer.take(u"chats/send"_s);
            QVERIFY(again);
            QCOMPARE(again->body().value(u"mode"_s).toString(), u"queue"_s);
            QCOMPARE(again->body().value(u"text"_s).toString(), u"Dalej: dokumentacja"_s);
        } else {
            // Refused there: the turn runs from here, through that computer's shared agent.
            sent->done(FakeComputer::error(404, code, u"No such conversation"_s));
            QTRY_COMPARE(chat::parse(ws->read(Path, &ok)).messages.size(), 2);
            QTRY_VERIFY(m_computer.waiting(u"streams"_s));
            QVERIFY(c->isRunning(Path));
            QCOMPARE(c->turn(Path)->remoteHost(), QString());
        }
    }

    void newThreadStartedThere()
    {
        auto ws = open(false);
        WorkspaceChats *c = ws->chats();
        QVERIFY(!c->startsThere(u"brulion"_s)); // (no such project yet)
        QTRY_VERIFY(m_computer.waiting(u"projects"_s));
        m_computer.take(u"projects"_s)
                ->done(FakeComputer::json(200, QJsonObject{{u"projects"_s, QJsonArray{QJsonObject{
                                                                               {u"id"_s, u"brulion"_s},
                                                                               {u"name"_s, u"Brulion"_s},
                                                                               {u"path"_s, u"/home/ala/brulion"_s}}}}}));
        QTRY_VERIFY(ws->projects()->isRemote(u"brulion"_s));
        QVERIFY(!c->startsThere(u"brulion"_s)); // (its app not heard from yet)
        poll(chats(1, {}));
        QTRY_VERIFY(c->startsThere(u"brulion"_s));
        QSignalSpy started(c, &WorkspaceChats::threadStartedThere);
        QCOMPARE(c->startThreadThere(u"brulion"_s, u"Zdjęcie"_s, {u"attachments/a.png"_s}), 0); // (pictures: not there yet)
        const int request = c->startThreadThere(u"brulion"_s, u"  Popraw testy żółwia  "_s, {}, u"acp:claude-nixos"_s,
                                                u"/home/ala/.brulion/worktrees/brulion/popraw"_s, u"t3/popraw"_s);
        QVERIFY(request > 0);
        std::optional<FakeComputer::Pending> start = m_computer.take(u"chats/start"_s);
        QVERIFY(start);
        // (This device's agent of that computer: its id there.)
        QCOMPARE(start->body(), (QJsonObject{{u"project"_s, u"brulion"_s},
                                              {u"text"_s, u"Popraw testy żółwia"_s},
                                              {u"partner"_s, u"acp:claude"_s},
                                              {u"worktree"_s, u"/home/ala/.brulion/worktrees/brulion/popraw"_s},
                                              {u"branch"_s, u"t3/popraw"_s}}));
        start->done(FakeComputer::json(201, QJsonObject{{u"path"_s, Path}}));
        QTRY_COMPARE(started.count(), 1);
        QCOMPARE(started.at(0).at(0).toInt(), request);
        QCOMPARE(started.at(0).at(1).toString(), Path);
        QCOMPARE(c->agents()->computerChats()->watched(), Path);
        // It runs there; its page from there (this device has none yet): where it works too.
        QTRY_VERIFY(m_computer.waiting(u"chats"_s, 0));
        acp::RemoteChat running = row(u"running"_s);
        running.worktree = u"/home/ala/.brulion/worktrees/brulion/popraw"_s;
        m_computer.take(u"chats"_s, 0)->done(FakeComputer::json(200, chats(2, {running})));
        QTRY_VERIFY(c->isRunning(Path));
        QTRY_VERIFY(m_computer.waiting(u"chats/page"_s));
        m_computer.take(u"chats/page"_s)->done(FakeComputer::json(200, QJsonObject{{u"path"_s, Path}, {u"text"_s, Page}}));
        QTRY_COMPARE(c->threadPlace(Path).value(u"project"_s).toString(), u"brulion"_s);
        QVERIFY(c->threadPlace(Path).value(u"remote"_s).toBool());
        // Refused there: said, nothing started.
        const int refused = c->startThreadThere(u"brulion"_s, u"Jeszcze jeden"_s, {});
        m_computer.take(u"chats/start"_s)->done(FakeComputer::error(400, u"bad_worktree"_s, u"Not a worktree of it"_s));
        QTRY_COMPARE(started.count(), 2);
        QCOMPARE(started.at(1).at(0).toInt(), refused);
        QCOMPARE(started.at(1).at(1).toString(), QString());
        QVERIFY(started.at(1).at(2).toString().contains(u"Not a worktree of it"_s));
    }

    void restartedComputerCountsAnew()
    {
        auto ws = open();
        WorkspaceChats *c = ws->chats();
        poll(chats(40, {}));
        // (Its revision back at 3: started again — its answer is taken.)
        poll(chats(3, {row(u"running"_s)}), 40);
        QTRY_VERIFY(c->isRunning(Path));
    }
};

QTEST_MAIN(TestComputerChats)
#include "tst_content_computerchats.moc"
