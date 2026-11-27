#pragma once

// Text elements (docs/ATRAMENT.md "Text"): a paragraph in the text style inherited from the
// elements around it (`div().textSize(18).textColor(c).child(u"Cześć")`), optionally with spans
// that restyle parts of it (bold, a link's colour, a highlight). Wraps at the width the layout
// gives it unless the style says whitespaceNowrap().
//
// A string converts to a text element by itself: `.child(u"Zażółć gęślą jaźń")`.
//
// Aligned right or centred (`textRight()`, `textCenter()`), each line goes where the paragraph's
// layout places it in the element's box (TextLayout "Alignment"): its links' hit regions, its
// inline objects, its layout handle's offsets and its nodes for screen readers with it.
//
// Links (hit regions in text, docs/ATRAMENT.md M2 "What is new"): `.link(range, onClick)` makes a
// range of the text clickable — the pointing hand over it, underlined while hovered, a click (or a
// finger's tap) runs `onClick` and goes no further, and a screen reader finds a link node named by
// the range's text, which it can activate. The range keeps the look its spans give it.
//
// Background shapes (docs/ATRAMENT.md "Text"): a span's `style.background` (ui/style.h,
// TextBackground) draws a pill or a rounded box under its glyphs, one per line it shows on; spans
// side by side with equal backgrounds are one shape.
//
// Inline objects: `.inlineObject(range, size, painter)` lays a range of the text out as one box of
// `size` — an icon over the no-break spaces reserved for it, a checkbox at U+FFFC — instead of its
// glyphs: the line reserves its width, ascent and descent, it wraps as one unit, carets and hit
// tests treat it as one cluster, and `painter` draws it in paint with its bounds in window
// coordinates (after the text: over the background shapes). An object is a span (`TextSpan::object`):
// it applies its style too, and spans that carry objects can come from anywhere spans do.

#include "base/arena.h"
#include "base/function.h"
#include "text/textlayout.h"
#include "ui/element.h"
#include "ui/events.h"
#include "ui/style.h"
#include "ui/window.h"

#include <functional>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>

namespace brulion::atrament {

using ArenaU16String = std::basic_string<char16_t, std::char_traits<char16_t>, ArenaAllocator<char16_t>>;

// The size of an inline object: logical px, or ems of the font size of the text there (`em`: an
// icon that grows with a heading's text).
struct InlineObjectSize {
    float width = 0;
    float ascent = 0; // above the baseline
    float descent = 0; // below it
    bool em = false;
};

// What an inline object's painter is told.
struct InlineObjectPaint {
    Rect bounds; // the object's box in window coordinates (logical px)
    float baseline = 0; // the line's baseline, window y
    Range range; // its text
    Color color; // the text's colour there
    float fontSize = 0; // the text's size there (px)
};

using InlineObjectPainter = std::function<void(const InlineObjectPaint &, Window &)>;

struct InlineObject {
    InlineObjectSize size;
    InlineObjectPainter paint; // may be empty: the room is reserved, nothing is drawn
    // In a TextInput's text (its styler's spans): the object takes the presses on its box — a
    // click (any button: `up.button`; its box in window px) and a finger held on it (also in the
    // focused field, where a long press is otherwise the platform's selection) — and the field's
    // caret and selection stay where they are. A Text's objects take none (its links do).
    std::function<void(const ClickEvent &, Rect box, Window &)> onClick = {};
    std::function<void(Point at, Rect box, Window &)> onLongPress = {};
};

struct TextSpan {
    Range range; // UTF-16 offsets of the element's text
    TextStyleRefinement style;
    // The range is laid out as this object instead of its glyphs (see above). Objects do not
    // overlap: of two that do, the first given wins.
    std::optional<InlineObject> object = {};
};

struct TextLink {
    using ClickListener = Fn<void(const ClickEvent &, Window &, App &)>;
    // The link's own menu (a chip's, a tag's): a right-click or a finger held on it, at that point
    // — the row's right-click and long press around it do not run.
    using MenuListener = Fn<void(Point, Window &, App &)>;
    Range range;
    ClickListener onClick;
    MenuListener onMenu = {};
};

// The paragraph a Text laid out, shared with code outside the frame (`trackLayout`): where a
// click on the element's row falls in its text, the caret an editor places from ↑/↓ — after the
// frame, the element itself is gone. Filled in prepaint; `frame` is the window's frame count then.
class TextLayoutHandle {
public:
    struct State {
        std::shared_ptr<const TextLayout> layout;
        Rect bounds; // window logical px
        std::uint64_t frame = 0;
    };
    TextLayoutHandle()
        : m_state(std::make_shared<State>())
    {
    }
    const TextLayout *layout() const { return m_state->layout.get(); }
    Rect bounds() const { return m_state->bounds; }
    std::uint64_t frame() const { return m_state->frame; }
    // The text offset nearest to a window point (nullopt before the element was laid out).
    std::optional<int> offsetAt(Point windowPoint) const
    {
        if (!m_state->layout)
            return std::nullopt;
        return m_state->layout->offsetAt(windowPoint - m_state->bounds.origin);
    }
    std::shared_ptr<State> shared() const { return m_state; }

private:
    std::shared_ptr<State> m_state;
};

class Text final : public Element {
public:
    explicit Text(std::u16string_view text);
    // Spans apply in order over the inherited style.
    Text &&spans(std::span<const TextSpan> spans) &&;
    Text &&span(Range range, TextStyleRefinement style) &&;
    Text &&link(Range range, TextLink::ClickListener onClick, TextLink::MenuListener onMenu = {}) &&;
    // Shares the layout of every frame with a handle (above).
    Text &&trackLayout(const TextLayoutHandle &handle) &&
    {
        m_layoutHandle = handle.shared();
        return std::move(*this);
    }
    // An inline object over `range` (a span with only an object).
    Text &&inlineObject(Range range, InlineObjectSize size, InlineObjectPainter painter = {}) &&;
    Text &&id(ElementId id) &&
    {
        m_id = id;
        return std::move(*this);
    }

    NodeId requestLayout(Window &window, App &app) override;
    void prepaint(Rect bounds, Window &window, App &app) override;
    void paint(Rect bounds, Window &window, App &app) override;
    ElementId id() const override { return m_id; }

    // The paragraph as laid out for this frame (after prepaint), for elements built on text.
    const TextLayout *layout() const { return m_layout.get(); }
    std::u16string_view text() const { return { m_text.data(), m_text.size() }; }
    // The link under the pointer in the last frame (an index into the links; −1: none) — tests.
    int hoveredLink() const;

private:
    void buildRuns(Window &window);
    // `boxWidth`: the box aligned lines go in (none: the wrap width, else the widest line).
    std::shared_ptr<const TextLayout> layoutAt(std::optional<float> wrapWidth, std::optional<float> boxWidth);
    void paintObjects(Rect bounds, Window &window);

    ArenaU16String m_text;
    ArenaVector<TextSpan> m_spans;
    ArenaVector<TextLink> m_links;
    ArenaVector<HitboxId> m_linkHitboxes;    // per link rectangle
    ArenaVector<std::uint32_t> m_hitboxLink; // the link of each
    ArenaVector<Range> m_pieces;             // the text range of each paint style
    ArenaVector<float> m_pieceSizes;         // the font size of each
    struct Object {
        const InlineObject *object = nullptr; // in m_spans
        Range range;
    };
    ArenaVector<Object> m_objects; // the inline objects laid out, in text order
    struct LinkState;
    LinkState *m_linkState = nullptr;
    ArenaVector<FontRun> m_runs;
    ArenaVector<TextPaintStyle> m_paint;
    ElementId m_id;
    float m_lineHeight = 0;
    TextAlign m_align = TextAlign::Left;
    WhiteSpace m_whiteSpace = WhiteSpace::Normal;
    App *m_app = nullptr;
    std::shared_ptr<const TextLayout> m_layout;
    std::shared_ptr<TextLayoutHandle::State> m_layoutHandle;
};

inline Text text(std::u16string_view s)
{
    return Text(s);
}

// Resolves a text style to a face of the app's fonts (kNoFace when its family is unknown).
FaceId resolveFace(App &app, const TextStyle &style);

} // namespace brulion::atrament
