// The PDF viewer's page layout (src/app/pdf/pdflayout.h): exact rectangles and the content's size,
// mixed page sizes, fit width with its cap (centred), zoom past the viewport, hostile sizes (zero,
// negative, NaN, a hostile aspect), the page at a height (gaps, before, after), ranges, anchors that
// keep a point in place across zooms, a page's size arriving later, 20 000 pages fast; two pages side
// by side (pairs, the cover alone, an odd last page, mixed sizes fitted by the widest row, the page
// at a point, ranges by rows, anchors, back to one page per row).
#include "pdflayout.h"

#include <QElapsedTimer>
#include <QRandomGenerator>
#include <QTest>

#include <cmath>

using namespace brulion::pdfview;

namespace {
constexpr qreal A4W = 595.28;
constexpr qreal A4H = 841.89;
const QSizeF A4(A4W, A4H);
const QSizeF A4Landscape(A4H, A4W);

bool near(qreal a, qreal b, qreal eps = 0.01)
{
    return std::abs(a - b) <= eps;
}
} // namespace

class TestPdfLayout : public QObject {
    Q_OBJECT

private Q_SLOTS:
    void threePagesFitWidth()
    {
        PdfPageLayout layout;
        layout.setPages({A4, A4, A4});
        layout.setGeometry(800, 24, 12, 1e6);
        layout.setScale(layout.fitWidthScale());
        const qreal k = (800 - 48) / A4W;
        QVERIFY(near(layout.scale(), k, 1e-9));
        const qreal h = A4H * k;
        QCOMPARE(layout.count(), 3);
        QVERIFY(near(layout.rect(0).x(), 24));
        QVERIFY(near(layout.rect(0).y(), 24));
        QVERIFY(near(layout.rect(0).width(), 752));
        QVERIFY(near(layout.rect(1).y(), 24 + h + 12));
        QVERIFY(near(layout.rect(2).y(), 24 + 2 * (h + 12)));
        QVERIFY(near(layout.contentSize().height(), 24 + 3 * h + 2 * 12 + 24));
        QVERIFY(near(layout.contentSize().width(), 800));
        QVERIFY(layout.rect(3).isNull());
        QVERIFY(layout.rect(-1).isNull());
    }

    void mixedSizes()
    {
        PdfPageLayout layout;
        layout.setPages({A4, A4Landscape, A4});
        layout.setGeometry(1000, 20, 10, 1e6);
        layout.setScale(layout.fitWidthScale());
        // The widest (landscape) page fills the width; portrait pages are centred.
        QVERIFY(near(layout.rect(1).width(), 960));
        QVERIFY(near(layout.rect(1).x(), 20));
        QVERIFY(near(layout.rect(0).x(), (1000 - layout.rect(0).width()) / 2));
        QVERIFY(near(layout.rect(0).height() / layout.rect(0).width(), A4H / A4W, 1e-6));
    }

    void fitWidthCapCentres()
    {
        PdfPageLayout layout;
        layout.setPages({A4});
        layout.setGeometry(2400, 24, 12, 1000);
        layout.setScale(layout.fitWidthScale());
        QVERIFY(near(layout.rect(0).width(), 1000));
        QVERIFY(near(layout.rect(0).x(), 700));
    }

    void zoomPastTheViewport()
    {
        PdfPageLayout layout;
        layout.setPages({A4, A4});
        layout.setGeometry(800, 24, 12, 1e6);
        layout.setScale(layout.fitWidthScale() * 3);
        QVERIFY(layout.contentSize().width() > 800);
        QVERIFY(near(layout.rect(0).x(), 24));
        QVERIFY(near(layout.contentSize().width(), layout.rect(0).width() + 48));
        // Clamped to the limits.
        layout.setScale(1000);
        QCOMPARE(layout.scale(), PdfPageLayout::MaxScale);
        layout.setScale(0);
        QCOMPARE(layout.scale(), PdfPageLayout::MinScale);
        layout.setScale(std::nan(""));
        QCOMPARE(layout.scale(), PdfPageLayout::MinScale);
    }

    void hostileSizes_data()
    {
        QTest::addColumn<QSizeF>("size");
        QTest::addColumn<QSizeF>("expected");
        QTest::newRow("zero") << QSizeF(0, 0) << A4Landscape; // takes the first valid size
        QTest::newRow("negative") << QSizeF(-5, 10) << A4Landscape;
        QTest::newRow("not a number") << QSizeF(std::nan(""), 10) << A4Landscape;
        QTest::newRow("infinite") << QSizeF(INFINITY, 10) << A4Landscape;
        QTest::newRow("a hostile strip") << QSizeF(1e6, 0.001) << QSizeF(1e6, 1e6 / 32);
        QTest::newRow("a hostile column") << QSizeF(0.001, 1e6) << QSizeF(1e6 / 32, 1e6);
        QTest::newRow("sane") << QSizeF(200, 300) << QSizeF(200, 300);
    }

    void hostileSizes()
    {
        QFETCH(QSizeF, size);
        QFETCH(QSizeF, expected);
        PdfPageLayout layout;
        layout.setPages({size, A4Landscape});
        QCOMPARE(layout.pagePoints(0), expected);
        // A PDF without a single valid size: A4.
        layout.setPages({QSizeF(), QSizeF(-1, -1)});
        QCOMPARE(layout.pagePoints(0), PdfPageLayout::DefaultPage);
        QCOMPARE(layout.pagePoints(1), PdfPageLayout::DefaultPage);
    }

    void indexAt()
    {
        PdfPageLayout layout;
        layout.setPages({A4, A4, A4});
        layout.setGeometry(800, 24, 12, 1e6);
        layout.setScale(1);
        QCOMPARE(layout.indexAt(-100), 0);
        QCOMPARE(layout.indexAt(0), 0);
        QCOMPARE(layout.indexAt(24 + 10), 0);
        // In the gap after page 1: the next page.
        QCOMPARE(layout.indexAt(24 + A4H + 6), 1);
        QCOMPARE(layout.indexAt(24 + A4H + 12 + 1), 1);
        QCOMPARE(layout.indexAt(1e9), 2);
        PdfPageLayout empty;
        QCOMPARE(empty.indexAt(10), -1);
        QCOMPARE(empty.range(0, 100), std::make_pair(-1, -1));
    }

    void range()
    {
        PdfPageLayout layout;
        layout.setPages({A4, A4, A4, A4});
        layout.setGeometry(800, 24, 12, 1e6);
        layout.setScale(1);
        QCOMPARE(layout.range(0, 10), std::make_pair(-1, -1)); // only the top margin
        QCOMPARE(layout.range(0, 30), std::make_pair(0, 0));
        QCOMPARE(layout.range(30, 24 + A4H + 3), std::make_pair(0, 0)); // ends in the gap
        QCOMPARE(layout.range(30, 24 + A4H + 13), std::make_pair(0, 1));
        QCOMPARE(layout.range(24 + A4H + 2, 24 + A4H + 5), std::make_pair(-1, -1)); // only a gap
        QCOMPARE(layout.range(0, 1e9), std::make_pair(0, 3));
        QCOMPARE(layout.range(100, 50), std::make_pair(-1, -1));
    }

    void anchorsKeepPoints()
    {
        PdfPageLayout layout;
        layout.setPages({A4, A4Landscape, A4, QSizeF(300, 500)});
        layout.setGeometry(900, 16, 12, 1e6);
        layout.setScale(layout.fitWidthScale());
        QRandomGenerator random(7);
        for (int i = 0; i < 10; ++i) {
            const int index = int(random.bounded(4));
            const QRectF r = layout.rect(index);
            const QPointF point(r.x() + random.bounded(r.width()), r.y() + random.bounded(r.height()));
            const PdfPageLayout::Anchor anchor = layout.anchorAt(point);
            QCOMPARE(anchor.index, index);
            for (const qreal scale : {0.5, 1.0, 2.5, 4.0, 8.0}) {
                PdfPageLayout other = layout;
                other.setScale(scale);
                const QPointF back = other.pointOf(anchor);
                const QRectF ro = other.rect(index);
                QVERIFY(ro.adjusted(-0.5, -0.5, 0.5, 0.5).contains(back));
                // Same fractions of the page.
                QVERIFY(near((back.x() - ro.x()) / ro.width(), (point.x() - r.x()) / r.width(), 1e-6));
                layout.setScale(layout.scale()); // (unchanged)
                QVERIFY(near(layout.pointOf(anchor).x(), point.x(), 0.5));
                QVERIFY(near(layout.pointOf(anchor).y(), point.y(), 0.5));
            }
        }
        QCOMPARE(layout.pointOf({}), QPointF());
    }

    void pageSizeLater()
    {
        PdfPageLayout layout;
        layout.setPages({A4, A4, A4});
        layout.setGeometry(800, 24, 12, 1e6);
        layout.setScale(1);
        const QRectF first = layout.rect(0);
        const QRectF third = layout.rect(2);
        layout.setPageSize(1, A4Landscape);
        // (Wider content: the portrait pages are centred anew; their heights and tops stay.)
        QCOMPARE(layout.rect(0).y(), first.y());
        QCOMPARE(layout.rect(0).size(), first.size());
        QVERIFY(near(layout.rect(2).y(), third.y() - (A4H - A4W)));
        // In a batch; unchanged sizes change nothing.
        QVERIFY(!layout.setPageSizes(0, {A4}));
        QVERIFY(layout.setPageSizes(1, {A4, A4Landscape, A4})); // (the last beyond the end: ignored)
        QCOMPARE(layout.pagePoints(2), A4Landscape);
        layout.setPageSize(7, A4); // out of range: nothing
        QCOMPARE(layout.count(), 3);
    }

    void twoPagesSideBySide()
    {
        PdfPageLayout layout;
        layout.setPages({A4, A4, A4, A4, A4});
        layout.setGeometry(1000, 20, 10, 1e6);
        layout.setSpread(true, false);
        layout.setScale(layout.fitWidthScale());
        // The widest row (two A4 and the gap between them) fills the width.
        const qreal k = (1000 - 40 - 10) / (2 * A4W);
        QVERIFY(near(layout.scale(), k, 1e-9));
        const qreal w = A4W * k;
        const qreal h = A4H * k;
        QCOMPARE(layout.rowCount(), 3);
        QVERIFY(near(layout.rect(0).x(), 20));
        QVERIFY(near(layout.rect(1).x(), 20 + w + 10));
        QVERIFY(near(layout.rect(1).y(), layout.rect(0).y()));
        QVERIFY(near(layout.rect(2).y(), 20 + h + 10));
        QVERIFY(near(layout.rect(3).x(), layout.rect(1).x()));
        // The odd last page alone, centred.
        QCOMPARE(layout.rowOf(4), 2);
        QVERIFY(near(layout.rect(4).center().x(), 500));
        QVERIFY(near(layout.rect(4).y(), 20 + 2 * (h + 10)));
        QVERIFY(near(layout.contentSize().height(), 20 + 3 * h + 2 * 10 + 20));
        QVERIFY(near(layout.contentSize().width(), 1000));
        QCOMPARE(layout.firstOfRow(1), 2);
        QCOMPARE(layout.firstOfRow(3), -1);
        QCOMPARE(layout.rowOf(5), -1);
        // A height names the row's first page; a point the page under it.
        QCOMPARE(layout.indexAt(20 + h + 15), 2);
        QCOMPARE(layout.indexAt(QPointF(30, 20 + h + 15)), 2);
        QCOMPARE(layout.indexAt(QPointF(990, 20 + h + 15)), 3);
        QCOMPARE(layout.indexAt(QPointF(500 + 6, 30)), 1); // just past the middle of the gap
        QCOMPARE(layout.indexAt(QPointF(500 - 6, 30)), 0);
        QCOMPARE(layout.indexAt(QPointF(500, 1e9)), 4);
        // Ranges are whole rows.
        QCOMPARE(layout.range(30, 40), std::make_pair(0, 1));
        QCOMPARE(layout.range(30, 20 + h + 20), std::make_pair(0, 3));
        QCOMPARE(layout.range(0, 1e9), std::make_pair(0, 4));
        // Back to one page per row.
        layout.setSpread(false, false);
        QCOMPARE(layout.rowCount(), 5);
        layout.setScale(layout.fitWidthScale());
        QVERIFY(near(layout.rect(1).x(), 20));
        QVERIFY(near(layout.rect(1).width(), 960));
    }

    void coverAlone()
    {
        PdfPageLayout layout;
        layout.setPages({A4, A4, A4, A4});
        layout.setGeometry(1000, 20, 10, 1e6);
        layout.setSpread(true, true);
        layout.setScale(1);
        // 1 | 2 3 | 4
        QCOMPARE(layout.rowCount(), 3);
        QCOMPARE(layout.rowOf(0), 0);
        QCOMPARE(layout.rowOf(1), 1);
        QCOMPARE(layout.rowOf(2), 1);
        QCOMPARE(layout.rowOf(3), 2);
        QVERIFY(near(layout.rect(0).center().x(), layout.contentSize().width() / 2));
        QVERIFY(near(layout.rect(2).x(), layout.rect(1).right() + 10));
        QVERIFY(near(layout.rect(1).y(), 20 + A4H + 10));
        // Content wider than the view: the widest row and the margins.
        QVERIFY(near(layout.contentSize().width(), 2 * A4W + 10 + 40));
        // One page only: alone either way.
        PdfPageLayout one;
        one.setPages({A4});
        one.setSpread(true, false);
        QCOMPARE(one.rowCount(), 1);
        QVERIFY(near(one.rect(0).width(), A4W * PdfPageLayout::PointsToLogical));
    }

    void twoPagesMixedSizes()
    {
        PdfPageLayout layout;
        // A slide next to a portrait page; a wider pair below.
        layout.setPages({A4, A4Landscape, A4Landscape, A4Landscape});
        layout.setGeometry(1200, 16, 12, 1e6);
        layout.setSpread(true, false);
        layout.setScale(layout.fitWidthScale());
        const qreal k = (1200 - 32 - 12) / (2 * A4H);
        QVERIFY(near(layout.scale(), k, 1e-9));
        // Pages at the top of their row; the row as tall as its tallest page.
        QVERIFY(near(layout.rect(0).y(), layout.rect(1).y()));
        QVERIFY(near(layout.rect(2).y(), 16 + A4H * k + 12));
        // The narrower first row centred.
        const qreal firstRow = layout.rect(1).right() - layout.rect(0).left();
        QVERIFY(near(layout.rect(0).x(), (1200 - firstRow) / 2));
        // Anchors keep a point of the right page in place across zooms.
        const QRectF r = layout.rect(3);
        const QPointF point(r.x() + r.width() * 0.25, r.y() + r.height() * 0.75);
        const PdfPageLayout::Anchor anchor = layout.anchorAt(point);
        QCOMPARE(anchor.index, 3);
        layout.setScale(3);
        const QRectF big = layout.rect(3);
        QVERIFY(near(layout.pointOf(anchor).x(), big.x() + big.width() * 0.25, 1e-6));
        QVERIFY(near(layout.pointOf(anchor).y(), big.y() + big.height() * 0.75, 1e-6));
    }

    void manyPagesFast()
    {
        PdfPageLayout layout;
        QList<QSizeF> pages(20000, A4);
        pages[777] = A4Landscape;
        QElapsedTimer timer;
        timer.start();
        layout.setPages(pages);
        layout.setGeometry(1200, 24, 12, 1000);
        layout.setScale(layout.fitWidthScale());
        const qint64 layoutMs = timer.elapsed();
        timer.restart();
        qint64 sum = 0; // (20 000 pages × 10^6 lookups overflow an int)
        const qreal height = layout.contentSize().height();
        for (int i = 0; i < 1'000'000; ++i)
            sum += layout.indexAt(height * (i % 1000) / 1000.0);
        const qint64 lookupMs = timer.elapsed();
        qInfo("20 000 pages: layout %lld ms, 10^6 lookups %lld ms (%d)", layoutMs, lookupMs, int(sum % 7));
        QVERIFY2(layoutMs < 50, qPrintable(QString::number(layoutMs)));
        QVERIFY2(lookupMs < 2000, qPrintable(QString::number(lookupMs)));
        QCOMPARE(layout.indexAt(height - 30), 19999);
    }
};

QTEST_GUILESS_MAIN(TestPdfLayout)
#include "tst_pdflayout.moc"
