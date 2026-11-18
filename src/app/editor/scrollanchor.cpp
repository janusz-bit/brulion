#include "scrollanchor.h"

#include <algorithm>
#include <cmath>

namespace brulion::editor {

void ScrollAnchor::resetRows(int count, qreal estimate)
{
    m_rows.reset(count, estimate);
    m_pending = false;
    m_row = m_rows.isEmpty() ? -1 : 0;
    m_offset = 0;
}

void ScrollAnchor::resetRows(const QList<qreal> &heights, bool measured)
{
    m_rows.assign(heights, measured);
    m_pending = false;
    m_row = m_rows.isEmpty() ? -1 : 0;
    m_offset = 0;
}

void ScrollAnchor::insertRows(int first, int count, qreal estimate)
{
    if (count <= 0)
        return;
    first = std::clamp(first, 0, m_rows.count());
    m_rows.insert(first, count, estimate);
    rowsInserted(first, count);
}

void ScrollAnchor::insertRows(int first, const QList<qreal> &heights, bool measured)
{
    if (heights.isEmpty())
        return;
    first = std::clamp(first, 0, m_rows.count());
    m_rows.insert(first, heights, measured);
    rowsInserted(first, int(heights.size()));
}

void ScrollAnchor::rowsInserted(int first, int count)
{
    if (m_pending) {
        if (first == m_row) {
            // The rows that take the removed anchor's place (a block removed and inserted again:
            // another device's edit of it): the first of them is the anchor, at its offset.
            m_pending = false;
            m_offset = m_pendingOffset;
        } else if (first < m_row) {
            m_row += count;
        } else {
            m_pending = false;
            m_offset = 0;
        }
    } else if (m_row < 0) {
        m_row = 0;
        m_offset = 0;
    } else if (first < m_row || (first == m_row && !(m_row == 0 && m_offset <= 0))) {
        // (The top of the content stays the top: rows inserted there are shown.)
        m_row += count;
    }
    clamp();
}

void ScrollAnchor::removeRows(int first, int count)
{
    first = std::clamp(first, 0, m_rows.count());
    count = std::clamp(count, 0, m_rows.count() - first);
    if (count == 0)
        return;
    m_rows.remove(first, count);
    if (m_pending) {
        // A place between rows: rows removed before it move it up.
        if (m_row >= first + count)
            m_row -= count;
        else if (m_row > first)
            m_row = first;
    } else if (m_row >= first + count) {
        m_row -= count;
    } else if (m_row >= first) {
        // The anchor's row is gone: until the change is complete, an insertion at its place
        // adopts the anchor; else the row now there is the anchor at offset 0 (settle()).
        m_pending = true;
        m_pendingOffset = m_offset;
        m_row = first;
        m_offset = 0;
    }
    clamp();
}

bool ScrollAnchor::moveRows(int first, int count, int destination)
{
    if (count <= 0 || first < 0 || first + count > m_rows.count() || destination < 0 || destination > m_rows.count()
        || (destination >= first && destination <= first + count))
        return false;
    settle();
    m_rows.move(first, count, destination);
    if (m_row >= first && m_row < first + count) {
        // The anchor moves with its row.
        m_row = (destination > first ? destination - count : destination) + (m_row - first);
    } else if (destination > first) {
        if (m_row >= first + count && m_row < destination)
            m_row -= count;
    } else if (m_row >= destination && m_row < first) {
        m_row += count;
    }
    clamp();
    return true;
}

void ScrollAnchor::setRowHeight(int row, qreal height, bool measured, Resize why)
{
    if (row < 0 || row >= m_rows.count())
        return;
    const qreal old = m_rows.height(row);
    m_rows.setHeight(row, height, measured);
    if (!m_pending && row == m_row && m_offset > 0) {
        const qreal now = m_rows.height(row);
        if (why == Resize::Reflow && old > 0 && m_offset <= old)
            m_offset = m_offset * now / old; // the same fraction of the row
        else if (m_offset > now)
            m_offset = now; // what was at the top edge is gone: the next row's top there
    }
    clamp();
}

void ScrollAnchor::setViewportHeight(qreal height)
{
    m_viewport = std::isnan(height) ? 0 : std::max<qreal>(0, height);
    clamp();
}

qreal ScrollAnchor::maxY() const
{
    return std::max<qreal>(0, m_rows.total() - m_viewport);
}

qreal ScrollAnchor::y() const
{
    if (m_row < 0)
        return 0;
    const qreal raw = m_rows.offsetOf(m_row) + (m_pending ? 0 : m_offset);
    return std::clamp<qreal>(raw, 0, maxY());
}

void ScrollAnchor::anchorAt(qreal y)
{
    m_pending = false;
    if (m_rows.isEmpty()) {
        m_row = -1;
        m_offset = 0;
        return;
    }
    const qreal target = std::isnan(y) ? 0 : std::clamp<qreal>(y, 0, maxY());
    if (target <= 0) {
        // The top: row 0 even without a height (a header measured later keeps the view at the top).
        m_row = 0;
        m_offset = 0;
        return;
    }
    const int row = m_rows.rowAt(target);
    if (row < 0) { // every row hidden
        m_row = 0;
        m_offset = 0;
        return;
    }
    m_row = row;
    m_offset = target - m_rows.offsetOf(row);
}

void ScrollAnchor::scrollTo(qreal y)
{
    anchorAt(y);
}

void ScrollAnchor::positionAtRow(int row, Position mode)
{
    settle();
    if (row < 0 || row >= m_rows.count())
        return;
    const qreal top = m_rows.offsetOf(row);
    const qreal h = m_rows.height(row);
    qreal offset = 0;
    switch (mode) {
    case Position::Beginning: offset = 0; break;
    case Position::Center: offset = h / 2 - m_viewport / 2; break;
    case Position::End: offset = h - m_viewport; break;
    case Position::Contain: {
        const qreal now = y();
        if (h >= m_viewport || top < now)
            offset = 0;
        else if (top + h > now + m_viewport)
            offset = h - m_viewport;
        else
            return; // shown whole: nothing moves
        break;
    }
    }
    m_row = row;
    m_offset = offset;
    clamp();
}

void ScrollAnchor::settle()
{
    if (!m_pending)
        return;
    m_pending = false;
    m_offset = 0;
    m_row = std::min(m_row, m_rows.count() - 1);
    clamp();
}

void ScrollAnchor::clamp()
{
    if (m_pending)
        return;
    if (m_rows.isEmpty()) {
        m_row = -1;
        m_offset = 0;
        return;
    }
    if (m_row < 0 || m_row >= m_rows.count()) {
        anchorAt(m_row < 0 ? 0 : m_rows.total());
        return;
    }
    if (m_rows.height(m_row) <= 0 && m_rows.total() > 0 && !(m_row == 0 && m_offset <= 0)) {
        // Hidden (a fold, a zoom): the next shown row is the anchor, at the same y. (Not the top:
        // row 0 at offset 0 is the top of the content, with a height or not.)
        anchorAt(m_rows.offsetOf(m_row) + std::min<qreal>(m_offset, 0));
        return;
    }
    const qreal raw = m_rows.offsetOf(m_row) + m_offset;
    if (raw < 0 || raw > maxY())
        anchorAt(raw);
}

QPair<int, int> ScrollAnchor::rowsShown(qreal above, qreal below) const
{
    if (m_rows.total() <= 0)
        return {-1, -1};
    const qreal top = std::max<qreal>(0, y() - std::max<qreal>(0, above));
    const qreal bottom = y() + m_viewport + std::max<qreal>(0, below);
    const int first = m_rows.rowAt(top);
    const int last = m_rows.rowAt(std::max(top, bottom - 1.0 / RowHeightTree::Scale));
    return {first, std::max(first, last)};
}

} // namespace brulion::editor
