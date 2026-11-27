#pragma once

// Text elements (docs/ATRAMENT.md "Text"): a paragraph in the text style inherited from the
// elements around it (`div().textSize(18).textColor(c).child(u"Cześć")`), optionally with spans
// that restyle parts of it (bold, a link's colour, a highlight). Wraps at the width the layout
// gives it unless the style says whitespaceNowrap().
//
// A string converts to a text element by itself: `.child(u"Zażółć gęślą jaźń")`.
//
// Links (hit regions in text, docs/ATRAMENT.md M2 "What is new"): `.link(range, onClick)` makes a
// range of the text clickable — the pointing hand over it, underlined while hovered, a click (or a
// finger's tap) runs `onClick` and goes no further, and a screen reader finds a link node named by
// the range's text, which it can activate. The range keeps the look its spans give it.

#include "base/arena.h"
#include "base/function.h"
#include "text/textlayout.h"
#include "ui/element.h"
#include "ui/events.h"
#include "ui/style.h"
#include "ui/window.h"

#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>

namespace brulion::atrament {

using ArenaU16String = std::basic_string<char16_t, std::char_traits<char16_t>, ArenaAllocator<char16_t>>;

struct TextSpan {
    Range range; // UTF-16 offsets of the element's text
    TextStyleRefinement style;
};

struct TextLink {
    using ClickListener = Fn<void(const ClickEvent &, Window &, App &)>;
    Range range;
    ClickListener onClick;
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
    Text &&link(Range range, TextLink::ClickListener onClick) &&;
    // Shares the layout of every frame with a handle (above).
    Text &&trackLayout(const TextLayoutHandle &handle) &&
    {
        m_layoutHandle = handle.shared();
        return std::move(*this);
    }
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
    std::shared_ptr<const TextLayout> layoutAt(std::optional<float> wrapWidth);

    ArenaU16String m_text;
    ArenaVector<TextSpan> m_spans;
    ArenaVector<TextLink> m_links;
    ArenaVector<HitboxId> m_linkHitboxes;    // per link rectangle
    ArenaVector<std::uint32_t> m_hitboxLink; // the link of each
    ArenaVector<Range> m_pieces;             // the text range of each paint style
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
