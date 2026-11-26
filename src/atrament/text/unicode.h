#pragma once

// Character properties for segmentation and layout (internal to the text area). The general
// category and the script come from HarfBuzz's Unicode functions (its UCD tables); what HarfBuzz
// does not expose — grapheme break classes, Extended_Pictographic, line break classes — is
// derived from them plus small hand-written tables, each marked where it approximates Unicode's
// data (docs/ATRAMENT.md D8: generated tables later).

#include <hb.h>

#include <cstdint>
#include <string_view>

namespace brulion::atrament::unicode {

hb_unicode_general_category_t generalCategory(char32_t c);
hb_script_t script(char32_t c);

inline bool isHighSurrogate(char16_t c)
{
    return c >= 0xD800 && c <= 0xDBFF;
}
inline bool isLowSurrogate(char16_t c)
{
    return c >= 0xDC00 && c <= 0xDFFF;
}

// The code point ending at `i` (a lone surrogate reads as itself); `i` moves to its start.
char32_t codepointBefore(std::u16string_view text, int &i);

// UAX #29 Grapheme_Cluster_Break.
enum class GraphemeBreak : std::uint8_t {
    Other,
    CR,
    LF,
    Control,
    Extend,
    ZWJ,
    RegionalIndicator,
    Prepend,
    SpacingMark,
    L,
    V,
    T,
    LV,
    LVT,
};
GraphemeBreak graphemeBreak(char32_t c);

// Extended_Pictographic (emoji-data.txt), as ranges copied from Unicode 15.1.
bool isExtendedPictographic(char32_t c);

bool isLetter(hb_unicode_general_category_t gc);
bool isMark(hb_unicode_general_category_t gc);
bool isNumber(hb_unicode_general_category_t gc);

// White space that ends a word and hangs at a line's end, with a break opportunity after it:
// U+0020, the tab and the breaking spaces of Zs (not the no-break U+00A0, U+2007, U+202F).
bool isHangingSpace(char32_t c);
// The characters after which a line must end (UAX #14 BK, CR, LF, NL).
bool isMandatoryBreak(char32_t c);
// Han ideographs, kana and Hangul syllables (UAX #14 ID, H2, H3 — approximated by blocks).
bool isIdeographic(char32_t c);
// Shown as no glyph in layout: C0/C1 controls (but the tab) and the line/paragraph separators.
bool isInvisibleControl(char32_t c);

} // namespace brulion::atrament::unicode
