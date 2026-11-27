// Calculations drawn by Atrament (PageView::CalcLook, docs/ATRAMENT.md step 5): CalcBlockView's
// look — the lines in the note's font on a sunken panel under a small "Calculation", each line's
// result right-aligned at its line's top past a thin separator. Where a line starts comes from the
// text's layout of the last frame (m_calcTops): a frame that lays the lines out anew (a narrower
// window wrapping them, a line break typed) draws once more with the new places.
#include "pageview.h"

#include "blocklistmodel.h"
#include "editorhost.h"
#include "pagecontroller.h"
#include "renderedspans.h"

#include <QCoreApplication>
#include <QRegularExpression>

#include <algorithm>
#include <cmath>

namespace brulion::editor::atr {

namespace at = brulion::atrament;

namespace {

// Where each line of `text` starts (UTF-16 offsets).
std::vector<int> lineStarts(const QString &text)
{
    std::vector<int> starts{0};
    for (qsizetype i = 0; i < text.size(); ++i)
        if (text.at(i) == u'\n')
            starts.push_back(int(i + 1));
    return starts;
}

// The tops of those lines in `layout` (from its origin); `fallback` per line without one.
std::vector<float> lineTops(const at::TextLayout *layout, const std::vector<int> &starts, float fallback)
{
    std::vector<float> tops;
    tops.reserve(starts.size());
    for (std::size_t k = 0; k < starts.size(); ++k)
        tops.push_back(layout && !layout->lines().empty() ? layout->caretRect(starts[k]).top() : float(k) * fallback);
    return tops;
}

// A total's line (CalcBlockView: `sum`, `razem`… alone on the line) is shown emphasised.
bool isTotal(const QString &line)
{
    static const QRegularExpression total(QStringLiteral("^\\s*(sum|suma|total|razem|avg|średnia|srednia)\\s*$"),
                                          QRegularExpression::CaseInsensitiveOption);
    return total.match(line).hasMatch();
}

struct CalcResult {
    int line = -1;
    QString text;
    bool error = false;
    bool assigned = false;
};

std::vector<CalcResult> calcResults(const QVariantList &results)
{
    std::vector<CalcResult> out;
    for (const QVariant &v : results) {
        const QVariantMap r = v.toMap();
        CalcResult c;
        c.line = r.value(QStringLiteral("line"), -1).toInt();
        const QString error = r.value(QStringLiteral("error")).toString();
        c.error = !error.isEmpty();
        c.text = c.error ? error : r.value(QStringLiteral("value")).toString();
        c.assigned = !r.value(QStringLiteral("assigned")).toString().isEmpty();
        if (c.line >= 0 && !c.text.isEmpty())
            out.push_back(std::move(c));
    }
    return out;
}

} // namespace

void PageView::setDrawsCalc(bool on, const CalcLook &look, at::Context<PageView> &cx)
{
    m_drawsCalc = on;
    m_calcLook = look;
    m_extents.markAllEstimated();
    cx.notify();
}

bool PageView::isCalc(int row) const
{
    return m_drawsCalc && m_model && row >= 0 && row < m_model->rowCount()
           && m_model->index(row, 0).data(BlockListModel::ViewRole).toString() == u"calc";
}

at::AnyElement PageView::renderCalcRow(std::size_t row, const QModelIndex &index, float textX, bool edited)
{
    const CalcLook &c = m_calcLook;
    const PageMetrics &m = m_metrics;
    const quint64 uid = index.data(BlockListModel::UidRole).toString().toULongLong();
    QString source = m_model->editTextAt(int(row));
    if (edited && m_app) {
        // What the field shows now (a composed word too): its lines are the ones the results go by.
        const std::u16string shown = m_input->read(*m_app).text();
        source = QString(reinterpret_cast<const QChar *>(shown.data()), qsizetype(shown.size()));
    }
    const std::vector<int> starts = lineStarts(source);
    const std::vector<CalcResult> results = calcResults(index.data(BlockListModel::CalcResultsRole).toList());
    // The results' column: as wide as the widest result (tabular figures ≈ 0.56 em) and some room,
    // at most 45 % of the block.
    float widest = 0;
    for (const CalcResult &r : results)
        widest = std::max(widest, float(r.text.size()) * (r.error ? c.errorSize : m.fontSize) * 0.56f);
    const float blockWidth = std::max(0.f, (m.contentWidth > 0 ? m.contentWidth : 600) - textX);
    const float resultsWidth = results.empty() ? 0 : std::min(blockWidth * 0.45f, std::ceil(widest) + 24);
    const float top = 24; // the lines start under the "Calculation" label (Theme.space6)

    at::Div panel = at::div().ml(textX).flex1().minW(0).relative().flexCol().bg(c.background).rounded(c.radius).pt(top).pb(12);
    if (edited)
        std::move(panel).border(1).borderColor(c.frame);
    std::move(panel).child(at::div()
                                           .absolute()
                                           .top(6)
                                           .left(16)
                                           .textSize(c.labelSize)
                                           .lineHeight(c.labelSize * 1.3f)
                                           .textColor(c.label)
                                           .fontFamily(c.uiFamily.empty() ? m_palette.bodyFamily : c.uiFamily)
                                           .ariaHidden()
                                           .child(at::text(c.title)));
    at::Div lines = at::div().pl(16).pr(resultsWidth + 16);
    if (edited) {
        std::move(lines).child(*m_input);
    } else {
        std::move(lines).child(at::text(u16View(source)).id(u"calc").trackLayout(m_rowText[uid]));
        m_rowTextFrame[uid] = m_frame;
    }
    std::move(panel).child(std::move(lines));
    if (results.empty())
        return std::move(panel);

    const std::vector<float> &known = m_calcTops[uid];
    const std::vector<float> tops = known.size() == starts.size() ? known : lineTops(nullptr, starts, m.lineHeight);
    std::move(panel).child(
            at::div().absolute().top(top).bottom(12).right(resultsWidth + 8).w(1).bg(c.separator).ariaHidden());
    const QStringList sourceLines = source.split(u'\n');
    for (const CalcResult &r : results) {
        if (std::size_t(r.line) >= starts.size())
            continue;
        const bool total = !r.error && isTotal(sourceLines.value(r.line));
        const QString name = (r.error ? QCoreApplication::translate("CalcBlockView", "Error: %1")
                                      : QCoreApplication::translate("CalcBlockView", "Result: %1"))
                                     .arg(r.text);
        at::Div value = at::div().child(at::text(u16View(r.text)));
        if (r.error)
            std::move(value).textColor(c.error).textSize(c.errorSize).fontFamily(c.uiFamily.empty() ? m_palette.bodyFamily : c.uiFamily);
        else
            std::move(value).textColor(r.assigned && !total ? c.muted : m_palette.text);
        if (total)
            std::move(value).fontSemibold().borderT(1).borderColor(c.rule);
        std::move(panel).child(at::div()
                                               .absolute()
                                               .top(top + tops[std::size_t(r.line)])
                                               .right(16)
                                               .w(std::max(0.f, resultsWidth - 16))
                                               .h(m.lineHeight)
                                               .flexRow()
                                               .justifyEnd()
                                               .itemsCenter()
                                               .overflowHidden()
                                               .role(at::Role::Text)
                                               .ariaLabel(u16View(name))
                                               .child(std::move(value)));
    }
    // After the lines' prepaint: where they start now; new places draw the frame once more.
    PageView *self = this;
    const bool fromInput = edited;
    return std::move(panel).child(at::canvas([self, uid, fromInput, starts](at::Rect, at::Window &w, at::App &a) {
                                      const at::TextLayout *layout = nullptr;
                                      if (fromInput && self->m_input)
                                          layout = self->m_input->read(a).layout();
                                      else if (const auto it = self->m_rowText.find(uid); it != self->m_rowText.end())
                                          layout = it->second.layout();
                                      if (!layout)
                                          return;
                                      std::vector<float> now = lineTops(layout, starts, self->m_metrics.lineHeight);
                                      std::vector<float> &kept = self->m_calcTops[uid];
                                      if (now != kept) {
                                          kept = std::move(now);
                                          w.refresh();
                                      }
                                  })
                                          .absolute()
                                          .size(0));
}

} // namespace brulion::editor::atr
