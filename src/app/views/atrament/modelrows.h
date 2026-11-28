// ModelRows — a list model's rows as a measured list's heights (docs/ATRAMENT.md "The views in
// Atrament (M3)"): a view drawn by Atrament keeps one per list. It follows the model's signals —
// rows inserted, removed or moved (the list's anchor following its row), the model reset, a row's
// data changed (its height an estimate again, measured in the next frame that shows it) — and
// tells `changed` after each (the view draws again; `rowsWent`: rows went, a current row may be
// gone).
#pragma once

#include "ui/measuredlist.h"

#include <QAbstractItemModel>
#include <QMetaObject>
#include <QPointer>

#include <functional>
#include <vector>

namespace brulion::views::atr {

class ModelRows {
public:
    explicit ModelRows(float estimate)
        : m_estimate(estimate)
    {
    }
    ~ModelRows();
    ModelRows(const ModelRows &) = delete;
    ModelRows &operator=(const ModelRows &) = delete;

    void follow(QAbstractItemModel *model, std::function<void(bool rowsWent)> changed);
    atrament::RowHeights &heights() { return m_heights; }
    const atrament::RowHeights &heights() const { return m_heights; }
    atrament::ListHandle &list() { return m_list; }
    // The next frame shows `row` (the least scroll that does); false: no such row.
    bool reveal(int row);
    // Every height an estimate again (another look).
    void remeasure() { m_heights.markAllEstimated(); }

private:
    void shifted(int first, int last, bool in);
    void moved(int first, int last, int destination);
    void reset();
    void dataChanged(int first, int last);

    QPointer<QAbstractItemModel> m_model;
    float m_estimate;
    atrament::RowHeights m_heights;
    atrament::ListHandle m_list;
    std::function<void(bool)> m_changed;
    std::vector<QMetaObject::Connection> m_connections;
};

} // namespace brulion::views::atr
