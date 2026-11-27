#include "ui/textinput.h"

#include "text/segmentation.h"
#include "text/textlayout.h"
#include "text/textsystem.h"
#include "ui/div.h"
#include "ui/text.h"

#include <algorithm>
#include <chrono>

namespace brulion::atrament {

namespace {

constexpr std::chrono::milliseconds kBlink { 530 };
constexpr std::size_t kUndoLimit = 200;

// The element inside a TextInput's div: lays the text out, paints the selection, the text, the
// composition's underline and the caret, and gives the window the input handler.
class TextInputElement final : public Element {
public:
    TextInputElement(App &app, EntityId id, TextInput *input)
        : m_app(app)
        , m_id(id)
        , m_input(input)
    {
    }

    NodeId requestLayout(Window &window, App &) override
    {
        const TextStyle &base = window.textStyle();
        m_lineHeight = base.resolvedLineHeight();
        m_textColor = base.color;
        m_whiteSpace = m_input->multiline() ? WhiteSpace::Normal : WhiteSpace::NoWrap;
        FontRun run;
        run.face = resolveFace(m_app, base);
        run.fontSize = base.fontSize;
        run.features = base.fontFeatures;
        m_baseRun = run;
        buildRuns(base);
        LayoutStyle style;
        style.flexGrow = 1;
        TextInputElement *self = this;
        return window.requestMeasuredLayout(style,
                [self](std::optional<float> knownWidth, std::optional<float> knownHeight, AvailableSpace availableWidth,
                        AvailableSpace) -> Size {
                    std::optional<float> wrap;
                    if (knownWidth)
                        wrap = *knownWidth;
                    else if (availableWidth.isDefinite())
                        wrap = availableWidth.value;
                    else if (availableWidth.kind == AvailableSpace::Kind::MinContent)
                        wrap = 0.0f;
                    auto layout = self->layoutAt(wrap);
                    Size s = layout->size();
                    s.width = knownWidth.value_or(std::max(s.width, 2.0f));
                    if (knownHeight)
                        s.height = *knownHeight;
                    return s;
                });
    }

    void prepaint(Rect bounds, Window &window, App &) override
    {
        m_layout = layoutAt(bounds.width());
        m_input->setLaidOut(m_layout, bounds, m_baseRun.fontSize);
        // The caret stays in view: when the field takes the focus, when it moves or the text
        // changes, and when the window gets another size (an on-screen keyboard came up).
        if (m_input->revealWanted(m_input->focusHandle().isFocused(window), window.viewportSize()))
            window.revealRect(m_layout->caretRect(m_input->selection().focus).translated(bounds.origin).dilated(8));
        // The objects that take presses: a hitbox over each one's box, the pointing hand.
        for (const PlacedInlineBox &box : m_layout->inlineBoxes()) {
            const Object *object = objectAt(box.start);
            if (!object || (!object->object->onClick && !object->object->onLongPress))
                continue;
            const Rect r = box.bounds.translated(bounds.origin);
            const HitboxId hitbox = window.insertHitbox(r);
            window.setCursorStyle(hitbox, CursorStyle::PointingHand);
            // (The handlers copied: the field's spans may be styled anew before the events of this
            // frame are over.)
            m_objectHitboxes.push_back(
                    ObjectHitbox{hitbox, object->range, r, object->object->onClick, object->object->onLongPress});
        }
    }

    void paint(Rect bounds, Window &window, App &) override
    {
        const bool focused = m_input->focusHandle().isFocused(window);
        const InputHandler::Selection sel = m_input->selection();
        const Range range = sel.range();
        const bool empty = m_input->text().empty();
        const std::span<const TextPaintStyle> paints(m_paint.data(), m_paint.size());
        // The spans' shapes under the selection, the glyphs over it.
        if (!empty)
            window.paintTextBackgrounds(*m_layout, bounds.origin, paints);
        if (focused && !range.isEmpty() && !empty) {
            for (const Rect &r : m_layout->selectionRects(range))
                window.paintQuad(r.translated(bounds.origin), Background::solid(m_input->options().selectionColor), {},
                        transparent(), Corners::all(2));
        }
        if (empty) {
            // The placeholder, faded, in the same layout position.
            auto placeholder = placeholderLayout();
            const TextPaintStyle style { m_textColor.fadedBy(0.45f), std::nullopt, false, std::nullopt };
            window.paintText(*placeholder, bounds.origin, std::span<const TextPaintStyle>(&style, 1));
        } else {
            window.paintTextGlyphs(*m_layout, bounds.origin, paints);
            paintObjects(bounds, window);
            if (const auto marked = m_input->markedRange()) {
                for (const Rect &r : m_layout->selectionRects(*marked))
                    window.paintQuad(Rect::fromXYWH(r.left(), r.bottom() - 2, r.width(), 1).translated(bounds.origin),
                            Background::solid(m_textColor), {}, transparent(), {});
            }
        }
        if (focused && m_input->caretVisible()) {
            if (const std::optional<Rect> caret = m_input->caretBox()) {
                // A block over the text, translucent (the character stays readable under it).
                const Color c = m_input->options().caretShape == CaretShape::Block
                        ? m_input->options().caretColor.fadedBy(0.45f)
                        : m_input->options().caretColor;
                window.paintQuad(*caret, Background::solid(c), {}, transparent(),
                        m_input->options().caretShape == CaretShape::Block ? Corners::all(1) : Corners {});
            }
        }
        if (focused && !m_input->readOnly()) {
            auto *handler = Arena::current()->make<Handler>();
            handler->app = &m_app;
            handler->id = m_id;
            window.handleInput(m_input->focusHandle(), handler);
        }
        registerObjectListeners(window);
        describeAccessibility(bounds, window);
    }

private:
    // An inline object of the spans as laid out: its object (in the input's spans) and its text.
    struct Object {
        const InlineObject *object = nullptr;
        Range range;
    };
    struct ObjectHitbox {
        HitboxId hitbox = 0;
        Range range;
        Rect bounds;
        std::function<void(const ClickEvent &, Rect, Window &)> onClick;
        std::function<void(Point, Rect, Window &)> onLongPress;
    };

    const Object *objectAt(int start) const
    {
        for (const Object &o : m_objects)
            if (o.range.start == start)
                return &o;
        return nullptr;
    }

    // Each object's painter after the glyphs, with its box, the baseline, its colour and size.
    void paintObjects(Rect bounds, Window &window)
    {
        if (m_objects.empty())
            return;
        const Rect mask = window.contentMask();
        for (const PlacedInlineBox &box : m_layout->inlineBoxes()) {
            const Object *object = objectAt(box.start);
            if (!object || !object->object->paint)
                continue;
            const TextLine &line = m_layout->lines()[std::size_t(box.line)];
            InlineObjectPaint p;
            p.bounds = box.bounds.translated(bounds.origin);
            if (!p.bounds.intersects(mask) && !p.bounds.isEmpty())
                continue; // off screen
            p.baseline = bounds.top() + line.baseline;
            p.range = object->range;
            if (box.styleIndex < m_paint.size()) {
                p.color = m_paint[box.styleIndex].color;
                p.fontSize = m_pieceSizes[box.styleIndex];
            }
            object->object->paint(p, window);
        }
    }

    // The presses of objects that take them, before the field's own (its caret stays): a click
    // (the press and the release over the same object, any button) and a finger held on one.
    void registerObjectListeners(Window &window)
    {
        if (m_objectHitboxes.empty())
            return;
        TextInputElement *self = this; // the listeners live in this frame's arena with it
        for (const ObjectHitbox &h : m_objectHitboxes) {
            if (!h.onLongPress)
                continue;
            const ObjectHitbox *hb = &h;
            window.onLongPress(h.hitbox, [self, hb](Point at, Window &w, App &) {
                if (TextInput *t = self->m_app.tryGet<TextInput>(self->m_id))
                    t->objectPress().reset();
                const auto onLongPress = hb->onLongPress; // (it may style the field anew)
                onLongPress(at, hb->bounds, w);
            });
        }
        window.onMouseEvent([self](const PlatformInput &input, DispatchPhase phase, Window &w, App &) {
            if (phase != DispatchPhase::Bubble)
                return;
            TextInput *t = self->m_app.tryGet<TextInput>(self->m_id);
            if (!t)
                return;
            if (const auto *down = std::get_if<MouseDownEvent>(&input)) {
                if (down->scrollOnly)
                    return;
                for (const ObjectHitbox &h : self->m_objectHitboxes) {
                    if (h.onClick && w.isHovered(h.hitbox)) {
                        t->objectPress() = TextInput::ObjectPress{h.range, *down};
                        w.stopPropagation();
                        return;
                    }
                }
                return;
            }
            const auto *up = std::get_if<MouseUpEvent>(&input);
            if (!up || !t->objectPress())
                return;
            const TextInput::ObjectPress press = *t->objectPress();
            t->objectPress().reset();
            if (up->cancelled)
                return;
            for (const ObjectHitbox &h : self->m_objectHitboxes) {
                if (h.range.start == press.range.start && h.onClick && w.isHovered(h.hitbox)) {
                    ClickEvent click;
                    click.down = press.down;
                    click.up = *up;
                    // (A copy: the click may change the text, and with it the spans.)
                    const auto onClick = h.onClick;
                    const Rect box = h.bounds;
                    w.stopPropagation();
                    onClick(click, box, w);
                    return;
                }
            }
        });
    }

    // Fills the text input's node (its div's): the text, the selection, the label and placeholder.
    void describeAccessibility(Rect bounds, Window &window)
    {
        AccessibilityNode *node = window.currentAccessibilityNode();
        if (!node || node->role != Role::TextInput)
            return;
        const TextInputOptions &o = m_input->options();
        AccessibilityStates &st = node->states;
        st.focusable = true;
        st.editable = !o.readOnly;
        st.multiline = m_input->multiline();
        st.password = o.purpose == InputHandler::Purpose::Password;
        const std::u16string &text = m_input->text();
        if (st.password) {
            const std::u16string dots(text.size(), u'\u2022');
            node->value = window.accessibilityString(dots);
        } else {
            node->value = window.accessibilityString(text);
        }
        if (!o.label.empty()) {
            node->name = window.accessibilityString(o.label);
            node->description = window.accessibilityString(o.placeholder);
        } else {
            node->name = window.accessibilityString(o.placeholder);
        }
        node->nameFromContent = false;
        node->selection = m_input->selection();
        node->textLayout = m_layout.get();
        node->textOrigin = bounds.origin;
        node->actions |= actionBit(AccessibilityAction::Click) | actionBit(AccessibilityAction::Focus)
                         | actionBit(AccessibilityAction::SetValue) | actionBit(AccessibilityAction::SetSelection)
                         | actionBit(AccessibilityAction::ReplaceText);
        App *app = &m_app;
        const EntityId id = m_id;
        node->handler = [app, id](const AccessibilityActionRequest &r, Window &w, App &) {
            bool done = false;
            app->updateEntity<TextInput>(
                    id, [&](TextInput &t, Context<TextInput> &cx) { done = t.performAccessibilityAction(r, w, cx); });
            return done;
        };
    }

    struct Handler final : InputHandler {
        App *app = nullptr;
        EntityId id = 0;
        TextInput *input() const { return app->tryGet<TextInput>(id); }
        template<class F>
        void edit(F &&f)
        {
            app->updateEntity<TextInput>(id, [&](TextInput &t, Context<TextInput> &cx) { f(t, cx); });
        }
        std::u16string textForRange(Range r) override
        {
            TextInput *t = input();
            if (!t)
                return {};
            const int n = int(t->text().size());
            const int a = std::clamp(r.start, 0, n);
            const int b = std::clamp(r.end, a, n);
            return t->text().substr(std::size_t(a), std::size_t(b - a));
        }
        int textLength() override { return input() ? int(input()->text().size()) : 0; }
        std::optional<Selection> selection() override
        {
            return input() ? std::optional<Selection>(input()->selection()) : std::nullopt;
        }
        std::optional<Range> markedRange() override { return input() ? input()->markedRange() : std::nullopt; }
        void replaceText(std::optional<Range> range, std::u16string_view text) override
        {
            edit([&](TextInput &t, Context<TextInput> &cx) { t.commitText(range, text, cx); });
        }
        void replaceAndMarkText(std::optional<Range> range, std::u16string_view text,
                std::optional<Range> selectedWithin) override
        {
            edit([&](TextInput &t, Context<TextInput> &cx) { t.setMarkedText(range, text, selectedWithin, cx); });
        }
        void unmarkText() override
        {
            edit([&](TextInput &t, Context<TextInput> &cx) { t.unmark(cx); });
        }
        void setSelection(Selection s) override
        {
            edit([&](TextInput &t, Context<TextInput> &cx) { t.setSelection(s.anchor, s.focus, cx); });
        }
        std::optional<Rect> boundsForRange(Range r) override { return input() ? input()->boundsForRange(r) : std::nullopt; }
        std::optional<int> offsetForPoint(Point p) override { return input() ? input()->offsetForPoint(p) : std::nullopt; }
        std::optional<Rect> inputBounds() override { return input() ? input()->bounds() : std::nullopt; }
        Purpose purpose() override { return input() && input()->multiline() ? Purpose::Multiline : Purpose::Text; }
    };

    std::shared_ptr<const TextLayout> layoutAt(std::optional<float> wrap)
    {
        ParagraphStyle style;
        style.wrapWidth = m_whiteSpace == WhiteSpace::NoWrap ? std::nullopt : wrap;
        style.lineHeight = m_lineHeight;
        const std::u16string &text = m_input->text();
        return m_app.textSystem().layout(text, std::span<const FontRun>(m_runs.data(), m_runs.size()), style);
    }

    std::shared_ptr<const TextLayout> placeholderLayout()
    {
        ParagraphStyle style;
        style.lineHeight = m_lineHeight;
        const std::u16string_view text = m_placeholder;
        FontRun run = m_baseRun;
        run.length = int(text.size());
        return m_app.textSystem().layout(text, std::span<const FontRun>(&run, 1), style);
    }

    // The text's font runs and paint styles: the inherited style, refined by the styler's spans
    // over the pieces they cover (as a Text element's spans).
    void buildRuns(const TextStyle &base)
    {
        auto paintStyle = [](const TextStyle &t) {
            return TextPaintStyle { t.color, t.underline, t.strikethrough, t.background };
        };
        const int length = int(m_input->text().size());
        const std::vector<TextSpan> &spans = m_input->spans();
        if (spans.empty()) {
            FontRun run = m_baseRun;
            run.length = length;
            m_runs.push_back(std::move(run));
            m_paint.push_back(paintStyle(base));
            m_pieceSizes.push_back(base.fontSize);
            return;
        }
        // The inline objects, in text order, without overlaps (the first given wins), as a Text's.
        for (const TextSpan &sp : spans) {
            const Range r { std::clamp(sp.range.start, 0, length), std::clamp(sp.range.end, 0, length) };
            if (!sp.object || r.isEmpty())
                continue;
            const bool overlaps = std::any_of(m_objects.begin(), m_objects.end(),
                    [&](const Object &o) { return o.range.start < r.end && r.start < o.range.end; });
            if (!overlaps)
                m_objects.push_back(Object { &*sp.object, r });
        }
        std::sort(m_objects.begin(), m_objects.end(),
                [](const Object &a, const Object &b) { return a.range.start < b.range.start; });
        ArenaVector<int> cuts;
        cuts.push_back(0);
        cuts.push_back(length);
        for (const TextSpan &sp : spans) {
            cuts.push_back(std::clamp(sp.range.start, 0, length));
            cuts.push_back(std::clamp(sp.range.end, 0, length));
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
            for (const TextSpan &sp : spans) {
                if (sp.range.start <= a && sp.range.end >= b)
                    sp.style.applyTo(style);
            }
            FontRun run;
            run.length = b - a;
            run.face = resolveFace(m_app, style);
            run.fontSize = style.fontSize;
            run.features = style.fontFeatures;
            run.styleIndex = std::uint32_t(m_paint.size());
            if (nextObject < m_objects.size() && m_objects[nextObject].range.start == a) {
                const InlineObjectSize &size = m_objects[nextObject++].object->size;
                const float unit = size.em ? style.fontSize : 1.0f;
                run.inlineBox = InlineBox { size.width * unit, size.ascent * unit, size.descent * unit };
            }
            m_runs.push_back(std::move(run));
            m_paint.push_back(paintStyle(style));
            m_pieceSizes.push_back(style.fontSize);
        }
        if (length == 0 && m_runs.empty()) {
            FontRun run = m_baseRun;
            run.length = 0;
            m_runs.push_back(std::move(run));
            m_paint.push_back(paintStyle(base));
            m_pieceSizes.push_back(base.fontSize);
        }
    }

public:
    std::u16string_view m_placeholder;

private:
    App &m_app;
    EntityId m_id;
    TextInput *m_input;
    ArenaVector<FontRun> m_runs;
    ArenaVector<TextPaintStyle> m_paint;
    ArenaVector<float> m_pieceSizes; // the font size of each paint style
    ArenaVector<Object> m_objects; // the inline objects laid out, in text order
    ArenaVector<ObjectHitbox> m_objectHitboxes;
    FontRun m_baseRun;
    float m_lineHeight = 0;
    Color m_textColor;
    WhiteSpace m_whiteSpace = WhiteSpace::Normal;
    std::shared_ptr<const TextLayout> m_layout;
};

} // namespace

TextInput::TextInput(Context<TextInput> &cx)
    : TextInput(cx, Options {})
{
}

TextInput::TextInput(Context<TextInput> &cx, Options options)
    : m_options(std::move(options))
    , m_text(m_options.text)
    , m_focus(cx.newFocusHandle())
{
    m_selection = { int(m_text.size()), int(m_text.size()) };
}

int TextInput::clampOffset(int offset) const
{
    return std::clamp(offset, 0, int(m_text.size()));
}

void TextInput::setLaidOut(std::shared_ptr<const TextLayout> layout, Rect bounds, float fontSize)
{
    m_layout = std::move(layout);
    m_origin = bounds.origin;
    m_bounds = bounds;
    m_fontSize = fontSize;
}

std::optional<Rect> TextInput::boundsForRange(Range range) const
{
    if (!m_layout)
        return std::nullopt;
    if (range.isEmpty())
        return m_layout->caretRect(clampOffset(range.start)).translated(m_origin);
    Rect total;
    for (const Rect &r : m_layout->selectionRects(range))
        total = total.united(r);
    return total.translated(m_origin);
}

std::optional<int> TextInput::offsetForPoint(Point point) const
{
    if (!m_layout)
        return std::nullopt;
    return m_layout->offsetAt(point - m_origin);
}

bool TextInput::revealWanted(bool focused, Size viewport)
{
    const RevealKey key{focused, m_selection.focus, m_text.size(), viewport};
    const bool want = focused && !(key == m_revealKey);
    m_revealKey = key;
    return want;
}

void TextInput::pushUndo()
{
    m_undo.push_back(Snapshot { m_text, m_selection });
    if (m_undo.size() > kUndoLimit)
        m_undo.erase(m_undo.begin());
    m_redo.clear();
}

void TextInput::changed(Context<TextInput> &cx)
{
    m_preferredX = -1;
    ++m_textRevision;
    restartBlink(cx);
    cx.emit(Changed { int(m_text.size()) });
    cx.emit(SelectionChanged { m_selection });
    cx.notify();
}

void TextInput::selectionMoved(Context<TextInput> &cx)
{
    cx.emit(SelectionChanged { m_selection });
}

void TextInput::setPlaceholder(std::u16string_view placeholder, Context<TextInput> &cx)
{
    if (m_options.placeholder == placeholder)
        return;
    m_options.placeholder = std::u16string(placeholder);
    cx.notify();
}

void TextInput::setStyler(Styler styler, Context<TextInput> &cx)
{
    m_styler = std::move(styler);
    m_spansRevision = 0;
    m_spans.clear();
    cx.notify();
}

void TextInput::setColors(Color caret, Color selection, Context<TextInput> &cx)
{
    m_options.caretColor = caret;
    m_options.selectionColor = selection;
    cx.notify();
}

void TextInput::setCaret(CaretShape shape, bool blinks, Context<TextInput> &cx)
{
    if (m_options.caretShape == shape && m_options.caretBlinks == blinks)
        return;
    m_options.caretShape = shape;
    m_options.caretBlinks = blinks;
    restartBlink(cx);
    cx.notify();
}

void TextInput::setReadOnly(bool readOnly, Context<TextInput> &cx)
{
    if (m_options.readOnly == readOnly)
        return;
    m_options.readOnly = readOnly;
    // (An input method's composition cannot go on in a field that takes no text.)
    if (readOnly && m_marked) {
        m_marked.reset();
        m_compositionUndo = false;
    }
    cx.notify();
}

void TextInput::restyle(Context<TextInput> &cx)
{
    m_spansRevision = 0;
    cx.notify();
}

int TextInput::previousStop(int offset) const
{
    if (m_layout && m_layout->text() == m_text)
        return m_layout->previousCaretStop(offset);
    return previousGraphemeBoundary(m_text, offset);
}

int TextInput::nextStop(int offset) const
{
    if (m_layout && m_layout->text() == m_text)
        return m_layout->nextCaretStop(offset);
    return nextGraphemeBoundary(m_text, offset);
}

std::optional<Rect> TextInput::caretBox() const
{
    if (!m_layout || m_layout->text() != m_text)
        return std::nullopt;
    const int focus = clampOffset(m_selection.focus);
    const Rect caret = m_layout->caretRect(focus);
    const CaretShape shape = m_options.caretShape;
    if (shape == CaretShape::Bar)
        return Rect::fromXYWH(caret.left() - 0.5f, caret.top() + 1, 1.5f, std::max(2.0f, caret.height() - 2))
                .translated(m_origin);
    // A block or a line: not while something is selected (the selection shows the place).
    if (!m_selection.range().isEmpty())
        return std::nullopt;
    // As wide as the character after the caret on its line; half an em at a line's end (before a
    // line break, after the last character).
    const int line = m_layout->lineIndex(focus);
    const TextLine &l = m_layout->lines()[std::size_t(line)];
    float width = 0;
    if (focus < l.end) {
        const int next = m_layout->nextCaretStop(focus);
        if (m_layout->lineIndex(next, CaretAffinity::Upstream) == line)
            width = m_layout->caretX(next, CaretAffinity::Upstream) - caret.left();
    }
    if (width < 1)
        width = std::max(2.0f, (m_fontSize > 0 ? m_fontSize : caret.height() / 1.5f) / 2);
    if (shape == CaretShape::Block)
        return Rect::fromXYWH(caret.left(), caret.top(), width, caret.height()).translated(m_origin);
    // Underline: under the glyphs, at the line's descent.
    const float y = l.top + l.baseline + std::max(1.0f, l.descent * 0.6f);
    return Rect::fromXYWH(caret.left(), std::min(y, caret.bottom() - 2), width, 2).translated(m_origin);
}

const std::vector<TextSpan> &TextInput::spans() const
{
    if (m_spansRevision != m_textRevision) {
        m_spansRevision = m_textRevision;
        m_spans = m_styler ? m_styler(m_text) : std::vector<TextSpan>{};
    }
    return m_spans;
}

void TextInput::clearUndo()
{
    m_undo.clear();
    m_redo.clear();
    m_coalesceTyping = false;
}

void TextInput::restartBlink(Context<TextInput> &cx)
{
    m_caretVisible = true;
    if (m_blinking && m_options.caretBlinks)
        scheduleBlink(cx);
    else
        m_blink = {};
}

void TextInput::scheduleBlink(Context<TextInput> &cx)
{
    m_blink = cx.setTimeout(kBlink, [](TextInput &t, Context<TextInput> &c) {
        if (!t.m_options.caretBlinks) {
            t.m_caretVisible = true;
            return;
        }
        t.m_caretVisible = !t.m_caretVisible;
        c.notify();
        t.scheduleBlink(c);
    });
}

void TextInput::setText(std::u16string_view text, Context<TextInput> &cx)
{
    pushUndo();
    m_text = std::u16string(text);
    m_marked.reset();
    m_selection = { int(m_text.size()), int(m_text.size()) };
    m_coalesceTyping = false;
    changed(cx);
}

void TextInput::replace(Range range, std::u16string_view text, Context<TextInput> &cx)
{
    const int a = clampOffset(std::min(range.start, range.end));
    const int b = clampOffset(std::max(range.start, range.end));
    // Typing coalesces into one undo step until the caret moves or something else is done.
    const bool typing = b == a && text.size() <= 2 && !text.empty() && text != u"\n";
    if (!(typing && m_coalesceTyping))
        pushUndo();
    m_coalesceTyping = typing;
    m_text.replace(std::size_t(a), std::size_t(b - a), text);
    const int caret = a + int(text.size());
    m_selection = { caret, caret };
    changed(cx);
}

void TextInput::insert(std::u16string_view text, Context<TextInput> &cx)
{
    replace(m_marked ? *m_marked : m_selection.range(), normalized(text), cx);
    m_marked.reset();
}

std::u16string TextInput::normalized(std::u16string_view text) const
{
    std::u16string t(text);
    if (!m_options.multiline) {
        // One line: line breaks typed or pasted become spaces.
        for (char16_t &c : t) {
            if (c == u'\n' || c == u'\r' || c == u' ' || c == u' ')
                c = u' ';
        }
    }
    return t;
}

void TextInput::setLabel(std::u16string_view label, Context<TextInput> &cx)
{
    m_options.label = std::u16string(label);
    cx.notify();
}

bool TextInput::performAccessibilityAction(const AccessibilityActionRequest &request, Window &window,
                                           Context<TextInput> &cx)
{
    switch (request.action) {
    case AccessibilityAction::Click:
    case AccessibilityAction::Focus: m_focus.focus(window); return true;
    case AccessibilityAction::SetValue:
        if (m_options.readOnly)
            return false;
        // One undo step, as a paste over everything would be.
        m_marked.reset();
        m_coalesceTyping = false;
        replace({0, int(m_text.size())}, normalized(request.text), cx);
        m_coalesceTyping = false;
        return true;
    case AccessibilityAction::ReplaceText:
        if (m_options.readOnly)
            return false;
        m_marked.reset();
        m_coalesceTyping = false;
        replace(request.range, normalized(request.text), cx);
        m_coalesceTyping = false;
        return true;
    case AccessibilityAction::SetSelection: setSelection(request.range.start, request.range.end, cx); return true;
    default: return false;
    }
}

void TextInput::setSelection(int anchor, int focus, Context<TextInput> &cx)
{
    const InputHandler::Selection before = m_selection;
    m_selection = { clampOffset(anchor), clampOffset(focus) };
    m_coalesceTyping = false;
    restartBlink(cx);
    if (m_selection.anchor != before.anchor || m_selection.focus != before.focus)
        selectionMoved(cx);
    cx.notify();
}

void TextInput::moveCaret(int offset, bool extend, Context<TextInput> &cx)
{
    const int o = clampOffset(offset);
    setSelection(extend ? m_selection.anchor : o, o, cx);
}

void TextInput::deleteBackward(bool word, Context<TextInput> &cx)
{
    const Range sel = m_selection.range();
    if (!sel.isEmpty()) {
        replace(sel, u"", cx);
        return;
    }
    if (sel.start == 0)
        return;
    const int from = word ? previousWordStart(m_text, sel.start) : previousStop(sel.start);
    m_coalesceTyping = false;
    replace({ from, sel.start }, u"", cx);
}

void TextInput::deleteForward(bool word, Context<TextInput> &cx)
{
    const Range sel = m_selection.range();
    if (!sel.isEmpty()) {
        replace(sel, u"", cx);
        return;
    }
    if (sel.start >= int(m_text.size()))
        return;
    const int to = word ? nextWordEnd(m_text, sel.start) : nextStop(sel.start);
    m_coalesceTyping = false;
    replace({ sel.start, to }, u"", cx);
}

void TextInput::selectAll(Context<TextInput> &cx)
{
    setSelection(0, int(m_text.size()), cx);
}

void TextInput::undo(Context<TextInput> &cx)
{
    if (m_undo.empty())
        return;
    m_redo.push_back(Snapshot { m_text, m_selection });
    m_text = std::move(m_undo.back().text);
    m_selection = m_undo.back().selection;
    m_undo.pop_back();
    m_marked.reset();
    m_coalesceTyping = false;
    changed(cx);
}

void TextInput::redo(Context<TextInput> &cx)
{
    if (m_redo.empty())
        return;
    m_undo.push_back(Snapshot { m_text, m_selection });
    m_text = std::move(m_redo.back().text);
    m_selection = m_redo.back().selection;
    m_redo.pop_back();
    m_marked.reset();
    m_coalesceTyping = false;
    changed(cx);
}

void TextInput::copy(App &app) const
{
    const Range r = m_selection.range();
    if (!r.isEmpty())
        app.writeToClipboard(std::u16string_view(m_text).substr(std::size_t(r.start), std::size_t(r.length())));
}

void TextInput::cut(Context<TextInput> &cx)
{
    copy(cx.app());
    if (!m_selection.range().isEmpty()) {
        m_coalesceTyping = false;
        replace(m_selection.range(), u"", cx);
    }
}

void TextInput::paste(Context<TextInput> &cx)
{
    if (auto text = cx.app().readFromClipboard()) {
        m_coalesceTyping = false;
        insert(*text, cx);
        m_coalesceTyping = false;
    }
}

void TextInput::setMarkedText(std::optional<Range> range, std::u16string_view text, std::optional<Range> selectedWithin,
        Context<TextInput> &cx)
{
    const Range r = range ? *range : (m_marked ? *m_marked : m_selection.range());
    const int a = clampOffset(std::min(r.start, r.end));
    const int b = clampOffset(std::max(r.start, r.end));
    if (!m_marked)
        m_compositionUndo = false;
    // A composition is one undo step, from its first change of the text: an input method that
    // marks a word already there (Android's setComposingRegion) changes nothing yet.
    if (!m_compositionUndo && !text.empty()
        && std::u16string_view(m_text).substr(std::size_t(a), std::size_t(b - a)) != text) {
        pushUndo();
        m_compositionUndo = true;
    }
    m_text.replace(std::size_t(a), std::size_t(b - a), text);
    if (text.empty()) {
        // Cancelled: the text is as it was before, and so is the history.
        if (m_marked && m_compositionUndo && !m_undo.empty() && m_undo.back().text == m_text)
            m_undo.pop_back();
        m_compositionUndo = false;
        m_marked.reset();
        m_selection = { a, a };
    } else {
        m_marked = Range { a, a + int(text.size()) };
        if (selectedWithin)
            m_selection = { a + std::clamp(selectedWithin->start, 0, int(text.size())),
                a + std::clamp(selectedWithin->end, 0, int(text.size())) };
        else
            m_selection = { a + int(text.size()), a + int(text.size()) };
    }
    m_coalesceTyping = false;
    changed(cx);
}

void TextInput::commitText(std::optional<Range> range, std::u16string_view text, Context<TextInput> &cx)
{
    if (!m_marked) {
        const Range r = range ? *range : m_selection.range();
        replace(r, text, cx);
        return;
    }
    // In place of the composition, in its undo step.
    const Range r = range ? *range : *m_marked;
    const int a = clampOffset(std::min(r.start, r.end));
    const int b = clampOffset(std::max(r.start, r.end));
    if (!m_compositionUndo && std::u16string_view(m_text).substr(std::size_t(a), std::size_t(b - a)) != text)
        pushUndo();
    m_text.replace(std::size_t(a), std::size_t(b - a), text);
    m_marked.reset();
    m_compositionUndo = false;
    m_coalesceTyping = false;
    const int caret = a + int(text.size());
    m_selection = { caret, caret };
    changed(cx);
}

void TextInput::unmark(Context<TextInput> &cx)
{
    if (!m_marked)
        return;
    m_marked.reset();
    m_compositionUndo = false;
    cx.notify();
}

bool TextInput::handleKey(const Keystroke &k, Window &window, Context<TextInput> &cx)
{
    const Modifiers &m = k.modifiers;
    // A key the field acts on while an input method composes ends the composition first: the
    // composed word becomes text and the key acts on it (Qt's text fields do the same), so the
    // next composition starts anew instead of replacing the word.
    static constexpr std::u16string_view endsComposition[] = { u"left", u"right", u"home", u"end", u"up",
                                                               u"down", u"backspace", u"delete", u"enter" };
    if (m_marked && (m.secondary() || std::ranges::find(endsComposition, k.key) != std::end(endsComposition)))
        m_marked.reset();
    const bool word = m.control || m.alt;
    const int focus = m_selection.focus;
    const Range sel = m_selection.range();
    auto lineOf = [&](int offset) -> const TextLine * {
        if (!m_layout || m_layout->lines().empty())
            return nullptr;
        return &m_layout->lines()[std::size_t(m_layout->lineIndex(offset))];
    };
    if (k.key == u"left") {
        if (!m.shift && !sel.isEmpty() && !word)
            moveCaret(sel.start, false, cx);
        else
            moveCaret(word ? previousWordStart(m_text, focus) : previousStop(focus), m.shift, cx);
        m_preferredX = -1;
        return true;
    }
    if (k.key == u"right") {
        if (!m.shift && !sel.isEmpty() && !word)
            moveCaret(sel.end, false, cx);
        else
            moveCaret(word ? nextWordEnd(m_text, focus) : nextStop(focus), m.shift, cx);
        m_preferredX = -1;
        return true;
    }
    if (k.key == u"home" || k.key == u"end") {
        int target = k.key == u"home" ? 0 : int(m_text.size());
        if (!m.control) {
            if (const TextLine *line = lineOf(focus))
                target = k.key == u"home" ? line->start : line->end;
        }
        moveCaret(target, m.shift, cx);
        m_preferredX = -1;
        return true;
    }
    if (k.key == u"up" || k.key == u"down") {
        if (!m_layout)
            return false;
        const float x = m_preferredX >= 0 ? m_preferredX : m_layout->caretX(focus);
        const int target = k.key == u"up" ? m_layout->offsetAbove(focus, x) : m_layout->offsetBelow(focus, x);
        if (target < 0) {
            // Past the first or last line: to the start or the end, as text fields do.
            if (!m_options.multiline)
                return false;
            moveCaret(k.key == u"up" ? 0 : int(m_text.size()), m.shift, cx);
        } else {
            moveCaret(target, m.shift, cx);
        }
        m_preferredX = x;
        return true;
    }
    const bool readOnly = m_options.readOnly;
    if (k.key == u"backspace") {
        if (!readOnly)
            deleteBackward(word, cx);
        return true;
    }
    if (k.key == u"delete") {
        if (!readOnly)
            deleteForward(word, cx);
        return true;
    }
    if (k.key == u"enter" && readOnly && m_options.multiline)
        return true;
    if (k.key == u"enter") {
        if (m_options.multiline && !m.control) {
            m_coalesceTyping = false;
            insert(u"\n", cx);
        } else {
            cx.emit(Submitted { m_text });
        }
        return true;
    }
    if (m.secondary() && !m.alt) {
        if (k.key == u"a") {
            selectAll(cx);
            return true;
        }
        if (k.key == u"c") {
            copy(cx.app());
            return true;
        }
        if (k.key == u"x") {
            if (readOnly)
                copy(cx.app());
            else
                cut(cx);
            return true;
        }
        if (k.key == u"v") {
            if (!readOnly)
                paste(cx);
            return true;
        }
        if (k.key == u"z") {
            if (readOnly)
                return true;
            if (m.shift)
                redo(cx);
            else
                undo(cx);
            return true;
        }
        if (k.key == u"y") {
            if (!readOnly)
                redo(cx);
            return true;
        }
    }
    (void)window;
    return false;
}

AnyElement TextInput::render(Window &window, Context<TextInput> &cx)
{
    // The caret blinks only while the field has the focus.
    const bool focused = m_focus.isFocused(window);
    if (focused && !m_blinking) {
        m_blinking = true;
        restartBlink(cx);
    } else if (!focused && m_blinking) {
        m_blinking = false;
        m_blink = {};
        m_caretVisible = true;
    }
    // A composition ends with the focus, as it stands (an input method's commit could no longer
    // reach this field).
    if (!focused && m_marked) {
        m_marked.reset();
        m_compositionUndo = false;
    }

    TextInputElement element(cx.app(), cx.entityId(), this);
    element.m_placeholder = m_options.placeholder;
    return div()
            .id(ElementId::number(cx.entityId()))
            .flexRow()
            .wFull()
            .minW0()
            .trackFocus(m_focus)
            .keyContext(u"TextInput")
            .role(Role::TextInput)
            .cursorText()
            .onKeyDown(cx.listener([](TextInput &t, const KeyDownEvent &e, Window &w, Context<TextInput> &c) {
                if (t.handleKey(e.keystroke, w, c))
                    w.stopPropagation();
            }))
            .onMouseDown(MouseButton::Left,
                         cx.listener([](TextInput &t, const MouseDownEvent &e, Window &w, Context<TextInput> &c) {
                             const std::optional<int> at = t.offsetForPoint(e.position);
                             if (!at)
                                 return;
                             t.m_focus.focus(w);
                             // A press ends the composition as it stands (the host has asked the
                             // input method to commit it first, where it can).
                             t.unmark(c);
                             if (e.clickCount >= 3) {
                                 t.selectAll(c);
                             } else if (e.clickCount == 2) {
                                 const WordRange wr = wordAt(t.m_text, *at);
                                 t.setSelection(wr.start, wr.end, c);
                             } else {
                                 t.moveCaret(*at, e.modifiers.shift, c);
                             }
                             // A finger or a pen drags no selection — its drag scrolls (the
                             // window's touch gesture): a touch platform selects by a long press
                             // and the handles (Android's input method sets the selection); a drag
                             // selecting from the press undid it.
                             t.m_dragging = e.pointer == PointerKind::Mouse;
                             t.m_dragAnchor = t.m_selection.anchor;
                         }))
            .onMouseMove(cx.listener([](TextInput &t, const MouseMoveEvent &e, Window &, Context<TextInput> &c) {
                if (!t.m_dragging || e.pressedButton != MouseButton::Left)
                    return;
                if (const std::optional<int> at = t.offsetForPoint(e.position))
                    t.setSelection(t.m_dragAnchor, *at, c);
            }))
            .onMouseUp(MouseButton::Left, cx.listener([](TextInput &t, const MouseUpEvent &, Window &,
                                                         Context<TextInput> &) { t.m_dragging = false; }))
            .child(std::move(element));
}

} // namespace brulion::atrament
