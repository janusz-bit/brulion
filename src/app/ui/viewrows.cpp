#include "viewrows.h"

#include <QMetaObject>
#include <QQuickItem>
#include <QVariant>

namespace brulion::ui {

namespace {

/// The view's item of row `index` (nullptr when the view has not placed one).
QQuickItem *itemAtIndex(QQuickItem *view, int index)
{
    QQuickItem *item = nullptr;
    QMetaObject::invokeMethod(view, "itemAtIndex", Qt::DirectConnection, qReturnArg(item), index);
    return item;
}

} // namespace

std::optional<int> placedRow(QQuickItem *view, const QQuickItem *delegate)
{
    if (!view || !delegate)
        return std::nullopt;
    const QVariant index = delegate->property("index");
    const QMetaObject *mo = view->metaObject();
    if (mo->indexOfMethod("itemAtIndex(int)") < 0 || mo->indexOfMethod("indexAt(qreal,qreal)") < 0)
        return index.isValid() ? index.toInt() : -1; // no item view: its own word
    int row = -1;
    if (index.isValid()) {
        row = index.toInt();
    } else {
        if (delegate->width() <= 0 || delegate->height() <= 0)
            return -1;
        // Content coordinates are the content item's: its children's own x and y.
        QMetaObject::invokeMethod(view, "indexAt", Qt::DirectConnection, qReturnArg(row),
                                  delegate->x() + delegate->width() / 2, delegate->y() + delegate->height() / 2);
    }
    if (row < 0 || itemAtIndex(view, row) != delegate)
        return std::nullopt;
    return row;
}

} // namespace brulion::ui
