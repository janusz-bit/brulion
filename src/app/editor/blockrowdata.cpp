#include "blockrowdata.h"

#include "blocklistmodel.h"

namespace brulion::editor {

BlockRowData::BlockRowData(QObject *parent)
    : QObject(parent)
{
}

BlockListModel *BlockRowData::model() const
{
    return m_model.data();
}

void BlockRowData::setModel(BlockListModel *model)
{
    if (m_model == model)
        return;
    disconnect(m_connection);
    m_connection = {};
    m_model = model;
    Q_EMIT modelChanged();
    announceAll();
}

void BlockRowData::setRow(int row)
{
    if (m_row == row)
        return;
    const bool first = m_row < 0;
    m_row = row;
    Q_EMIT rowChanged();
    // The delegate's index moved (rows inserted or removed above): the same block, the same data.
    if (first)
        announceAll();
}

QVariant BlockRowData::value(int role) const
{
    if (!m_model || m_row < 0)
        return {};
    if (!m_connection) {
        auto *self = const_cast<BlockRowData *>(this);
        self->m_connection = connect(m_model.data(), &QAbstractItemModel::dataChanged, self,
                                     [self](const QModelIndex &from, const QModelIndex &to, const QList<int> &roles) {
                                         self->dataChanged(from.row(), to.row(), roles);
                                     });
    }
    return m_model->data(m_model->index(m_row), role);
}

void BlockRowData::dataChanged(int first, int last, const QList<int> &roles)
{
    if (m_row < first || m_row > last)
        return;
    const auto has = [&roles](int role) { return roles.isEmpty() || roles.contains(role); };
    if (has(BlockListModel::HtmlRole))
        Q_EMIT htmlChanged();
    if (has(BlockListModel::PlainTextRole))
        Q_EMIT plainTextChanged();
    if (has(BlockListModel::EditTextRole))
        Q_EMIT editTextChanged();
    if (has(BlockListModel::LangRole))
        Q_EMIT langChanged();
    if (has(BlockListModel::CalloutTypeRole))
        Q_EMIT calloutTypeChanged();
    if (has(BlockListModel::IsSelectedRole))
        Q_EMIT isSelectedChanged();
    if (has(BlockListModel::CalcResultsRole))
        Q_EMIT calcResultsChanged();
    if (has(BlockListModel::TableDataRole))
        Q_EMIT tableDataChanged();
    if (has(BlockListModel::ChartDataRole))
        Q_EMIT chartDataChanged();
    if (has(BlockListModel::QueryRole))
        Q_EMIT queryChanged();
    if (has(BlockListModel::PropertiesRole))
        Q_EMIT propertiesChanged();
    if (has(BlockListModel::PropertiesInfoRole))
        Q_EMIT propertiesInfoChanged();
}

void BlockRowData::announceAll()
{
    dataChanged(m_row, m_row, {});
}

QString BlockRowData::html() const
{
    return value(BlockListModel::HtmlRole).toString();
}

QString BlockRowData::plainText() const
{
    return value(BlockListModel::PlainTextRole).toString();
}

QString BlockRowData::editText() const
{
    return value(BlockListModel::EditTextRole).toString();
}

QString BlockRowData::lang() const
{
    return value(BlockListModel::LangRole).toString();
}

QString BlockRowData::calloutType() const
{
    return value(BlockListModel::CalloutTypeRole).toString();
}

bool BlockRowData::isSelected() const
{
    return value(BlockListModel::IsSelectedRole).toBool();
}

QVariantList BlockRowData::calcResults() const
{
    return value(BlockListModel::CalcResultsRole).toList();
}

QVariantMap BlockRowData::tableData() const
{
    return value(BlockListModel::TableDataRole).toMap();
}

QVariantMap BlockRowData::chartData() const
{
    return value(BlockListModel::ChartDataRole).toMap();
}

QVariantMap BlockRowData::query() const
{
    return value(BlockListModel::QueryRole).toMap();
}

QVariantList BlockRowData::properties() const
{
    return value(BlockListModel::PropertiesRole).toList();
}

QVariantMap BlockRowData::propertiesInfo() const
{
    return value(BlockListModel::PropertiesInfoRole).toMap();
}

} // namespace brulion::editor
