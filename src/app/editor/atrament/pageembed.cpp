// PageView's embedded mode (pageview.h; docs/ATRAMENT.md "The journal stream as one list"): the
// page drawn into another view's list — a day of the journal stream — as a column of its header,
// every row it shows and its footer, without a list of its own. Each row is measured as the frame
// lays it out (its height to PageRows, its layout node kept for embeddedPart()); what would scroll
// the page's list asks the view around (PageEmbedding, takeReveal()).
#include "pageview.h"

#include "blocklistmodel.h"
#include "editorhost.h"
#include "pagecontroller.h"

#include <algorithm>
#include <cmath>
#include <utility>

namespace brulion::editor::atr {

namespace at = brulion::atrament;

// A row of the embedded column: it lays out as its row, tells the page the height the frame gave
// it (the page's rows measured exactly, as its list would) and remembers its layout node.
class PageView::RowBox final : public at::Element {
public:
    RowBox(at::AnyElement child, PageView *page, std::size_t row, quint64 uid)
        : m_child(std::move(child)), m_page(page), m_row(row), m_uid(uid)
    {
    }
    at::NodeId requestLayout(at::Window &window, at::App &app) override
    {
        const at::NodeId node = m_child.requestLayout(window, app);
        m_page->m_rowNodes.emplace_back(int(m_row), node);
        return node;
    }
    void prepaint(at::Rect bounds, at::Window &window, at::App &app) override
    {
        const float h = std::max(1.0f, bounds.height());
        PageExtents &ex = m_page->m_extents;
        if (!ex.isMeasured(m_row) || std::abs(ex.heightOf(m_row) - h) > 0.01f)
            ex.setMeasured(m_row, h);
        m_child.prepaint(window, app);
    }
    void paint(at::Rect, at::Window &window, at::App &app) override { m_child.paint(window, app); }
    at::ElementId id() const override { return at::ElementId(u"row", m_uid); }
    bool accessibilityHidden() const override
    {
        return m_child.element() && m_child.element()->accessibilityHidden();
    }

private:
    at::AnyElement m_child;
    PageView *m_page;
    std::size_t m_row;
    quint64 m_uid;
};

void PageView::setEmbedding(std::optional<PageEmbedding> embedding, at::Context<PageView> &cx)
{
    m_embedding = std::move(embedding);
    m_reveal.reset();
    cx.notify();
}

std::optional<PageView::Reveal> PageView::takeReveal()
{
    if (m_revealEdited) {
        m_revealEdited = false;
        if (const int edited = editedRow(); edited >= 0)
            m_reveal = Reveal{edited, at::ListPosition::Nearest};
    }
    return std::exchange(m_reveal, std::nullopt);
}

std::optional<at::RowPart> PageView::embeddedPart(int row, at::Window &window) const
{
    // (The last layout of the row: the page may be laid out twice in a frame — measured, shown.)
    for (auto it = m_rowNodes.rbegin(); it != m_rowNodes.rend(); ++it) {
        if (it->first != row)
            continue;
        const at::Rect r = window.layoutEngine().absolute(it->second);
        return at::RowPart{r.top(), r.height()};
    }
    return std::nullopt;
}

void PageView::setKeysActive(bool on, at::Context<PageView> &cx)
{
    if (on == m_keysActive)
        return;
    m_keysActive = on;
    cx.notify();
}

void PageView::pointerReleased(at::Context<PageView> &cx)
{
    if (!m_dragUid && !m_dragSelected)
        return;
    m_dragUid = 0;
    m_dragSelected = false;
    m_dragFocus = -1;
    cx.notify();
}

int PageView::pressedRow() const
{
    return m_dragUid && m_model ? m_model->rowOfUid(m_dragUid) : -1;
}

void PageView::scrollRowTo(std::size_t row, at::ListPosition position)
{
    if (m_embedding)
        m_reveal = Reveal{int(row), position};
    else
        m_handle.scrollToRow(row, position);
}

bool PageView::scrollPageBy(float dy)
{
    if (m_embedding)
        return m_embedding->scrollBy && m_embedding->scrollBy(dy);
    return m_handle.scrollBy(dy);
}

void PageView::footerTapped()
{
    if (onFooterTap) {
        onFooterTap();
        return;
    }
    // (Reading mode, a selection: a tap under the blocks — often a scroll's end — adds nothing.)
    if (!m_controller || m_controller->readOnly() || m_controller->selectionCount() > 0)
        return;
    const int row = m_controller->appendEmptyBlock();
    if (row >= 0)
        m_controller->startEditing(row, -1);
}

at::AnyElement PageView::embeddedFooter()
{
    at::AnyElement edge = edgeElement(ForeignFrame::Kind::Footer);
    if (m_footerTap <= 0 || !m_controller)
        return edge;
    // The tap strip under the blocks, reaching past the footer by as much as a touch target needs.
    const at::EntityId self = m_selfId;
    const bool editable = !m_controller->readOnly();
    return at::div()
            .child(std::move(edge))
            .child(at::div()
                           .id(u"footer")
                           .absolute()
                           .top(0)
                           .left(0)
                           .right(0)
                           .h(std::max(m_footerTap, m_footerHeight))
                           .role(at::Role::None)
                           .when(editable, [](at::Div &&d) { return std::move(d).cursorText(); })
                           .onClick([self](const at::ClickEvent &click, at::Window &, at::App &a) {
                               if (click.isKeyboard || click.fromAccessibility)
                                   return;
                               a.updateEntity<PageView>(self, [](PageView &v, at::Context<PageView> &) {
                                   v.footerTapped();
                               });
                           }));
}

at::AnyElement PageView::embeddedColumn()
{
    m_rowNodes.clear();
    const at::EntityId rootSelf = m_selfId;
    at::Div column = at::div()
                             .flexCol()
                             .textSize(m_metrics.fontSize)
                             .lineHeight(m_metrics.lineHeight)
                             .textColor(m_palette.text)
                             .fontFamily(m_palette.bodyFamily)
                             // The page's own listeners (render()'s): a press dragged across its
                             // blocks, a drag of blocks.
                             .onMouseUp(at::MouseButton::Left,
                                        [rootSelf](const at::MouseUpEvent &e, at::Window &, at::App &a) {
                                            a.updateEntity<PageView>(rootSelf, [&](PageView &v, at::Context<PageView> &c) {
                                                v.m_dragUid = 0;
                                                if (v.m_drag.kind != DragKind::None)
                                                    v.dragReleased(e.cancelled, c);
                                            });
                                        })
                             .onMouseMove([rootSelf](const at::MouseMoveEvent &e, at::Window &, at::App &a) {
                                 a.updateEntity<PageView>(rootSelf, [&](PageView &v, at::Context<PageView> &c) {
                                     if (v.m_drag.kind != DragKind::None) {
                                         if (v.followsPointer(e.pointer))
                                             v.dragMoved(e.position, c);
                                     } else if (e.pointer == at::PointerKind::Mouse
                                                && e.pressedButton == at::MouseButton::Left)
                                         v.dragOver(e.position, c);
                                 });
                             });
    std::move(column).child(edgeElement(ForeignFrame::Kind::Header));
    const std::size_t n = m_extents.count();
    for (std::size_t r = 0; r < n; ++r) {
        if (m_extents.isHidden(r))
            continue; // a fold, a zoom
        const quint64 uid = m_model ? m_model->uidAt(int(r)) : 0;
        std::move(column).child(RowBox(renderRow(r), this, r, uid));
    }
    return std::move(column)
            .child(embeddedFooter())
            // A drag of blocks: the drop line, the listener that follows it (pagedrag.cpp); the
            // selection's keys (pageselection.cpp: the active day's).
            .child(dragLayer())
            .child(keyLayer());
}

} // namespace brulion::editor::atr
