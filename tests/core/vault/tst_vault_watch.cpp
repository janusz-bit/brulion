// External changes: rescan() diffing and the file system watcher (echo suppression, renames,
// atomic replace by other programs, new folders, config reloads). Files are changed "behind
// the vault's back" with plain QFile/rename calls and explicit modification times.
#include "vaulttest.h"

#include <QRegularExpression>

#include <cstdio>

using namespace vaulttest;

namespace {

qint64 diskMtime(const QString &abs)
{
    return QFileInfo(abs).fileTime(QFileDevice::FileModificationTime, QTimeZone(QTimeZone::UTC)).toMSecsSinceEpoch();
}

/// rename(2) that replaces an existing target, like editors and Syncthing do.
bool replaceBehind(const QString &root, const QString &from, const QString &to)
{
    const QByteArray a = QFile::encodeName(root + u'/' + from);
    const QByteArray b = QFile::encodeName(root + u'/' + to);
    return std::rename(a.constData(), b.constData()) == 0;
}

} // namespace

class TestVaultWatch : public QObject {
    Q_OBJECT
private Q_SLOTS:
    // ---- rescan --------------------------------------------------------------------------
    void rescanDetectsChangesMadeWhileNotWatching()
    {
        Fixture f;
        f.write(u"a.md"_s, "a");
        f.write(u"b.md"_s, "b");
        f.write(u"c.md"_s, "c");
        f.write(u"img.png"_s, "\x01");
        QVERIFY(f.open());
        QCOMPARE(f.text(u"a.md"_s), u"a"_s);
        QCOMPARE(f.text(u"c.md"_s), u"c"_s);
        f.write(u"a.md"_s, "a2");
        touchBehind(f.root(), u"c.md"_s); // same content
        QVERIFY(removeBehind(f.root(), u"b.md"_s));
        f.write(u"d/nowy.md"_s, "d");
        f.write(u"img.png"_s, "\x01\x02");
        QVERIFY(QDir().mkpath(f.root() + u"/pusty"_s));
        const QList<VaultChange> changes = f.vault->rescan();
        QCOMPARE(sorted(shortChanges(changes)), (QStringList{u"create:d/nowy.md@external"_s, u"delete:b.md@external"_s,
                                                             u"modify:a.md@external"_s, u"modify:img.png@external"_s}));
        QCOMPARE(f.spy->count(), 1);
        QCOMPARE(f.changes(), shortChanges(changes));
        QCOMPARE(f.folders(), (QStringList{u"d"_s, u"pusty"_s}));
        QCOMPARE(f.text(u"a.md"_s), u"a2"_s);
        QCOMPARE(f.vault->stat(u"c.md"_s)->mtime, diskMtime(f.root() + u"/c.md"_s));
        QCOMPARE(f.historyContents(u"a.md"_s), QStringList{u"a"_s}); // replaced content kept
        QCOMPARE(f.historyReasons(u"a.md"_s), QStringList{u"external"_s});
        QVERIFY(f.vault->rescan().isEmpty());
        QCOMPARE(f.spy->count(), 1);
    }

    void rescanDropsVanishedFolders()
    {
        Fixture f;
        f.write(u"F/a.md"_s, "a");
        QVERIFY(f.open());
        QVERIFY(QDir().mkpath(f.root() + u"/E/pod"_s));
        QVERIFY(f.vault->rescan().isEmpty()); // folders produce no changes
        QCOMPARE(f.folders(), (QStringList{u"E"_s, u"E/pod"_s, u"F"_s}));
        QVERIFY(removeBehind(f.root(), u"F"_s));
        QVERIFY(removeBehind(f.root(), u"E"_s));
        QCOMPARE(shortChanges(f.vault->rescan()), QStringList{u"delete:F/a.md@external"_s});
        QVERIFY(f.folders().isEmpty());
    }

    void rescanReportsModificationsOfUnreadFiles()
    {
        Fixture f;
        f.write(u"a.md"_s, "a");
        QVERIFY(f.open());
        f.write(u"a.md"_s, "b");
        QCOMPARE(shortChanges(f.vault->rescan()), QStringList{u"modify:a.md@external"_s});
        QVERIFY(f.vault->history()->list(u"a.md"_s).isEmpty()); // the old content was never seen
    }

    void rescanDetectsRenames()
    {
        Fixture f;
        f.write(u"a.md"_s, "v0");
        f.write(u"zdjęcie.png"_s, "\x01\x02\x03"); // never read: matched by size + mtime
        QVERIFY(f.open());
        QVERIFY(f.vault->writeText(u"a.md"_s, u"v1"_s));
        f.spy->clear();
        QVERIFY(renameBehind(f.root(), u"a.md"_s, u"Przeniesione/ą.md"_s));
        QVERIFY(renameBehind(f.root(), u"zdjęcie.png"_s, u"assets/zdjęcie.png"_s));
        QCOMPARE(sorted(shortChanges(f.vault->rescan())),
                 (QStringList{u"rename:a.md->Przeniesione/ą.md@external"_s,
                              u"rename:zdjęcie.png->assets/zdjęcie.png@external"_s}));
        QCOMPARE(f.historyContents(u"Przeniesione/ą.md"_s), QStringList{u"v0"_s});
        QVERIFY(f.vault->exists(u"Przeniesione"_s));
        QCOMPARE(f.text(u"Przeniesione/ą.md"_s), u"v1"_s);
    }

    void rescanRenameWithChangedContentIsDeleteAndCreate()
    {
        Fixture f;
        f.write(u"a.md"_s, "v0");
        QVERIFY(f.open());
        QCOMPARE(f.text(u"a.md"_s), u"v0"_s);
        QVERIFY(renameBehind(f.root(), u"a.md"_s, u"b.md"_s));
        f.write(u"b.md"_s, "v1"); // same size, other content
        QCOMPARE(sorted(shortChanges(f.vault->rescan())),
                 (QStringList{u"create:b.md@external"_s, u"delete:a.md@external"_s}));
    }

    void rescanDetectsFolderRenames()
    {
        Fixture f;
        f.write(u"F/a.md"_s, "a");
        f.write(u"F/sub/b.md"_s, "b");
        f.write(u"F/sub/pusty.md"_s, "");
        f.write(u"F/też pusty.md"_s, "");
        QVERIFY(f.open());
        QVERIFY(renameBehind(f.root(), u"F"_s, u"G"_s));
        QCOMPARE(sorted(shortChanges(f.vault->rescan())),
                 (QStringList{u"rename:F/a.md->G/a.md@external"_s, u"rename:F/sub/b.md->G/sub/b.md@external"_s,
                              u"rename:F/sub/pusty.md->G/sub/pusty.md@external"_s,
                              u"rename:F/też pusty.md->G/też pusty.md@external"_s}));
        QCOMPARE(f.folders(), (QStringList{u"G"_s, u"G/sub"_s}));
    }

    void rescanFileReplacedByFolder()
    {
        Fixture f;
        f.write(u"Szkic"_s, "stary plik — zażółć");
        f.write(u"inne.md"_s, "i");
        QVERIFY(f.open());
        QCOMPARE(f.text(u"Szkic"_s), u"stary plik — zażółć"_s);
        QVERIFY(removeBehind(f.root(), u"Szkic"_s));
        f.write(u"Szkic/nowy.md"_s, "nowy");
        QCOMPARE(sorted(shortChanges(f.vault->rescan())),
                 (QStringList{u"create:Szkic/nowy.md@external"_s, u"delete:Szkic@external"_s}));
        QCOMPARE(f.files(), (QStringList{u"Szkic/nowy.md"_s, u"inne.md"_s}));
        QCOMPARE(f.folders(), QStringList{u"Szkic"_s});
        QCOMPARE(f.historyReasons(u"Szkic"_s), QStringList{u"external"_s});
        QCOMPARE(f.historyContents(u"Szkic"_s), QStringList{u"stary plik — zażółć"_s});
    }

    void rescanFolderReplacedByFile()
    {
        Fixture f;
        f.write(u"Szkic/a.md"_s, "a");
        f.write(u"Szkic/b/ć.md"_s, "ć");
        f.write(u"inne.md"_s, "i");
        QVERIFY(f.open());
        QVERIFY(removeBehind(f.root(), u"Szkic"_s));
        f.write(u"Szkic"_s, "teraz plik");
        QCOMPARE(sorted(shortChanges(f.vault->rescan())),
                 (QStringList{u"create:Szkic@external"_s, u"delete:Szkic/a.md@external"_s,
                              u"delete:Szkic/b/ć.md@external"_s}));
        QCOMPARE(f.files(), (QStringList{u"Szkic"_s, u"inne.md"_s}));
        QVERIFY(f.folders().isEmpty());
    }

    void rescanDistinctCaseVariants()
    {
        Fixture f;
        f.write(u"A.md"_s, "duże");
        QVERIFY(f.open());
        QVERIFY(removeBehind(f.root(), u"A.md"_s));
        f.write(u"a.md"_s, "małe");
        QCOMPARE(sorted(shortChanges(f.vault->rescan())),
                 (QStringList{u"create:a.md@external"_s, u"delete:A.md@external"_s}));
    }

    void rescanTakesInConfigChanges()
    {
        Fixture f;
        QVERIFY(f.open());
        QSignalSpy configSpy(f.vault.get(), &Vault::configChanged);
        f.write(u".brulion/config.json"_s, R"({"journalsFolder":"Dni"})");
        f.write(u".brulion/inne.json"_s, "{}");
        QCOMPARE(shortChanges(f.vault->rescan()), QStringList{u"create:.brulion/config.json@external"_s});
        QCOMPARE(f.vault->config().journalsFolder, u"Dni"_s);
        QCOMPARE(configSpy.count(), 1);
    }

    void aMissingVaultFolderIsNotAMassDeletion()
    {
        Fixture f;
        f.write(u"a.md"_s, "a");
        f.write(u"b.md"_s, "b");
        QVERIFY(f.open());
        QVERIFY(QDir().rename(f.root(), f.root() + u"-odmontowany"_s));
        QTest::ignoreMessage(QtWarningMsg, QRegularExpression(u"vault folder is missing"_s));
        QVERIFY(f.vault->rescan().isEmpty());
        QCOMPARE(f.files(), (QStringList{u"a.md"_s, u"b.md"_s}));
        QVERIFY(QDir().rename(f.root() + u"-odmontowany"_s, f.root()));
        QVERIFY(f.vault->rescan().isEmpty());
        QCOMPARE(f.spy->count(), 0);
    }

    void readingAChangedFileSchedulesACheck()
    {
        Fixture f;
        f.write(u"a.md"_s, "v0");
        QVERIFY(f.open());
        QCOMPARE(f.text(u"a.md"_s), u"v0"_s);
        f.write(u"a.md"_s, "v1 zmieniona");
        QCOMPARE(f.text(u"a.md"_s), u"v1 zmieniona"_s);
        QCOMPARE(f.spy->count(), 0); // never synchronously from a read
        QTRY_COMPARE(f.changes(), QStringList{u"modify:a.md@external"_s});
        QCOMPARE(f.historyContents(u"a.md"_s), QStringList{u"v0"_s});
    }

    void readingAnUnknownFileReportsIt()
    {
        Fixture f;
        QVERIFY(f.open());
        f.write(u"nowy.md"_s, "nowy");
        QCOMPARE(f.text(u"nowy.md"_s), u"nowy"_s);
        f.vault->processPendingEvents();
        QCOMPARE(f.changes(), QStringList{u"create:nowy.md@external"_s});
    }

    void periodicRescan()
    {
        Fixture f;
        QVERIFY(f.open());
        f.vault->setRescanInterval(20);
        f.write(u"x.md"_s, "x");
        QTRY_COMPARE(f.changes(), QStringList{u"create:x.md@external"_s});
        f.vault->setRescanInterval(0);
    }

    // ---- watching ------------------------------------------------------------------------
    void watcherReportsExternalChanges()
    {
        Fixture f;
        f.write(u"a.md"_s, "a");
        f.write(u"b.md"_s, "b");
        QVERIFY(f.open());
        QCOMPARE(f.text(u"a.md"_s), u"a"_s);
        f.vault->startWatching();
        QVERIFY(f.vault->isWatching());
        f.write(u"new.md"_s, "nowa");
        f.write(u"a.md"_s, "a zmieniona");
        QVERIFY(removeBehind(f.root(), u"b.md"_s));
        QTRY_COMPARE(sorted(f.changes()),
                     (QStringList{u"create:new.md@external"_s, u"delete:b.md@external"_s, u"modify:a.md@external"_s}));
        QCOMPARE(f.text(u"a.md"_s), u"a zmieniona"_s);
        QVERIFY(!f.vault->exists(u"b.md"_s));
        f.vault->stopWatching();
        QVERIFY(!f.vault->isWatching());
        f.write(u"c.md"_s, "c");
        QTest::qWait(200);
        QCOMPARE(f.changes().size(), 3);
    }

    void watcherSuppressesEchoesOfOwnChanges()
    {
        Fixture f;
        f.write(u"a.md"_s, "a");
        f.write(u"F/x.md"_s, "x");
        QVERIFY(f.open());
        f.vault->startWatching();
        QVERIFY(f.vault->writeText(u"a.md"_s, u"moja zmiana"_s));
        QVERIFY(f.vault->writeText(u"b.md"_s, u"nowy"_s));
        QVERIFY(f.vault->writeBinary(u"img.png"_s, "\x01"_ba));
        QVERIFY(f.vault->rename(u"b.md"_s, u"c.md"_s));
        QVERIFY(f.vault->rename(u"F"_s, u"G"_s));
        QVERIFY(f.vault->remove(u"c.md"_s));
        VaultConfig config;
        config.weekStartsOn = Qt::Sunday;
        QVERIFY(f.vault->saveConfig(config));
        QVERIFY(f.vault->createFolder(u"Nowy folder"_s));
        const QStringList own = f.changes();
        QCOMPARE(own.size(), 7);
        QTest::qWait(300); // let every watcher echo arrive
        f.vault->processPendingEvents();
        QCOMPARE(f.changes(), own);
        // The watches are still alive afterwards.
        f.write(u"a.md"_s, "z zewnątrz");
        f.write(u"G/x.md"_s, "x2");
        f.write(u"Nowy folder/y.md"_s, "y");
        QTRY_COMPARE(sorted(f.changes().mid(own.size())),
                     (QStringList{u"create:Nowy folder/y.md@external"_s, u"modify:G/x.md@external"_s,
                                  u"modify:a.md@external"_s}));
    }

    void watcherIgnoresTouches()
    {
        Fixture f;
        f.write(u"a.md"_s, "same");
        QVERIFY(f.open());
        QCOMPARE(f.text(u"a.md"_s), u"same"_s);
        f.vault->startWatching();
        touchBehind(f.root(), u"a.md"_s);
        const qint64 mtime = diskMtime(f.root() + u"/a.md"_s);
        QTRY_COMPARE(f.vault->stat(u"a.md"_s)->mtime, mtime);
        QVERIFY(f.changes().isEmpty());
    }

    void watcherSnapshotsReplacedAndDeletedContent()
    {
        Fixture f;
        f.write(u"a.md"_s, "moja wersja");
        f.write(u"b.md"_s, "do usunięcia");
        QVERIFY(f.open());
        QCOMPARE(f.text(u"a.md"_s), u"moja wersja"_s);
        QCOMPARE(f.text(u"b.md"_s), u"do usunięcia"_s);
        f.vault->startWatching();
        f.write(u"a.md"_s, "wersja z Syncthinga");
        QVERIFY(removeBehind(f.root(), u"b.md"_s));
        QTRY_COMPARE(f.changes().size(), 2);
        QCOMPARE(f.historyReasons(u"a.md"_s), QStringList{u"external"_s});
        QCOMPARE(f.historyContents(u"a.md"_s), QStringList{u"moja wersja"_s});
        QCOMPARE(f.historyContents(u"b.md"_s), QStringList{u"do usunięcia"_s});
    }

    void watcherDetectsRenames()
    {
        Fixture f;
        f.write(u"a.md"_s, "v0");
        QVERIFY(f.open());
        QVERIFY(f.vault->writeText(u"a.md"_s, u"v1"_s));
        f.vault->startWatching();
        QVERIFY(renameBehind(f.root(), u"a.md"_s, u"Przeniesione/ą.md"_s));
        QTRY_COMPARE(f.changes().mid(1), QStringList{u"rename:a.md->Przeniesione/ą.md@external"_s});
        QCOMPARE(f.historyContents(u"Przeniesione/ą.md"_s), QStringList{u"v0"_s});
        QVERIFY(f.vault->exists(u"Przeniesione"_s));
        // The moved file is watched under its new name.
        f.write(u"Przeniesione/ą.md"_s, "v2 z zewnątrz");
        QTRY_COMPARE(f.changes().mid(2), QStringList{u"modify:Przeniesione/ą.md@external"_s});
    }

    void watcherDetectsFolderRenames()
    {
        Fixture f;
        f.write(u"F/a.md"_s, "a");
        f.write(u"F/sub/b.md"_s, "b");
        QVERIFY(f.open());
        f.vault->startWatching();
        QVERIFY(renameBehind(f.root(), u"F"_s, u"G"_s));
        QTRY_COMPARE(sorted(f.changes()),
                     (QStringList{u"rename:F/a.md->G/a.md@external"_s, u"rename:F/sub/b.md->G/sub/b.md@external"_s}));
        QCOMPARE(f.folders(), (QStringList{u"G"_s, u"G/sub"_s}));
        f.write(u"G/sub/c.md"_s, "c");
        QTRY_COMPARE(f.changes().mid(2), QStringList{u"create:G/sub/c.md@external"_s});
    }

    void watcherScansAndWatchesNewFolders()
    {
        Fixture f;
        QVERIFY(f.open());
        f.vault->startWatching();
        f.write(u"Nowy/a.md"_s, "a");
        f.write(u"Nowy/Głęboki/b.md"_s, "b");
        QTRY_COMPARE(sorted(f.changes()),
                     (QStringList{u"create:Nowy/Głęboki/b.md@external"_s, u"create:Nowy/a.md@external"_s}));
        QCOMPARE(f.folders(), (QStringList{u"Nowy"_s, u"Nowy/Głęboki"_s}));
        f.write(u"Nowy/a.md"_s, "a zmieniona");
        f.write(u"Nowy/Głęboki/c.md"_s, "c");
        QTRY_COMPARE(sorted(f.changes().mid(2)),
                     (QStringList{u"create:Nowy/Głęboki/c.md@external"_s, u"modify:Nowy/a.md@external"_s}));
    }

    void watcherFollowsAtomicReplaces()
    {
        // Editors and Syncthing write a temporary file and rename it over the note: the watch
        // on the old inode dies and must be re-armed every time.
        Fixture f;
        f.write(u"a.md"_s, "v0");
        QVERIFY(f.open());
        QCOMPARE(f.text(u"a.md"_s), u"v0"_s);
        f.vault->startWatching();
        for (int i = 1; i <= 3; ++i) {
            f.write(u"~syncthing~a.md.tmp"_s, u"wersja %1"_s.arg(i).toUtf8());
            QVERIFY(replaceBehind(f.root(), u"~syncthing~a.md.tmp"_s, u"a.md"_s));
            QTRY_COMPARE(f.changes().size(), i);
            QCOMPARE(f.changes().last(), u"modify:a.md@external"_s);
            QCOMPARE(f.text(u"a.md"_s), u"wersja %1"_s.arg(i));
        }
        QCOMPARE(f.historyContents(u"a.md"_s), (QStringList{u"wersja 2"_s, u"wersja 1"_s, u"v0"_s}));
    }

    void watcherReloadsTheConfigAndIgnoresIgnoredPaths()
    {
        Fixture f;
        QVERIFY(f.open());
        f.vault->startWatching();
        QSignalSpy configSpy(f.vault.get(), &Vault::configChanged);
        f.write(u".brulion/config.json"_s, R"({"journalsFolder":"Dni"})");
        QTRY_COMPARE(f.vault->config().journalsFolder, u"Dni"_s);
        f.write(u".obsidian/workspace.json"_s, "{}");
        f.write(u"~syncthing~x.md.tmp"_s, "tmp");
        f.write(u".brulion/config.json"_s, R"({"journalsFolder":"Dziennik"})");
        QTRY_COMPARE(f.vault->config().journalsFolder, u"Dziennik"_s);
        QVERIFY(removeBehind(f.root(), u".brulion/config.json"_s));
        QTRY_COMPARE(f.vault->config().journalsFolder, u"journals"_s);
        QCOMPARE(f.changes(),
                 (QStringList{u"create:.brulion/config.json@external"_s, u"modify:.brulion/config.json@external"_s,
                              u"delete:.brulion/config.json@external"_s}));
        QCOMPARE(configSpy.count(), 3);
    }

    void watcherComparesBinaryFilesByStat()
    {
        Fixture f;
        f.write(u"img.png"_s, "\x01");
        QVERIFY(f.open());
        f.vault->startWatching();
        f.write(u"img.png"_s, "\x01\x02");
        QTRY_COMPARE(f.changes(), QStringList{u"modify:img.png@external"_s});
    }

    void ownWritesRacingWithEventsAreNeverExternal()
    {
        Fixture f;
        f.write(u"a.md"_s, "a");
        QVERIFY(f.open());
        f.vault->startWatching();
        for (int i = 0; i < 10; ++i) {
            QVERIFY(f.vault->writeText(u"a.md"_s, u"pisanie %1"_s.arg(i)));
            QTest::qWait(5); // let some events through in between
        }
        QTest::qWait(200);
        f.vault->processPendingEvents();
        const QList<VaultChange> all = collected(*f.spy);
        QCOMPARE(all.size(), 10);
        for (const VaultChange &change : all)
            QCOMPARE(change.source, ChangeSource::User);
        QCOMPARE(f.read(u"a.md"_s), "pisanie 9"_ba);
    }

    void manyNewFoldersInOneBatch()
    {
        // Syncthing's first sync: the watcher reports only the new top-level folders.
        Fixture f;
        QVERIFY(f.open());
        f.vault->startWatching();
        for (int i = 0; i < 20; ++i)
            f.write(u"Folder %1/Notatka ż %1.md"_s.arg(i), u"treść %1"_s.arg(i).toUtf8());
        QTRY_COMPARE(f.files().size(), 20);
        const QList<VaultChange> all = collected(*f.spy);
        QCOMPARE(all.size(), 20);
        for (const VaultChange &change : all) {
            QCOMPARE(change.type, ChangeType::Created);
            QCOMPARE(change.source, ChangeSource::External);
        }
    }

    void foldersDeletedByOtherPrograms()
    {
        Fixture f;
        f.write(u"Projekt/Etap/a.md"_s, "a");
        f.write(u"Projekt/b.md"_s, "b");
        f.write(u"Inne/c.md"_s, "c");
        QVERIFY(f.open());
        f.vault->startWatching();
        QVERIFY(removeBehind(f.root(), u"Projekt"_s));
        QVERIFY(removeBehind(f.root(), u"Inne/c.md"_s)); // emptied, but still on disk
        QTRY_COMPARE(sorted(f.changes()),
                     (QStringList{u"delete:Inne/c.md@external"_s, u"delete:Projekt/Etap/a.md@external"_s,
                                  u"delete:Projekt/b.md@external"_s}));
        QTRY_COMPARE(f.folders(), QStringList{u"Inne"_s});
    }

    void startWatchingIsIdempotentAndSurvivesReopen()
    {
        Fixture f;
        QVERIFY(f.open());
        f.vault->startWatching();
        f.vault->startWatching();
        QVERIFY(f.vault->isWatching());
        QVERIFY(f.vault->open());
        QVERIFY(f.vault->isWatching());
        f.write(u"po.md"_s, "po");
        QTRY_COMPARE(f.changes(), QStringList{u"create:po.md@external"_s});
    }

    void watchDelayCoalescesEvents()
    {
        Fixture f;
        QVERIFY(f.open());
        f.vault->setWatchDelay(300);
        f.vault->startWatching();
        f.write(u"a.md"_s, "a");
        QTest::qWait(50);
        f.write(u"b.md"_s, "b");
        QTRY_COMPARE(f.changes().size(), 2);
        QCOMPARE(f.spy->count(), 1); // one batch
    }
};

QTEST_GUILESS_MAIN(TestVaultWatch)
#include "tst_vault_watch.moc"
