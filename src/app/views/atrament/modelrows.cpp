#include "modelrows.h"

namespace brulion::views::atr {

ModelRows::~ModelRows()
{
    for (const QMetaObject::Connection &c : m_connections)
        QObject::disconnect(c);
}

void ModelRows::follow(QAbstractItemModel *model, std::function<void(bool)> changed)
{
    m_model = model;
    m_changed = std::move(changed);
    m_heights.reset(model ? std::size_t(model->rowCount()) : 0, m_estimate);
    if (!model)
        return;
    using M = QAbstractItemModel;
    m_connections.push_back(QObject::connect(model, &M::rowsInserted, model,
                                             [this](const QModelIndex &, int first, int last) { shifted(first, last, true); }));
    m_connections.push_back(QObject::connect(model, &M::rowsRemoved, model,
                                             [this](const QModelIndex &, int first, int last) { shifted(first, last, false); }));
    m_connections.push_back(QObject::connect(model, &M::rowsMoved, model,
                                             [this](const QModelIndex &, int first, int last, const QModelIndex &, int to) {
                                                 moved(first, last, to);
                                             }));
    m_connections.push_back(QObject::connect(model, &M::modelReset, model, [this] { reset(); }));
    m_connections.push_back(QObject::connect(model, &M::dataChanged, model,
                                             [this](const QModelIndex &from, const QModelIndex &to) {
                                                 dataChanged(from.row(), to.row());
                                             }));
}

// Rows [first, last] came (`in`) or went: the heights and the anchor follow.
void ModelRows::shifted(int first, int last, bool in)
{
    const auto n = std::size_t(last - first + 1);
    if (in) {
        m_heights.insert(std::size_t(first), n, m_estimate);
        m_list.rowsInserted(std::size_t(first), n);
    } else {
        m_heights.remove(std::size_t(first), n);
        m_list.rowsRemoved(std::size_t(first), n);
    }
    m_changed(!in);
}

// Rows moved: they keep the heights measured for them.
void ModelRows::moved(int first, int last, int destination)
{
    const auto n = std::size_t(last - first + 1);
    const auto to = std::size_t(destination > first ? destination - int(n) : destination);
    std::vector<std::pair<float, bool>> kept;
    for (std::size_t r = std::size_t(first); r < std::size_t(first) + n; ++r)
        kept.emplace_back(m_heights.heightOf(r), m_heights.isMeasured(r));
    m_heights.remove(std::size_t(first), n);
    m_list.rowsRemoved(std::size_t(first), n);
    m_heights.insert(to, n, m_estimate);
    m_list.rowsInserted(to, n);
    for (std::size_t k = 0; k < n; ++k)
        m_heights.setHeight(to + k, kept[k].first, kept[k].second);
    m_changed(true);
}

void ModelRows::reset()
{
    m_heights.reset(std::size_t(m_model->rowCount()), m_estimate); // (the model told it: it is there)
    m_changed(true);
}

void ModelRows::dataChanged(int first, int last)
{
    for (int r = first; r <= last; ++r) // (the rows the model has: their heights follow them)
        m_heights.setEstimated(std::size_t(r));
    m_changed(false);
}

bool ModelRows::reveal(int row)
{
    if (row < 0 || std::size_t(row) >= m_heights.count())
        return false;
    m_list.scrollToRow(std::size_t(row), atrament::ListPosition::Nearest);
    return true;
}

} // namespace brulion::views::atr
