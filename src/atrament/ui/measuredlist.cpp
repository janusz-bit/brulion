#include "ui/measuredlist.h"

#include <algorithm>
#include <cmath>

namespace brulion::atrament {

// --- RowHeights ---------------------------------------------------------------------------------

namespace {
constexpr float kScale = 64;
constexpr float kMinRowHeight = 1; // a row the list lays out is never hidden by its layout
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
    m_items.clear();
    m_indices.clear();
    m_tops.clear();
    const std::size_t n = ex.count();
    const std::size_t first = ex.rowAt(y);
    for (std::size_t i = first; i < n; ++i) {
        // Measuring row i changes the offsets after it, never its own.
        const float top = ex.offsetOf(i);
        if (top >= y + viewport && !m_items.empty())
            break;
        if (ex.isHidden(i))
            continue;
        m_items.push_back(measureRow(i, inner.width(), window, app));
        m_indices.push_back(i);
        m_tops.push_back(top);
    }
}

void MeasuredList::prepaint(Rect bounds, Window &window, App &app)
{
    m_hitbox = window.insertHitbox(bounds);
    ListHandle::State &st = m_handle ? *m_handle : window.elementState<ListHandle::State>();
    m_state = &st;
    RowExtents &ex = *m_extents;
    const Rect inner = bounds.inset(m_style.layout.padding).inset(m_style.layout.border);
    const float viewport = inner.height();
    const std::size_t n = ex.count();
    m_items.clear();
    m_indices.clear();
    m_tops.clear();
    m_kept = AnyElement();
    st.scroll.viewport = inner.size;
    if (!m_render || n == 0) {
        st.scroll.content = {};
        st.scroll.scrollBy({});
        st.anchorRow = 0;
        st.anchorInto = 0;
        st.lastY = 0;
        window.pushScrollFrame(st.scroll, inner, false, true, m_hitbox);
        window.popScrollFrame();
        return;
    }
    if (st.width != inner.width()) {
        if (st.width >= 0)
            ex.markAllEstimated();
        st.width = inner.width();
    }
    if (st.anchorRow >= n) {
        st.anchorRow = n - 1;
        st.anchorInto = 0;
    }

    // The list's text style around its rows from their first layout on: they are measured with it.
    const bool pushText = !m_style.text.isEmpty();
    if (pushText)
        window.pushTextStyle(m_style.text);

    auto clampedY = [&] {
        const float maxY = std::max(0.0f, ex.total() - viewport);
        return st.atEnd ? maxY : std::clamp(ex.offsetOf(st.anchorRow) + st.anchorInto, 0.0f, maxY);
    };
    auto measure = [&](std::size_t row) { return measureRow(row, inner.width(), window, app); };

    // Someone scrolled since the last frame (the wheel, a finger, a fling, a caret revealed, a
    // screen reader): the anchor moves to the pixel offset — on the first measured row in view if
    // there is one (the rows shown last frame are), so rows measured above it later move nothing.
    if (std::abs(st.scroll.offset.y - st.lastY) > 0.01f) {
        const float py = st.scroll.offset.y;
        st.atEnd = py > 0 && py >= st.scroll.maxOffset().y - 0.5f;
        std::size_t row = ex.rowAt(py);
        if (row >= n) {
            row = 0;
        } else {
            for (std::size_t k = row; k < n && ex.offsetOf(k) < py + viewport; ++k) {
                if (ex.isMeasured(k) && !ex.isHidden(k)) {
                    row = k;
                    break;
                }
            }
        }
        st.anchorRow = row;
        st.anchorInto = py - ex.offsetOf(row);
    }

    if (st.scrollTo) {
        const std::size_t row = std::min(*st.scrollTo, n - 1);
        const ListPosition position = st.scrollToPosition;
        st.scrollTo.reset();
        st.atEnd = false;
        const float y = clampedY();
        const float h = ex.isHidden(row) ? 0.0f : (measure(row), ex.heightOf(row));
        const float top = ex.offsetOf(row);
        switch (position) {
        case ListPosition::Start:
            st.anchorRow = row;
            st.anchorInto = 0;
            break;
        case ListPosition::Center:
            st.anchorRow = row;
            st.anchorInto = (h - viewport) / 2;
            break;
        case ListPosition::End:
            st.anchorRow = row;
            st.anchorInto = h - viewport;
            break;
        case ListPosition::Nearest:
            if (top < y || h > viewport) {
                st.anchorRow = row;
                st.anchorInto = 0;
            } else if (top + h > y + viewport) {
                st.anchorRow = row;
                st.anchorInto = h - viewport;
            }
            break;
        }
    }

    // The focused row out of view stays in the frame: measured first, so it moves no row in view.
    const std::optional<std::size_t> kept =
            st.focusedRow && window.focusedId() != 0 && *st.focusedRow < n && !ex.isHidden(*st.focusedRow)
                    ? st.focusedRow
                    : std::nullopt;
    AnyElement keptRow;
    if (kept)
        keptRow = measure(*kept);
    // At the end: the rows that fill the view from the bottom up are measured first, so the last
    // row ends at the bottom however wrong the estimates were.
    if (st.atEnd) {
        float filled = 0;
        for (std::size_t i = n; i-- > 0 && filled < viewport;) {
            if (ex.isHidden(i))
                continue;
            measure(i);
            filled += ex.heightOf(i);
        }
    }

    // Rows measured at the end can make the page shorter than the view's place in it: laid out
    // again at the clamped y (a few passes at most — each one measures the rows it shows).
    float y = clampedY();
    for (int pass = 0; pass < 4; ++pass) {
        layoutRows(y, viewport, inner, window, app);
        const float again = clampedY();
        if (std::abs(again - y) < 0.5f)
            break;
        y = again;
    }
    // The anchor is the row at the view's top now, measured.
    st.anchorRow = std::min(ex.rowAt(y), n - 1);
    st.anchorInto = y - ex.offsetOf(st.anchorRow);
    st.lastY = y;
    st.scroll.content = {inner.width(), ex.total()};
    st.scroll.offset = {0, y};

    window.pushContentMask(inner);
    window.pushScrollFrame(st.scroll, inner, false, true, m_hitbox);
    for (std::size_t k = 0; k < m_items.size(); ++k)
        m_items[k].prepaintAt({inner.left(), inner.top() + m_tops[k] - y}, window, app);
    if (kept && std::find(m_indices.begin(), m_indices.end(), *kept) == m_indices.end()) {
        m_keptIndex = *kept;
        m_kept = std::move(keptRow);
        m_kept.prepaintAt({inner.left(), inner.top() + ex.offsetOf(m_keptIndex) - y}, window, app);
    }
    window.popScrollFrame();
    window.popContentMask();
    if (pushText)
        window.popTextStyle();
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
                           m_state ? &m_state->focusedRow : nullptr},
                          window, app);
}

} // namespace brulion::atrament
