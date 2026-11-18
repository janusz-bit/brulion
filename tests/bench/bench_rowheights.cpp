// One item per page's height sum tree and scroll anchor (src/app/editor/rowheighttree.h,
// scrollanchor.h; docs/UI.md "One item per page (Z10 stage C)"): what a frame asks of them
// (the row at the top edge, the rows on screen, a row measured) and what a model change costs (rows
// inserted, removed, moved: the O(n) rebuild), at 1000, 5000 and 100 000 rows.
#include "rowheighttree.h"
#include "scrollanchor.h"

#include <QRandomGenerator>
#include <QTest>

using namespace brulion::editor;

namespace {

QList<qreal> heights(int n)
{
    QRandomGenerator rng(7);
    QList<qreal> out;
    out.reserve(n);
    for (int k = 0; k < n; ++k)
        out.append(24 + int(rng.bounded(4)) * 24);
    return out;
}

void sizes()
{
    QTest::addColumn<int>("rows");
    for (const int n : {1000, 5000, 100000})
        QTest::addRow("%d rows", n) << n;
}

} // namespace

class BenchRowHeights : public QObject {
    Q_OBJECT

private Q_SLOTS:
    void rowAt_data() { sizes(); }
    /// 1000 lookups of the row at a y and of where a row starts.
    void rowAt()
    {
        QFETCH(int, rows);
        RowHeightTree tree;
        tree.assign(heights(rows), true);
        const qreal total = tree.total();
        int sum = 0;
        QBENCHMARK {
            for (int k = 0; k < 1000; ++k) {
                const int row = tree.rowAt(total * k / 1000);
                sum += row + int(tree.offsetOf(row));
            }
        }
        QVERIFY(sum != 0);
    }

    void setHeight_data() { sizes(); }
    /// 1000 rows measured (an estimate replaced by its layout's height).
    void setHeight()
    {
        QFETCH(int, rows);
        RowHeightTree tree;
        tree.reset(rows, 30);
        int k = 0;
        QBENCHMARK {
            for (int i = 0; i < 1000; ++i, ++k)
                tree.setHeight((k * 7919) % rows, 24 + k % 50, k % 2 == 0);
        }
        QVERIFY(tree.total() > 0);
    }

    void insertRemove_data() { sizes(); }
    /// A row inserted in the middle and removed again (Enter, then Backspace): two rebuilds.
    void insertRemove()
    {
        QFETCH(int, rows);
        RowHeightTree tree;
        tree.assign(heights(rows), true);
        QBENCHMARK {
            tree.insert(rows / 2, 1, 24);
            tree.remove(rows / 2, 1);
        }
        QCOMPARE(tree.count(), rows);
    }

    void moveRows_data() { sizes(); }
    /// Three rows moved down and back (Alt+↓, Alt+↑).
    void moveRows()
    {
        QFETCH(int, rows);
        RowHeightTree tree;
        tree.assign(heights(rows), true);
        QBENCHMARK {
            tree.move(rows / 3, 3, rows / 3 + 10);
            tree.move(rows / 3 + 7, 3, rows / 3);
        }
        QCOMPARE(tree.count(), rows);
    }

    void frame_data() { sizes(); }
    /// A scrolling frame's questions: scrolled by a wheel step, the rows on screen and in the
    /// margins, ten of them measured.
    void frame()
    {
        QFETCH(int, rows);
        ScrollAnchor view;
        view.resetRows(heights(rows), false);
        view.setViewportHeight(800);
        view.scrollTo(view.rows().total() / 2);
        int k = 0;
        QBENCHMARK {
            view.scrollBy(k % 2 ? 57 : -57);
            const auto [first, last] = view.rowsShown(400, 400);
            for (int row = first; row <= last && row < first + 10; ++row)
                view.setRowHeight(row, 24 + (row + k) % 3 * 24);
            ++k;
        }
        QVERIFY(view.anchorRow() >= 0);
    }
};

QTEST_GUILESS_MAIN(BenchRowHeights)
#include "bench_rowheights.moc"
