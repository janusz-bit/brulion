// PageView's drags of blocks (docs/ATRAMENT.md "The block editor in Atrament (M2)", steps 3
// "Selection" and 4 "Structure"): selecting by dragging — a mouse's press dragged from one block
// into another (PageView::dragOver starts it), a finger's after a long press, a grip of the touch
// selection mode — and moving blocks — an outline's bullet (a list item's marker) dragged with the
// mouse, a selected block long-pressed while its grips show — with the drop line between the rows
// and one undo step (PageController::moveSelectionNextTo). Near the view's top or bottom edge the
// page scrolls while the drag is held there (atrament::EdgeScroller). While a drag goes on its moves
// and its release are taken before the rows see them (a capture listener of the drag layer): no
// hover follows it, its release clicks nothing.
#include "pageview.h"

#include "blocklistmodel.h"
#include "editorhost.h"
#include "pagecontroller.h"

#include <algorithm>
#include <cmath>

namespace brulion::editor::atr {

namespace at = brulion::atrament;

namespace {

// A bullet's press becomes a drag past this many px (a click zooms into the block).
constexpr float kDragThreshold = 4;

} // namespace

float PageView::scrolledSinceFrame() const
{
    if (m_embedding)
        return m_embedding->scrolledSinceFrame ? m_embedding->scrolledSinceFrame() : 0.0f;
    const std::shared_ptr<at::ListHandle::State> st = m_handle.shared();
    return st->scroll.offset.y - st->lastY;
}

std::optional<at::Rect> PageView::listViewport() const
{
    const ForeignFrame *header = edgeFrame(ForeignFrame::Kind::Header);
    if (!header)
        return std::nullopt;
    return header->mask;
}

int PageView::dragRowAt(at::Point p) const
{
    const ForeignFrame *header = edgeFrame(ForeignFrame::Kind::Header);
    if (!header || !m_controller || !m_model || m_model->rowCount() == 0)
        return -1;
    // The pointer held into the view (above or below it, the rows at its edge), where the rows
    // are now.
    const at::Rect view = header->mask;
    const float bottom = std::max(view.top() + 2, view.bottom() - m_dragBottomInset);
    const float y = std::clamp(p.y, view.top() + 1, bottom - 1) + scrolledSinceFrame();
    const int row = rowAtY(y);
    if (row >= 0)
        return row;
    if (y < header->bounds.bottom()) {
        // Above the blocks (the title, the properties): the first block that is no property.
        int first = m_controller->nextVisibleRow(-1);
        while (first >= 0 && m_model->index(first, 0).data(BlockListModel::ViewRole).toString() == u"frontmatter")
            first = m_controller->nextVisibleRow(first);
        return first;
    }
    return m_controller->lastVisibleRow();
}

bool PageView::followsPointer(at::PointerKind pointer) const
{
    return (pointer == at::PointerKind::Mouse) == (m_drag.pointer == at::PointerKind::Mouse);
}

void PageView::beginDrag(DragKind kind, int fixedRow, at::Point p, bool active, at::Context<PageView> &cx)
{
    m_drag.kind = kind;
    m_drag.active = active;
    m_drag.uid = m_model && fixedRow >= 0 ? m_model->uidAt(fixedRow) : 0;
    m_drag.start = p;
    m_drag.last = p;
    m_drag.drop.reset();
    m_edgeScroll.stop();
    if (active && kind == DragKind::Move)
        updateDropTarget();
    cx.notify(); // the drag layer follows it from the next frame
}

void PageView::bulletPressed(int row, at::Point p, at::Context<PageView> &cx)
{
    if (!m_controller || m_controller->readOnly() || !m_model)
        return;
    m_drag.pointer = at::PointerKind::Mouse;
    beginDrag(DragKind::Move, row, p, false, cx);
}

void PageView::rowLongPressed(int row, at::Point p, at::Window &window, at::Context<PageView> &cx)
{
    if (!m_model || row < 0 || row >= m_model->rowCount())
        return;
    PageController *ctl = m_controller.data();
    m_drag.pointer = at::PointerKind::Touch;
    // A block of the selection while its grips show: the finger moves the selected blocks.
    if (ctl && !ctl->readOnly() && m_touchGrips && ctl->selectionCount() > 0
        && m_model->index(row, 0).data(BlockListModel::IsSelectedRole).toBool()) {
        beginDrag(DragKind::Move, row, p, true, cx);
        window.dragAfterLongPress();
        return;
    }
    // Else the touch selection mode (BlockEditor.longPressText), and the finger moving on extends
    // the selection from its anchor ("One by one" picks the block: nothing follows).
    if (onLongPressRow)
        onLongPressRow(row);
    if (!ctl || m_touchPicking || ctl->selectionCount() == 0)
        return;
    const int anchor = ctl->selectionAnchorRow();
    beginDrag(DragKind::Extend, anchor >= 0 ? anchor : row, p, true, cx);
    window.dragAfterLongPress();
}

void PageView::dragMoved(at::Point p, at::Context<PageView> &cx)
{
    if (m_drag.kind == DragKind::None)
        return;
    m_drag.last = p;
    if (!m_drag.active) {
        // A bullet's press: a drag once it went a few px (else it stays a click).
        if (std::max(std::abs(p.x - m_drag.start.x), std::abs(p.y - m_drag.start.y)) < kDragThreshold)
            return;
        const int row = m_model ? m_model->rowOfUid(m_drag.uid) : -1;
        if (row < 0 || !m_controller) {
            m_drag = {};
            return;
        }
        // The pressed block's selection moves; a block outside it is selected alone first.
        if (!m_model->index(row, 0).data(BlockListModel::IsSelectedRole).toBool()) {
            if (onDragSelect) {
                onDragSelect(row, row);
            } else {
                m_controller->stopEditing();
                m_controller->selectRange(row, row);
            }
        }
        m_drag.active = true;
    }
    dragUpdate(cx);
    // Near the view's top or bottom edge the page scrolls while the drag stays there.
    const std::optional<at::Rect> view = listViewport();
    const float speed =
            view ? at::edgeScrollSpeed(p.y, view->top(), std::max(view->top() + 1, view->bottom() - m_dragBottomInset))
                 : 0;
    at::App *app = &cx.app();
    const at::EntityId self = m_selfId;
    m_edgeScroll.update(cx.app(), speed, [app, self](float dy) {
        app->updateEntity<PageView>(self, [dy](PageView &v, at::Context<PageView> &c) {
            if (v.dragKind() == DragKind::None) {
                v.m_edgeScroll.stop();
                return;
            }
            if (v.scrollPageBy(dy)) {
                v.dragUpdate(c); // other rows are under the pointer now
                c.notify();
            }
        });
    });
    cx.notify();
}

void PageView::dragUpdate(at::Context<PageView> &)
{
    PageController *ctl = m_controller.data();
    if (!ctl || !m_model || !m_drag.active)
        return;
    switch (m_drag.kind) {
    case DragKind::None:
        return;
    case DragKind::Select: {
        // A mouse's press dragged across blocks (PageView::dragOver began it).
        const int start = m_model->rowOfUid(m_dragUid);
        const int row = dragRowAt(m_drag.last);
        if (start < 0 || row < 0 || row == m_dragFocus
            || m_model->index(row, 0).data(BlockListModel::ViewRole).toString() == u"frontmatter")
            return;
        m_dragFocus = row;
        if (onDragSelect) {
            onDragSelect(start, row);
        } else {
            ctl->stopEditing();
            ctl->selectRange(start, row);
        }
        return;
    }
    case DragKind::Extend: {
        // A grip or a long press: the fixed end stays, the finger moves the other one.
        const int fixed = m_model->rowOfUid(m_drag.uid);
        const int row = dragRowAt(m_drag.last);
        if (fixed < 0 || row < 0 || m_model->index(row, 0).data(BlockListModel::ViewRole).toString() == u"frontmatter")
            return;
        if (ctl->selectionAnchorRow() == fixed && ctl->selectedRow() == row && !ctl->selectionPicked())
            return;
        ctl->selectRange(fixed, row);
        return;
    }
    case DragKind::Move:
        updateDropTarget();
        return;
    }
}

void PageView::updateDropTarget()
{
    m_drag.drop.reset();
    PageController *ctl = m_controller.data();
    const ForeignFrame *header = edgeFrame(ForeignFrame::Kind::Header);
    if (!ctl || !m_model || !header)
        return;
    const QList<int> siblings = ctl->selectionSiblings();
    if (siblings.isEmpty())
        return;
    const markdown::BlockDocument &doc = m_model->document();
    const qsizetype n = siblings.size();
    // The selected siblings: [a, b].
    qsizetype a = -1;
    qsizetype b = -1;
    for (qsizetype k = 0; k < n; ++k) {
        if (m_model->index(siblings[k], 0).data(BlockListModel::IsSelectedRole).toBool()) {
            if (a < 0)
                a = k;
            b = k;
        }
    }
    if (a < 0)
        return;
    // Each gap's y where the rows are now: before sibling k (the top of its row), after the last
    // one (the end of its subtree).
    const float rowsTop = header->bounds.bottom();
    auto gapY = [&](qsizetype g) {
        const int row = g < n ? siblings[g] : doc.subtreeRange(siblings[n - 1]).last() + 1;
        return rowsTop + m_extents.offsetOf(std::size_t(row));
    };
    const float y = m_drag.last.y + scrolledSinceFrame();
    qsizetype best = -1;
    float bestDistance = 0;
    for (qsizetype g = 0; g <= n; ++g) {
        // (A sibling hidden by a fold or a zoom shows no gap of its own.)
        if (g < n && m_extents.heightOf(std::size_t(siblings[g])) <= 0)
            continue;
        const float d = std::abs(gapY(g) - y);
        if (best < 0 || d < bestDistance) {
            best = g;
            bestDistance = d;
        }
    }
    // Their own place (before, inside or right after them): nothing moves.
    if (best < 0 || (best >= a && best <= b + 1))
        return;
    DropTarget t;
    t.sibling = best < n ? siblings[best] : siblings[n - 1];
    t.after = best == n;
    m_drag.drop = t;
}

std::optional<PageView::DropTarget> PageView::dropTarget() const
{
    if (m_drag.kind != DragKind::Move || !m_drag.active)
        return std::nullopt;
    return m_drag.drop;
}

void PageView::dragReleased(bool cancelled, at::Context<PageView> &cx)
{
    const DragKind kind = m_drag.kind;
    const bool active = m_drag.active;
    const std::optional<DropTarget> drop = m_drag.drop;
    m_drag = {};
    m_edgeScroll.stop();
    m_dragUid = 0;
    m_dragSelected = false;
    m_dragFocus = -1;
    if (kind == DragKind::Move && active && !cancelled && drop && m_controller
        && m_controller->moveSelectionNextTo(drop->sibling, drop->after))
        revealRow(m_controller->selectedRow(), cx);
    cx.notify();
}

at::AnyElement PageView::dragLayer()
{
    // Nothing between drags (no listener, nothing painted).
    if (m_drag.kind == DragKind::None && !m_dragSelected)
        return at::div().absolute().size(0);
    PageView *self = this;
    const at::EntityId id = m_selfId;
    return at::canvas([self, id](at::Rect, at::Window &window, at::App &) {
               // The drop line where the moved blocks would go (BlockDelegate's text column of their
               // level, to the column's right edge), cut to the list's view.
               if (self->m_drag.kind == DragKind::Move && self->m_drag.active && self->m_drag.drop && self->m_model) {
                   DropTarget &t = *self->m_drag.drop;
                   const ForeignFrame *header = self->edgeFrame(ForeignFrame::Kind::Header);
                   if (header && t.sibling >= 0 && t.sibling < self->m_model->rowCount()) {
                       const markdown::BlockDocument &doc = self->m_model->document();
                       const int gapRow = t.after ? doc.subtreeRange(t.sibling).last() + 1 : t.sibling;
                       const float y = header->bounds.bottom() + self->m_extents.offsetOf(std::size_t(gapRow));
                       const PageMetrics &m = self->m_metrics;
                       const QModelIndex index = self->m_model->index(t.sibling, 0);
                       const int depth = std::max(0, index.data(BlockListModel::DepthRole).toInt());
                       const bool listItem = index.data(BlockListModel::KindRole).toString() == u"listItem";
                       const float x0 = m.documentMode
                                                ? m.contentX + m.gutter + (listItem ? float(depth) * m.listIndent : 0)
                                                : m.contentX + m.gutter + float(depth) * m.indentStep - m.bulletColumn;
                       const float columnRight = m.contentWidth > 0 ? m.contentX + m.contentWidth : header->bounds.right();
                       const float x1 = std::min(columnRight, header->bounds.right());
                       t.line = at::Rect::fromXYWH(x0, y - 1, std::max(0.0f, x1 - x0), 2);
                       window.pushContentMask(header->mask);
                       window.paintQuad(t.line, at::Background::solid(self->m_dropColor), {}, {}, at::Corners::all(1));
                       // A dot at its start, as the line's handle.
                       window.paintQuad(at::Rect::fromXYWH(x0 - 4, y - 4, 8, 8), at::Background::solid(self->m_dropColor),
                                        {}, {}, at::Corners::all(4));
                       window.popContentMask();
                   }
               }
               // The drag's moves and its release before anything under it sees them.
               window.onMouseEvent([id](const at::PlatformInput &input, at::DispatchPhase phase, at::Window &w,
                                        at::App &app) {
                   if (phase != at::DispatchPhase::Capture)
                       return;
                   const auto *move = std::get_if<at::MouseMoveEvent>(&input);
                   const auto *up = std::get_if<at::MouseUpEvent>(&input);
                   if (!move && !up)
                       return;
                   bool stop = false;
                   app.updateEntity<PageView>(id, [&](PageView &v, at::Context<PageView> &c) {
                       // A mouse's drag selection that dragOver began: followed from here on.
                       if (v.m_drag.kind == DragKind::None && v.m_dragSelected) {
                           v.m_drag.kind = DragKind::Select;
                           v.m_drag.active = true;
                           v.m_drag.pointer = at::PointerKind::Mouse;
                       }
                       if (v.m_drag.kind == DragKind::None)
                           return;
                       if (move) {
                           // Only the dragging pointer's moves (not a hover Qt Quick makes where a
                           // finger is).
                           if (!v.followsPointer(move->pointer))
                               return;
                           // A mouse released where the window did not see it: the drag is over.
                           if (move->pointer == at::PointerKind::Mouse && move->pressedButton != at::MouseButton::Left) {
                               v.dragReleased(true, c);
                               return;
                           }
                           v.dragMoved(move->position, c);
                           stop = v.m_drag.active;
                       } else {
                           stop = v.m_drag.active;
                           // (Where it was let go counts: a last stretch no move showed.)
                           if (v.m_drag.active && !up->cancelled && !(up->position == v.m_drag.last)) {
                               v.m_drag.last = up->position;
                               v.dragUpdate(c);
                           }
                           v.dragReleased(up->cancelled, c);
                       }
                   });
                   if (stop)
                       w.stopPropagation();
               });
           })
            .absolute()
            .size(0);
}

} // namespace brulion::editor::atr
