// PdfPageLayout — where each page of the PDF viewer lies (docs/UI.md "PDF viewer"): pages one under
// another, each its own size × the scale, centred when narrower than the viewport, with margins
// around them and a gap between them. Pure (no Qt Quick), so the positions are exact: page jumps,
// zoom anchoring and search hits never drift — a ListView would estimate the positions of delegates
// it never created (variable heights). Binary search for the page at a height: 20 000 pages are
// laid out in a few milliseconds.
#pragma once

#include <QList>
#include <QPointF>
#include <QRectF>
#include <QSizeF>

#include <utility>

namespace brulion::pdfview {

class PdfPageLayout {
public:
    /// "100 %" is the page's real size: a point is 1/72 inch, a logical pixel 1/96.
    static constexpr qreal PointsToLogical = 96.0 / 72.0;
    static constexpr qreal MinScale = 0.1 * PointsToLogical;
    static constexpr qreal MaxScale = 8.0 * PointsToLogical;
    /// Hostile pages (1e6 × 0.001 pt): height / width clamped to [1/MaxAspect, MaxAspect].
    static constexpr qreal MaxAspect = 32;
    /// The size of a page whose size is unknown and of a PDF without one valid size: A4.
    static constexpr QSizeF DefaultPage{595.28, 841.89};

    /// The pages' sizes in points; invalid, ≤ 0 and non-finite sizes take the first valid size
    /// (else A4); every size's aspect clamped.
    void setPages(const QList<QSizeF> &points);
    /// One page's size known later (Android's sizes arrive in chunks): the pages after it move.
    void setPageSize(int index, QSizeF points);
    /// The sizes of pages [from, from + points.size()) at once (one layout pass); false when none
    /// of them changed.
    bool setPageSizes(int from, const QList<QSizeF> &points);
    void setGeometry(qreal viewportWidth, qreal margin, qreal gap, qreal maxFitWidth);
    /// The scale that makes the widest page min(viewport width − 2 · margin, maxFitWidth) wide.
    qreal fitWidthScale() const;
    /// Clamped to [MinScale, MaxScale].
    void setScale(qreal scale);
    qreal scale() const { return m_scale; }
    int count() const { return int(m_points.size()); }
    /// The page's size in points (as laid out: clamped), invalid for an index out of range.
    QSizeF pagePoints(int index) const;
    /// Page `index` in content coordinates; centred when narrower than the content.
    QRectF rect(int index) const;
    /// The content's size: the pages and the margins; at least the viewport's width.
    QSizeF contentSize() const;
    /// The page at `y`; in a gap the next page, before the first the first, after the last the last;
    /// −1 without pages.
    int indexAt(qreal y) const;
    /// The first and the last page intersecting [y0, y1]; {−1, −1} when none.
    std::pair<int, int> range(qreal y0, qreal y1) const;

    /// A point of the content as a page and fractions of it (zoom keeps it in place).
    struct Anchor {
        int index = -1;
        qreal fx = 0;
        qreal fy = 0;
    };
    Anchor anchorAt(QPointF content) const;
    QPointF pointOf(const Anchor &anchor) const;

private:
    void relayout();
    static QSizeF sane(QSizeF points, QSizeF fallback);

    QList<QSizeF> m_points;
    /// Each page's top (content coordinates, scaled) and the content's height.
    QList<qreal> m_tops;
    qreal m_widest = 0;
    qreal m_height = 0;
    qreal m_viewportWidth = 0;
    qreal m_margin = 0;
    qreal m_gap = 0;
    qreal m_maxFitWidth = 1e9;
    qreal m_scale = PointsToLogical;
};

} // namespace brulion::pdfview
