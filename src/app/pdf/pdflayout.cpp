#include "pdflayout.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace brulion::pdfview {

QSizeF PdfPageLayout::sane(QSizeF points, QSizeF fallback)
{
    const qreal w = points.width();
    const qreal h = points.height();
    if (!(w > 0) || !(h > 0) || !std::isfinite(w) || !std::isfinite(h))
        return fallback;
    // The aspect clamped, keeping the longer side.
    if (h / w > MaxAspect)
        return QSizeF(h / MaxAspect, h);
    if (w / h > MaxAspect)
        return QSizeF(w, w / MaxAspect);
    return points;
}

void PdfPageLayout::setPages(const QList<QSizeF> &points)
{
    const auto valid = std::ranges::find_if(points, [](QSizeF p) { return sane(p, QSizeF()).isValid(); });
    const QSizeF fallback = valid == points.cend() ? DefaultPage : sane(*valid, QSizeF());
    m_points.clear();
    m_points.reserve(points.size());
    for (const QSizeF &p : points)
        m_points.append(sane(p, fallback));
    relayout();
}

void PdfPageLayout::setPageSize(int index, QSizeF points)
{
    if (index < 0 || index >= m_points.size())
        return;
    m_points[index] = sane(points, m_points.at(index));
    relayout();
}

bool PdfPageLayout::setPageSizes(int from, const QList<QSizeF> &points)
{
    bool changed = false;
    for (qsizetype i = 0; i < points.size(); ++i) {
        const qsizetype index = from + i;
        if (index < 0 || index >= m_points.size())
            continue;
        const QSizeF s = sane(points.at(i), m_points.at(index));
        if (s != m_points.at(index)) {
            m_points[index] = s;
            changed = true;
        }
    }
    if (changed)
        relayout();
    return changed;
}

void PdfPageLayout::setGeometry(qreal viewportWidth, qreal margin, qreal gap, qreal maxFitWidth)
{
    m_viewportWidth = std::max<qreal>(0, viewportWidth);
    m_margin = std::max<qreal>(0, margin);
    m_gap = std::max<qreal>(0, gap);
    m_maxFitWidth = maxFitWidth > 0 ? maxFitWidth : 1e9;
    relayout();
}

void PdfPageLayout::setSpread(bool twoPages, bool coverAlone)
{
    if (twoPages == m_twoPages && coverAlone == m_coverAlone)
        return;
    m_twoPages = twoPages;
    m_coverAlone = coverAlone;
    relayout();
}

qreal PdfPageLayout::fitWidthScale() const
{
    if (m_widestSingle <= 0 && m_widestPair <= 0)
        return PointsToLogical;
    const qreal width = std::min(m_viewportWidth - 2 * m_margin, m_maxFitWidth);
    qreal scale = std::numeric_limits<qreal>::infinity();
    if (m_widestSingle > 0)
        scale = width / m_widestSingle;
    if (m_widestPair > 0)
        scale = std::min(scale, (width - m_gap) / m_widestPair);
    return std::clamp(scale, MinScale, MaxScale);
}

void PdfPageLayout::setScale(qreal scale)
{
    if (!std::isfinite(scale))
        return;
    m_scale = std::clamp(scale, MinScale, MaxScale);
    relayout();
}

QSizeF PdfPageLayout::pagePoints(int index) const
{
    return index >= 0 && index < m_points.size() ? m_points.at(index) : QSizeF();
}

void PdfPageLayout::relayout()
{
    m_rows.clear();
    m_rowOf.resize(m_points.size());
    m_widestSingle = 0;
    m_widestPair = 0;
    qreal y = m_margin;
    for (int i = 0; i < m_points.size();) {
        Row row{.first = i, .count = m_twoPages && !(m_coverAlone && i == 0) && i + 1 < m_points.size() ? 2 : 1};
        for (int k = i; k < i + row.count; ++k) {
            row.width += m_points.at(k).width();
            row.height = std::max(row.height, m_points.at(k).height());
            m_rowOf[k] = int(m_rows.size());
        }
        row.top = y;
        y += row.height * m_scale + m_gap;
        if (row.count == 2)
            m_widestPair = std::max(m_widestPair, row.width);
        else
            m_widestSingle = std::max(m_widestSingle, row.width);
        m_rows.append(row);
        i += row.count;
    }
    m_height = m_rows.isEmpty() ? 0 : y - m_gap + m_margin;
}

int PdfPageLayout::rowOf(int index) const
{
    return index >= 0 && index < m_rowOf.size() ? m_rowOf.at(index) : -1;
}

int PdfPageLayout::firstOfRow(int row) const
{
    return row >= 0 && row < m_rows.size() ? m_rows.at(row).first : -1;
}

QRectF PdfPageLayout::rect(int index) const
{
    if (index < 0 || index >= m_points.size())
        return {};
    const Row &row = m_rows.at(m_rowOf.at(index));
    const qreal w = m_points.at(index).width() * m_scale;
    const qreal h = m_points.at(index).height() * m_scale;
    const qreal rowWidth = row.width * m_scale + (row.count == 2 ? m_gap : 0);
    qreal x = (contentSize().width() - rowWidth) / 2;
    if (index > row.first)
        x += m_points.at(row.first).width() * m_scale + m_gap;
    return QRectF(x, row.top, w, h);
}

QSizeF PdfPageLayout::contentSize() const
{
    if (m_points.isEmpty())
        return QSizeF(m_viewportWidth, 0);
    const qreal widest = std::max(m_widestSingle * m_scale, m_widestPair > 0 ? m_widestPair * m_scale + m_gap : 0.0);
    return QSizeF(std::max(m_viewportWidth, widest + 2 * m_margin), m_height);
}

int PdfPageLayout::rowAt(qreal y) const
{
    if (m_rows.isEmpty())
        return -1;
    // The last row whose top is at or above y…
    const auto it = std::ranges::upper_bound(m_rows, y, {}, &Row::top);
    if (it == m_rows.cbegin())
        return 0;
    const int r = int(it - m_rows.cbegin()) - 1;
    // …or, in the gap after it, the next one.
    const qreal bottom = m_rows.at(r).top + m_rows.at(r).height * m_scale;
    return y > bottom && r + 1 < m_rows.size() ? r + 1 : r;
}

int PdfPageLayout::indexAt(qreal y) const
{
    const int r = rowAt(y);
    return r < 0 ? -1 : m_rows.at(r).first;
}

int PdfPageLayout::indexAt(QPointF content) const
{
    const int r = rowAt(content.y());
    if (r < 0)
        return -1;
    const Row &row = m_rows.at(r);
    if (row.count == 1)
        return row.first;
    // The second page from the middle of the gap between them on.
    const QRectF second = rect(row.first + 1);
    return content.x() >= second.left() - m_gap / 2 ? row.first + 1 : row.first;
}

std::pair<int, int> PdfPageLayout::range(qreal y0, qreal y1) const
{
    if (m_rows.isEmpty() || y1 < y0)
        return {-1, -1};
    const int first = rowAt(y0);
    int last = rowAt(y1);
    // rowAt() gives the next row in a gap: y1 in the gap after `last - 1` does not reach `last`.
    if (last > first && m_rows.at(last).top > y1)
        --last;
    const Row &top = m_rows.at(first);
    if (top.top + top.height * m_scale < y0 || top.top > y1)
        return {-1, -1};
    const Row &bottom = m_rows.at(std::max(first, last));
    return {top.first, bottom.first + bottom.count - 1};
}

PdfPageLayout::Anchor PdfPageLayout::anchorAt(QPointF content) const
{
    Anchor a;
    a.index = indexAt(content);
    if (a.index < 0)
        return a;
    const QRectF r = rect(a.index);
    a.fx = r.width() > 0 ? (content.x() - r.x()) / r.width() : 0;
    a.fy = r.height() > 0 ? (content.y() - r.y()) / r.height() : 0;
    return a;
}

QPointF PdfPageLayout::pointOf(const Anchor &anchor) const
{
    if (anchor.index < 0 || anchor.index >= m_points.size())
        return {};
    const QRectF r = rect(anchor.index);
    return QPointF(r.x() + anchor.fx * r.width(), r.y() + anchor.fy * r.height());
}

} // namespace brulion::pdfview
