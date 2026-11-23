// A file above 1 GiB — what a backup and a restore once held in memory whole, so that a restore left it
// alone — goes through both in pieces: read 64 KiB at a time into the ZIP writer, inflated 64 KiB at a
// time into a staging file and put in place by the Vault. The file is sparse (no disk is written for it,
// its holes read as zeros), so the test costs time (every byte is checksummed a few times) and the space
// of the restored copy, and skips where that is not there. Bounded buffers are asserted by what is
// observable: no read of the file is larger than a piece, and a restore that needed the whole entry in
// memory would have failed on ArchiveReader::read()'s limit of 1 GiB.
#include "backuptest.h"

#include "vault/vault.h"

#include <QStorageInfo>
#include <QTemporaryDir>

using namespace backuptest;

namespace {

constexpr qint64 BigSize = (qint64(1) << 30) + (5 << 20) + 7; // over the old limit, not a round number
constexpr qint64 NeededBytes = qint64(3) << 30;
const QByteArray Mark = "żółć ąę\n";

/// A sparse file of `size` bytes with a few marks in it.
bool makeSparse(const QString &path, qint64 size)
{
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly) || !file.resize(size))
        return false;
    for (const qint64 at : {qint64(0), size / 2, size - Mark.size()}) {
        if (!file.seek(at) || file.write(Mark) != Mark.size())
            return false;
    }
    return file.flush();
}

} // namespace

class TestBackupLarge : public QObject {
    Q_OBJECT

    QThreadPool m_threads;

private Q_SLOTS:
    void aFileOverOneGibGoesThroughABackupAndARestoreInPieces()
    {
#ifndef Q_OS_LINUX
        QSKIP("a sparse file of this size is made on Linux only");
#endif
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        if (QStorageInfo(dir.path()).bytesAvailable() < NeededBytes)
            QSKIP("less than 3 GiB free in the temporary folder");
        const QString rootA = dir.filePath(u"Źródło"_s);
        const QString big = u"Wielkie/film z podróży żółwia.bin"_s;
        QVERIFY(makeSparse(rootA + u'/' + big, BigSize));
        writeFile(rootA + u"/Notatka ąę.md"_s, "- obok\n");
        Vault source(rootA);
        QVERIFY(source.open());

        // The backup: the file is read 64 KiB (a piece) at a time, in order, and deflated.
        WriteOptions options;
        options.targetPath = dir.filePath(u"kopie/Brulion-Źródło-2026-11-23-100000.zip"_s);
        options.notebookId = source.id();
        options.createFolder = true;
        qint64 reads = 0;
        qint64 previous = -1;
        qint64 widestStep = 0;
        options.onRead = [&](const VaultPath &path, qint64 offset) {
            if (path != big)
                return;
            ++reads;
            if (previous >= 0 && offset > previous)
                widestStep = std::max(widestStep, offset - previous);
            previous = offset;
        };
        const WriteResult written = runWriter(poolOn(&m_threads), collectSources(source), options);
        QVERIFY2(written.ok(), qPrintable(written.detail));
        QVERIFY(written.complete());
        QVERIFY2(widestStep <= (1 << 20), qPrintable(QString::number(widestStep)));
        QVERIFY(reads >= BigSize / (1 << 20));
        QVERIFY(QFileInfo(options.targetPath).size() < (64 << 20)); // zeros deflate to almost nothing

        ArchiveReader reader(options.targetPath);
        const ArchiveInfo info = reader.readEntries();
        QVERIFY2(info.ok(), qPrintable(info.detail));
        const auto entry = std::ranges::find_if(info.entries, [&](const ArchiveEntry &e) { return e.path == big; });
        QVERIFY(entry != info.entries.cend());
        QCOMPARE(entry->size, BigSize);
        QCOMPARE(fileCrc32(rootA + u'/' + big), std::optional<quint32>(entry->crc32));
        QVERIFY(!reader.read(*entry).has_value()); // holding it whole is not on offer

        // The restore into another notebook: planned, checked, applied through a staging file.
        const QString rootB = dir.filePath(u"Cel"_s);
        writeFile(rootB + u"/inna.md"_s, "- cel\n");
        QTemporaryDir state;
        Vault target(rootB, state.path());
        QVERIFY(target.open());
        RestoreCheckOptions checkOptions;
        checkOptions.requireManifest = true;
        const RestoreCheck check = runChecker(poolOn(&m_threads), options.targetPath, notebookFiles(target),
                                              notebookFolders(target), checkOptions);
        QVERIFY2(check.ok(), qPrintable(check.detail));
        QVERIFY(check.plan.tooLarge.isEmpty());
        QCOMPARE(check.plan.toRecreate.size(), 2);

        RestoreApplier applier(target, poolOn(&m_threads));
        QSignalSpy spy(&applier, &RestoreApplier::finished);
        applier.start(check.plan, options.targetPath);
        QVERIFY(spy.wait(600000));
        const RestoreResult result = spy.first().first().value<RestoreResult>();
        QVERIFY2(result.ok(), qPrintable(result.detail + result.failed.join(u", ")));
        QCOMPARE(result.recreated, 2);

        const QString restored = rootB + u'/' + big;
        QCOMPARE(QFileInfo(restored).size(), BigSize);
        QCOMPARE(fileCrc32(restored), std::optional<quint32>(entry->crc32));
        QFile file(restored);
        QVERIFY(file.open(QIODevice::ReadOnly));
        for (const qint64 at : {qint64(0), BigSize / 2, BigSize - Mark.size()}) {
            QVERIFY(file.seek(at));
            QCOMPARE(file.read(Mark.size()), Mark);
        }
        QCOMPARE(readFile(rootB + u"/Notatka ąę.md"_s), QByteArray("- obok\n"));
        QCOMPARE(readFile(rootB + u"/inna.md"_s), QByteArray()); // not in the backup: it went to the trash
        QVERIFY(target.exists(big));
    }
};

QTEST_GUILESS_MAIN(TestBackupLarge)
#include "tst_backup_large.moc"
