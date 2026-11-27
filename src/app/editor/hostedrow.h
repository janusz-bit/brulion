// A row's delegate outside a ListView: the editor's BlockDelegate made for one row of
// BlockListModel by an item that draws the other rows itself — PageItem's hosted rows (pageitem.h)
// and the Atrament page's foreign rows (atrament/foreignhost.h) — given what QQmlDelegateModel gives
// a ListView's delegate: every required property named after one of the model's roles, `index`,
// and a `model` object of its own (a QQmlPropertyMap with the roles BlockRowData reads).
#pragma once

#include <QList>

class QQmlComponent;
class QQmlPropertyMap;
class QQuickItem;

namespace brulion::editor {

class BlockListModel;

namespace hostedrow {

// `delegate` made for model row `row`, a child of `parent` (in `parent`'s QML context when the
// component has none of its own); `roles` gets its model object (a child of the item). Null when
// the component is not ready or makes no item.
QQuickItem *create(QQmlComponent *delegate, QQuickItem *parent, BlockListModel *model, int row,
                   QQmlPropertyMap **roles);
// Roles of the row changed (`changed` empty: all of them): the delegate's properties and its model
// object follow.
void update(QQuickItem *item, QQmlPropertyMap *roles, BlockListModel *model, int row, const QList<int> &changed);
// The row is at `row` now (rows inserted or removed before it).
void setIndex(QQuickItem *item, int row);
// The delegate goes: hidden and out of the scene at once, deleted later.
void release(QQuickItem *item);

} // namespace hostedrow
} // namespace brulion::editor
