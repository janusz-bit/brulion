// Archive and data formats of the io module: ZIP (miniz wrapper), tar, CSV and iCalendar.
#include "iotest.h"

#include "io/csv.h"
#include "io/ical.h"
#include "io/tar.h"
#include "io/zip.h"

#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <QTest>

using namespace brulion;
using namespace brulion::io;
using namespace Qt::StringLiterals;

namespace {

/// `size` bytes of a pattern (`seed` makes another).
QByteArray pattern(qint64 size, int seed = 0)
{
    QByteArray out(size, Qt::Uninitialized);
    for (qint64 i = 0; i < size; ++i)
        out[i] = char((i * 7919 + 13 + seed) & 0xff);
    return out;
}

/// `bytes` saved as `name` in `dir`: its path ("" when it cannot be written).
QString saved(const QTemporaryDir &dir, const QString &name, const QByteArray &bytes)
{
    const QString path = dir.filePath(name);
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size())
        return {};
    return path;
}

/// Replace every occurrence of `from` by `to` (same length) in `data`.
QByteArray patched(QByteArray data, const QByteArray &from, const QByteArray &to)
{
    Q_ASSERT(from.size() == to.size());
    qsizetype pos = 0;
    while ((pos = data.indexOf(from, pos)) >= 0) {
        data.replace(pos, from.size(), to);
        pos += to.size();
    }
    return data;
}

/// Clear the UTF-8 name flag (bit 11) in local and central headers.
QByteArray withoutUtf8Flag(QByteArray zip)
{
    for (qsizetype pos = 0; (pos = zip.indexOf("PK\x03\x04", pos)) >= 0; pos += 4)
        zip[pos + 7] = char(zip[pos + 7] & ~0x08);
    for (qsizetype pos = 0; (pos = zip.indexOf("PK\x01\x02", pos)) >= 0; pos += 4)
        zip[pos + 9] = char(zip[pos + 9] & ~0x08);
    return zip;
}

} // namespace

class TestIoFormats : public QObject {
    Q_OBJECT

private Q_SLOTS:
    // --- ZIP ------------------------------------------------------------------------------------

    void zipRoundTrip()
    {
        QByteArray binary;
        for (int i = 0; i < 5000; ++i)
            binary += char(i * 7);
        const QList<FileEntry> entries = {
                {u"Notatki/Zażółć gęślą jaźń.md"_s, u"# Łódź\n\n- [ ] zadanie 📅 2026-10-15\n"_s.toUtf8()},
                {u"assets/zdjęcie.png"_s, binary},
                {u"pusty.md"_s, QByteArray()},
                {u"głęboko/a/b/c.txt"_s, QByteArray(100'000, 'x')},
        };
        const QByteArray zip = writeZip(entries);
        QVERIFY(isZip(zip));
        QStringList warnings;
        const QList<FileEntry> read = readZip(zip, &warnings);
        QVERIFY2(warnings.isEmpty(), qPrintable(warnings.join(u'\n')));
        QCOMPARE(read, entries);
        QVERIFY(zip.size() < 100'000); // compressed
    }

    void zipIsDeterministic()
    {
        const QList<FileEntry> entries = {{u"a.md"_s, "alpha"}, {u"b/c.md"_s, "beta"}};
        QCOMPARE(writeZip(entries), writeZip(entries));
        ZipWriteOptions o;
        o.modified = QDateTime(QDate(2026, 10, 15), QTime(9, 30));
        QVERIFY(writeZip(entries, o) != writeZip(entries));
        QCOMPARE(readZip(writeZip(entries, o)), entries);
    }

    void zipTimestamps_data()
    {
        // DOS date/time fields of the first local header: dates outside 1980–2107 cannot be
        // represented and are clamped instead of overflowing into the other bit fields.
        QTest::addColumn<QDateTime>("modified");
        QTest::addColumn<int>("dosDate");
        QTest::addColumn<int>("dosTime");
        QTest::newRow("default") << QDateTime() << ((0 << 9) | (1 << 5) | 1) << 0;
        QTest::newRow("2026") << QDateTime(QDate(2026, 10, 15), QTime(12, 0, 30)) << ((46 << 9) | (10 << 5) | 15)
                              << ((12 << 11) | (0 << 5) | 15);
        QTest::newRow("before 1980") << QDateTime(QDate(1970, 1, 1), QTime(5, 0)) << ((0 << 9) | (1 << 5) | 1) << 0;
        QTest::newRow("after 2107") << QDateTime(QDate(2200, 6, 1), QTime(5, 0)) << ((127 << 9) | (12 << 5) | 31)
                                    << ((23 << 11) | (59 << 5) | 29);
    }

    void zipTimestamps()
    {
        QFETCH(QDateTime, modified);
        QFETCH(int, dosDate);
        QFETCH(int, dosTime);
        ZipWriteOptions o;
        o.modified = modified;
        const QByteArray zip = writeZip({{u"a.md"_s, "a"}}, o);
        QVERIFY(zip.size() > 30);
        const auto le16 = [&](int offset) {
            return int(static_cast<unsigned char>(zip[offset]))
                   | (int(static_cast<unsigned char>(zip[offset + 1])) << 8);
        };
        QCOMPARE(le16(12), dosDate);
        QCOMPARE(le16(10), dosTime);
    }

    void zipStoreAndLevels()
    {
        const QList<FileEntry> entries = {{u"a.txt"_s, QByteArray(10'000, 'a')}, {u"b.png"_s, QByteArray(10'000, 'b')}};
        ZipWriteOptions stored;
        stored.compressionLevel = 0;
        const QByteArray big = writeZip(entries, stored);
        const QByteArray small = writeZip(entries);
        QVERIFY(big.size() > 20'000);
        QVERIFY(small.size() > 10'000); // the PNG is stored
        QVERIFY(small.size() < 12'000);
        QCOMPARE(readZip(big), entries);
        QCOMPARE(readZip(small), entries);
    }

    void zipWriteSkipsUnsafeAndDuplicates()
    {
        const QByteArray zip = writeZip({{u"../../etc/passwd"_s, "x"},
                                         {u"/abs/path.md"_s, "y"},
                                         {u"dup.md"_s, "first"},
                                         {u"dup.md"_s, "second"},
                                         {u"./.."_s, "nothing"},
                                         {QString(), "empty"}});
        const QList<FileEntry> read = readZip(zip);
        QCOMPARE(iotest::entryPaths(read), QStringList({u"abs/path.md"_s, u"dup.md"_s, u"etc/passwd"_s}));
        QCOMPARE(iotest::entryData(read, u"dup.md"_s), QByteArray("first"));
    }

    void zipNested()
    {
        const QByteArray inner = writeZip({{u"Strona.md"_s, "inner"}, {u"sub/x.md"_s, "x"}});
        const QByteArray deeper = writeZip({{u"deep.zip"_s, writeZip({{u"z.md"_s, "z"}})}});
        const QByteArray outer = writeZip({{u"top.md"_s, "top"},
                                           {u"Export/Part-1.zip"_s, inner},
                                           {u"nest/two.zip"_s, deeper},
                                           {u"bad.zip"_s, "PK\x03\x04garbage"}});
        QStringList warnings;
        const QList<FileEntry> read = readZip(outer, &warnings);
        QCOMPARE(iotest::entryPaths(read),
                 QStringList({u"Export/Strona.md"_s, u"Export/sub/x.md"_s, u"bad.zip"_s, u"nest/z.md"_s, u"top.md"_s}));
        QCOMPARE(warnings.size(), 1); // bad.zip kept as a file
        ZipReadOptions flat;
        flat.expandNested = false;
        QCOMPARE(iotest::entryPaths(readZip(outer, nullptr, flat)),
                 QStringList({u"Export/Part-1.zip"_s, u"bad.zip"_s, u"nest/two.zip"_s, u"top.md"_s}));
        ZipReadOptions shallow;
        shallow.maxNestingDepth = 1;
        QVERIFY(iotest::entryPaths(readZip(outer, nullptr, shallow)).contains(u"nest/deep.zip"_s));
    }

    void zipLimits()
    {
        const QByteArray zip = writeZip({{u"small.md"_s, "abc"}, {u"big.md"_s, QByteArray(5000, 'x')}});
        ZipReadOptions o;
        o.maxEntrySize = 1000;
        QStringList warnings;
        QCOMPARE(iotest::entryPaths(readZip(zip, &warnings, o)), QStringList({u"small.md"_s}));
        QCOMPARE(warnings.size(), 1);
        ZipReadOptions total;
        total.maxTotalSize = 100;
        warnings.clear();
        QCOMPARE(iotest::entryPaths(readZip(zip, &warnings, total)), QStringList({u"small.md"_s}));
        QCOMPARE(warnings.size(), 1);
        ZipReadOptions count;
        count.maxEntries = 1;
        QCOMPARE(readZip(zip, nullptr, count).size(), 1);
    }

    // --- unpacked into files of a staging folder ----------------------------------------------
    void zipFileStagesLargeEntriesInPieces_data()
    {
        QTest::addColumn<bool>("deflated");
        QTest::newRow("stored") << false;
        QTest::newRow("deflated") << true;
    }
    void zipFileStagesLargeEntriesInPieces()
    {
        // A 24 MB film from an archive on disk: unpacked a piece at a time into the staging
        // folder, the notes and small files in memory as before.
        QFETCH(bool, deflated);
        QTemporaryDir work;
        QTemporaryDir staging;
        const QByteArray film = pattern(24LL << 20);
        const QByteArray icon = pattern(4096, 1);
        ZipWriteOptions write;
        write.storeCompressedFormats = !deflated;
        write.compressionLevel = deflated ? 1 : 0;
        const QString archive = saved(work, u"Eksport.zip"_s,
                                      writeZip({{u"Notatka ąćę.md"_s, u"Zażółć gęślą jaźń"_s.toUtf8()},
                                                {u"assets/łódź.mp4"_s, film},
                                                {u"assets/ikona.png"_s, icon}},
                                               write));
        QVERIFY(!archive.isEmpty());
        ZipReadOptions options;
        options.stagingFolder = staging.path();
        qint64 largest = 0;
        qint64 total = 0;
        options.onStagedPiece = [&](qint64 bytes) {
            largest = std::max(largest, bytes);
            total += bytes;
        };
        QStringList warnings;
        const QList<FileEntry> read = readZipFile(archive, &warnings, options);
        QVERIFY2(warnings.isEmpty(), qPrintable(warnings.join(u'\n')));
        QCOMPARE(iotest::entryPaths(read),
                 QStringList({u"Notatka ąćę.md"_s, u"assets/ikona.png"_s, u"assets/łódź.mp4"_s}));
        const auto entry = [&](const QString &path) { return *std::ranges::find(read, path, &FileEntry::path); };
        const FileEntry staged = entry(u"assets/łódź.mp4"_s);
        QVERIFY(staged.onDisk());
        QVERIFY(staged.data.isEmpty());
        QCOMPARE(staged.size(), film.size());
        QVERIFY(staged.file.startsWith(QDir(staging.path()).absolutePath() + u'/'));
        QCOMPARE(bytesOf(staged), film);
        QCOMPARE(total, film.size());
        QVERIFY2(largest <= 256 * 1024, qPrintable(QString::number(largest))); // miniz's buffers
        QVERIFY(!entry(u"assets/ikona.png"_s).onDisk());                       // small: in memory
        QCOMPARE(entry(u"assets/ikona.png"_s).data, icon);
        QVERIFY(!entry(u"Notatka ąćę.md"_s).onDisk()); // a note: always in memory
        // The same archive from memory unpacks to the same content.
        QFile in(archive);
        QVERIFY(in.open(QIODevice::ReadOnly));
        const QList<FileEntry> fromMemory = readZip(in.readAll());
        QCOMPARE(iotest::entryData(fromMemory, u"assets/łódź.mp4"_s), film);
    }

    void zipFileStagingKeepsTheLimitsAndPaths()
    {
        QTemporaryDir work;
        QTemporaryDir staging;
        const QByteArray big = pattern(3LL << 20, 2);
        const QString archive =
                saved(work, u"a.zip"_s,
                      writeZip({{u"../../poza/ucieczka.pdf"_s, big}, {u"za-duży.mov"_s, pattern(5LL << 20, 3)}}));
        ZipReadOptions options;
        options.stagingFolder = staging.path();
        options.maxEntrySize = 4LL << 20;
        QStringList warnings;
        const QList<FileEntry> read = readZipFile(archive, &warnings, options);
        // ".." resolved inside the archive; the file lands in the staging folder, nowhere else.
        QCOMPARE(iotest::entryPaths(read), QStringList{u"poza/ucieczka.pdf"_s});
        QVERIFY(read.first().file.startsWith(QDir(staging.path()).absolutePath() + u'/'));
        QCOMPARE(bytesOf(read.first()), big);
        QCOMPARE(warnings.size(), 1); // the one over maxEntrySize, never unpacked
        QCOMPARE(QDir(staging.path()).entryList(QDir::Files).size(), 1);
        // The total holds as before: nothing past it is unpacked.
        ZipReadOptions total = options;
        total.maxEntrySize = 64LL << 20;
        total.maxTotalSize = 4LL << 20;
        warnings.clear();
        QTemporaryDir staging2;
        total.stagingFolder = staging2.path();
        QCOMPARE(readZipFile(archive, &warnings, total).size(), 1);
        QCOMPARE(QDir(staging2.path()).entryList(QDir::Files).size(), 1);
    }

    void zipFileDamagedStagedEntryLeavesNothing()
    {
        QTemporaryDir work;
        QTemporaryDir staging;
        const QByteArray film = pattern(2LL << 20, 4);
        ZipWriteOptions write;
        write.storeCompressedFormats = false;
        QByteArray zip = writeZip({{u"film.mp4"_s, film}, {u"a.md"_s, "a"}}, write);
        zip[zip.indexOf("film.mp4") + 8 + 1000] ^= 0x5a; // inside the compressed data
        const QString archive = saved(work, u"uszkodzony.zip"_s, zip);
        ZipReadOptions options;
        options.stagingFolder = staging.path();
        QStringList warnings;
        const QList<FileEntry> read = readZipFile(archive, &warnings, options);
        QCOMPARE(iotest::entryPaths(read), QStringList{u"a.md"_s});
        QCOMPARE(warnings.size(), 1);
        QVERIFY(QDir(staging.path()).entryList(QDir::Files | QDir::Hidden).isEmpty());
        // Not a ZIP at all: a warning, nothing read.
        warnings.clear();
        QVERIFY(readZipFile(saved(work, u"nie.zip"_s, "to nie jest archiwum"), &warnings, options).isEmpty());
        QCOMPARE(warnings.size(), 1);
        QVERIFY(readZipFile(work.filePath(u"brak.zip"_s), &warnings, options).isEmpty());
    }

    void zipMalformed_data()
    {
        QTest::addColumn<QByteArray>("data");
        const QByteArray good = writeZip({{u"a.md"_s, QByteArray(3000, 'a')}, {u"b.md"_s, "b"}});
        QTest::newRow("empty") << QByteArray();
        QTest::newRow("text") << QByteArray("hello world, not a zip");
        QTest::newRow("magic only") << QByteArray("PK\x03\x04");
        QTest::newRow("truncated") << good.left(good.size() / 2);
        QTest::newRow("no central directory") << good.left(good.size() - 22);
        QByteArray corrupt = good;
        corrupt[40] = char(corrupt[40] ^ 0x55); // inside the compressed data of a.md
        QTest::newRow("corrupt data") << corrupt;
    }

    void zipMalformed()
    {
        QFETCH(QByteArray, data);
        QStringList warnings;
        const QList<FileEntry> read = readZip(data, &warnings);
        QVERIFY(!warnings.isEmpty());
        for (const FileEntry &e : read)
            QVERIFY(!e.path.isEmpty());
    }

    void zipLegacyNames()
    {
        // A name without the UTF-8 flag in CP437 ("café.txt", é = 0x82) and one that is valid UTF-8.
        QByteArray zip = writeZip({{u"caf_.txt"_s, "cp437"}, {u"zaz_.txt"_s, "utf8"}});
        zip = patched(zip, "caf_", "caf\x82");
        zip = patched(withoutUtf8Flag(zip), "z_.txt", "\xC5\xBC.txt"); // "zaż.txt", same byte length
        const QList<FileEntry> read = readZip(zip);
        QCOMPARE(read.size(), 2);
        QCOMPARE(read[0].path, u"café.txt"_s);
        QCOMPARE(read[0].data, QByteArray("cp437"));
        QCOMPARE(read[1].path, u"zaż.txt"_s);
        QCOMPARE(read[1].data, QByteArray("utf8"));
    }

    void sanitizeArchivePath_data()
    {
        QTest::addColumn<QString>("input");
        QTest::addColumn<QString>("expected");
        QTest::newRow("plain") << u"a/b.md"_s << u"a/b.md"_s;
        QTest::newRow("backslashes") << u"a\\b\\c.md"_s << u"a/b/c.md"_s;
        QTest::newRow("absolute") << u"/etc/passwd"_s << u"etc/passwd"_s;
        QTest::newRow("drive letter") << u"C:\\Users\\x.md"_s << u"Users/x.md"_s;
        QTest::newRow("parent escape") << u"../../x.md"_s << u"x.md"_s;
        QTest::newRow("parent inside") << u"a/b/../c.md"_s << u"a/c.md"_s;
        QTest::newRow("dots and empties") << u"./a//./b/"_s << u"a/b"_s;
        QTest::newRow("control chars") << u"a\u0001b/\u007fc.md"_s << u"ab/c.md"_s;
        QTest::newRow("polish nfd") << u"Z\u0307o\u0301\u0142w.md"_s << u"Żółw.md"_s;
        QTest::newRow("only dots") << u"../.."_s << QString();
    }

    void sanitizeArchivePath()
    {
        QFETCH(QString, input);
        QFETCH(QString, expected);
        QCOMPARE(io::sanitizeArchivePath(input), expected);
    }

    // --- tar ------------------------------------------------------------------------------------

    void tarRead()
    {
        const QByteArray longName = QByteArray("very/") + QByteArray(120, 'n') + ".md";
        const QByteArray tar = iotest::tarArchive({
                {"0123456789abcdef0123456789abcdef.md", "Notatka\n\nTreść ąę\n", '0', {}},
                {"resources/", {}, '5', {}},
                {"img.png", QByteArray(700, 'p'), '0', "resources"},
                {"././@LongLink", longName + '\0', 'L', {}},
                {"truncated-name", "long body", '0', {}},
                {"pax", "28 path=pax/real name ż.md\n", 'x', {}},
                {"PaxShort", "pax body", '0', {}},
                {"link", {}, '2', {}},
                {"../escape.md", "x", '0', {}},
        });
        QVERIFY(isTar(tar));
        QStringList warnings;
        const QList<FileEntry> read = readTar(tar, &warnings);
        QVERIFY2(warnings.isEmpty(), qPrintable(warnings.join(u'\n')));
        QCOMPARE(iotest::entryPaths(read),
                 QStringList({u"0123456789abcdef0123456789abcdef.md"_s, u"escape.md"_s, u"pax/real name ż.md"_s,
                              u"resources/img.png"_s, QString::fromUtf8(longName)}));
        QCOMPARE(iotest::entryData(read, u"resources/img.png"_s), QByteArray(700, 'p'));
        QCOMPARE(QString::fromUtf8(iotest::entryData(read, u"0123456789abcdef0123456789abcdef.md"_s)),
                 u"Notatka\n\nTreść ąę\n"_s);
        QCOMPARE(iotest::entryData(read, QString::fromUtf8(longName)), QByteArray("long body"));
    }

    void tarFileStagesLargeEntriesInPieces()
    {
        // A Joplin JEX of 24 MB on disk: read header by header, the film copied into the staging
        // folder a piece at a time, the notes and small files in memory as from readTar().
        QTemporaryDir work;
        QTemporaryDir staging;
        const QByteArray film = pattern(24LL << 20, 5);
        const QByteArray icon = pattern(4096, 6);
        const QByteArray tar = iotest::tarArchive({{"0123abcd.md", u"Zażółć gęślą jaźń\n"_s.toUtf8(), '0', {}},
                                                   {"resources/łódź.mp4", film, '0', {}},
                                                   {"resources/ikona.png", icon, '0', {}}});
        const QString archive = saved(work, u"Eksport.jex"_s, tar);
        ZipReadOptions options;
        options.stagingFolder = staging.path();
        qint64 largest = 0;
        qint64 total = 0;
        options.onStagedPiece = [&](qint64 bytes) {
            largest = std::max(largest, bytes);
            total += bytes;
        };
        QStringList warnings;
        const QList<FileEntry> read = readTarFile(archive, &warnings, options);
        QVERIFY2(warnings.isEmpty(), qPrintable(warnings.join(u'\n')));
        QCOMPARE(iotest::entryPaths(read),
                 QStringList({u"0123abcd.md"_s, u"resources/ikona.png"_s, u"resources/łódź.mp4"_s}));
        const auto entry = [&](const QString &path) { return *std::ranges::find(read, path, &FileEntry::path); };
        const FileEntry staged = entry(u"resources/łódź.mp4"_s);
        QVERIFY(staged.onDisk());
        QVERIFY(staged.data.isEmpty());
        QCOMPARE(staged.size(), film.size());
        QVERIFY(staged.file.startsWith(QDir(staging.path()).absolutePath() + u'/'));
        QCOMPARE(bytesOf(staged), film);
        QCOMPARE(total, film.size());
        QVERIFY2(largest <= 256 * 1024, qPrintable(QString::number(largest)));
        QCOMPARE(entry(u"resources/ikona.png"_s).data, icon);
        QVERIFY(!entry(u"0123abcd.md"_s).onDisk());
        // Without a staging folder everything comes into memory, as readTar() gives it.
        const QList<FileEntry> whole = readTarFile(archive);
        QCOMPARE(iotest::entryData(whole, u"resources/łódź.mp4"_s), film);
        QCOMPARE(iotest::entryPaths(whole), iotest::entryPaths(readTar(tar)));
    }

    void tarFileKeepsTheLimitsAndWarnings()
    {
        QTemporaryDir work;
        QTemporaryDir staging;
        const QByteArray tar = iotest::tarArchive({{"a.md", "aaa", '0', {}},
                                                   {"za-duży.mov", pattern(5LL << 20, 7), '0', {}},
                                                   {"b.pdf", pattern(3LL << 20, 8), '0', {}}});
        const QString archive = saved(work, u"a.tar"_s, tar);
        ZipReadOptions options;
        options.stagingFolder = staging.path();
        options.maxEntrySize = 4LL << 20;
        QStringList warnings;
        // Too large: skipped (never read), the rest goes on.
        QCOMPARE(iotest::entryPaths(readTarFile(archive, &warnings, options)), (QStringList{u"a.md"_s, u"b.pdf"_s}));
        QCOMPARE(warnings.size(), 1);
        // Damaged and cut archives: what was read so far, a warning, nothing left in the staging folder.
        for (const QByteArray &bad : {tar.left(512 + 512 + 512 + (1 << 20)), patched(tar, "b.pdf", "X.pdf")}) {
            QTemporaryDir staging2;
            ZipReadOptions o;
            o.stagingFolder = staging2.path();
            warnings.clear();
            const QList<FileEntry> read = readTarFile(saved(work, u"zły.tar"_s, bad), &warnings, o);
            QCOMPARE(iotest::entryPaths(read).first(), u"a.md"_s);
            QCOMPARE(warnings.size(), 1);
            QVERIFY2(warnings.first().contains(u"truncated"_s) || warnings.first().contains(u"damaged"_s),
                     qPrintable(warnings.first())); // (never taken for a full disk)
            // (Only what was read whole stays staged: the cut film is gone.)
            const qsizetype staged = std::ranges::count_if(read, [](const FileEntry &e) { return e.onDisk(); });
            QCOMPARE(QDir(staging2.path()).entryList(QDir::Files).size(), staged);
        }
        warnings.clear();
        QVERIFY(readTarFile(work.filePath(u"brak.tar"_s), &warnings).isEmpty());
        QCOMPARE(warnings.size(), 1);
    }

    void tarFileBoundaries()
    {
        QTemporaryDir work;
        QTemporaryDir staging;
        // No end blocks, an empty file last: its header ends the file and is still read.
        QByteArray tar = iotest::tarArchive({{"a.md", "aaa", '0', {}}, {"pusty.md", {}, '0', {}}});
        tar.chop(1024);
        QCOMPARE(iotest::entryPaths(readTarFile(saved(work, u"bez-końca.tar"_s, tar))),
                 (QStringList{u"a.md"_s, u"pusty.md"_s}));
        // The limits are inclusive: a total of exactly maxTotalSize is read, one byte more is not.
        const QByteArray three = iotest::tarArchive({{"1.md", QByteArray(600, '1'), '0', {}},
                                                     {"2.md", QByteArray(400, '2'), '0', {}},
                                                     {"3.md", QByteArray(10, '3'), '0', {}}});
        const QString file = saved(work, u"trzy.tar"_s, three);
        ZipReadOptions limits;
        limits.maxTotalSize = 1000;
        QStringList warnings;
        QCOMPARE(iotest::entryPaths(readTarFile(file, &warnings, limits)), (QStringList{u"1.md"_s, u"2.md"_s}));
        QCOMPARE(warnings.size(), 1);
        limits.maxTotalSize = 1010;
        QCOMPARE(readTarFile(file, nullptr, limits).size(), 3);
        limits.maxEntries = 2;
        warnings.clear();
        QCOMPARE(readTarFile(file, &warnings, limits).size(), 2);
        QCOMPARE(warnings.size(), 1);
        // Staged only above stageAbove; a last piece of one byte is copied too.
        const QByteArray film = pattern(256 * 1024 + 1, 9);
        const QString films = saved(work, u"filmy.tar"_s, iotest::tarArchive({{"równo.mp4", QByteArray(100, 'r'), '0', {}},
                                                                               {"film.mp4", film, '0', {}}}));
        ZipReadOptions staged;
        staged.stagingFolder = staging.path();
        staged.stageAbove = 100;
        warnings.clear();
        const QList<FileEntry> read = readTarFile(films, &warnings, staged);
        QVERIFY2(warnings.isEmpty(), qPrintable(warnings.join(u'\n')));
        QCOMPARE(read.size(), 2);
        QVERIFY(!read.at(0).onDisk()); // exactly stageAbove: in memory
        QVERIFY(read.at(1).onDisk());
        QCOMPARE(bytesOf(read.at(1)), film);
    }

    void tarMalformed()
    {
        QVERIFY(!isTar(QByteArray()));
        QVERIFY(!isTar(QByteArray(512, '\0')));
        QVERIFY(!isTar(writeZip({{u"a"_s, "b"}})));
        QByteArray tar = iotest::tarArchive({{"a.md", "aaa", '0', {}}, {"b.md", QByteArray(2000, 'b'), '0', {}}});
        QStringList warnings;
        // Damaged second header: the first file is still read.
        QByteArray damaged = tar;
        damaged[1024 + 3] = 'X';
        QCOMPARE(iotest::entryPaths(readTar(damaged, &warnings)), QStringList({u"a.md"_s}));
        QCOMPARE(warnings.size(), 1);
        // Truncated data of the second file.
        warnings.clear();
        QCOMPARE(iotest::entryPaths(readTar(tar.left(1024 + 512 + 100), &warnings)), QStringList({u"a.md"_s}));
        QCOMPARE(warnings.size(), 1);
        // Garbage.
        warnings.clear();
        QVERIFY(readTar(QByteArray(2000, 'z'), &warnings).isEmpty());
        QCOMPARE(warnings.size(), 1);
    }

    // --- CSV ------------------------------------------------------------------------------------

    void csvParse_data()
    {
        QTest::addColumn<QString>("input");
        QTest::addColumn<QList<QStringList>>("expected");
        QTest::newRow("simple") << u"a,b\nc,d"_s << QList<QStringList>{{u"a"_s, u"b"_s}, {u"c"_s, u"d"_s}};
        QTest::newRow("crlf + final newline") << u"Imię,Miasto\r\nŻaneta,Łódź\r\n"_s
                                              << QList<QStringList>{{u"Imię"_s, u"Miasto"_s}, {u"Żaneta"_s, u"Łódź"_s}};
        QTest::newRow("quotes") << u"\"a,b\",\"say \"\"hi\"\"\",c"_s
                                << QList<QStringList>{{u"a,b"_s, u"say \"hi\""_s, u"c"_s}};
        QTest::newRow("newline in quotes")
                << u"\"line1\nline2\",x\ny,z"_s << QList<QStringList>{{u"line1\nline2"_s, u"x"_s}, {u"y"_s, u"z"_s}};
        QTest::newRow("empty fields") << u",,\na,,"_s
                                      << QList<QStringList>{{QString(), QString(), QString()},
                                                            {u"a"_s, QString(), QString()}};
        QTest::newRow("bom") << u"\uFEFFName,Tags\nX,\"a, b\""_s
                             << QList<QStringList>{{u"Name"_s, u"Tags"_s}, {u"X"_s, u"a, b"_s}};
        QTest::newRow("stray quote") << u"ab\"c,d"_s << QList<QStringList>{{u"ab\"c"_s, u"d"_s}};
        QTest::newRow("unterminated quote") << u"\"abc,d\nx"_s << QList<QStringList>{{u"abc,d\nx"_s}};
        QTest::newRow("empty") << QString() << QList<QStringList>{};
    }

    void csvParse()
    {
        QFETCH(QString, input);
        QFETCH(QList<QStringList>, expected);
        QCOMPARE(parseCsv(input), expected);
    }

    void csvWrite()
    {
        QCOMPARE(csvField(u"plain"_s), u"plain"_s);
        QCOMPARE(csvField(u"a,b"_s), u"\"a,b\""_s);
        QCOMPARE(csvField(u"say \"hi\""_s), u"\"say \"\"hi\"\"\""_s);
        QCOMPARE(csvField(u"two\nlines"_s), u"\"two\nlines\""_s);
        QCOMPARE(csvField(u" padded"_s), u"\" padded\""_s);
        QCOMPARE(csvRow({u"Zadanie"_s, u"a;b"_s, QString(), u"x,y"_s}), u"Zadanie,a;b,,\"x,y\""_s);
        QCOMPARE(csvRow({u"a;b"_s, u"c"_s}, u';'), u"\"a;b\";c"_s);
        // Round trip.
        const QList<QStringList> rows = {{u"ą,ę"_s, u"\"q\""_s, u"multi\r\nline"_s}, {u""_s, u"x"_s, u" y "_s}};
        QString text;
        for (const QStringList &r : rows)
            text += csvRow(r) + u"\r\n"_s;
        QCOMPARE(parseCsv(text), rows);
    }

    void csvSafeText_data()
    {
        QTest::addColumn<QString>("input");
        QTest::addColumn<QString>("expected");
        QTest::newRow("formula") << u"=HYPERLINK(\"x\")"_s << u"'=HYPERLINK(\"x\")"_s;
        QTest::newRow("plus") << u"+cmd"_s << u"'+cmd"_s;
        QTest::newRow("at") << u"@SUM(A1)"_s << u"'@SUM(A1)"_s;
        QTest::newRow("dash text") << u"-zadanie"_s << u"'-zadanie"_s;
        QTest::newRow("negative number") << u"-5.5"_s << u"-5.5"_s;
        QTest::newRow("normal") << u"Kup mleko"_s << u"Kup mleko"_s;
        QTest::newRow("empty") << QString() << QString();
    }

    void csvSafeText()
    {
        QFETCH(QString, input);
        QFETCH(QString, expected);
        QCOMPARE(io::csvSafeText(input), expected);
    }

    // --- iCalendar ------------------------------------------------------------------------------

    void icalParse()
    {
        const QByteArray data = "BEGIN:VCALENDAR\r\n"
                                "VERSION:2.0\r\n"
                                "X-WR-CALNAME:Praca\r\n"
                                "BEGIN:VEVENT\r\n"
                                "UID:1@x\r\n"
                                "SUMMARY:Spotkanie\\, ważne\\; pokój 5\\nDruga linia\r\n"
                                "DESCRIPTION:Bardzo długi opis który został złamany w środku sł\r\n"
                                " owa i jeszcze raz\r\n"
                                "\ttabem\r\n"
                                "DTSTART;TZID=\"Europe/Warsaw\":20261014T100000\r\n"
                                "BEGIN:VALARM\r\n"
                                "TRIGGER:-PT15M\r\n"
                                "END:VALARM\r\n"
                                "END:VEVENT\r\n"
                                "END:VCALENDAR\r\n";
        QStringList warnings;
        const QList<ical::Component> roots = ical::parse(data, &warnings);
        QVERIFY(warnings.isEmpty());
        QCOMPARE(roots.size(), 1);
        const ical::Component &cal = roots.first();
        QCOMPARE(cal.name, u"VCALENDAR"_s);
        QCOMPARE(cal.text(u"X-WR-CALNAME"), u"Praca"_s);
        const auto events = cal.childrenNamed(u"VEVENT");
        QCOMPARE(events.size(), 1);
        const ical::Component &ev = *events.first();
        QCOMPARE(ev.text(u"SUMMARY"), u"Spotkanie, ważne; pokój 5\nDruga linia"_s);
        QCOMPARE(ev.text(u"DESCRIPTION"),
                 u"Bardzo długi opis który został złamany w środku słowa i jeszcze raztabem"_s);
        QCOMPARE(ev.property(u"DTSTART")->param(u"TZID"_s), u"Europe/Warsaw"_s);
        QCOMPARE(ev.childrenNamed(u"VALARM").size(), 1);
        QVERIFY(ev.property(u"LOCATION") == nullptr);
    }

    void icalParseByteOrderMark()
    {
        QStringList warnings;
        const auto roots = ical::parse("\xEF\xBB\xBF"
                                       "BEGIN:VCALENDAR\r\nBEGIN:VEVENT\r\nSUMMARY:Zebranie\r\n"
                                       "END:VEVENT\r\nEND:VCALENDAR\r\n",
                                       &warnings);
        QVERIFY(warnings.isEmpty());
        QCOMPARE(roots.size(), 1);
        QCOMPARE(roots.first().name, u"VCALENDAR"_s);
        QCOMPARE(roots.first().childrenNamed(u"VEVENT").size(), 1);
    }

    void icalParseLegacyEncoding()
    {
        // Not UTF-8 (old Outlook exports on Polish Windows): read like other text files.
        const auto roots = ical::parse("BEGIN:VCALENDAR\r\nBEGIN:VEVENT\r\nSUMMARY:Spotkanie zespo\xB3u w \xA3odzi\r\n"
                                       "END:VEVENT\r\nEND:VCALENDAR\r\n");
        QCOMPARE(roots.size(), 1);
        QCOMPARE(roots.first().childrenNamed(u"VEVENT").first()->text(u"SUMMARY"), u"Spotkanie zespołu w Łodzi"_s);
    }

    void icalParseFoldInsideUtf8()
    {
        // A producer folded inside the two bytes of "ż": unfolding on bytes repairs it.
        QByteArray data = "BEGIN:VEVENT\nSUMMARY:Ża\xC5\n \xBC\x62\nEND:VEVENT\n";
        const auto roots = ical::parse(data);
        QCOMPARE(roots.size(), 1);
        QCOMPARE(roots.first().text(u"SUMMARY"), u"Żażb"_s);
    }

    void icalParseMalformed()
    {
        QStringList warnings;
        const auto roots =
                ical::parse("garbage line\nBEGIN:VCALENDAR\nBEGIN:VEVENT\nSUMMARY:x\nEND:VTODO\n:novalue\n", &warnings);
        QCOMPARE(warnings.size(), 1);
        QCOMPARE(roots.size(), 1); // unterminated components are closed
        QCOMPARE(roots.first().children.size(), 1);
        QCOMPARE(roots.first().children.first().text(u"SUMMARY"), u"x"_s);
        QVERIFY(ical::parse(QByteArray()).isEmpty());
        QByteArray deep;
        for (int i = 0; i < 100; ++i)
            deep += "BEGIN:X\n";
        QCOMPARE(ical::parse(deep).size(), 1); // nesting is limited, no crash
    }

    void icalEscape()
    {
        const QString text = u"Zażółć, gęślą; jaźń\\ \nnowa\r\nlinia"_s;
        const QString escaped = ical::escapeText(text);
        QCOMPARE(escaped, u"Zażółć\\, gęślą\\; jaźń\\\\ \\nnowa\\nlinia"_s);
        QCOMPARE(ical::unescapeText(escaped), u"Zażółć, gęślą; jaźń\\ \nnowa\nlinia"_s);
        QCOMPARE(ical::unescapeText(u"a\\Nb\\"_s), u"a\nb\\"_s);
        // TEXT values cannot carry control characters (RFC 5545 3.3.11); tabs are allowed.
        QCOMPARE(ical::escapeText(u"a\u0001b\u001Bc\td\u007F"_s), u"abc\td"_s);
    }

    void icalFold_data()
    {
        QTest::addColumn<QString>("line");
        QTest::newRow("short") << u"SUMMARY:krótko"_s;
        QTest::newRow("ascii 200") << u"DESCRIPTION:"_s + QString(200, u'a');
        QTest::newRow("polish 200") << u"DESCRIPTION:"_s + QString(100, u'ż') + QString(50, u'ą');
        QTest::newRow("emoji") << u"SUMMARY:"_s + QString(40, u'x') + u"⏰📅🔁"_s.repeated(20);
        QTest::newRow("exactly 75") << QString(75, u'x');
        QTest::newRow("76") << QString(76, u'x');
    }

    void icalFold()
    {
        QFETCH(QString, line);
        const QByteArray folded = ical::foldLine(line);
        const QList<QByteArray> parts = folded.split('\n');
        for (qsizetype k = 0; k < parts.size(); ++k) {
            QByteArray p = parts[k];
            if (p.endsWith('\r'))
                p.chop(1);
            QVERIFY2(p.size() <= 75, p.constData());
            if (k > 0)
                QVERIFY(p.startsWith(' '));
            // Every part is valid UTF-8 on its own (no split sequence).
            QStringDecoder dec(QStringDecoder::Utf8, QStringDecoder::Flag::Stateless);
            const QString s = dec(p);
            QVERIFY(!dec.hasError());
            Q_UNUSED(s);
        }
        // Unfolding gives the line back.
        const QByteArray data = QByteArray("BEGIN:X\r\nP:") + folded + QByteArray("\r\nEND:X\r\n");
        const auto roots = ical::parse(data);
        QCOMPARE(roots.first().property(u"P")->value, line);
    }

    void icalDateTime_data()
    {
        QTest::addColumn<QString>("value");
        QTest::addColumn<QString>("valueParam");
        QTest::addColumn<QString>("tzid");
        QTest::addColumn<QString>("expected"); // wall clock in Europe/Warsaw, "" for invalid
        QTest::newRow("date") << u"20261014"_s << QString() << QString() << u"2026-10-14"_s;
        QTest::newRow("value=date") << u"20261014"_s << u"DATE"_s << QString() << u"2026-10-14"_s;
        QTest::newRow("floating") << u"20261014T100000"_s << QString() << QString() << u"2026-10-14 10:00"_s;
        QTest::newRow("utc summer") << u"20260714T080000Z"_s << QString() << QString() << u"2026-07-14 10:00"_s;
        QTest::newRow("utc winter") << u"20261214T080000Z"_s << QString() << QString() << u"2026-12-14 09:00"_s;
        QTest::newRow("utc across midnight")
                << u"20261231T233000Z"_s << QString() << QString() << u"2027-01-01 00:30"_s;
        QTest::newRow("tzid new york") << u"20261014T100000"_s << QString() << u"America/New_York"_s
                                       << u"2026-10-14 16:00"_s;
        QTest::newRow("tzid warsaw") << u"20261014T100000"_s << QString() << u"Europe/Warsaw"_s
                                     << u"2026-10-14 10:00"_s;
        QTest::newRow("tzid mozilla prefix") << u"20261014T100000"_s << QString()
                                             << u"/mozilla.org/20050126_1/Europe/London"_s << u"2026-10-14 11:00"_s;
        QTest::newRow("tzid windows") << u"20261014T100000"_s << QString() << u"Pacific Standard Time"_s
                                      << u"2026-10-14 19:00"_s;
        // Also what every TZID gets where Qt has no time zone database (WebAssembly).
        QTest::newRow("unknown tzid -> floating")
                << u"20261014T100000"_s << QString() << u"Mars/Olympus"_s << u"2026-10-14 10:00"_s;
        QTest::newRow("dashed") << u"2026-10-14T10:00:00Z"_s << QString() << QString() << u"2026-10-14 12:00"_s;
        QTest::newRow("bad date") << u"20261332"_s << QString() << QString() << QString();
        QTest::newRow("bad time") << u"20261014T250000"_s << QString() << QString() << QString();
        QTest::newRow("garbage") << u"tomorrow"_s << QString() << QString() << QString();
    }

    void icalDateTime()
    {
        QFETCH(QString, value);
        QFETCH(QString, valueParam);
        QFETCH(QString, tzid);
        QFETCH(QString, expected);
        ical::Property p;
        p.name = u"DTSTART"_s;
        p.value = value;
        if (!valueParam.isEmpty())
            p.params.insert(u"VALUE"_s, valueParam);
        if (!tzid.isEmpty())
            p.params.insert(u"TZID"_s, tzid);
        const auto dt = ical::parseDateTime(p);
        if (expected.isEmpty()) {
            QVERIFY(!dt);
            return;
        }
        QVERIFY(dt);
        bool unknown = false;
        QCOMPARE(ical::toWallClock(*dt, iotest::warsaw(), &unknown).toString(), expected);
        QCOMPARE(unknown, tzid == u"Mars/Olympus");
    }

    void icalDuration_data()
    {
        QTest::addColumn<QString>("value");
        QTest::addColumn<qint64>("seconds");
        QTest::addColumn<bool>("valid");
        QTest::newRow("15 min before") << u"-PT15M"_s << qint64(-900) << true;
        QTest::newRow("1 day 2 h") << u"P1DT2H"_s << qint64(93600) << true;
        QTest::newRow("2 weeks") << u"P2W"_s << qint64(1209600) << true;
        QTest::newRow("plus seconds") << u"+PT30S"_s << qint64(30) << true;
        QTest::newRow("zero") << u"PT0S"_s << qint64(0) << true;
        QTest::newRow("empty P") << u"P"_s << qint64(0) << false;
        QTest::newRow("dangling T") << u"P1DT"_s << qint64(0) << false;
        QTest::newRow("garbage") << u"15 minutes"_s << qint64(0) << false;
    }

    void icalDuration()
    {
        QFETCH(QString, value);
        QFETCH(qint64, seconds);
        QFETCH(bool, valid);
        const auto d = ical::parseDuration(value);
        QCOMPARE(d.has_value(), valid);
        if (valid)
            QCOMPARE(*d, seconds);
    }

    void icalRRule_data()
    {
        QTest::addColumn<QString>("rrule");
        QTest::addColumn<QString>("canonical"); // "" = not representable
        QTest::addColumn<QStringList>("dropped");
        QTest::addColumn<QString>("formatted");
        QTest::newRow("daily") << u"FREQ=DAILY"_s << u"every day"_s << QStringList() << u"FREQ=DAILY"_s;
        QTest::newRow("every 3 days") << u"FREQ=DAILY;INTERVAL=3"_s << u"every 3 days"_s << QStringList()
                                      << u"FREQ=DAILY;INTERVAL=3"_s;
        QTest::newRow("weekdays") << u"FREQ=DAILY;BYDAY=MO,TU,WE,TH,FR"_s << u"every weekday"_s << QStringList()
                                  << u"FREQ=DAILY;BYDAY=MO,TU,WE,TH,FR"_s;
        QTest::newRow("weekly days") << u"FREQ=WEEKLY;BYDAY=FR,MO;WKST=MO"_s << u"every week on Monday, Friday"_s
                                     << QStringList() << u"FREQ=WEEKLY;BYDAY=MO,FR"_s;
        QTest::newRow("biweekly") << u"freq=weekly;interval=2"_s << u"every 2 weeks"_s << QStringList()
                                  << u"FREQ=WEEKLY;INTERVAL=2"_s;
        QTest::newRow("monthly 15th") << u"FREQ=MONTHLY;BYMONTHDAY=15"_s << u"every month on the 15th"_s
                                      << QStringList() << u"FREQ=MONTHLY;BYMONTHDAY=15"_s;
        QTest::newRow("monthly last") << u"FREQ=MONTHLY;BYMONTHDAY=-1"_s << u"every month on the last"_s
                                      << QStringList() << u"FREQ=MONTHLY;BYMONTHDAY=-1"_s;
        QTest::newRow("yearly with count") << u"FREQ=YEARLY;COUNT=5;BYMONTH=10"_s << u"every year"_s
                                           << QStringList({u"BYMONTH"_s, u"COUNT"_s}) << u"FREQ=YEARLY"_s;
        QTest::newRow("until") << u"FREQ=WEEKLY;UNTIL=20271231T000000Z"_s << u"every week"_s
                               << QStringList({u"UNTIL"_s}) << u"FREQ=WEEKLY"_s;
        QTest::newRow("second tuesday") << u"FREQ=MONTHLY;BYDAY=2TU"_s << QString() << QStringList() << QString();
        QTest::newRow("hourly") << u"FREQ=HOURLY"_s << QString() << QStringList() << QString();
        QTest::newRow("many month days") << u"FREQ=MONTHLY;BYMONTHDAY=1,15"_s << QString() << QStringList()
                                         << QString();
        QTest::newRow("no freq") << u"INTERVAL=2"_s << QString() << QStringList() << QString();
        QTest::newRow("garbage") << u"every week"_s << QString() << QStringList() << QString();
        QTest::newRow("huge interval") << u"FREQ=DAILY;INTERVAL=99999999"_s << QString() << QStringList() << QString();
    }

    void icalRRule()
    {
        QFETCH(QString, rrule);
        QFETCH(QString, canonical);
        QFETCH(QStringList, dropped);
        QFETCH(QString, formatted);
        QStringList gotDropped;
        const auto rule = ical::parseRRule(rrule, &gotDropped);
        QCOMPARE(rule.has_value(), !canonical.isEmpty());
        if (!rule)
            return;
        QCOMPARE(dates::formatRecurrence(*rule, dates::UiLocale::English), canonical);
        QCOMPARE(gotDropped, dropped);
        QCOMPARE(ical::formatRRule(*rule), formatted);
    }

    void icalFormatRRuleFromText_data()
    {
        QTest::addColumn<QString>("rule");
        QTest::addColumn<QString>("rrule");
        QTest::newRow("polish weekly") << u"co tydzień w poniedziałek, piątek"_s << u"FREQ=WEEKLY;BYDAY=MO,FR"_s;
        QTest::newRow("when done ignored") << u"every 2 days when done"_s << u"FREQ=DAILY;INTERVAL=2"_s;
        QTest::newRow("every 2 years") << u"co 2 lata"_s << u"FREQ=YEARLY;INTERVAL=2"_s;
        QTest::newRow("weekdays of week") << u"every weekday"_s << u"FREQ=DAILY;BYDAY=MO,TU,WE,TH,FR"_s;
    }

    void icalFormatRRuleFromText()
    {
        QFETCH(QString, rule);
        QFETCH(QString, rrule);
        const auto parsed = dates::parseRecurrence(rule);
        QVERIFY(parsed);
        QCOMPARE(ical::formatRRule(*parsed), rrule);
    }

    void icalWriter()
    {
        ical::Writer w;
        w.begin(u"VCALENDAR");
        w.property(u"VERSION", u"2.0");
        w.text(u"SUMMARY", u"Kup mleko, chleb; masło"_s);
        w.property(u"DTSTART", u"20261015", {{u"VALUE"_s, u"DATE"_s}});
        w.property(u"X-TEST", u"a\nb", {{u"CN"_s, u"Kowalski, Jan: \"szef\""_s}});
        w.end(u"VCALENDAR");
        QCOMPARE(QString::fromUtf8(w.data()),
                 u"BEGIN:VCALENDAR\r\nVERSION:2.0\r\nSUMMARY:Kup mleko\\, chleb\\; masło\r\n"
                 u"DTSTART;VALUE=DATE:20261015\r\nX-TEST;CN=\"Kowalski, Jan: szef\":a b\r\nEND:VCALENDAR\r\n"_s);
        QCOMPARE(ical::formatDate(QDate(2026, 1, 5)), u"20260105"_s);
        QCOMPARE(ical::formatLocalDateTime(QDate(2026, 1, 5), QTime(9, 7)), u"20260105T090700"_s);
        QCOMPARE(ical::formatUtc(QDateTime(QDate(2026, 10, 15), QTime(12, 0), iotest::warsaw())),
                 u"20261015T100000Z"_s);
    }
};

QTEST_MAIN(TestIoFormats)
#include "tst_io_formats.moc"
