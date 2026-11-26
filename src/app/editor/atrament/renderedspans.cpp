#include "renderedspans.h"

#include <QColor>
#include <QStringList>

#include <algorithm>

namespace brulion::editor::atr {

namespace at = brulion::atrament;
using markdown::RenderedBackground;
using markdown::RenderedRole;
using markdown::RenderedRun;

at::Color cssColor(const QString &css, at::Color fallback)
{
    if (css.isEmpty())
        return fallback;
    // rgb(r, g, b) and rgba(r, g, b, a) (a 0 … 1), which QColor does not read.
    const QString t = css.trimmed();
    if (t.startsWith(u"rgb") && t.endsWith(u')')) {
        const qsizetype open = t.indexOf(u'(');
        const QStringList parts = t.mid(open + 1, t.size() - open - 2).split(u',');
        const bool alpha = t.startsWith(u"rgba");
        if (open < 0 || parts.size() != (alpha ? 4 : 3))
            return fallback;
        float v[4] = {0, 0, 0, 1};
        for (qsizetype i = 0; i < parts.size(); ++i) {
            bool ok = false;
            const float x = parts[i].trimmed().toFloat(&ok);
            if (!ok)
                return fallback;
            v[i] = i < 3 ? std::clamp(x, 0.0f, 255.0f) / 255.0f : std::clamp(x, 0.0f, 1.0f);
        }
        return {v[0], v[1], v[2], v[3]};
    }
    const QColor c = QColor::fromString(t);
    if (!c.isValid())
        return fallback;
    return {float(c.redF()), float(c.greenF()), float(c.blueF()), float(c.alphaF())};
}

namespace {

at::Color roleColor(RenderedRole role, const PagePalette &p)
{
    switch (role) {
    case RenderedRole::Link: return p.link;
    case RenderedRole::Muted: return p.muted;
    case RenderedRole::Chip: return p.chip;
    case RenderedRole::Tag: return p.tag;
    case RenderedRole::Code: return p.code;
    case RenderedRole::Icon: return p.link;
    case RenderedRole::Text:
    case RenderedRole::Highlight: break;
    }
    return p.text;
}

} // namespace

std::vector<at::TextSpan> renderedSpans(const markdown::RenderedText &text, const PagePalette &palette)
{
    std::vector<at::TextSpan> spans;
    spans.reserve(std::size_t(text.runs.size()));
    for (const RenderedRun &run : text.runs) {
        if (run.to <= run.from)
            continue;
        at::TextStyleRefinement s;
        if (run.style & RenderedRun::Bold)
            s.fontWeight = at::FontWeight{at::FontWeight::Bold};
        if (run.style & RenderedRun::Italic)
            s.fontStyle = at::FontStyle::Italic;
        if (run.style & RenderedRun::Strike)
            s.strikethrough = true;
        if (run.style & RenderedRun::Code)
            s.fontFamily = palette.codeFamily;
        if (run.style & RenderedRun::Underline)
            s.underline = std::optional<at::UnderlineStyle>(at::UnderlineStyle{});
        if (!run.foreground.isEmpty() || run.role != RenderedRole::Text)
            s.color = cssColor(run.foreground, roleColor(run.role, palette));
        if (run.background != RenderedBackground::None || !run.backgroundColor.isEmpty()) {
            at::Color fallback = palette.codeBackground;
            if (run.role == RenderedRole::Highlight)
                fallback = palette.highlightBackground;
            else if (run.background == RenderedBackground::Pill)
                fallback = palette.chipBackground;
            s.background = std::optional<at::Color>(cssColor(run.backgroundColor, fallback));
        }
        if (s.isEmpty())
            continue;
        spans.push_back({at::Range{int(run.from), int(run.to)}, std::move(s)});
    }
    return spans;
}

} // namespace brulion::editor::atr
