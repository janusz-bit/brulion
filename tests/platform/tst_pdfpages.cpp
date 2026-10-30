// PDF pages as pictures (src/platform/pdfpages.h) with the backend this build has — poppler-qt6 in
// the Nix dev shell, Qt PDF on CI's Linux build and Windows (Android's PdfRenderer: the bridge
// self-test, src/platform/android/selftest): a PDF written here with QPdfWriter (an A4 page with
// Polish text, a landscape A4 page with a red square at a known place) read and drawn; pages out of
// range, broken, empty, missing and renamed files refused without a crash; a Polish file name;
// hand-made pages far larger than A4 (a hostile MediaBox: drawn at the resolution of the size
// asked for, pdfRenderDpi()) and one thinner than a pixel. Skipped where the build has no PDF
// renderer (the browser).
#include "pdfpages.h"

#include <QElapsedTimer>
#include <QFile>
#include <QPageLayout>
#include <QPageSize>
#include <QPainter>
#include <QPdfWriter>
#include <QRandomGenerator>
#include <QTemporaryDir>
#include <QTest>

using namespace Qt::StringLiterals;
using namespace brulion::platform;

namespace {

/// Points of A4 (210 × 297 mm).
constexpr double A4W = 595.28;
constexpr double A4H = 841.89;

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

/// A one-page PDF of `mediaBox` points whose page draws `content` (a content stream), its cross-
/// reference table right — pages no QPdfWriter makes.
QByteArray handMadePdf(const QSizeF &mediaBox, const QByteArray &content)
{
    const QList<QByteArray> objects{
            "<< /Type /Catalog /Pages 2 0 R >>"_ba, "<< /Type /Pages /Kids [3 0 R] /Count 1 >>"_ba,
            "<< /Type /Page /Parent 2 0 R /MediaBox [0 0 "_ba + QByteArray::number(mediaBox.width()) + ' '
                    + QByteArray::number(mediaBox.height()) + "] /Contents 4 0 R /Resources << >> >>",
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

QTEST_MAIN(TestPdfPages)
#include "tst_pdfpages.moc"
