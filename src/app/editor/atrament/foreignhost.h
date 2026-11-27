// ForeignRowHost — the QML items over a PageView's foreign rows, its header and its footer
// (foreignrows.h; docs/ATRAMENT.md "Rows Atrament does not draw yet"), inside the AtramentItem that
// shows the page (qthost.h "Hosted children").
//
// After every frame the item builds (AtramentItem::placeHostedChildren → place()):
// * each foreign row the frame painted gets its delegate — the editor's BlockDelegate, made for the
//   row as PageItem makes its hosted rows (hostedrow.h: the row's roles as required properties,
//   `index`, a model object), reused by the block's uid — placed over the row's box: in the same
//   polish, so it scrolls with the list in the same frame;
// * foreign rows within half a screen above and below the view get theirs too (placed where their
//   rows are, out of view), so they are measured before they scroll in;
// * delegates more than one and a half screens away go, unless edited or holding the keyboard
//   (a little hysteresis: a scroll back and forth over the edge makes none again);
// * the delegates', the header's and the footer's heights go to the page (setForeignHeight…): a
//   change draws the next frame at once, in the same polish, so nothing lags behind. A height that
//   changes later (a picture loaded, a query's results) is told when it changes.
// Delegates follow the model's rows (their `index`, their roles), and go with their rows.
//
//   ForeignRowHost host(item, app, pageEntity, model);   // item: the AtramentItem showing the page
//   host.setDelegate(component); host.setHeader(headerItem); host.setFooter(footerItem);
//   … AtramentItem subclass: void placeHostedChildren() override { host.place(); }
#pragma once

#include "blocklistmodel.h"
#include "editorhost.h"
#include "pageview.h"

#include "ui/app.h"

#include <QList>
#include <QModelIndex>
#include <QObject>
#include <QPointer>

#include <unordered_map>
#include <vector>

class QQmlComponent;
class QQmlPropertyMap;
class QQuickItem;

namespace brulion::atrament {
class AtramentItem;
}

namespace brulion::editor::atr {

class ForeignRowHost final : public QObject {
    Q_OBJECT
public:
    ForeignRowHost(atrament::AtramentItem *item, atrament::App &app, atrament::Entity<PageView> page,
                   BlockListModel *model);
    ~ForeignRowHost() override;

    // The delegate of foreign rows (a BlockDelegate: required properties named after the model's roles).
    void setDelegate(QQmlComponent *delegate);
    QQmlComponent *delegate() const { return m_delegate.data(); }
    // Above the blocks and below them (made children of the item; their widths are their own).
    void setHeader(QQuickItem *header);
    void setFooter(QQuickItem *footer);
    QQuickItem *header() const { return m_header.data(); }
    QQuickItem *footer() const { return m_footer.data(); }

    // After each frame of the item (see above).
    void place();

    // The delegate hosted for model row `row` (null: none now).
    QQuickItem *rowItem(int row) const;
    int hostedCount() const { return int(m_hosted.size()); }

private:
    struct Hosted {
        QPointer<QQuickItem> item;
        QQmlPropertyMap *roles = nullptr; // the delegate's model object (a child of the item)
        int row = -1;
        quint64 lastFrame = 0;
    };

    QQuickItem *hostedItem(int row, quint64 uid);
    void release(quint64 uid);
    void releaseAll();
    void rowsChanged();
    void dataChanged(const QModelIndex &first, const QModelIndex &last, const QList<int> &roles);
    // The items' heights to the page; whether one changed (the page then draws again).
    bool tellHeights();
    void itemHeightChanged();
    void adopt(QQuickItem *edge);

    QPointer<atrament::AtramentItem> m_item;
    atrament::App &m_app;
    atrament::Entity<PageView> m_page;
    QPointer<BlockListModel> m_model;
    QPointer<QQmlComponent> m_delegate;
    QPointer<QQuickItem> m_header;
    QPointer<QQuickItem> m_footer;
    std::unordered_map<quint64, Hosted> m_hosted; // by block uid
    quint64 m_frame = 0;
    bool m_placing = false;
};

} // namespace brulion::editor::atr
