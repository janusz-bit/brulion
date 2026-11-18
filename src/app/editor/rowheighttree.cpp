#include "rowheighttree.h"

#include <algorithm>
#include <cmath>

namespace brulion::editor {

namespace {

/// A row is at most this tall (px): sums of a few million rows stay far inside 64 bits.
constexpr qreal MaxHeight = 1e9;

} // namespace

qint64 RowHeightTree::toFixed(qreal px)
{
    if (!(px > 0)) // negative, 0, NaN
        return 0;
    return std::llround(std::min(px, MaxHeight) * Scale);
}

qreal RowHeightTree::height(int row) const
{
    return row >= 0 && row < count() ? fromFixed(m_heights.at(row)) : 0;
}

bool RowHeightTree::isMeasured(int row) const
{
    return row >= 0 && row < count() && m_measured.at(row);
}

qint64 RowHeightTree::prefix(int n) const
{
    qint64 sum = 0;
    for (int k = n; k > 0; k -= k & -k)
        sum += m_tree.at(k);
    return sum;
}

void RowHeightTree::add(int row, qint64 delta)
{
    const int n = count();
    for (int k = row + 1; k <= n; k += k & -k)
        m_tree[k] += delta;
}

qreal RowHeightTree::offsetOf(int row) const
{
    return fromFixed(prefix(std::clamp(row, 0, count())));
}

int RowHeightTree::rowAt(qreal y) const
{
    if (m_total <= 0)
        return -1;
    // The largest n with prefix(n) <= y: row n holds y (its height > 0, as prefix(n + 1) > y).
    qint64 rest = std::isnan(y) ? 0
                                : std::clamp<qint64>(qint64(std::floor(std::clamp<qreal>(y, 0, 1e15) * Scale)), 0,
                                                     m_total - 1);
    int n = 0;
    for (int step = m_topBit; step > 0; step >>= 1) {
        const int next = n + step;
        if (next <= count() && m_tree.at(next) <= rest) {
            n = next;
            rest -= m_tree.at(next);
        }
    }
    return n;
}

int RowHeightTree::nextEstimated(int from) const
{
    for (int row = std::max(0, from); row < count(); ++row) {
        if (!m_measured.at(row))
            return row;
    }
    return -1;
}

void RowHeightTree::setHeight(int row, qreal height, bool measured)
{
    if (row < 0 || row >= count())
        return;
    const qint64 h = toFixed(height);
    const qint64 old = m_heights.at(row);
    const bool was = m_measured.at(row);
    if (was) {
        m_measuredTotal -= old;
        --m_measuredCount;
    }
    if (measured) {
        m_measuredTotal += h;
        ++m_measuredCount;
    }
    m_measured[row] = measured ? 1 : 0;
    if (h == old)
        return;
    m_heights[row] = h;
    m_total += h - old;
    add(row, h - old);
}

void RowHeightTree::setEstimated(int row)
{
    if (row < 0 || row >= count() || !m_measured.at(row))
        return;
    m_measured[row] = 0;
    m_measuredTotal -= m_heights.at(row);
    --m_measuredCount;
}

void RowHeightTree::insert(int row, int n, qreal estimate)
{
    if (n <= 0)
        return;
    row = std::clamp(row, 0, count());
    m_heights.insert(row, n, toFixed(estimate));
    m_measured.insert(row, n, quint8(0));
    rebuild();
}

void RowHeightTree::insert(int row, const QList<qreal> &heights, bool measured)
{
    if (heights.isEmpty())
        return;
    row = std::clamp(row, 0, count());
    QList<qint64> fixed;
    fixed.reserve(heights.size());
    for (const qreal h : heights)
        fixed.append(toFixed(h));
    m_heights.insert(row, fixed.size(), 0);
    std::copy(fixed.cbegin(), fixed.cend(), m_heights.begin() + row);
    m_measured.insert(row, fixed.size(), measured ? quint8(1) : quint8(0));
    rebuild();
}

void RowHeightTree::remove(int row, int n)
{
    row = std::clamp(row, 0, count());
    n = std::clamp(n, 0, count() - row);
    if (n == 0)
        return;
    m_heights.remove(row, n);
    m_measured.remove(row, n);
    rebuild();
}

bool RowHeightTree::move(int first, int n, int destination)
{
    if (n <= 0 || first < 0 || first + n > count() || destination < 0 || destination > count()
        || (destination >= first && destination <= first + n))
        return false;
    // A rotation of the span between the rows' old and new place.
    if (destination < first) {
        std::rotate(m_heights.begin() + destination, m_heights.begin() + first, m_heights.begin() + first + n);
        std::rotate(m_measured.begin() + destination, m_measured.begin() + first, m_measured.begin() + first + n);
    } else {
        std::rotate(m_heights.begin() + first, m_heights.begin() + first + n, m_heights.begin() + destination);
        std::rotate(m_measured.begin() + first, m_measured.begin() + first + n, m_measured.begin() + destination);
    }
    rebuild();
    return true;
}

void RowHeightTree::reset(int n, qreal estimate)
{
    n = std::max(0, n);
    // (Not QList::fill(value, size): after rows were removed at the front, Qt 6.12's fill appends
    // past the end of the allocation when the free space is at its beginning — an assertion.)
    m_heights = QList<qint64>(n, toFixed(estimate));
    m_measured = QList<quint8>(n, quint8(0));
    rebuild();
}

void RowHeightTree::assign(const QList<qreal> &heights, bool measured)
{
    m_heights.resize(heights.size());
    for (qsizetype k = 0; k < heights.size(); ++k)
        m_heights[k] = toFixed(heights.at(k));
    m_measured = QList<quint8>(heights.size(), measured ? quint8(1) : quint8(0));
    rebuild();
}

void RowHeightTree::markAllEstimated()
{
    m_measured.fill(0);
    m_measuredCount = 0;
    m_measuredTotal = 0;
}

void RowHeightTree::rebuild()
{
    const int n = count();
    m_tree = QList<qint64>(n + 1, 0);
    m_total = 0;
    m_measuredTotal = 0;
    m_measuredCount = 0;
    for (int k = 1; k <= n; ++k) {
        const qint64 h = m_heights.at(k - 1);
        m_total += h;
        if (m_measured.at(k - 1)) {
            m_measuredTotal += h;
            ++m_measuredCount;
        }
        m_tree[k] += h;
        const int parent = k + (k & -k);
        if (parent <= n)
            m_tree[parent] += m_tree.at(k);
    }
    m_topBit = 1;
    while (m_topBit * 2 <= n)
        m_topBit *= 2;
    if (n == 0)
        m_topBit = 0;
}

} // namespace brulion::editor
