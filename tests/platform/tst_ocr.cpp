// OCR (src/platform/ocr.h) with this build's backend — Tesseract in the Nix dev shell and package
// (its English and Polish "fast" models), whatever the computer has elsewhere; skipped where it is
// not available (CI's Ubuntu without Tesseract, Android, the browser): the pictures of
// tests/data/attachments (make-fixtures.sh: written by writeFixtures() below with the bundled
// Literata, so they are the same everywhere) — a picture with Polish text, a photo stored sideways
// with an EXIF orientation, the page of a scanned PDF drawn by this build's PDF renderer — read with
// their words in place; a picture without text, a cancelled read, a picture too large, unknown
// languages; "BRULION_TESSERACT_LIBRARY=none" turns OCR off (a child process: the library is looked
// for once).
#include "ocr.h"
#include "pdfpages.h"

#include "text/text.h"

#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFontDatabase>
#include <QImageReader>
#include <QImageWriter>
#include <QPageLayout>
#include <QPageSize>
#include <QPainter>
#include <QPdfWriter>
#include <QProcess>
#include <QTest>

using namespace Qt::StringLiterals;
using namespace brulion;
using namespace brulion::platform;

namespace {

QString dataDir()
{
    return QStringLiteral(BRULION_TEST_DATA) + u"/attachments"_s;
}

QString fontFile()
{
    return QStringLiteral(BRULION_FONTS_DIR) + u"/ui/BrulionLiterata-Regular.ttf"_s;
}

/// The words OCR read, folded and joined by spaces ("zazolc gesla jazn").
QString foldedWords(const OcrResult &result)
{
    QStringList words;
    for (const OcrWord &w : result.words)
        words.append(text::foldKey(w.text));
    return words.join(u' ');
}

const OcrWord *findWord(const OcrResult &result, const QString &folded)
{
    for (const OcrWord &w : result.words) {
        if (text::foldKey(w.text).contains(folded))
            return &w;
    }
    return nullptr;
}

/// A white picture with `lines` of text (Literata, `pixelSize` px), black.
QImage textPicture(QSize size, const QStringList &lines, int pixelSize)
{
    QImage image(size, QImage::Format_RGB32);
    image.fill(Qt::white);
    QPainter p(&image);
    p.setRenderHint(QPainter::TextAntialiasing);
    const int id = QFontDatabase::addApplicationFont(fontFile());
    const QStringList families = QFontDatabase::applicationFontFamilies(id);
    QFont font(families.isEmpty() ? QString() : families.constFirst());
    font.setPixelSize(pixelSize);
    p.setFont(font);
    p.setPen(Qt::black);
    int y = pixelSize * 2;
    for (const QString &line : lines) {
        p.drawText(QPoint(pixelSize, y), line);
        y += pixelSize * 2;
    }
    p.end();
    return image;
}

bool needsOcr()
{
    if (!ocrAvailable())
        return false;
    const QStringList l = ocrLanguages();
    return l.contains(u"pol"_s) && l.contains(u"eng"_s);
}

} // namespace

class TestOcr : public QObject {
    Q_OBJECT

private Q_SLOTS:
    void initTestCase()
    {
        qInfo("OCR backend %s: %s (%s), models %s in %s", qPrintable(ocrBackend()),
              ocrAvailable() ? "available" : "not available", qPrintable(ocrUnavailableReason()),
              qPrintable(ocrLanguages().join(u',')), qPrintable(ocrDataDir()));
    }

    void stateIsConsistent()
    {
        if (ocrBackend() == u"none"_s) {
            QVERIFY(!ocrAvailable());
            QCOMPARE(ocrUnavailableReason(), u"unsupported"_s);
            QVERIFY(ocrLanguages().isEmpty());
            QString error;
            QVERIFY(!OcrEngine::create(u"eng"_s, &error));
            QCOMPARE(error, u"unsupported"_s);
            return;
        }
        QCOMPARE(ocrBackend(), u"tesseract"_s);
        if (ocrAvailable()) {
            QVERIFY(ocrUnavailableReason().isEmpty());
            QVERIFY(ocrEngineName().startsWith(u"Tesseract "_s));
            QVERIFY(!ocrLanguages().isEmpty());
            QVERIFY(!ocrLanguages().contains(u"osd"_s));
            QVERIFY(!ocrDataDir().isEmpty());
        } else {
            QVERIFY(ocrUnavailableReason() == u"noLibrary"_s || ocrUnavailableReason() == u"noLanguages"_s);
        }
        QVERIFY(ocrUserDataDir().endsWith(u"tessdata"_s));
    }

    void unknownLanguage()
    {
        if (!ocrAvailable())
            QSKIP("no OCR here");
        QString error;
        QVERIFY(!OcrEngine::create(u"xyz"_s, &error));
        QVERIFY(!error.isEmpty());
        QVERIFY(!OcrEngine::create(QString(), &error));
    }

    void polishPicture()
    {
        if (!needsOcr())
            QSKIP("Tesseract with Polish and English models is not available here");
        QImage image(dataDir() + u"/tablica.png"_s);
        QVERIFY(!image.isNull());
        std::unique_ptr<OcrEngine> engine = OcrEngine::create(u"pol+eng"_s);
        QVERIFY(engine);
        QCOMPARE(engine->languages(), u"pol+eng"_s);
        QElapsedTimer timer;
        timer.start();
        const OcrResult result = engine->recognize(image, 0);
        qInfo("tablica.png (%dx%d): %lld ms, %lld words", image.width(), image.height(), qint64(timer.elapsed()),
              qint64(result.words.size()));
        QVERIFY(result.ok);
        const QString words = foldedWords(result);
        for (const QString &w : {u"spotkanie"_s, u"zespolu"_s, u"zolw"_s, u"powoli"_s, u"lake"_s})
            QVERIFY2(words.contains(w), qPrintable(words));
        // Diacritics as written (the Polish model).
        QStringList raw;
        for (const OcrWord &w : result.words)
            raw.append(w.text);
        QVERIFY2(raw.join(u' ').contains(u"Żółw"_s), qPrintable(raw.join(u' ')));
        // Boxes where the words are: the first line above the second, inside the picture.
        const OcrWord *first = findWord(result, u"spotkanie"_s);
        const OcrWord *second = findWord(result, u"zolw"_s);
        QVERIFY(first && second);
        QVERIFY(first->box.bottom() < second->box.top());
        QVERIFY(QRect(QPoint(0, 0), image.size()).contains(first->box));
        QVERIFY(first->line < second->line);
        for (const OcrWord &w : result.words)
            QVERIFY(w.confidence >= MinOcrConfidence);
    }

    void sidewaysPhoto()
    {
        if (!needsOcr())
            QSKIP("Tesseract with Polish and English models is not available here");
        // Stored lying on its side, upright through its EXIF orientation (a phone's photo).
        QImageReader reader(dataDir() + u"/obrocone.jpg"_s);
        reader.setAutoTransform(true);
        const QImage image = reader.read();
        QVERIFY(!image.isNull());
        QCOMPARE(int(reader.transformation()), int(QImageIOHandler::TransformationRotate90));
        QVERIFY(image.height() > image.width()); // upright: portrait
        std::unique_ptr<OcrEngine> engine = OcrEngine::create(u"pol+eng"_s);
        QVERIFY(engine);
        const OcrResult result = engine->recognize(image, 0);
        QVERIFY(result.ok);
        QVERIFY2(foldedWords(result).contains(u"notatki"_s), qPrintable(foldedWords(result)));
    }

    void scannedPdfPage()
    {
        if (!needsOcr())
            QSKIP("Tesseract with Polish and English models is not available here");
        if (!pdfAvailable())
            QSKIP("no PDF renderer in this build");
        const QString file = dataDir() + u"/skan.pdf"_s;
        PdfInfo info;
        std::unique_ptr<PdfReader> reader = PdfReader::open(file, &info);
        QVERIFY(reader);
        // A scan: no text of its own.
        QVERIFY(reader->pageText(1).trimmed().isEmpty());
        const QSizeF points = reader->pageSize(1);
        const QSize pixels = pdfPagePixels(points, int(std::max(points.width(), points.height()) * 300 / 72));
        const QImage page = reader->render(1, pixels);
        QVERIFY(!page.isNull());
        std::unique_ptr<OcrEngine> engine = OcrEngine::create(u"pol+eng"_s);
        QVERIFY(engine);
        QElapsedTimer timer;
        timer.start();
        const OcrResult result = engine->recognize(page, 300);
        qInfo("skan.pdf page 1 at 300 dpi (%dx%d): %lld ms", page.width(), page.height(), qint64(timer.elapsed()));
        QVERIFY(result.ok);
        const QString words = foldedWords(result);
        for (const QString &w : {u"calka"_s, u"oznaczona"_s, u"zrodlo"_s})
            QVERIFY2(words.contains(w), qPrintable(words));
    }

    void noText()
    {
        if (!ocrAvailable())
            QSKIP("no OCR here");
        QImage blank(800, 600, QImage::Format_RGB32);
        blank.fill(QColor(200, 220, 240));
        std::unique_ptr<OcrEngine> engine = OcrEngine::create(ocrLanguages().constFirst());
        QVERIFY(engine);
        const OcrResult result = engine->recognize(blank, 0);
        QVERIFY(result.ok);
        QVERIFY(result.words.isEmpty());
        // Null and too large.
        QVERIFY(!engine->recognize(QImage(), 0).ok);
        QVERIFY(!engine->recognize(QImage(MaxOcrSide + 1, 10, QImage::Format_Grayscale8), 0).ok);
    }

    void cancelled()
    {
        if (!needsOcr())
            QSKIP("Tesseract with Polish and English models is not available here");
        const QImage image(dataDir() + u"/tablica.png"_s);
        std::unique_ptr<OcrEngine> engine = OcrEngine::create(u"pol"_s);
        QVERIFY(engine);
        const OcrResult result = engine->recognize(image, 0, [] { return true; });
        QVERIFY(result.cancelled);
        QVERIFY(!result.ok);
        // The engine still works afterwards.
        QVERIFY(engine->recognize(image, 0).ok);
    }

    void turnedOff()
    {
        if (ocrBackend() == u"none"_s)
            QSKIP("no OCR backend in this build");
        // The library is looked for once per process: a child with the switch.
        QProcess child;
        QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
        env.insert(u"BRULION_TESSERACT_LIBRARY"_s, u"none"_s);
        child.setProcessEnvironment(env);
        child.start(QCoreApplication::applicationFilePath(), {u"childTurnedOff"_s});
        QVERIFY(child.waitForFinished(120000));
        QVERIFY2(child.exitCode() == 0, child.readAllStandardOutput().constData());
    }

    void childTurnedOff()
    {
        if (qEnvironmentVariable("BRULION_TESSERACT_LIBRARY") != u"none"_s)
            QSKIP("the child of turnedOff()");
        QVERIFY(!ocrAvailable());
        QCOMPARE(ocrUnavailableReason(), u"noLibrary"_s);
        QVERIFY(ocrLanguages().isEmpty());
        QVERIFY(!OcrEngine::create(u"eng"_s));
    }

    void writeFixtures()
    {
        // tests/data/attachments/make-fixtures.sh: BRULION_OCR_FIXTURES_OUT=<dir> tst_ocr writeFixtures.
        const QString out = qEnvironmentVariable("BRULION_OCR_FIXTURES_OUT");
        if (out.isEmpty())
            QSKIP("BRULION_OCR_FIXTURES_OUT is not set");
        QVERIFY(QDir().mkpath(out));
        // A whiteboard's photo, cropped: two lines of Polish.
        const QImage board =
                textPicture(QSize(1100, 260), {u"Spotkanie zespołu we wtorek"_s, u"Żółw idzie powoli przez łąkę"_s}, 44)
                        .convertToFormat(QImage::Format_Grayscale8);
        QVERIFY(board.save(QDir(out).filePath(u"tablica.png"_s)));
        // A phone's photo of notes, stored sideways (rotated 90° counter-clockwise); make-fixtures.sh
        // adds the EXIF orientation that turns it upright (Qt's writer turns the pixels instead).
        const QImage notes = textPicture(QSize(700, 1000), {u"Notatki z wykładu"_s, u"Twierdzenie Pitagorasa"_s}, 40);
        QImageWriter jpeg(QDir(out).filePath(u"obrocone.jpg"_s), "jpeg");
        jpeg.setQuality(85);
        QVERIFY(jpeg.write(notes.transformed(QTransform().rotate(-90))));
        // A scanned A4 page: a picture of text at 150 dpi in a PDF, no text layer.
        const QImage scan =
                textPicture(QSize(1240, 1754),
                            {u"Całka oznaczona"_s, u"Źródło: notatki z ćwiczeń"_s, u"Pole pod wykresem funkcji"_s}, 48)
                        .convertToFormat(QImage::Format_Grayscale8);
        QPdfWriter writer(QDir(out).filePath(u"skan.pdf"_s));
        writer.setResolution(72);
        writer.setPageSize(QPageSize(QPageSize::A4));
        writer.setPageMargins(QMarginsF(0, 0, 0, 0));
        writer.setCreator(u"Brulion tests"_s);
        QPainter p;
        QVERIFY(p.begin(&writer));
        p.drawImage(QRectF(0, 0, 595.28, 841.89), scan);
        p.end();
    }
};

QTEST_MAIN(TestOcr)
#include "tst_ocr.moc"
