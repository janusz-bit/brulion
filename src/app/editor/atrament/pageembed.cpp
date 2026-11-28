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
    // (The row is laid out in the frame that shows it: its part is where the view around goes.)
    if (m_reveal)
        m_layoutAround = m_reveal->row;
    return std::exchange(m_reveal, std::nullopt);
}

void PageView::setEmbeddedView(float top, float bottom)
{
    m_embeddedView = std::pair{top, bottom};
}

std::vector<std::pair<float, float>> PageView::embeddedSpans(float screen) const
{
    // What the view around shows of the page, in its rows' coordinates: by the last frame (the
    // scroll since added) and where the view around says its next frame goes; the edited row and a
    // row revealed.
    std::vector<std::pair<float, float>> spans;
    const ForeignFrame *header = edgeFrame(ForeignFrame::Kind::Header);
    if (const std::optional<at::Rect> view = listViewport(); header && view) {
        const float top = view->top() - header->bounds.bottom() + scrolledSinceFrame();
        spans.emplace_back(top, top + view->height());
    }
    if (m_embeddedView)
        spans.emplace_back(m_embeddedView->first - m_headerHeight, m_embeddedView->second - m_headerHeight);
    if (spans.empty())
        spans.emplace_back(0.0f, screen);
    for (const int row : {m_controller ? m_controller->editingRow() : -1, m_layoutAround.value_or(-1)}) {
        if (row >= 0 && std::size_t(row) < m_extents.count()) {
            const float top = m_extents.offsetOf(std::size_t(row));
            spans.emplace_back(top, top + m_extents.heightOf(std::size_t(row)));
        }
    }
    return spans;
}

std::vector<std::pair<std::size_t, std::size_t>> PageView::embeddedRows(at::Window &window)
{
    const std::size_t n = m_extents.count();
    if (n == 0 || m_extents.total() <= 0)
        return {};
    const std::optional<at::Rect> view = listViewport();
    const float screen = std::max(1.0f, view ? view->height() : window.viewportSize().height);
    const std::vector<std::pair<float, float>> spans = embeddedSpans(screen);
    m_layoutAround.reset();
    // A screen around each, as rows, in order and merged.
    std::vector<std::pair<std::size_t, std::size_t>> ranges;
    for (const auto &[from, to] : spans) {
        if (to + screen < 0 || from - screen > m_extents.total())
            continue;
        const std::size_t first = std::min(n - 1, m_extents.rowAt(std::max(0.0f, from - screen)));
        const std::size_t last = std::min(n - 1, m_extents.rowAt(std::max(0.0f, to + screen)));
        ranges.emplace_back(first, std::max(first, last));
    }
    std::sort(ranges.begin(), ranges.end());
    std::vector<std::pair<std::size_t, std::size_t>> merged;
    for (const auto &r : ranges) {
        if (!merged.empty() && r.first <= merged.back().second + 1)
            merged.back().second = std::max(merged.back().second, r.second);
        else
            merged.push_back(r);
    }
    return merged;
}

std::optional<std::pair<quint64, at::RowPart>> PageView::embeddedRowAt(float y, at::Window &window) const
{
    for (auto it = m_rowNodes.rbegin(); it != m_rowNodes.rend(); ++it) {
        const at::Rect r = window.layoutEngine().absolute(it->second);
        if (y >= r.top() && y < r.bottom() && m_model)
            return std::pair{m_model->uidAt(it->first), at::RowPart{r.top(), r.height()}};
    }
    return std::nullopt;
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
    // Only the rows near what the view around shows (and the edited one, one being revealed); the
    // rest are room of their heights — measured, or the estimate.
    float laidTo = 0;
    for (const auto &[first, last] : m_embeddedRanges) {
        if (last >= m_extents.count())
            break; // (rows removed since the frame began)
        if (const float gap = m_extents.offsetOf(first) - laidTo; gap > 0.01f)
            std::move(column).child(at::div().h(gap).flexShrink0().ariaHidden());
        for (std::size_t r = first; r <= last; ++r) {
            if (m_extents.isHidden(r))
                continue; // a fold, a zoom
            const quint64 uid = m_model ? m_model->uidAt(int(r)) : 0;
            std::move(column).child(RowBox(renderRow(r), this, r, uid));
        }
        laidTo = m_extents.offsetOf(last + 1);
    }
    if (const float rest = m_extents.total() - laidTo; rest > 0.01f)
        std::move(column).child(at::div().h(rest).flexShrink0().ariaHidden());
    return std::move(column)
            .child(embeddedFooter())
            // A drag of blocks: the drop line, the listener that follows it (pagedrag.cpp); the
            // selection's keys (pageselection.cpp: the active day's).
            .child(dragLayer())
            .child(keyLayer());
}

} // namespace brulion::editor::atr
