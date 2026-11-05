#include "blockrowdata.h"

#include <QJSValue>
#include <QMetaMethod>

namespace brulion::editor {

BlockRowData::BlockRowData(QObject *parent) : QObject(parent) { }

QObject *BlockRowData::source() const
{
    if (!m_sourceLooked && parent()) {
        const QVariant model = parent()->property("model");
        m_source = model.metaType() == QMetaType::fromType<QJSValue>() ? model.value<QJSValue>().toQObject()
                                                                       : model.value<QObject *>();
        m_sourceLooked = !m_source.isNull();
    }
    return m_source.data();
}

QVariant BlockRowData::value(Role role, const char *name, void (BlockRowData::*changed)()) const
{
    QObject *object = source();
    if (!object)
        return {};
    const QMetaObject *meta = object->metaObject();
    const int index = meta->indexOfProperty(name);
    if (index < 0)
        return {};
    const QMetaProperty property = meta->property(index);
    if (!(m_connected & (1 << role))) {
        m_connected |= 1 << role;
        if (property.hasNotifySignal()) {
            auto *self = const_cast<BlockRowData *>(this);
            connect(object, property.notifySignal(), self, QMetaMethod::fromSignal(changed));
        }
    }
    return property.read(object);
}

QString BlockRowData::html() const
{
    return value(Html, "html", &BlockRowData::htmlChanged).toString();
}

QString BlockRowData::editText() const
{
    return value(EditText, "editText", &BlockRowData::editTextChanged).toString();
}

QString BlockRowData::lang() const
{
    return value(Lang, "lang", &BlockRowData::langChanged).toString();
}

QString BlockRowData::calloutType() const
{
    return value(CalloutType, "calloutType", &BlockRowData::calloutTypeChanged).toString();
}

bool BlockRowData::isSelected() const
{
    return value(IsSelected, "isSelected", &BlockRowData::isSelectedChanged).toBool();
}

QVariantList BlockRowData::calcResults() const
{
    return value(CalcResults, "calcResults", &BlockRowData::calcResultsChanged).toList();
}

QVariantMap BlockRowData::tableData() const
{
    return value(TableData, "tableData", &BlockRowData::tableDataChanged).toMap();
}

QVariantMap BlockRowData::chartData() const
{
    return value(ChartData, "chartData", &BlockRowData::chartDataChanged).toMap();
}

QVariantMap BlockRowData::query() const
{
    return value(Query, "query", &BlockRowData::queryChanged).toMap();
}

QVariantList BlockRowData::properties() const
{
    return value(Properties, "properties", &BlockRowData::propertiesChanged).toList();
}

QVariantMap BlockRowData::propertiesInfo() const
{
    return value(PropertiesInfo, "propertiesInfo", &BlockRowData::propertiesInfoChanged).toMap();
}

} // namespace brulion::editor
