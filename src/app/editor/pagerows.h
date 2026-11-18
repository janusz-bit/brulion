// PageRows — the rows of a page as a view that lays out only what is on screen sees them
// (docs/UI.md "One item per page (Z10 stage C)"): a header row, the model's rows, a footer row,
// their heights and the scroll anchor (ScrollAnchor), kept in step with the model's row signals —
// inserted, removed, moved, reset — as QPersistentModelIndex follows a row. Pure QtCore (any
// QAbstractItemModel; PageItem gives it BlockListModel), no QML.
//
// Rows of the tree: 0 the header (the page title above the blocks), 1 … count the model's rows,
// count + 1 the footer (the room under the last block). The header and the footer are measured
// rows like the others: a view at the top is anchored on the header, so a title that wraps again or
// rows inserted at the top of the page keep the top in view.
//
//   PageRows rows;
//   rows.setEstimator([](int row) { return 26.0; }); // a model row's height before its layout
//   rows.setModel(controller->blocks());
//   rows.view().setViewportHeight(800);
//   rows.setRowHeight(PageRows::treeRow(12), 78);     // model row 12 laid out
#pragma once

#include "scrollanchor.h"

#include <QList>
#include <QObject>
#include <QPointer>

#include <functional>

class QAbstractItemModel;
class QModelIndex;

namespace brulion::editor {

class PageRows : public QObject {
    Q_OBJECT
public:
    explicit PageRows(QObject *parent = nullptr);

    QAbstractItemModel *model() const { return m_model.data(); }
    /// Follows `model` (null: no rows but the header and the footer); every row estimated.
    void setModel(QAbstractItemModel *model);
    /// The height of a model row nobody has laid out yet (estimated); without one, `fallback`.
    void setEstimator(std::function<qreal(int modelRow)> estimate) { m_estimate = std::move(estimate); }
    void setFallbackHeight(qreal px) { m_fallback = px; }
    /// A role whose value false hides a row (BlockListModel's `visible`: folds, a zoom): a hidden
    /// row has height 0 (measured) and is shown again with an estimate (−1: every row is shown).
    /// Set before the model.
    void setShownRole(int role) { m_shownRole = role; }
    bool isShown(int modelRow) const;

    ScrollAnchor &view() { return m_view; }
    const ScrollAnchor &view() const { return m_view; }
    const RowHeightTree &heights() const { return m_view.rows(); }

    /// The model's row count.
    int count() const { return m_view.rows().count() - 2; }
    static int treeRow(int modelRow) { return modelRow + 1; }
    /// The model row of a tree row (−1: the header or the footer).
    int modelRow(int treeRow) const { return treeRow >= 1 && treeRow <= count() ? treeRow - 1 : -1; }
    int footerRow() const { return count() + 1; }

    void setHeaderHeight(qreal px) { m_view.setRowHeight(0, px, true); }
    void setFooterHeight(qreal px) { m_view.setRowHeight(footerRow(), px, true); }
    /// A tree row's height (measured by default; a reflow keeps the anchor offset's fraction).
    void setRowHeight(int treeRow, qreal px, bool measured = true,
                      ScrollAnchor::Resize why = ScrollAnchor::Resize::Content)
    {
        m_view.setRowHeight(treeRow, px, measured, why);
    }

Q_SIGNALS:
    /// Rows were inserted, removed, moved or the model reset (after the tree and the anchor
    /// followed).
    void rowsChanged();
    /// Model rows [first, last] changed these roles (empty: any).
    void dataChanged(int first, int last, const QList<int> &roles);

private:
    QList<qreal> estimates(int first, int last) const;
    void reset();
    /// Model rows [first, last] after a change of the shown role: hidden rows 0, shown again an
    /// estimate.
    void followShown(int first, int last);

    QPointer<QAbstractItemModel> m_model;
    QList<QMetaObject::Connection> m_connections;
    std::function<qreal(int)> m_estimate;
    qreal m_fallback = 24;
    int m_shownRole = -1;
    ScrollAnchor m_view;
};

} // namespace brulion::editor
