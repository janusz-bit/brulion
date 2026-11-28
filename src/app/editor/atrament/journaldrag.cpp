// JournalView's drags across days (journalview.h; docs/ATRAMENT.md "The journal stream as one
// list"): a drag that leaves the day it began in goes on in the stream — the selection into the
// days around (StreamSelection.extendFrom), blocks moved into another day (its drop line shown
// there, the Markdown moved between the two days' files) —, its edge scrolling scrolls the stream
// and the days that scroll in take it (the pointer's day and block where the days are now), and
// its moves and release are followed by the stream while the day it began in is scrolled away
// (PageView::followDrag). A move into another day is two undo steps, one per file — the blocks
// inserted into the day they went to, deleted from the one they left — taken back (and done again)
// together by Ctrl+Z (Ctrl+Y) in either day while they are still those days' last steps.
#include "journalview.h"

#include "blocklistmodel.h"
#include "pagecontroller.h"
#include "streamselection.h"

#include <algorithm>
#include <cmath>
#include <utility>

namespace brulion::editor::atr {

namespace at = brulion::atrament;

namespace {

// Whether every block selected is a list item (only those go below another day's top level).
bool onlyListItems(const PageController &c)
{
    const markdown::BlockDocument &doc = c.blocks()->document();
    const QVariantList rows = c.selectedRows();
    return std::ranges::all_of(rows, [&](const QVariant &r) {
        return doc.at(r.toInt()).kind == BlockKind::ListItem;
    });
}

} // namespace


std::optional<at::Rect> JournalView::viewport() const
{
    return m_viewport;
}

float JournalView::scrolledSinceFrame() const
{
    return m_handle.scrolledSinceFrame();
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
    const int first = v.firstBlockRow();
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

bool JournalView::dragBlocksTo(PageController *from, at::Point p, float textX)
{
    const int source = dayOfController(from);
    const float y = pointerY(p);
    const int day = dayUnder(y);
    if (source < 0 || day < 0 || day == source) {
        showCrossDrop(-1, std::nullopt, source);
        return false;
    }
    showCrossDrop(day, dropInto(day, *from, {p.x, y - scrolledSinceFrame()}, textX), source);
    return true;
}

std::optional<PageView::DropTarget> JournalView::dropInto(int day, const PageController &from, at::Point p,
                                                          float textX) const
{
    // Where the blocks would go in that day: a place it takes them at (a paragraph, a heading…
    // only at the top level), its level nearest their text column.
    PageController *target = controller(day);
    const at::Entity<PageView> *targetPage = page(day);
    if (!target || !targetPage || !target->loaded() || target->readOnly())
        return std::nullopt;
    const bool listItems = onlyListItems(from);
    return targetPage->read(*m_app).dropAt(p, textX, [target, listItems](BlockPlace place) {
        return (listItems || place.depth == 0) && target->canInsertBlocksAt(place);
    });
}

void JournalView::showCrossDrop(int day, std::optional<PageView::DropTarget> drop, int source)
{
    m_crossDrop = drop ? std::optional<CrossDrop>(CrossDrop{day, drop->place}) : std::nullopt;
    // (Not the day the drag is in: it shows its own drop line.)
    for (int d : madeDays()) {
        if (d != source)
            page(d)->update(*m_app, [&](PageView &v, at::Context<PageView> &pc) {
                v.setIncomingDrop(d == day ? drop : std::nullopt, pc);
            });
    }
}

void JournalView::endBlockDrag(PageController *from, bool drop)
{
    const std::optional<CrossDrop> target = m_crossDrop;
    showCrossDrop(-1, std::nullopt, dayOfController(from));
    if (drop && target)
        moveAcross(from, controller(target->day), target->place);
}

bool JournalView::moveAcross(PageController *from, PageController *to, BlockPlace place)
{
    if (!from || !to || from == to)
        return false;
    const QString markdown = from->selectionMarkdown();
    // Into the day they go to first (nothing is lost when it refuses them), then out of the one they
    // leave; they stay selected where they went.
    const markdown::RowRange rows = to->insertBlocksAt(place, markdown);
    if (markdown.isEmpty() || rows.count <= 0)
        return false;
    from->deleteSelection();
    from->clearSelection();
    to->selectRange(rows.first, rows.last());
    m_lastMove = MoveAcross{from, to, from->undoToken(), to->undoToken(), false};
    return true;
}

bool JournalView::MoveAcross::stands(PageController *in, bool redo) const
{
    // Its days still there, `in` one of them, both still at the move (or at its undo, to redo it).
    return from && to && (in == from || in == to) && undone == redo && from->undoToken() == fromToken
           && to->undoToken() == toToken;
}

bool JournalView::undoMoveAcross(PageController *in, bool redo)
{
    MoveAcross &m = m_lastMove;
    if (!m.stands(in, redo))
        return false;
    if (redo) {
        m.to->redo();
        m.from->redo();
    } else if (!m.to->undoStep(m.toToken) || !m.from->undoStep(m.fromToken)) {
        return false;
    }
    m.undone = !redo;
    m.fromToken = m.from->undoToken();
    m.toToken = m.to->undoToken();
    return true;
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
