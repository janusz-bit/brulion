// Backup names, what pruning takes and leaves alone, the schedule's decision (never wedged by a clock
// set back or a restored notebook), what goes into a backup and the folder checks (backup/backup.h).
#include "backup/backup.h"

#include "filesystemtraits.h"
#include "vault/vault.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>
#include <QTest>

using namespace Qt::StringLiterals;
using namespace brulion;
using namespace brulion::backup;

namespace {

const QDateTime Now(QDate(2026, 11, 2), QTime(14, 30, 5));

void touch(const QString &path, const QByteArray &bytes = "x")
{
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.write(bytes);
}

QList<BackupFile> backupsAt(const QStringList &names)
{
    QList<BackupFile> out;
    for (const QString &name : names)
        out.append(BackupFile{name, name, *backupFileTime(name, u"Żółw"_s), 1});
    return out;
}

QString named(int day)
{
    return backupFileName(u"Żółw"_s, QDateTime(QDate(2026, 10, day), QTime(8, 0)));
}

} // namespace

class TestBackup : public QObject {
    Q_OBJECT
private Q_SLOTS:
    void fileNames_data()
    {
        QTest::addColumn<QString>("notebook");
        QTest::addColumn<QString>("expected");
        QTest::newRow("plain") << u"Notatki"_s << u"Brulion-Notatki-2026-11-02-143005.zip"_s;
        QTest::newRow("Polish") << u"Mój żółw ąćęłńóśźż"_s << u"Brulion-Mój żółw ąćęłńóśźż-2026-11-02-143005.zip"_s;
        QTest::newRow("reserved characters") << u"a/b:c*d"_s << u"Brulion-a b c d-2026-11-02-143005.zip"_s;
        QTest::newRow("leading and trailing dots") << u"..tajne.."_s << u"Brulion-tajne-2026-11-02-143005.zip"_s;
        QTest::newRow("nothing usable") << u"///"_s << u"Brulion-Untitled-2026-11-02-143005.zip"_s;
    }
    void fileNames()
    {
        QFETCH(QString, notebook);
        QFETCH(QString, expected);
        QCOMPARE(backupFileName(notebook, Now), expected);
        QCOMPARE(backupFileTime(expected, notebook), std::optional<QDateTime>(Now));
    }

    void foreignNames_data()
    {
        QTest::addColumn<QString>("name");
        QTest::newRow("another notebook") << u"Brulion-Inny-2026-11-02-143005.zip"_s;
        QTest::newRow("a longer notebook name") << u"Brulion-Żółw 2-2026-11-02-143005.zip"_s;
        QTest::newRow("a user's zip") << u"Żółw.zip"_s;
        QTest::newRow("no stamp") << u"Brulion-Żółw-.zip"_s;
        QTest::newRow("short stamp") << u"Brulion-Żółw-2026-11-02-1430.zip"_s;
        QTest::newRow("impossible date") << u"Brulion-Żółw-2026-13-45-143005.zip"_s;
        QTest::newRow("impossible time") << u"Brulion-Żółw-2026-11-02-256105.zip"_s;
        QTest::newRow("letters in the stamp") << u"Brulion-Żółw-2026-11-02-14300a.zip"_s;
        QTest::newRow("other digits") << u"Brulion-Żółw-٢٠٢٦-11-02-143005.zip"_s;
        QTest::newRow("another extension") << u"Brulion-Żółw-2026-11-02-143005.tar"_s;
        QTest::newRow("partial") << u"Brulion-Żółw-2026-11-02-143005.zip.AbCdEf"_s;
        QTest::newRow("a prefix") << u"kopia Brulion-Żółw-2026-11-02-143005.zip"_s;
    }
    void foreignNames()
    {
        QFETCH(QString, name);
        QVERIFY(!backupFileTime(name, u"Żółw"_s).has_value());
    }

    void decomposedNamesAreRecognised()
    {
        // macOS, or a copy from it: the same name in NFD.
        const QString nfd = backupFileName(u"Żółw"_s, Now).normalized(QString::NormalizationForm_D);
        QCOMPARE(backupFileTime(nfd, u"Żółw"_s), std::optional<QDateTime>(Now));
    }

    void freePath()
    {
        QTemporaryDir dir;
        const QString first = freeBackupPath(dir.path(), u"Żółw"_s, Now);
        QCOMPARE(QFileInfo(first).fileName(), backupFileName(u"Żółw"_s, Now));
        touch(first);
        const QString second = freeBackupPath(dir.path(), u"Żółw"_s, Now);
        QCOMPARE(QFileInfo(second).fileName(), backupFileName(u"Żółw"_s, Now.addSecs(1)));
    }

    void listIsNewestFirstAndOursOnly()
    {
        QTemporaryDir dir;
        const QString folder = dir.path();
        touch(folder + u'/' + named(1));
        touch(folder + u'/' + named(3), "abc");
        touch(folder + u'/' + named(2));
        touch(folder + u"/Brulion-Inny-2026-10-02-080000.zip"_s);
        touch(folder + u"/zdjęcia.zip"_s);
        touch(folder + u"/notatka.md"_s);
        QDir(folder).mkdir(backupFileName(u"Żółw"_s, Now)); // a folder of that name
        const QList<BackupFile> list = listBackups(folder, u"Żółw"_s);
        QCOMPARE(list.size(), 3);
        QCOMPARE(list.at(0).fileName, named(3));
        QCOMPARE(list.at(0).size, qint64(3));
        QCOMPARE(list.at(1).fileName, named(2));
        QCOMPARE(list.at(2).fileName, named(1));
        QVERIFY(listBackups(folder + u"/brak"_s, u"Żółw"_s).isEmpty());
    }

    void symlinksAreNotOurs()
    {
        if (!brulion::test::hasSymlinks())
            QSKIP("no symbolic links here");
        QTemporaryDir dir;
        touch(dir.filePath(u"prawdziwy.zip"_s));
        QVERIFY(QFile::link(dir.filePath(u"prawdziwy.zip"_s), dir.filePath(named(4))));
        QVERIFY(listBackups(dir.path(), u"Żółw"_s).isEmpty());
    }

    void pruneChoice_data()
    {
        QTest::addColumn<QStringList>("backups"); // newest first
        QTest::addColumn<int>("keep");
        QTest::addColumn<QStringList>("own");
        QTest::addColumn<QStringList>("protect");
        QTest::addColumn<QStringList>("pruned");
        const QStringList three{named(3), named(2), named(1)};
        QTest::newRow("under the limit") << three << 3 << three << QStringList() << QStringList();
        QTest::newRow("oldest goes") << three << 2 << three << QStringList() << QStringList{named(1)};
        QTest::newRow("keeps at least one") << three << 0 << three << QStringList() << QStringList{named(2), named(1)};
        QTest::newRow("foreign ones are left alone and not counted")
                << three << 1 << QStringList{named(3), named(1)} << QStringList() << QStringList{named(1)};
        QTest::newRow("nothing of its own") << three << 1 << QStringList() << QStringList() << QStringList();
        QTest::newRow("protected is never pruned and counts")
                << three << 1 << three << QStringList{named(1)} << QStringList{named(3), named(2)};
        QTest::newRow("a new backup older by its name (clock set back) is protected")
                << three << 2 << three << QStringList{named(1)} << QStringList{named(2)};
    }
    void pruneChoice()
    {
        QFETCH(QStringList, backups);
        QFETCH(int, keep);
        QFETCH(QStringList, own);
        QFETCH(QStringList, protect);
        QFETCH(QStringList, pruned);
        const QSet<QString> ownSet(own.cbegin(), own.cend());
        const QSet<QString> protectSet(protect.cbegin(), protect.cend());
        QCOMPARE(backupsToPrune(backupsAt(backups), keep, ownSet, protectSet), pruned);
    }

    void pruneLeavesForeignFilesAlone()
    {
        QTemporaryDir dir;
        const QString folder = dir.path();
        QSet<QString> own;
        for (int day = 1; day <= 5; ++day) {
            touch(folder + u'/' + named(day));
            own.insert(named(day));
        }
        const QStringList foreign{u"Brulion-Inny-2026-10-01-080000.zip"_s, u"Żółw.zip"_s, u"zdjęcia.zip"_s,
                                  u"Brulion-Żółw-ważne.zip"_s, u"notatka.md"_s};
        for (const QString &name : foreign)
            touch(folder + u'/' + name);
        // The same notebook name, written by another notebook (not in `own`).
        const QString twin = named(9);
        touch(folder + u'/' + twin);

        const QStringList removed = pruneBackups(folder, u"Żółw"_s, 2, own, {});
        QCOMPARE(removed, (QStringList{named(3), named(2), named(1)}));
        for (const QString &name : foreign)
            QVERIFY2(QFileInfo::exists(folder + u'/' + name), qPrintable(name));
        QVERIFY(QFileInfo::exists(folder + u'/' + twin));
        QVERIFY(QFileInfo::exists(folder + u'/' + named(5)));
        QVERIFY(QFileInfo::exists(folder + u'/' + named(4)));
    }

    void stalePartials()
    {
        QTemporaryDir dir;
        const QString folder = dir.path();
        const QString stale = folder + u'/' + named(1) + u".AbCdEf"_s;
        const QString fresh = folder + u'/' + named(2) + u".GhIjKl"_s;
        const QString foreign = folder + u"/Brulion-Inny-2026-10-01-080000.zip.AbCdEf"_s;
        const QString user = folder + u'/' + named(3) + u".kopia"_s;
        for (const QString &file : {stale, fresh, foreign, user})
            touch(file);
        const QDateTime later = QFileInfo(fresh).lastModified().addDays(2);
        QFile f(fresh);
        QVERIFY(f.open(QIODevice::ReadWrite));
        QVERIFY(f.setFileTime(later, QFileDevice::FileModificationTime));
        f.close();
        QCOMPARE(removeStalePartials(folder, u"Żółw"_s, later), 1);
        QVERIFY(!QFileInfo::exists(stale));
        QVERIFY(QFileInfo::exists(fresh));
        QVERIFY(QFileInfo::exists(foreign));
        QVERIFY(QFileInfo::exists(user));
    }

    void due_data()
    {
        QTest::addColumn<int>("interval");
        QTest::addColumn<QDateTime>("last");
        QTest::addColumn<bool>("expected");
        const int daily = int(Interval::Daily);
        const int weekly = int(Interval::Weekly);
        QTest::newRow("never") << daily << QDateTime() << true;
        QTest::newRow("today") << daily << Now.addSecs(-3600) << false;
        QTest::newRow("yesterday late") << daily << QDateTime(Now.date().addDays(-1), QTime(23, 59)) << true;
        QTest::newRow("six days") << weekly << Now.addDays(-6) << false;
        QTest::newRow("seven days") << weekly << Now.addDays(-7) << true;
        QTest::newRow("clock set back, daily") << daily << Now.addSecs(60) << true;
        QTest::newRow("clock set back, weekly") << weekly << Now.addDays(400) << true;
    }
    void due()
    {
        QFETCH(int, interval);
        QFETCH(QDateTime, last);
        QFETCH(bool, expected);
        QCOMPARE(isDue(Interval(interval), last, Now), expected);
    }

    void decision()
    {
        Settings settings;
        ScheduleState state;
        QCOMPARE(decide(settings, state, u"a"_s, 3, Now), Decision::Run);
        QCOMPARE(decide(settings, state, u"a"_s, 0, Now), Decision::Empty);
        state.lastBackup = Now.addDays(-1);
        state.lastFingerprint = u"a"_s;
        state.lastFileExists = true;
        QCOMPARE(decide(settings, state, u"a"_s, 3, Now), Decision::Unchanged);
        QCOMPARE(decide(settings, state, u"b"_s, 3, Now), Decision::Run);
        state.lastFileExists = false; // the user deleted it, or changed the folder
        QCOMPARE(decide(settings, state, u"a"_s, 3, Now), Decision::Run);
        state.lastFileExists = true;
        state.lastBackup = Now;
        QCOMPARE(decide(settings, state, u"b"_s, 3, Now), Decision::NotDue);
        // A clock set back: a later backup in the books never stops backups of changes.
        state.lastBackup = Now.addYears(3);
        QCOMPARE(decide(settings, state, u"b"_s, 3, Now), Decision::Run);
        settings.enabled = false;
        QCOMPARE(decide(settings, state, u"b"_s, 3, Now), Decision::Disabled);
    }

    void sourcesAndFingerprint()
    {
        QTemporaryDir dir;
        const QString root = dir.filePath(u"Notatnik"_s);
        touch(root + u"/Żółw.md"_s, "- ąćę\n");
        touch(root + u"/assets/zdjęcie.png"_s, QByteArray("\x89PNG\0\1", 6));
        touch(root + u"/.brulion/config.json"_s, "{}");
        touch(root + u"/.brulion/inny.json"_s, "{}");
        touch(root + u"/.trash/stary.md"_s, "stary");
        touch(root + u"/.git/config"_s, "[core]");
        touch(root + u"/.obsidian/app.json"_s, "{}");
        touch(root + u"/node_modules/x.js"_s, "x");
        touch(root + u"/.DS_Store"_s, "x");
        touch(root + u"/szkic.md.part"_s, "x");
        QDir().mkpath(root + u"/Puste/Głębiej"_s);
        QDir().mkpath(root + u"/Pełne"_s);
        touch(root + u"/Pełne/a.md"_s, "a");

        Vault vault(root);
        QVERIFY(vault.open());
        const Sources sources = collectSources(vault);
        QStringList paths;
        for (const SourceFile &f : sources.files)
            paths.append(f.path);
        QCOMPARE(paths,
                 (QStringList{u".brulion/config.json"_s, u"Pełne/a.md"_s, u"assets/zdjęcie.png"_s, u"Żółw.md"_s}));
        QCOMPARE(sources.emptyFolders, QStringList{u"Puste/Głębiej"_s});
        const qint64 total = 2 + 1 + 6 + QFileInfo(root + u"/Żółw.md"_s).size();
        QCOMPARE(sources.totalBytes, total);

        const QString before = fingerprint(sources);
        QCOMPARE(fingerprint(collectSources(vault)), before);
        // Anything ignored changes nothing.
        touch(root + u"/.trash/nowy.md"_s, "nowy");
        vault.rescan();
        QCOMPARE(fingerprint(collectSources(vault)), before);
        // A real change does.
        QVERIFY(vault.writeText(u"Pełne/a.md"_s, u"inny tekst"_s));
        const QString afterEdit = fingerprint(collectSources(vault));
        QVERIFY(afterEdit != before);
        QVERIFY(vault.createFolder(u"Nowy pusty"_s));
        QVERIFY(fingerprint(collectSources(vault)) != afterEdit);
    }

    void folderInsideNotebook()
    {
        QTemporaryDir dir;
        const QString root = dir.filePath(u"Notatnik"_s);
        QDir().mkpath(root + u"/Podfolder"_s);
        QVERIFY(isInsideFolder(root, root));
        QVERIFY(isInsideFolder(root + u"/Podfolder"_s, root));
        QVERIFY(isInsideFolder(root + u"/Nie ma/Głęboko"_s, root));
        QVERIFY(!isInsideFolder(dir.path(), root));
        QVERIFY(!isInsideFolder(root + u" 2"_s, root)); // only a prefix of its name
        QVERIFY(!isInsideFolder(dir.filePath(u"Notatnik-kopie"_s), root));
        if (brulion::test::hasSymlinks()) {
            QVERIFY(QFile::link(root + u"/Podfolder"_s, dir.filePath(u"skrót"_s)));
            QVERIFY(isInsideFolder(dir.filePath(u"skrót/x"_s), root));
        }
    }

    void writableFolder()
    {
        QTemporaryDir dir;
        QString error;
        QVERIFY(checkFolderWritable(dir.path(), &error));
        QCOMPARE(QDir(dir.path()).entryList(QDir::AllEntries | QDir::Hidden | QDir::NoDotAndDotDot), QStringList());
        QVERIFY(!checkFolderWritable(dir.filePath(u"brak"_s), &error));
        QVERIFY(!error.isEmpty());
        if (brulion::test::hasUnixPermissions()) {
            const QString locked = dir.filePath(u"tylko do odczytu"_s);
            QDir().mkpath(locked);
            QFile::setPermissions(locked, QFileDevice::ReadOwner | QFileDevice::ExeOwner);
            if (QFileInfo(locked).isWritable()) // root
                QSKIP("this user can write everywhere");
            QVERIFY(!checkFolderWritable(locked, &error));
            QFile::setPermissions(locked, QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner);
        }
    }
};

QTEST_GUILESS_MAIN(TestBackup)
#include "tst_backup.moc"
