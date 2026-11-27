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

// Which link is hovered and pressed, kept across frames.
struct Text::LinkState {
    int hovered = -1;
    int pressed = -1;
    MouseDownEvent down;
};

Text &&Text::link(Range range, TextLink::ClickListener onClick, TextLink::MenuListener onMenu) &&
{
    m_links.push_back(TextLink{range, onClick, onMenu});
    return std::move(*this);
}

int Text::hoveredLink() const
{
    return m_linkState ? m_linkState->hovered : -1;
}

Text &&Text::span(Range range, TextStyleRefinement style) &&
{
    m_spans.push_back(TextSpan { range, std::move(style) });
    return std::move(*this);
}

Text &&Text::inlineObject(Range range, InlineObjectSize size, InlineObjectPainter painter) &&
{
    m_spans.push_back(TextSpan { range, {}, InlineObject { size, std::move(painter) } });
    return std::move(*this);
}

void Text::buildRuns(Window &window)
{
    const TextStyle &base = window.textStyle();
    m_lineHeight = base.resolvedLineHeight();
    m_align = base.align;
    m_whiteSpace = base.whiteSpace;
    auto paintStyle = [](const TextStyle &s) {
        return TextPaintStyle { s.color, s.underline, s.strikethrough, s.background, s.overline };
    };
    auto run = [&](int length, const TextStyle &s, std::uint32_t styleIndex, const InlineObject *object) {
        FontRun r;
        r.length = length;
        r.face = resolveFace(*m_app, s);
        r.fontSize = s.fontSize;
        r.styleIndex = styleIndex;
        r.features = s.fontFeatures;
        r.baselineShift = s.baselineShift;
        if (object) {
            const InlineObjectSize &size = object->size;
            const float unit = size.em ? s.fontSize : 1.0f;
            r.inlineBox = InlineBox { size.width * unit, size.ascent * unit, size.descent * unit };
        }
        m_runs.push_back(std::move(r));
    };
    const int length = int(m_text.size());
    if (m_spans.empty() && m_links.empty()) {
        m_paint.push_back(paintStyle(base));
        m_pieces.push_back(Range{0, length});
        m_pieceSizes.push_back(base.fontSize);
        run(length, base, 0, nullptr);
        return;
    }
    // The inline objects, in text order, without overlaps (the first given wins).
    for (const TextSpan &s : m_spans) {
        const Range r { std::clamp(s.range.start, 0, length), std::clamp(s.range.end, 0, length) };
        if (!s.object || r.isEmpty())
            continue;
        const bool overlaps = std::any_of(m_objects.begin(), m_objects.end(),
                [&](const Object &o) { return o.range.start < r.end && r.start < o.range.end; });
        if (!overlaps)
            m_objects.push_back(Object { &*s.object, r });
    }
    std::sort(m_objects.begin(), m_objects.end(),
            [](const Object &a, const Object &b) { return a.range.start < b.range.start; });
    // Cut the text at every span boundary; each piece gets the base style refined by the spans
    // covering it, in order.
    ArenaVector<int> cuts;
    cuts.push_back(0);
    cuts.push_back(length);
    for (const TextSpan &s : m_spans) {
        cuts.push_back(std::clamp(s.range.start, 0, length));
        cuts.push_back(std::clamp(s.range.end, 0, length));
    }
    // A link's ends cut too: its pieces get the hover underline of their own.
    for (const TextLink &l : m_links) {
        cuts.push_back(std::clamp(l.range.start, 0, length));
        cuts.push_back(std::clamp(l.range.end, 0, length));
    }
    std::sort(cuts.begin(), cuts.end());
    cuts.erase(std::unique(cuts.begin(), cuts.end()), cuts.end());
    // An object is one piece (one run): nothing cuts inside it.
    if (!m_objects.empty()) {
        cuts.erase(std::remove_if(cuts.begin(), cuts.end(),
                           [this](int c) {
                               return std::any_of(m_objects.begin(), m_objects.end(),
                                       [c](const Object &o) { return o.range.start < c && c < o.range.end; });
                           }),
                cuts.end());
    }
    std::size_t nextObject = 0;
    for (std::size_t i = 0; i + 1 < cuts.size(); ++i) {
        const int a = cuts[i];
        const int b = cuts[i + 1];
        TextStyle style = base;
        for (const TextSpan &s : m_spans) {
            if (s.range.start <= a && s.range.end >= b)
                s.style.applyTo(style);
        }
        const InlineObject *object = nullptr;
        if (nextObject < m_objects.size() && m_objects[nextObject].range.start == a)
            object = m_objects[nextObject++].object;
        const auto index = std::uint32_t(m_paint.size());
        m_paint.push_back(paintStyle(style));
        m_pieces.push_back(Range{a, b});
        m_pieceSizes.push_back(style.fontSize);
        run(b - a, style, index, object);
    }
}

std::shared_ptr<const TextLayout> Text::layoutAt(std::optional<float> wrapWidth)
{
    ParagraphStyle style;
    style.wrapWidth = m_whiteSpace == WhiteSpace::NoWrap ? std::nullopt : wrapWidth;
    style.lineHeight = m_lineHeight;
    std::shared_ptr<const TextLayout> layout = m_app->textSystem().layout(
            std::u16string_view(m_text.data(), m_text.size()), std::span<const FontRun>(m_runs.data(), m_runs.size()),
            style);
    if (m_maxLines <= 0 || layout->lines().size() <= std::size_t(m_maxLines) || !style.wrapWidth)
        return layout;
    std::shared_ptr<const TextLayout> clamped = clampedLayout(*layout, style);
    return clamped ? clamped : layout;
}

std::shared_ptr<const TextLayout> Text::clampedLayout(const TextLayout &full, const ParagraphStyle &style)
{
    if (m_runs.empty() || m_maxLines <= 0 || std::size_t(m_maxLines) > full.lines().size())
        return nullptr;
    const TextLine &last = full.lines()[std::size_t(m_maxLines - 1)];
    // The ellipsis in the run of the last character shown on that line (not as an inline box).
    const int lastChar = std::max(last.start, last.end - 1);
    FontRun ellipsis = m_runs.back();
    int runStart = 0;
    for (const FontRun &r : m_runs) {
        if (lastChar < runStart + r.length) {
            ellipsis = r;
            break;
        }
        runStart += r.length;
    }
    ellipsis.length = 1;
    ellipsis.inlineBox.reset();
    static constexpr char16_t kEllipsis = u'…';
    TextSystem &ts = m_app->textSystem();
    const float ellipsisWidth =
            ts.layout(std::u16string_view(&kEllipsis, 1), std::span<const FontRun>(&ellipsis, 1), ParagraphStyle{})
                    ->size()
                    .width;
    // Where the line is cut: the last grapheme boundary from which the ellipsis still fits, the
    // spaces before it dropped.
    const float width = style.wrapWidth ? *style.wrapWidth : full.size().width;
    int cut = last.end;
    while (cut > last.start && full.caretX(cut, CaretAffinity::Upstream) + ellipsisWidth > width)
        cut = std::min(cut - 1, full.previousCaretStop(cut));
    while (cut > last.start && (m_text[std::size_t(cut - 1)] == u' ' || m_text[std::size_t(cut - 1)] == u'\t'))
        --cut;
    std::u16string shown(m_text.data(), std::size_t(cut));
    shown.push_back(kEllipsis);
    std::vector<FontRun> runs;
    int left = cut;
    for (const FontRun &r : m_runs) {
        if (left <= 0)
            break;
        FontRun piece = r;
        piece.length = std::min(r.length, left);
        if (piece.length < r.length)
            piece.inlineBox.reset(); // (a caret stop never falls inside a box: none is cut)
        runs.push_back(std::move(piece));
        left -= r.length;
    }
    runs.push_back(std::move(ellipsis));
    return ts.layout(shown, runs, style);
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

void Text::prepaint(Rect bounds, Window &window, App &)
{
    // The layout's last measure may have been at another width (min-content while sizing).
    const bool wraps = m_whiteSpace != WhiteSpace::NoWrap;
    if (!m_layout || (wraps && (!m_layout->wrapWidth() || std::abs(*m_layout->wrapWidth() - bounds.width()) > 0.01f)))
        m_layout = layoutAt(wraps ? std::optional<float>(bounds.width()) : std::nullopt);
    if (m_layoutHandle) {
        m_layoutHandle->layout = m_layout;
        m_layoutHandle->bounds = bounds;
        m_layoutHandle->frame = window.frameCount();
    }
    if (m_links.empty() || !m_layout)
        return;
    m_linkState = &window.elementState<LinkState>();
    for (std::size_t k = 0; k < m_links.size(); ++k) {
        for (const Rect &r : m_layout->selectionRects(m_links[k].range)) {
            const HitboxId hitbox =
                    window.insertHitbox(Rect{{bounds.left() + r.left(), bounds.top() + r.top()}, r.size});
            window.setCursorStyle(hitbox, CursorStyle::PointingHand);
            m_linkHitboxes.push_back(hitbox);
            m_hitboxLink.push_back(std::uint32_t(k));

        }
    }
    // The hovered link underlined (its pieces' paint styles; the layout stays).
    if (const int h = m_linkState->hovered; h >= 0 && std::size_t(h) < m_links.size()) {
        const Range link = m_links[std::size_t(h)].range;
        for (std::size_t i = 0; i < m_pieces.size(); ++i)
            if (m_pieces[i].start >= link.start && m_pieces[i].end <= link.end && !m_paint[i].underline)
                m_paint[i].underline = UnderlineStyle{};
    }
}

void Text::paintObjects(Rect bounds, Window &window)
{
    if (m_objects.empty())
        return;
    const Rect mask = window.contentMask();
    const std::vector<TextLine> &lines = m_layout->lines();
    std::size_t k = 0;
    for (const PlacedInlineBox &box : m_layout->inlineBoxes()) {
        while (k < m_objects.size() && m_objects[k].range.start < box.start)
            ++k;
        if (k >= m_objects.size())
            break;
        const Object &object = m_objects[k];
        if (object.range.start != box.start || !object.object->paint)
            continue;
        const TextLine &line = lines[std::size_t(box.line)];
        const Point at { bounds.left() + alignedLineX(line, m_align, bounds.width()), bounds.top() };
        InlineObjectPaint p;
        p.bounds = box.bounds.translated(at);
        if (!p.bounds.intersects(mask) && !p.bounds.isEmpty())
            continue; // off screen
        p.baseline = at.y + line.baseline;
        p.range = object.range;
        if (box.styleIndex < m_paint.size()) {
            p.color = m_paint[box.styleIndex].color;
            p.fontSize = m_pieceSizes[box.styleIndex];
        }
        object.object->paint(p, window);
    }
}

void Text::paint(Rect bounds, Window &window, App &)
{
    // (The whole text for a screen reader; the layout's offsets only when it shows all of it.)
    if (window.accessibilityEnabled())
        window.addAccessibilityText(text(), bounds,
                m_layout && m_layout->length() == int(m_text.size()) ? m_layout.get() : nullptr);
    if (!m_layout)
        return;
    window.paintText(*m_layout, bounds.origin, std::span<const TextPaintStyle>(m_paint.data(), m_paint.size()),
            m_align, bounds.width());
    paintObjects(bounds, window);
    if (m_links.empty() || !m_linkState)
        return;

    Text *self = this; // in the frame arena, as the listeners are
    if (window.accessibilityEnabled()) {
        // One link node per link, named by its text, activated as a click.
        for (std::size_t k = 0; k < m_links.size(); ++k) {
            const Range range = m_links[k].range;
            Rect box;
            bool first = true;
            for (const Rect &r : m_layout->selectionRects(range)) {
                const Rect at{{bounds.left() + r.left(), bounds.top() + r.top()}, r.size};
                box = first ? at : box.united(at);
                first = false;
            }
            AccessibilityNode *node = window.beginAccessibilityNode(Role::Link, box, hashCombine(0x6c696e6bull, k) | 1);
            if (!node)
                continue;
            const int from = std::clamp(range.start, 0, int(m_text.size()));
            const int to = std::clamp(range.end, from, int(m_text.size()));
            node->name = std::u16string_view(m_text.data() + from, std::size_t(to - from));
            node->actions |= actionBit(AccessibilityAction::Click);
            node->handler = [self, k](const AccessibilityActionRequest &r, Window &w, App &app) {
                if (r.action != AccessibilityAction::Click)
                    return false;
                ClickEvent click;
                click.fromAccessibility = true;
                self->m_links[k].onClick(click, w, app);
                return true;
            };
            window.endAccessibilityNode();
        }
    }

    // A finger held on a link with a menu: its menu, before the long press of the elements around
    // it (registered in paint, as theirs are: the later, the more on top).
    for (std::size_t i = 0; i < m_linkHitboxes.size(); ++i) {
        const std::size_t k = m_hitboxLink[i];
        if (m_links[k].onMenu)
            window.onLongPress(m_linkHitboxes[i], [self, k](Point p, Window &w, App &app) { self->m_links[k].onMenu(p, w, app); });
    }
    window.onMouseEvent([self](const PlatformInput &input, DispatchPhase phase, Window &w, App &app) {
        LinkState &st = *self->m_linkState;
        int under = -1;
        for (std::size_t i = 0; i < self->m_linkHitboxes.size() && under < 0; ++i)
            if (w.isHovered(self->m_linkHitboxes[i]))
                under = int(self->m_hitboxLink[i]);
        if (phase == DispatchPhase::Capture) {
            if (std::holds_alternative<KeyDownEvent>(input) || std::holds_alternative<KeyUpEvent>(input))
                return;
            if (under != st.hovered) {
                st.hovered = under;
                w.refresh();
            }
            return;
        }
        if (const auto *down = std::get_if<MouseDownEvent>(&input)) {
            st.pressed = down->button == MouseButton::Left ? under : -1;
            if (under >= 0)
                st.down = *down;
            // A right-click on a link with a menu: its menu, not the row's.
            if (down->button == MouseButton::Right && under >= 0 && self->m_links[std::size_t(under)].onMenu) {
                self->m_links[std::size_t(under)].onMenu(down->position, w, app);
                w.stopPropagation();
            }
            return;
        }
        if (const auto *up = std::get_if<MouseUpEvent>(&input)) {
            const int pressed = st.pressed;
            st.pressed = -1;
            if (up->button != MouseButton::Left || pressed < 0 || pressed != under)
                return;
            ClickEvent click;
            click.down = st.down;
            click.up = *up;
            self->m_links[std::size_t(pressed)].onClick(click, w, app);
            w.stopPropagation(); // the link takes the click, not the row around it
        }
    });
}

} // namespace brulion::atrament
