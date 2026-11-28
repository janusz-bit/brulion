#pragma once

// A list of rows of different heights (docs/ATRAMENT.md "The block editor in Atrament (M2)"): a
// page's blocks, chat messages, search results with snippets. Virtual like uniformList — only the
// rows in view exist in a frame — but each row is as tall as it lays out at the list's width.
//
//   RowHeights heights;          // kept by the view, across frames
//   heights.reset(blocks.size(), 28);
//   list(u"blocks", heights, [&](std::size_t i, Window &, App &) -> AnyElement {
//       return div().id(ElementId::number(blocks[i].uid)).px(12).py(4).child(blocks[i].text);
//   }).flex1()
//
// The heights live outside the frame, behind the RowExtents interface: how many rows, where each
// starts, the row at an offset. A row not laid out yet has an estimate; the list lays out the rows
// it shows and tells their heights back (setMeasured), so the sums become exact where the user
// has been. RowHeights is the ready implementation (a Fenwick tree); the editor's adapter goes over
// its own row model. A row whose height is measured as 0 is hidden (a folded block): the list never
// shows it, and the row at an offset is never one.
//
// The list scrolls by an anchor — a row and an offset into it — not by a pixel offset: a row above
// the view measured for the first time, or a picture's height arriving, changes the rows'
// coordinates but never what is on screen. A ListHandle (trackScroll) shares the anchor and the
// scroll state with the view: scrollToRow(), and the rows inserted or removed in front of the
// anchor so it follows its row. A list scrolled to its end stays there (its last row at the
// bottom) while rows are measured or added. Touch scrolling and the fling, the focused row kept in the frame,
// the accessibility list, the wheel and the scroll bar are uniformList's (list.h).
//
// A header and a footer (`header(render)`, `footer(render)`: a page's title above its blocks, the
// room under the last one) scroll with the rows: laid out at the list's width in every frame, they
// are no rows — no index, no list item for screen readers — and the rows' offsets do not count
// them (the extents know only rows). A list at its very top stays there while the header grows.

#include "base/function.h"
#include "ui/div.h"
#include "ui/element.h"
#include "ui/list.h"
#include "ui/style.h"
#include "ui/window.h"

#include <cstddef>
#include <algorithm>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <vector>

namespace brulion::atrament {

// The heights of a list's rows, kept outside the frame.
class RowExtents {
public:
    virtual ~RowExtents() = default;
    virtual std::size_t count() const = 0;
    // Where `row` starts: the sum of the heights before it. offsetOf(count()) is the total.
    virtual float offsetOf(std::size_t row) const = 0;
    virtual float heightOf(std::size_t row) const = 0; // measured, or the estimate
    virtual bool isMeasured(std::size_t row) const = 0;
    // The row whose span [offset, offset + height) holds `y`, never one of height 0; above the
    // first such row, the first, at or past the end, the last; count() when no row has a height.
    virtual std::size_t rowAt(float y) const = 0;
    // The list laid `row` out at its width: it is `height` tall.
    virtual void setMeasured(std::size_t row, float height) = 0;
    // The width changed: every height is an estimate again until its row is laid out.
    virtual void markAllEstimated() = 0;

    float total() const { return offsetOf(count()); }
    bool isHidden(std::size_t row) const { return isMeasured(row) && heightOf(row) <= 0; }
};

// RowExtents over a Fenwick tree: offsets and rowAt in O(log n), a height set in O(log n),
// inserting and removing rows O(n). Heights are kept in 1/64 px so sums never drift.
class RowHeights final : public RowExtents {
public:
    RowHeights() = default;
    RowHeights(std::size_t count, float estimate) { reset(count, estimate); }

    std::size_t count() const override { return m_heights.size(); }
    float offsetOf(std::size_t row) const override;
    float heightOf(std::size_t row) const override;
    bool isMeasured(std::size_t row) const override;
    std::size_t rowAt(float y) const override;
    void setMeasured(std::size_t row, float height) override { setHeight(row, height, true); }
    void markAllEstimated() override;

    // `count` rows estimated at `estimate`, nothing else.
    void reset(std::size_t count, float estimate);
    void setHeight(std::size_t row, float height, bool measured = true);
    void setEstimated(std::size_t row); // its content changed: the last height is the estimate
    void hide(std::size_t row) { setHeight(row, 0, true); }
    void insert(std::size_t row, std::size_t n, float estimate); // before `row` (clamped)
    void remove(std::size_t row, std::size_t n);
    std::size_t measuredCount() const { return m_measuredCount; }

private:
    static std::int64_t toFixed(float px);
    std::int64_t prefix(std::size_t n) const; // the first n rows' sum
    void add(std::size_t row, std::int64_t delta);
    void rebuild();

    std::vector<std::int64_t> m_heights;
    std::vector<std::uint8_t> m_measured;
    std::vector<std::int64_t> m_tree; // 1-based
    std::size_t m_measuredCount = 0;
    std::size_t m_topBit = 0;
};

enum class ListPosition : std::uint8_t {
    Start, // the row's top at the view's top
    Center,
    End,     // the row's bottom at the view's bottom
    Nearest, // the least scroll that shows it (none when it is in view)
};

// A part of a row: from `top` px below the row's top, `height` px tall (a block inside a journal
// day's section).
struct RowPart {
    float top = 0;
    float height = 0;
};
// Where the part is, asked once the list laid the row out in the frame that scrolls to it (nothing:
// the whole row).
using RowPartResolver = std::function<std::optional<RowPart>(Window &window)>;

// The scroll position of a list() shared with the view that owns it.
class ListHandle {
public:
    struct State {
        ScrollState scroll;
        std::size_t anchorRow = 0;
        float anchorInto = 0; // from the anchor row's top to the view's top (may be < 0)
        float lastY = 0;      // the y the last frame drew
        float width = -1;     // the width the rows were measured at
        bool atEnd = false;   // scrolled to the end: the last row stays at the bottom
        bool atTop = true;    // scrolled to the very top (the header's): it stays there
        float leading = 0;    // the header's height (above row 0) and the footer's, last laid out
        float trailing = 0;
        std::optional<std::size_t> focusedRow;
        std::optional<std::size_t> scrollTo;
        ListPosition scrollToPosition = ListPosition::Start;
        RowPartResolver scrollToPart; // a part of the row (scrollToRowPart), else the whole row
        float scrollToAbove = 0;      // air kept above and below it
        float scrollToBelow = 0;
        // The view kept on a part of a row in the next frame (anchorOnPart).
        std::size_t partRow = 0;
        RowPartResolver part;
        float partInto = 0;
    };

    ListHandle() : m_state(std::make_shared<State>()) { }
    // The next frame shows `row` there.
    void scrollToRow(std::size_t row, ListPosition position = ListPosition::Start)
    {
        m_state->scrollTo = row;
        m_state->scrollToPosition = position;
        m_state->scrollToPart = nullptr;
        m_state->scrollToAbove = 0;
        m_state->scrollToBelow = 0;
    }
    // The next frame shows a part of `row` there: [top, top + height) of it as `part` gives it once
    // the row is laid out in that frame (so a part that just grew is shown by its real size), with
    // `above` / `below` px of air at the view's edges. `part` giving nothing: the whole row.
    void scrollToRowPart(std::size_t row, RowPartResolver part, ListPosition position, float above = 0,
                         float below = 0)
    {
        m_state->scrollTo = row;
        m_state->scrollToPosition = position;
        m_state->scrollToPart = std::move(part);
        m_state->scrollToAbove = std::max(0.0f, above);
        m_state->scrollToBelow = std::max(0.0f, below);
    }
    // The next frame keeps the view on a part of `row` (a block inside a journal day's section): once
    // the list laid the row out, `part` says where the part starts in it and the view's top goes
    // `into` px below that (a scroll since added) — so content of the row above the part laid out
    // anew (a long day's blocks measured as they come near) moves nothing on screen. For one frame:
    // the owner gives it again after each; a jump (scrollToRow, scrollToTop, scrollToAnchor), the
    // list at its top or its end, rows inserted or removed drop it.
    void anchorOnPart(std::size_t row, RowPartResolver part, float into)
    {
        m_state->partRow = row;
        m_state->part = std::move(part);
        m_state->partInto = into;
    }
    // The next frame shows the list's very top, its header too (a page opened anew).
    void scrollToTop() { jumpTo(0, 0, true); }
    // The next frame puts the view's top `into` px below the top of `row` (< 0: above it): a scroll
    // by rows, which no estimate of the rows around changes (scrollBy clamps to the last frame's
    // estimated height).
    void scrollToAnchor(std::size_t row, float into) { jumpTo(row, into, false); }
    std::size_t anchorRow() const { return m_state->anchorRow; }
    float anchorInto() const { return m_state->anchorInto; }
    const ScrollState &scroll() const { return m_state->scroll; }
    // Scrolls by `dy` px (clamped to the content as the last frame measured it); the list takes
    // the new offset in its next frame, as after the wheel (refresh the window).
    bool scrollBy(float dy) { return m_state->scroll.scrollBy({0, dy}); }
    // How far the list was scrolled since its last frame (it takes that in its next one).
    float scrolledSinceFrame() const { return m_state->scroll.offset.y - m_state->lastY; }
    // The rows changed in front of the anchor: it follows its row.
    void rowsInserted(std::size_t first, std::size_t n);
    void rowsRemoved(std::size_t first, std::size_t n);
    std::shared_ptr<State> shared() const { return m_state; }

private:
    void jumpTo(std::size_t row, float into, bool top)
    {
        m_state->part = nullptr;
        m_state->scrollTo.reset();
        m_state->atEnd = false;
        m_state->atTop = top;
        m_state->anchorRow = row;
        m_state->anchorInto = into;
    }
    std::shared_ptr<State> m_state;
};

class MeasuredList final : public Element, public Styled<MeasuredList> {
public:
    using RenderItem = Fn<AnyElement(std::size_t index, Window &, App &)>;
    using RenderEdge = Fn<AnyElement(Window &, App &)>;

    MeasuredList(ElementId id, RowExtents &extents, RenderItem render);

    Style &styleRef() { return m_style; }
    MeasuredList &&trackScroll(const ListHandle &handle) &&
    {
        m_handle = handle.shared();
        return std::move(*this);
    }
    MeasuredList &&ariaLabel(std::u16string_view name) &&;
    // Above the first row / below the last, scrolling with the rows (see above).
    MeasuredList &&header(RenderEdge render) &&
    {
        m_renderHeader = render;
        return std::move(*this);
    }
    MeasuredList &&footer(RenderEdge render) &&
    {
        m_renderFooter = render;
        return std::move(*this);
    }

    NodeId requestLayout(Window &window, App &app) override;
    void prepaint(Rect bounds, Window &window, App &app) override;
    void paint(Rect bounds, Window &window, App &app) override;
    ElementId id() const override { return m_id; }

    // The rows of the last prepaint (for tests): the first shown and how many.
    std::size_t firstVisible() const { return m_indices.empty() ? 0 : m_indices.front(); }
    std::size_t visibleCount() const { return m_items.size(); }

private:
    // Lays out the rows from the one at `y` down to the view's bottom; their heights go to the
    // extents.
    void layoutRows(float y, float viewport, Rect inner, Window &window, App &app);
    // Row `row` rendered and laid out at `width`; its height told to the extents.
    AnyElement measureRow(std::size_t row, float width, Window &window, App &app);
    // The anchor on the part of a row the owner keeps the view on (anchorOnPart), the scroll since
    // the last frame added; whether there was one.
    bool anchorOnPart(ListHandle::State &st, float scrolled, float width, Window &window, App &app);
    // A scroll since the last frame: the anchor at the new pixel offset.
    void anchorAtOffset(ListHandle::State &st, float oldLead, float viewport);
    // The parts of a frame's prepaint (its state, the inner bounds, their height, the rows).
    struct Frame {
        ListHandle::State *st;
        Rect inner;
        float viewport;
        std::size_t n;
        Window *window;
        App *app;
    };
    float measureEdge(const RenderEdge &render, AnyElement &into, std::uint32_t sibling, float width, Window &window,
                      App &app);
    void placeEdges(const Frame &f, float y, float rowsHeight);
    void prepaintEmpty(const Frame &f);
    void prepaintRows(const Frame &f, float oldLead);
    void fitAnchor(const Frame &f);
    void followScroll(const Frame &f, float oldLead);
    std::optional<std::size_t> keptRow(const Frame &f) const;
    float clampedY(const Frame &f) const;
    // A row asked for (scrollToRow, scrollToRowPart): the anchor that shows it there.
    void anchorForScrollTo(const Frame &f);
    void anchorOn(const Frame &f, std::size_t row, ListPosition position, float into, float h, float y);
    void measureFromEnd(const Frame &f);
    float layoutSettled(const Frame &f);
    void prepaintShown(const Frame &f, float y, std::optional<std::size_t> kept, AnyElement keptRow);

    ElementId m_id;
    RowExtents *m_extents;
    RenderItem m_render;
    Style m_style;
    std::shared_ptr<ListHandle::State> m_handle;
    ArenaU16String m_label;

    NodeId m_layout = kNoNode;
    HitboxId m_hitbox = 0;
    ListHandle::State *m_state = nullptr;
    ArenaVector<AnyElement> m_items;
    ArenaVector<std::size_t> m_indices;
    ArenaVector<float> m_tops; // each row's top in the rows' coordinates
    AnyElement m_kept;
    std::size_t m_keptIndex = 0;
    RenderEdge m_renderHeader;
    RenderEdge m_renderFooter;
    AnyElement m_header;
    AnyElement m_footer;
};

inline MeasuredList list(ElementId id, RowExtents &extents, MeasuredList::RenderItem render)
{
    return MeasuredList(id, extents, render);
}

} // namespace brulion::atrament
