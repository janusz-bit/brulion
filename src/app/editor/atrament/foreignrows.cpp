// PageView's foreign rows, header and footer (foreignrows.h): the boxes the host's QML items go
// over, where each frame painted them, and the heights those items measure.
#include "pageview.h"

#include "blocklistmodel.h"
#include "editorhost.h"

#include <algorithm>
#include <cmath>

namespace brulion::editor::atr {

namespace at = brulion::atrament;

at::AnyElement PageView::foreignRow(std::size_t row)
{
    const quint64 uid = m_model ? m_model->uidAt(int(row)) : 0;
    // As tall as its delegate (or the estimate until the host measured it), nothing drawn: the
    // delegate is over it. No node for screen readers either: the delegate's own are Qt Quick's.
    const float h = float(m_rows->heights().height(PageRows::treeRow(int(row))));
    PageView *self = this;
    at::Div box = at::div().id(at::ElementId::number(uid)).h(std::max(h, 1.0f)).ariaHidden();
    // A selected row's band (and grips) under its delegate, as the rows Atrament draws have it
    // (pageselection.cpp; the delegate draws none of its own here).
    if (std::optional<at::AnyElement> band = selectionBand(int(row)))
        std::move(box).child(std::move(*band));
    return std::move(box).child(at::canvas([self, row, uid](at::Rect bounds, at::Window &window, at::App &) {
                                    self->m_foreignFrames.push_back(
                                            {ForeignFrame::Kind::Row, int(row), uid, bounds, window.contentMask()});
                                }).sizeFull());
}

at::AnyElement edgeBox(ForeignFrame::Kind kind, float height, std::function<void(const ForeignFrame &)> painted)
{
    return at::div().h(height).ariaHidden().child(
            at::canvas([kind, painted = std::move(painted)](at::Rect bounds, at::Window &window, at::App &) {
                painted({kind, -1, 0, bounds, window.contentMask()});
            }).sizeFull());
}

at::AnyElement PageView::edgeElement(ForeignFrame::Kind kind)
{
    PageView *self = this;
    // (Painted also when it is 0 px tall: the header's place says where the rows start.)
    return edgeBox(kind, kind == ForeignFrame::Kind::Header ? m_headerHeight : m_footerHeight,
                   [self](const ForeignFrame &f) { self->m_foreignFrames.push_back(f); });
}

const ForeignFrame *PageView::edgeFrame(ForeignFrame::Kind kind) const
{
    for (const ForeignFrame &f : m_foreignFrames)
        if (f.kind == kind)
            return &f;
    return nullptr;
}

bool PageView::setForeignHeight(int row, float height)
{
    if (row < 0 || row >= m_rows->count() || !m_rows->isShown(row))
        return false;
    const int t = PageRows::treeRow(row);
    const RowHeightTree &tree = m_rows->heights();
    height = std::max(0.0f, height);
    if (tree.isMeasured(t) && std::abs(tree.height(t) - qreal(height)) <= 0.01)
        return false;
    m_rows->setRowHeight(t, height, true);
    return true;
}

bool PageView::setEdgeHeights(std::optional<float> header, std::optional<float> footer)
{
    const bool a = header && setHeaderHeight(*header);
    const bool b = footer && setFooterHeight(*footer);
    return a || b;
}

bool PageView::setHeaderHeight(float height)
{
    height = std::max(0.0f, height);
    if (std::abs(m_headerHeight - height) <= 0.01f)
        return false;
    m_headerHeight = height;
    return true;
}

bool PageView::setFooterHeight(float height)
{
    height = std::max(0.0f, height);
    if (std::abs(m_footerHeight - height) <= 0.01f)
        return false;
    m_footerHeight = height;
    return true;
}

std::optional<at::Rect> PageView::rowBounds(int row) const
{
    const ForeignFrame *header = edgeFrame(ForeignFrame::Kind::Header);
    if (!header || row < 0 || std::size_t(row) >= m_extents.count() || m_extents.isHidden(std::size_t(row)))
        return std::nullopt;
    // The rows start below the header.
    const float top = header->bounds.bottom() + m_extents.offsetOf(std::size_t(row));
    return at::Rect::fromXYWH(header->bounds.left(), top, header->bounds.width(), m_extents.heightOf(std::size_t(row)));
}

int PageView::rowAtY(float y) const
{
    const ForeignFrame *header = edgeFrame(ForeignFrame::Kind::Header);
    const std::size_t n = m_extents.count();
    if (!header || n == 0)
        return -1;
    const float into = y - header->bounds.bottom();
    if (into < 0 || into >= m_extents.total())
        return -1;
    const std::size_t row = m_extents.rowAt(into);
    return row < n ? int(row) : -1;
}

std::vector<int> PageView::foreignRowsNear(float above, float below) const
{
    std::vector<int> rows;
    const ForeignFrame *header = edgeFrame(ForeignFrame::Kind::Header);
    const std::size_t n = m_extents.count();
    if (!header || !m_model || n == 0)
        return rows;
    // The view and its margins in the rows' coordinates.
    const float rowsTop = header->bounds.bottom();
    const float from = header->mask.top() - above - rowsTop;
    const float to = header->mask.bottom() + below - rowsTop;
    if (to <= 0 || from >= m_extents.total())
        return rows;
    for (std::size_t r = m_extents.rowAt(std::max(0.0f, from)); r < n && m_extents.offsetOf(r) < to; ++r) {
        if (m_extents.heightOf(r) <= 0)
            continue; // hidden (a fold, a zoom)
        if (!drawsRow(int(r)))
            rows.push_back(int(r));
    }
    return rows;
}

} // namespace brulion::editor::atr
