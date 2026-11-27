// Points of a drawn row the self-tests in the app click (PageView::linkPoint, taskBoxPoint): taken
// from the row's text as the last frame laid it out.
#include "pageview.h"

#include "blocklistmodel.h"
#include "editorhost.h"
#include "pagecontroller.h"
#include "renderedtextitem.h"

namespace brulion::editor::atr {

namespace at = brulion::atrament;

namespace {

// The middle of the first piece of `range` in a text laid out by the last frame.
std::optional<at::Point> middleOf(const at::TextLayoutHandle &t, at::Range range)
{
    if (!t.layout() || range.end <= range.start)
        return std::nullopt;
    const std::vector<at::LineRect> rects = t.layout()->glyphRects(range);
    if (rects.empty())
        return std::nullopt;
    const at::Rect r = rects.front().rect.translated(t.bounds().origin);
    return at::Point{r.left() + r.width() / 2, r.top() + r.height() / 2};
}

} // namespace

std::optional<at::Point> PageView::linkPoint(int row, const QString &hrefPart) const
{
    if (const at::TextLayoutHandle *t = rowText(row)) {
        const auto rendered = m_model->index(row, 0).data(BlockListModel::RenderedTextRole).value<RenderedTextRef>();
        if (rendered.d)
            for (const auto &run : rendered.d->runs) {
                if (run.href.isEmpty() || !run.href.contains(hrefPart) || run.to <= run.from)
                    continue;
                if (const std::optional<at::Point> p = middleOf(*t, at::Range{int(run.from), int(run.to)}))
                    return p;
            }
    }
    // A link of its embedded blocks or query results (the last laid out of a text drawn twice).
    for (auto it = m_partTexts.rbegin(); it != m_partTexts.rend(); ++it) {
        if (it->row != row)
            continue;
        for (const auto &[range, href] : it->links)
            if (href.contains(hrefPart))
                if (const std::optional<at::Point> p = middleOf(it->handle, range))
                    return p;
    }
    return std::nullopt;
}

std::optional<at::Point> PageView::taskBoxPoint(int row) const
{
    const at::TextLayoutHandle *t = rowText(row);
    if (!t || m_model->index(row, 0).data(BlockListModel::TaskStatusRole).toString().isEmpty())
        return std::nullopt;
    // The box is in the marker's column, just left of the text, on its first line (renderRow).
    const float marker = m_metrics.documentMode ? m_metrics.listIndent : m_metrics.bulletColumn;
    return at::Point{t->bounds().left() - marker / 2, t->bounds().top() + m_metrics.lineHeight / 2};
}

} // namespace brulion::editor::atr
