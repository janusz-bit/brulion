#pragma once

// Text elements (docs/ATRAMENT.md "Text"): a paragraph in the text style inherited from the
// elements around it (`div().textSize(18).textColor(c).child(u"Cześć")`), optionally with spans
// that restyle parts of it (bold, a link's colour, a highlight). Wraps at the width the layout
// gives it unless the style says whitespaceNowrap().
//
// A string converts to a text element by itself: `.child(u"Zażółć gęślą jaźń")`.

#include "base/arena.h"
#include "text/textlayout.h"
#include "ui/element.h"
#include "ui/style.h"
#include "ui/window.h"

#include <memory>
#include <span>
#include <string>
#include <string_view>

namespace brulion::atrament {

using ArenaU16String = std::basic_string<char16_t, std::char_traits<char16_t>, ArenaAllocator<char16_t>>;

struct TextSpan {
    Range range; // UTF-16 offsets of the element's text
    TextStyleRefinement style;
};

class Text final : public Element {
public:
    explicit Text(std::u16string_view text);
    // Spans apply in order over the inherited style.
    Text &&spans(std::span<const TextSpan> spans) &&;
    Text &&span(Range range, TextStyleRefinement style) &&;
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

private:
    void buildRuns(Window &window);
    std::shared_ptr<const TextLayout> layoutAt(std::optional<float> wrapWidth);

    ArenaU16String m_text;
    ArenaVector<TextSpan> m_spans;
    ArenaVector<FontRun> m_runs;
    ArenaVector<TextPaintStyle> m_paint;
    ElementId m_id;
    float m_lineHeight = 0;
    TextAlign m_align = TextAlign::Left;
    WhiteSpace m_whiteSpace = WhiteSpace::Normal;
    App *m_app = nullptr;
    std::shared_ptr<const TextLayout> m_layout;
};

inline Text text(std::u16string_view s)
{
    return Text(s);
}

// Resolves a text style to a face of the app's fonts (kNoFace when its family is unknown).
FaceId resolveFace(App &app, const TextStyle &style);

} // namespace brulion::atrament
