// PDF pages as pictures (src/platform/pdfpages.h) with the backend this build has — poppler-qt6 in
// the Nix dev shell, Qt PDF on CI's Linux build and Windows (Android's PdfRenderer: the bridge
// self-test, src/platform/android/selftest): a PDF written here with QPdfWriter (an A4 page with
// Polish text, a landscape A4 page with a red square at a known place) read and drawn; pages out of
// range, broken, empty, missing and renamed files refused without a crash; a Polish file name;
// hand-made pages far larger than A4 (a hostile MediaBox: drawn at the resolution of the size
// asked for, pdfRenderDpi()) and one thinner than a pixel. PdfReader (the PDF viewer's): opened
// once, page sizes, parts of pages equal to the whole page's crop, its limits, text and search
// (Polish, without diacritics), encrypted PDFs (tests/data/pdf: AES-256 and RC4 with a user
// password — locked —, an owner password only — opens), broken files, a rotated page, two
// threads with readers of one file. Skipped where the build has no PDF renderer (the browser).
#include "pdfpages.h"
#include "pdfsearch.h"

#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QPageLayout>
#include <QPageSize>
#include <QPainter>
#include <QPdfWriter>
#include <QRandomGenerator>
#include <QTemporaryDir>
#include <QTest>
#include <QThread>

#include <atomic>

using namespace Qt::StringLiterals;
using namespace brulion::platform;

namespace {

/// Points of A4 (210 × 297 mm).
constexpr double A4W = 595.28;
constexpr double A4H = 841.89;

/// The directory of the checked-in PDFs (tests/data/pdf).
QString dataDir()
{
    return QStringLiteral(BRULION_TEST_PDF_DIR);
}

/// The mean difference of the channels of two pictures of one size (0 … 255).
double meanDifference(const QImage &a, const QImage &b)
{
    if (a.size() != b.size() || a.isNull())
        return 255;
    const QImage x = a.convertToFormat(QImage::Format_RGB32);
    const QImage y = b.convertToFormat(QImage::Format_RGB32);
    double sum = 0;
    for (int row = 0; row < x.height(); ++row) {
        for (int col = 0; col < x.width(); ++col) {
            const QRgb p = x.pixel(col, row);
            const QRgb q = y.pixel(col, row);
            sum += qAbs(qRed(p) - qRed(q)) + qAbs(qGreen(p) - qGreen(q)) + qAbs(qBlue(p) - qBlue(q));
        }
    }
    return sum / (3.0 * x.width() * x.height());
}

/// The 2-page PDF: page 1 A4 portrait with black text, page 2 A4 landscape with a red 20 mm square
/// whose top left lies 40 mm from the page's left and 30 mm from its top.
bool writeFixture(const QString &path)
{
    QPdfWriter writer(path);
    writer.setResolution(72);
    writer.setPageSize(QPageSize(QPageSize::A4));
    writer.setPageMargins(QMarginsF(0, 0, 0, 0));
    writer.setPageOrientation(QPageLayout::Portrait);
    QPainter p;
    if (!p.begin(&writer))
        return false;
    QFont font;
    font.setPointSize(24);
    p.setFont(font);
    p.setPen(Qt::black);
    p.drawText(QPointF(60, 120), u"Zażółć gęślą jaźń"_s);
    writer.setPageOrientation(QPageLayout::Landscape);
    if (!writer.newPage())
        return false;
    const double mm = 72 / 25.4;
    p.fillRect(QRectF(40 * mm, 30 * mm, 20 * mm, 20 * mm), Qt::red);
    p.end();
    return QFile::exists(path);
}

/// The lecture of tests/data/pdf/wyklad.pdf (make-fixtures.sh): page 1 A4 portrait "Wykład 3 —
/// Zażółć gęślą jaźń", page 2 A4 landscape (a slide) with the red square of writeFixture(), page 3
/// A4 portrait "Strona trzecia: żółw".
bool writeLecture(const QString &path)
{
    QPdfWriter writer(path);
    writer.setResolution(72);
    writer.setPageSize(QPageSize(QPageSize::A4));
    writer.setPageMargins(QMarginsF(0, 0, 0, 0));
    writer.setPageOrientation(QPageLayout::Portrait);
    writer.setTitle(u"Wykład 3"_s);
    writer.setCreator(u"Brulion tests"_s);
    QPainter p;
    if (!p.begin(&writer))
        return false;
    QFont font;
    font.setPointSize(24);
    p.setFont(font);
    p.setPen(Qt::black);
    p.drawText(QPointF(60, 120), u"Wykład 3 — Zażółć gęślą jaźń"_s);
    writer.setPageOrientation(QPageLayout::Landscape);
    if (!writer.newPage())
        return false;
    const double mm = 72 / 25.4;
    p.fillRect(QRectF(40 * mm, 30 * mm, 20 * mm, 20 * mm), Qt::red);
    writer.setPageOrientation(QPageLayout::Portrait);
    if (!writer.newPage())
        return false;
    p.drawText(QPointF(60, 120), u"Strona trzecia: żółw"_s);
    p.end();
    return QFile::exists(path);
}

/// A one-page PDF of `mediaBox` points whose page draws `content` (a content stream; Helvetica is
/// font /F1), its cross-reference table right — pages no QPdfWriter makes; `pageEntries` go into
/// the page's dictionary ("/Rotate 90").
QByteArray handMadePdf(const QSizeF &mediaBox, const QByteArray &content, const QByteArray &pageEntries = {})
{
    const QList<QByteArray> objects{
            "<< /Type /Catalog /Pages 2 0 R >>"_ba, "<< /Type /Pages /Kids [3 0 R] /Count 1 >>"_ba,
            "<< /Type /Page /Parent 2 0 R /MediaBox [0 0 "_ba + QByteArray::number(mediaBox.width()) + ' '
                    + QByteArray::number(mediaBox.height()) + "] /Contents 4 0 R /Resources << /Font << /F1 << "
                    + "/Type /Font /Subtype /Type1 /BaseFont /Helvetica >> >> >> " + pageEntries + " >>",
            "<< /Length "_ba + QByteArray::number(content.size()) + " >>\nstream\n" + content + "\nendstream"};
    QByteArray pdf = "%PDF-1.4\n"_ba;
    QList<qsizetype> offsets;
    for (qsizetype i = 0; i < objects.size(); ++i) {
        offsets.append(pdf.size());
        pdf += QByteArray::number(i + 1) + " 0 obj\n" + objects.at(i) + "\nendobj\n";
    }
    const qsizetype xref = pdf.size();
    pdf += "xref\n0 " + QByteArray::number(objects.size() + 1) + "\n0000000000 65535 f \n";
    for (const qsizetype offset : std::as_const(offsets))
        pdf += QByteArray::number(offset).rightJustified(10, '0') + " 00000 n \n";
    pdf += "trailer\n<< /Size " + QByteArray::number(objects.size() + 1) + " /Root 1 0 R >>\nstartxref\n"
           + QByteArray::number(xref) + "\n%%EOF\n";
    return pdf;
}

} // namespace

class TestPdfPages : public QObject {
    Q_OBJECT

private Q_SLOTS:
    void initTestCase();
    void backendName();
    void infoAndSizes();
    void renderSecondPage();
    void renderFirstPageText();
    void outOfRange_data();
    void outOfRange();
    void unreadable_data();
    void unreadable();
    void pagePixels_data();
    void pagePixels();
    void renderDpi_data();
    void renderDpi();
    void hugePage();
    void thinPage();
    void readerOpenAndSizes();
    void renderRegion_data();
    void renderRegion();
    void renderLimits();
    void textAndFind();
    void lectureText_data();
    void lectureText();
    void encrypted_data();
    void encrypted();
    void broken_data();
    void broken();
    void rotatedPage();
    void concurrentReaders();
    void writeFixtures();

private:
    QTemporaryDir m_dir;
    QString m_pdf;
};

void TestPdfPages::initTestCase()
{
    QVERIFY(m_dir.isValid());
    m_pdf = m_dir.filePath(u"Wykład ćwiczenia.pdf"_s);
    QVERIFY(writeFixture(m_pdf));
}

void TestPdfPages::backendName()
{
    QVERIFY(!pdfBackendName().isEmpty());
    qInfo("PDF backend: %s", qPrintable(pdfBackendName()));
    if (!pdfAvailable()) {
        QCOMPARE(pdfInfo(m_pdf).pages, -1);
        QVERIFY(renderPdfPage(m_pdf, 1, 100).isNull());
    }
}

void TestPdfPages::infoAndSizes()
{
    if (!pdfAvailable())
        QSKIP("no PDF renderer in this build");
    const PdfInfo info = pdfInfo(m_pdf);
    QCOMPARE(info.pages, 2);
    QVERIFY(!info.locked);
    QCOMPARE(info.pagePoints.size(), 2);
    QVERIFY(qAbs(info.pagePoints.at(0).width() - A4W) < 2);
    QVERIFY(qAbs(info.pagePoints.at(0).height() - A4H) < 2);
    QVERIFY(qAbs(info.pagePoints.at(1).width() - A4H) < 2);
    QVERIFY(qAbs(info.pagePoints.at(1).height() - A4W) < 2);
    // The count and the first page's size (the page picker opening a long PDF).
    const PdfInfo count = pdfInfo(m_pdf, false);
    QCOMPARE(count.pages, 2);
    QCOMPARE(count.pagePoints.size(), 1);
    QVERIFY(qAbs(count.pagePoints.at(0).width() - A4W) < 2);
    QVERIFY(qAbs(count.pagePoints.at(0).height() - A4H) < 2);
}

void TestPdfPages::renderSecondPage()
{
    if (!pdfAvailable())
        QSKIP("no PDF renderer in this build");
    QString error;
    const QImage page = renderPdfPage(m_pdf, 2, 600, &error);
    QVERIFY2(!page.isNull(), qPrintable(error));
    QCOMPARE(page.width(), 600);
    QVERIFY(qAbs(page.height() - 424) <= 1); // 600 × 210 / 297
    // The square's centre (50 mm, 40 mm of 297 × 210 mm): red; a corner: white.
    const QColor centre = page.pixelColor(int(600 * 50 / 297.0), int(424 * 40 / 210.0));
    QVERIFY2(centre.red() > 200 && centre.green() < 60 && centre.blue() < 60, qPrintable(centre.name()));
    QCOMPARE(page.pixelColor(3, 3), QColor(Qt::white));
    QCOMPARE(page.pixelColor(596, 420), QColor(Qt::white));
}

void TestPdfPages::renderFirstPageText()
{
    if (!pdfAvailable())
        QSKIP("no PDF renderer in this build");
    const QImage page = renderPdfPage(m_pdf, 1, 400);
    QVERIFY(!page.isNull());
    QCOMPARE(page.height(), 400);
    // Black text somewhere along its line; white elsewhere.
    bool ink = false;
    const int y0 = int(400 * 100 / A4H), y1 = int(400 * 125 / A4H);
    for (int y = y0; y <= y1 && !ink; ++y) {
        for (int x = 0; x < page.width() && !ink; ++x)
            ink = qGray(page.pixel(x, y)) < 100;
    }
    QVERIFY(ink);
    QCOMPARE(page.pixelColor(page.width() / 2, 380), QColor(Qt::white));
}

void TestPdfPages::outOfRange_data()
{
    QTest::addColumn<int>("page");
    QTest::addColumn<int>("maxSide");
    QTest::newRow("page 0") << 0 << 100;
    QTest::newRow("page 3 of 2") << 3 << 100;
    QTest::newRow("negative") << -1 << 100;
}

void TestPdfPages::outOfRange()
{
    if (!pdfAvailable())
        QSKIP("no PDF renderer in this build");
    QFETCH(int, page);
    QFETCH(int, maxSide);
    QString error;
    QVERIFY(renderPdfPage(m_pdf, page, maxSide, &error).isNull());
    QVERIFY(!error.isEmpty());
    // A size beyond the limit is clamped, not refused.
    const QImage tiny = renderPdfPage(m_pdf, 1, 0);
    QVERIFY(!tiny.isNull());
    QCOMPARE(qMax(tiny.width(), tiny.height()), 1);
}

void TestPdfPages::unreadable_data()
{
    QTest::addColumn<QString>("name");
    QTest::addColumn<QByteArray>("bytes");
    QByteArray noise(10, Qt::Uninitialized);
    for (char &c : noise)
        c = char(QRandomGenerator::global()->bounded(256));
    QTest::newRow("10 random bytes") << u"szum.pdf"_s << noise;
    QTest::newRow("empty file") << u"pusty.pdf"_s << QByteArray();
    QTest::newRow("text renamed .pdf") << u"notatka.pdf"_s << "Zażółć gęślą jaźń\n"_ba;
    QTest::newRow("a PDF's start only") << u"ucięty.pdf"_s << "%PDF-1.7\n1 0 obj\n<< /Type /Catalog"_ba;
    QTest::newRow("missing file") << u"nie ma.pdf"_s << QByteArray("-");
}

void TestPdfPages::unreadable()
{
    if (!pdfAvailable())
        QSKIP("no PDF renderer in this build");
    QFETCH(QString, name);
    QFETCH(QByteArray, bytes);
    const QString path = m_dir.filePath(name);
    if (bytes != "-") {
        QFile f(path);
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write(bytes);
    }
    const PdfInfo info = pdfInfo(path);
    QCOMPARE(info.pages, -1);
    QVERIFY(!info.error.isEmpty());
    QVERIFY(!info.locked);
    QString error;
    QVERIFY(renderPdfPage(path, 1, 200, &error).isNull());
    QVERIFY(!error.isEmpty());
}

void TestPdfPages::pagePixels_data()
{
    QTest::addColumn<QSizeF>("points");
    QTest::addColumn<int>("maxSide");
    QTest::addColumn<QSize>("expected");
    QTest::newRow("A4 landscape") << QSizeF(A4H, A4W) << 2400 << QSize(2400, 1697);
    QTest::newRow("A4 portrait") << QSizeF(A4W, A4H) << 2400 << QSize(1697, 2400);
    QTest::newRow("A0 poster") << QSizeF(2384, 3370) << 2400 << QSize(1698, 2400);
    QTest::newRow("a thin strip") << QSizeF(1000, 1) << 600 << QSize(600, 1);
    QTest::newRow("clamped") << QSizeF(100, 100) << 100000 << QSize(MaxPdfPageSide, MaxPdfPageSide);
    QTest::newRow("empty") << QSizeF(0, 10) << 600 << QSize();
}

void TestPdfPages::pagePixels()
{
    QFETCH(QSizeF, points);
    QFETCH(int, maxSide);
    QFETCH(QSize, expected);
    QCOMPARE(pdfPagePixels(points, maxSide), expected);
}

void TestPdfPages::renderDpi_data()
{
    QTest::addColumn<QSizeF>("points");
    QTest::addColumn<QSize>("pixels");
    QTest::addColumn<double>("expected");
    QTest::newRow("A4 for a backdrop") << QSizeF(A4W, A4H) << QSize(1697, 2400) << 72.0 * 2400 / A4H;
    QTest::newRow("a slide's thumbnail") << QSizeF(960, 540) << QSize(230, 129) << 72.0 * 230 / 960;
    // A hostile MediaBox: the size asked for, not 18 dpi (5000 × 5000 px for a thumbnail).
    QTest::newRow("20000 pt for 2400 px") << QSizeF(20000, 20000) << QSize(2400, 2400) << 8.64;
    QTest::newRow("100000 pt for a thumbnail") << QSizeF(100000, 100000) << QSize(230, 230) << 0.1656;
    QTest::newRow("a strip: one pixel across") << QSizeF(1000, 1) << QSize(600, 1) << 72.0;
    QTest::newRow("a hostile strip: at most MaxPdfPageSide long")
            << QSizeF(1000000, 0.001) << QSize(2400, 1) << 72.0 * MaxPdfPageSide / 1000000;
    QTest::newRow("empty page") << QSizeF(0, 10) << QSize(10, 10) << 0.0;
    QTest::newRow("no pixels") << QSizeF(A4W, A4H) << QSize() << 0.0;
    QTest::newRow("not a number") << QSizeF(qQNaN(), 10) << QSize(10, 10) << 0.0;
}

void TestPdfPages::renderDpi()
{
    QFETCH(QSizeF, points);
    QFETCH(QSize, pixels);
    QFETCH(double, expected);
    const double dpi = pdfRenderDpi(points, pixels);
    QVERIFY2(qAbs(dpi - expected) < 1e-3 * qMax(1.0, expected), qPrintable(QString::number(dpi)));
    // Never a picture smaller than asked or longer than the limit.
    if (dpi > 0) {
        const double longSide = qMax(points.width(), points.height()) * dpi / 72;
        QVERIFY(longSide <= MaxPdfPageSide + 1e-6);
        if (longSide < MaxPdfPageSide - 1e-6) { // (not capped)
            QVERIFY(points.width() * dpi / 72 >= pixels.width() - 1e-6);
            QVERIFY(points.height() * dpi / 72 >= pixels.height() - 1e-6);
        }
    }
}

void TestPdfPages::hugePage()
{
    if (!pdfAvailable())
        QSKIP("no PDF renderer in this build");
    // 20000 × 20000 pt (7 m a side) with a red square in its middle: a thumbnail and a backdrop
    // drawn at their own size.
    const QString path = m_dir.filePath(u"plakat – ogromny.pdf"_s);
    {
        QFile f(path);
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write(handMadePdf(QSizeF(20000, 20000), "1 0 0 rg 7500 7500 5000 5000 re f"_ba));
    }
    const PdfInfo info = pdfInfo(path, false);
    QCOMPARE(info.pages, 1);
    QCOMPARE(info.pagePoints.value(0), QSizeF(20000, 20000));
    for (const int side : {230, 2400}) {
        QElapsedTimer timer;
        timer.start();
        QString error;
        const QImage page = renderPdfPage(path, 1, side, &error);
        qInfo("%d px of a 20000-pt page: %lld ms", side, qint64(timer.elapsed()));
        QVERIFY2(!page.isNull(), qPrintable(error));
        QCOMPARE(page.size(), QSize(side, side));
        const QColor centre = page.pixelColor(side / 2, side / 2);
        QVERIFY2(centre.red() > 200 && centre.green() < 60 && centre.blue() < 60, qPrintable(centre.name()));
        QCOMPARE(page.pixelColor(2, 2), QColor(Qt::white));
    }
}

void TestPdfPages::thinPage()
{
    if (!pdfAvailable())
        QSKIP("no PDF renderer in this build");
    // 14400 × 4 pt, all red: thinner than a pixel at 600 px — still one pixel, red.
    const QString path = m_dir.filePath(u"pasek.pdf"_s);
    {
        QFile f(path);
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write(handMadePdf(QSizeF(14400, 4), "1 0 0 rg 0 0 14400 4 re f"_ba));
    }
    QString error;
    const QImage page = renderPdfPage(path, 1, 600, &error);
    QVERIFY2(!page.isNull(), qPrintable(error));
    QCOMPARE(page.size(), QSize(600, 1));
    const QColor middle = page.pixelColor(300, 0);
    QVERIFY2(middle.red() > 150 && middle.green() < 120, qPrintable(middle.name()));
}

void TestPdfPages::readerOpenAndSizes()
{
    if (!pdfAvailable())
        QSKIP("no PDF renderer in this build");
    PdfInfo info;
    const std::unique_ptr<PdfReader> reader = PdfReader::open(m_pdf, &info);
    QVERIFY2(reader, qPrintable(info.error));
    QCOMPARE(reader->pageCount(), 2);
    // Opening reads the first page's size only.
    QCOMPARE(info.pages, 2);
    QCOMPARE(info.pagePoints.size(), 1);
    const PdfInfo all = pdfInfo(m_pdf);
    for (int page = 1; page <= 2; ++page) {
        const QSizeF size = reader->pageSize(page);
        QVERIFY(qAbs(size.width() - all.pagePoints.at(page - 1).width()) < 0.5);
        QVERIFY(qAbs(size.height() - all.pagePoints.at(page - 1).height()) < 0.5);
    }
    QVERIFY(qAbs(reader->pageSize(2).width() - A4H) < 2);
    QVERIFY(!reader->pageSize(0).isValid());
    QVERIFY(!reader->pageSize(3).isValid());
    QVERIFY(reader->render(3, QSize(100, 100)).isNull());
    QVERIFY(reader->render(0, QSize(100, 100)).isNull());
}

void TestPdfPages::renderRegion_data()
{
    QTest::addColumn<double>("scale"); // `full` = the page's points × scale
    QTest::addColumn<QRectF>("clip");  // in fractions of `full`
    QTest::newRow("around the square, 1×") << 1.0 << QRectF(0.10, 0.12, 0.15, 0.20);
    QTest::newRow("around the square, 3×") << 3.0 << QRectF(0.10, 0.12, 0.15, 0.20);
    QTest::newRow("beside it") << 2.0 << QRectF(0.6, 0.6, 0.2, 0.2);
    QTest::newRow("top left corner") << 2.0 << QRectF(0, 0, 0.05, 0.05);
    QTest::newRow("bottom right corner") << 2.0 << QRectF(0.95, 0.95, 0.05, 0.05);
    QTest::newRow("1 × 1") << 1.0 << QRectF(0.5, 0.5, 0, 0);
    QTest::newRow("37 × 511, 3×") << 3.0 << QRectF(0.3, 0.2, 0, 0);
}

void TestPdfPages::renderRegion()
{
    if (!pdfAvailable())
        QSKIP("no PDF renderer in this build");
    QFETCH(double, scale);
    QFETCH(QRectF, clip);
    const std::unique_ptr<PdfReader> reader = PdfReader::open(m_pdf);
    QVERIFY(reader);
    const QSizeF points = reader->pageSize(2);
    const QSize full(qRound(points.width() * scale), qRound(points.height() * scale));
    QRect part(qRound(clip.x() * full.width()), qRound(clip.y() * full.height()),
               qMax(1, qRound(clip.width() * full.width())), qMax(1, qRound(clip.height() * full.height())));
    if (QTest::currentDataTag() == QByteArray("37 × 511, 3×"))
        part.setSize(QSize(37, 511));
    part &= QRect(QPoint(0, 0), full); // (rounding: render() clips to the page too)
    const QImage whole = reader->render(2, full);
    QVERIFY(!whole.isNull());
    QCOMPARE(whole.size(), full);
    const QImage region = reader->render(2, full, part);
    QVERIFY(!region.isNull());
    QCOMPARE(region.size(), part.size());
    // The part equals the whole page's crop (anti-aliasing and the backends' rounding aside).
    const double difference = meanDifference(region, whole.copy(part));
    QVERIFY2(difference <= 6, qPrintable(QString::number(difference)));
    // The square's centre (50 mm, 40 mm of 297 × 210 mm) is red where the part holds it.
    const QPoint centre(qRound(full.width() * 50 / 297.0), qRound(full.height() * 40 / 210.0));
    if (part.adjusted(2, 2, -2, -2).contains(centre)) {
        const QColor c = region.pixelColor(centre - part.topLeft());
        QVERIFY2(c.red() > 200 && c.green() < 60 && c.blue() < 60, qPrintable(c.name()));
    } else if (!part.intersects(QRect(qRound(full.width() * 40 / 297.0), qRound(full.height() * 30 / 210.0),
                                      qRound(full.width() * 20 / 297.0) + 2, qRound(full.height() * 20 / 210.0) + 2))) {
        QCOMPARE(region.pixelColor(region.width() / 2, region.height() / 2), QColor(Qt::white));
    }
}

void TestPdfPages::renderLimits()
{
    if (!pdfAvailable())
        QSKIP("no PDF renderer in this build");
    const std::unique_ptr<PdfReader> reader = PdfReader::open(m_pdf);
    QVERIFY(reader);
    // A page drawn longer than MaxPdfFullSide: nothing, not even a part of it.
    QVERIFY(reader->render(1, QSize(MaxPdfFullSide * 7 / 10, MaxPdfFullSide + 1), QRect(0, 0, 10, 10)).isNull());
    // A part of more than MaxPdfClipPixels; the whole page at the largest side.
    const QSize huge(MaxPdfFullSide, MaxPdfFullSide * 707 / 1000);
    QVERIFY(reader->render(2, huge, QRect(0, 0, 5000, 4000)).isNull());
    QVERIFY(reader->render(2, huge).isNull());
    // …but a screenful of it is drawn.
    const QImage part = reader->render(2, huge, QRect(1000, 1000, 1200, 800));
    QVERIFY(!part.isNull());
    QCOMPARE(part.size(), QSize(1200, 800));
    // Outside the page: nothing; partly outside: the part on the page.
    QVERIFY(reader->render(2, QSize(842, 595), QRect(900, 700, 50, 50)).isNull());
    const QImage edge = reader->render(2, QSize(842, 595), QRect(832, 0, 100, 100));
    QVERIFY(!edge.isNull());
    QCOMPARE(edge.size(), QSize(10, 100));
    QVERIFY(reader->render(2, QSize()).isNull());
    QVERIFY(reader->render(2, QSize(-5, 10)).isNull());
}

void TestPdfPages::textAndFind()
{
    if (!pdfAvailable())
        QSKIP("no PDF renderer in this build");
    const std::unique_ptr<PdfReader> reader = PdfReader::open(m_pdf);
    QVERIFY(reader);
    const QString text = reader->pageText(1);
    qInfo("page 1 text: %s", qPrintable(text.simplified()));
    if (!pdfTextAvailable()) {
        QVERIFY(text.isEmpty());
        QVERIFY(reader->find(1, u"gesla"_s).isEmpty());
        return;
    }
    QVERIFY2(!findPdfText(text, u"Zażółć gęślą jaźń"_s, 1).isEmpty(), qPrintable(text));
    const QList<PdfTextHit> hits = reader->find(1, u"GESLA"_s);
    QCOMPARE(hits.size(), 1);
    QCOMPARE(text.mid(hits.first().start, hits.first().length), u"gęślą"_s);
    QVERIFY(!hits.first().rects.isEmpty());
    // Drawn at baseline 120 pt, 24 pt high, from x 60: the word lies within that line.
    for (const QRectF &r : hits.first().rects) {
        qInfo("hit rectangle: %g %g %g %g", r.x(), r.y(), r.width(), r.height());
        QVERIFY(r.top() >= 120 - 24 - 12 && r.bottom() <= 120 + 12);
        QVERIFY(r.left() >= 60 && r.right() <= 60 + 400);
    }
    // The slide has a square only: no text, no hits.
    QVERIFY(reader->pageText(2).trimmed().isEmpty());
    QVERIFY(reader->find(2, u"gesla"_s).isEmpty());
    // Out of range; an empty query.
    QVERIFY(reader->pageText(0).isEmpty());
    QVERIFY(reader->find(3, u"gesla"_s).isEmpty());
    QVERIFY(reader->find(1, u"  "_s).isEmpty());
    QCOMPARE(reader->find(1, u"a"_s, 2).size(), 2);
}

void TestPdfPages::lectureText_data()
{
    QTest::addColumn<int>("page");
    QTest::addColumn<QString>("query");
    QTest::addColumn<int>("hits");
    QTest::newRow("page 1, without diacritics") << 1 << u"zazolc"_s << 1;
    QTest::newRow("page 3, a turtle") << 3 << u"ŻÓŁW"_s << 1;
    QTest::newRow("page 3, two words") << 3 << u"strona   trzecia"_s << 1;
    QTest::newRow("page 2, a slide") << 2 << u"zolw"_s << 0;
}

void TestPdfPages::lectureText()
{
    if (!pdfAvailable() || !pdfTextAvailable())
        QSKIP("no PDF text in this build");
    QFETCH(int, page);
    QFETCH(QString, query);
    QFETCH(int, hits);
    const std::unique_ptr<PdfReader> reader = PdfReader::open(QDir(dataDir()).filePath(u"wyklad.pdf"_s));
    QVERIFY(reader);
    QCOMPARE(reader->pageCount(), 3);
    QCOMPARE(reader->find(page, query).size(), hits);
}

void TestPdfPages::encrypted_data()
{
    QTest::addColumn<QString>("name");
    QTest::addColumn<bool>("locked");
    QTest::newRow("AES-256, a user password") << u"zaszyfrowany.pdf"_s << true;
    QTest::newRow("RC4 40-bit, a user password") << u"zaszyfrowany-rc4.pdf"_s << true;
    QTest::newRow("an owner password only") << u"tylko-wlasciciel.pdf"_s << false;
}

void TestPdfPages::encrypted()
{
    if (!pdfAvailable())
        QSKIP("no PDF renderer in this build");
    QFETCH(QString, name);
    QFETCH(bool, locked);
    const QString path = QDir(dataDir()).filePath(name);
    QVERIFY2(QFile::exists(path), qPrintable(path));
    PdfInfo info;
    const std::unique_ptr<PdfReader> reader = PdfReader::open(path, &info);
    if (locked) {
        QVERIFY(!reader);
        QVERIFY(info.locked);
        QCOMPARE(info.pages, -1);
        QVERIFY(!info.error.isEmpty());
        QVERIFY(pdfInfo(path).locked);
        return;
    }
    // Permissions do not stop viewing: it opens and draws (ink on page 1).
    QVERIFY2(reader, qPrintable(info.error));
    QCOMPARE(reader->pageCount(), 3);
    const QImage page = reader->render(1, QSize(400, 566));
    QVERIFY(!page.isNull());
    bool ink = false;
    for (int y = 0; y < page.height() && !ink; ++y) {
        for (int x = 0; x < page.width() && !ink; ++x)
            ink = qGray(page.pixel(x, y)) < 100;
    }
    QVERIFY(ink);
}

void TestPdfPages::broken_data()
{
    QTest::addColumn<QString>("name");
    QTest::addColumn<QByteArray>("bytes"); // "-": missing; "dir": a directory
    QFile lecture(QDir(dataDir()).filePath(u"wyklad.pdf"_s));
    QByteArray whole;
    if (lecture.open(QIODevice::ReadOnly))
        whole = lecture.readAll();
    QTest::newRow("first half") << u"połowa.pdf"_s << whole.left(whole.size() / 2);
    QTest::newRow("first 10 bytes") << u"początek.pdf"_s << whole.left(10);
    QTest::newRow("header and garbage") << u"śmieci.pdf"_s << QByteArray("%PDF-1.4\n" + QByteArray(2000, '\x7f'));
    QTest::newRow("HTML renamed") << u"strona.pdf"_s << "<!doctype html><html><body>Zażółć</body></html>"_ba;
    QTest::newRow("0 bytes") << u"zero.pdf"_s << QByteArray();
    QTest::newRow("a directory") << u"katalog.pdf"_s << "dir"_ba;
    QTest::newRow("missing") << u"brak.pdf"_s << "-"_ba;
    QTest::newRow("a Polish folder") << u"Ćwiczenia ąę/żółw.pdf"_s << whole;
}

void TestPdfPages::broken()
{
    if (!pdfAvailable())
        QSKIP("no PDF renderer in this build");
    QFETCH(QString, name);
    QFETCH(QByteArray, bytes);
    const QString path = m_dir.filePath(name);
    QDir().mkpath(QFileInfo(path).absolutePath());
    if (bytes == "dir") {
        QVERIFY(QDir().mkpath(path));
    } else if (bytes != "-") {
        QFile f(path);
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write(bytes);
    }
    PdfInfo info;
    const std::unique_ptr<PdfReader> reader = PdfReader::open(path, &info);
    if (!reader) {
        QVERIFY(!info.error.isEmpty());
        QCOMPARE(info.pages, -1);
        return;
    }
    // A repaired file: pages, each drawn or not, never a crash.
    QVERIFY(reader->pageCount() >= 1);
    for (int page = 1; page <= reader->pageCount(); ++page) {
        const QSizeF size = reader->pageSize(page);
        if (size.isValid() && !size.isEmpty())
            reader->render(page, pdfPagePixels(size, 200));
        reader->pageText(page);
        reader->find(page, u"żółw"_s);
    }
    if (QTest::currentDataTag() == QByteArray("a Polish folder"))
        QCOMPARE(reader->pageCount(), 3);
}

void TestPdfPages::rotatedPage()
{
    if (!pdfAvailable())
        QSKIP("no PDF renderer in this build");
    // A US Letter page turned by /Rotate 90 with "Zolw" on it.
    const QString path = m_dir.filePath(u"obrócony.pdf"_s);
    {
        QFile f(path);
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write(handMadePdf(QSizeF(612, 792), "BT /F1 36 Tf 100 600 Td (Zolw) Tj ET 1 0 0 rg 0 0 50 50 re f"_ba,
                            "/Rotate 90"_ba));
    }
    const std::unique_ptr<PdfReader> reader = PdfReader::open(path);
    QVERIFY(reader);
    const QSizeF size = reader->pageSize(1);
    QVERIFY2(qAbs(size.width() - 792) < 1 && qAbs(size.height() - 612) < 1,
             qPrintable(u"%1 × %2"_s.arg(size.width()).arg(size.height())));
    const QImage page = reader->render(1, QSize(792, 612));
    QVERIFY(!page.isNull());
    QCOMPARE(page.size(), QSize(792, 612));
    // The red square of the page's bottom left corner is at the shown page's top left (turned
    // clockwise).
    const QColor corner = page.pixelColor(25, 25);
    QVERIFY2(corner.red() > 200 && corner.green() < 60, qPrintable(corner.name()));
    if (!pdfTextAvailable())
        return;
    const QList<PdfTextHit> hits = reader->find(1, u"zolw"_s);
    QCOMPARE(hits.size(), 1);
    for (const QRectF &r : hits.first().rects) {
        qInfo("%s: rotated hit %g %g %g %g", qPrintable(pdfBackendName()), r.x(), r.y(), r.width(), r.height());
        QVERIFY(QRectF(0, 0, 792, 612).adjusted(-1, -1, 1, 1).contains(r));
    }
}

void TestPdfPages::concurrentReaders()
{
    if (!pdfAvailable())
        QSKIP("no PDF renderer in this build");
    std::atomic_int drawn{0};
    std::atomic_int failed{0};
    const QString file = m_pdf;
    const auto work = [&drawn, &failed, file] {
        const std::unique_ptr<PdfReader> reader = PdfReader::open(file);
        if (!reader) {
            ++failed;
            return;
        }
        reader->attachToCurrentThread();
        for (int i = 0; i < 20; ++i) {
            const int page = 1 + i % 2;
            const QImage image = reader->render(page, pdfPagePixels(reader->pageSize(page), 200 + i));
            if (image.isNull())
                ++failed;
            else
                ++drawn;
        }
        reader->detachFromThread();
    };
    std::unique_ptr<QThread> a(QThread::create(work));
    std::unique_ptr<QThread> b(QThread::create(work));
    a->start();
    b->start();
    QVERIFY(a->wait(60000));
    QVERIFY(b->wait(60000));
    QCOMPARE(failed.load(), 0);
    QCOMPARE(drawn.load(), 40);
}

void TestPdfPages::writeFixtures()
{
    // tests/data/pdf/make-fixtures.sh: BRULION_PDF_FIXTURES_OUT=<dir> tst_pdfpages writeFixtures.
    const QString out = qEnvironmentVariable("BRULION_PDF_FIXTURES_OUT");
    if (out.isEmpty())
        QSKIP("BRULION_PDF_FIXTURES_OUT is not set");
    QVERIFY(QDir().mkpath(out));
    QVERIFY(writeLecture(QDir(out).filePath(u"wyklad.pdf"_s)));
}

QTEST_MAIN(TestPdfPages)
#include "tst_pdfpages.moc"
