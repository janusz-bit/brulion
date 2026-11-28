// JournalView's drags across days (journalview.h; docs/ATRAMENT.md "The journal stream as one
// list"): a drag that leaves the day it began in goes on in the stream — the selection into the
// days around (StreamSelection.extendFrom) —, its edge scrolling scrolls the stream and the days
// that scroll in take it (the pointer's day and block where the days are now), and its moves and
// release are followed by the stream while the day it began in is scrolled away
// (PageView::followDrag).
#include "journalview.h"

#include "blocklistmodel.h"
#include "pagecontroller.h"
#include "streamselection.h"

#include <algorithm>
#include <cmath>
#include <utility>

namespace brulion::editor::atr {

namespace at = brulion::atrament;


std::optional<at::Rect> JournalView::viewport() const
{
    for (const ForeignFrame &f : m_edgeFrames)
        if (f.kind == ForeignFrame::Kind::Header)
            return f.mask;
    return std::nullopt;
}

float JournalView::scrolledSinceFrame() const
{
    const std::shared_ptr<at::ListHandle::State> st = m_handle.shared();
    return st->scroll.offset.y - st->lastY;
}

float JournalView::pointerY(at::Point p) const
{
    // Held into the view (its edge's days when the pointer is past it), where the days are now.
    const std::optional<at::Rect> view = viewport();
    const float y = view ? std::clamp(p.y, view->top() + 1, std::max(view->top() + 1, view->bottom() - 1)) : p.y;
    return y + scrolledSinceFrame();
}

int JournalView::dayUnder(float y) const
{
    // The day drawn there, else the nearest one drawn (the pointer over the stream's header).
    int best = -1;
    float distance = 0;
    for (int r = 0; r < dayCount(); ++r) {
        const std::optional<at::Rect> s = dayBounds(r);
        if (!s)
            continue;
        const float d = y < s->top() ? s->top() - y : (y >= s->bottom() ? y - s->bottom() : 0.0f);
        if (best < 0 || d < distance) {
            best = r;
            distance = d;
        }
    }
    return best;
}

int JournalView::blockUnder(int day, float y) const
{
    // The block at `y` of the day's page; above its blocks the first one (no property), below them
    // the last one.
    PageController *c = controller(day);
    const at::Entity<PageView> *p = page(day);
    if (!c || !p || !c->loaded())
        return -1;
    const PageView &v = p->read(*m_app);
    if (const int row = v.rowAtY(y); row >= 0)
        return row;
    int first = c->nextVisibleRow(-1);
    while (first >= 0 && c->blocks()->index(first, 0).data(BlockListModel::ViewRole).toString() == u"frontmatter")
        first = c->nextVisibleRow(first);
    const std::optional<at::Rect> top = first >= 0 ? v.rowBounds(first) : std::nullopt;
    return top && y < top->top() ? first : c->lastVisibleRow();
}

std::optional<bool> JournalView::selectAcross(PageController *from, int fromRow, at::Point p)
{
    const int source = dayOfController(from);
    const float y = pointerY(p);
    const int day = dayUnder(y);
    if (!m_stream || source < 0 || day < 0 || day == source)
        return std::nullopt;
    PageController *target = controller(day);
    const int row = blockUnder(day, y);
    return target && row >= 0 && m_stream->extendFrom(from, fromRow, target, row);
}

void JournalView::dragAcross(at::Point p)
{
    // A mouse's press on a block dragged straight into another day (before the day's own drag
    // began): the blocks between are selected across the days.
    for (int d : madeDays()) {
        const int pressed = page(d)->read(*m_app).pressedRow();
        if (pressed < 0)
            continue;
        if (selectAcross(controller(d), pressed, p).value_or(false))
            page(d)->update(*m_app, [](PageView &v, at::Context<PageView> &) {
                v.markDragSelected();
            });
        return;
    }
}

bool JournalView::followDrag(const at::PlatformInput &input)
{
    // The day whose drag goes on (its own layer follows none: it may be scrolled away).
    for (int d : madeDays()) {
        const at::Entity<PageView> *p = page(d);
        if (!p->read(*m_app).dragging())
            continue;
        bool stop = false;
        p->update(*m_app, [&](PageView &v, at::Context<PageView> &pc) { stop = v.followDrag(input, pc); });
        return stop;
    }
    return false;
}

} // namespace brulion::editor::atr
