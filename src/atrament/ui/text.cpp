#include "ui/text.h"

#include "text/fonts.h"
#include "text/textsystem.h"
#include "ui/app.h"

#include <algorithm>
#include <cmath>

namespace brulion::atrament {

AnyElement::AnyElement(const char16_t *text)
    : AnyElement(Text(std::u16string_view(text ? text : u"")))
{
}

AnyElement::AnyElement(std::u16string text)
    : AnyElement(Text(std::u16string_view(text)))
{
}

AnyElement::AnyElement(std::u16string_view text)
    : AnyElement(Text(text))
{
}

FaceId resolveFace(App &app, const TextStyle &style)
{
    return app.textSystem().fonts().match(style.fontFamily, style.fontWeight, style.fontStyle);
}

Text::Text(std::u16string_view text)
    : m_text(text.begin(), text.end())
{
}

Text &&Text::spans(std::span<const TextSpan> spans) &&
{
    m_spans.insert(m_spans.end(), spans.begin(), spans.end());
    return std::move(*this);
}

Text &&Text::span(Range range, TextStyleRefinement style) &&
{
    m_spans.push_back(TextSpan { range, std::move(style) });
    return std::move(*this);
}

void Text::buildRuns(Window &window)
{
    const TextStyle &base = window.textStyle();
    m_lineHeight = base.resolvedLineHeight();
    m_align = base.align;
    m_whiteSpace = base.whiteSpace;
    auto paintStyle = [](const TextStyle &s) {
        return TextPaintStyle { s.color, s.underline, s.strikethrough, s.background };
    };
    auto run = [&](int length, const TextStyle &s, std::uint32_t styleIndex) {
        FontRun r;
        r.length = length;
        r.face = resolveFace(*m_app, s);
        r.fontSize = s.fontSize;
        r.styleIndex = styleIndex;
        r.features = s.fontFeatures;
        m_runs.push_back(std::move(r));
    };
    const int length = int(m_text.size());
    if (m_spans.empty()) {
        m_paint.push_back(paintStyle(base));
        run(length, base, 0);
        return;
    }
    // Cut the text at every span boundary; each piece gets the base style refined by the spans
    // covering it, in order.
    ArenaVector<int> cuts;
    cuts.push_back(0);
    cuts.push_back(length);
    for (const TextSpan &s : m_spans) {
        cuts.push_back(std::clamp(s.range.start, 0, length));
        cuts.push_back(std::clamp(s.range.end, 0, length));
    }
    std::sort(cuts.begin(), cuts.end());
    cuts.erase(std::unique(cuts.begin(), cuts.end()), cuts.end());
    for (std::size_t i = 0; i + 1 < cuts.size(); ++i) {
        const int a = cuts[i];
        const int b = cuts[i + 1];
        TextStyle style = base;
        for (const TextSpan &s : m_spans) {
            if (s.range.start <= a && s.range.end >= b)
                s.style.applyTo(style);
        }
        const auto index = std::uint32_t(m_paint.size());
        m_paint.push_back(paintStyle(style));
        run(b - a, style, index);
    }
}

std::shared_ptr<const TextLayout> Text::layoutAt(std::optional<float> wrapWidth)
{
    ParagraphStyle style;
    style.wrapWidth = m_whiteSpace == WhiteSpace::NoWrap ? std::nullopt : wrapWidth;
    style.lineHeight = m_lineHeight;
    return m_app->textSystem().layout(std::u16string_view(m_text.data(), m_text.size()),
            std::span<const FontRun>(m_runs.data(), m_runs.size()), style);
}

NodeId Text::requestLayout(Window &window, App &app)
{
    m_app = &app;
    buildRuns(window);
    LayoutStyle style;
    Text *self = this; // in the frame arena for as long as the layout runs
    return window.requestMeasuredLayout(style,
            [self](std::optional<float> knownWidth, std::optional<float> knownHeight, AvailableSpace availableWidth,
                    AvailableSpace) -> Size {
                std::optional<float> wrap;
                if (knownWidth)
                    wrap = *knownWidth;
                else if (availableWidth.kind == AvailableSpace::Kind::Definite)
                    wrap = std::max(0.0f, availableWidth.value);
                else if (availableWidth.kind == AvailableSpace::Kind::MinContent)
                    wrap = 0.0f;
                self->m_layout = self->layoutAt(wrap);
                Size size = self->m_layout->size();
                if (knownWidth)
                    size.width = *knownWidth;
                if (knownHeight)
                    size.height = *knownHeight;
                return size;
            });
}

void Text::prepaint(Rect bounds, Window &, App &)
{
    // The layout's last measure may have been at another width (min-content while sizing).
    const bool wraps = m_whiteSpace != WhiteSpace::NoWrap;
    if (!m_layout || (wraps && (!m_layout->wrapWidth() || std::abs(*m_layout->wrapWidth() - bounds.width()) > 0.01f)))
        m_layout = layoutAt(wraps ? std::optional<float>(bounds.width()) : std::nullopt);
}

void Text::paint(Rect bounds, Window &window, App &)
{
    if (window.accessibilityEnabled())
        window.addAccessibilityText(text(), bounds, m_layout.get());
    if (!m_layout)
        return;
    window.paintText(*m_layout, bounds.origin, std::span<const TextPaintStyle>(m_paint.data(), m_paint.size()),
            m_align, bounds.width());
}

} // namespace brulion::atrament
