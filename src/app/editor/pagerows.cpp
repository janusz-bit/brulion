#include "pagerows.h"

#include <QAbstractItemModel>

#include <algorithm>

namespace brulion::editor {

PageRows::PageRows(QObject *parent) : QObject(parent)
{
    reset();
}

void PageRows::setModel(QAbstractItemModel *model)
{
    if (model == m_model)
        return;
    for (const QMetaObject::Connection &c : std::as_const(m_connections))
        disconnect(c);
    m_connections.clear();
    m_model = model;
    if (model) {
        m_connections = {
                connect(model, &QAbstractItemModel::modelReset, this, &PageRows::reset),
                connect(model, &QAbstractItemModel::layoutChanged, this, &PageRows::reset),
                connect(model, &QAbstractItemModel::rowsInserted, this,
                        [this](const QModelIndex &parent, int first, int last) {
                            if (parent.isValid())
                                return;
                            m_view.insertRows(treeRow(first), estimates(first, last), false);
                            if (m_shownRole >= 0)
                                followShown(first, last);
                            Q_EMIT rowsChanged();
                        }),
                connect(model, &QAbstractItemModel::rowsRemoved, this,
                        [this](const QModelIndex &parent, int first, int last) {
                            if (parent.isValid())
                                return;
                            m_view.removeRows(treeRow(first), last - first + 1);
                            Q_EMIT rowsChanged();
                        }),
                connect(model, &QAbstractItemModel::rowsMoved, this,
                        [this](const QModelIndex &parent, int first, int last, const QModelIndex &to, int row) {
                            if (parent.isValid() || to.isValid())
                                return;
                            if (!m_view.moveRows(treeRow(first), last - first + 1, treeRow(row)))
                                reset();
                            Q_EMIT rowsChanged();
                        }),
                connect(model, &QAbstractItemModel::dataChanged, this,
                        [this](const QModelIndex &from, const QModelIndex &to, const QList<int> &roles) {
                            if (from.parent().isValid())
                                return;
                            if (m_shownRole >= 0 && (roles.isEmpty() || roles.contains(m_shownRole)))
                                followShown(from.row(), to.row());
                            Q_EMIT dataChanged(from.row(), to.row(), roles);
                        }),
                connect(model, &QObject::destroyed, this,
                        [this] {
                            m_model.clear();
                            m_connections.clear();
                            reset();
                        }),
        };
    }
    reset();
}

bool PageRows::isShown(int modelRow) const
{
    if (m_shownRole < 0 || !m_model)
        return true;
    return m_model->data(m_model->index(modelRow, 0), m_shownRole).toBool();
}

QList<qreal> PageRows::estimates(int first, int last) const
{
    QList<qreal> out;
    out.reserve(std::max(0, last - first + 1));
    for (int row = first; row <= last; ++row)
        out.append(!isShown(row) ? 0 : m_estimate ? m_estimate(row) : m_fallback);
    return out;
}

void PageRows::followShown(int first, int last)
{
    const RowHeightTree &tree = m_view.rows();
    for (int row = std::max(0, first); row <= last && row < count(); ++row) {
        const bool shown = isShown(row);
        const qreal now = tree.height(treeRow(row));
        if (!shown && (now > 0 || !tree.isMeasured(treeRow(row))))
            m_view.setRowHeight(treeRow(row), 0, true);
        else if (shown && now <= 0)
            m_view.setRowHeight(treeRow(row), m_estimate ? m_estimate(row) : m_fallback, false);
    }
}

void PageRows::reset()
{
    // The header and the footer keep their heights (measured); the model's rows are estimated.
    const qreal header = m_view.rows().count() >= 2 ? m_view.rows().height(0) : 0;
    const qreal footer = m_view.rows().count() >= 2 ? m_view.rows().height(m_view.rows().count() - 1) : 0;
    const int n = m_model ? m_model->rowCount() : 0;
    QList<qreal> heights = estimates(0, n - 1);
    heights.prepend(header);
    heights.append(footer);
    m_view.resetRows(heights, false);
    m_view.setRowHeight(0, header, true);
    m_view.setRowHeight(n + 1, footer, true);
    if (m_shownRole >= 0) {
        for (int row = 0; row < n; ++row) {
            if (heights.at(row + 1) <= 0 && !isShown(row))
                m_view.setRowHeight(treeRow(row), 0, true); // hidden: known
        }
    }
    Q_EMIT rowsChanged();
}

} // namespace brulion::editor
