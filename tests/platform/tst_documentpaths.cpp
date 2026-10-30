// The file an Android document URI names (src/platform/documentpaths.h), as the system's picker
// hands them out: the phone's storage ("primary", "home"), a memory card, tree URIs, Downloads'
// "raw:" paths, Polish names percent-encoded; and what names no file — MediaStore, cloud drives,
// opaque ids, ".." leaving the volume, other schemes, broken URIs.
#include "documentpaths.h"

#include <QTest>

using namespace Qt::StringLiterals;
using brulion::platform::localPathOfDocument;
using brulion::platform::primaryStorageOf;

class TestDocumentPaths : public QObject {
    Q_OBJECT

private Q_SLOTS:
    void localPath_data()
    {
        QTest::addColumn<QString>("url");
        QTest::addColumn<QString>("primary");
        QTest::addColumn<QString>("expected");
        const QString phone = u"/storage/emulated/0"_s;
        const QString storage = u"content://com.android.externalstorage.documents"_s;
        QTest::newRow("the phone's storage, Polish name")
                << storage + u"/document/primary%3ANotatki%2FWyk%C5%82ady%2FFizyka%20%E2%80%93%20wyk%C5%82ad%203.pdf"_s
                << phone << u"/storage/emulated/0/Notatki/Wykłady/Fizyka – wykład 3.pdf"_s;
        QTest::newRow("another user's storage")
                << storage + u"/document/primary%3ANotatki%2Fa.pdf"_s << u"/storage/emulated/10"_s
                << u"/storage/emulated/10/Notatki/a.pdf"_s;
        QTest::newRow("a tree URI")
                << storage + u"/tree/primary%3ANotatki/document/primary%3ANotatki%2Fza%C5%BC%C3%B3%C5%82%C4%87.png"_s
                << phone << u"/storage/emulated/0/Notatki/zażółć.png"_s;
        QTest::newRow("the storage's root") << storage + u"/document/primary%3A"_s << phone << phone;
        QTest::newRow("Documents (home)") << storage + u"/document/home%3Aslajdy.pdf"_s << phone
                                          << u"/storage/emulated/0/Documents/slajdy.pdf"_s;
        QTest::newRow("a memory card") << storage + u"/document/1234-ABCD%3AStudia%2Fwyk%C5%82ad.pdf"_s << phone
                                       << u"/storage/1234-ABCD/Studia/wykład.pdf"_s;
        QTest::newRow("a memory card without the phone's storage")
                << storage + u"/document/0A1B-2C3D%3Aa.pdf"_s << QString() << u"/storage/0A1B-2C3D/a.pdf"_s;
        QTest::newRow("Downloads' raw path")
                << u"content://com.android.providers.downloads.documents/document/raw%3A%2Fstorage%2Femulated%2F0%2FDownload%2Fwyk%C5%82ad.pdf"_s
                << phone << u"/storage/emulated/0/Download/wykład.pdf"_s;
        QTest::newRow("a doubled slash is cleaned") << storage + u"/document/primary%3ANotatki%2F%2Fa.pdf"_s << phone
                                                    << u"/storage/emulated/0/Notatki/a.pdf"_s;
        // Nothing to say which file it is.
        QTest::newRow("no phone storage known") << storage + u"/document/primary%3Aa.pdf"_s << QString() << QString();
        QTest::newRow("home without it") << storage + u"/document/home%3Aa.pdf"_s << QString() << QString();
        QTest::newRow("leaves the volume") << storage + u"/document/primary%3A..%2F..%2Fdata%2Fx.pdf"_s << phone
                                           << QString();
        QTest::newRow("leaves the card") << storage + u"/document/1234-ABCD%3Aa%2F..%2F..%2Fx"_s << phone << QString();
        QTest::newRow("a volume that is no id") << storage + u"/document/..%3Ax.pdf"_s << phone << QString();
        QTest::newRow("no volume") << storage + u"/document/Notatki%2Fa.pdf"_s << phone << QString();
        QTest::newRow("no document") << storage + u"/tree/primary%3ANotatki"_s << phone << QString();
        QTest::newRow("Downloads' opaque id")
                << u"content://com.android.providers.downloads.documents/document/msf%3A1000012345"_s << phone
                << QString();
        QTest::newRow("Downloads' raw path leaving it")
                << u"content://com.android.providers.downloads.documents/document/raw%3A%2Fstorage%2F..%2Fdata%2Fx"_s
                << phone << QString();
        QTest::newRow("MediaStore") << u"content://com.android.providers.media.documents/document/image%3A42"_s
                                    << phone << QString();
        QTest::newRow("a cloud drive")
                << u"content://com.google.android.apps.docs.storage/document/acc%3D1%3Bdoc%3Dencoded%3Dabc"_s << phone
                << QString();
        QTest::newRow("a local file is no document") << u"file:///storage/emulated/0/a.pdf"_s << phone << QString();
        QTest::newRow("no authority") << u"content:///document/primary%3Aa.pdf"_s << phone << QString();
        QTest::newRow("empty") << QString() << phone << QString();
        QTest::newRow("garbage") << u"to nie jest adres ąę"_s << phone << QString();
    }
    void localPath()
    {
        QFETCH(QString, url);
        QFETCH(QString, primary);
        QFETCH(QString, expected);
        QCOMPARE(localPathOfDocument(QUrl(url), primary), expected);
    }

    void primaryStorage_data()
    {
        QTest::addColumn<QString>("path");
        QTest::addColumn<QString>("expected");
        QTest::newRow("a notebook on the phone") << u"/storage/emulated/0/Notatki"_s << u"/storage/emulated/0"_s;
        QTest::newRow("the storage itself") << u"/storage/emulated/0"_s << u"/storage/emulated/0"_s;
        QTest::newRow("a work profile") << u"/storage/emulated/10/Brulion"_s << u"/storage/emulated/10"_s;
        QTest::newRow("a memory card") << u"/storage/1234-ABCD/Notatki"_s << QString();
        QTest::newRow("the app's folder") << u"/data/user/0/io.github.januszbit.brulion/files/Brulion"_s
                                          << QString();
        QTest::newRow("a desktop path") << u"/home/natalia/Notatki"_s << QString();
        QTest::newRow("not a user") << u"/storage/emulated/obb/x"_s << QString();
        QTest::newRow("a longer name") << u"/storage/emulated/0abc/x"_s << QString();
        QTest::newRow("empty") << QString() << QString();
    }
    void primaryStorage()
    {
        QFETCH(QString, path);
        QFETCH(QString, expected);
        QCOMPARE(primaryStorageOf(path), expected);
    }
};

QTEST_GUILESS_MAIN(TestDocumentPaths)
#include "tst_documentpaths.moc"
