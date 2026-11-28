#include "ui/measuredlist.h"

#include <algorithm>
#include <cmath>

namespace brulion::atrament {

// --- RowHeights ---------------------------------------------------------------------------------

namespace {
constexpr float kScale = 64;
constexpr float kMinRowHeight = 1; // a row the list lays out is never hidden by its layout
// The header's and the footer's place among the list's children (rows are known by their index).
constexpr std::uint32_t kHeaderSibling = 0xfffffffeu;
constexpr std::uint32_t kFooterSibling = 0xffffffffu;
} // namespace

std::int64_t RowHeights::toFixed(float px)
{
    if (!(px > 0)) // negative or NaN
        return 0;
    return std::int64_t(std::llround(double(px) * kScale));
}

std::int64_t RowHeights::prefix(std::size_t n) const
{
    std::int64_t sum = 0;
    for (std::size_t i = std::min(n, m_heights.size()); i > 0; i &= i - 1)
        sum += m_tree[i];
    return sum;
}

void RowHeights::add(std::size_t row, std::int64_t delta)
{
    for (std::size_t i = row + 1; i < m_tree.size(); i += i & (~i + 1))
        m_tree[i] += delta;
}

void RowHeights::rebuild()
{
    const std::size_t n = m_heights.size();
    m_tree.assign(n + 1, 0);
    for (std::size_t i = 1; i <= n; ++i) {
        m_tree[i] += m_heights[i - 1];
        const std::size_t j = i + (i & (~i + 1));
        if (j <= n)
            m_tree[j] += m_tree[i];
    }
    m_topBit = 1;
    while (m_topBit * 2 <= n)
        m_topBit *= 2;
    m_measuredCount = std::size_t(std::count(m_measured.begin(), m_measured.end(), std::uint8_t(1)));
}

float RowHeights::offsetOf(std::size_t row) const
{
    return float(double(prefix(row)) / kScale);
}

float RowHeights::heightOf(std::size_t row) const
{
    return row < m_heights.size() ? float(double(m_heights[row]) / kScale) : 0.0f;
}

bool RowHeights::isMeasured(std::size_t row) const
{
    return row < m_measured.size() && m_measured[row];
}

std::size_t RowHeights::rowAt(float y) const
{
    const std::size_t n = m_heights.size();
    const std::int64_t total = prefix(n);
    if (n == 0 || total == 0)
        return n;
    std::int64_t rem = std::clamp(toFixed(y), std::int64_t(0), total - 1);
    // The largest pos whose prefix(pos) ≤ y: row pos holds y (rows of height 0 there come before it).
    std::size_t pos = 0;
    for (std::size_t bit = m_topBit; bit; bit >>= 1) {
        if (pos + bit <= n && m_tree[pos + bit] <= rem) {
            pos += bit;
            rem -= m_tree[pos];
        }
    }
    return std::min(pos, n - 1);
}

void RowHeights::reset(std::size_t count, float estimate)
{
    m_heights.assign(count, toFixed(estimate));
    m_measured.assign(count, 0);
    rebuild();
}

void RowHeights::setHeight(std::size_t row, float height, bool measured)
{
    if (row >= m_heights.size())
        return;
    const std::int64_t h = toFixed(height);
    if (h != m_heights[row]) {
        add(row, h - m_heights[row]);
        m_heights[row] = h;
    }
    const std::uint8_t m = measured ? 1 : 0;
    if (m != m_measured[row]) {
        m_measuredCount += m ? 1 : std::size_t(-1);
        m_measured[row] = m;
    }
}

void RowHeights::setEstimated(std::size_t row)
{
    if (row < m_measured.size() && m_measured[row]) {
        m_measured[row] = 0;
        --m_measuredCount;
    }
}

void RowHeights::markAllEstimated()
{
    // Hidden rows stay hidden: their 0 is no estimate.
    for (std::size_t i = 0; i < m_heights.size(); ++i)
        if (m_heights[i] > 0)
            m_measured[i] = 0;
    m_measuredCount = std::size_t(std::count(m_measured.begin(), m_measured.end(), std::uint8_t(1)));
}

void RowHeights::insert(std::size_t row, std::size_t n, float estimate)
{
    row = std::min(row, m_heights.size());
    m_heights.insert(m_heights.begin() + std::ptrdiff_t(row), n, toFixed(estimate));
    m_measured.insert(m_measured.begin() + std::ptrdiff_t(row), n, std::uint8_t(0));
    rebuild();
}

void RowHeights::remove(std::size_t row, std::size_t n)
{
    row = std::min(row, m_heights.size());
    n = std::min(n, m_heights.size() - row);
    m_heights.erase(m_heights.begin() + std::ptrdiff_t(row), m_heights.begin() + std::ptrdiff_t(row + n));
    m_measured.erase(m_measured.begin() + std::ptrdiff_t(row), m_measured.begin() + std::ptrdiff_t(row + n));
    rebuild();
}

// --- ListHandle ---------------------------------------------------------------------------------

void ListHandle::rowsInserted(std::size_t first, std::size_t n)
{
    State &s = *m_state;
    // The top is sticky: a view at its very top shows rows inserted there.
    const bool atTop = s.anchorRow == 0 && s.anchorInto <= 0;
    if (first < s.anchorRow || (first == s.anchorRow && !atTop))
        s.anchorRow += n;
    if (s.focusedRow && first <= *s.focusedRow)
        *s.focusedRow += n;
    s.part = nullptr; // (its row may have moved: the owner gives it again)
}

void ListHandle::rowsRemoved(std::size_t first, std::size_t n)
{
    State &s = *m_state;
    if (s.anchorRow >= first + n) {
        s.anchorRow -= n;
    } else if (s.anchorRow >= first) {
        // The anchor's row went: the row that took its place starts the view.
        s.anchorRow = first;
        s.anchorInto = 0;
    }
    if (s.focusedRow) {
        if (*s.focusedRow >= first + n)
            *s.focusedRow -= n;
        else if (*s.focusedRow >= first)
            s.focusedRow.reset();
    }
    s.part = nullptr;
}

// --- MeasuredList -------------------------------------------------------------------------------

MeasuredList::MeasuredList(ElementId id, RowExtents &extents, RenderItem render)
    : m_id(id), m_extents(&extents), m_render(render)
{
    m_style.layout.overflowY = Overflow::Scroll;
}

MeasuredList &&MeasuredList::ariaLabel(std::u16string_view name) &&
{
    m_label.assign(name.begin(), name.end());
    return std::move(*this);
}

NodeId MeasuredList::requestLayout(Window &window, App &)
{
    // Its rows are laid out on their own in prepaint: the list is a leaf sized by its style.
    m_layout = window.requestLayout(m_style.layout, {});
    return m_layout;
}

AnyElement MeasuredList::measureRow(std::size_t row, float width, Window &window, App &app)
{
    AnyElement e = m_render(row, window, app);
    e.setSiblingIndex(std::uint32_t(row));
    const Size size = e.layoutAsRoot(AvailableSpace::definite(width), AvailableSpace::maxContent(), window, app);
    const float h = std::max(kMinRowHeight, size.height);
    if (!m_extents->isMeasured(row) || std::abs(m_extents->heightOf(row) - h) > 0.01f)
        m_extents->setMeasured(row, h);
    return e;
}

void MeasuredList::layoutRows(float y, float viewport, Rect inner, Window &window, App &app)
{
    RowExtents &ex = *m_extents;
    // The rows start below the header: `y` and the tops are the scrolled content's.
    const float lead = m_state ? m_state->leading : 0.0f;
    m_items.clear();
    m_indices.clear();
    m_tops.clear();
    const std::size_t n = ex.count();
    const std::size_t first = ex.rowAt(y - lead);
    for (std::size_t i = first; i < n; ++i) {
        // Measuring row i changes the offsets after it, never its own.
        const float top = lead + ex.offsetOf(i);
        if (top >= y + viewport && !m_items.empty())
            break;
        if (ex.isHidden(i))
            continue;
        m_items.push_back(measureRow(i, inner.width(), window, app));
        m_indices.push_back(i);
        m_tops.push_back(top);
    }
}

bool MeasuredList::anchorOnPart(ListHandle::State &st, float scrolled, float width, Window &window, App &app)
{
    // (One frame's: the owner gives it again after each.)
    const RowPartResolver part = std::move(st.part);
    st.part = nullptr;
    RowExtents &ex = *m_extents;
    if (!part || st.scrollTo || st.atEnd || st.atTop || st.partRow >= ex.count() || ex.isHidden(st.partRow))
        return false;
    measureRow(st.partRow, width, window, app);
    const std::optional<RowPart> p = part(window);
    if (!p)
        return false;
    st.anchorRow = st.partRow;
    st.anchorInto = p->top + st.partInto + scrolled;
    return true;
}

void MeasuredList::anchorAtOffset(ListHandle::State &st, float oldLead, float viewport)
{
    // On the first measured row in view if there is one (the rows shown last frame are), so rows
    // measured above it later move nothing. (The offset is the last frame's: below the header as it
    // was then.)
    RowExtents &ex = *m_extents;
    const std::size_t n = ex.count();
    const float py = st.scroll.offset.y;
    std::size_t row = ex.rowAt(py - oldLead);
    if (row >= n) {
        row = 0;
    } else {
        for (std::size_t k = row; k < n && oldLead + ex.offsetOf(k) < py + viewport; ++k) {
            if (ex.isMeasured(k) && !ex.isHidden(k)) {
                row = k;
                break;
            }
        }
    }
    st.anchorRow = row;
    st.anchorInto = py - oldLead - ex.offsetOf(row);
}

float MeasuredList::measureEdge(const RenderEdge &render, AnyElement &into, std::uint32_t sibling, float width,
                                Window &window, App &app)
{
    if (!render)
        return 0.0f;
    into = render(window, app);
    into.setSiblingIndex(sibling);
    const Size size = into.layoutAsRoot(AvailableSpace::definite(width), AvailableSpace::maxContent(), window, app);
    return std::max(0.0f, size.height);
}

void MeasuredList::placeEdges(const Frame &f, float y, float rowsHeight)
{
    if (!m_header.isEmpty())
        m_header.prepaintAt({f.inner.left(), f.inner.top() - y}, *f.window, *f.app);
    if (!m_footer.isEmpty())
        m_footer.prepaintAt({f.inner.left(), f.inner.top() + f.st->leading + rowsHeight - y}, *f.window, *f.app);
}

void MeasuredList::prepaintEmpty(const Frame &f)
{
    ListHandle::State &st = *f.st;
    const float edges = st.leading + st.trailing;
    st.scroll.content = edges > 0 ? Size{f.inner.width(), edges} : Size{};
    st.scroll.scrollBy({});
    st.anchorRow = 0;
    st.anchorInto = 0;
    st.lastY = st.scroll.offset.y;
    st.atTop = st.lastY <= 0.01f;
    f.window->pushContentMask(f.inner);
    f.window->pushScrollFrame(st.scroll, f.inner, false, true, m_hitbox);
    placeEdges(f, st.lastY, 0);
    f.window->popScrollFrame();
    f.window->popContentMask();
}

float MeasuredList::clampedY(const Frame &f) const
{
    // y is the scrolled content's: the header from 0, the rows from `leading` on.
    const ListHandle::State &st = *f.st;
    const RowExtents &ex = *m_extents;
    const float maxY = std::max(0.0f, st.leading + ex.total() + st.trailing - f.viewport);
    if (st.atEnd)
        return maxY;
    if (st.atTop)
        return 0.0f;
    return std::clamp(st.leading + ex.offsetOf(st.anchorRow) + st.anchorInto, 0.0f, maxY);
}

void MeasuredList::anchorForScrollTo(const Frame &f)
{
    ListHandle::State &st = *f.st;
    RowExtents &ex = *m_extents;
    const std::size_t row = std::min(*st.scrollTo, f.n - 1);
    const ListPosition position = st.scrollToPosition;
    const RowPartResolver part = std::move(st.scrollToPart);
    const float above = st.scrollToAbove;
    const float below = st.scrollToBelow;
    st.scrollTo.reset();
    st.scrollToPart = nullptr;
    st.scrollToAbove = 0;
    st.scrollToBelow = 0;
    st.atEnd = false;
    const float y = clampedY(f);
    st.atTop = false;
    const bool hidden = ex.isHidden(row);
    if (!hidden)
        measureRow(row, f.inner.width(), *f.window, *f.app);
    float h = hidden ? 0.0f : ex.heightOf(row);
    // A part of the row, where the layout just made put it (and the air around it).
    float into = 0;
    if (const std::optional<RowPart> p = part && !hidden ? part(*f.window) : std::nullopt) {
        into = p->top;
        h = std::max(0.0f, p->height);
    }
    anchorOn(f, row, position, into - above, h + above + below, y);
}

void MeasuredList::anchorOn(const Frame &f, std::size_t row, ListPosition position, float into, float h, float y)
{
    ListHandle::State &st = *f.st;
    const RowExtents &ex = *m_extents;
    const float top = st.leading + ex.offsetOf(row) + into;
    // Nearest: the least scroll — none when it is in view (also in the header's part of the list).
    if (position == ListPosition::Nearest && top >= y && h <= f.viewport) {
        if (top + h <= y + f.viewport) {
            st.anchorRow = std::min(ex.rowAt(y - st.leading), f.n - 1);
            st.anchorInto = y - st.leading - ex.offsetOf(st.anchorRow);
            st.atTop = y <= 0.01f;
            return;
        }
        position = ListPosition::End;
    }
    st.anchorRow = row;
    switch (position) {
    case ListPosition::Start:
    case ListPosition::Nearest:
        st.anchorInto = into;
        break;
    case ListPosition::Center:
        st.anchorInto = into + (h - f.viewport) / 2;
        break;
    case ListPosition::End:
        st.anchorInto = into + h - f.viewport;
        break;
    }
}

void MeasuredList::measureFromEnd(const Frame &f)
{
    // At the end: the rows that fill the view from the bottom up are measured first, so the last row
    // ends at the bottom however wrong the estimates were.
    RowExtents &ex = *m_extents;
    float filled = f.st->trailing;
    for (std::size_t i = f.n; i-- > 0 && filled < f.viewport;) {
        if (ex.isHidden(i))
            continue;
        measureRow(i, f.inner.width(), *f.window, *f.app);
        filled += ex.heightOf(i);
    }
}

float MeasuredList::layoutSettled(const Frame &f)
{
    // Rows measured at the end can make the page shorter than the view's place in it: laid out again
    // at the clamped y (a few passes at most — each one measures the rows it shows).
    float y = clampedY(f);
    for (int pass = 0; pass < 4; ++pass) {
        layoutRows(y, f.viewport, f.inner, *f.window, *f.app);
        const float again = clampedY(f);
        if (std::abs(again - y) < 0.5f)
            break;
        y = again;
    }
    return y;
}

void MeasuredList::prepaintShown(const Frame &f, float y, std::optional<std::size_t> kept, AnyElement keptRow)
{
    ListHandle::State &st = *f.st;
    const RowExtents &ex = *m_extents;
    f.window->pushContentMask(f.inner);
    f.window->pushScrollFrame(st.scroll, f.inner, false, true, m_hitbox);
    for (std::size_t k = 0; k < m_items.size(); ++k)
        m_items[k].prepaintAt({f.inner.left(), f.inner.top() + m_tops[k] - y}, *f.window, *f.app);
    if (kept && std::find(m_indices.begin(), m_indices.end(), *kept) == m_indices.end()) {
        m_keptIndex = *kept;
        m_kept = std::move(keptRow);
        m_kept.prepaintAt({f.inner.left(), f.inner.top() + st.leading + ex.offsetOf(m_keptIndex) - y}, *f.window,
                          *f.app);
    }
    placeEdges(f, y, ex.total());
    f.window->popScrollFrame();
    f.window->popContentMask();
}

void MeasuredList::prepaint(Rect bounds, Window &window, App &app)
{
    m_hitbox = window.insertHitbox(bounds);
    ListHandle::State &st = m_handle ? *m_handle : window.elementState<ListHandle::State>();
    m_state = &st;
    RowExtents &ex = *m_extents;
    const Rect inner = bounds.inset(m_style.layout.padding).inset(m_style.layout.border);
    const Frame f{&st, inner, inner.height(), ex.count(), &window, &app};
    m_items.clear();
    m_indices.clear();
    m_tops.clear();
    m_kept = AnyElement();
    m_header = AnyElement();
    m_footer = AnyElement();
    st.scroll.viewport = inner.size;

    // The list's text style around its rows (and its header and footer) from their first layout
    // on: they are measured with it.
    const bool pushText = !m_style.text.isEmpty();
    if (pushText)
        window.pushTextStyle(m_style.text);
    // The header and the footer, laid out at the list's width in every frame.
    const float oldLead = st.leading;
    st.leading = measureEdge(m_renderHeader, m_header, kHeaderSibling, inner.width(), window, app);
    st.trailing = measureEdge(m_renderFooter, m_footer, kFooterSibling, inner.width(), window, app);
    if (!m_render || f.n == 0)
        prepaintEmpty(f);
    else
        prepaintRows(f, oldLead);
    if (pushText)
        window.popTextStyle();
}

void MeasuredList::fitAnchor(const Frame &f)
{
    // A new width: every height an estimate again; the anchor among the rows there are.
    ListHandle::State &st = *f.st;
    if (st.width != f.inner.width()) {
        if (st.width >= 0)
            m_extents->markAllEstimated();
        st.width = f.inner.width();
    }
    if (st.anchorRow >= f.n) {
        st.anchorRow = f.n - 1;
        st.anchorInto = 0;
    }
}

void MeasuredList::followScroll(const Frame &f, float oldLead)
{
    // Someone scrolled since the last frame (the wheel, a finger, a fling, a caret revealed, a
    // screen reader): the anchor moves by as much — on a part of a row (anchorOnPart) where the
    // row's new layout put it, else to the pixel offset.
    ListHandle::State &st = *f.st;
    const float scrolled = st.scroll.offset.y - st.lastY;
    const bool moved = std::abs(scrolled) > 0.01f;
    if (moved) {
        const float py = st.scroll.offset.y;
        st.atEnd = py > 0 && py >= st.scroll.maxOffset().y - 0.5f;
        st.atTop = py <= 0.01f;
    }
    if (!anchorOnPart(st, scrolled, f.inner.width(), *f.window, *f.app) && moved)
        anchorAtOffset(st, oldLead, f.viewport);
}

std::optional<std::size_t> MeasuredList::keptRow(const Frame &f) const
{
    // The focused row out of view stays in the frame: measured first, so it moves no row in view.
    const ListHandle::State &st = *f.st;
    if (!st.focusedRow || f.window->focusedId() == 0 || *st.focusedRow >= f.n || m_extents->isHidden(*st.focusedRow))
        return std::nullopt;
    return st.focusedRow;
}

void MeasuredList::prepaintRows(const Frame &f, float oldLead)
{
    ListHandle::State &st = *f.st;
    RowExtents &ex = *m_extents;
    fitAnchor(f);
    followScroll(f, oldLead);
    if (st.scrollTo)
        anchorForScrollTo(f);
    const std::optional<std::size_t> kept = keptRow(f);
    AnyElement keptRow = kept ? measureRow(*kept, f.inner.width(), *f.window, *f.app) : AnyElement();
    if (st.atEnd)
        measureFromEnd(f);
    const float y = layoutSettled(f);
    // The anchor is the row at the view's top now, measured (above the first row: the first, by a
    // negative offset into it).
    st.anchorRow = std::min(ex.rowAt(y - st.leading), f.n - 1);
    st.anchorInto = y - st.leading - ex.offsetOf(st.anchorRow);
    st.lastY = y;
    st.atTop = y <= 0.01f;
    st.scroll.content = {f.inner.width(), st.leading + ex.total() + st.trailing};
    st.scroll.offset = {0, y};
    prepaintShown(f, y, kept, std::move(keptRow));
}

void MeasuredList::paint(Rect bounds, Window &window, App &app)
{
    detail::paintListRows({bounds,
                           m_style,
                           {m_label.data(), m_label.size()},
                           m_extents->count(),
                           m_state ? &m_state->scroll : nullptr,
                           m_hitbox,
                           std::span<AnyElement>(m_items.data(), m_items.size()),
                           0,
                           m_indices.data(),
                           &m_kept,
                           m_keptIndex,
                           m_state ? &m_state->focusedRow : nullptr,
                           &m_header,
                           &m_footer},
                          window, app);
}

} // namespace brulion::atrament
