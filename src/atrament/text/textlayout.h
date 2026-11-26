#pragma once

// A paragraph shaped and wrapped (docs/ATRAMENT.md "Text"), and every question an editor asks of
// it. Positions are logical pixels from the layout's top-left corner; offsets are UTF-16 code units
// of the paragraph's text.
//
// Shaping: the text is split into runs of one face — the requested face where it has the
// character, else the fallback (FontCollection::faceFor), whole grapheme clusters at a time — and
// each run is shaped by HarfBuzz (script and direction guessed per run: left to right for now,
// bidirectional text later; language from the run). The whole paragraph is shaped once; wrapping
// at another width reuses the shaping (a ligature or kerning across a line break stays as shaped,
// as in GPUI).
//
// Wrapping: lines end at mandatory breaks and, past the wrap width, at the last allowed break
// (segmentation.h) that fits; spaces at a line's end hang (they do not count for its width); a
// word longer than the width breaks at a grapheme boundary. A paragraph ending with a line break
// has an empty last line (where a caret goes after Enter).
//
// Lines are as tall as `lineHeight` (logical px) when it is set, else as the tallest run's
// ascent + descent + line gap; the baseline sits so that the extra height is shared above and
// below (CSS's half-leading).

#include "base/color.h"
#include "base/geometry.h"
#include "text/fonts.h"

#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace brulion::atrament {

// A run of one font over the paragraph's text, in order; their lengths add up to the text's.
struct FontRun {
    int length = 0; // UTF-16 code units
    FaceId face = kNoFace;
    float fontSize = 14; // logical px
    // Free for the caller (an index into its style runs): every glyph carries it, so the painter
    // knows its colour, underline, …
    std::uint32_t styleIndex = 0;
    // OpenType features as HarfBuzz parses them ("tnum", "-liga", "ss01=1"), empty for defaults.
    std::u16string features;
};

struct PositionedGlyph {
    FaceId face = kNoFace;
    float fontSize = 0;
    std::uint32_t glyph = 0;
    Point position; // the pen position on the baseline, from the layout's top-left
    std::uint32_t styleIndex = 0;
    int cluster = 0; // the UTF-16 offset the glyph came from
};

struct TextLine {
    int start = 0; // first code unit
    int end = 0; // one past the last code unit shown, before a mandatory break's characters
    int next = 0; // where the next line starts (after the break's characters / hanging spaces)
    float top = 0;
    float height = 0;
    float baseline = 0; // from the layout's top
    float ascent = 0;
    float descent = 0;
    float width = 0; // without hanging spaces
    std::uint32_t firstGlyph = 0; // into glyphs()
    std::uint32_t glyphCount = 0;
};

enum class CaretAffinity : std::uint8_t { Downstream, Upstream };

class TextLayout {
public:
    const std::u16string &text() const { return m_text; }
    int length() const { return int(m_text.size()); }
    std::optional<float> wrapWidth() const { return m_wrapWidth; }
    Size size() const { return m_size; }
    const std::vector<TextLine> &lines() const { return m_lines; }
    const std::vector<PositionedGlyph> &glyphs() const { return m_glyphs; }
    // The widest unbreakable piece (CSS min-content) and the width unwrapped (max-content).
    float minContentWidth() const { return m_minContentWidth; }
    float maxContentWidth() const { return m_maxContentWidth; }

    // The line an offset is on; at a wrap point Downstream gives the next line, Upstream this one.
    int lineIndex(int offset, CaretAffinity affinity = CaretAffinity::Downstream) const;
    // x of the caret before `offset` on its line (offsets inside a grapheme cluster or ligature
    // are spread over its advance by graphemes).
    float caretX(int offset, CaretAffinity affinity = CaretAffinity::Downstream) const;
    // The caret: a 0-wide rectangle as tall as its line.
    Rect caretRect(int offset, CaretAffinity affinity = CaretAffinity::Downstream) const;
    // The nearest grapheme boundary to a point (lines above the first count as the first, below
    // the last as the last), and the affinity a caret placed there should have.
    int offsetAt(Point p, CaretAffinity *affinity = nullptr) const;
    // One rectangle per line the range touches, each as tall as its line; a selection that
    // goes on past a line's end covers its hanging spaces and a space's width for the break.
    std::vector<Rect> selectionRects(Range range) const;
    // The offset on the line above / below at the same x (keyboard ↑/↓); -1 when there is none.
    int offsetAbove(int offset, float x) const;
    int offsetBelow(int offset, float x) const;

    // Built by layoutParagraph() (and the text system's cache).
    struct Builder;

private:
    friend struct Builder;
    std::u16string m_text;
    std::optional<float> m_wrapWidth;
    Size m_size;
    float m_minContentWidth = 0;
    float m_maxContentWidth = 0;
    std::vector<TextLine> m_lines;
    std::vector<PositionedGlyph> m_glyphs;
    // x of each offset 0..length on the unwrapped shaped text (caret positions before wrapping).
    std::vector<float> m_offsetX;
};

struct ParagraphStyle {
    std::optional<float> wrapWidth; // none: one line per mandatory break
    float lineHeight = 0; // logical px; 0: the fonts' own
};

class FontCollection;

// Shapes `text` with `runs` and wraps it. Runs with kNoFace, or text past their total length,
// use the first face of the collection. Never fails: missing glyphs are shaped as .notdef.
TextLayout layoutParagraph(const FontCollection &fonts, std::u16string_view text, std::span<const FontRun> runs,
        const ParagraphStyle &style);

// Wraps an existing layout at another width without shaping again.
TextLayout rewrapParagraph(const FontCollection &fonts, const TextLayout &layout, std::span<const FontRun> runs,
        const ParagraphStyle &style);

} // namespace brulion::atrament
