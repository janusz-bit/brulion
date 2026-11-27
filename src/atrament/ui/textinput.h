#pragma once

// A text field (docs/ATRAMENT.md M1): a view with its own state — the text, the selection, the
// input method's marked text, an undo history — for a single line (a search box) or a paragraph
// (a block of the editor).
//
//   Entity<TextInput> name = cx.newEntity<TextInput>(TextInput::Options { .placeholder = u"Imię" });
//   … .child(name) …                                  // it is an element
//   name.read(app).text()
//   subscription = cx.subscribe<TextInput::Changed>(name, [](Me &, const TextInput::Changed &, …) { … });
//
// Editing: typing and the input method (composition shown underlined), arrows by grapheme,
// Ctrl+arrows by word, Home/End by line, Shift extends, Backspace/Delete (Ctrl: a word), Ctrl+A,
// Ctrl+C/X/V, Ctrl+Z / Ctrl+Shift+Z / Ctrl+Y, Enter (a new line when multiline, else Submitted),
// the mouse: click, drag, Shift+click, double click (a word), triple click (all).
//
// Styled text (docs/ATRAMENT.md M2, "A styled TextInput"): `setStyler(f)` gives the text spans
// from a function of the text (the edited block's Markdown highlighted: markers dimmed, bold bold,
// code in its face), asked again whenever the text changes — the spans refine the inherited text
// style as a Text element's do. `setColors` sets the caret's and the selection's colours.
// An owner around the field sees its changes as events: Changed (the text), SelectionChanged
// (the caret or the selection moved, the text changed too). `restyle()` asks the styler again
// when what it depends on besides the text changed (a spell checker's verdicts, the caret).
//
// Inline objects in the spans (a span's `object`: a reminder chip over its token) are laid out as
// in a Text, one unit for the caret — ←/→, Backspace and Delete step over or remove one whole, a
// click never lands inside — and an object with `onClick` / `onLongPress` takes its presses.
//
// The caret (docs/ATRAMENT.md "Text", carets): a bar before the character (the default,
// blinking), a block over the character after it or an underline under it (`setCaret`: Vim's
// normal and replace modes), steady or blinking; a block or an underline is not drawn while
// something is selected (the selection shows where it is). `setReadOnly(true)`: keys move the
// caret and select, nothing changes the text but the owner's own calls (replace, setText…), and
// the field gives the window no input handler — no input method, keys reach it as keys.

#include "ui/app.h"
#include "ui/element.h"
#include "ui/platform.h"
#include "ui/style.h"
#include "ui/text.h"
#include "ui/window.h"

#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace brulion::atrament {

class TextLayout;

// Where the caret is drawn: a bar before the character at the caret, a block over it (as wide as
// the character; half an em at a line's end), a line under it.
enum class CaretShape : std::uint8_t { Bar, Block, Underline };

struct TextInputOptions {
    std::u16string placeholder;
    bool multiline = false;
    InputHandler::Purpose purpose = InputHandler::Purpose::Text;
    std::u16string text;
    // The field's accessible name ("Szukaj"); without one a screen reader names it by the
    // placeholder, which is otherwise its description.
    std::u16string label;
    Color caretColor = rgb(0x8b93ff);
    Color selectionColor = rgba(0x8b93ff55);
    CaretShape caretShape = CaretShape::Bar;
    bool caretBlinks = true;
    bool readOnly = false;
};

class TextInput {
public:
    using Options = TextInputOptions;
    struct Changed {
        int length;
    };
    struct Submitted {
        std::u16string text;
    };
    struct SelectionChanged {
        InputHandler::Selection selection;
    };
    // The spans of a text (ranges in UTF-16 offsets of it), in order.
    using Styler = std::function<std::vector<TextSpan>(std::u16string_view text)>;

    explicit TextInput(Context<TextInput> &cx);
    TextInput(Context<TextInput> &cx, Options options);
    AnyElement render(Window &window, Context<TextInput> &cx);

    const std::u16string &text() const { return m_text; }
    void setText(std::u16string_view text, Context<TextInput> &cx);
    InputHandler::Selection selection() const { return m_selection; }
    std::optional<Range> markedRange() const { return m_marked; }
    const FocusHandle &focusHandle() const { return m_focus; }
    bool multiline() const { return m_options.multiline; }
    const Options &options() const { return m_options; }
    void setLabel(std::u16string_view label, Context<TextInput> &cx);
    void setPlaceholder(std::u16string_view placeholder, Context<TextInput> &cx);
    void setStyler(Styler styler, Context<TextInput> &cx);
    void setColors(Color caret, Color selection, Context<TextInput> &cx);
    void setCaret(CaretShape shape, bool blinks, Context<TextInput> &cx);
    CaretShape caretShape() const { return m_options.caretShape; }
    void setReadOnly(bool readOnly, Context<TextInput> &cx);
    bool readOnly() const { return m_options.readOnly; }
    // The styler asked again for the current text (in the next frame).
    void restyle(Context<TextInput> &cx);
    // The spans of the current text (the styler's, computed once per text).
    const std::vector<TextSpan> &spans() const;
    // Forgets the undo history (an owner that keeps its own: the editor's undo stack).
    void clearUndo();

    // Editing operations (what keys and the input method do), each one an undo step.
    void replace(Range range, std::u16string_view text, Context<TextInput> &cx);
    void insert(std::u16string_view text, Context<TextInput> &cx);
    void setSelection(int anchor, int focus, Context<TextInput> &cx);
    void moveCaret(int offset, bool extend, Context<TextInput> &cx);
    void deleteBackward(bool word, Context<TextInput> &cx);
    void deleteForward(bool word, Context<TextInput> &cx);
    void selectAll(Context<TextInput> &cx);
    void undo(Context<TextInput> &cx);
    void redo(Context<TextInput> &cx);
    void copy(App &app) const;
    void cut(Context<TextInput> &cx);
    void paste(Context<TextInput> &cx);
    bool handleKey(const Keystroke &key, Window &window, Context<TextInput> &cx);

    // The input method's composition.
    void setMarkedText(std::optional<Range> range, std::u16string_view text, std::optional<Range> selectedWithin,
            Context<TextInput> &cx);
    void unmark(Context<TextInput> &cx);
    // Commits text in place of the composition (or the selection): the composition's undo step.
    void commitText(std::optional<Range> range, std::u16string_view text, Context<TextInput> &cx);
    // An assistive technology's action (focus, set the text, replace a range, select).
    bool performAccessibilityAction(const AccessibilityActionRequest &request, Window &window, Context<TextInput> &cx);

    // Where things are (window logical px), from the last frame.
    std::optional<Rect> boundsForRange(Range range) const;
    std::optional<int> offsetForPoint(Point point) const;
    std::optional<Rect> bounds() const { return m_layout ? std::optional<Rect>(m_bounds) : std::nullopt; }
    // The text as laid out in the last frame (nullptr before the first).
    const TextLayout *layout() const { return m_layout.get(); }
    bool caretVisible() const { return m_caretVisible; }

    // Set by the element when it is laid out and painted.
    void setLaidOut(std::shared_ptr<const TextLayout> layout, Rect bounds, float fontSize = 0);
    // Whether the caret is to be scrolled into view in this frame (focused, and the focus, caret,
    // text or window size changed since the last frame); remembers the state.
    bool revealWanted(bool focused, Size viewport);
    // The caret's shape on the last frame's layout (window px): the bar, the block or the line
    // (tests, the owner).
    std::optional<Rect> caretBox() const;
    // An inline object's press being followed (its range, the press), set by the element.
    struct ObjectPress {
        Range range;
        MouseDownEvent down;
    };
    std::optional<ObjectPress> &objectPress() { return m_objectPress; }

private:
    struct Snapshot {
        std::u16string text;
        InputHandler::Selection selection;
    };
    void pushUndo();
    void changed(Context<TextInput> &cx);
    void selectionMoved(Context<TextInput> &cx);
    void restartBlink(Context<TextInput> &cx);
    void scheduleBlink(Context<TextInput> &cx);
    int clampOffset(int offset) const;
    std::u16string normalized(std::u16string_view text) const;

    Options m_options;
    std::u16string m_text;
    InputHandler::Selection m_selection;
    std::optional<Range> m_marked;
    FocusHandle m_focus;
    Styler m_styler;
    mutable std::vector<TextSpan> m_spans; // the styler's, for m_spansRevision's text
    std::uint64_t m_textRevision = 1;
    mutable std::uint64_t m_spansRevision = 0;
    std::vector<Snapshot> m_undo;
    std::vector<Snapshot> m_redo;
    bool m_coalesceTyping = false;
    bool m_compositionUndo = false; // the composition pushed the undo step it belongs to
    bool m_caretVisible = true;
    bool m_blinking = false;
    Subscription m_blink;
    float m_preferredX = -1; // for ↑/↓ across lines of different widths
    std::shared_ptr<const TextLayout> m_layout;
    Point m_origin;
    Rect m_bounds;
    float m_fontSize = 0; // the field's own (a block caret at a line's end is half of it wide)
    bool m_dragging = false;
    int m_dragAnchor = 0;
    struct RevealKey {
        bool focused = false;
        int caret = 0;
        std::size_t length = 0;
        Size viewport;
        bool operator==(const RevealKey &) const = default;
    };
    RevealKey m_revealKey;
    std::optional<ObjectPress> m_objectPress;
    // The previous / next caret stop (an inline object one unit), by the last layout.
    int previousStop(int offset) const;
    int nextStop(int offset) const;
};

} // namespace brulion::atrament
