// RowHeightTree — the heights of a page's rows and their sums (docs/UI.md "One item per page
// (Z10 stage C)"): which row is at a y, where a row starts, the page's height, for a view that
// lays out only the rows on screen. Pure (QtCore only), no QML.
//
// One entry per row; a hidden row (a fold, a zoom) has height 0. Each entry is *measured* (its
// height comes from a layout at the current width) or *estimated* (not laid out yet, or laid out
// at another width: the last height is the estimate). Heights are kept in 1/64 px (Qt's 26.6
// fixed point), so sums are exact and no sequence of updates drifts. A Fenwick tree gives a
// row's offset and the row at an offset in O(log n), a height set in O(log n); inserting,
// removing and moving ranges and setting every height rebuild it in O(n) (a memmove and a linear
// rebuild: tests/bench/bench_rowheights.cpp).
//
//   RowHeightTree rows;
//   rows.reset(5000, 26);                 // 5000 rows estimated at 26 px
//   rows.setHeight(12, 78);               // row 12 laid out: 78 px, measured
//   const int top = rows.rowAt(contentY); // the row at the viewport's top edge
//   const qreal y = rows.offsetOf(top);   // where it starts
#pragma once

#include <QList>
#include <QtGlobal>

namespace brulion::editor {

class RowHeightTree {
public:
    /// Fixed-point units per pixel.
    static constexpr int Scale = 64;

    int count() const { return int(m_heights.size()); }
    bool isEmpty() const { return m_heights.isEmpty(); }
    /// Row `row`'s height in px (0 outside [0, count)).
    qreal height(int row) const;
    bool isMeasured(int row) const;
    /// The sum of every row's height.
    qreal total() const { return fromFixed(m_total); }
    /// Where row `row` starts: the sum of the heights of the rows before it. `row` is clamped to
    /// [0, count]; offsetOf(count()) is total().
    qreal offsetOf(int row) const;
    /// The row whose span [offset, offset + height) holds `y` — never a row of height 0 (the rows of
    /// height 0 at an offset come before the row that starts there). A `y` above the first row is
    /// the first row with a height, one at or past the end the last one; −1 when no row has a
    /// height (none, or all hidden).
    int rowAt(qreal y) const;
    /// The measured rows: how many, their height (the estimate of the rest can be their mean).
    int measuredCount() const { return m_measuredCount; }
    qreal measuredTotal() const { return fromFixed(m_measuredTotal); }
    /// The first estimated row at or after `from` (−1: none).
    int nextEstimated(int from) const;

    /// Row `row`'s height (negative or NaN: 0), measured or estimated.
    void setHeight(int row, qreal height, bool measured = true);
    /// Row `row` keeps its height as an estimate (its text changed, its layout is gone).
    void setEstimated(int row);
    /// `n` rows of height `estimate` (estimated) before row `row` (clamped to [0, count]).
    void insert(int row, int n, qreal estimate);
    /// Rows of these heights before row `row`.
    void insert(int row, const QList<qreal> &heights, bool measured);
    /// Removes rows [row, row + n) (clamped to the rows there are).
    void remove(int row, int n);
    /// Moves rows [first, first + n) before row `destination` as QAbstractItemModel::beginMoveRows()
    /// counts it (a row index before the move, not in [first, first + n]); false (nothing done) for
    /// an invalid move.
    bool move(int first, int n, int destination);
    /// `n` rows of height `estimate` (estimated), nothing else.
    void reset(int n, qreal estimate);
    /// These rows, nothing else.
    void assign(const QList<qreal> &heights, bool measured);
    /// Every row keeps its height as an estimate (a new width, font or theme).
    void markAllEstimated();

    static qint64 toFixed(qreal px);
    static qreal fromFixed(qint64 units) { return qreal(units) / Scale; }

private:
    /// The Fenwick tree from m_heights, and the sums.
    void rebuild();
    /// The sum of the first `n` rows' heights (fixed point).
    qint64 prefix(int n) const;
    void add(int row, qint64 delta);

    QList<qint64> m_heights;  ///< per row, fixed point
    QList<quint8> m_measured; ///< per row: 1 measured, 0 estimated
    QList<qint64> m_tree;     ///< Fenwick tree, 1-based (m_tree[0] unused)
    qint64 m_total = 0;
    qint64 m_measuredTotal = 0;
    int m_measuredCount = 0;
    int m_topBit = 0; ///< the largest power of two ≤ count (rowAt()'s descent)
};

} // namespace brulion::editor
