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
        m_runs.push_back(run);
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
        m_input->setLaidOut(m_layout, bounds);
        // The caret stays in view: when the field takes the focus, when it moves or the text
        // changes, and when the window gets another size (an on-screen keyboard came up).
        if (m_input->revealWanted(m_input->focusHandle().isFocused(window), window.viewportSize()))
            window.revealRect(m_layout->caretRect(m_input->selection().focus).translated(bounds.origin).dilated(8));
    }

    void paint(Rect bounds, Window &window, App &) override
    {
        const bool focused = m_input->focusHandle().isFocused(window);
        const InputHandler::Selection sel = m_input->selection();
        const Range range = sel.range();
        const bool empty = m_input->text().empty();
        if (focused && !range.isEmpty() && !empty) {
            for (const Rect &r : m_layout->selectionRects(range))
                window.paintQuad(r.translated(bounds.origin), Background::solid(rgba(0x8b93ff55)), {}, transparent(),
                        Corners::all(2));
        }
        if (empty) {
            // The placeholder, faded, in the same layout position.
            auto placeholder = placeholderLayout();
            const TextPaintStyle style { m_textColor.fadedBy(0.45f), std::nullopt, false, std::nullopt };
            window.paintText(*placeholder, bounds.origin, std::span<const TextPaintStyle>(&style, 1));
        } else {
            const TextPaintStyle styles[] = {
                { m_textColor, std::nullopt, false, std::nullopt },
            };
            window.paintText(*m_layout, bounds.origin, styles);
            if (const auto marked = m_input->markedRange()) {
                for (const Rect &r : m_layout->selectionRects(*marked))
                    window.paintQuad(Rect::fromXYWH(r.left(), r.bottom() - 2, r.width(), 1).translated(bounds.origin),
                            Background::solid(m_textColor), {}, transparent(), {});
            }
        }
        if (focused && m_input->caretVisible()) {
            const Rect caret = m_layout->caretRect(sel.focus).translated(bounds.origin);
            window.paintQuad(Rect::fromXYWH(caret.left() - 0.5f, caret.top() + 1, 1.5f, std::max(2.0f, caret.height() - 2)),
                    Background::solid(rgb(0x8b93ff)), {}, transparent(), {});
        }
        if (focused) {
            auto *handler = Arena::current()->make<Handler>();
            handler->app = &m_app;
            handler->id = m_id;
            window.handleInput(m_input->focusHandle(), handler);
        }
        describeAccessibility(bounds, window);
    }

private:
    // Fills the text input's node (its div's): the text, the selection, the label and placeholder.
    void describeAccessibility(Rect bounds, Window &window)
    {
        AccessibilityNode *node = window.currentAccessibilityNode();
        if (!node || node->role != Role::TextInput)
            return;
        const TextInputOptions &o = m_input->options();
        AccessibilityStates &st = node->states;
        st.focusable = true;
        st.editable = true;
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
        FontRun run = m_runs.front();
        run.length = int(text.size());
        return m_app.textSystem().layout(text, std::span<const FontRun>(&run, 1), style);
    }

    std::shared_ptr<const TextLayout> placeholderLayout()
    {
        ParagraphStyle style;
        style.lineHeight = m_lineHeight;
        const std::u16string_view text = m_placeholder;
        FontRun run = m_runs.front();
        run.length = int(text.size());
        return m_app.textSystem().layout(text, std::span<const FontRun>(&run, 1), style);
    }

public:
    std::u16string_view m_placeholder;

private:
    App &m_app;
    EntityId m_id;
    TextInput *m_input;
    ArenaVector<FontRun> m_runs;
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

void TextInput::setLaidOut(std::shared_ptr<const TextLayout> layout, Rect bounds)
{
    m_layout = std::move(layout);
    m_origin = bounds.origin;
    m_bounds = bounds;
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
    restartBlink(cx);
    cx.emit(Changed { int(m_text.size()) });
    cx.notify();
}

void TextInput::restartBlink(Context<TextInput> &cx)
{
    m_caretVisible = true;
    if (m_blinking)
        scheduleBlink(cx);
}

void TextInput::scheduleBlink(Context<TextInput> &cx)
{
    m_blink = cx.setTimeout(kBlink, [](TextInput &t, Context<TextInput> &c) {
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
        // One undo step, as a paste over everything would be.
        m_marked.reset();
        m_coalesceTyping = false;
        replace({0, int(m_text.size())}, normalized(request.text), cx);
        m_coalesceTyping = false;
        return true;
    case AccessibilityAction::ReplaceText:
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
    m_selection = { clampOffset(anchor), clampOffset(focus) };
    m_coalesceTyping = false;
    restartBlink(cx);
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
    const int from = word ? previousWordStart(m_text, sel.start) : previousGraphemeBoundary(m_text, sel.start);
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
    const int to = word ? nextWordEnd(m_text, sel.start) : nextGraphemeBoundary(m_text, sel.start);
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
            moveCaret(word ? previousWordStart(m_text, focus) : previousGraphemeBoundary(m_text, focus), m.shift, cx);
        m_preferredX = -1;
        return true;
    }
    if (k.key == u"right") {
        if (!m.shift && !sel.isEmpty() && !word)
            moveCaret(sel.end, false, cx);
        else
            moveCaret(word ? nextWordEnd(m_text, focus) : nextGraphemeBoundary(m_text, focus), m.shift, cx);
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
    if (k.key == u"backspace") {
        deleteBackward(word, cx);
        return true;
    }
    if (k.key == u"delete") {
        deleteForward(word, cx);
        return true;
    }
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
            cut(cx);
            return true;
        }
        if (k.key == u"v") {
            paste(cx);
            return true;
        }
        if (k.key == u"z") {
            if (m.shift)
                redo(cx);
            else
                undo(cx);
            return true;
        }
        if (k.key == u"y") {
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
