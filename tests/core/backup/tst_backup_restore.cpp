// A restore (backup/restore.h): the pure plan — what changes, comes back and goes to the trash —, the
// check of a backup against a real notebook, and applying the plan through the Vault: bytes exact,
// identical files untouched, History and the trash make it reversible, and the sync's mass-deletion
// guard still stands between a restore's deletions and the server.
#include "backuptest.h"
#include "synctest.h"

#include <QSet>
#include <QTemporaryDir>

using namespace backuptest;

namespace {

ArchiveEntry fileEntry(const QString &path, qint64 size, quint32 crc, int index = 0)
{
    ArchiveEntry entry;
    entry.path = path;
    entry.index = index;
    entry.size = size;
    entry.compressedSize = size;
    entry.crc32 = crc;
    return entry;
}
ArchiveEntry folderEntry(const QString &path)
{
    ArchiveEntry entry;
    entry.path = path;
    entry.folder = true;
    return entry;
}
NotebookFile file(const QString &path, qint64 size)
{
    return NotebookFile{path, u"/x/"_s + path, size};
}
QStringList targets(const QList<PlannedWrite> &writes)
{
    QStringList out;
    for (const PlannedWrite &write : writes)
        out.append(write.target);
    return out;
}

/// The vault's files as a list of paths.
QStringList pathsOf(const Vault &vault)
{
    QStringList out;
    for (const FileStat &stat : vault.files())
        out.append(stat.path);
    return out;
}

} // namespace

class TestBackupRestore : public QObject {
    Q_OBJECT

    QThreadPool m_threads;

    /// A backup of what `vault` holds now, at `name` in `folder`.
    QString backupOf(const Vault &vault, const QString &folder,
                     const QString &name = u"Brulion-N-2026-11-02-143005.zip"_s)
    {
        WriteOptions options;
        options.targetPath = QDir(folder).filePath(name);
        options.createFolder = true;
        const WriteResult result = runWriter(poolOn(&m_threads), collectSources(vault), options);
        if (!result.ok()) {
            qWarning() << result.detail;
            return {};
        }
        return result.path;
    }

    RestoreResult applyTo(Vault &vault, const notebook::WorkerPool &pool, const RestorePlan &plan, const QString &zip)
    {
        RestoreApplier applier(vault, pool);
        QSignalSpy spy(&applier, &RestoreApplier::finished);
        applier.start(plan, zip);
        if (!spy.wait(60000)) {
            RestoreResult timedOut;
            timedOut.detail = u"timed out"_s;
            return timedOut;
        }
        return spy.first().first().value<RestoreResult>();
    }

private Q_SLOTS:
    void plan_data()
    {
        QTest::addColumn<QList<ArchiveEntry>>("entries");
        QTest::addColumn<QList<NotebookFile>>("current");
        QTest::addColumn<QStringList>("change");
        QTest::addColumn<QStringList>("recreate");
        QTest::addColumn<QStringList>("trash");
        QTest::addColumn<int>("identical");

        QTest::newRow("identical bytes are skipped") << QList{fileEntry(u"a.md"_s, 3, 11)} << QList{file(u"a.md"_s, 3)}
                                                     << QStringList() << QStringList() << QStringList() << 1;
        QTest::newRow("another size changes") << QList{fileEntry(u"a.md"_s, 3, 11)} << QList{file(u"a.md"_s, 4)}
                                              << QStringList{u"a.md"_s} << QStringList() << QStringList() << 0;
        QTest::newRow("the same size, another checksum changes")
                << QList{fileEntry(u"a.md"_s, 3, 99)} << QList{file(u"a.md"_s, 3)} << QStringList{u"a.md"_s}
                << QStringList() << QStringList() << 0;
        QTest::newRow("gone comes back, extra goes")
                << QList{fileEntry(u"Żółw.md"_s, 3, 11)} << QList{file(u"nowa.md"_s, 3)} << QStringList()
                << QStringList{u"Żółw.md"_s} << QStringList{u"nowa.md"_s} << 0;
        QTest::newRow("settings are never trashed")
                << QList{fileEntry(u"a.md"_s, 3, 11)} << QList{file(u"a.md"_s, 3), file(u".brulion/config.json"_s, 2)}
                << QStringList() << QStringList() << QStringList() << 1;
        QTest::newRow("settings are restored when the backup has them")
                << QList{fileEntry(u".brulion/config.json"_s, 3, 99)} << QList{file(u".brulion/config.json"_s, 2)}
                << QStringList{u".brulion/config.json"_s} << QStringList() << QStringList() << 0;
        QTest::newRow("letter case: the notebook's spelling, not a second file")
                << QList{fileEntry(u"Dom.md"_s, 3, 99)} << QList{file(u"dom.md"_s, 3)} << QStringList{u"dom.md"_s}
                << QStringList() << QStringList() << 0;
        QTest::newRow("twins differing in letter case stay apart")
                << QList{fileEntry(u"Dom.md"_s, 3, 11)} << QList{file(u"Dom.md"_s, 3), file(u"dom.md"_s, 3)}
                << QStringList() << QStringList() << QStringList{u"dom.md"_s} << 1;
        QTest::newRow("an empty backup empties the notebook")
                << QList<ArchiveEntry>() << QList{file(u"a.md"_s, 1)} << QStringList() << QStringList()
                << QStringList{u"a.md"_s} << 0;
    }
    void plan()
    {
        QFETCH(QList<ArchiveEntry>, entries);
        QFETCH(QList<NotebookFile>, current);
        QFETCH(QStringList, change);
        QFETCH(QStringList, recreate);
        QFETCH(QStringList, trash);
        QFETCH(int, identical);
        QHash<VaultPath, quint32> crcs;
        for (const NotebookFile &f : current)
            crcs.insert(f.path, 11);
        const RestorePlan result = planRestore(entries, current, {}, crcs);
        QCOMPARE(targets(result.toChange), change);
        QCOMPARE(targets(result.toRecreate), recreate);
        QCOMPARE(result.toTrash, trash);
        QCOMPARE(result.identical, identical);
    }

    void planWithoutAChecksumCountsAsChanged()
    {
        // The file could not be read for its checksum.
        const RestorePlan result = planRestore({fileEntry(u"a.md"_s, 3, 11)}, {file(u"a.md"_s, 3)}, {}, {});
        QCOMPARE(targets(result.toChange), QStringList{u"a.md"_s});
    }

    void planBytesAndLimit()
    {
        const RestorePlan result = planRestore({fileEntry(u"a.md"_s, 10, 1), fileEntry(u"b.md"_s, 20, 1),
                                                fileEntry(u"film.mp4"_s, 5000, 1), fileEntry(u"nowy.mp4"_s, 5000, 1)},
                                               {file(u"a.md"_s, 3), file(u"film.mp4"_s, 7)}, {}, {}, 100);
        QCOMPARE(result.bytesToWrite, qint64(30));
        QCOMPARE(result.tooLarge, (QStringList{u"film.mp4"_s, u"nowy.mp4"_s}));
        QVERIFY(result.toTrash.isEmpty()); // what could not be restored stays
    }

    void planFolders()
    {
        const RestorePlan result =
                planRestore({folderEntry(u"Puste"_s), folderEntry(u"Już jest"_s)}, {}, {u"Już jest"_s}, {});
        QCOMPARE(result.foldersToCreate, QStringList{u"Puste"_s});
        QVERIFY(result.hasChanges());
    }

    void planBlockers()
    {
        // The backup has a file where the notebook has a folder, a folder where it has a file.
        const RestorePlan result = planRestore({fileEntry(u"a"_s, 1, 1), fileEntry(u"b/c.md"_s, 1, 1)},
                                               {file(u"a/x.md"_s, 1), file(u"b"_s, 1)}, {u"a"_s}, {});
        QCOMPARE(result.removeFirst, (QStringList{u"a"_s, u"b"_s}));
        QCOMPARE(result.toTrash, (QStringList{u"a/x.md"_s, u"b"_s}));
        QCOMPARE(targets(result.toRecreate), (QStringList{u"a"_s, u"b/c.md"_s}));
    }

    void modes_data()
    {
        QTest::addColumn<bool>("threads");
        QTest::newRow("worker pool") << true;
        QTest::newRow("slices") << false;
    }

    void roundTrip_data() { modes_data(); }
    void roundTrip()
    {
        QFETCH(bool, threads);
        const notebook::WorkerPool pool = poolOn(threads ? &m_threads : nullptr);
        QTemporaryDir dir;
        const QString root = dir.filePath(u"N"_s);
        const QByteArray crlf = "- zażółć\r\n- gęślą\r\n";
        const QByteArray bom = "\xEF\xBB\xBF- z BOM\n";
        const QByteArray binary("\x89PNG\r\n\x1a\n\0\0\0\rIHDR", 16);
        const QByteArray config = "{\"journalsFolder\": \"dziennik\"}\n";
        writeFile(root + u"/Żółw.md"_s, crlf);
        writeFile(root + u"/BOM.md"_s, bom);
        writeFile(root + u"/assets/obrazek.png"_s, binary);
        writeFile(root + u"/Bez zmian.md"_s, "tak samo");
        writeFile(root + u"/.brulion/config.json"_s, config);
        QDir().mkpath(root + u"/Pusty folder"_s);

        QTemporaryDir state;
        Vault vault(root, state.path());
        QVERIFY(vault.open());
        const QString zip = backupOf(vault, dir.filePath(u"kopie"_s));
        QVERIFY(!zip.isEmpty());

        // The notebook moves on: edited, deleted, added, folder removed.
        QVERIFY(vault.writeBinary(u"Żółw.md"_s, "- nowy tekst\n"));
        QVERIFY(vault.remove(u"BOM.md"_s));
        QVERIFY(vault.writeBinary(u"assets/obrazek.png"_s, "inne bajty"));
        QVERIFY(vault.writeText(u"Dodana później.md"_s, u"- po kopii\n"_s));
        QVERIFY(vault.writeBinary(u".brulion/config.json"_s, "{}\n"));
        QVERIFY(QDir(root + u"/Pusty folder"_s).removeRecursively());
        vault.rescan();
        const qint64 untouchedStamp = vault.stat(u"Bez zmian.md"_s)->mtime;

        const RestoreCheck check = runChecker(pool, zip, notebookFiles(vault), notebookFolders(vault));
        QVERIFY2(check.ok(), qPrintable(check.detail));
        const RestorePlan &plan = check.plan;
        QCOMPARE(targets(plan.toChange),
                 (QStringList{u".brulion/config.json"_s, u"assets/obrazek.png"_s, u"Żółw.md"_s}));
        QCOMPARE(targets(plan.toRecreate), QStringList{u"BOM.md"_s});
        QCOMPARE(plan.toTrash, QStringList{u"Dodana później.md"_s});
        QCOMPARE(plan.foldersToCreate, QStringList{u"Pusty folder"_s});
        QCOMPARE(plan.identical, 1);

        QSignalSpy config_(&vault, &Vault::configChanged);
        QList<VaultChange> changes;
        connect(&vault, &Vault::changed, this, [&](const QList<VaultChange> &list) { changes += list; });
        const RestoreResult result = applyTo(vault, pool, plan, zip);
        QVERIFY2(result.ok(), qPrintable(result.detail + result.failed.join(u", ")));
        QCOMPARE(result.changed, 3);
        QCOMPARE(result.recreated, 1);
        QCOMPARE(result.trashed, 1);
        QCOMPARE(result.foldersCreated, 1);

        QCOMPARE(readFile(root + u"/Żółw.md"_s), crlf);
        QCOMPARE(readFile(root + u"/BOM.md"_s), bom);
        QCOMPARE(readFile(root + u"/assets/obrazek.png"_s), binary);
        QCOMPARE(readFile(root + u"/.brulion/config.json"_s), config);
        QVERIFY(QFileInfo(root + u"/Pusty folder"_s).isDir());
        QVERIFY(!QFileInfo::exists(root + u"/Dodana później.md"_s));
        QCOMPARE(vault.stat(u"Bez zmian.md"_s)->mtime, untouchedStamp);
        // The settings were taken in, as for any other write of the file.
        QVERIFY(config_.count() >= 1);
        QCOMPARE(vault.config().journalsFolder, u"dziennik"_s);

        // Reversible: the trash has what left, History what was replaced, and nothing else moved.
        QVERIFY(std::any_of(vault.trash().cbegin(), vault.trash().cend(),
                            [](const FileStat &f) { return f.path.contains(u"Dodana później"_s); }));
        const QList<HistoryEntry> versions = vault.history()->list(u"Żółw.md"_s);
        QVERIFY(!versions.isEmpty());
        QCOMPARE(vault.history()->readBytes(versions.first().id), QByteArray("- nowy tekst\n"));
        QCOMPARE(versions.first().reason, u"import"_s);
        for (const VaultChange &change : std::as_const(changes))
            QCOMPARE(change.source, ChangeSource::Import);

        // Restoring the same backup again finds nothing to do.
        const RestoreCheck again = runChecker(pool, zip, notebookFiles(vault), notebookFolders(vault));
        QVERIFY2(again.ok(), qPrintable(again.detail));
        QVERIFY(!again.plan.hasChanges());
        QCOMPARE(again.plan.identical, 5);
    }

    void manyFilesAcrossBatches_data() { modes_data(); }
    void manyFilesAcrossBatches()
    {
        QFETCH(bool, threads);
        const notebook::WorkerPool pool = poolOn(threads ? &m_threads : nullptr);
        QTemporaryDir dir;
        const QString root = dir.filePath(u"N"_s);
        for (int i = 0; i < 200; ++i)
            writeFile(root + u"/Folder %1/Notatka ż %2.md"_s.arg(i % 7).arg(i), "treść " + QByteArray::number(i));
        Vault vault(root);
        QVERIFY(vault.open());
        const QString zip = backupOf(vault, dir.filePath(u"kopie"_s));
        for (int i = 0; i < 200; ++i)
            QVERIFY(QFile::remove(root + u"/Folder %1/Notatka ż %2.md"_s.arg(i % 7).arg(i)));
        vault.rescan();
        const RestoreCheck check = runChecker(pool, zip, notebookFiles(vault), notebookFolders(vault));
        QVERIFY2(check.ok(), qPrintable(check.detail));
        QCOMPARE(check.plan.toRecreate.size(), 200);
        const RestoreResult result = applyTo(vault, pool, check.plan, zip);
        QVERIFY2(result.ok(), qPrintable(result.detail));
        QCOMPARE(result.recreated, 200);
        for (int i = 0; i < 200; ++i)
            QCOMPARE(readFile(root + u"/Folder %1/Notatka ż %2.md"_s.arg(i % 7).arg(i)),
                     "treść " + QByteArray::number(i));
    }

    void blockersGoFirst()
    {
        QTemporaryDir dir;
        const QString root = dir.filePath(u"N"_s);
        writeFile(root + u"/a"_s, "plik a");
        writeFile(root + u"/b/c.md"_s, "plik c");
        Vault vault(root);
        QVERIFY(vault.open());
        const QString zip = backupOf(vault, dir.filePath(u"kopie"_s));
        QVERIFY(vault.remove(u"a"_s));
        QVERIFY(vault.remove(u"b"_s));
        QVERIFY(vault.writeText(u"a/teraz folder.md"_s, u"x"_s));
        QVERIFY(vault.writeText(u"b"_s, u"teraz plik"_s));
        const RestoreCheck check = runChecker(poolOn(nullptr), zip, notebookFiles(vault), notebookFolders(vault));
        QVERIFY2(check.ok(), qPrintable(check.detail));
        const RestoreResult result = applyTo(vault, poolOn(nullptr), check.plan, zip);
        QVERIFY2(result.ok(), qPrintable(result.detail + result.failed.join(u", ")));
        QCOMPARE(readFile(root + u"/a"_s), QByteArray("plik a"));
        QCOMPARE(readFile(root + u"/b/c.md"_s), QByteArray("plik c"));
    }

    void aDamagedBackupChangesNothing_data() { modes_data(); }
    void aDamagedBackupChangesNothing()
    {
        QFETCH(bool, threads);
        QTemporaryDir dir;
        const QString root = dir.filePath(u"N"_s);
        writeFile(root + u"/a.md"_s, "stara treść");
        Vault vault(root);
        QVERIFY(vault.open());
        const QString zip = dir.filePath(u"z.zip"_s);
        QByteArray bytes = rawZip({{"a.md", QByteArray(1000, 'x')}, {"b.md", "nowy"}}, 0);
        bytes[bytes.indexOf(QByteArray(1000, 'x')) + 10] = 'y';
        writeFile(zip, bytes);
        const RestoreCheck check =
                runChecker(poolOn(threads ? &m_threads : nullptr), zip, notebookFiles(vault), notebookFolders(vault));
        QCOMPARE(check.problem, ArchiveProblem::Damaged);
        QCOMPARE(readFile(root + u"/a.md"_s), QByteArray("stara treść"));
    }

    void hostileBackupIsRefusedBeforeAnything_data()
    {
        QTest::addColumn<QByteArray>("name");
        QTest::newRow("zip slip") << QByteArray("../poza.md");
        QTest::newRow("into the trash") << QByteArray(".trash/x.md");
        QTest::newRow("into git") << QByteArray(".git/hooks/pre-commit");
    }
    void hostileBackupIsRefusedBeforeAnything()
    {
        QFETCH(QByteArray, name);
        QTemporaryDir dir;
        const QString root = dir.filePath(u"N/głęboko"_s);
        writeFile(root + u"/a.md"_s, "a");
        Vault vault(root);
        QVERIFY(vault.open());
        const QString zip = dir.filePath(u"z.zip"_s);
        writeFile(zip, rawZip({{"dobry.md", "dobry"}, {name, "zły"}}));
        const RestoreCheck check = runChecker(poolOn(nullptr), zip, notebookFiles(vault), notebookFolders(vault));
        QVERIFY(!check.ok());
        QVERIFY(check.plan.toRecreate.isEmpty());
        QVERIFY(!QFileInfo::exists(dir.filePath(u"N/poza.md"_s)));
        QVERIFY(!QFileInfo::exists(root + u"/dobry.md"_s));
    }

    void aBackupSwappedAfterTheCheckIsRefused()
    {
        QTemporaryDir dir;
        const QString root = dir.filePath(u"N"_s);
        writeFile(root + u"/a.md"_s, "stara");
        Vault vault(root);
        QVERIFY(vault.open());
        const QString zip = dir.filePath(u"z.zip"_s);
        writeFile(zip, rawZip({{"a.md", "z kopii"}}));
        const RestoreCheck check = runChecker(poolOn(nullptr), zip, notebookFiles(vault), notebookFolders(vault));
        QVERIFY(check.ok());
        writeFile(zip, rawZip({{"a.md", "podmienione"}}));
        const RestoreResult result = applyTo(vault, poolOn(nullptr), check.plan, zip);
        QVERIFY(!result.ok());
        QCOMPARE(readFile(root + u"/a.md"_s), QByteArray("stara"));
    }

    void aFailedWriteTrashesNothing()
    {
        if (!brulion::test::hasUnixPermissions())
            QSKIP("no Unix file modes here");
        QTemporaryDir dir;
        const QString root = dir.filePath(u"N"_s);
        writeFile(root + u"/Zamknięty/a.md"_s, "stara");
        writeFile(root + u"/dodatkowy.md"_s, "został");
        Vault vault(root);
        QVERIFY(vault.open());
        const QString zip = dir.filePath(u"z.zip"_s);
        writeFile(zip, rawZip({{"Zamknięty/a.md", "nowa"}, {"b.md", "b"}}));
        const RestoreCheck check = runChecker(poolOn(nullptr), zip, notebookFiles(vault), notebookFolders(vault));
        QVERIFY2(check.ok(), qPrintable(check.detail));
        QFile::setPermissions(root + u"/Zamknięty"_s, QFileDevice::ReadOwner | QFileDevice::ExeOwner);
        if (QFileInfo(root + u"/Zamknięty"_s).isWritable())
            QSKIP("this user can write everywhere");
        const RestoreResult result = applyTo(vault, poolOn(nullptr), check.plan, zip);
        QFile::setPermissions(root + u"/Zamknięty"_s,
                              QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner);
        QCOMPARE(result.failed, QStringList{u"Zamknięty/a.md"_s});
        QVERIFY(result.trashSkipped);
        QCOMPARE(result.recreated, 1); // the rest of what the backup holds is back
        QCOMPARE(readFile(root + u"/dodatkowy.md"_s), QByteArray("został"));
    }

    void cancelStopsApplying()
    {
        QTemporaryDir dir;
        const QString root = dir.filePath(u"N"_s);
        Vault vault(root);
        QVERIFY(vault.open());
        const QString zip = dir.filePath(u"z.zip"_s);
        QList<QPair<QByteArray, QByteArray>> entries;
        for (int i = 0; i < 300; ++i)
            entries.append({"plik" + QByteArray::number(i) + ".md", "treść"});
        writeFile(zip, rawZip(entries));
        const RestoreCheck check = runChecker(poolOn(nullptr), zip, notebookFiles(vault), notebookFolders(vault));
        QVERIFY2(check.ok(), qPrintable(check.detail));
        RestoreApplier applier(vault, poolOn(nullptr));
        QSignalSpy spy(&applier, &RestoreApplier::finished);
        applier.start(check.plan, zip);
        QVERIFY(applier.isRunning());
        applier.cancel();
        QVERIFY(spy.wait(60000));
        QVERIFY(spy.first().first().value<RestoreResult>().cancelled);
        QVERIFY(!applier.isRunning());
        QVERIFY(vault.files().size() < 300);
    }

    void aMassDeletionByARestoreIsHeldBackBySync()
    {
        // A backup that holds little restored into a notebook that grew: the deletions reach the
        // sync as any other local ones, and its guard asks the user before the server loses them.
        synctest::FakeWebDavServer server;
        QMap<QString, QByteArray> notes;
        for (int i = 0; i < 20; ++i)
            notes.insert(u"Notatka %1.md"_s.arg(i), "- treść\n");
        synctest::Device device(server, u"telefon"_s, notes);
        QCOMPARE(device.sync().uploaded, 20);

        QTemporaryDir dir;
        const QString zip = dir.filePath(u"z.zip"_s);
        writeFile(zip, rawZip({{"Notatka 0.md", "- treść\n"}}));
        const RestoreCheck check =
                runChecker(poolOn(nullptr), zip, notebookFiles(*device.vault), notebookFolders(*device.vault));
        QVERIFY2(check.ok(), qPrintable(check.detail));
        QCOMPARE(check.plan.toTrash.size(), 19);
        const RestoreResult result = applyTo(*device.vault, poolOn(nullptr), check.plan, zip);
        QVERIFY2(result.ok(), qPrintable(result.detail));
        QCOMPARE(pathsOf(*device.vault).size(), 1);

        const auto report = device.sync();
        QVERIFY(report.needsConfirmation);
        QCOMPARE(report.deletedRemote, 0);
    }
};

QTEST_GUILESS_MAIN(TestBackupRestore)
#include "tst_backup_restore.moc"
