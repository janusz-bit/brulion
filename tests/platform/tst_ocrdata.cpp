// Where OCR's engine and models are looked for and which languages it reads (src/platform/ocrdata.h)
// — pure, so tested on every platform: the library's candidates (the environment's switch "none",
// the build's path, Linux's and Windows' names), the models' folders in order, model files to
// language codes (osd, equ and odd names left out), UI languages to Tesseract's codes, and the
// languages chosen from the setting and what is installed.
#include "ocrdata.h"

#include <QTest>

using namespace Qt::StringLiterals;
using namespace brulion::platform;

class TestOcrData : public QObject {
    Q_OBJECT

private Q_SLOTS:
    void libraryCandidates()
    {
        QCOMPARE(tesseractLibraryCandidates(u"none"_s, u"/nix/store/x/lib/libtesseract.so.5"_s, false, {}),
                 QStringList());
        QCOMPARE(tesseractLibraryCandidates(u"NONE"_s, {}, true, {}), QStringList());
        const QStringList linux = tesseractLibraryCandidates(u"/opt/t/libtesseract.so"_s,
                                                             u"/nix/store/x/lib/libtesseract.so.5"_s, false, {});
        QCOMPARE(linux, (QStringList{u"/opt/t/libtesseract.so"_s, u"/nix/store/x/lib/libtesseract.so.5"_s,
                                     u"libtesseract.so.5"_s, u"libtesseract.so.4"_s}));
        QCOMPARE(tesseractLibraryCandidates({}, {}, false, {}),
                 (QStringList{u"libtesseract.so.5"_s, u"libtesseract.so.4"_s}));
        const QStringList windows =
                tesseractLibraryCandidates({}, {}, true, {u"C:/Program Files/Tesseract-OCR"_s, QString()});
        QCOMPARE(windows.first(), u"C:/Program Files/Tesseract-OCR/libtesseract-5.dll"_s);
        QVERIFY(windows.contains(u"tesseract55.dll"_s));
        QVERIFY(windows.indexOf(u"C:/Program Files/Tesseract-OCR/tesseract41.dll"_s)
                < windows.indexOf(u"libtesseract-5.dll"_s));
        // Duplicates once.
        QCOMPARE(tesseractLibraryCandidates(u"libtesseract.so.5"_s, {}, false, {}).count(u"libtesseract.so.5"_s), 1);
    }

    void dataCandidates()
    {
        const QStringList linux =
                tessdataCandidates(u"/home/ola/.local/share/Brulion/tessdata"_s, {u"/env/tessdata/"_s, QString()},
                                   {u"/nix/store/d"_s}, u"/nix/store/t/lib"_s, false);
        QCOMPARE(linux.mid(0, 5),
                 (QStringList{u"/home/ola/.local/share/Brulion/tessdata"_s, u"/env/tessdata"_s, u"/nix/store/d"_s,
                              u"/nix/store/t/share/tessdata"_s, u"/nix/store/t/lib/tessdata"_s}));
        QVERIFY(linux.contains(u"/usr/share/tesseract-ocr/5/tessdata"_s));
        QVERIFY(linux.contains(u"/usr/share/tessdata"_s));
        const QStringList windows = tessdataCandidates({}, {}, {}, u"C:/Program Files/Tesseract-OCR"_s, true);
        QVERIFY(windows.contains(u"C:/Program Files/Tesseract-OCR/tessdata"_s));
        QVERIFY(!windows.contains(u"/usr/share/tessdata"_s));
        QVERIFY(tessdataCandidates({}, {}, {}, {}, true).isEmpty());
    }

    void languagesOfFiles()
    {
        QCOMPARE(tessdataLanguages({u"pol.traineddata"_s, u"eng.traineddata"_s, u"osd.traineddata"_s,
                                    u"equ.traineddata"_s, u"pdf.ttf"_s, u"configs"_s, u"chi_sim.traineddata"_s,
                                    u"x.traineddata"_s, u"a b.traineddata"_s, u"pol.traineddata"_s}),
                 (QStringList{u"chi_sim"_s, u"eng"_s, u"pol"_s}));
        QVERIFY(tessdataLanguages({}).isEmpty());
    }

    void codes_data()
    {
        QTest::addColumn<QString>("language");
        QTest::addColumn<QString>("code");
        QTest::newRow("pl") << u"pl"_s << u"pol"_s;
        QTest::newRow("en") << u"en"_s << u"eng"_s;
        QTest::newRow("region") << u"pt_BR"_s << u"por"_s;
        QTest::newRow("dash") << u"de-AT"_s << u"deu"_s;
        QTest::newRow("upper") << u"PL"_s << u"pol"_s;
        QTest::newRow("unknown") << u"xx"_s << QString();
        QTest::newRow("empty") << QString() << QString();
    }
    void codes()
    {
        QFETCH(QString, language);
        QFETCH(QString, code);
        QCOMPARE(tesseractCode(language), code);
    }

    void chosen_data()
    {
        QTest::addColumn<QString>("setting");
        QTest::addColumn<QString>("ui");
        QTest::addColumn<QStringList>("installed");
        QTest::addColumn<QString>("languages");
        const QStringList both{u"eng"_s, u"pol"_s};
        QTest::newRow("auto pl") << u"auto"_s << u"pl"_s << both << u"pol+eng"_s;
        QTest::newRow("auto en") << u"auto"_s << u"en"_s << both << u"eng"_s;
        QTest::newRow("empty is auto") << QString() << u"pl"_s << both << u"pol+eng"_s;
        QTest::newRow("auto, Polish missing") << u"auto"_s << u"pl"_s << QStringList{u"eng"_s} << u"eng"_s;
        QTest::newRow("auto, neither") << u"auto"_s << u"pl"_s << QStringList{u"deu"_s, u"fra"_s} << u"deu"_s;
        QTest::newRow("auto, none") << u"auto"_s << u"pl"_s << QStringList() << QString();
        QTest::newRow("chosen") << u"eng,pol"_s << u"pl"_s << both << u"eng+pol"_s;
        QTest::newRow("chosen plus") << u"pol+eng"_s << u"en"_s << both << u"pol+eng"_s;
        QTest::newRow("UI codes") << u"pl en"_s << u"en"_s << both << u"pol+eng"_s;
        QTest::newRow("not installed left out") << u"deu,pol"_s << u"pl"_s << both << u"pol"_s;
        QTest::newRow("nothing installed chosen") << u"deu"_s << u"pl"_s << both << QString();
        QTest::newRow("repeats") << u"pol,pol,pl"_s << u"pl"_s << both << u"pol"_s;
        QTest::newRow("script model") << u"chi_sim"_s << u"pl"_s << QStringList{u"chi_sim"_s} << u"chi_sim"_s;
    }
    void chosen()
    {
        QFETCH(QString, setting);
        QFETCH(QString, ui);
        QFETCH(QStringList, installed);
        QFETCH(QString, languages);
        QCOMPARE(ocrLanguagesFor(setting, ui, installed), languages);
    }
};

QTEST_GUILESS_MAIN(TestOcrData)
#include "tst_ocrdata.moc"
