#include "hostedrow.h"

#include "blocklistmodel.h"
#include "editorhost.h"

#include <QQmlComponent>
#include <QQmlContext>
#include <QQmlEngine>
#include <QQmlPropertyMap>
#include <QQuickItem>

namespace brulion::editor::hostedrow {

using namespace Qt::StringLiterals;

namespace {

/// The roles BlockRowData reads from a delegate's model object (blockrowdata.h).
const QList<QByteArray> &rowDataRoles()
{
    static const QList<QByteArray> roles = {"html",        "renderedText", "editText",    "lang",
                                            "calloutType", "isSelected",   "calcResults", "tableData",
                                            "chartData",   "query",        "properties",  "propertiesInfo"};
    return roles;
}

/// The delegate's own property named `role` (−1: none) — declared by the delegate, not one of
/// QQuickItem's (the role `visible` is BlockDelegate's `shown`, not Item.visible).
int delegateProperty(const QMetaObject *meta, const QByteArray &role)
{
    const int p = meta->indexOfProperty(role.constData());
    return p >= QQuickItem::staticMetaObject.propertyCount() && meta->property(p).isWritable() ? p : -1;
}

} // namespace

QQuickItem *create(QQmlComponent *delegate, QQuickItem *parent, BlockListModel *model, int row,
                   QQmlPropertyMap **rolesOut)
{
    if (!delegate || !delegate->isReady() || !model)
        return nullptr;
    QQmlContext *context = delegate->creationContext() ? delegate->creationContext() : qmlContext(parent);
    if (!context && delegate->engine())
        context = delegate->engine()->rootContext();
    if (!context)
        return nullptr;
    QObject *object = delegate->beginCreate(context);
    auto *item = qobject_cast<QQuickItem *>(object);
    if (!item) {
        delete object;
        return nullptr;
    }
    // The model object BlockRowData reads, and every required property the delegate names after a
    // role (as QQmlDelegateModel gives a ListView's delegate).
    auto *roles = QQmlPropertyMap::create(item);
    const QModelIndex index = model->index(row);
    const QHash<int, QByteArray> names = model->roleNames();
    QVariantMap initial;
    const QMetaObject *meta = item->metaObject();
    for (auto it = names.cbegin(); it != names.cend(); ++it) {
        const QString name = QString::fromLatin1(it.value());
        if (rowDataRoles().contains(it.value()))
            roles->insert(name, model->data(index, it.key()));
        if (delegateProperty(meta, it.value()) >= 0)
            initial.insert(name, model->data(index, it.key()));
    }
    initial.insert(u"index"_s, row);
    initial.insert(u"model"_s, QVariant::fromValue<QObject *>(roles));
    delegate->setInitialProperties(item, initial);
    item->setParentItem(parent);
    delegate->completeCreate();
    if (rolesOut)
        *rolesOut = roles;
    return item;
}

void update(QQuickItem *item, QQmlPropertyMap *roles, BlockListModel *model, int row, const QList<int> &changed)
{
    if (!item || !model)
        return;
    const QHash<int, QByteArray> names = model->roleNames();
    const QModelIndex index = model->index(row);
    const QMetaObject *meta = item->metaObject();
    for (auto it = names.cbegin(); it != names.cend(); ++it) {
        if (!changed.isEmpty() && !changed.contains(it.key()))
            continue;
        const QVariant value = model->data(index, it.key());
        if (roles && rowDataRoles().contains(it.value()))
            roles->insert(QString::fromLatin1(it.value()), value);
        if (const int p = delegateProperty(meta, it.value()); p >= 0)
            meta->property(p).write(item, value);
    }
}

void setIndex(QQuickItem *item, int row)
{
    if (item)
        item->setProperty("index", row);
}

void release(QQuickItem *item)
{
    if (!item)
        return;
    item->setVisible(false);
    item->setParentItem(nullptr);
    item->deleteLater();
}

} // namespace brulion::editor::hostedrow
