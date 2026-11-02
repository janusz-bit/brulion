// Workspace.backups on a real Workspace and folders (shell/workspacebackups.h): the schedule (first
// backup, nothing changed, a clock set back, weekly), pruning only what this notebook wrote, an
// unplugged folder as a status and one message with older backups kept, the folder refused inside the
// notebook, a restore from plan to safety backup to files, a failed safety backup aborting it, a
// hostile file in the folder, the browser's rules (nothing pruned until IndexedDB has the copy, a
// refused copy removed and the older ones kept) and the AppController's commands. ctest runs it on
// worker threads and, as _inline, in slices on the GUI thread (the browser's way).
#include "appcontroller.h"
#include "commandmodel.h"
#include "workspace.h"
#include "workspacebackups.h"
#include "workspacesync.h"

#include "filesystemtraits.h"
#include "vault/vault.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>
#include <QUrl>

#include <atomic>

using namespace Qt::StringLiterals;
using brulion::shell::AppController;
using brulion::shell::Workspace;
using brulion::shell::WorkspaceBackups;

namespace {

bool writeRaw(const QString &file, const QByteArray &bytes)
{
    QDir().mkpath(QFileInfo(file).absolutePath());
    QFile f(file);
    return f.open(QIODevice::WriteOnly) && f.write(bytes) == bytes.size();
}

QByteArray readRaw(const QString &file)
{
    QFile f(file);
    return f.open(QIODevice::ReadOnly) ? f.readAll() : QByteArray();
}

QStringList zipsIn(const QString &folder)
{
    return QDir(folder).entryList({u"*.zip"_s}, QDir::Files, QDir::Name);
}

} // namespace

class TestWorkspaceBackups : public QObject {
    Q_OBJECT

    QTemporaryDir m_tmp;
    int m_n = 0;
    QDateTime m_now{QDate(2026, 10, 14), QTime(10, 30)};

    QString newRoot() { return m_tmp.filePath(u"Żółw-%1"_s.arg(++m_n)); }

    /// A notebook ready, its first scheduled check not yet run (the tests call check()).
    std::unique_ptr<Workspace> openAt(const QString &root)
    {
        auto ws = std::make_unique<Workspace>();
        ws->setClock([this] { return m_now; });
        ws->sync()->setSyncthingDiscovery(false);
        ws->backups()->setCheckTimings(24 * 3600 * 1000, 24 * 3600 * 1000);
        QString error;
        if (!ws->open(root, root + u"-state"_s, &error))
            qWarning() << error;
        ws->finishIndexing();
        return ws;
    }

    /// Run a backup job to its end.
    bool settled(WorkspaceBackups *backups)
    {
        QTest::qWait(1); // queued folder work
        return QTest::qWaitFor([&] { return !backups->running(); }, 30000);
    }

    bool createNow(WorkspaceBackups *backups)
    {
        QSignalSpy written(backups, &WorkspaceBackups::backupWritten);
        if (!backups->createNow())
            return false;
        return settled(backups) && written.size() == 1
               && QTest::qWaitFor([&] { return !backups->backups().isEmpty(); }, 5000);
    }

    /// check(), and what it wrote: the new file's name or "".
    QString checked(WorkspaceBackups *backups)
    {
        QSignalSpy written(backups, &WorkspaceBackups::backupWritten);
        QSignalSpy idle(backups, &WorkspaceBackups::idle);
        backups->check();
        if (!QTest::qWaitFor([&] { return !idle.isEmpty(); }, 30000))
            qWarning("the check did not end");
        return written.isEmpty() ? QString() : written.first().first().toString();
    }

    QVariantMap plannedRestore(WorkspaceBackups *backups, const QString &name)
    {
        QSignalSpy planned(backups, &WorkspaceBackups::restorePlanReady);
        if (!backups->prepareRestore(name) || !planned.wait(30000))
            return {};
        return planned.first().first().toMap();
    }

private Q_SLOTS:
    void initTestCase() { QVERIFY(m_tmp.isValid()); }
    void init() { m_now = QDateTime(QDate(2026, 10, 14), QTime(10, 30)); }

    void scheduleOverDays()
    {
        const QString root = newRoot();
        writeRaw(root + u"/Notatka ąć.md"_s, "- a\n");
        auto ws = openAt(root);
        WorkspaceBackups *b = ws->backups();
        QVERIFY(b->available());
        QVERIFY(b->enabled()); // on by default where the device has its own folder
        QCOMPARE(b->interval(), u"daily"_s);
        QCOMPARE(b->keep(), 7);
        QCOMPARE(b->status(), u"none"_s);
        QVERIFY(b->folder().startsWith(root + u"-state"_s));
        QVERIFY(!b->folder().startsWith(root + u'/'));

        // First check: a backup, named after the notebook, by the injected clock.
        const QString first = checked(b);
        QCOMPARE(first, u"Brulion-%1-2026-10-14-103000.zip"_s.arg(QFileInfo(root).fileName()));
        QCOMPARE(zipsIn(b->folder()), QStringList{first});
        QCOMPARE(b->status(), u"ok"_s);
        QVERIFY(!b->lastBackupText().isEmpty());
        QTRY_COMPARE(b->backups().size(), 1);

        // The same day, also after an edit: not due.
        QVERIFY(ws->writeFile(u"Notatka ąć.md"_s, u"- b\n"_s));
        QCOMPARE(checked(b), QString());
        // The next day: due, and the edit is not in a backup yet.
        m_now = m_now.addDays(1);
        QVERIFY(!checked(b).isEmpty());
        // The day after, nothing changed: nothing written — however often it checks.
        m_now = m_now.addDays(1);
        QCOMPARE(checked(b), QString());
        QCOMPARE(checked(b), QString());
        QCOMPARE(zipsIn(b->folder()).size(), 2);
        // A change is backed up at the next check of that day, not only tomorrow.
        QVERIFY(ws->writeFile(u"Nowa.md"_s, u"- nowa\n"_s));
        QVERIFY(!checked(b).isEmpty());
        // Unchanged, but the last file is gone (deleted, another disk): written again.
        m_now = m_now.addDays(1);
        const QStringList before = zipsIn(b->folder());
        QVERIFY(QFile::remove(QDir(b->folder()).filePath(before.last())));
        QVERIFY(!checked(b).isEmpty());

        // Weekly: six days are not enough, seven are.
        b->setInterval(u"weekly"_s);
        QVERIFY(ws->writeFile(u"Nowa.md"_s, u"- zmiana\n"_s));
        m_now = m_now.addDays(6);
        QCOMPARE(checked(b), QString());
        m_now = m_now.addDays(1);
        QVERIFY(!checked(b).isEmpty());
        // Off: never.
        b->setEnabled(false);
        QVERIFY(ws->writeFile(u"Nowa.md"_s, u"- znów\n"_s));
        m_now = m_now.addDays(30);
        QCOMPARE(checked(b), QString());
        // The settings stay with the notebook on this device.
        ws.reset();
        ws = openAt(root);
        QVERIFY(!ws->backups()->enabled());
        QCOMPARE(ws->backups()->interval(), u"weekly"_s);
        // Never in the vault.
        QVERIFY(!QFileInfo::exists(root + u"/backups"_s));
    }

    void aClockSetBackDoesNotWedgeIt()
    {
        const QString root = newRoot();
        writeRaw(root + u"/a.md"_s, "- a\n");
        auto ws = openAt(root);
        WorkspaceBackups *b = ws->backups();
        const QString future = checked(b);
        QVERIFY(!future.isEmpty());
        m_now = m_now.addYears(-2);
        QVERIFY(ws->writeFile(u"a.md"_s, u"- b\n"_s));
        const QString past = checked(b);
        QVERIFY2(!past.isEmpty(), "a last backup in the future is due");
        // The newest by the clock is older by its name: it survives pruning all the same.
        b->setKeep(1);
        QVERIFY(ws->writeFile(u"a.md"_s, u"- c\n"_s));
        m_now = m_now.addDays(1);
        const QString next = checked(b);
        QVERIFY(!next.isEmpty());
        QTRY_VERIFY(zipsIn(b->folder()).contains(next));
    }

    void anEmptyNotebookIsNotBackedUpBySchedule()
    {
        const QString root = newRoot();
        QDir().mkpath(root);
        auto ws = openAt(root);
        QCOMPARE(checked(ws->backups()), QString());
        QVERIFY(!ws->backups()->createNow());
    }

    void pruningTakesOnlyItsOwn()
    {
        const QString root = newRoot();
        writeRaw(root + u"/a.md"_s, "- a\n");
        const QString chosen = m_tmp.filePath(u"pendrive"_s);
        const QString part = QFileInfo(root).fileName();
        const QString foreignTwin = chosen + u"/Brulion-%1-2020-01-01-120000.zip"_s.arg(part); // no manifest
        const QStringList foreign{foreignTwin, chosen + u"/Brulion-Inny-2020-01-01-120000.zip"_s,
                                  chosen + u"/zdjęcia.zip"_s, chosen + u"/notatka.md"_s};
        for (const QString &file : foreign)
            writeRaw(file, "cudze");
        auto ws = openAt(root);
        WorkspaceBackups *b = ws->backups();
        b->chooseFolder(QUrl::fromLocalFile(chosen));
        QTRY_COMPARE(b->folder(), chosen);
        QVERIFY(!b->usingDefaultFolder());
        b->setKeep(2);
        for (int i = 0; i < 4; ++i) {
            QVERIFY(ws->writeFile(u"a.md"_s, u"- wersja %1\n"_s.arg(i)));
            QVERIFY(!checked(b).isEmpty());
            m_now = m_now.addDays(1);
        }
        QTRY_COMPARE(zipsIn(chosen).size(), 2 + 3); // two of its own, three foreign .zip
        for (const QString &file : foreign)
            QCOMPARE(readRaw(file), QByteArray("cudze"));
        // Only names of this notebook can be deleted through the UI's call.
        QVERIFY(!b->remove(u"zdjęcia.zip"_s));
        QVERIFY(!b->remove(u"../a.md"_s));
        QVERIFY(QFileInfo::exists(chosen + u"/zdjęcia.zip"_s));
        // Older backups stay in a folder given up, and the default one takes over.
        b->useDefaultFolder();
        QVERIFY(b->usingDefaultFolder());
        QCOMPARE(zipsIn(chosen).size(), 5);
    }

    void folderChoices()
    {
        const QString root = newRoot();
        writeRaw(root + u"/a.md"_s, "- a\n");
        auto ws = openAt(root);
        WorkspaceBackups *b = ws->backups();
        const QString defaultFolder = b->folder();
        QSignalSpy messages(b, &WorkspaceBackups::messageRequested);

        // Inside the notebook: refused, the folder in use stays.
        QDir().mkpath(root + u"/Kopie"_s);
        b->chooseFolder(QUrl::fromLocalFile(root + u"/Kopie"_s));
        QCOMPARE(messages.size(), 1);
        QCOMPARE(messages.last().at(1).toString(), u"error"_s);
        b->chooseFolder(QUrl::fromLocalFile(root));
        QCOMPARE(messages.size(), 2);
        QCOMPARE(b->folder(), defaultFolder);

        // A document the system's picker gives that names no file (a cloud drive).
        b->chooseFolder(QUrl(u"content://com.google.android.apps.docs.storage/tree/acc%3D1"_s));
        QCOMPARE(messages.size(), 3);
        QCOMPARE(b->folder(), defaultFolder);
        b->chooseFolder(QUrl(u"https://example.com/kopie"_s));
        QCOMPARE(messages.size(), 4);
        QCOMPARE(b->folder(), defaultFolder);

        // Missing folder: not taken.
        b->chooseFolder(QUrl::fromLocalFile(m_tmp.filePath(u"nie ma"_s)));
        QTRY_COMPARE(messages.size(), 5);
        QCOMPARE(b->folder(), defaultFolder);
        QVERIFY(!QFileInfo::exists(m_tmp.filePath(u"nie ma"_s)));
    }

    void anUnpluggedFolderIsAStatusNotALoss()
    {
        const QString root = newRoot();
        writeRaw(root + u"/a.md"_s, "- a\n");
        const QString drive = m_tmp.filePath(u"dysk-%1"_s.arg(m_n));
        QDir().mkpath(drive);
        auto ws = openAt(root);
        WorkspaceBackups *b = ws->backups();
        b->chooseFolder(QUrl::fromLocalFile(drive));
        QTRY_COMPARE(b->folder(), drive);
        QVERIFY(!checked(b).isEmpty());
        const QString older = zipsIn(drive).first();
        const QByteArray olderBytes = readRaw(drive + u'/' + older);

        // Unplugged: the mount point is gone.
        const QString away = drive + u"-odpięty"_s;
        QVERIFY(QDir().rename(drive, away));
        QVERIFY(ws->writeFile(u"a.md"_s, u"- b\n"_s));
        m_now = m_now.addDays(1);
        QSignalSpy messages(b, &WorkspaceBackups::messageRequested);
        QCOMPARE(checked(b), QString());
        QCOMPARE(b->status(), u"failed"_s);
        QVERIFY(b->statusText().contains(QDir::toNativeSeparators(drive)));
        QCOMPARE(messages.size(), 1);
        QCOMPARE(messages.first().at(1).toString(), u"warning"_s);
        QVERIFY2(!QFileInfo::exists(drive), "a chosen folder is never made: it would land on the empty mount point");
        // Each hourly check says so in the status, not in another toast.
        QCOMPARE(checked(b), QString());
        QCOMPARE(messages.size(), 1);

        // Back again: the next check makes the backup, the older one is untouched.
        QVERIFY(QDir().rename(away, drive));
        QVERIFY(!checked(b).isEmpty());
        QCOMPARE(b->status(), u"ok"_s);
        QCOMPARE(b->statusText(), QString());
        QCOMPARE(readRaw(drive + u'/' + older), olderBytes);
        QCOMPARE(zipsIn(drive).size(), 2);
    }

    void restoreFromPlanToFiles()
    {
        const QString root = newRoot();
        const QByteArray crlf = "- zażółć\r\n- gęślą\r\n";
        const QByteArray binary("\x89PNG\0\0\1\2", 8);
        writeRaw(root + u"/Żółw.md"_s, crlf);
        writeRaw(root + u"/assets/a.png"_s, binary);
        writeRaw(root + u"/Bez zmian.md"_s, "- ta sama\n");
        auto ws = openAt(root);
        WorkspaceBackups *b = ws->backups();
        b->setKeep(1); // the safety backup must not push out the backup being restored
        QVERIFY(createNow(b));
        const QString source = b->backups().first().toMap().value(u"name"_s).toString();

        QVERIFY(ws->writeFile(u"Żółw.md"_s, u"- po kopii\n"_s));
        ws->deletePage(u"assets/a.png"_s);
        QVERIFY(ws->writeFile(u"Późniejsza.md"_s, u"- nowa\n"_s));
        m_now = m_now.addSecs(120);

        const QVariantMap plan = plannedRestore(b, source);
        QCOMPARE(plan.value(u"name"_s).toString(), source);
        QCOMPARE(plan.value(u"changed"_s).toInt(), 1);
        QCOMPARE(plan.value(u"recreated"_s).toInt(), 1);
        QCOMPARE(plan.value(u"trashed"_s).toInt(), 1);
        QCOMPARE(plan.value(u"identical"_s).toInt(), 1);
        QVERIFY(plan.value(u"hasChanges"_s).toBool());
        // Only a plan so far.
        QVERIFY(QFileInfo::exists(root + u"/Późniejsza.md"_s));

        QSignalSpy messages(b, &WorkspaceBackups::messageRequested);
        QSignalSpy files(ws.get(), &Workspace::filesChanged);
        QSignalSpy written(b, &WorkspaceBackups::backupWritten);
        b->confirmRestore();
        QVERIFY(b->running());
        QTRY_VERIFY_WITH_TIMEOUT(!b->running(), 30000);

        QCOMPARE(readRaw(root + u"/Żółw.md"_s), crlf);
        QCOMPARE(readRaw(root + u"/assets/a.png"_s), binary);
        QVERIFY(!QFileInfo::exists(root + u"/Późniejsza.md"_s));
        QVERIFY(ws->index() && !files.isEmpty());
        QCOMPARE(messages.last().at(1).toString(), u"success"_s);

        // Reversible three ways: the safety backup (the restored one kept beside it), History, trash.
        QCOMPARE(written.size(), 1);
        const QString safety = written.first().first().toString();
        QVERIFY(safety != source);
        QTRY_COMPARE(zipsIn(b->folder()).size(), 2);
        QVERIFY(zipsIn(b->folder()).contains(source));
        QVERIFY(zipsIn(b->folder()).contains(safety));
        const QVariantList versions = ws->history(u"Żółw.md"_s);
        QVERIFY(!versions.isEmpty());
        QCOMPARE(ws->historyText(versions.first().toMap().value(u"id"_s).toString()), u"- po kopii\n"_s);
        QVERIFY(ws->trashCount() >= 1);

        // The restored state is what a further backup holds; restoring again changes nothing.
        const QVariantMap again = plannedRestore(b, source);
        QVERIFY(!again.value(u"hasChanges"_s).toBool());
        b->dismissRestore();
    }

    void aFailedSafetyBackupAbortsTheRestore()
    {
        if (!brulion::test::hasUnixPermissions())
            QSKIP("no Unix file modes here");
        const QString root = newRoot();
        writeRaw(root + u"/a.md"_s, "- z kopii\n");
        const QString folder = m_tmp.filePath(u"tylko-do-odczytu-%1"_s.arg(m_n));
        QDir().mkpath(folder);
        auto ws = openAt(root);
        WorkspaceBackups *b = ws->backups();
        b->chooseFolder(QUrl::fromLocalFile(folder));
        QTRY_COMPARE(b->folder(), folder);
        QVERIFY(createNow(b));
        const QString source = b->backups().first().toMap().value(u"name"_s).toString();
        QVERIFY(ws->writeFile(u"a.md"_s, u"- po kopii\n"_s));
        QVERIFY(!plannedRestore(b, source).isEmpty());

        QFile::setPermissions(folder, QFileDevice::ReadOwner | QFileDevice::ExeOwner);
        if (QFileInfo(folder).isWritable())
            QSKIP("this user can write everywhere");
        QSignalSpy messages(b, &WorkspaceBackups::messageRequested);
        b->confirmRestore();
        QTRY_VERIFY_WITH_TIMEOUT(!b->running(), 30000);
        QFile::setPermissions(folder, QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner);
        QCOMPARE(messages.size(), 1);
        QCOMPARE(messages.first().at(1).toString(), u"error"_s);
        QCOMPARE(readRaw(root + u"/a.md"_s), QByteArray("- po kopii\n"));
    }

    void aHostileFileInTheFolderIsRefused()
    {
        const QString root = newRoot();
        writeRaw(root + u"/a.md"_s, "- moja\n");
        auto ws = openAt(root);
        WorkspaceBackups *b = ws->backups();
        QVERIFY(createNow(b));
        const QString name = u"Brulion-%1-2020-01-01-120000.zip"_s.arg(QFileInfo(root).fileName());
        writeRaw(QDir(b->folder()).filePath(name), "to nie jest archiwum");
        b->refresh();
        QTRY_COMPARE(b->backups().size(), 2);
        QSignalSpy planned(b, &WorkspaceBackups::restorePlanReady);
        QSignalSpy messages(b, &WorkspaceBackups::messageRequested);
        // It has no manifest: not this notebook's, so it takes the explicit confirmation.
        QVERIFY(!b->prepareRestore(name));
        QVERIFY(b->prepareRestore(name, true));
        QTRY_COMPARE(messages.size(), 1);
        QCOMPARE(messages.first().at(1).toString(), u"error"_s);
        QVERIFY(planned.isEmpty());
        b->confirmRestore(); // no plan: nothing happens
        QVERIFY(!b->running());
        QCOMPARE(readRaw(root + u"/a.md"_s), QByteArray("- moja\n"));
        // A name that is not this notebook's is not even looked at.
        QVERIFY(!b->prepareRestore(u"../inny.zip"_s));
    }

    void browserRules()
    {
        const QString root = newRoot();
        writeRaw(root + u"/a.md"_s, "- a\n");
        auto ws = std::make_unique<Workspace>();
        ws->setClock([this] { return m_now; });
        ws->sync()->setSyncthingDiscovery(false);
        WorkspaceBackups *b = ws->backups();
        b->setBrowser(true);
        b->setDeferUntilStored(true);
        QVERIFY(ws->open(root, root + u"-state"_s));
        ws->finishIndexing();

        // Off by default there, few kept, no folder to choose.
        QVERIFY(!b->enabled());
        QCOMPARE(b->keep(), 2);
        QCOMPARE(b->keepChoices().size(), 3);
        const QString folder = b->folder();
        b->chooseFolder(QUrl::fromLocalFile(m_tmp.path()));
        QTest::qWait(10);
        QCOMPARE(b->folder(), folder);

        // Three copies, limit two: the oldest stays until IndexedDB has the new one.
        b->setKeep(2);
        for (int i = 0; i < 3; ++i) {
            QVERIFY(ws->writeFile(u"a.md"_s, u"- %1\n"_s.arg(i)));
            QVERIFY(b->createNow());
            QVERIFY(settled(b));
            if (i < 2)
                b->storageFlushed();
            m_now = m_now.addSecs(60);
        }
        QTRY_COMPARE(zipsIn(folder).size(), 3);
        b->storageFlushed();
        QTRY_COMPARE(zipsIn(folder).size(), 2);
        const QStringList kept = zipsIn(folder);

        // The quota refuses the next: only it goes, the status says so, nothing older is pruned.
        QVERIFY(ws->writeFile(u"a.md"_s, u"- za duża\n"_s));
        QSignalSpy messages(b, &WorkspaceBackups::messageRequested);
        QVERIFY(b->createNow());
        QVERIFY(settled(b));
        QCOMPARE(zipsIn(folder).size(), 3);
        QSignalSpy listed(b, &WorkspaceBackups::backupsChanged);
        b->storageRefused(u"QuotaExceededError"_s);
        QCOMPARE(zipsIn(folder), kept);
        QCOMPARE(b->status(), u"failed"_s);
        QTRY_COMPARE(b->backups().size(), 2);

        // The refused copy leaves no trace in the schedule: the last one counts, the next is due tomorrow.
        QVERIFY(ws->writeFile(u"a.md"_s, u"- mniejsza\n"_s));
        m_now = m_now.addDays(1);
        QSignalSpy written(b, &WorkspaceBackups::backupWritten);
        b->setEnabled(true); // checks at once
        QVERIFY(written.wait(30000));
        QVERIFY(settled(b));
        b->storageFlushed();
        QTRY_COMPARE(zipsIn(folder).size(), 2);
        QCOMPARE(b->status(), u"ok"_s);

        // A restore waits for its safety backup to be stored; refused, nothing is restored.
        const QString source = b->backups().first().toMap().value(u"name"_s).toString();
        QVERIFY(ws->writeFile(u"a.md"_s, u"- przed przywróceniem\n"_s));
        QVERIFY(!plannedRestore(b, source).isEmpty());
        b->confirmRestore();
        QVERIFY(QTest::qWaitFor([&] { return zipsIn(folder).size() == 3; }, 30000));
        QVERIFY(b->running());
        b->storageRefused(u"QuotaExceededError"_s);
        QVERIFY(!b->running());
        QCOMPARE(ws->readFile(u"a.md"_s), u"- przed przywróceniem\n"_s);
        QCOMPARE(zipsIn(folder).size(), 2);

        // Stored: it goes on.
        QVERIFY(!plannedRestore(b, source).isEmpty());
        b->confirmRestore();
        QVERIFY(QTest::qWaitFor([&] { return zipsIn(folder).size() == 3; }, 30000));
        b->storageFlushed();
        QTRY_VERIFY_WITH_TIMEOUT(!b->running(), 30000);
        QCOMPARE(ws->readFile(u"a.md"_s), u"- mniejsza\n"_s);
    }

    void closingWhileItRunsIsSafe()
    {
        const QString root = newRoot();
        for (int i = 0; i < 30; ++i)
            writeRaw(root + u"/Notatka %1.md"_s.arg(i), QByteArray(50'000, 'x'));
        auto ws = openAt(root);
        WorkspaceBackups *b = ws->backups();
        QVERIFY(b->createNow());
        QVERIFY(b->running());
        const QString folder = b->folder();
        ws->close();
        QVERIFY(!b->running());
        QVERIFY(!b->available());
        ws.reset();
        QTest::qWait(50);
        // A finished copy or none; never a half-written one under that name.
        for (const QString &name : zipsIn(folder))
            QVERIFY(QFileInfo(QDir(folder).filePath(name)).size() > 0);
    }

    void aRunTheStorageWentUnderIsNoBackup()
    {
        // The drive is pulled mid-run: what remains vanishes. The only good copy (keep 1) stays and
        // the schedule does not think the fragment stands for the notebook.
        const QString root = newRoot();
        for (const QString &name : {u"a.md"_s, u"b.md"_s, u"c.md"_s})
            writeRaw(root + u'/' + name, "- " + name.toUtf8() + "\n");
        const QString drive = m_tmp.filePath(u"kopie-%1"_s.arg(m_n));
        QDir().mkpath(drive);
        auto ws = openAt(root);
        WorkspaceBackups *b = ws->backups();
        b->chooseFolder(QUrl::fromLocalFile(drive));
        QTRY_COMPARE(b->folder(), drive);
        b->setKeep(1);
        QVERIFY(createNow(b));
        const QStringList good = zipsIn(drive);
        QCOMPARE(good.size(), 1);

        QVERIFY(ws->writeFile(u"a.md"_s, u"- zmiana\n"_s));
        m_now = m_now.addDays(1);
        const QString away = root + u"-odpięty"_s;
        std::atomic_bool pulled = false;
        // a.md is in the archive already, the rest goes with the drive.
        b->setReadHook([&](const QString &path, qint64) {
            if (path == u"b.md" && !pulled.exchange(true))
                QDir().rename(root, away);
        });
        QSignalSpy written(b, &WorkspaceBackups::backupWritten);
        QSignalSpy messages(b, &WorkspaceBackups::messageRequested);
        QVERIFY(b->createNow());
        QVERIFY(settled(b));
        b->setReadHook({});
        QVERIFY(pulled);
        QVERIFY(written.isEmpty());
        QCOMPARE(zipsIn(drive), good);
        QCOMPARE(b->status(), u"failed"_s);
        QCOMPARE(messages.size(), 1);
        QVERIFY(QDir().rename(away, root));
        // Back: the next run is a real one, not "unchanged".
        ws->vault()->rescan();
        QVERIFY(!checked(b).isEmpty());
    }

    void anIncompleteCopyNeitherPrunesNorCountsAsCurrent()
    {
        if (!brulion::test::hasUnixPermissions())
            QSKIP("no Unix file modes here");
        const QString root = newRoot();
        writeRaw(root + u"/a.md"_s, "- a\n");
        writeRaw(root + u"/tajne.md"_s, "- tajne\n");
        auto ws = openAt(root);
        WorkspaceBackups *b = ws->backups();
        b->setKeep(1);
        const QString good = checked(b);
        QVERIFY(!good.isEmpty());
        QTRY_COMPARE(b->backups().size(), 1);
        QVERIFY(b->backups().first().toMap().value(u"complete"_s).toBool());

        QFile::setPermissions(root + u"/tajne.md"_s, QFileDevice::Permissions{});
        if (QFile probe(root + u"/tajne.md"_s); probe.open(QIODevice::ReadOnly))
            QSKIP("this user can read everything");
        QVERIFY(ws->writeFile(u"a.md"_s, u"- b\n"_s));
        m_now = m_now.addDays(1);
        QSignalSpy messages(b, &WorkspaceBackups::messageRequested);
        const QString partial = checked(b);
        QVERIFY(!partial.isEmpty());
        QTRY_COMPARE(b->backups().size(), 2);
        QVERIFY(zipsIn(b->folder()).contains(good)); // keep 1: the good copy stays all the same
        QCOMPARE(b->status(), u"warning"_s);
        QCOMPARE(messages.size(), 1);
        QCOMPARE(messages.first().at(1).toString(), u"warning"_s);

        // Nothing changed since, but it left a file out: the next day tries again, and the older
        // incomplete copy makes way for the new one.
        m_now = m_now.addDays(1);
        const QString again = checked(b);
        QVERIFY(!again.isEmpty());
        QTRY_VERIFY(!zipsIn(b->folder()).contains(partial));
        QVERIFY(zipsIn(b->folder()).contains(good));

        // Readable again: a complete copy, and only now the older complete one goes.
        QFile::setPermissions(root + u"/tajne.md"_s, QFileDevice::ReadOwner | QFileDevice::WriteOwner);
        m_now = m_now.addDays(1);
        const QString complete = checked(b);
        QVERIFY(!complete.isEmpty());
        QCOMPARE(b->status(), u"ok"_s);
        QTRY_VERIFY(!zipsIn(b->folder()).contains(good));
    }

    void onlyTheSettingsLeftIsAnEmptyNotebook()
    {
        const QString root = newRoot();
        writeRaw(root + u"/.brulion/config.json"_s, "{}");
        auto ws = openAt(root);
        QCOMPARE(checked(ws->backups()), QString());
        QVERIFY(!ws->backups()->createNow());
        QVERIFY(zipsIn(ws->backups()->folder()).isEmpty());
    }

    void aSafetyBackupMissingAFileThePlanTouchesAborts()
    {
        if (!brulion::test::hasUnixPermissions())
            QSKIP("no Unix file modes here");
        const QString root = newRoot();
        writeRaw(root + u"/a.md"_s, "- z kopii\n");
        writeRaw(root + u"/obraz.bin"_s, QByteArray("\0z kopii", 8));
        auto ws = openAt(root);
        WorkspaceBackups *b = ws->backups();
        QVERIFY(createNow(b));
        const QString source = b->backups().first().toMap().value(u"name"_s).toString();
        writeRaw(root + u"/obraz.bin"_s, QByteArray("\0inne bajty", 11));
        ws->vault()->rescan();
        QVERIFY(ws->writeFile(u"a.md"_s, u"- po kopii\n"_s));
        m_now = m_now.addSecs(120);
        const QVariantMap plan = plannedRestore(b, source);
        QCOMPARE(plan.value(u"changed"_s).toInt(), 2);

        QFile::setPermissions(root + u"/obraz.bin"_s, QFileDevice::WriteOwner);
        if (QFile probe(root + u"/obraz.bin"_s); probe.open(QIODevice::ReadOnly))
            QSKIP("this user can read everything");
        QSignalSpy messages(b, &WorkspaceBackups::messageRequested);
        b->confirmRestore();
        QTRY_VERIFY_WITH_TIMEOUT(!b->running(), 30000);
        QFile::setPermissions(root + u"/obraz.bin"_s, QFileDevice::ReadOwner | QFileDevice::WriteOwner);
        QCOMPARE(messages.last().at(1).toString(), u"error"_s);
        QVERIFY(messages.last().at(0).toString().contains(u"obraz.bin"_s));
        QCOMPARE(readRaw(root + u"/obraz.bin"_s), QByteArray("\0inne bajty", 11));
        QCOMPARE(readRaw(root + u"/a.md"_s), QByteArray("- po kopii\n"));
    }

    void anotherNotebooksCopiesAreForeign()
    {
        // Two notebooks with one folder name, one backup folder.
        const QString shared = m_tmp.filePath(u"wspólny-%1"_s.arg(++m_n));
        QDir().mkpath(shared);
        const QString rootA = m_tmp.filePath(u"pierwszy-%1/Notatki"_s.arg(m_n));
        const QString rootB = m_tmp.filePath(u"drugi-%1/Notatki"_s.arg(m_n));
        writeRaw(rootA + u"/a.md"_s, "- pierwszy\n");
        writeRaw(rootB + u"/b.md"_s, "- drugi\n");
        auto a = openAt(rootA);
        a->backups()->chooseFolder(QUrl::fromLocalFile(shared));
        QTRY_COMPARE(a->backups()->folder(), shared);
        QVERIFY(createNow(a->backups()));
        const QString copyOfA = a->backups()->backups().first().toMap().value(u"name"_s).toString();
        a.reset();

        auto b = openAt(rootB);
        WorkspaceBackups *backups = b->backups();
        backups->chooseFolder(QUrl::fromLocalFile(shared));
        QTRY_COMPARE(backups->folder(), shared);
        QTRY_COMPARE(backups->backups().size(), 1);
        QVERIFY(backups->backups().first().toMap().value(u"foreign"_s).toBool());

        // Neither restored nor deleted without the explicit confirmation, never pruned.
        QVERIFY(!backups->prepareRestore(copyOfA));
        QVERIFY(!backups->remove(copyOfA));
        QVERIFY(QFileInfo::exists(shared + u'/' + copyOfA));
        backups->setKeep(1);
        m_now = m_now.addSecs(60);
        QVERIFY(createNow(backups));
        QVERIFY(b->writeFile(u"b.md"_s, u"- zmiana\n"_s));
        m_now = m_now.addSecs(60);
        QVERIFY(createNow(backups));
        QTRY_COMPARE(zipsIn(shared).size(), 2);
        QVERIFY(zipsIn(shared).contains(copyOfA));
        QTRY_COMPARE(backups->backups().size(), 2);

        // Asked for by name: the plan says so.
        const QVariantMap plan = [&] {
            QSignalSpy planned(backups, &WorkspaceBackups::restorePlanReady);
            backups->prepareRestore(copyOfA, true);
            return planned.wait(30000) ? planned.first().first().toMap() : QVariantMap();
        }();
        QVERIFY(plan.value(u"foreign"_s).toBool());
        backups->dismissRestore();
        QVERIFY(backups->remove(copyOfA, true));
        QVERIFY(!QFileInfo::exists(shared + u'/' + copyOfA));
    }

    void copiesFromBeforeALostDeviceStateAreStillOwn()
    {
        const QString root = newRoot();
        writeRaw(root + u"/a.md"_s, "- a\n");
        const QString shared = m_tmp.filePath(u"pendrive-%1"_s.arg(m_n));
        QDir().mkpath(shared);
        auto ws = openAt(root);
        ws->backups()->chooseFolder(QUrl::fromLocalFile(shared));
        QTRY_COMPARE(ws->backups()->folder(), shared);
        QVERIFY(createNow(ws->backups()));
        const QString old = zipsIn(shared).first();
        ws.reset();
        QVERIFY(QDir(root + u"-state"_s).removeRecursively()); // the app's data lost; the path is the same

        ws = openAt(root);
        WorkspaceBackups *b = ws->backups();
        b->chooseFolder(QUrl::fromLocalFile(shared));
        QTRY_COMPARE(b->backups().size(), 1);
        QVERIFY(!b->backups().first().toMap().value(u"foreign"_s).toBool());
        b->setKeep(1);
        m_now = m_now.addDays(1);
        QVERIFY(ws->writeFile(u"a.md"_s, u"- b\n"_s));
        QVERIFY(createNow(b));
        QTRY_VERIFY(!zipsIn(shared).contains(old)); // pruned like any of its own
    }

    void anUnloadedBrowserMountRefusesEverything()
    {
        const QString root = newRoot();
        writeRaw(root + u"/a.md"_s, "- a\n");
        auto ws = openAt(root);
        WorkspaceBackups *b = ws->backups();
        QVERIFY(createNow(b));
        const QString source = b->backups().first().toMap().value(u"name"_s).toString();
        const qsizetype before = zipsIn(b->folder()).size();
        b->setStorageUnavailable(u"UnknownError"_s);
        QCOMPARE(b->status(), u"failed"_s);
        QVERIFY(!b->statusText().isEmpty());
        QSignalSpy messages(b, &WorkspaceBackups::messageRequested);
        QVERIFY(!b->createNow());
        QCOMPARE(messages.size(), 1);
        QVERIFY(!b->prepareRestore(source));
        m_now = m_now.addDays(1);
        QCOMPARE(checked(b), QString());
        QCOMPARE(zipsIn(b->folder()).size(), before);
    }

    void anInterruptedRestoreIsToldAgain()
    {
        const QString root = newRoot();
        for (int i = 0; i < 300; ++i)
            writeRaw(root + u"/Notatka %1.md"_s.arg(i), "- z kopii\n");
        auto ws = openAt(root);
        WorkspaceBackups *b = ws->backups();
        QVERIFY(createNow(b));
        const QString source = b->backups().first().toMap().value(u"name"_s).toString();
        for (int i = 0; i < 300; ++i)
            QVERIFY(ws->writeFile(u"Notatka %1.md"_s.arg(i), u"- po kopii\n"_s));
        m_now = m_now.addSecs(120);
        QVERIFY(!plannedRestore(b, source).isEmpty());
        b->confirmRestore();
        QVERIFY(QTest::qWaitFor([&] { return b->phase() == u"restore"; }, 30000));
        QSignalSpy messages(b, &WorkspaceBackups::messageRequested);
        ws->close();
        QCOMPARE(messages.size(), 1);
        QCOMPARE(messages.first().at(1).toString(), u"warning"_s);

        // The next open says so, names the way back, and keeps that way back from pruning.
        ws.reset();
        ws = std::make_unique<Workspace>();
        ws->setClock([this] { return m_now; });
        ws->sync()->setSyncthingDiscovery(false);
        b = ws->backups();
        b->setCheckTimings(24 * 3600 * 1000, 24 * 3600 * 1000);
        QSignalSpy told(b, &WorkspaceBackups::messageRequested);
        QVERIFY(ws->open(root, root + u"-state"_s));
        ws->finishIndexing();
        QVERIFY(b->interrupted());
        QCOMPARE(b->status(), u"failed"_s);
        QCOMPARE(told.size(), 1);
        QVERIFY(told.first().at(0).toString() == b->statusText());
        m_now = m_now.addDays(1);
        QCOMPARE(checked(b), QString()); // no scheduled copies of a half-restored notebook
        b->dismissInterrupted();
        QVERIFY(!b->interrupted());
        QCOMPARE(b->status(), u"ok"_s);
    }

    void aFinishedRestoreLeavesNoMarker()
    {
        const QString root = newRoot();
        writeRaw(root + u"/a.md"_s, "- z kopii\n");
        auto ws = openAt(root);
        WorkspaceBackups *b = ws->backups();
        QVERIFY(createNow(b));
        const QString source = b->backups().first().toMap().value(u"name"_s).toString();
        QVERIFY(ws->writeFile(u"a.md"_s, u"- po kopii\n"_s));
        m_now = m_now.addSecs(120);
        QVERIFY(!plannedRestore(b, source).isEmpty());
        b->confirmRestore();
        QTRY_VERIFY_WITH_TIMEOUT(!b->running(), 30000);
        QVERIFY(!b->interrupted());
    }

    void theDefaultFolderInsideTheNotebookIsRefused()
    {
        // A notebook opened over the app's own data folder: its device state lies inside it.
        const QString root = newRoot();
        writeRaw(root + u"/a.md"_s, "- a\n");
        Workspace ws;
        ws.setClock([this] { return m_now; });
        ws.sync()->setSyncthingDiscovery(false);
        QVERIFY(ws.open(root, root + u"/stan"_s));
        ws.finishIndexing();
        WorkspaceBackups *b = ws.backups();
        QVERIFY(b->folder().startsWith(root + u'/'));
        QSignalSpy messages(b, &WorkspaceBackups::messageRequested);
        QVERIFY(!b->createNow());
        QCOMPARE(messages.size(), 1);
        QCOMPARE(b->status(), u"failed"_s);
        QVERIFY(zipsIn(b->folder()).isEmpty());
    }

    void noDeviceStateNoBackups()
    {
        const QString root = newRoot();
        writeRaw(root + u"/a.md"_s, "- a\n");
        Workspace ws;
        ws.sync()->setSyncthingDiscovery(false);
        QVERIFY(ws.open(root, QString()));
        ws.finishIndexing();
        QVERIFY(!ws.backups()->available());
        QVERIFY(!ws.backups()->createNow());
    }

    void appControllerCommands()
    {
        AppController::Options options;
        options.settingsFile = m_tmp.filePath(u"app/settings.ini"_s);
        options.dataDir = m_tmp.filePath(u"app/data"_s);
        AppController app(options);
        app.setClock([this] { return m_now; });
        Workspace *ws = app.workspace();
        ws->sync()->setSyncthingDiscovery(false);
        ws->backups()->setCheckTimings(24 * 3600 * 1000, 24 * 3600 * 1000);
        const QString root = newRoot();
        writeRaw(root + u"/Start.md"_s, "- start\n");
        app.openVault(root);
        QTRY_COMPARE(app.status(), u"ready"_s);
        QTRY_VERIFY(ws->ready());
        QVERIFY(ws->backups()->available());

        // The palette: to the settings' section; a backup now, confirmed by a toast.
        QVERIFY(app.commands()->ids().contains(u"backup.open"_s));
        QVERIFY(app.commands()->run(u"backup.open"_s));
        QCOMPARE(app.currentRoute().value(u"view"_s).toString(), u"settings"_s);
        QCOMPARE(app.currentRoute().value(u"section"_s).toString(), u"backups"_s);
        QSignalSpy toasts(&app, &AppController::toastRequested);
        QVERIFY(app.commands()->run(u"backup.now"_s));
        QTRY_VERIFY_WITH_TIMEOUT(!ws->backups()->running(), 30000);
        QTRY_COMPARE(ws->backups()->backups().size(), 1);
        QCOMPARE(toasts.last().at(1).toString(), u"success"_s);

        // The documentation is no notebook of the user's: no backups, and the commands are off.
        ws->backups()->setPaused(true);
        QVERIFY(!ws->backups()->available());
        QVERIFY(!app.commands()->run(u"backup.now"_s));
        ws->backups()->setPaused(false);

        app.closeVault();
        QTRY_VERIFY(!ws->isOpen());
    }
};

QTEST_GUILESS_MAIN(TestWorkspaceBackups)
#include "tst_workspace_backups.moc"
