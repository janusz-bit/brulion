// Code mode's threads settled and snoozed (docs/CODE.md "Settling and snoozing") on a real Workspace
// and its notebook, a fake clock: Settle, Unsettle, Snooze (presets, words, a time that passed) and
// Wake written into the thread's page; the sidebar's list (ChatListModel) folding Settled and Snoozed
// away, their counts, a snooze that ends by the clock, a question that breaks one; a new message —
// the user's and the answer's — taking both away; threads untouched for a week settled by themselves
// (what holds one, `auto-settle: never`, the notebook's conversations left to archiving), and the
// settledChanged() signal the terminals follow.
#include "chatmodels.h"

#include "codethreads.h"
#include "workspace.h"
#include "workspacechats.h"
#include "workspaceextensions.h"

#include "codeprojects.h"

#include "ai/local.h"
#include "chat/chat.h"

#include <QDir>
#include <QProcess>
#include <QStandardPaths>
#include <QFile>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>
#include <QTimer>

using namespace Qt::StringLiterals;
using namespace brulion;
using brulion::content::ChatListModel;
using brulion::shell::CodeThreads;
using brulion::shell::Workspace;

namespace {

bool writeRaw(const QString &file, const QByteArray &bytes)
{
    QDir().mkpath(QFileInfo(file).absolutePath());
    QFile f(file);
    return f.open(QIODevice::WriteOnly) && f.write(bytes) == bytes.size();
}

QVariant value(const QAbstractItemModel &model, int row, const QByteArray &role)
{
    return model.data(model.index(row, 0), model.roleNames().key(role, -1));
}

/// The model on the device, played: "Gotowe." each time; `hold` keeps the answer until release().
struct Model : ai::LocalBackend {
    bool hold = false;
    bool fail = false;
    std::function<void()> held;
    void send(const ai::Settings &, const ai::Request &, std::function<void(const ai::Reply &)> done,
              ai::PartialCallback, std::shared_ptr<std::atomic<bool>>) override
    {
        ai::Reply reply;
        reply.ok = !fail;
        reply.text = fail ? QString() : u"Gotowe."_s;
        reply.error = fail ? u"brak sieci"_s : QString();
        reply.model = u"lfm3-1.7b"_s;
        reply.raw = "[]";
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

const QString Rabat = u"AI/2026-11-20 Rabat żółwia.md"_s;
const QString Koszyk = u"AI/2026-11-21 Koszyk.md"_s;
const QString Notatki = u"AI/2026-11-10 Notatki.md"_s;

/// git in `folder` (an identity of its own): its exit code.
int git(const QString &folder, const QStringList &args)
{
    QProcess p;
    p.start(u"git"_s, QStringList{u"-C"_s, folder, u"-c"_s, u"user.name=Ala"_s, u"-c"_s, u"user.email=ala@example.com"_s,
                                  u"-c"_s, u"init.defaultBranch=main"_s, u"-c"_s, u"commit.gpgsign=false"_s}
                              + args);
    p.waitForFinished(30000);
    return p.exitCode();
}

} // namespace

class TestContentCodeThreads : public QObject {
    Q_OBJECT

    QTemporaryDir m_tmp;
    int m_n = 0;
    QDateTime m_now{QDate(2026, 11, 28), QTime(12, 0)};
    Model m_model;

    std::unique_ptr<Workspace> open(const QHash<QString, QString> &files)
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
        ws->extensions()->setEnabled({u"asystent"_s});
        QCoreApplication::processEvents();
        return ws;
    }

    static QHash<QString, QString> threads()
    {
        return {
                {Rabat, u"---\nproject: sklep\n---\n- 🧑 Dodaj rabat\n  czas:: 2026-11-27 09:00\n- 🤖 Dodany.\n  czas:: 2026-11-27 09:01\n"_s},
                {Koszyk, u"---\nproject: sklep\n---\n- 🧑 Koszyk\n  czas:: 2026-11-27 10:00\n- 🤖 Jest.\n  czas:: 2026-11-27 10:01\n"_s},
                {Notatki, u"- 🧑 O notatkach\n  czas:: 2026-11-10 09:00\n- 🤖 Tak.\n  czas:: 2026-11-10 09:01\n"_s},
        };
    }

    static QStringList listed(const ChatListModel &list)
    {
        QStringList out;
        for (int i = 0; i < list.rowCount(); ++i)
            out << u"[%1] %2"_s.arg(value(list, i, "section").toString(), value(list, i, "title").toString());
        return out;
    }

private Q_SLOTS:
    void initTestCase() { QVERIFY(m_tmp.isValid()); }
    void init()
    {
        m_now = QDateTime(QDate(2026, 11, 28), QTime(12, 0));
        m_model = Model();
    }

    void settleAndUnsettle()
    {
        auto ws = open(threads());
        CodeThreads *threads = ws->codeThreads();
        QSignalSpy settled(threads, &CodeThreads::settledChanged);
        ChatListModel list;
        list.setWorkspace(ws.get());
        list.setProject(u"sklep"_s);
        list.refreshNow();
        QCOMPARE(list.count(), 2);

        QVERIFY(threads->settle(Rabat));
        QCOMPARE(ws->read(Rabat),
                 u"---\nproject: sklep\nsettled: 2026-11-28 12:00\n---\n- 🧑 Dodaj rabat\n  czas:: 2026-11-27 09:00\n"
                 u"- 🤖 Dodany.\n  czas:: 2026-11-27 09:01\n"_s);
        QCOMPARE(settled.size(), 1);
        QCOMPARE(settled.last(), (QVariantList{Rabat, true}));
        QCOMPARE(threads->state(Rabat).value(u"settled"_s).toString(), u"2026-11-28 12:00"_s);
        // Folded away, counted; unfolded at the bottom.
        list.refreshNow();
        QCOMPARE(listed(list), QStringList{u"[recent] Koszyk"_s});
        QCOMPARE(list.settledCount(), 1);
        QSignalSpy shown(&list, &ChatListModel::showSettledChanged);
        list.setShowSettled(true);
        QCOMPARE(shown.size(), 1);
        QCOMPARE(listed(list), (QStringList{u"[recent] Koszyk"_s, u"[settled] Rabat żółwia"_s}));
        QCOMPARE(value(list, 1, "settled").toString(), u"2026-11-28 12:00"_s);
        QCOMPARE(value(list, 0, "settled").toString(), QString());
        // Back.
        QVERIFY(threads->unsettle(Rabat));
        QCOMPARE(settled.last(), (QVariantList{Rabat, false}));
        QVERIFY(!ws->read(Rabat).contains(u"settled:"_s));
        list.refreshNow();
        QCOMPARE(list.settledCount(), 0);
        QCOMPARE(list.count(), 2);
        // Not a thread: nothing written.
        QVERIFY(!threads->settle(Notatki));
        QVERIFY(!threads->settle(u"AI/brak.md"_s));
        QVERIFY(!threads->settle(QString()));
        QVERIFY(!ws->read(Notatki).contains(u"settled"_s));
        QCOMPARE(settled.size(), 2);
    }

    void snoozeAndWake()
    {
        auto ws = open(threads());
        CodeThreads *threads = ws->codeThreads();
        QSignalSpy snoozed(threads, &CodeThreads::snoozedChanged);
        ChatListModel list;
        list.setWorkspace(ws.get());
        list.setProject(u"sklep"_s);
        QCOMPARE(threads->snooze(Koszyk, u"3h"_s), QString());
        QVERIFY(ws->read(Koszyk).contains(u"\nsnoozed: 2026-11-28 15:00\n"_s));
        QCOMPARE(snoozed.size(), 1);
        QVERIFY(threads->state(Koszyk).value(u"snoozed"_s).toBool());
        list.refreshNow();
        QCOMPARE(listed(list), QStringList{u"[recent] Rabat żółwia"_s});
        QCOMPARE(list.snoozedCount(), 1);
        list.setShowSettled(true);
        QCOMPARE(listed(list), (QStringList{u"[recent] Rabat żółwia"_s, u"[snoozed] Koszyk"_s}));
        QCOMPARE(value(list, 1, "snoozedUntil").toString(), u"2026-11-28 15:00"_s);
        list.setShowSettled(false);
        // The clock passes its end: back among the others, the line left (a snooze that ended).
        m_now = m_now.addSecs(3 * 3600 + 60);
        list.refreshNow();
        QCOMPARE(list.count(), 2);
        QCOMPARE(list.snoozedCount(), 0);
        QVERIFY(!threads->state(Koszyk).value(u"snoozed"_s).toBool());
        // Words; a time that passed is refused, nothing written.
        QCOMPARE(threads->snooze(Koszyk, u"jutro o 9"_s), QString());
        QVERIFY(ws->read(Koszyk).contains(u"snoozed: 2026-11-29 09:00"_s));
        QVERIFY(!threads->snooze(Koszyk, u"wczoraj"_s).isEmpty());
        QVERIFY(ws->read(Koszyk).contains(u"snoozed: 2026-11-29 09:00"_s));
        QVERIFY(!threads->snooze(Notatki, u"1h"_s).isEmpty());
        // Wake.
        QVERIFY(threads->wake(Koszyk));
        QVERIFY(!ws->read(Koszyk).contains(u"snoozed:"_s));
        QCOMPARE(snoozed.size(), 3);
        QVERIFY(!threads->snoozeLabel(u"evening"_s).isEmpty());
        QCOMPARE(threads->snoozeLabel(u"wczoraj"_s), QString());
        // Snoozing a settled thread: snoozed, no longer settled.
        QVERIFY(threads->settle(Rabat));
        QCOMPARE(threads->snooze(Rabat, u"tomorrow"_s), QString());
        QCOMPARE(threads->state(Rabat).value(u"settled"_s).toString(), QString());
    }

    void aNewMessageReactivates()
    {
        auto ws = open(threads());
        CodeThreads *threads = ws->codeThreads();
        shell::WorkspaceChats *chats = ws->chats();
        QVERIFY(threads->settle(Rabat));
        // The user's message.
        m_model.hold = true;
        QCOMPARE(chats->send(Rabat, u"Jeszcze jedno"_s), Rabat);
        QVERIFY(!ws->read(Rabat).contains(u"settled:"_s));
        QVERIFY(ws->read(Rabat).contains(u"Jeszcze jedno"_s));
        // The answer of a turn that runs while the user settles it.
        if (!chats->isRunning(Rabat))
            QSKIP("the thread's turn did not start here");
        QTRY_VERIFY(m_model.held != nullptr);
        QVERIFY(threads->settle(Rabat));
        QCOMPARE(threads->snooze(Koszyk, u"1h"_s), QString());
        m_model.release();
        QTRY_VERIFY(!chats->isRunning(Rabat));
        QCOMPARE(chat::parse(ws->read(Rabat)).messages.last().text, u"Gotowe."_s);
        QVERIFY(!ws->read(Rabat).contains(u"settled:"_s));
        QVERIFY(ws->read(Koszyk).contains(u"snoozed:"_s));
    }

    // Snoozed while its turn works (it goes on in the background): its failure ends the snooze.
    void aFailureBreaksTheSnooze()
    {
        auto ws = open(threads());
        ChatListModel list;
        list.setWorkspace(ws.get());
        list.setProject(u"sklep"_s);
        m_model.hold = true;
        m_model.fail = true;
        QCOMPARE(ws->chats()->send(Rabat, u"Napraw testy"_s), Rabat);
        if (!ws->chats()->isRunning(Rabat))
            QSKIP("the thread's turn did not start here");
        QTRY_VERIFY(m_model.held != nullptr);
        QCOMPARE(ws->codeThreads()->snooze(Rabat, u"tomorrow"_s), QString());
        list.refreshNow();
        QCOMPARE(list.snoozedCount(), 1);
        m_model.release();
        QTRY_VERIFY(!ws->chats()->failureOf(Rabat).isEmpty());
        list.refreshNow();
        QCOMPARE(list.snoozedCount(), 0);
        QCOMPARE(value(list, list.rowOf(Rabat), "section").toString(), u"recent"_s);
        QCOMPARE(value(list, list.rowOf(Rabat), "snoozedUntil").toString(), QString());
        // (The page still says so: the snooze is back once the failure is dealt with.)
        QVERIFY(ws->read(Rabat).contains(u"snoozed:"_s));
    }

    void settledByThemselves()
    {
        QHash<QString, QString> files = threads();
        files.insert(u"AI/2026-11-01 Stary.md"_s,
                     u"---\nproject: sklep\n---\n- 🧑 Stary\n  czas:: 2026-11-01 09:00\n- 🤖 Tak.\n  czas:: 2026-11-01 09:01\n"_s);
        files.insert(u"AI/2026-11-02 Zostaw.md"_s,
                     u"---\nproject: sklep\nauto-settle: never\n---\n- 🧑 Zostaw\n  czas:: 2026-11-02 09:00\n"_s);
        files.insert(u"AI/2026-11-03 Osiadły.md"_s,
                     u"---\nproject: sklep\nsettled: 2026-11-04 09:00\n---\n- 🧑 Osiadły\n  czas:: 2026-11-03 09:00\n"_s);
        auto ws = open(files);
        CodeThreads *threads = ws->codeThreads();
        QCOMPARE(threads->autoSettleDays(), CodeThreads::DefaultAutoSettleDays);
        // (Settled already when the notebook got ready; a second look finds nothing more.)
        QVERIFY(ws->read(u"AI/2026-11-01 Stary.md"_s).contains(u"settled: 2026-11-28 12:00"_s));
        QSignalSpy autoSettled(threads, &CodeThreads::autoSettled);
        QSignalSpy settled(threads, &CodeThreads::settledChanged);
        QCOMPARE(threads->settleUntouched(), QStringList());
        QVERIFY(autoSettled.isEmpty());
        QVERIFY(ws->codeThreads()->unsettle(u"AI/2026-11-01 Stary.md"_s));
        QCOMPARE(threads->settleUntouched(), QStringList{u"AI/2026-11-01 Stary.md"_s});
        QCOMPARE(autoSettled.size(), 1);
        QCOMPARE(autoSettled.last().at(1).toInt(), 7);
        QCOMPARE(settled.size(), 2);
        QCOMPARE(ws->read(u"AI/2026-11-03 Osiadły.md"_s), files.value(u"AI/2026-11-03 Osiadły.md"_s));
        QVERIFY(!ws->read(u"AI/2026-11-02 Zostaw.md"_s).contains(u"settled"_s));
        // The notebook's conversation is no thread: never settled, and archiving leaves threads alone.
        QVERIFY(!ws->read(Notatki).contains(u"settled"_s));
        ws->chats()->setAutoArchiveDays(7); // (archives at once)
        ws->chats()->archiveUntouched();
        QVERIFY(chat::parse(ws->read(Notatki)).archived);
        QVERIFY(!chat::parse(ws->read(Rabat)).archived);
        QVERIFY(!ws->read(u"AI/2026-11-02 Zostaw.md"_s).contains(u"zarchiwizowana"_s));
        // A week later the others; held back by a draft.
        m_now = m_now.addDays(8);
        ws->chats()->setDraft(Koszyk, u"Szkic"_s);
        QCOMPARE(threads->settleUntouched(), QStringList{Rabat});
        // Off: nothing.
        QSignalSpy days(threads, &CodeThreads::autoSettleDaysChanged);
        threads->setAutoSettleDays(0);
        threads->setAutoSettleDays(-3);
        QCOMPARE(days.size(), 1);
        ws->chats()->setDraft(Koszyk, QString());
        QCOMPARE(threads->settleUntouched(), QStringList());
        // "Never" set and taken off from the app.
        QVERIFY(threads->setNeverAutoSettle(Koszyk, true));
        QVERIFY(threads->state(Koszyk).value(u"never"_s).toBool());
        QVERIFY(threads->setNeverAutoSettle(Koszyk, false));
        QVERIFY(!ws->read(Koszyk).contains(u"auto-settle"_s));
    }

    /// A repository with a worktree on branch brulion/rabat, a project of `ws`, and a thread of it
    /// (`extra` more frontmatter lines); gh played by a script that says the branch's pull request is
    /// `prState` ("" none). The thread's page path.
    QString threadWithWorktree(Workspace *ws, const QString &prState, const QString &extra, QString *folder)
    {
        const QString repo = m_tmp.filePath(u"repo-%1"_s.arg(++m_n));
        QDir().mkpath(repo);
        if (git(repo, {u"init"_s, u"-q"_s}) != 0)
            return {};
        writeRaw(repo + u"/a.txt"_s, "a\n");
        git(repo, {u"add"_s, u"-A"_s});
        git(repo, {u"commit"_s, u"-qm"_s, u"a"_s});
        *folder = m_tmp.filePath(u"drzewa-%1/rabat"_s.arg(m_n));
        git(repo, {u"worktree"_s, u"add"_s, u"-q"_s, u"-b"_s, u"brulion/rabat"_s, *folder});
        const QString gh = m_tmp.filePath(u"gh-%1"_s.arg(m_n));
        writeRaw(gh, prState.isEmpty() ? QByteArray("#!/bin/sh\necho 'no pull requests found' >&2\nexit 1\n")
                                       : "#!/bin/sh\necho '{\"number\":7,\"url\":\"https://github.com/ala/sklep/pull/7\","
                                         "\"state\":\"" + prState.toUtf8() + "\",\"isDraft\":false,\"title\":\"Rabat\","
                                         "\"baseRefName\":\"main\"}'\n");
        QFile(gh).setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner);
        ws->projects()->setPrograms(QStandardPaths::findExecutable(u"git"_s), gh);
        const QString id = ws->projects()->add(repo);
        const QString path = u"AI/2026-11-20 Rabat w drzewie %1.md"_s.arg(m_n);
        ws->write(path, u"---\nproject: %1\nworktree: \"%2\"\nbranch: brulion/rabat\n%3---\n- 🧑 Rabat\n"
                        u"  czas:: 2026-11-27 09:00\n- 🤖 Jest.\n  czas:: 2026-11-27 09:01\n"_s.arg(id, *folder, extra));
        return path;
    }

    // Its pull request merged: the thread settles, and its worktree (clean) goes — its branch stays;
    // a worktree with an uncommitted file stays.
    void mergedPullRequestTidies()
    {
#ifdef Q_OS_WIN
        QSKIP("the fake gh is a shell script");
#endif
        if (QStandardPaths::findExecutable(u"git"_s).isEmpty())
            QSKIP("no git");
        auto ws = open(threads());
        CodeThreads *threads = ws->codeThreads();
        QString folder;
        const QString path = threadWithWorktree(ws.get(), u"MERGED"_s, QString(), &folder);
        QVERIFY(QFileInfo(folder).isDir());
        QSignalSpy tidied(threads, &CodeThreads::tidied);
        QSignalSpy cleaned(threads, &CodeThreads::worktreeCleaned);
        QSignalSpy autoSettled(threads, &CodeThreads::autoSettled);
        // Off: nothing removed, but it settles.
        threads->setWorktreeCleanup(u"never"_s);
        QCOMPARE(threads->tidy(), 1);
        QTRY_COMPARE_WITH_TIMEOUT(tidied.size(), 1, 30000);
        QVERIFY(ws->read(path).contains(u"settled: 2026-11-28 12:00"_s));
        QCOMPARE(autoSettled.size(), 1);
        QCOMPARE(autoSettled.last().at(1).toInt(), 0);
        QVERIFY(QFileInfo(folder).isDir());
        // An uncommitted file: kept.
        threads->setWorktreeCleanup(u"merged"_s);
        QCOMPARE(threads->worktreeCleanup(), u"merged"_s);
        writeRaw(folder + u"/szkic.txt"_s, "żółw\n");
        QCOMPARE(threads->tidy(), 1);
        QTRY_COMPARE_WITH_TIMEOUT(tidied.size(), 2, 30000);
        QVERIFY(QFileInfo(folder).isDir());
        QVERIFY(cleaned.isEmpty());
        // Clean: removed, the branch kept.
        QVERIFY(QFile::remove(folder + u"/szkic.txt"_s));
        QCOMPARE(threads->tidy(), 1);
        QTRY_COMPARE_WITH_TIMEOUT(tidied.size(), 3, 30000);
        QCOMPARE(cleaned.size(), 1);
        QCOMPARE(cleaned.last(), (QVariantList{path, folder}));
        QVERIFY(!QFileInfo::exists(folder));
        QCOMPARE(autoSettled.size(), 1);
    }

    // No pull request: a thread settled a week ago loses its worktree with "7"; one settled
    // yesterday keeps it; another active thread in the same worktree keeps it too.
    void settledLongAgoTidies()
    {
#ifdef Q_OS_WIN
        QSKIP("the fake gh is a shell script");
#endif
        if (QStandardPaths::findExecutable(u"git"_s).isEmpty())
            QSKIP("no git");
        auto ws = open(threads());
        CodeThreads *threads = ws->codeThreads();
        threads->setWorktreeCleanup(u"7"_s);
        QSignalSpy tidied(threads, &CodeThreads::tidied);
        QString folder;
        const QString path = threadWithWorktree(ws.get(), QString(), u"settled: 2026-11-27 10:00\n"_s, &folder);
        QCOMPARE(threads->tidy(), 1);
        QTRY_COMPARE_WITH_TIMEOUT(tidied.size(), 1, 30000);
        QVERIFY(QFileInfo(folder).isDir());
        // A week on, with another thread there that is not settled.
        m_now = m_now.addDays(7);
        const QString other = u"AI/2026-11-21 Drugi w drzewie.md"_s;
        ws->write(other, QString(ws->read(path)).replace(u"settled: 2026-11-27 10:00\n"_s, QString()));
        QCOMPARE(threads->tidy(), 2);
        QTRY_COMPARE_WITH_TIMEOUT(tidied.size(), 2, 30000);
        QVERIFY(QFileInfo(folder).isDir());
        // That one settled too: gone.
        QVERIFY(threads->settle(other));
        threads->setAutoSettleDays(0);
        QCOMPARE(threads->tidy(), 2);
        QTRY_COMPARE_WITH_TIMEOUT(tidied.size(), 3, 30000);
        QTRY_VERIFY(!QFileInfo::exists(folder));
        // Nothing to do when both are off.
        threads->setWorktreeCleanup(u"never"_s);
        QCOMPARE(threads->tidy(), 0);
        threads->setWorktreeCleanup(u"30"_s);
        QCOMPARE(threads->worktreeCleanup(), u"30"_s);
        threads->setWorktreeCleanup(u"bzdura"_s);
        QCOMPARE(threads->worktreeCleanup(), u"never"_s);
    }
};

QTEST_MAIN(TestContentCodeThreads)
#include "tst_content_codethreads.moc"
