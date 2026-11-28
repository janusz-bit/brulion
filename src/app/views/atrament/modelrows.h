// ModelRows — a list model's rows as a measured list's heights (docs/ATRAMENT.md "The views in
// Atrament (M3)"): a view drawn by Atrament keeps one per list. It follows the model's signals —
// rows inserted, removed or moved (the list's anchor following its row), the model reset, a row's
// data changed (its height an estimate again, measured in the next frame that shows it) — and
// tells `changed` after each (the view draws again; `rowsWent`: rows went, a current row may be
// gone).
#pragma once

#include "ui/atrament.h"
#include "ui/measuredlist.h"

#include <QAbstractItemModel>
#include <QMetaObject>
#include <QPointer>

#include <functional>
#include <vector>

namespace brulion::views::atr {

// The first row of `model` whose `role` is `value` (−1: none, no model).
inline int rowWhere(const QAbstractItemModel *model, int role, const QVariant &value)
{
    const int count = model ? model->rowCount() : 0;
    for (int r = 0; r < count; ++r)
        if (model->index(r, 0).data(role) == value)
            return r;
    return -1;
}

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
    // follow() for the view entity `cx` is: each change draws it again, `rowsWent(view)` first when
    // rows went (a current row may be gone).
    template<class View, class F>
    void followFor(QAbstractItemModel *model, atrament::Context<View> &cx, F rowsWent)
    {
        atrament::App *app = &cx.app();
        const atrament::EntityId id = cx.entityId();
        follow(model, [app, id, rowsWent](bool went) {
            app->updateEntity<View>(id, [&](View &v, atrament::Context<View> &c) {
                if (went)
                    rowsWent(v);
                c.notify();
            });
        });
    }
    atrament::RowHeights &heights() { return m_heights; }
    const atrament::RowHeights &heights() const { return m_heights; }
    atrament::ListHandle &list() { return m_list; }
    // The next frame shows `row` (the least scroll that does); false: no such row.
    bool reveal(int row);
    // Every height an estimate again (another look).
    void remeasure() { m_heights.markAllEstimated(); }
    // A view's new look: its rows measured again, the view drawn again.
    template<class Look, class Cx>
    void restyle(Look &field, Look look, Cx &cx)
    {
        field = std::move(look);
        remeasure();
        cx.notify();
    }

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
