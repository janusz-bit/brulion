// The foundations of one item per page (docs/UI.md "One item per page (Z10 stage C)"):
// RowHeightTree (heights and their sums: offsets, the row at a y, inserts, removals, moves, exact
// fixed-point sums) against a naive list, ScrollAnchor (the scroll position as a row and an
// offset: what is on screen stays through height changes, edits, removals, a remote edit of the
// anchor's block, folds, moves, the ends and "scroll to block") and PageRows (both kept in step
// with a model's row signals — a list model and the editor's BlockListModel with another device's
// changes merged in).
#include "blocklistmodel.h"
#include "editorhost.h"
#include "pagerows.h"
#include "rowheighttree.h"
#include "scrollanchor.h"

#include <QAbstractListModel>
#include <QRandomGenerator>
#include <QTest>

#include <algorithm>
#include <cmath>
#include <numeric>

using namespace brulion::editor;
using namespace Qt::StringLiterals;

namespace {

/// The tree's arithmetic done the long way.
struct NaiveRows {
    QList<qint64> heights; // fixed point
    QList<bool> measured;

    int count() const { return int(heights.size()); }
    qint64 offsetOf(int row) const
    {
        return std::accumulate(heights.cbegin(), heights.cbegin() + std::clamp(row, 0, count()), qint64(0));
    }
    qint64 total() const { return offsetOf(count()); }
    int rowAt(qreal y) const
    {
        if (total() <= 0)
            return -1;
        const qint64 at =
                std::clamp<qint64>(qint64(std::floor(std::max<qreal>(0, y) * RowHeightTree::Scale)), 0, total() - 1);
        qint64 sum = 0;
        for (int k = 0; k < count(); ++k) {
            if (at < sum + heights.at(k))
                return k;
            sum += heights.at(k);
        }
        return -1;
    }
    void insert(int row, const QList<qreal> &hs, bool m)
    {
        row = std::clamp(row, 0, count());
        for (qsizetype k = 0; k < hs.size(); ++k) {
            heights.insert(row + k, RowHeightTree::toFixed(hs.at(k)));
            measured.insert(row + k, m);
        }
    }
    void remove(int row, int n)
    {
        row = std::clamp(row, 0, count());
        n = std::clamp(n, 0, count() - row);
        heights.remove(row, n);
        measured.remove(row, n);
    }
    void move(int first, int n, int destination)
    {
        const QList<qint64> h = heights.mid(first, n);
        const QList<bool> m = measured.mid(first, n);
        heights.remove(first, n);
        measured.remove(first, n);
        const int at = destination > first ? destination - n : destination;
        for (int k = 0; k < n; ++k) {
            heights.insert(at + k, h.at(k));
            measured.insert(at + k, m.at(k));
        }
    }
};

void compare(const RowHeightTree &tree, const NaiveRows &naive, const char *step)
{
    QVERIFY2(tree.count() == naive.count(), step);
    QVERIFY2(RowHeightTree::toFixed(tree.total()) == naive.total(), step);
    int measured = 0;
    qint64 measuredTotal = 0;
    for (int k = 0; k <= naive.count(); ++k) {
        QVERIFY2(RowHeightTree::toFixed(tree.offsetOf(k)) == naive.offsetOf(k), step);
        if (k < naive.count()) {
            QVERIFY2(RowHeightTree::toFixed(tree.height(k)) == naive.heights.at(k), step);
            QVERIFY2(tree.isMeasured(k) == naive.measured.at(k), step);
            measured += naive.measured.at(k) ? 1 : 0;
            measuredTotal += naive.measured.at(k) ? naive.heights.at(k) : 0;
        }
    }
    QVERIFY2(tree.measuredCount() == measured, step);
    QVERIFY2(RowHeightTree::toFixed(tree.measuredTotal()) == measuredTotal, step);
    // The row at the start, the middle and the end of every row, and past both ends.
    QList<qreal> ys = {-10, 0, tree.total(), tree.total() + 50};
    for (int k = 0; k < naive.count(); ++k) {
        const qreal top = RowHeightTree::fromFixed(naive.offsetOf(k));
        const qreal h = RowHeightTree::fromFixed(naive.heights.at(k));
        ys << top << top + h / 2 << top + h - 1.0 / RowHeightTree::Scale;
    }
    for (const qreal y : std::as_const(ys))
        QVERIFY2(tree.rowAt(y) == naive.rowAt(y), qPrintable(u"%1: rowAt(%2)"_s.arg(QLatin1String(step)).arg(y)));
}

QList<qreal> heightsOf(const QString &list)
{
    QList<qreal> out;
    for (const QString &part : list.split(u',', Qt::SkipEmptyParts))
        out.append(part.trimmed().toDouble());
    return out;
}

/// A list model of uids with the row signals of every kind and a `shown` role (Qt::UserRole).
class UidModel : public QAbstractListModel {
public:
    QList<quint64> uids;
    QSet<quint64> hidden;
    quint64 next = 1;

    int rowCount(const QModelIndex &parent = {}) const override { return parent.isValid() ? 0 : int(uids.size()); }
    QVariant data(const QModelIndex &index, int role) const override
    {
        if (!index.isValid() || index.row() >= uids.size())
            return {};
        if (role == Qt::UserRole)
            return !hidden.contains(uids.at(index.row()));
        return uids.at(index.row());
    }
    void insert(int row, int n)
    {
        beginInsertRows({}, row, row + n - 1);
        for (int k = 0; k < n; ++k)
            uids.insert(row + k, next++);
        endInsertRows();
    }
    void remove(int row, int n)
    {
        beginRemoveRows({}, row, row + n - 1);
        uids.remove(row, n);
        endRemoveRows();
    }
    bool move(int first, int n, int destination)
    {
        if (!beginMoveRows({}, first, first + n - 1, {}, destination))
            return false;
        const QList<quint64> part = uids.mid(first, n);
        uids.remove(first, n);
        const int at = destination > first ? destination - n : destination;
        for (int k = 0; k < n; ++k)
            uids.insert(at + k, part.at(k));
        endMoveRows();
        return true;
    }
    void setHidden(int first, int last, bool hide)
    {
        for (int row = first; row <= last; ++row) {
            if (hide)
                hidden.insert(uids.at(row));
            else
                hidden.remove(uids.at(row));
        }
        Q_EMIT dataChanged(index(first), index(last), {Qt::UserRole});
    }
    void reset(int n)
    {
        beginResetModel();
        uids.clear();
        for (int k = 0; k < n; ++k)
            uids.append(next++);
        endResetModel();
    }
};

/// Where the top of row `row` is on screen (0: the viewport's top edge).
qreal onScreen(const ScrollAnchor &view, int row)
{
    return view.rows().offsetOf(row) - view.y();
}

} // namespace

class TstEditorRowHeights : public QObject {
    Q_OBJECT

private Q_SLOTS:
    // ---- RowHeightTree ------------------------------------------------------------------------
    void treeEmpty()
    {
        RowHeightTree tree;
        QCOMPARE(tree.count(), 0);
        QCOMPARE(tree.total(), 0.0);
        QCOMPARE(tree.rowAt(0), -1);
        QCOMPARE(tree.rowAt(100), -1);
        QCOMPARE(tree.offsetOf(3), 0.0);
        QCOMPARE(tree.height(0), 0.0);
        QCOMPARE(tree.nextEstimated(0), -1);
        tree.remove(0, 5);
        QVERIFY(!tree.move(0, 1, 2));
        tree.setHeight(0, 10);
        QCOMPARE(tree.count(), 0);
    }

    void treeRowAt_data()
    {
        QTest::addColumn<QString>("heights");
        QTest::addColumn<qreal>("y");
        QTest::addColumn<int>("row");
        QTest::newRow("first row's top") << u"10,20,30"_s << 0.0 << 0;
        QTest::newRow("inside the first") << u"10,20,30"_s << 9.5 << 0;
        QTest::newRow("a row's top belongs to it") << u"10,20,30"_s << 10.0 << 1;
        QTest::newRow("last row") << u"10,20,30"_s << 59.0 << 2;
        QTest::newRow("past the end: the last row") << u"10,20,30"_s << 600.0 << 2;
        QTest::newRow("above the top: the first row") << u"10,20,30"_s << -40.0 << 0;
        QTest::newRow("hidden rows are skipped") << u"10,0,0,10"_s << 10.0 << 3;
        QTest::newRow("hidden rows at the start") << u"0,0,5"_s << 0.0 << 2;
        QTest::newRow("hidden rows at the end") << u"5,7,0,0"_s << 100.0 << 1;
        QTest::newRow("every row hidden") << u"0,0,0"_s << 0.0 << -1;
        QTest::newRow("fractions") << u"0.5,0.25,0.25"_s << 0.6 << 1;
        QTest::newRow("NaN: the first row") << u"3,4"_s << qQNaN() << 0;
        QTest::newRow("one row") << u"26"_s << 13.0 << 0;
        QTest::newRow("a power of two of rows") << u"1,1,1,1,1,1,1,1"_s << 7.0 << 7;
        QTest::newRow("negative heights are 0") << u"-5,4"_s << 0.0 << 1;
    }
    void treeRowAt()
    {
        QFETCH(QString, heights);
        QFETCH(qreal, y);
        QFETCH(int, row);
        RowHeightTree tree;
        tree.assign(heightsOf(heights), true);
        QCOMPARE(tree.rowAt(y), row);
    }

    void treeEdits_data()
    {
        QTest::addColumn<QString>("heights");
        QTest::addColumn<QString>("op");
        QTest::addColumn<QString>("expected");
        QTest::newRow("insert at the start") << u"1,2,3"_s << u"insert 0 9"_s << u"9,1,2,3"_s;
        QTest::newRow("insert in the middle") << u"1,2,3"_s << u"insert 2 9"_s << u"1,2,9,3"_s;
        QTest::newRow("insert at the end") << u"1,2,3"_s << u"insert 3 9"_s << u"1,2,3,9"_s;
        QTest::newRow("insert past the end: at the end") << u"1,2"_s << u"insert 9 9"_s << u"1,2,9"_s;
        QTest::newRow("remove the first") << u"1,2,3"_s << u"remove 0 1"_s << u"2,3"_s;
        QTest::newRow("remove a range") << u"1,2,3,4,5"_s << u"remove 1 3"_s << u"1,5"_s;
        QTest::newRow("remove past the end") << u"1,2,3"_s << u"remove 2 10"_s << u"1,2"_s;
        QTest::newRow("move down") << u"1,2,3,4,5"_s << u"move 0 2 4"_s << u"3,4,1,2,5"_s;
        QTest::newRow("move down to the end") << u"1,2,3,4,5"_s << u"move 1 1 5"_s << u"1,3,4,5,2"_s;
        QTest::newRow("move up") << u"1,2,3,4,5"_s << u"move 3 2 0"_s << u"4,5,1,2,3"_s;
        QTest::newRow("move up by one") << u"1,2,3,4,5"_s << u"move 2 1 1"_s << u"1,3,2,4,5"_s;
        QTest::newRow("move onto itself: refused") << u"1,2,3"_s << u"move 0 1 1"_s << u"1,2,3"_s;
        QTest::newRow("move into itself: refused") << u"1,2,3,4"_s << u"move 0 3 2"_s << u"1,2,3,4"_s;
        QTest::newRow("move past the end: refused") << u"1,2,3"_s << u"move 1 3 0"_s << u"1,2,3"_s;
    }
    void treeEdits()
    {
        QFETCH(QString, heights);
        QFETCH(QString, op);
        QFETCH(QString, expected);
        RowHeightTree tree;
        tree.assign(heightsOf(heights), true);
        const QStringList w = op.split(u' ');
        if (w.at(0) == u"insert")
            tree.insert(w.at(1).toInt(), QList<qreal>{w.at(2).toDouble()}, true);
        else if (w.at(0) == u"remove")
            tree.remove(w.at(1).toInt(), w.at(2).toInt());
        else
            tree.move(w.at(1).toInt(), w.at(2).toInt(), w.at(3).toInt());
        QList<qreal> now;
        for (int k = 0; k < tree.count(); ++k)
            now.append(tree.height(k));
        QCOMPARE(now, heightsOf(expected));
        qreal sum = 0;
        for (int k = 0; k < tree.count(); ++k) {
            QCOMPARE(tree.offsetOf(k), sum);
            sum += tree.height(k);
        }
        QCOMPARE(tree.total(), sum);
    }

    void treeMeasured()
    {
        RowHeightTree tree;
        tree.reset(5, 20);
        QCOMPARE(tree.measuredCount(), 0);
        QCOMPARE(tree.nextEstimated(0), 0);
        tree.setHeight(0, 30);
        tree.setHeight(2, 50);
        QCOMPARE(tree.measuredCount(), 2);
        QCOMPARE(tree.measuredTotal(), 80.0);
        QCOMPARE(tree.nextEstimated(0), 1);
        QCOMPARE(tree.nextEstimated(2), 3);
        tree.setHeight(2, 10, false); // estimated again, with a new height
        QCOMPARE(tree.measuredCount(), 1);
        QCOMPARE(tree.measuredTotal(), 30.0);
        tree.setEstimated(0);
        QCOMPARE(tree.measuredCount(), 0);
        QCOMPARE(tree.height(0), 30.0); // kept as the estimate
        tree.setHeight(4, 40);
        tree.markAllEstimated();
        QCOMPARE(tree.measuredCount(), 0);
        QCOMPARE(tree.total(), 30.0 + 20 + 10 + 20 + 40);
        QCOMPARE(tree.nextEstimated(5), -1);
    }

    /// Heights are 1/64 px: the sums are exact after any number of updates (doubles drift).
    void treeFixedPoint()
    {
        QCOMPARE(RowHeightTree::toFixed(0.1), qint64(6));
        QCOMPARE(RowHeightTree::toFixed(26.015625), qint64(26 * 64 + 1));
        RowHeightTree tree;
        tree.reset(1000, 0.1);
        QCOMPARE(tree.total(), 1000 * 6.0 / 64);
        QRandomGenerator rng(7);
        QList<qint64> naive(1000, 6);
        for (int k = 0; k < 100000; ++k) {
            const int row = int(rng.bounded(1000));
            const qreal h = rng.bounded(300.0);
            tree.setHeight(row, h);
            naive[row] = RowHeightTree::toFixed(h);
        }
        const qint64 sum = std::accumulate(naive.cbegin(), naive.cend(), qint64(0));
        QCOMPARE(RowHeightTree::toFixed(tree.total()), sum);
        QCOMPARE(RowHeightTree::toFixed(tree.offsetOf(500)),
                 std::accumulate(naive.cbegin(), naive.cbegin() + 500, qint64(0)));
    }

    void treeRandom_data()
    {
        QTest::addColumn<quint32>("seed");
        for (quint32 seed : {1u, 2u, 3u, 42u, 2026u, 1118u})
            QTest::newRow(qPrintable(u"seed %1"_s.arg(seed))) << seed;
    }
    /// Random edits of every kind, the tree compared with the naive list after each.
    void treeRandom()
    {
        QFETCH(quint32, seed);
        QRandomGenerator rng(seed);
        RowHeightTree tree;
        NaiveRows naive;
        const auto randomHeights = [&rng](int n) {
            QList<qreal> out;
            for (int k = 0; k < n; ++k)
                out.append(rng.bounded(5) == 0 ? 0 : rng.bounded(400.0)); // some hidden rows
            return out;
        };
        for (int step = 0; step < 600; ++step) {
            const int n = naive.count();
            const int kind = int(rng.bounded(10));
            if (kind <= 2 && n > 0) {
                const int row = int(rng.bounded(n));
                const qreal h = rng.bounded(3) == 0 ? 0 : rng.bounded(500.0);
                const bool m = rng.bounded(2) == 0;
                tree.setHeight(row, h, m);
                naive.heights[row] = RowHeightTree::toFixed(h);
                naive.measured[row] = m;
            } else if (kind == 3 && n > 0) {
                const int row = int(rng.bounded(n));
                tree.setEstimated(row);
                naive.measured[row] = false;
            } else if (kind <= 5 && n < 250) {
                const int row = int(rng.bounded(n + 1));
                const QList<qreal> hs = randomHeights(1 + int(rng.bounded(12)));
                const bool m = rng.bounded(2) == 0;
                if (m || rng.bounded(2) == 0) {
                    tree.insert(row, hs, m);
                    naive.insert(row, hs, m);
                } else {
                    tree.insert(row, int(hs.size()), hs.first());
                    naive.insert(row, QList<qreal>(hs.size(), hs.first()), false);
                }
            } else if (kind == 6 && n > 0) {
                const int row = int(rng.bounded(n));
                const int count = 1 + int(rng.bounded(std::min(n - row, 10)));
                tree.remove(row, count);
                naive.remove(row, count);
            } else if (kind == 7 && n > 1) {
                const int first = int(rng.bounded(n));
                const int count = 1 + int(rng.bounded(std::min(n - first, 6)));
                int destination = int(rng.bounded(n + 1));
                const bool valid = !(destination >= first && destination <= first + count);
                QCOMPARE(tree.move(first, count, destination), valid);
                if (valid)
                    naive.move(first, count, destination);
            } else if (kind == 8) {
                if (rng.bounded(4) == 0) {
                    const int count = int(rng.bounded(200));
                    const qreal h = rng.bounded(60.0);
                    tree.reset(count, h);
                    naive.heights = QList<qint64>(count, RowHeightTree::toFixed(h));
                    naive.measured = QList<bool>(count, false);
                } else {
                    tree.markAllEstimated();
                    // (Not QList::fill(): GCC 13 -O3 with _FORTIFY_SOURCE=3 sees an unbounded memset in it.)
                    naive.measured = QList<bool>(naive.measured.size(), false);
                }
            } else if (kind == 9 && rng.bounded(8) == 0) {
                const QList<qreal> hs = randomHeights(int(rng.bounded(200)));
                tree.assign(hs, true);
                naive.heights.clear();
                naive.measured.clear();
                naive.insert(0, hs, true);
            }
            compare(tree, naive, qPrintable(u"seed %1 step %2"_s.arg(seed).arg(step)));
            if (QTest::currentTestFailed())
                return;
        }
    }

    // ---- ScrollAnchor -------------------------------------------------------------------------
    void anchorScroll()
    {
        ScrollAnchor view;
        QCOMPARE(view.anchorRow(), -1);
        QCOMPARE(view.y(), 0.0);
        view.resetRows(100, 50); // 5000 px
        view.setViewportHeight(400);
        QCOMPARE(view.anchorRow(), 0);
        QCOMPARE(view.maxY(), 4600.0);
        view.scrollTo(1234);
        QCOMPARE(view.y(), 1234.0);
        QCOMPARE(view.anchorRow(), 24);
        QCOMPARE(view.anchorOffset(), 34.0);
        QCOMPARE(view.rowsShown(), qMakePair(24, 32)); // 1234 … 1633
        QCOMPARE(view.rowsShown(100, 100), qMakePair(22, 34));
        view.scrollBy(-2000); // past the top
        QCOMPARE(view.y(), 0.0);
        QCOMPARE(view.anchorRow(), 0);
        view.scrollTo(1e9); // past the end
        QCOMPARE(view.y(), 4600.0);
        QCOMPARE(view.rowsShown().second, 99);
        view.scrollTo(qQNaN());
        QCOMPARE(view.y(), 0.0);
    }

    /// Heights changing above and below the anchor (estimates replaced by measurements, a row
    /// typed into, a picture's size arriving): nothing on screen moves.
    void anchorHeightsElsewhere()
    {
        ScrollAnchor view;
        view.resetRows(200, 30);
        view.setViewportHeight(600);
        view.scrollTo(3015); // row 100, 15 px in
        QCOMPARE(view.anchorRow(), 100);
        const qreal before = onScreen(view, 100);
        QCOMPARE(before, -15.0);
        for (int row = 60; row < 100; ++row)
            view.setRowHeight(row, 30 + row % 7 * 11); // measured, taller
        QCOMPARE(onScreen(view, 100), before);
        QCOMPARE(view.y(), view.rows().offsetOf(100) + 15);
        view.setRowHeight(3, 0);     // hidden far above
        view.setRowHeight(101, 500); // below: grows downwards
        view.setRowHeight(140, 1);
        QCOMPARE(onScreen(view, 100), before);
        QCOMPARE(onScreen(view, 101), before + 30); // under the anchor's row, where it was
        view.insertRows(10, 5, 80);                 // inserted above
        QCOMPARE(view.anchorRow(), 105);
        QCOMPARE(onScreen(view, 105), before);
        view.removeRows(0, 20); // removed above
        QCOMPARE(view.anchorRow(), 85);
        QCOMPARE(onScreen(view, 85), before);
        view.insertRows(86, 3, 40); // inserted right below
        QCOMPARE(onScreen(view, 85), before);
        view.setViewportHeight(300); // the keyboard: the top stays
        QCOMPARE(onScreen(view, 85), before);
    }

    void anchorOwnResize_data()
    {
        QTest::addColumn<qreal>("offset");
        QTest::addColumn<qreal>("height");
        QTest::addColumn<bool>("reflow");
        QTest::addColumn<qreal>("expected");
        QTest::newRow("grows: the top stays") << 20.0 << 300.0 << false << 20.0;
        QTest::newRow("shrinks, the offset still inside") << 20.0 << 50.0 << false << 20.0;
        QTest::newRow("shrinks under the offset: its end") << 80.0 << 50.0 << false << 50.0;
        QTest::newRow("reflow: the same fraction") << 50.0 << 200.0 << true << 100.0;
        QTest::newRow("reflow narrower") << 75.0 << 50.0 << true << 37.5;
        QTest::newRow("at its top") << 0.0 << 10.0 << true << 0.0;
    }
    void anchorOwnResize()
    {
        QFETCH(qreal, offset);
        QFETCH(qreal, height);
        QFETCH(bool, reflow);
        QFETCH(qreal, expected);
        ScrollAnchor view;
        view.resetRows(50, 100);
        view.setViewportHeight(300);
        view.scrollTo(2000 + offset);
        QCOMPARE(view.anchorRow(), 20);
        view.setRowHeight(20, height, true, reflow ? ScrollAnchor::Resize::Reflow : ScrollAnchor::Resize::Content);
        QCOMPARE(view.anchorRow(), 20);
        QCOMPARE(view.anchorOffset(), expected);
        QCOMPARE(view.y(), 2000 + expected);
    }

    /// The anchor's row removed and inserted again at its place in one change (BlockListModel's
    /// replaceContent(): another device edited that block): the new row is the anchor, at the old
    /// offset — also with removals and insertions above it in between, in replaceContent()'s order.
    void anchorRemovedAndInsertedAgain()
    {
        ScrollAnchor view;
        view.resetRows(100, 40);
        view.setViewportHeight(400);
        view.scrollTo(50 * 40 + 12);
        QCOMPARE(view.anchorRow(), 50);
        // Removed from the end backwards: a row below, the anchor, a row above.
        view.removeRows(70, 1);
        view.removeRows(50, 1);
        QVERIFY(view.pending());
        view.removeRows(10, 2);
        QVERIFY(view.pending());
        QCOMPARE(view.anchorRow(), 48);
        // Inserted top-down: above, at the place, below.
        view.insertRows(10, 1, 40);
        QVERIFY(view.pending());
        view.insertRows(49, QList<qreal>{40}, false);
        QVERIFY(!view.pending());
        QCOMPARE(view.anchorRow(), 49);
        QCOMPARE(view.anchorOffset(), 12.0);
        view.insertRows(69, 1, 40);
        QCOMPARE(view.anchorRow(), 49);
        // One row fewer above: the content moved up by one row, the screen did not.
        QCOMPARE(view.y(), 49 * 40 + 12.0);
    }

    /// The anchor's row removed for good: the row that comes up to its place is the anchor, at its
    /// top, once the change is complete (settle(), or the next change that is not at its place).
    void anchorRemoved_data()
    {
        QTest::addColumn<QString>("then");
        QTest::newRow("settled") << u"settle"_s;
        QTest::newRow("an insertion below") << u"insert below"_s;
        QTest::newRow("a scroll") << u"scroll"_s;
        QTest::newRow("a move") << u"move"_s;
    }
    void anchorRemoved()
    {
        QFETCH(QString, then);
        ScrollAnchor view;
        view.resetRows(100, 40);
        view.setViewportHeight(400);
        view.scrollTo(30 * 40 + 25);
        view.removeRows(29, 3); // the anchor (30) and its neighbours
        QVERIFY(view.pending());
        QCOMPARE(view.y(), 29 * 40.0); // the next row's top at the top edge
        if (then == u"settle")
            view.settle();
        else if (then == u"insert below")
            view.insertRows(60, 2, 40);
        else if (then == u"scroll")
            view.scrollBy(0);
        else
            QVERIFY(view.moveRows(80, 2, 90));
        QVERIFY(!view.pending());
        QCOMPARE(view.anchorRow(), 29);
        QCOMPARE(view.anchorOffset(), 0.0);
        QCOMPARE(view.y(), 29 * 40.0);
    }

    void anchorRemovedAtTheEnd()
    {
        ScrollAnchor view;
        view.resetRows(20, 50);
        view.setViewportHeight(200);
        view.scrollTo(800); // the end
        QCOMPARE(view.anchorRow(), 16);
        view.removeRows(15, 5);
        view.settle();
        QCOMPARE(view.y(), view.maxY());
        QCOMPARE(view.y(), 550.0);
        view.removeRows(0, 15);
        view.settle();
        QCOMPARE(view.anchorRow(), -1);
        QCOMPARE(view.y(), 0.0);
        view.insertRows(0, 3, 10); // a page again: from its top
        QCOMPARE(view.anchorRow(), 0);
        QCOMPARE(view.y(), 0.0);
    }

    /// A fold hides the anchor's row: the next shown row is the anchor at the same y; opening the
    /// fold again grows rows above it, which keeps the screen as it is.
    void anchorFolded()
    {
        ScrollAnchor view;
        view.resetRows(100, 40);
        view.setViewportHeight(400);
        view.scrollTo(40 * 40 + 10);
        QCOMPARE(view.anchorRow(), 40);
        // Rows 38..45 folded under row 37.
        for (int row = 38; row <= 45; ++row)
            view.setRowHeight(row, 0);
        QCOMPARE(view.anchorRow(), 46);
        QCOMPARE(view.anchorOffset(), 0.0);
        QCOMPARE(view.y(), 38 * 40.0);
        const qreal before = onScreen(view, 46);
        for (int row = 38; row <= 45; ++row)
            view.setRowHeight(row, 40, false);
        QCOMPARE(onScreen(view, 46), before);
        QCOMPARE(view.anchorRow(), 46);
    }

    void anchorMoves()
    {
        ScrollAnchor view;
        view.resetRows(100, 40);
        view.setViewportHeight(400);
        view.scrollTo(50 * 40 + 5);
        // Other rows moved across the anchor: the screen stays.
        QVERIFY(view.moveRows(10, 3, 80));
        QCOMPARE(view.anchorRow(), 47);
        QCOMPARE(view.y(), 47 * 40 + 5.0);
        QVERIFY(view.moveRows(90, 2, 0));
        QCOMPARE(view.anchorRow(), 49);
        QVERIFY(view.moveRows(60, 1, 55)); // below the anchor: nothing
        QCOMPARE(view.anchorRow(), 49);
        // The anchor's own row moved: the view goes with it.
        QVERIFY(view.moveRows(48, 3, 70));
        QCOMPARE(view.anchorRow(), 68);
        QCOMPARE(view.anchorOffset(), 5.0);
        QVERIFY(!view.moveRows(68, 1, 69)); // onto itself: refused
        QCOMPARE(view.anchorRow(), 68);
    }

    /// The content ends: rows under the view shrinking pull the view up to the new end, and the
    /// view does not jump back when they grow again; a page shorter than the view stays at 0.
    void anchorEnds()
    {
        ScrollAnchor view;
        view.resetRows(30, 100);
        view.setViewportHeight(500);
        view.scrollTo(2500); // the end
        QCOMPARE(view.y(), 2500.0);
        for (int row = 26; row < 30; ++row)
            view.setRowHeight(row, 20);
        QCOMPARE(view.y(), view.maxY());
        QCOMPARE(view.y(), 26 * 100 + 4 * 20 - 500.0);
        const int anchor = view.anchorRow();
        for (int row = 26; row < 30; ++row)
            view.setRowHeight(row, 100);
        QCOMPARE(view.anchorRow(), anchor);
        QCOMPARE(view.y(), 2180.0);
        view.setViewportHeight(5000);
        QCOMPARE(view.y(), 0.0);
        view.setViewportHeight(500);
        QCOMPARE(view.y(), 0.0);
    }

    void anchorPositionAtRow_data()
    {
        QTest::addColumn<int>("mode");
        QTest::addColumn<qreal>("from");
        QTest::addColumn<int>("row");
        QTest::addColumn<qreal>("screen"); // where the row's top is on screen afterwards
        using P = ScrollAnchor::Position;
        QTest::newRow("beginning") << int(P::Beginning) << 0.0 << 40 << 0.0;
        QTest::newRow("center") << int(P::Center) << 0.0 << 40 << 180.0;
        QTest::newRow("end") << int(P::End) << 0.0 << 40 << 360.0;
        QTest::newRow("contain, below") << int(P::Contain) << 0.0 << 40 << 360.0;
        QTest::newRow("contain, above") << int(P::Contain) << 3000.0 << 40 << 0.0;
        QTest::newRow("contain, shown whole: no move") << int(P::Contain) << 1500.0 << 40 << 100.0;
        QTest::newRow("contain, half shown") << int(P::Contain) << 1630.0 << 40 << 0.0;
        QTest::newRow("center near the top: the top") << int(P::Center) << 2000.0 << 1 << 40.0;
        QTest::newRow("center near the end: the end") << int(P::Center) << 0.0 << 99 << 360.0;
    }
    /// "Scroll to block": the row is where it was asked to be, and stays there while the rows on
    /// the way are measured (the anchor is the row itself).
    void anchorPositionAtRow()
    {
        QFETCH(int, mode);
        QFETCH(qreal, from);
        QFETCH(int, row);
        QFETCH(qreal, screen);
        ScrollAnchor view;
        view.resetRows(100, 40); // estimated
        view.setViewportHeight(400);
        view.scrollTo(from);
        view.positionAtRow(row, ScrollAnchor::Position(mode));
        QCOMPARE(onScreen(view, row), screen);
        if (view.anchorRow() != row || view.y() <= 0 || view.y() >= view.maxY())
            return; // an end, or no move (Contain): the anchor is not the row
        // The rows around it measured, taller and shorter: it does not move.
        for (int k = std::max(0, row - 15); k < std::min(100, row + 15); ++k) {
            if (k != row)
                view.setRowHeight(k, 10 + (k * 37) % 90);
        }
        QCOMPARE(onScreen(view, row), screen);
    }

    void anchorPositionHiddenRow()
    {
        ScrollAnchor view;
        view.resetRows(50, 40);
        view.setViewportHeight(200);
        view.setRowHeight(20, 0);
        view.positionAtRow(20, ScrollAnchor::Position::Beginning);
        QCOMPARE(view.anchorRow(), 21);
        QCOMPARE(view.y(), 20 * 40.0);
        view.positionAtRow(-1, ScrollAnchor::Position::Center); // nothing
        view.positionAtRow(50, ScrollAnchor::Position::Center);
        QCOMPARE(view.anchorRow(), 21);
    }

    void anchorRandom_data()
    {
        QTest::addColumn<quint32>("seed");
        for (quint32 seed : {5u, 11u, 77u, 1000u, 31337u})
            QTest::newRow(qPrintable(u"seed %1"_s.arg(seed))) << seed;
    }
    /// Random changes that do not touch the anchor's row, the anchor followed by its uid in a list
    /// beside it: its row and where it is on screen stay, unless the view reached an end.
    void anchorRandom()
    {
        QFETCH(quint32, seed);
        QRandomGenerator rng(seed);
        ScrollAnchor view;
        QList<quint64> uids;
        quint64 next = 1;
        for (int k = 0; k < 300; ++k)
            uids.append(next++);
        view.resetRows(300, 40);
        view.setViewportHeight(500);
        view.scrollTo(150 * 40 + 7);
        for (int step = 0; step < 800; ++step) {
            const int n = int(uids.size());
            const int anchor = view.anchorRow();
            QVERIFY(anchor >= 0 && anchor < n);
            const quint64 uid = uids.at(anchor);
            const qreal screen = onScreen(view, anchor);
            const qreal offset = view.anchorOffset();
            const int kind = int(rng.bounded(6));
            if (kind <= 1) {
                int row = int(rng.bounded(n));
                if (row == anchor)
                    continue;
                view.setRowHeight(row, rng.bounded(4) == 0 ? 0 : rng.bounded(300.0), rng.bounded(2) == 0);
            } else if (kind == 2 && n < 600) {
                const int row = int(rng.bounded(n + 1));
                const int count = 1 + int(rng.bounded(8));
                for (int k = 0; k < count; ++k)
                    uids.insert(row + k, next++);
                view.insertRows(row, count, rng.bounded(200.0));
            } else if (kind == 3 && n > 50) {
                const int row = int(rng.bounded(n));
                const int count = 1 + int(rng.bounded(std::min(n - row, 8)));
                if (anchor >= row && anchor < row + count)
                    continue;
                uids.remove(row, count);
                view.removeRows(row, count);
            } else if (kind == 4) {
                const int first = int(rng.bounded(n));
                const int count = 1 + int(rng.bounded(std::min(n - first, 5)));
                const int destination = int(rng.bounded(n + 1));
                if ((destination >= first && destination <= first + count)
                    || (anchor >= first && anchor < first + count))
                    continue;
                const QList<quint64> part = uids.mid(first, count);
                uids.remove(first, count);
                const int at = destination > first ? destination - count : destination;
                for (int k = 0; k < count; ++k)
                    uids.insert(at + k, part.at(k));
                QVERIFY(view.moveRows(first, count, destination));
            } else {
                view.scrollBy(rng.bounded(2000.0) - 1000);
                if (view.y() <= 0 || view.y() >= view.maxY())
                    view.scrollTo(view.rows().total() / 2);
                continue;
            }
            const int now = view.anchorRow();
            const QString where = u"seed %1 step %2"_s.arg(seed).arg(step);
            if (view.y() <= 0 || view.y() >= view.maxY() || view.rows().height(uids.indexOf(uid)) <= 0) {
                // An end clamped the view, or the anchor's row is hidden (a row of height 0 from the
                // start of the run): the anchor moved on; start again from the middle.
                view.scrollTo(view.rows().total() / 2);
                continue;
            }
            QVERIFY2(now >= 0 && uids.at(now) == uid, qPrintable(where));
            QVERIFY2(qFuzzyCompare(1 + onScreen(view, now), 1 + screen), qPrintable(where));
            QVERIFY2(view.anchorOffset() == offset, qPrintable(where));
        }
    }

    // ---- PageRows -----------------------------------------------------------------------------
    void pageRowsFollowsAModel()
    {
        UidModel model;
        model.reset(50);
        PageRows rows;
        rows.setShownRole(Qt::UserRole);
        rows.setEstimator([](int) { return 30.0; });
        rows.setModel(&model);
        rows.setHeaderHeight(120);
        rows.setFooterHeight(64);
        QCOMPARE(rows.count(), 50);
        QCOMPARE(rows.heights().count(), 52);
        QCOMPARE(rows.heights().total(), 120 + 50 * 30 + 64.0);
        QCOMPARE(rows.modelRow(0), -1);
        QCOMPARE(rows.modelRow(1), 0);
        QCOMPARE(rows.modelRow(51), -1);
        QCOMPARE(rows.footerRow(), 51);
        rows.view().setViewportHeight(300);
        // At the top the anchor is the header: rows inserted at the page's top stay in view.
        model.insert(0, 2);
        QCOMPARE(rows.view().anchorRow(), 0);
        QCOMPARE(rows.view().y(), 0.0);
        rows.view().scrollTo(120 + 20 * 30 + 4); // model row 20
        const quint64 uid = model.uids.at(20);
        QCOMPARE(rows.modelRow(rows.view().anchorRow()), 20);
        model.insert(5, 3);
        model.remove(0, 1);
        QVERIFY(model.move(40, 2, 1));
        const int at = int(model.uids.indexOf(uid));
        QCOMPARE(rows.modelRow(rows.view().anchorRow()), at);
        QCOMPARE(rows.view().anchorOffset(), 4.0);
        // Folded: the rows above it hidden (0 px) and shown again (estimated).
        model.setHidden(at - 5, at - 1, true);
        for (int k = at - 5; k < at; ++k)
            QCOMPARE(rows.heights().height(PageRows::treeRow(k)), 0.0);
        QCOMPARE(rows.modelRow(rows.view().anchorRow()), at);
        const qreal screen = onScreen(rows.view(), rows.view().anchorRow());
        model.setHidden(at - 5, at - 1, false);
        QCOMPARE(rows.heights().height(PageRows::treeRow(at - 5)), 30.0);
        QVERIFY(!rows.heights().isMeasured(PageRows::treeRow(at - 5)));
        QCOMPARE(onScreen(rows.view(), rows.view().anchorRow()), screen);
        // Hidden rows inserted are known (measured 0).
        model.hidden.insert(model.next);
        model.insert(0, 1);
        QCOMPARE(rows.heights().height(1), 0.0);
        QVERIFY(rows.heights().isMeasured(1));
        // A reset: back to the top; the header and the footer keep their heights.
        model.reset(10);
        QCOMPARE(rows.view().anchorRow(), 0);
        QCOMPARE(rows.heights().total(), 120 + 10 * 30 + 64.0);
        rows.setModel(nullptr);
        QCOMPARE(rows.count(), 0);
        QCOMPARE(rows.heights().total(), 184.0);
    }

    /// The editor's model: another device's change merged in (replaceContent) keeps the anchor on
    /// its block when that block is unchanged, and on the block's new version, at the same offset,
    /// when it is what changed.
    void pageRowsRemoteChange()
    {
        BlockListModel model;
        QStringList lines;
        for (int k = 0; k < 120; ++k)
            lines << u"- Zażółć gęślą jaźń, punkt %1"_s.arg(k);
        model.resetContent(lines.join(u'\n') + u'\n');
        QCOMPARE(model.count(), 120);
        PageRows rows;
        rows.setShownRole(BlockListModel::VisibleRole);
        rows.setEstimator([](int) { return 26.0; });
        rows.setModel(&model);
        rows.setHeaderHeight(100);
        rows.view().setViewportHeight(400);
        rows.view().scrollTo(100 + 60 * 26 + 9);
        QCOMPARE(rows.modelRow(rows.view().anchorRow()), 60);
        const quint64 uid = model.uidAt(60);

        // Blocks above and below changed: the anchor's block is kept, so is its place on screen.
        QStringList other = lines;
        other[10] = u"- Pchnąć w tę łódź jeża"_s;
        other.insert(20, u"- nowy punkt ćma"_s);
        other.removeAt(90);
        const qreal screen = onScreen(rows.view(), rows.view().anchorRow());
        model.replaceContent(other.join(u'\n') + u'\n');
        rows.view().settle();
        QCOMPARE(model.uidAt(rows.modelRow(rows.view().anchorRow())), uid);
        QCOMPARE(onScreen(rows.view(), rows.view().anchorRow()), screen);

        // The anchor's own block changed: its new version is the anchor, at the same offset.
        const int row = rows.modelRow(rows.view().anchorRow());
        QStringList edited = other;
        edited[row] = u"- Zażółć gęślą jaźń, punkt 60 — zmieniony na telefonie"_s;
        edited[5] = u"- też zmieniony ąę"_s;
        model.replaceContent(edited.join(u'\n') + u'\n');
        rows.view().settle();
        QCOMPARE(rows.modelRow(rows.view().anchorRow()), row);
        QVERIFY(model.uidAt(row) != uid);
        QCOMPARE(rows.view().anchorOffset(), 9.0);
        QCOMPARE(onScreen(rows.view(), rows.view().anchorRow()), screen);

        // Removed: the block after it comes up to the top edge.
        QStringList removed = edited;
        removed.removeAt(row);
        const quint64 after = model.uidAt(row + 1);
        model.replaceContent(removed.join(u'\n') + u'\n');
        rows.view().settle();
        QCOMPARE(model.uidAt(rows.modelRow(rows.view().anchorRow())), after);
        QCOMPARE(rows.view().anchorOffset(), 0.0);
    }

    /// Folding through the model (`visible` false for the children): the anchor inside the fold
    /// moves to the next shown row at the same y.
    void pageRowsFold()
    {
        BlockListModel model;
        QStringList lines;
        for (int k = 0; k < 40; ++k) {
            lines << u"- rodzic %1"_s.arg(k);
            for (int c = 0; c < 3; ++c)
                lines << u"  - dziecko %1.%2 źdźbło"_s.arg(k).arg(c);
        }
        model.resetContent(lines.join(u'\n') + u'\n');
        QCOMPARE(model.count(), 160);
        PageRows rows;
        rows.setShownRole(BlockListModel::VisibleRole);
        rows.setEstimator([](int) { return 26.0; });
        rows.setModel(&model);
        rows.view().setViewportHeight(300);
        rows.view().scrollTo(82 * 26.0 + 3); // a child of parent 20 (row 80); no header
        QCOMPARE(rows.modelRow(rows.view().anchorRow()), 82);
        const qreal y = rows.heights().offsetOf(PageRows::treeRow(81));
        model.setCollapsed(80, true);
        QCOMPARE(rows.modelRow(rows.view().anchorRow()), 84); // the next parent
        QCOMPARE(rows.view().anchorOffset(), 0.0);
        QCOMPARE(rows.view().y(), y);
        model.setCollapsed(80, false);
        QCOMPARE(rows.modelRow(rows.view().anchorRow()), 84);
        QCOMPARE(rows.view().y(), y + 3 * 26);
    }
};

QTEST_MAIN(TstEditorRowHeights)
#include "tst_editor_rowheights.moc"
