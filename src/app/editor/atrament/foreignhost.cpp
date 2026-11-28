#include "foreignhost.h"

#include "blocklistmodel.h"
#include "editorhost.h"
#include "hostedrow.h"
#include "qthost.h"

#include <QQmlComponent>
#include <QQuickItem>
#include <QQuickWindow>

#include <algorithm>
#include <optional>
#include <unordered_set>

namespace brulion::editor::atr {

namespace at = brulion::atrament;

ForeignRowHost::ForeignRowHost(at::AtramentItem *item, at::App &app, at::Entity<PageView> page, BlockListModel *model)
    : m_item(item), m_app(app), m_page(std::move(page)), m_model(model)
{
    if (!model)
        return;
    // Delegates follow their rows (QQmlDelegateModel's index and roles); removed rows' go.
    connect(model, &QAbstractItemModel::rowsInserted, this, &ForeignRowHost::rowsChanged);
    connect(model, &QAbstractItemModel::rowsRemoved, this, &ForeignRowHost::rowsChanged);
    connect(model, &QAbstractItemModel::rowsMoved, this, &ForeignRowHost::rowsChanged);
    connect(model, &QAbstractItemModel::modelReset, this, &ForeignRowHost::rowsChanged);
    connect(model, &QAbstractItemModel::layoutChanged, this, &ForeignRowHost::rowsChanged);
    connect(model, &QAbstractItemModel::dataChanged, this, &ForeignRowHost::dataChanged);
}

ForeignRowHost::~ForeignRowHost()
{
    for (auto &[uid, h] : m_hosted)
        delete h.item.data();
    m_hosted.clear();
}

void ForeignRowHost::setDelegate(QQmlComponent *delegate)
{
    if (delegate == m_delegate)
        return;
    releaseAll();
    m_delegate = delegate;
    if (m_item)
        m_item->requestFrame();
}

void ForeignRowHost::adopt(QQuickItem *edge)
{
    if (!edge)
        return;
    edge->setParentItem(m_item);
    connect(edge, &QQuickItem::heightChanged, this, &ForeignRowHost::itemHeightChanged);
}

void ForeignRowHost::setHeader(QQuickItem *header)
{
    setEdge(m_header, header, ForeignFrame::Kind::Header);
}

void ForeignRowHost::setFooter(QQuickItem *footer)
{
    setEdge(m_footer, footer, ForeignFrame::Kind::Footer);
}

void ForeignRowHost::setEdge(QPointer<QQuickItem> &edge, QQuickItem *item, ForeignFrame::Kind kind)
{
    if (item == edge)
        return;
    const bool had = edge;
    if (edge)
        disconnect(edge, nullptr, this, nullptr);
    edge = item;
    adopt(item);
    // (Its item gone, the page has no room there: a day zoomed out has no breadcrumb.)
    if (had && !item)
        tellEdge(kind);
    itemHeightChanged();
}

void ForeignRowHost::tellEdge(ForeignFrame::Kind kind)
{
    m_page.update(m_app, [kind](PageView &page, at::Context<PageView> &cx) {
        if (kind == ForeignFrame::Kind::Header ? page.setHeaderHeight(0) : page.setFooterHeight(0))
            cx.notify();
    });
}

QQuickItem *ForeignRowHost::rowItem(int row) const
{
    if (!m_model || row < 0 || row >= m_model->count())
        return nullptr;
    const auto it = m_hosted.find(m_model->uidAt(row));
    return it != m_hosted.end() ? it->second.item.data() : nullptr;
}

QQuickItem *ForeignRowHost::hostedItem(int row, quint64 uid)
{
    if (auto it = m_hosted.find(uid); it != m_hosted.end()) {
        Hosted &h = it->second;
        if (h.item) {
            if (h.row != row) {
                h.row = row;
                hostedrow::setIndex(h.item, row);
            }
            return h.item;
        }
        m_hosted.erase(it);
    }
    if (!m_item || !m_model)
        return nullptr;
    QQmlPropertyMap *roles = nullptr;
    QQuickItem *item = hostedrow::create(m_delegate, m_item, m_model, row, &roles);
    if (!item)
        return nullptr;
    m_hosted[uid] = Hosted{item, roles, row, m_frame};
    connect(item, &QQuickItem::heightChanged, this, &ForeignRowHost::itemHeightChanged);
    return item;
}

bool ForeignRowHost::hosts(const QQuickItem *item) const
{
    if (!item)
        return false;
    for (const auto &[uid, h] : m_hosted)
        if (h.item && (h.item == item || h.item->isAncestorOf(item)))
            return true;
    return false;
}

void ForeignRowHost::release(quint64 uid)
{
    const auto it = m_hosted.find(uid);
    if (it == m_hosted.end())
        return;
    if (it->second.item)
        disconnect(it->second.item, nullptr, this, nullptr);
    hostedrow::release(it->second.item);
    m_hosted.erase(it);
}

void ForeignRowHost::releaseAll()
{
    std::vector<quint64> uids;
    for (const auto &[uid, h] : m_hosted)
        uids.push_back(uid);
    for (quint64 uid : uids)
        release(uid);
}

void ForeignRowHost::rowsChanged()
{
    if (!m_model) {
        releaseAll();
        return;
    }
    std::vector<quint64> gone;
    for (auto &[uid, h] : m_hosted) {
        const int row = m_model->rowOfUid(uid);
        if (row < 0 || !h.item) {
            gone.push_back(uid);
            continue;
        }
        if (row != h.row) {
            h.row = row;
            hostedrow::setIndex(h.item, row);
        }
    }
    for (quint64 uid : gone)
        release(uid);
}

void ForeignRowHost::dataChanged(const QModelIndex &first, const QModelIndex &last, const QList<int> &roles)
{
    if (!m_model || m_hosted.empty())
        return;
    for (int row = std::max(0, first.row()); row <= last.row() && row < m_model->count(); ++row) {
        const auto it = m_hosted.find(m_model->uidAt(row));
        if (it != m_hosted.end() && it->second.item)
            hostedrow::update(it->second.item, it->second.roles, m_model, row, roles);
    }
}

bool ForeignRowHost::tellHeights()
{
    // (Read first: a delegate's height may change while the page is updated.)
    struct Height {
        int row;
        float px;
    };
    std::vector<Height> rows;
    for (const auto &[uid, h] : m_hosted)
        if (h.item)
            rows.push_back({h.row, float(h.item->height())});
    const std::optional<float> header = m_header ? std::optional<float>(float(m_header->height())) : std::nullopt;
    const std::optional<float> footer = m_footer ? std::optional<float>(float(m_footer->height())) : std::nullopt;
    return m_page.update(m_app, [&](PageView &page, at::Context<PageView> &cx) {
        // Each edge while it has an item (a day of the journal stream: the room under its blocks is
        // the stream's, a header only while it is zoomed into a block).
        bool changed = page.setEdgeHeights(header, footer);
        for (const Height &h : rows) {
            // Only rows the page does not draw itself (a delegate on its way out keeps nothing).
            if (!page.drawsRow(h.row))
                changed = page.setForeignHeight(h.row, h.px) || changed;
        }
        if (changed)
            cx.notify();
        return changed;
    });
}

void ForeignRowHost::itemHeightChanged()
{
    // Between frames (a picture loaded, a query answered, the header's title wrapped): the page
    // has it now, and draws again. During place() it is told at its end.
    if (!m_placing)
        tellHeights();
}

void ForeignRowHost::place()
{
    if (!m_item || !m_model)
        return;
    m_placing = true;
    ++m_frame;
    const PageView &page = m_page.read(m_app);
    const std::vector<ForeignFrame> frames = page.foreignFrames();

    // The header, the footer and the rows the frame painted: over their boxes.
    std::unordered_set<quint64> shown;
    for (const ForeignFrame &f : frames) {
        QQuickItem *item = nullptr;
        switch (f.kind) {
        case ForeignFrame::Kind::Header: item = m_header; break;
        case ForeignFrame::Kind::Footer: item = m_footer; break;
        case ForeignFrame::Kind::Row:
            item = hostedItem(f.row, f.uid);
            if (item) {
                m_hosted[f.uid].lastFrame = m_frame;
                shown.insert(f.uid);
            }
            break;
        }
        if (item) {
            item->setPosition(QPointF(f.bounds.left(), f.bounds.top()));
            item->setVisible(true);
        }
    }

    // Foreign rows near the view: made (and measured) before they scroll in, where their rows are.
    const float screen = float(m_item->height());
    for (int row : page.foreignRowsNear(screen / 2, screen / 2)) {
        const quint64 uid = m_model->uidAt(row);
        if (shown.count(uid))
            continue;
        if (hostedItem(row, uid))
            m_hosted[uid].lastFrame = m_frame;
    }

    // Away from the view: gone, unless edited or holding the keyboard; within one and a half screens
    // they stay (a scroll back and forth over the edge, a new width, makes none again).
    std::unordered_set<int> keep;
    for (int row : page.foreignRowsNear(screen * 1.5f, screen * 1.5f))
        keep.insert(row);
    QQuickItem *focus = m_item->window() ? m_item->window()->activeFocusItem() : nullptr;
    std::vector<quint64> gone;
    for (auto &[uid, h] : m_hosted) {
        if (!h.item || page.drawsRow(h.row)) {
            gone.push_back(uid);
            continue;
        }
        const bool busy =
                h.item->property("isEditing").toBool() || (focus && (focus == h.item || h.item->isAncestorOf(focus)));
        if (h.lastFrame != m_frame && !keep.count(h.row) && !busy) {
            gone.push_back(uid);
            continue;
        }
        if (!shown.count(uid)) {
            // Out of the frame: where its row is (out of view, cut by the item), or nowhere.
            if (const std::optional<at::Rect> b = page.rowBounds(h.row)) {
                h.item->setPosition(QPointF(b->left(), b->top()));
                h.item->setVisible(true);
            } else {
                h.item->setVisible(false);
            }
        }
    }
    for (quint64 uid : gone)
        release(uid);
    m_placing = false;

    // What the items measured: the page's next frame comes at once when it moved anything.
    tellHeights();
}

} // namespace brulion::editor::atr
