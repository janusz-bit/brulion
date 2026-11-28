// PageView's drags of blocks (docs/ATRAMENT.md "The block editor in Atrament (M2)", steps 3
// "Selection" and 4 "Structure"): selecting by dragging — a mouse's press dragged from one block
// into another (PageView::dragOver starts it), a finger's after a long press, a grip of the touch
// selection mode — and moving blocks — an outline's bullet (a list item's marker) dragged with the
// mouse, a selected block long-pressed while its grips show — with the drop line between any two
// rows, as deep as the pointer's x says (the moved blocks' text column: a child of the block above,
// out of a parent), and one undo step (PageController::moveSelectionTo). Near the view's top or
// bottom edge the page scrolls while the drag is held there (atrament::EdgeScroller). While a drag
// goes on its moves and its release are taken before the rows see them (a capture listener of the
// drag layer): no hover follows it, its release clicks nothing.
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
    const std::optional<at::Rect> viewport = listViewport();
    if (!header || !viewport || !m_controller || !m_model || m_model->rowCount() == 0)
        return -1;
    // The pointer held into the view (above or below it, the rows at its edge), where the rows
    // are now.
    const at::Rect view = *viewport;
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

float PageView::depthX(int depth) const
{
    const PageMetrics &m = m_metrics;
    return m.contentX + m.gutter + float(depth) * (m.documentMode ? m.listIndent : m.indentStep);
}

int PageView::depthAtX(float x) const
{
    const PageMetrics &m = m_metrics;
    const float step = std::max(1.0f, m.documentMode ? m.listIndent : m.indentStep);
    return std::max(0, int(std::lround((x - m.contentX - m.gutter) / step)));
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
    if (active && kind == DragKind::Move) {
        grabMovedBlocks();
        updateDropTarget();
    }
    cx.notify(); // the drag layer follows it from the next frame
}

void PageView::grabMovedBlocks()
{
    // The pointer's offset from the moved blocks' text column: where it goes, their column goes
    // (and with it the depth they would get).
    const int first = m_controller ? m_controller->selectionFirstRow() : -1;
    const int depth = first >= 0 && m_model ? m_model->index(first, 0).data(BlockListModel::DepthRole).toInt() : 0;
    m_drag.grab = m_drag.start.x - depthX(std::max(0, depth));
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
        grabMovedBlocks();
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
    case DragKind::Select:
    case DragKind::Extend: {
        // A mouse's press dragged across blocks (dragOver began it), a grip or a long press (the
        // fixed end stays, the pointer moves the other one).
        const int fixed = m_model->rowOfUid(m_drag.kind == DragKind::Select ? m_dragUid : m_drag.uid);
        if (fixed < 0)
            return;
        const int row = dragRowAt(m_drag.last);
        if (row < 0 || m_model->index(row, 0).data(BlockListModel::ViewRole).toString() == u"frontmatter")
            return;
        selectDraggedTo(fixed, row);
        return;
    }
    case DragKind::Move:
        updateDropTarget();
        return;
    }
}

void PageView::selectDraggedTo(int fixed, int row)
{
    PageController *ctl = m_controller.data();
    if (m_drag.kind == DragKind::Select) {
        if (row == m_dragFocus)
            return;
        m_dragFocus = row;
        if (onDragSelect) {
            onDragSelect(fixed, row);
        } else {
            ctl->stopEditing();
            ctl->selectRange(fixed, row);
        }
        return;
    }
    if (ctl->selectionAnchorRow() == fixed && ctl->selectedRow() == row && !ctl->selectionPicked())
        return;
    ctl->selectRange(fixed, row);
}

float PageView::dragTextX() const
{
    return m_drag.last.x - m_drag.grab;
}

void PageView::updateDropTarget()
{
    PageController *ctl = m_controller.data();
    m_drag.drop = ctl ? dropAt(m_drag.last, dragTextX(), [ctl](BlockPlace place) {
        return ctl->canMoveSelectionTo(place);
    })
                      : std::nullopt;
}

std::optional<PageView::DropTarget> PageView::dropAt(at::Point p, float textX,
                                                     const std::function<bool(BlockPlace)> &takes) const
{
    const ForeignFrame *header = edgeFrame(ForeignFrame::Kind::Header);
    const PageController *ctl = m_controller.data();
    if (!header || !ctl || !m_model || !takes)
        return std::nullopt;
    const markdown::BlockDocument &doc = m_model->document();
    // The gap nearest the pointer (where the rows are now): above or below the half of the shown
    // row under it, before the first one, after the last one.
    const float into = p.y + scrolledSinceFrame() - header->bounds.bottom();
    int above = -1;
    int below = -1;
    if (into < 0) {
        below = ctl->nextVisibleRow(-1);
    } else if (into >= m_extents.total()) {
        above = ctl->lastVisibleRow();
    } else {
        const std::size_t r = m_extents.rowAt(into);
        if (into < m_extents.offsetOf(r) + m_extents.heightOf(r) / 2) {
            below = int(r);
            above = ctl->previousVisibleRow(int(r));
        } else {
            above = int(r);
            below = ctl->nextVisibleRow(int(r));
        }
    }
    const int zoom = ctl->zoomBlock();
    const int end = zoom >= 0 ? doc.subtreeRange(zoom).last() + 1 : doc.count();
    const int gap = below >= 0 ? below : end;
    // The levels the gap allows: from the next block's (a sibling of it) to a child of the one
    // above; the one nearest the moved blocks' text column that takes them.
    const int lo = below >= 0 ? doc.at(below).depth : (zoom >= 0 ? doc.at(zoom).depth + 1 : 0);
    const int hi = std::max(lo, above >= 0 ? doc.at(above).depth + 1 : lo);
    const int wanted = std::clamp(depthAtX(textX), lo, hi);
    for (int k = 0; k <= hi - lo; ++k) {
        for (const int depth : {wanted - k, wanted + k}) {
            if (depth < lo || depth > hi || (k > 0 && depth == wanted))
                continue;
            if (takes({gap, depth}))
                return DropTarget{{gap, depth}, {}};
        }
    }
    return std::nullopt;
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
        && m_controller->moveSelectionTo(drop->place))
        revealRow(m_controller->selectedRow(), cx);
    cx.notify();
}

void PageView::paintDropLine(DropTarget &t, at::Window &window)
{
    const ForeignFrame *header = edgeFrame(ForeignFrame::Kind::Header);
    if (!header || !m_model || t.place.gap < 0 || t.place.gap > m_model->rowCount())
        return;
    // From the text column of the drop's level (its bullet's, an outline's) to the column's right edge,
    // cut to the list's view.
    const float y = header->bounds.bottom() + m_extents.offsetOf(std::size_t(t.place.gap));
    const PageMetrics &m = m_metrics;
    const float x0 = depthX(t.place.depth) - (m.documentMode ? 0.0f : m.bulletColumn);
    const float columnRight = m.contentWidth > 0 ? m.contentX + m.contentWidth : header->bounds.right();
    const float x1 = std::min(columnRight, header->bounds.right());
    t.line = at::Rect::fromXYWH(x0, y - 1, std::max(0.0f, x1 - x0), 2);
    window.pushContentMask(header->mask);
    window.paintQuad(t.line, at::Background::solid(m_dropColor), {}, {}, at::Corners::all(1));
    // A dot at its start, as the line's handle.
    window.paintQuad(at::Rect::fromXYWH(x0 - 4, y - 4, 8, 8), at::Background::solid(m_dropColor), {}, {},
                     at::Corners::all(4));
    window.popContentMask();
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
               if (self->m_drag.kind == DragKind::Move && self->m_drag.active && self->m_drag.drop)
                   self->paintDropLine(*self->m_drag.drop, window);
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
