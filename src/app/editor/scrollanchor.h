// ScrollAnchor — a scroll position that is a row and an offset into it, not a pixel offset
// (docs/UI.md "One item per page (Z10 stage C)", "Scroll anchoring"). Pure (QtCore only), no QML.
//
// It owns the rows' heights (RowHeightTree) and is told every change of them in the model's terms
// — rows inserted, removed, moved, a row's new height, a new width — so the anchor follows its
// row the way a QPersistentModelIndex does. The view's y is derived: the anchor row's offset plus
// the anchor offset, clamped to [0, total − viewport]; a height changing above the anchor (an
// estimate replaced by a measurement, a row typed into, a picture's size arriving) changes the
// rows' coordinates, never what is on screen. The rules (anchor row's own resize, its removal, a
// remote edit that removes and inserts it again, a fold hiding it, moves, the ends, "scroll to
// block") are in docs/UI.md; tst_editor_rowheights checks each against a model of the page. The
// top is sticky: a view at y 0 is anchored on row 0 at offset 0, also while row 0 has no height
// (PageRows' header before it is measured), and rows inserted at the top are shown.
//
//   ScrollAnchor view;
//   view.resetRows(5000, 26);
//   view.setViewportHeight(800);
//   view.scrollTo(12000);                         // the user scrolled
//   view.setRowHeight(3, 140);                    // a row above measured: y() moves, the screen not
//   view.positionAtRow(4000, ScrollAnchor::Center);
#pragma once

#include "rowheighttree.h"

#include <QList>
#include <QPair>
#include <QtGlobal>

namespace brulion::editor {

class ScrollAnchor {
public:
    /// Where positionAtRow() puts a row (ListView's PositionMode).
    enum class Position { Beginning, Center, End, Contain };
    /// Why a row's height changed: its content (the anchor offset stays) or a reflow at another
    /// width (the offset keeps its fraction of the row).
    enum class Resize { Content, Reflow };

    const RowHeightTree &rows() const { return m_rows; }

    // --- The rows (the model's changes; the tree changes with them) ---------------------------
    void resetRows(int count, qreal estimate);
    void resetRows(const QList<qreal> &heights, bool measured);
    void insertRows(int first, int count, qreal estimate);
    void insertRows(int first, const QList<qreal> &heights, bool measured);
    void removeRows(int first, int count);
    /// QAbstractItemModel::beginMoveRows()'s counting; false (nothing done) for an invalid move.
    bool moveRows(int first, int count, int destination);
    void setRowHeight(int row, qreal height, bool measured = true, Resize why = Resize::Content);
    /// Row `row` keeps its height as an estimate (RowHeightTree::setEstimated()).
    void setRowEstimated(int row) { m_rows.setEstimated(row); }
    /// A new width: every height is an estimate until its row is laid out again.
    void markAllEstimated() { m_rows.markAllEstimated(); }

    // --- The view ------------------------------------------------------------------------------
    void setViewportHeight(qreal height);
    qreal viewportHeight() const { return m_viewport; }
    /// The viewport's top edge in the rows' coordinates.
    qreal y() const;
    /// The largest y: total − viewport (0 when everything fits).
    qreal maxY() const;
    /// The user scrolled: the anchor is the row at `y` (clamped) and the offset into it.
    void scrollTo(qreal y);
    void scrollBy(qreal dy) { scrollTo(y() + dy); }
    /// "Scroll to block": the anchor is `row` itself, with the offset `mode` needs; measuring the
    /// rows on the way moves nothing on screen. Contain moves only as far as needed (none when
    /// the row is shown whole, its top when it is taller than the viewport).
    void positionAtRow(int row, Position mode);
    /// A removed anchor waiting for an insertion at its place (see removeRows()) is given up: the
    /// row now at that place is the anchor, at offset 0. Called once a change is complete (the
    /// model's batch: PageItem before it lays out).
    void settle();

    /// The anchor: a row (−1 without rows) and the offset from its top to the viewport's top edge
    /// (negative after positionAtRow() put the row lower than the top edge).
    int anchorRow() const { return m_row; }
    qreal anchorOffset() const { return m_offset; }
    /// The anchor's row was removed and an insertion at its place may still adopt it.
    bool pending() const { return m_pending; }

    /// The rows that meet [y() − above, y() + viewport + below): first and last (−1, −1: none).
    QPair<int, int> rowsShown(qreal above = 0, qreal below = 0) const;

private:
    /// The anchor after rows [first, first + count) were inserted into the tree.
    void rowsInserted(int first, int count);
    /// The anchor is the row at `y` (clamped to [0, maxY]) and the offset into it.
    void anchorAt(qreal y);
    /// After a change: a y outside [0, maxY] anchors at the end it passed, a hidden anchor row
    /// gives the anchor to the next shown row (the same y).
    void clamp();

    RowHeightTree m_rows;
    qreal m_viewport = 0;
    int m_row = -1;
    qreal m_offset = 0;
    bool m_pending = false;
    qreal m_pendingOffset = 0; ///< the removed anchor's offset, for the row that takes its place
};

} // namespace brulion::editor
