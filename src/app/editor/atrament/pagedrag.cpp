// PageView's drags of blocks (docs/ATRAMENT.md "The block editor in Atrament (M2)", steps 3
// "Selection" and 4 "Structure"): selecting by dragging — a mouse's press dragged from one block
// into another (PageView::dragOver starts it), a finger's after a long press, a grip of the touch
// selection mode — and moving blocks — an outline's bullet (a list item's marker) dragged with the
// mouse, a selected block long-pressed while its grips show — with the drop line between any two
// rows, as deep as the pointer's x says (the moved blocks' text column: a child of the block above,
// out of a parent), and one undo step (PageController::moveSelectionTo). Near the view's top or
// bottom edge the page scrolls while the drag is held there (atrament::EdgeScroller). While a drag
// goes on its moves and its release are taken before the rows see them (a capture listener of the
// drag layer, or of the view around for a page embedded in it: followDrag()): no hover follows it,
// its release clicks nothing. Embedded (a day of the journal stream), a drag that leaves the page
// goes on in the view around (PageEmbedding: the selection into the other days, blocks moved into
// another day — the drop line there: setIncomingDrop).
#include "pageview.h"

#include "blocklistmodel.h"
#include "editorhost.h"
#include "pagecontroller.h"

#include <algorithm>
#include <cmath>
#include <utility>
#include <vector>

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
    return m_handle.scrolledSinceFrame();
}

std::optional<at::Rect> PageView::listViewport() const
{
    // Embedded: the view around (the page's header may be far out of it, or not drawn at all).
    if (m_embedding && m_embedding->viewport)
        return m_embedding->viewport();
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
    // Above the blocks (the title, the properties), below them.
    return y < header->bounds.bottom() ? firstBlockRow() : m_controller->lastVisibleRow();
}

int PageView::firstBlockRow() const
{
    if (!m_controller || !m_model)
        return -1;
    int first = m_controller->nextVisibleRow(-1);
    while (first >= 0 && m_model->index(first, 0).data(BlockListModel::ViewRole).toString() == u"frontmatter")
        first = m_controller->nextVisibleRow(first);
    return first;
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
    if (!m_drag.active && !activateBulletDrag(p))
        return;
    dragUpdate(cx);
    followEdge(p, cx);
    cx.notify();
}

bool PageView::activateBulletDrag(at::Point p)
{
    // A bullet's press: a drag once it went a few px (else it stays a click).
    if (std::max(std::abs(p.x - m_drag.start.x), std::abs(p.y - m_drag.start.y)) < kDragThreshold)
        return false;
    const int row = m_model ? m_model->rowOfUid(m_drag.uid) : -1;
    if (row < 0 || !m_controller) {
        m_drag = {};
        return false;
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
    return true;
}

void PageView::followEdge(at::Point p, at::Context<PageView> &cx)
{
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
    case DragKind::Extend:
        selectionDragUpdate();
        return;
    case DragKind::Move:
        moveDragUpdate();
        return;
    }
}

void PageView::moveDragUpdate()
{
    // Over another page of the view around: its drop line shows where the blocks would go.
    if (m_embedding && m_embedding->dragBlocksTo && m_embedding->dragBlocksTo(m_drag.last, dragTextX())) {
        m_drag.drop.reset();
        return;
    }
    updateDropTarget();
}

void PageView::selectionDragUpdate()
{
    // A mouse's press dragged across blocks (dragOver began it), a grip or a long press (the fixed
    // end stays, the pointer moves the other one).
    const int fixed = m_model->rowOfUid(m_drag.kind == DragKind::Select ? m_dragUid : m_drag.uid);
    if (fixed < 0)
        return;
    // Over another page of the view around: the selection goes on there.
    if (m_embedding && m_embedding->selectTo && m_embedding->selectTo(fixed, m_drag.last)) {
        m_dragFocus = -1;
        return;
    }
    const int row = dragRowAt(m_drag.last);
    if (row < 0 || m_model->index(row, 0).data(BlockListModel::ViewRole).toString() == u"frontmatter")
        return;
    selectDraggedTo(fixed, row);
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

std::pair<int, int> PageView::rowsAround(float into) const
{
    // Above or below the half of the shown row there, before the first one, after the last one.
    const PageController *ctl = m_controller.data();
    if (into < 0)
        return {-1, ctl->nextVisibleRow(-1)};
    if (into >= m_extents.total())
        return {ctl->lastVisibleRow(), -1};
    const std::size_t r = m_extents.rowAt(into);
    if (into < m_extents.offsetOf(r) + m_extents.heightOf(r) / 2)
        return {ctl->previousVisibleRow(int(r)), int(r)};
    return {int(r), ctl->nextVisibleRow(int(r))};
}

std::optional<PageView::DropGap> PageView::dropGapAt(at::Point p) const
{
    const ForeignFrame *header = edgeFrame(ForeignFrame::Kind::Header);
    const PageController *ctl = m_controller.data();
    if (!header || !ctl || !m_model)
        return std::nullopt;
    // The gap nearest the pointer, where the rows are now; the levels it allows: from the next
    // block's (a sibling of it) to a child of the one above.
    const auto [above, below] = rowsAround(p.y + scrolledSinceFrame() - header->bounds.bottom());
    const markdown::BlockDocument &doc = m_model->document();
    const int zoom = ctl->zoomBlock();
    const int end = zoom >= 0 ? doc.subtreeRange(zoom).last() + 1 : doc.count();
    const int lo = below >= 0 ? doc.at(below).depth : (zoom >= 0 ? doc.at(zoom).depth + 1 : 0);
    return DropGap{below >= 0 ? below : end, lo, std::max(lo, above >= 0 ? doc.at(above).depth + 1 : lo)};
}

std::optional<PageView::DropTarget> PageView::dropAt(at::Point p, float textX,
                                                     const std::function<bool(BlockPlace)> &takes) const
{
    const std::optional<DropGap> g = takes ? dropGapAt(p) : std::nullopt;
    if (!g)
        return std::nullopt;
    // Its levels nearest the moved blocks' text column first (the shallower of two as near).
    const int wanted = depthAtX(textX);
    std::vector<int> levels;
    for (int depth = g->lo; depth <= g->hi; ++depth)
        levels.push_back(depth);
    std::stable_sort(levels.begin(), levels.end(),
                     [wanted](int a, int b) { return std::abs(a - wanted) < std::abs(b - wanted); });
    for (const int depth : levels) {
        if (takes({g->gap, depth}))
            return DropTarget{{g->gap, depth}, {}};
    }
    return std::nullopt;
}

std::optional<PageView::DropTarget> PageView::dropTarget() const
{
    if (m_drag.kind == DragKind::Move && m_drag.active)
        return m_drag.drop;
    return m_incomingDrop;
}

void PageView::setIncomingDrop(std::optional<DropTarget> drop, at::Context<PageView> &cx)
{
    const bool same = drop.has_value() == m_incomingDrop.has_value() && (!drop || drop->place == m_incomingDrop->place);
    m_incomingDrop = drop;
    if (!same)
        cx.notify();
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
    if (kind == DragKind::Move && active)
        dropMoved(drop, cancelled, cx);
    cx.notify();
}

void PageView::dropMoved(const std::optional<DropTarget> &drop, bool cancelled, at::Context<PageView> &cx)
{
    if (!cancelled && drop && m_controller && m_controller->moveSelectionTo(drop->place))
        revealRow(m_controller->selectedRow(), cx);
    // (Embedded: let go over another page, the view around moves them there.)
    if (m_embedding && m_embedding->endBlockDrag)
        m_embedding->endBlockDrag(!cancelled && !drop);
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

bool PageView::followDrag(const at::PlatformInput &input, at::Context<PageView> &c)
{
    const auto *move = std::get_if<at::MouseMoveEvent>(&input);
    const auto *up = std::get_if<at::MouseUpEvent>(&input);
    if (!move && !up)
        return false;
    // A mouse's drag selection that dragOver began: followed from here on.
    if (m_drag.kind == DragKind::None && m_dragSelected) {
        m_drag.kind = DragKind::Select;
        m_drag.active = true;
        m_drag.pointer = at::PointerKind::Mouse;
    }
    if (m_drag.kind == DragKind::None)
        return false;
    return move ? followMove(*move, c) : followRelease(*up, c);
}

bool PageView::followMove(const at::MouseMoveEvent &move, at::Context<PageView> &c)
{
    // Only the dragging pointer's moves (not a hover Qt Quick makes where a finger is).
    if (!followsPointer(move.pointer))
        return false;
    // A mouse released where the window did not see it: the drag is over.
    if (move.pointer == at::PointerKind::Mouse && move.pressedButton != at::MouseButton::Left) {
        dragReleased(true, c);
        return false;
    }
    dragMoved(move.position, c);
    return m_drag.active;
}

bool PageView::followRelease(const at::MouseUpEvent &up, at::Context<PageView> &c)
{
    const bool stop = m_drag.active;
    // (Where it was let go counts: a last stretch no move showed.)
    if (m_drag.active && !up.cancelled && !(up.position == m_drag.last)) {
        m_drag.last = up.position;
        dragUpdate(c);
    }
    dragReleased(up.cancelled, c);
    return stop;
}

bool PageView::dragging() const
{
    return m_drag.kind != DragKind::None || m_dragSelected;
}

at::AnyElement PageView::dragLayer()
{
    // Nothing between drags (no listener, nothing painted).
    if (!dragging() && !m_incomingDrop)
        return at::div().absolute().size(0);
    PageView *self = this;
    const at::EntityId id = m_selfId;
    // Embedded, the view around follows the drag (it goes on while this page is scrolled away).
    const bool listens = !m_embedding;
    return at::canvas([self, id, listens](at::Rect, at::Window &window, at::App &) {
               // The drop line where the moved blocks would go — this page's own, or blocks coming
               // from another page of the view around.
               if (self->m_drag.kind == DragKind::Move && self->m_drag.active && self->m_drag.drop)
                   self->paintDropLine(*self->m_drag.drop, window);
               else if (self->m_incomingDrop)
                   self->paintDropLine(*self->m_incomingDrop, window);
               if (listens)
                   listenToDrag(window, id);
           })
            .absolute()
            .size(0);
}

void PageView::listenToDrag(at::Window &window, at::EntityId page)
{
    // The drag's moves and its release before anything under it sees them.
    window.onMouseEvent([page](const at::PlatformInput &input, at::DispatchPhase phase, at::Window &w, at::App &app) {
        if (phase != at::DispatchPhase::Capture)
            return;
        bool stop = false;
        app.updateEntity<PageView>(page, [&](PageView &v, at::Context<PageView> &c) { stop = v.followDrag(input, c); });
        if (stop)
            w.stopPropagation();
    });
}

} // namespace brulion::editor::atr
