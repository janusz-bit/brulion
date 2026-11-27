// Math formulas drawn and edited by Atrament (docs/ATRAMENT.md "The block editor in Atrament (M2)",
// step 5 "Blocks with views"): a ```math block's formula as MathBlockView shows it — the LaTeX
// rendered by markdown::renderMath (Unicode symbols, scripts, fractions as ⁄, radicals as √ with an
// overlined radicand, matrices on one line), read into runs by renderedTextFromHtml — drawn as
// Atrament text in the editor's font (Brulion Math's symbols through the fonts' fallback), centred
// on the sunken panel; scripts are smaller runs raised or lowered from the baseline
// (TextStyle::baselineShift), the radicand overlined (TextStyle::overline). A click edits the LaTeX
// source in the field (Enter a line break of the formula, as MathBlockView's). A formula with what
// Atrament's text does not draw yet (small capitals: \textsc) stays the QML view's (mathDrawable).
#include "pageview.h"

#include "blocklistmodel.h"
#include "editorhost.h"
#include "pagecontroller.h"
#include "renderedtextitem.h"

#include <QAbstractItemModel>

#include <algorithm>

namespace brulion::editor::atr {

namespace at = brulion::atrament;
using markdown::RenderedElement;
using markdown::RenderedRun;

namespace {

// The spans renderMath writes (markdown/math.cpp `styled()`), those Atrament draws.
bool drawableStyle(const QString &style)
{
    return style.isEmpty() || style == u"text-decoration:overline" || style == u"font-family:monospace"
           || style == u"font-family:sans-serif";
}

// Sub- and superscripts: Qt's rich text draws them at about two thirds of the size, raised by a
// third of the line or lowered by a fifth.
constexpr float kScriptScale = 0.7f;
constexpr float kSupShift = 0.4f;
constexpr float kSubShift = -0.2f;

} // namespace

bool PageView::mathDrawable(const markdown::RenderedText &text)
{
    if (!text.valid || !text.objects.isEmpty())
        return false;
    for (const RenderedElement &e : text.elements) {
        switch (e.tag) {
        case RenderedElement::Tag::Bold:
        case RenderedElement::Tag::Italic:
        case RenderedElement::Tag::Sup:
        case RenderedElement::Tag::Sub: break;
        case RenderedElement::Tag::Span:
            if (!drawableStyle(e.style))
                return false;
            break;
        case RenderedElement::Tag::Strike:
        case RenderedElement::Tag::Code:
        case RenderedElement::Tag::Anchor:
        case RenderedElement::Tag::Pre: return false;
        }
    }
    for (const RenderedRun &run : text.runs)
        if ((run.style & RenderedRun::SmallCaps) || !run.href.isEmpty() || !run.icon.isEmpty())
            return false;
    return true;
}

std::vector<at::TextSpan> PageView::mathSpans(const markdown::RenderedText &text, float fontSize,
                                              const PagePalette &palette, std::u16string_view sansFamily)
{
    std::vector<at::TextSpan> spans;
    for (const RenderedRun &run : text.runs) {
        if (run.to <= run.from)
            continue;
        at::TextStyleRefinement s;
        if (run.style & RenderedRun::Bold)
            s.fontWeight = at::FontWeight{at::FontWeight::Bold};
        if (run.style & RenderedRun::Italic)
            s.fontStyle = at::FontStyle::Italic;
        if (run.style & RenderedRun::Code)
            s.fontFamily = palette.codeFamily;
        else if (run.style & RenderedRun::SansSerif)
            s.fontFamily = std::u16string(sansFamily);
        if (run.style & RenderedRun::Overline)
            s.overline = true;
        if (run.style & (RenderedRun::Sup | RenderedRun::Sub)) {
            s.fontSize = fontSize * kScriptScale;
            s.baselineShift = fontSize * (run.style & RenderedRun::Sup ? kSupShift : kSubShift);
        }
        if (s.isEmpty())
            continue;
        spans.push_back({at::Range{int(run.from), int(run.to)}, std::move(s)});
    }
    return spans;
}

void PageView::setDrawsMath(bool on, at::Context<PageView> &cx)
{
    m_app = &cx.app();
    if (m_drawsMath == on)
        return;
    m_drawsMath = on;
    m_extents.markAllEstimated();
    cx.notify();
}

namespace {

bool mathRow(const BlockListModel *model, int row, bool drawsMath)
{
    return drawsMath && model && row >= 0 && row < model->rowCount()
           && model->index(row, 0).data(BlockListModel::ViewRole).toString() == u"math";
}

bool drawableAt(const BlockListModel *model, int row)
{
    const auto rendered = model->index(row, 0).data(BlockListModel::RenderedTextRole).value<RenderedTextRef>();
    return rendered.d && PageView::mathDrawable(*rendered.d);
}

} // namespace

bool PageView::isMath(int row) const
{
    if (!mathRow(m_model.data(), row, m_drawsMath))
        return false;
    // The formula Atrament edits stays Atrament's whatever is typed; the one the QML view edits
    // stays the view's until it is left.
    if (m_input && m_model->uidAt(row) == m_editUid)
        return true;
    if (m_controller && m_controller->editingRow() == row)
        return false;
    return drawableAt(m_model.data(), row);
}

bool PageView::mathEditable(int row) const
{
    if (!mathRow(m_model.data(), row, m_drawsMath))
        return false;
    return (m_input && m_model->uidAt(row) == m_editUid) || drawableAt(m_model.data(), row);
}

at::AnyElement PageView::renderMathPanel(std::size_t row, const QModelIndex &index, float textX, bool edited)
{
    const ViewLook &v = m_viewLook;
    // MathBlockView: the formula from space4 in and space3 down, centred, in the editor's font.
    at::Div panel = at::div()
                            .ml(textX)
                            .flex1()
                            .minW(0)
                            .flexCol()
                            .bg(v.panel)
                            .border(1)
                            .borderColor(edited ? v.editingBorder : v.panelBorder)
                            .rounded(v.radius)
                            .my(m_metrics.documentMode ? 0.0f : std::max(0.0f, v.framedPad - m_metrics.rowPad))
                            .pt(12)
                            .pb(12)
                            .pl(16)
                            .pr(16);
    if (edited)
        return std::move(panel).cursorText().child(*m_input);
    (void)row;
    const auto rendered = index.data(BlockListModel::RenderedTextRole).value<RenderedTextRef>();
    if (!rendered.d)
        return panel;
    const markdown::RenderedText &text = *rendered.d;
    const std::vector<at::TextSpan> spans = mathSpans(text, m_metrics.fontSize, m_palette, v.family);
    return std::move(panel).textCenter().cursorPointer().child(at::text(u16View(text.text)).id(u"formula").spans(spans));
}

} // namespace brulion::editor::atr
