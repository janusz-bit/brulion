// The streamed ZIP (backup/archive.h): every byte comes back exactly, a run on a worker pool and one in
// slices give the same files, files changing under the writer never tear an entry, a failed or
// cancelled run leaves nothing behind, and malformed or hostile archives are refused.
#include "backuptest.h"
#include "filesystemtraits.h"

#include "vault/vault.h"

#include <QRandomGenerator>
#include <QTemporaryDir>

using namespace backuptest;

namespace {

const QDateTime Now(QDate(2026, 11, 2), QTime(14, 30, 5));

/// The notes of the round trip: BOM, CRLF, stray CR, binary, empty, Polish names, deep folders.
QList<QPair<QString, QByteArray>> sampleFiles()
{
    QByteArray binary(70'000, '\0');
    for (qsizetype i = 0; i < binary.size(); ++i)
        binary[i] = char((i * 7 + i / 251) & 0xff);
    QByteArray random(300'000, '\0');
    QRandomGenerator generator(42);
    for (qsizetype i = 0; i < random.size(); ++i)
        random[i] = char(generator.bounded(256));
    return {
            {u"Żółw ąćęłńóśźż.md"_s, "- zażółć gęślą jaźń\n"},
            {u"BOM.md"_s, "\xEF\xBB\xBF- z BOM\n"},
            {u"Windows.md"_s, "- a\r\n- b\r\n"},
            {u"Mieszane.md"_s, "- a\r- b\n- c\r\n\n\n"},
            {u"Pusty.md"_s, ""},
            {u"Bez końca wiersza.md"_s, "x"},
            {u"Dwa.md"_s, "ab"},
            {u"Folder/Głębiej/jeszcze głębiej/notatka.md"_s, "- głęboko\n"},
            {u"assets/obrazek.png"_s, binary},
            {u"assets/losowe.bin"_s, random},
            {u"assets/film z dźwiękiem.mp4"_s, random.left(10'000)},
            {u".brulion/config.json"_s, "{\"journalsFolder\": \"dziennik\"}\n"},
            {u"Spotkanie 10:30.md"_s, "- dwukropek w środku nazwy\n"},
    };
}

QStringList entryNames(const QString &zipPath)
{
    ArchiveReader reader(zipPath);
    QStringList out;
    const ArchiveInfo info = reader.readEntries();
    for (const ArchiveEntry &entry : info.entries)
        out.append(entry.path + (entry.folder ? u"/"_s : QString()));
    out.sort();
    return out;
}

} // namespace

class TestBackupArchive : public QObject {
    Q_OBJECT

    QThreadPool m_threads;

private Q_SLOTS:
    void modes_data()
    {
        QTest::addColumn<bool>("threads");
        QTest::newRow("worker pool") << true;
        QTest::newRow("slices") << false;
    }

    void roundTripIsByteExact_data() { modes_data(); }
    void roundTripIsByteExact()
    {
        QFETCH(bool, threads);
        QTemporaryDir dir;
        const QString root = dir.filePath(u"Notatnik"_s);
        const auto files = sampleFiles();
        for (const auto &[path, bytes] : files)
            writeFile(root + u'/' + path, bytes);
        QDir().mkpath(root + u"/Pusty folder/w środku"_s);
        writeFile(root + u"/.trash/usunięty.md"_s, "nie");
        writeFile(root + u"/.git/HEAD"_s, "nie");

        Vault vault(root);
        QVERIFY(vault.open());
        const Sources sources = collectSources(vault);

        WriteOptions options;
        options.targetPath = dir.filePath(u"kopie/Brulion-Notatnik-2026-11-02-143005.zip"_s);
        options.createFolder = true;
        const WriteResult result = runWriter(poolOn(threads ? &m_threads : nullptr), sources, options);
        QVERIFY2(result.ok(), qPrintable(result.detail));
        QCOMPARE(result.path, options.targetPath);
        QCOMPARE(result.bytes, QFileInfo(options.targetPath).size());
        QCOMPARE(result.files, files.size());
        QVERIFY(result.skipped.isEmpty());
        QCOMPARE(QDir(dir.filePath(u"kopie"_s)).entryList(QDir::Files | QDir::Hidden).size(), 1);

        ArchiveReader reader(options.targetPath);
        const ArchiveInfo info = reader.readEntries();
        QVERIFY2(info.ok(), qPrintable(info.detail));
        QCOMPARE(info.entries.size(), files.size() + 1);
        QHash<QString, ArchiveEntry> byPath;
        for (const ArchiveEntry &entry : info.entries)
            byPath.insert(entry.path, entry);
        QVERIFY(byPath.value(u"Pusty folder/w środku"_s).folder);
        QVERIFY(!byPath.contains(u".trash/usunięty.md"_s));
        QVERIFY(!byPath.contains(u".git/HEAD"_s));
        for (const auto &[path, bytes] : files) {
            QVERIFY2(byPath.contains(path), qPrintable(path));
            const ArchiveEntry entry = byPath.value(path);
            QVERIFY(reader.verify(entry));
            const std::optional<QByteArray> read = reader.read(entry);
            QVERIFY2(read.has_value(), qPrintable(path));
            QCOMPARE(*read, bytes);
            QCOMPARE(fileCrc32(root + u'/' + path), std::optional<quint32>(entry.crc32));
        }
        // Text is deflated, a film is stored as it is.
        QVERIFY(byPath.value(u"assets/obrazek.png"_s).compressedSize == byPath.value(u"assets/obrazek.png"_s).size);
        QVERIFY(byPath.value(u"assets/film z dźwiękiem.mp4"_s).compressedSize == 10'000);
    }

    void aLargeFileIsStreamed_data() { modes_data(); }
    void aLargeFileIsStreamed()
    {
        QFETCH(bool, threads);
        QTemporaryDir dir;
        const QString root = dir.filePath(u"N"_s);
        QByteArray chunk(1 << 20, 'a');
        QDir().mkpath(root);
        {
            QFile file(root + u"/duży.txt"_s);
            QVERIFY(file.open(QIODevice::WriteOnly));
            for (int i = 0; i < 12; ++i) {
                chunk[0] = char(i);
                file.write(chunk);
            }
        }
        Vault vault(root);
        QVERIFY(vault.open());
        WriteOptions options;
        options.targetPath = dir.filePath(u"Brulion-N-2026-11-02-143005.zip"_s);
        qint64 reads = 0;
        options.onRead = [&reads](const VaultPath &, qint64) { ++reads; };
        const WriteResult result = runWriter(poolOn(threads ? &m_threads : nullptr), collectSources(vault), options);
        QVERIFY2(result.ok(), qPrintable(result.detail));
        QVERIFY2(reads > 100, "read in many small pieces, never whole");
        ArchiveReader reader(options.targetPath);
        const ArchiveInfo info = reader.readEntries();
        QCOMPARE(info.entries.size(), 1);
        QCOMPARE(info.entries.first().size, qint64(12) << 20);
        QVERIFY(reader.verify(info.entries.first()));
        // In slices (no threads) a big file is stored, so a slice stays a moment.
        if (!threads)
            QCOMPARE(info.entries.first().compressedSize, info.entries.first().size);
        else
            QVERIFY(info.entries.first().compressedSize < info.entries.first().size / 10);
    }

    void aFileChangingDuringTheRunIsReadAgain_data()
    {
        QTest::addColumn<QByteArray>("first");
        QTest::addColumn<QByteArray>("second");
        const QByteArray big(300'000, 'x');
        QTest::newRow("it grows") << big << big + "dopisane";
        QTest::newRow("it shrinks") << big << QByteArray("krótki");
        QTest::newRow("same size, other bytes") << big << QByteArray(300'000, 'y');
        QTest::newRow("emptied") << big << QByteArray();
    }
    void aFileChangingDuringTheRunIsReadAgain()
    {
        QFETCH(QByteArray, first);
        QFETCH(QByteArray, second);
        QTemporaryDir dir;
        const QString root = dir.filePath(u"N"_s);
        writeFile(root + u"/edytowana.md"_s, first);
        writeFile(root + u"/po niej.md"_s, "następna");
        Vault vault(root);
        QVERIFY(vault.open());
        const QString path = root + u"/edytowana.md"_s;
        const QDateTime stamp = QFileInfo(path).lastModified();
        WriteOptions options;
        options.targetPath = dir.filePath(u"Brulion-N-2026-11-02-143005.zip"_s);
        bool changed = false;
        options.onRead = [&](const VaultPath &read, qint64 offset) {
            if (read == u"edytowana.md" && offset > 0 && !changed) {
                changed = true;
                // Rewritten in place, as some editors do (an atomic rename never tears a read).
                QFile file(path);
                if (file.open(QIODevice::WriteOnly)) {
                    file.write(second);
                    file.close();
                }
                QFile again(path);
                if (again.open(QIODevice::ReadWrite)) {
                    again.setFileTime(stamp.addSecs(5), QFileDevice::FileModificationTime);
                }
            }
        };
        const WriteResult result = runWriter(poolOn(nullptr), collectSources(vault), options);
        QVERIFY2(result.ok(), qPrintable(result.detail));
        QVERIFY(changed);
        QVERIFY(result.skipped.isEmpty());
        QCOMPARE(result.files, 2);
        ArchiveReader reader(options.targetPath);
        const ArchiveInfo info = reader.readEntries();
        QVERIFY2(info.ok(), qPrintable(info.detail));
        QCOMPARE(info.entries.size(), 2);
        for (const ArchiveEntry &entry : info.entries) {
            QVERIFY(reader.verify(entry));
            QCOMPARE(*reader.read(entry), entry.path == u"edytowana.md" ? second : QByteArray("następna"));
        }
    }

    void aFileThatKeepsChangingIsReportedNotTorn()
    {
        QTemporaryDir dir;
        const QString root = dir.filePath(u"N"_s);
        writeFile(root + u"/log.txt"_s, QByteArray(200'000, 'x'));
        writeFile(root + u"/spokojna.md"_s, "ok");
        Vault vault(root);
        QVERIFY(vault.open());
        WriteOptions options;
        options.targetPath = dir.filePath(u"Brulion-N-2026-11-02-143005.zip"_s);
        options.onRead = [&](const VaultPath &read, qint64 offset) {
            if (read == u"log.txt" && offset > 0) {
                QFile file(root + u"/log.txt"_s);
                if (file.open(QIODevice::Append))
                    file.write("dopisek");
            }
        };
        const WriteResult result = runWriter(poolOn(nullptr), collectSources(vault), options);
        QVERIFY2(result.ok(), qPrintable(result.detail));
        QCOMPARE(result.skipped, QStringList{u"log.txt"_s});
        QCOMPARE(entryNames(options.targetPath), QStringList{u"spokojna.md"_s});
        ArchiveReader reader(options.targetPath);
        QVERIFY(reader.verify(reader.readEntries().entries.first()));
    }

    void aFileGoneMeanwhileIsNoFailure()
    {
        QTemporaryDir dir;
        const QString root = dir.filePath(u"N"_s);
        writeFile(root + u"/a.md"_s, "a");
        writeFile(root + u"/b.md"_s, "b");
        Vault vault(root);
        QVERIFY(vault.open());
        const Sources sources = collectSources(vault);
        QVERIFY(QFile::remove(root + u"/b.md"_s));
        WriteOptions options;
        options.targetPath = dir.filePath(u"Brulion-N-2026-11-02-143005.zip"_s);
        const WriteResult result = runWriter(poolOn(nullptr), sources, options);
        QVERIFY2(result.ok(), qPrintable(result.detail));
        QCOMPARE(result.vanished, 1);
        QVERIFY(result.skipped.isEmpty());
        QCOMPARE(entryNames(options.targetPath), QStringList{u"a.md"_s});
    }

    void anUnreadableFileIsReported()
    {
        if (!brulion::test::hasUnixPermissions())
            QSKIP("no Unix file modes here");
        QTemporaryDir dir;
        const QString root = dir.filePath(u"N"_s);
        writeFile(root + u"/tajne.md"_s, "a");
        writeFile(root + u"/jawne.md"_s, "b");
        Vault vault(root);
        QVERIFY(vault.open());
        QFile::setPermissions(root + u"/tajne.md"_s, QFileDevice::Permissions{});
        if (QFile file(root + u"/tajne.md"_s); file.open(QIODevice::ReadOnly))
            QSKIP("this user can read everything");
        WriteOptions options;
        options.targetPath = dir.filePath(u"Brulion-N-2026-11-02-143005.zip"_s);
        const WriteResult result = runWriter(poolOn(nullptr), collectSources(vault), options);
        QVERIFY2(result.ok(), qPrintable(result.detail));
        QCOMPARE(result.skipped, QStringList{u"tajne.md"_s});
        QCOMPARE(entryNames(options.targetPath), QStringList{u"jawne.md"_s});
    }

    void aNameABackupMayNotHoldIsLeftOutAndReported()
    {
        // Legal on Linux, a way out of the notebook elsewhere: never written, so that every backup
        // Brulion writes passes its own check.
        Sources sources;
        QTemporaryDir dir;
        writeFile(dir.filePath(u"plik"_s), "x");
        for (const QString &path : {u"back\\slash.md"_s, u"C:plik.md"_s, u"zwykły.md"_s})
            sources.files.append(SourceFile{path, dir.filePath(u"plik"_s), 1, 0});
        sources.totalBytes = 3;
        WriteOptions options;
        options.targetPath = dir.filePath(u"Brulion-N-2026-11-02-143005.zip"_s);
        const WriteResult result = runWriter(poolOn(nullptr), sources, options);
        QVERIFY2(result.ok(), qPrintable(result.detail));
        QCOMPARE(result.skipped, (QStringList{u"back\\slash.md"_s, u"C:plik.md"_s}));
        QCOMPARE(entryNames(options.targetPath), QStringList{u"zwykły.md"_s});
    }

    void cancelLeavesNothing_data() { modes_data(); }
    void cancelLeavesNothing()
    {
        QFETCH(bool, threads);
        QTemporaryDir dir;
        const QString root = dir.filePath(u"N"_s);
        for (int i = 0; i < 20; ++i)
            writeFile(root + u"/plik %1.md"_s.arg(i), QByteArray(100'000, 'x'));
        Vault vault(root);
        QVERIFY(vault.open());
        const QString folder = dir.filePath(u"kopie"_s);
        QDir().mkpath(folder);
        writeFile(folder + u"/Brulion-N-2026-10-01-080000.zip"_s, "starsza");

        BackupWriter writer(poolOn(threads ? &m_threads : nullptr));
        QSignalSpy spy(&writer, &BackupWriter::finished);
        WriteOptions options;
        options.targetPath = folder + u"/Brulion-N-2026-11-02-143005.zip"_s;
        std::atomic_int reads = 0;
        options.onRead = [&](const VaultPath &, qint64) {
            if (++reads == 5)
                writer.cancel(); // (only sets a flag: from any thread)
        };
        writer.start(collectSources(vault), options);
        QVERIFY(writer.isRunning());
        QVERIFY(spy.wait(60000));
        const WriteResult result = spy.first().first().value<WriteResult>();
        QCOMPARE(result.error, WriteError::Cancelled);
        QVERIFY(result.path.isEmpty());
        QVERIFY(!writer.isRunning());
        QCOMPARE(QDir(folder).entryList(QDir::Files | QDir::Hidden), QStringList{u"Brulion-N-2026-10-01-080000.zip"_s});
        QCOMPARE(readFile(folder + u"/Brulion-N-2026-10-01-080000.zip"_s), QByteArray("starsza"));
    }

    void destroyingAWriterWhileItRunsIsSafe()
    {
        QTemporaryDir dir;
        const QString root = dir.filePath(u"N"_s);
        for (int i = 0; i < 20; ++i)
            writeFile(root + u"/plik %1.md"_s.arg(i), QByteArray(50'000, 'x'));
        const QString folder = dir.filePath(u"kopie"_s);
        QDir().mkpath(folder);
        {
            Vault vault(root);
            QVERIFY(vault.open());
            BackupWriter writer(poolOn(&m_threads));
            WriteOptions options;
            options.targetPath = folder + u"/Brulion-N-2026-11-02-143005.zip"_s;
            writer.start(collectSources(vault), options);
        }
        m_threads.waitForDone();
        QCoreApplication::processEvents();
        QCOMPARE(QDir(folder).entryList(QDir::Files | QDir::Hidden), QStringList());
    }

    void aMissingFolderIsNotCreated()
    {
        QTemporaryDir dir;
        writeFile(dir.filePath(u"N/a.md"_s), "a");
        Vault vault(dir.filePath(u"N"_s));
        QVERIFY(vault.open());
        WriteOptions options;
        options.targetPath = dir.filePath(u"odpięty dysk/kopie/Brulion-N-2026-11-02-143005.zip"_s);
        const WriteResult result = runWriter(poolOn(nullptr), collectSources(vault), options);
        QCOMPARE(result.error, WriteError::FolderMissing);
        QVERIFY(!QFileInfo::exists(dir.filePath(u"odpięty dysk"_s)));
    }

    void anUnwritableFolderFailsAndKeepsOlderBackups()
    {
        if (!brulion::test::hasUnixPermissions())
            QSKIP("no Unix file modes here");
        QTemporaryDir dir;
        writeFile(dir.filePath(u"N/a.md"_s), "a");
        Vault vault(dir.filePath(u"N"_s));
        QVERIFY(vault.open());
        const QString folder = dir.filePath(u"kopie"_s);
        writeFile(folder + u"/Brulion-N-2026-10-01-080000.zip"_s, "starsza");
        QFile::setPermissions(folder, QFileDevice::ReadOwner | QFileDevice::ExeOwner);
        if (QFileInfo(folder).isWritable())
            QSKIP("this user can write everywhere");
        WriteOptions options;
        options.targetPath = folder + u"/Brulion-N-2026-11-02-143005.zip"_s;
        const WriteResult result = runWriter(poolOn(nullptr), collectSources(vault), options);
        QCOMPARE(result.error, WriteError::FolderUnwritable);
        QFile::setPermissions(folder, QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner);
        QCOMPARE(QDir(folder).entryList(QDir::Files | QDir::Hidden), QStringList{u"Brulion-N-2026-10-01-080000.zip"_s});
    }

    void aSizeLimitStopsTheRun()
    {
        QTemporaryDir dir;
        QByteArray random(200'000, '\0');
        QRandomGenerator generator(7);
        for (qsizetype i = 0; i < random.size(); ++i)
            random[i] = char(generator.bounded(256));
        writeFile(dir.filePath(u"N/losowe.bin"_s), random);
        Vault vault(dir.filePath(u"N"_s));
        QVERIFY(vault.open());
        WriteOptions options;
        options.targetPath = dir.filePath(u"Brulion-N-2026-11-02-143005.zip"_s);
        options.maxBytes = 50'000;
        const WriteResult result = runWriter(poolOn(nullptr), collectSources(vault), options);
        QCOMPARE(result.error, WriteError::TooLarge);
        QVERIFY(!QFileInfo::exists(options.targetPath));
        QCOMPARE(QDir(dir.path()).entryList(QDir::Files | QDir::Hidden), QStringList());
    }

    void workFolderKeepsTheTargetFolderClean()
    {
        // The browser: the temporary file outside the storage's mounts.
        QTemporaryDir dir;
        writeFile(dir.filePath(u"N/a.md"_s), "a");
        Vault vault(dir.filePath(u"N"_s));
        QVERIFY(vault.open());
        WriteOptions options;
        options.targetPath = dir.filePath(u"kopie/Brulion-N-2026-11-02-143005.zip"_s);
        options.createFolder = true;
        options.workFolder = dir.filePath(u"robocze"_s);
        const WriteResult result = runWriter(poolOn(nullptr), collectSources(vault), options);
        QVERIFY2(result.ok(), qPrintable(result.detail));
        QVERIFY(QFileInfo::exists(options.targetPath));
        QCOMPARE(QDir(options.workFolder).entryList(QDir::Files | QDir::Hidden), QStringList());
        QCOMPARE(entryNames(options.targetPath), QStringList{u"a.md"_s});
    }

    void entryPaths_data()
    {
        QTest::addColumn<QString>("path");
        QTest::addColumn<int>("expected");
        const int ok = int(EntryPath::Ok);
        const int unsafe = int(EntryPath::Unsafe);
        const int ignored = int(EntryPath::Ignored);
        QTest::newRow("a page") << u"Żółw.md"_s << ok;
        QTest::newRow("deep") << u"a/b c/d.png"_s << ok;
        QTest::newRow("settings") << u".brulion/config.json"_s << ok;
        QTest::newRow("a colon inside a name") << u"Spotkanie 10:30.md"_s << ok;
        QTest::newRow("dots inside a name") << u"a..b/c...md"_s << ok;
        QTest::newRow("empty") << QString() << unsafe;
        QTest::newRow("absolute") << u"/etc/passwd"_s << unsafe;
        QTest::newRow("parent") << u"../poza.md"_s << unsafe;
        QTest::newRow("parent inside") << u"a/../../poza.md"_s << unsafe;
        QTest::newRow("Windows parent with spaces") << u"a/.. /poza.md"_s << unsafe;
        QTest::newRow("Windows parent with dots") << u"a/.../poza.md"_s << unsafe;
        QTest::newRow("only a dot") << u"a/./b.md"_s << unsafe;
        QTest::newRow("backslash") << u"..\\poza.md"_s << unsafe;
        QTest::newRow("drive") << u"C:/Windows/x.md"_s << unsafe;
        QTest::newRow("drive-relative") << u"C:x.md"_s << unsafe;
        QTest::newRow("double slash") << u"a//b.md"_s << unsafe;
        QTest::newRow("newline") << u"a\nb.md"_s << unsafe;
        QTest::newRow("NUL") << u"a"_s + QChar(0) + u"b.md"_s << unsafe;
        QTest::newRow("DEL") << u"a\x7f.md"_s << unsafe;
        QTest::newRow("not NFC") << u"z\u0307o\u0301\u0142w.md"_s << unsafe;
        QTest::newRow("trash") << u".trash/x.md"_s << ignored;
        QTest::newRow("git") << u".git/config"_s << ignored;
        QTest::newRow("hooks in git, deeper") << u"a/.git/hooks/post-commit"_s << ignored;
        QTest::newRow("other device state") << u".brulion/secret.json"_s << ignored;
        QTest::newRow("temporary") << u"a/~syncthing~x.tmp"_s << ignored;
    }
    void entryPaths()
    {
        QFETCH(QString, path);
        QFETCH(int, expected);
        QCOMPARE(int(checkEntryPath(path)), expected);
    }

    void hostileArchives_data()
    {
        QTest::addColumn<QByteArray>("zip");
        QTest::addColumn<int>("problem");
        using Entries = QList<QPair<QByteArray, QByteArray>>;
        const auto zip = [](const Entries &entries) { return rawZip(entries); };
        // miniz itself refuses a leading "/": the name is patched in the finished bytes.
        QByteArray absolute = zip({{"xetc/passwd", "owned"}});
        absolute.replace("xetc/passwd", "/etc/passwd");
        QByteArray badUtf8 = zip({{"zzz.md", "x"}});
        badUtf8.replace("zzz", "\xC5\x41z");
        QByteArray nul = zip({{"a_b.md", "x"}});
        nul.replace("a_b", QByteArray("a\0b", 3));
        QTest::newRow("zip slip") << zip({{"../../.config/autostart/x.desktop", "owned"}})
                                  << int(ArchiveProblem::UnsafePath);
        QTest::newRow("zip slip, nested") << zip({{"ok/../../poza.md", "owned"}}) << int(ArchiveProblem::UnsafePath);
        QTest::newRow("zip slip, backslash") << zip({{"..\\..\\poza.md", "owned"}}) << int(ArchiveProblem::UnsafePath);
        QTest::newRow("absolute") << absolute << int(ArchiveProblem::UnsafePath);
        QTest::newRow("drive") << zip({{"C:/x.md", "owned"}}) << int(ArchiveProblem::UnsafePath);
        QTest::newRow("no UTF-8") << badUtf8 << int(ArchiveProblem::UnsafePath);
        QTest::newRow("NUL in a name") << nul << int(ArchiveProblem::UnsafePath);
        QTest::newRow("into the trash") << zip({{".trash/x.md", "owned"}}) << int(ArchiveProblem::IgnoredPath);
        QTest::newRow("into git") << zip({{".git/hooks/pre-commit", "owned"}}) << int(ArchiveProblem::IgnoredPath);
        QTest::newRow("twice") << zip({{"a.md", "1"}, {"a.md", "2"}}) << int(ArchiveProblem::Duplicate);
        QTest::newRow("a file and a folder") << zip({{"a", "1"}, {"a/b.md", "2"}}) << int(ArchiveProblem::Duplicate);
        QTest::newRow("a folder and a file") << zip({{"a/", ""}, {"a", "2"}}) << int(ArchiveProblem::Duplicate);
        QTest::newRow("not a zip") << QByteArray("to nie jest archiwum") << int(ArchiveProblem::NotAZip);
        QTest::newRow("a zip's head only") << QByteArray("PK\x03\x04 ucięte") << int(ArchiveProblem::NotAZip);
        QTest::newRow("empty file") << QByteArray() << int(ArchiveProblem::NotAZip);
        QByteArray cut = zip({{"a.md", "treść"}, {"b.md", "treść"}});
        cut.chop(10);
        QTest::newRow("cut short") << cut << int(ArchiveProblem::NotAZip);
    }
    void hostileArchives()
    {
        QFETCH(QByteArray, zip);
        QFETCH(int, problem);
        QTemporaryDir dir;
        const QString path = dir.filePath(u"Brulion-N-2026-11-02-143005.zip"_s);
        writeFile(path, zip);
        ArchiveReader reader(path);
        const ArchiveInfo info = reader.readEntries();
        QCOMPARE(int(info.problem), problem);
        QVERIFY(info.entries.isEmpty());
        // No control characters of a hostile name reach the log's line.
        for (const QChar c : info.detail)
            QVERIFY(c.unicode() >= 0x20);
    }

    void aDamagedEntryFailsItsChecksum()
    {
        QTemporaryDir dir;
        QByteArray zip = rawZip({{"a.md", QByteArray(1000, 'x')}, {"b.md", "ok"}}, 0);
        const qsizetype data = zip.indexOf(QByteArray(1000, 'x'));
        QVERIFY(data > 0);
        zip[data + 500] = 'y';
        const QString path = dir.filePath(u"z.zip"_s);
        writeFile(path, zip);
        ArchiveReader reader(path);
        const ArchiveInfo info = reader.readEntries();
        QVERIFY2(info.ok(), qPrintable(info.detail)); // the directory is fine
        QVERIFY(!reader.verify(info.entries.at(0)));
        QVERIFY(!reader.read(info.entries.at(0)).has_value());
        QVERIFY(reader.verify(info.entries.at(1)));
    }

    void limits_data()
    {
        QTest::addColumn<int>("maxEntries");
        QTest::addColumn<qint64>("maxEntryBytes");
        QTest::addColumn<qint64>("maxTotalBytes");
        QTest::newRow("too many entries") << 1 << qint64(1000) << qint64(1000);
        QTest::newRow("an entry too large") << 10 << qint64(5) << qint64(1000);
        QTest::newRow("too much in all") << 10 << qint64(100) << qint64(15);
    }
    void limits()
    {
        QFETCH(int, maxEntries);
        QFETCH(qint64, maxEntryBytes);
        QFETCH(qint64, maxTotalBytes);
        QTemporaryDir dir;
        const QString path = dir.filePath(u"z.zip"_s);
        writeFile(path, rawZip({{"a.md", "0123456789"}, {"b.md", "0123456789"}}));
        ArchiveReader reader(path);
        ArchiveLimits limits;
        limits.maxEntries = maxEntries;
        limits.maxEntryBytes = maxEntryBytes;
        limits.maxTotalBytes = maxTotalBytes;
        QCOMPARE(reader.readEntries(limits).problem, ArchiveProblem::TooLarge);
    }

    void aZipBombsClaimIsRefused()
    {
        // The central directory claims 1 TiB in a few compressed bytes: sizes deflate cannot give.
        QByteArray zip = rawZip({{"bomba.md", QByteArray(100'000, 'x')}});
        const qsizetype central = zip.indexOf(QByteArray("PK\x01\x02", 4));
        QVERIFY(central > 0);
        const quint32 huge = 0xFFFFFF00u;
        for (int i = 0; i < 4; ++i)
            zip[central + 24 + i] = char((huge >> (8 * i)) & 0xff);
        QTemporaryDir dir;
        const QString path = dir.filePath(u"z.zip"_s);
        writeFile(path, zip);
        ArchiveReader reader(path);
        QCOMPARE(reader.readEntries().problem, ArchiveProblem::TooLarge);
    }

    void randomBytesNeverCrash()
    {
        QTemporaryDir dir;
        const QString path = dir.filePath(u"z.zip"_s);
        const QByteArray good = rawZip({{"a.md", QByteArray(5000, 'x')}, {"folder/b.md", "treść"}, {"pusty/", ""}});
        QRandomGenerator generator(2026);
        for (int round = 0; round < 300; ++round) {
            QByteArray bad = good;
            for (int i = 0; i < 8; ++i)
                bad[generator.bounded(int(bad.size()))] = char(generator.bounded(256));
            writeFile(path, bad);
            ArchiveReader reader(path);
            const ArchiveInfo info = reader.readEntries();
            for (const ArchiveEntry &entry : info.entries) {
                QCOMPARE(int(checkEntryPath(entry.path)), int(EntryPath::Ok)); // never anything else
                reader.verify(entry);
                reader.read(entry);
            }
        }
    }
};

QTEST_GUILESS_MAIN(TestBackupArchive)
#include "tst_backup_archive.moc"
