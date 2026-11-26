#include "text/unicode.h"

#include <algorithm>
#include <iterator>

namespace brulion::atrament::unicode {

namespace {

hb_unicode_funcs_t *funcs()
{
    static hb_unicode_funcs_t *const f = hb_unicode_funcs_get_default();
    return f;
}

struct CodeRange {
    char32_t first;
    char32_t last;
};

template<std::size_t N>
bool inRanges(const CodeRange (&ranges)[N], char32_t c)
{
    if (c < ranges[0].first || c > ranges[N - 1].last)
        return false;
    const CodeRange *it = std::upper_bound(std::begin(ranges), std::end(ranges), c,
                                           [](char32_t v, const CodeRange &r) { return v < r.first; });
    return it != std::begin(ranges) && c <= std::prev(it)->last;
}

// Extended_Pictographic from Unicode 15's emoji-data.txt (unchanged in 15.1).
constexpr CodeRange kExtendedPictographic[] = {
        {0x00A9, 0x00A9},   {0x00AE, 0x00AE},   {0x203C, 0x203C},   {0x2049, 0x2049},   {0x2122, 0x2122},
        {0x2139, 0x2139},   {0x2194, 0x2199},   {0x21A9, 0x21AA},   {0x231A, 0x231B},   {0x2328, 0x2328},
        {0x2388, 0x2388},   {0x23CF, 0x23CF},   {0x23E9, 0x23F3},   {0x23F8, 0x23FA},   {0x24C2, 0x24C2},
        {0x25AA, 0x25AB},   {0x25B6, 0x25B6},   {0x25C0, 0x25C0},   {0x25FB, 0x25FE},   {0x2600, 0x2605},
        {0x2607, 0x2612},   {0x2614, 0x2685},   {0x2690, 0x2705},   {0x2708, 0x2712},   {0x2714, 0x2714},
        {0x2716, 0x2716},   {0x271D, 0x271D},   {0x2721, 0x2721},   {0x2728, 0x2728},   {0x2733, 0x2734},
        {0x2744, 0x2744},   {0x2747, 0x2747},   {0x274C, 0x274C},   {0x274E, 0x274E},   {0x2753, 0x2755},
        {0x2757, 0x2757},   {0x2763, 0x2767},   {0x2795, 0x2797},   {0x27A1, 0x27A1},   {0x27B0, 0x27B0},
        {0x27BF, 0x27BF},   {0x2934, 0x2935},   {0x2B05, 0x2B07},   {0x2B1B, 0x2B1C},   {0x2B50, 0x2B50},
        {0x2B55, 0x2B55},   {0x3030, 0x3030},   {0x303D, 0x303D},   {0x3297, 0x3297},   {0x3299, 0x3299},
        {0x1F000, 0x1F0FF}, {0x1F10D, 0x1F10F}, {0x1F12F, 0x1F12F}, {0x1F16C, 0x1F171}, {0x1F17E, 0x1F17F},
        {0x1F18E, 0x1F18E}, {0x1F191, 0x1F19A}, {0x1F1AD, 0x1F1E5}, {0x1F201, 0x1F20F}, {0x1F21A, 0x1F21A},
        {0x1F22F, 0x1F22F}, {0x1F232, 0x1F23A}, {0x1F23C, 0x1F23F}, {0x1F249, 0x1F3FA}, {0x1F400, 0x1F53D},
        {0x1F546, 0x1F64F}, {0x1F680, 0x1F6FF}, {0x1F774, 0x1F77F}, {0x1F7D5, 0x1F7FF}, {0x1F80C, 0x1F80F},
        {0x1F848, 0x1F84F}, {0x1F85A, 0x1F85F}, {0x1F888, 0x1F88F}, {0x1F8AE, 0x1F8FF}, {0x1F90C, 0x1F93A},
        {0x1F93C, 0x1F945}, {0x1F947, 0x1FAFF}, {0x1FC00, 0x1FFFD},
};

// Grapheme_Cluster_Break=Prepend (Unicode 15): Arabic, Syriac and Brahmic prefixed marks.
constexpr CodeRange kPrepend[] = {
        {0x0600, 0x0605},   {0x06DD, 0x06DD},   {0x070F, 0x070F},   {0x0890, 0x0891},   {0x08E2, 0x08E2},
        {0x0D4E, 0x0D4E},   {0x110BD, 0x110BD}, {0x110CD, 0x110CD}, {0x111C2, 0x111C3}, {0x1193F, 0x1193F},
        {0x11941, 0x11941}, {0x11A3A, 0x11A3A}, {0x11A84, 0x11A89}, {0x11D46, 0x11D46}, {0x11F02, 0x11F02},
};

// Line break classes ID, H2 and H3 approximated by whole blocks: CJK radicals and ideographs,
// kana, bopomofo, Hangul (jamo for compatibility, syllables), Yi, CJK compatibility.
constexpr CodeRange kIdeographic[] = {
        {0x2E80, 0x2FFF}, {0x3040, 0x30FF}, {0x3105, 0x312F},   {0x3131, 0x318E},   {0x31A0, 0x31BF},
        {0x31F0, 0x31FF}, {0x3200, 0x33FF}, {0x3400, 0x4DBF},   {0x4E00, 0x9FFF},   {0xA000, 0xA4CF},
        {0xAC00, 0xD7A3}, {0xF900, 0xFAFF}, {0x1F200, 0x1F2FF}, {0x20000, 0x2FFFD}, {0x30000, 0x3FFFD},
};

} // namespace

hb_unicode_general_category_t generalCategory(char32_t c)
{
    return hb_unicode_general_category(funcs(), c);
}

hb_script_t script(char32_t c)
{
    return hb_unicode_script(funcs(), c);
}

char32_t codepointBefore(std::u16string_view text, int &i)
{
    if (i <= 0) {
        i = 0;
        return 0;
    }
    if (i > int(text.size()))
        i = int(text.size());
    const char16_t low = text[std::size_t(i - 1)];
    if (isLowSurrogate(low) && i >= 2) {
        const char16_t high = text[std::size_t(i - 2)];
        if (isHighSurrogate(high)) {
            i -= 2;
            return 0x10000 + ((char32_t(high) - 0xD800) << 10) + (char32_t(low) - 0xDC00);
        }
    }
    --i;
    return low;
}

GraphemeBreak graphemeBreak(char32_t c)
{
    if (c < 0x80) {
        if (c == U'\r')
            return GraphemeBreak::CR;
        if (c == U'\n')
            return GraphemeBreak::LF;
        return c < 0x20 || c == 0x7F ? GraphemeBreak::Control : GraphemeBreak::Other;
    }
    // Hangul syllables and jamo, by their blocks (exact).
    if (c >= 0x1100 && c <= 0x11FF)
        return c <= 0x115F ? GraphemeBreak::L : c <= 0x11A7 ? GraphemeBreak::V : GraphemeBreak::T;
    if (c >= 0xAC00 && c <= 0xD7A3)
        return (c - 0xAC00) % 28 == 0 ? GraphemeBreak::LV : GraphemeBreak::LVT;
    if (c >= 0xA960 && c <= 0xA97C)
        return GraphemeBreak::L;
    if (c >= 0xD7B0 && c <= 0xD7C6)
        return GraphemeBreak::V;
    if (c >= 0xD7CB && c <= 0xD7FB)
        return GraphemeBreak::T;
    if (c == 0x200D)
        return GraphemeBreak::ZWJ;
    // Other_Grapheme_Extend outside Mn/Me: ZWNJ, the halfwidth voiced marks, the emoji skin-tone
    // modifiers (Sk) and the tag characters (Cf).
    if (c == 0x200C || c == 0xFF9E || c == 0xFF9F || (c >= 0x1F3FB && c <= 0x1F3FF) || (c >= 0xE0020 && c <= 0xE007F))
        return GraphemeBreak::Extend;
    if (c >= 0x1F1E6 && c <= 0x1F1FF)
        return GraphemeBreak::RegionalIndicator;
    if (inRanges(kPrepend, c))
        return GraphemeBreak::Prepend;
    if (c == 0x0E33 || c == 0x0EB3) // Thai / Lao sara am (Lo)
        return GraphemeBreak::SpacingMark;
    if (c >= 0xD800 && c <= 0xDFFF) // a lone surrogate
        return GraphemeBreak::Control;
    switch (generalCategory(c)) {
    case HB_UNICODE_GENERAL_CATEGORY_CONTROL:
    case HB_UNICODE_GENERAL_CATEGORY_FORMAT:
    case HB_UNICODE_GENERAL_CATEGORY_LINE_SEPARATOR:
    case HB_UNICODE_GENERAL_CATEGORY_PARAGRAPH_SEPARATOR:
    case HB_UNICODE_GENERAL_CATEGORY_SURROGATE: return GraphemeBreak::Control;
    case HB_UNICODE_GENERAL_CATEGORY_NON_SPACING_MARK:
    case HB_UNICODE_GENERAL_CATEGORY_ENCLOSING_MARK: return GraphemeBreak::Extend;
    // Approximation: every Mc is a SpacingMark, though a few are Grapheme_Extend (U+09BE…).
    // Both forbid a break before them; they differ only inside emoji sequences.
    case HB_UNICODE_GENERAL_CATEGORY_SPACING_MARK: return GraphemeBreak::SpacingMark;
    default: return GraphemeBreak::Other;
    }
}

bool isExtendedPictographic(char32_t c)
{
    return c >= 0xA9 && inRanges(kExtendedPictographic, c);
}

bool isLetter(hb_unicode_general_category_t gc)
{
    switch (gc) {
    case HB_UNICODE_GENERAL_CATEGORY_LOWERCASE_LETTER:
    case HB_UNICODE_GENERAL_CATEGORY_MODIFIER_LETTER:
    case HB_UNICODE_GENERAL_CATEGORY_OTHER_LETTER:
    case HB_UNICODE_GENERAL_CATEGORY_TITLECASE_LETTER:
    case HB_UNICODE_GENERAL_CATEGORY_UPPERCASE_LETTER: return true;
    default: return false;
    }
}

bool isMark(hb_unicode_general_category_t gc)
{
    return gc == HB_UNICODE_GENERAL_CATEGORY_NON_SPACING_MARK || gc == HB_UNICODE_GENERAL_CATEGORY_SPACING_MARK
           || gc == HB_UNICODE_GENERAL_CATEGORY_ENCLOSING_MARK;
}

bool isNumber(hb_unicode_general_category_t gc)
{
    return gc == HB_UNICODE_GENERAL_CATEGORY_DECIMAL_NUMBER || gc == HB_UNICODE_GENERAL_CATEGORY_LETTER_NUMBER
           || gc == HB_UNICODE_GENERAL_CATEGORY_OTHER_NUMBER;
}

bool isHangingSpace(char32_t c)
{
    return c == 0x20 || c == 0x09 || c == 0x1680 || (c >= 0x2000 && c <= 0x2006) || (c >= 0x2008 && c <= 0x200A)
           || c == 0x205F || c == 0x3000;
}

bool isMandatoryBreak(char32_t c)
{
    return (c >= 0x0A && c <= 0x0D) || c == 0x85 || c == 0x2028 || c == 0x2029;
}

bool isIdeographic(char32_t c)
{
    return c >= 0x2E80 && inRanges(kIdeographic, c);
}

bool isInvisibleControl(char32_t c)
{
    return (c < 0x20 && c != 0x09) || (c >= 0x7F && c <= 0x9F) || c == 0x2028 || c == 0x2029;
}

} // namespace brulion::atrament::unicode
