// The platform's file calls for the vault's own writes (src/platform/filesync.h): Windows'
// extended-length paths for MoveFileExW (pure: checked everywhere) — drive paths, shares, paths
// with a prefix or of a device kept, relative ones not prefixed, Polish names, a path deeper than
// MAX_PATH — and the installed sync and rename: a vault write through them lands, synced, with no
// temporary file left.
#include "filesync.h"

#include "vault/filesync.h"

#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <QTest>

using namespace Qt::StringLiterals;

class TestFileSync : public QObject {
    Q_OBJECT

private Q_SLOTS:
    void extendedLengthPath_data()
    {
        QTest::addColumn<QString>("path");
        QTest::addColumn<QString>("expected");
        QTest::newRow("drive") << u"C:/Users/Jan/Notatki/Zakupy.md"_s << u"\\\\?\\C:\\Users\\Jan\\Notatki\\Zakupy.md"_s;
        QTest::newRow("drive, backslashes") << u"D:\\Notatki\\.brulion-tmp-0123456789abcdef"_s
                                            << u"\\\\?\\D:\\Notatki\\.brulion-tmp-0123456789abcdef"_s;
        QTest::newRow("Polish") << u"C:/Użytkownicy/Łucja/Dziennik/2026-11-18 środa.md"_s
                                << u"\\\\?\\C:\\Użytkownicy\\Łucja\\Dziennik\\2026-11-18 środa.md"_s;
        QTest::newRow("share") << u"//serwer/wspólne/Notatki/a.md"_s << u"\\\\?\\UNC\\serwer\\wspólne\\Notatki\\a.md"_s;
        QTest::newRow("share, backslashes") << u"\\\\serwer\\dział\\a.md"_s << u"\\\\?\\UNC\\serwer\\dział\\a.md"_s;
        QTest::newRow("prefixed already") << u"\\\\?\\C:\\a.md"_s << u"\\\\?\\C:\\a.md"_s;
        QTest::newRow("prefixed share") << u"\\\\?\\UNC\\s\\x\\a.md"_s << u"\\\\?\\UNC\\s\\x\\a.md"_s;
        QTest::newRow("device") << u"\\\\.\\pipe\\x"_s << u"\\\\.\\pipe\\x"_s;
        QTest::newRow("relative") << u"Notatki/a.md"_s << u"Notatki\\a.md"_s;
        QTest::newRow("drive-relative") << u"C:a.md"_s << u"C:a.md"_s;
        QTest::newRow("root of a drive") << u"E:/"_s << u"\\\\?\\E:\\"_s;
        QTest::newRow("empty") << QString() << QString();
        QString deep = u"C:/Notatki"_s;
        while (deep.size() < 300)
            deep += u"/Bardzo długi folder z notatkami"_s;
        QString deepNative = deep;
        deepNative.replace(u'/', u'\\');
        QTest::newRow("deeper than MAX_PATH") << deep + u"/a.md"_s << u"\\\\?\\"_s + deepNative + u"\\a.md"_s;
    }
    void extendedLengthPath()
    {
        QFETCH(QString, path);
        QFETCH(QString, expected);
        QCOMPARE(brulion::platform::extendedLengthPath(path), expected);
    }

    // What installFileSync() sets is what the vault's writes use: a sync that succeeds on an open
    // file and fails on a closed one with a reason; on Windows a rename that replaces the file.
    void installed()
    {
        brulion::platform::installFileSync();
        const brulion::vaultutil::FileSync sync = brulion::vaultutil::fileSync();
        QVERIFY(sync);
        QTemporaryDir dir;
        QFile file(dir.path() + u"/Zakupy.md"_s);
        QVERIFY(file.open(QIODevice::WriteOnly));
        QCOMPARE(file.write("- chleb\n"), qint64(8));
        QVERIFY(file.flush());
        QString error;
        QVERIFY2(sync(file, &error), qPrintable(error));
        file.close();
        QVERIFY(!sync(file, &error));
        QVERIFY(!error.isEmpty());
#ifdef Q_OS_WIN
        const brulion::vaultutil::FileReplace replace = brulion::vaultutil::fileReplace();
        QVERIFY(replace);
        QFile other(dir.path() + u"/.brulion-tmp-0123456789abcdef"_s);
        QVERIFY(other.open(QIODevice::WriteOnly));
        QCOMPARE(other.write("- mleko\n"), qint64(8));
        other.close();
        QCOMPARE(replace(other.fileName(), file.fileName(), &error), brulion::vaultutil::Replaced::Done);
        QVERIFY(!QFile::exists(other.fileName()));
        QVERIFY(file.open(QIODevice::ReadOnly));
        QCOMPARE(file.readAll(), QByteArray("- mleko\n"));
        file.close();
        // A file held open without sharing its deletion: Busy (a virus scanner would let go).
        QFile held(file.fileName());
        QVERIFY(held.open(QIODevice::ReadOnly)); // (Qt shares reading and writing, not deletion)
        QFile again(dir.path() + u"/.brulion-tmp-fedcba9876543210"_s);
        QVERIFY(again.open(QIODevice::WriteOnly));
        again.close();
        QCOMPARE(replace(again.fileName(), file.fileName(), &error), brulion::vaultutil::Replaced::Busy);
        QVERIFY(!error.isEmpty());
        held.close();
        QCOMPARE(replace(again.fileName(), file.fileName(), &error), brulion::vaultutil::Replaced::Done);
#else
        QVERIFY(!brulion::vaultutil::fileReplace()); // (std::filesystem::rename(): POSIX rename())
#endif
    }
};

QTEST_GUILESS_MAIN(TestFileSync)
#include "tst_filesync.moc"
