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

#include "ui/app.h"
#include "ui/element.h"
#include "ui/platform.h"
#include "ui/style.h"
#include "ui/window.h"

#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace brulion::atrament {

class TextLayout;

struct TextInputOptions {
    std::u16string placeholder;
    bool multiline = false;
    InputHandler::Purpose purpose = InputHandler::Purpose::Text;
    std::u16string text;
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

    explicit TextInput(Context<TextInput> &cx);
    TextInput(Context<TextInput> &cx, Options options);
    AnyElement render(Window &window, Context<TextInput> &cx);

    const std::u16string &text() const { return m_text; }
    void setText(std::u16string_view text, Context<TextInput> &cx);
    InputHandler::Selection selection() const { return m_selection; }
    std::optional<Range> markedRange() const { return m_marked; }
    const FocusHandle &focusHandle() const { return m_focus; }
    bool multiline() const { return m_options.multiline; }

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

    // Where things are (window logical px), from the last frame.
    std::optional<Rect> boundsForRange(Range range) const;
    std::optional<int> offsetForPoint(Point point) const;
    bool caretVisible() const { return m_caretVisible; }

    // Set by the element when it is laid out and painted.
    void setLaidOut(std::shared_ptr<const TextLayout> layout, Point origin);

private:
    struct Snapshot {
        std::u16string text;
        InputHandler::Selection selection;
    };
    void pushUndo();
    void changed(Context<TextInput> &cx);
    void restartBlink(Context<TextInput> &cx);
    void scheduleBlink(Context<TextInput> &cx);
    int clampOffset(int offset) const;

    Options m_options;
    std::u16string m_text;
    InputHandler::Selection m_selection;
    std::optional<Range> m_marked;
    FocusHandle m_focus;
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
    bool m_dragging = false;
    int m_dragAnchor = 0;
};

} // namespace brulion::atrament
