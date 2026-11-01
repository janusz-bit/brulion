#include "pdflayout.h"

#include <algorithm>
#include <cmath>

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
    QSizeF fallback = DefaultPage;
    for (const QSizeF &p : points) {
        const QSizeF s = sane(p, QSizeF());
        if (s.isValid()) {
            fallback = s;
            break;
        }
    }
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

qreal PdfPageLayout::fitWidthScale() const
{
    if (m_widest <= 0)
        return PointsToLogical;
    const qreal width = std::min(m_viewportWidth - 2 * m_margin, m_maxFitWidth);
    return std::clamp(width / m_widest, MinScale, MaxScale);
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
    m_tops.resize(m_points.size());
    m_widest = 0;
    qreal y = m_margin;
    for (qsizetype i = 0; i < m_points.size(); ++i) {
        m_tops[i] = y;
        y += m_points.at(i).height() * m_scale + m_gap;
        m_widest = std::max(m_widest, m_points.at(i).width());
    }
    m_height = m_points.isEmpty() ? 0 : y - m_gap + m_margin;
}

QRectF PdfPageLayout::rect(int index) const
{
    if (index < 0 || index >= m_points.size())
        return {};
    const qreal w = m_points.at(index).width() * m_scale;
    const qreal h = m_points.at(index).height() * m_scale;
    const qreal content = contentSize().width();
    return QRectF((content - w) / 2, m_tops.at(index), w, h);
}

QSizeF PdfPageLayout::contentSize() const
{
    if (m_points.isEmpty())
        return QSizeF(m_viewportWidth, 0);
    return QSizeF(std::max(m_viewportWidth, m_widest * m_scale + 2 * m_margin), m_height);
}

int PdfPageLayout::indexAt(qreal y) const
{
    if (m_tops.isEmpty())
        return -1;
    // The last page whose top is at or above y…
    const auto it = std::upper_bound(m_tops.cbegin(), m_tops.cend(), y);
    if (it == m_tops.cbegin())
        return 0;
    const int i = int(it - m_tops.cbegin()) - 1;
    // …or, in the gap after it, the next one.
    const qreal bottom = m_tops.at(i) + m_points.at(i).height() * m_scale;
    return y > bottom && i + 1 < m_tops.size() ? i + 1 : i;
}

std::pair<int, int> PdfPageLayout::range(qreal y0, qreal y1) const
{
    if (m_tops.isEmpty() || y1 < y0)
        return {-1, -1};
    int first = indexAt(y0);
    int last = indexAt(y1);
    // indexAt() gives the next page in a gap: y1 in the gap after `last - 1` does not reach `last`.
    if (last > first && rect(last).top() > y1)
        --last;
    if (rect(first).bottom() < y0 || rect(first).top() > y1)
        return {-1, -1};
    return {first, std::max(first, last)};
}

PdfPageLayout::Anchor PdfPageLayout::anchorAt(QPointF content) const
{
    Anchor a;
    a.index = indexAt(content.y());
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
