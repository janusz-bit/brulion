// The streaming ZIP writer under the backups (backup/zipwriter_p.h): what it writes is read back by
// miniz byte for byte, in every size class and with ZIP64 records (forced, where a test cannot write
// 4 GiB), the sizes of an entry are in its local header for readers that stream, an entry given up is
// overwritten by the next one, and a size limit stops the archive.
#include "backuptest.h"

#include "backup/zipwriter_p.h"

#include <QRandomGenerator>
#include <QTemporaryDir>

using namespace backuptest;
using brulion::backup::detail::ZipWriter;

namespace {

quint32 le32(const QByteArray &bytes, qsizetype at)
{
    return quint32(quint8(bytes[at])) | quint32(quint8(bytes[at + 1])) << 8 | quint32(quint8(bytes[at + 2])) << 16
            | quint32(quint8(bytes[at + 3])) << 24;
}

QByteArray noise(qsizetype size, quint32 seed)
{
    QByteArray out(size, '\0');
    QRandomGenerator generator(seed);
    for (qsizetype i = 0; i < size; ++i)
        out[i] = char(generator.bounded(256));
    return out;
}

/// Text that deflates well, with Polish letters.
QByteArray text(qsizetype size)
{
    QByteArray out;
    while (out.size() < size)
        out += "- zażółć gęślą jaźń, a potem [[Notatka " + QByteArray::number(out.size() % 97) + "]]\n";
    out.truncate(size);
    return out;
}

} // namespace

class TestBackupZipWriter : public QObject {
    Q_OBJECT

private Q_SLOTS:
    void roundTrip_data()
    {
        QTest::addColumn<int>("size");
        QTest::addColumn<int>("level");
        QTest::addColumn<bool>("zip64");
        QTest::addColumn<int>("piece");
        for (const bool zip64 : {false, true}) {
            const QByteArray kind = zip64 ? " (ZIP64)" : "";
            for (const int size : {0, 1, 3, 4, 100, 70'000, 1 << 20}) {
                for (const int level : {0, 1, 6, 10})
                    QTest::addRow("%d bytes, level %d%s", size, level, kind.constData())
                            << size << level << zip64 << 4093;
            }
            QTest::addRow("a byte at a time%s", kind.constData()) << 3000 << 6 << zip64 << 1;
            QTest::addRow("one piece%s", kind.constData()) << (1 << 20) << 6 << zip64 << (1 << 20);
        }
    }
    void roundTrip()
    {
        QFETCH(int, size);
        QFETCH(int, level);
        QFETCH(bool, zip64);
        QFETCH(int, piece);
        QTemporaryDir dir;
        const QString path = dir.filePath(u"z.zip"_s);
        const QByteArray bytes = size > 1000 && level % 2 == 0 ? noise(size, 1) : text(size);
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly));
        ZipWriter writer(file, {.forceZip64 = zip64});
        QVERIFY(writer.addFolder("Pusty żółw", 0));
        QVERIFY(writer.beginFile("Żółć/gęślą jaźń.md", QDateTime(QDate(2026, 11, 2), QTime(14, 30, 5)).toMSecsSinceEpoch(),
                                 size, level));
        for (qsizetype at = 0; at < bytes.size(); at += piece)
            QVERIFY(writer.write(bytes.constData() + at, std::min<qsizetype>(piece, bytes.size() - at)));
        QVERIFY(writer.endFile());
        QVERIFY(writer.beginFile("drugi.md", 0, 2, level));
        QVERIFY(writer.write("ok", 2));
        QVERIFY(writer.endFile());
        const QByteArray manifest = backupManifest(u"id"_s, true, 2);
        QVERIFY(writer.finish(manifest));
        QCOMPARE(writer.error(), ZipWriter::Error::None);
        QCOMPARE(writer.offset(), file.size());
        QVERIFY(file.flush());
        file.close();

        ArchiveReader reader(path);
        const ArchiveInfo info = reader.readEntries();
        QVERIFY2(info.ok(), qPrintable(info.detail));
        QCOMPARE(info.entries.size(), 3);
        QCOMPARE(info.entries.at(0).path, u"Pusty żółw"_s);
        QVERIFY(info.entries.at(0).folder);
        const ArchiveEntry entry = info.entries.at(1);
        QCOMPARE(entry.path, u"Żółć/gęślą jaźń.md"_s);
        QCOMPARE(entry.size, qint64(size));
        QVERIFY(reader.verify(entry));
        QCOMPARE(*reader.read(entry), bytes);
        QCOMPARE(*reader.read(info.entries.at(2)), QByteArray("ok"));
        if (level == 0 || size <= 3)
            QCOMPARE(entry.compressedSize, entry.size);
        if (level > 0 && size > 100'000 && bytes == text(size))
            QVERIFY(entry.compressedSize < entry.size / 4);
        QVERIFY(readBackupManifest(path).valid);
        QCOMPARE(readBackupManifest(path).notebookId, u"id"_s);
        // The checksum a streaming reader would compare.
        QCOMPARE(fileCrc32(path).has_value(), true);
        quint32 crc = quint32(mz_crc32(0, reinterpret_cast<const unsigned char *>(bytes.constData()), size_t(bytes.size())));
        QCOMPARE(entry.crc32, crc);
    }

    void theSizesOfAnEntryAreInItsLocalHeader_data()
    {
        QTest::addColumn<int>("level");
        QTest::newRow("stored") << 0;
        QTest::newRow("deflated") << 6;
    }
    void theSizesOfAnEntryAreInItsLocalHeader()
    {
        // Android's ZipInputStream, and every reader that goes entry by entry, takes the sizes and the
        // checksum from the local header: no data descriptor (bit 3) follows the entry.
        QFETCH(int, level);
        QTemporaryDir dir;
        const QString path = dir.filePath(u"z.zip"_s);
        const QByteArray bytes = text(50'000);
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly));
        ZipWriter writer(file, {});
        QVERIFY(writer.beginFile("a.md", 0, bytes.size(), level));
        QVERIFY(writer.write(bytes.constData(), bytes.size()));
        QVERIFY(writer.endFile());
        QVERIFY(writer.finish({}));
        file.close();
        const QByteArray zip = readFile(path);
        QCOMPARE(le32(zip, 0), quint32(0x04034b50));
        QCOMPARE(quint8(zip[6]) & 0x08, 0); // no data descriptor
        QCOMPARE(le32(zip, 22), quint32(bytes.size()));
        QVERIFY(le32(zip, 18) > 0);
        QCOMPARE(le32(zip, 18) == quint32(bytes.size()), level == 0);
        QCOMPARE(le32(zip, 14),
                 quint32(mz_crc32(0, reinterpret_cast<const unsigned char *>(bytes.constData()), size_t(bytes.size()))));
    }

    void manyEntriesNeedTheZip64End()
    {
        // 65 535 entries do not fit the end record's counter: the ZIP64 end record carries them.
        QTemporaryDir dir;
        const QString path = dir.filePath(u"z.zip"_s);
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly));
        ZipWriter writer(file, {});
        constexpr int count = 70'000;
        for (int i = 0; i < count; ++i) {
            QVERIFY(writer.beginFile("d" + QByteArray::number(i % 100) + "/n" + QByteArray::number(i) + ".md", 0, 0, 6));
            QVERIFY(writer.endFile());
        }
        QVERIFY(writer.finish(backupManifest(u"id"_s, true, count)));
        file.close();
        ArchiveReader reader(path);
        const ArchiveInfo info = reader.readEntries();
        QVERIFY2(info.ok(), qPrintable(info.detail));
        QCOMPARE(info.entries.size(), count);
        QCOMPARE(info.entries.last().path, u"d99/n69999.md"_s);
        QVERIFY(readBackupManifest(path).valid);
    }

    void anEntryGivenUpIsOverwrittenByTheNext()
    {
        QTemporaryDir dir;
        const QString path = dir.filePath(u"z.zip"_s);
        QFile file(path);
        QVERIFY(file.open(QIODevice::ReadWrite));
        ZipWriter writer(file, {});
        QVERIFY(writer.beginFile("pierwszy.md", 0, 2, 6));
        QVERIFY(writer.write("ok", 2));
        QVERIFY(writer.endFile());
        const QByteArray big = noise(300'000, 3);
        QVERIFY(writer.beginFile("zmienia się.md", 0, big.size(), 6));
        QVERIFY(writer.write(big.constData(), 200'000));
        QVERIFY(writer.inFile());
        writer.abandonFile();
        QVERIFY(!writer.inFile());
        const QByteArray third = "treść";
        QVERIFY(writer.beginFile("trzeci.md", 0, third.size(), 0));
        QVERIFY(writer.write(third.constData(), third.size()));
        QVERIFY(writer.endFile());
        QVERIFY(writer.finish({}));
        QVERIFY(file.resize(writer.offset()));
        file.close();
        ArchiveReader reader(path);
        const ArchiveInfo info = reader.readEntries();
        QVERIFY2(info.ok(), qPrintable(info.detail));
        QCOMPARE(info.entries.size(), 2);
        QCOMPARE(info.entries.at(0).path, u"pierwszy.md"_s);
        QCOMPARE(info.entries.at(1).path, u"trzeci.md"_s);
        QCOMPARE(*reader.read(info.entries.at(1)), QByteArray("treść"));
        QVERIFY(QFileInfo(path).size() < 1000);
    }

    void aSizeLimitStopsTheArchive()
    {
        QTemporaryDir dir;
        QFile file(dir.filePath(u"z.zip"_s));
        QVERIFY(file.open(QIODevice::WriteOnly));
        ZipWriter writer(file, {.maxBytes = 100'000});
        const QByteArray big = noise(200'000, 5);
        QVERIFY(writer.beginFile("a.bin", 0, big.size(), 0));
        bool refused = false;
        for (qsizetype at = 0; at < big.size() && !refused; at += 4096)
            refused = !writer.write(big.constData() + at, std::min<qsizetype>(4096, big.size() - at));
        QVERIFY(refused);
        QCOMPARE(writer.error(), ZipWriter::Error::TooLarge);
        QVERIFY(file.size() <= 100'000);
    }

    void misuseIsRefused()
    {
        QTemporaryDir dir;
        QFile file(dir.filePath(u"z.zip"_s));
        QVERIFY(file.open(QIODevice::WriteOnly));
        {
            ZipWriter writer(file, {});
            QVERIFY(writer.beginFile("a.md", 0, 3, 0));
            QVERIFY(!writer.write("abcd", 4)); // more than promised
            QCOMPARE(writer.error(), ZipWriter::Error::Misuse);
        }
        {
            ZipWriter writer(file, {});
            QVERIFY(writer.beginFile("a.md", 0, 3, 0));
            QVERIFY(writer.write("ab", 2));
            QVERIFY(!writer.endFile()); // fewer than promised
        }
        {
            ZipWriter writer(file, {});
            QVERIFY(!writer.beginFile("", 0, 3, 0));
        }
        {
            ZipWriter writer(file, {});
            QVERIFY(writer.beginFile("a.md", 0, 3, 0));
            QVERIFY(!writer.finish({})); // an entry is open
        }
    }
};

QTEST_GUILESS_MAIN(TestBackupZipWriter)
#include "tst_backup_zipwriter.moc"
