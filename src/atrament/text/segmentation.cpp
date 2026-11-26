#include "text/segmentation.h"

#include "text/segmentation_p.h"
#include "text/unicode.h"

#include <algorithm>
#include <array>
#include <iterator>

namespace brulion::atrament {

using unicode::GraphemeBreak;

char32_t readCodepoint(std::u16string_view text, int &i)
{
    const int n = int(text.size());
    if (i < 0)
        i = 0;
    if (i >= n) {
        i = n;
        return 0;
    }
    const char16_t c = text[std::size_t(i)];
    ++i;
    if (unicode::isHighSurrogate(c) && i < n) {
        const char16_t low = text[std::size_t(i)];
        if (unicode::isLowSurrogate(low)) {
            ++i;
            return 0x10000 + ((char32_t(c) - 0xD800) << 10) + (char32_t(low) - 0xDC00);
        }
    }
    return c;
}

// ─── Grapheme clusters (UAX #29) ────────────────────────────────────────────────────────────────

namespace {

bool isControlLike(GraphemeBreak b)
{
    return b == GraphemeBreak::Control || b == GraphemeBreak::CR || b == GraphemeBreak::LF;
}

// GB6–GB8: Hangul syllable sequences.
bool hangulJoins(GraphemeBreak a, GraphemeBreak b)
{
    if (a == GraphemeBreak::L)
        return b == GraphemeBreak::L || b == GraphemeBreak::V || b == GraphemeBreak::LV || b == GraphemeBreak::LVT;
    if (a == GraphemeBreak::LV || a == GraphemeBreak::V)
        return b == GraphemeBreak::V || b == GraphemeBreak::T;
    if (a == GraphemeBreak::LVT || a == GraphemeBreak::T)
        return b == GraphemeBreak::T;
    return false;
}

// The rules that need no more than the two code points around a position. Gives true for a
// certain boundary; false when there is none or when it depends on what came before (GB11's
// emoji sequences, GB12/13's regional indicator pairs).
bool certainBoundary(char32_t before, char32_t at)
{
    const GraphemeBreak a = unicode::graphemeBreak(before);
    const GraphemeBreak b = unicode::graphemeBreak(at);
    if (a == GraphemeBreak::CR && b == GraphemeBreak::LF)
        return false;
    if (isControlLike(a) || isControlLike(b))
        return true;
    if (hangulJoins(a, b))
        return false;
    if (b == GraphemeBreak::Extend || b == GraphemeBreak::ZWJ || b == GraphemeBreak::SpacingMark)
        return false;
    if (a == GraphemeBreak::Prepend)
        return false;
    if (a == GraphemeBreak::ZWJ && unicode::isExtendedPictographic(at))
        return false;
    if (a == GraphemeBreak::RegionalIndicator && b == GraphemeBreak::RegionalIndicator)
        return false;
    return true;
}

// A grapheme boundary at or before `pos` from which scanning forward needs no earlier state.
int certainBoundaryAtOrBefore(std::u16string_view text, int pos)
{
    const int n = int(text.size());
    pos = std::clamp(pos, 0, n);
    if (pos == n)
        return n;
    if (pos > 0 && unicode::isLowSurrogate(text[std::size_t(pos)])
        && unicode::isHighSurrogate(text[std::size_t(pos - 1)]))
        --pos;
    while (pos > 0) {
        int j = pos;
        const char32_t at = readCodepoint(text, j);
        int k = pos;
        const char32_t before = unicode::codepointBefore(text, k);
        if (certainBoundary(before, at))
            return pos;
        pos = k;
    }
    return 0;
}

// The boundary after `start`, which must be a boundary (GB3–GB13, GB999).
int nextBoundaryFrom(std::u16string_view text, int start)
{
    const int n = int(text.size());
    if (start >= n)
        return n;
    int i = start;
    char32_t c = readCodepoint(text, i);
    GraphemeBreak prev = unicode::graphemeBreak(c);
    // GB11's state: 1 after ExtPict Extend*, 2 after ExtPict Extend* ZWJ.
    int pictographic = unicode::isExtendedPictographic(c) ? 1 : 0;
    int regionalIndicators = prev == GraphemeBreak::RegionalIndicator ? 1 : 0;
    while (i < n) {
        int j = i;
        c = readCodepoint(text, j);
        const GraphemeBreak cur = unicode::graphemeBreak(c);
        // The common case first: two plain characters.
        if (prev == GraphemeBreak::Other && cur == GraphemeBreak::Other && c < 0xA9)
            return i;
        const bool curPictographic = unicode::isExtendedPictographic(c);
        bool boundary = true;
        if (prev == GraphemeBreak::CR && cur == GraphemeBreak::LF)
            boundary = false;
        else if (isControlLike(prev) || isControlLike(cur))
            boundary = true;
        else if (hangulJoins(prev, cur))
            boundary = false;
        else if (cur == GraphemeBreak::Extend || cur == GraphemeBreak::ZWJ || cur == GraphemeBreak::SpacingMark)
            boundary = false;
        else if (prev == GraphemeBreak::Prepend)
            boundary = false;
        else if (prev == GraphemeBreak::ZWJ && pictographic == 2 && curPictographic)
            boundary = false;
        else if (prev == GraphemeBreak::RegionalIndicator && cur == GraphemeBreak::RegionalIndicator
                 && regionalIndicators % 2 == 1)
            boundary = false;
        if (boundary)
            return i;
        if (curPictographic)
            pictographic = 1;
        else if (cur == GraphemeBreak::Extend && pictographic == 1)
            pictographic = 1;
        else if (cur == GraphemeBreak::ZWJ && pictographic == 1)
            pictographic = 2;
        else
            pictographic = 0;
        regionalIndicators = cur == GraphemeBreak::RegionalIndicator ? regionalIndicators + 1 : 0;
        prev = cur;
        i = j;
    }
    return n;
}

} // namespace

int nextGraphemeBoundary(std::u16string_view text, int offset)
{
    const int n = int(text.size());
    if (offset >= n)
        return n;
    offset = std::max(offset, 0);
    int b = certainBoundaryAtOrBefore(text, offset);
    for (;;) {
        const int next = nextBoundaryFrom(text, b);
        if (next > offset || next >= n)
            return next;
        b = next;
    }
}

int previousGraphemeBoundary(std::u16string_view text, int offset)
{
    const int n = int(text.size());
    if (offset <= 0)
        return 0;
    offset = std::min(offset, n);
    int b = certainBoundaryAtOrBefore(text, offset - 1);
    for (;;) {
        const int next = nextBoundaryFrom(text, b);
        if (next >= offset)
            return b;
        b = next;
    }
}

bool isGraphemeBoundary(std::u16string_view text, int offset)
{
    const int n = int(text.size());
    if (offset <= 0 || offset >= n)
        return offset == 0 || offset == n;
    return nextGraphemeBoundary(text, previousGraphemeBoundary(text, offset)) == offset;
}

void detail::graphemeBoundaries(std::u16string_view text, std::vector<int> &out)
{
    out.clear();
    out.push_back(0);
    const int n = int(text.size());
    int b = 0;
    while (b < n) {
        b = nextBoundaryFrom(text, b);
        out.push_back(b);
    }
}

// ─── Words ──────────────────────────────────────────────────────────────────────────────────────

namespace {

// UAX #29's word classes, reduced: letters (ALetter, Hebrew_Letter, Katakana, ExtendNumLet's
// underscore — approximation: Katakana and the underscore join letters as ALetter does), digits
// (Numeric), MidNumLet and Single_Quote (' ’ ‘ .: between letters or between digits, WB6/7 and
// WB11/12), MidNum (, ;: between digits only), ideographs and hiragana (a word each, as without a
// dictionary), spaces and everything else (a word per character).
enum class WordClass : std::uint8_t { Space, Letter, Digit, MidLetterNum, MidNum, Ideograph, Other };

WordClass wordClassOf(char32_t c)
{
    if (c < 0x80) {
        if (c == ' ' || (c >= 0x09 && c <= 0x0D))
            return WordClass::Space;
        if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_')
            return WordClass::Letter;
        if (c >= '0' && c <= '9')
            return WordClass::Digit;
        if (c == '\'' || c == '.')
            return WordClass::MidLetterNum;
        if (c == ',' || c == ';')
            return WordClass::MidNum;
        return WordClass::Other;
    }
    if (c == 0x2018 || c == 0x2019 || c == 0x2024 || c == 0xFE52 || c == 0xFF07 || c == 0xFF0E)
        return WordClass::MidLetterNum;
    if (c == 0xFF0C || c == 0xFF1B)
        return WordClass::MidNum;
    const hb_unicode_general_category_t gc = unicode::generalCategory(c);
    switch (gc) {
    case HB_UNICODE_GENERAL_CATEGORY_SPACE_SEPARATOR:
    case HB_UNICODE_GENERAL_CATEGORY_LINE_SEPARATOR:
    case HB_UNICODE_GENERAL_CATEGORY_PARAGRAPH_SEPARATOR: return WordClass::Space;
    case HB_UNICODE_GENERAL_CATEGORY_CONTROL: return c == 0x85 ? WordClass::Space : WordClass::Other;
    case HB_UNICODE_GENERAL_CATEGORY_CONNECT_PUNCTUATION: return WordClass::Letter;
    default: break;
    }
    if (unicode::isLetter(gc)) {
        const hb_script_t s = unicode::script(c);
        return s == HB_SCRIPT_HAN || s == HB_SCRIPT_HIRAGANA ? WordClass::Ideograph : WordClass::Letter;
    }
    if (unicode::isMark(gc))
        return WordClass::Letter; // only at the start of a text: a mark joins its base's cluster
    if (unicode::isNumber(gc))
        return WordClass::Digit;
    return WordClass::Other;
}

WordClass wordClassAt(std::u16string_view text, int pos)
{
    return wordClassOf(readCodepoint(text, pos));
}

bool isWordLike(WordClass c)
{
    return c == WordClass::Letter || c == WordClass::Digit || c == WordClass::Ideograph;
}

// The end of the segment (a word, a space run or one other cluster) starting at the boundary
// `start`.
int wordSegmentEnd(std::u16string_view text, int start, WordClass &cls)
{
    const int n = int(text.size());
    cls = wordClassAt(text, start);
    int end = nextGraphemeBoundary(text, start);
    if (cls == WordClass::Space) {
        while (end < n && wordClassAt(text, end) == WordClass::Space)
            end = nextGraphemeBoundary(text, end);
    } else if (cls == WordClass::Letter || cls == WordClass::Digit) {
        WordClass last = cls;
        while (end < n) {
            const WordClass c = wordClassAt(text, end);
            if (c == WordClass::Letter || c == WordClass::Digit) {
                last = c;
                end = nextGraphemeBoundary(text, end);
                continue;
            }
            if (c == WordClass::MidLetterNum || c == WordClass::MidNum) {
                const int after = nextGraphemeBoundary(text, end);
                if (after < n) {
                    const WordClass next = wordClassAt(text, after);
                    const bool joins =
                            (c == WordClass::MidLetterNum && last == WordClass::Letter && next == WordClass::Letter)
                            || (last == WordClass::Digit && next == WordClass::Digit);
                    if (joins) {
                        last = next;
                        end = nextGraphemeBoundary(text, after);
                        continue;
                    }
                }
            }
            break;
        }
    }
    return end;
}

// A segment boundary at or before `pos` (a grapheme boundary where a space run starts or ends,
// or next to a character that is a word of its own).
int certainWordBoundaryAtOrBefore(std::u16string_view text, int pos)
{
    const int n = int(text.size());
    pos = std::clamp(pos, 0, n);
    int g = isGraphemeBoundary(text, pos) ? pos : previousGraphemeBoundary(text, pos);
    while (g > 0 && g < n) {
        const int before = previousGraphemeBoundary(text, g);
        const WordClass at = wordClassAt(text, g);
        const WordClass prev = wordClassAt(text, before);
        if ((at == WordClass::Space) != (prev == WordClass::Space))
            return g;
        if (at == WordClass::Other || at == WordClass::Ideograph || prev == WordClass::Other
            || prev == WordClass::Ideograph)
            return g;
        g = before;
    }
    return g;
}

} // namespace

int nextWordEnd(std::u16string_view text, int offset)
{
    const int n = int(text.size());
    if (offset >= n)
        return n;
    offset = std::max(offset, 0);
    int b = certainWordBoundaryAtOrBefore(text, offset);
    while (b < n) {
        WordClass cls;
        const int e = wordSegmentEnd(text, b, cls);
        if (e > offset && cls != WordClass::Space)
            return e;
        b = e;
    }
    return n;
}

int previousWordStart(std::u16string_view text, int offset)
{
    const int n = int(text.size());
    if (offset <= 0)
        return 0;
    offset = std::min(offset, n);
    int from = certainWordBoundaryAtOrBefore(text, offset - 1);
    for (;;) {
        int best = -1;
        for (int s = from; s < offset;) {
            WordClass cls;
            const int e = wordSegmentEnd(text, s, cls);
            if (cls != WordClass::Space)
                best = s;
            s = e;
        }
        if (best >= 0)
            return best;
        if (from == 0)
            return 0;
        from = certainWordBoundaryAtOrBefore(text, from - 1);
    }
}

WordRange wordAt(std::u16string_view text, int offset)
{
    const int n = int(text.size());
    if (n == 0)
        return {};
    offset = std::clamp(offset, 0, n);
    int t = isGraphemeBoundary(text, offset) ? offset : previousGraphemeBoundary(text, offset);
    // A click just after a word, before a space (on the word's last character's right half), means
    // that word.
    if (t == n || (t > 0 && wordClassAt(text, t) == WordClass::Space)) {
        const int before = previousGraphemeBoundary(text, t);
        if (t == n || isWordLike(wordClassAt(text, before)))
            t = before;
    }
    int b = certainWordBoundaryAtOrBefore(text, t);
    for (;;) {
        WordClass cls;
        const int e = wordSegmentEnd(text, b, cls);
        if (e > t || e >= n)
            return {b, e};
        b = e;
    }
}

// ─── Line breaks (UAX #14, a subset) ────────────────────────────────────────────────────────────

namespace {

// UAX #14 classes, merged where Brulion's rules treat them alike: CL is every "no break before"
// class (CL, CP, IS, EX, NS, IN and CJK closing punctuation), OP every "no break after" one.
enum class LineClass : std::uint8_t {
    AL,     // letters, symbols, everything not below
    Letter, // AL that is a letter or mark (the hyphen rule)
    NU,
    BK, // mandatory break after
    SP, // hanging space
    ZW, // zero width space: a break after
    GL, // no-break glue
    OP,
    CL,
    QU, // ambiguous quotes " ': no break on either side
    HY, // hyphen and en dash: a break after, between letters
    B2, // em dash: a break before and after
    SY, // slash
    BA, // soft hyphen: a break after
    ID, // ideographs, kana, emoji
};

// The characters outside ASCII whose class is not their general category's, sorted: no-break
// glue, the hyphens and dashes, opening (“ ‘ „ ‚ « ‹ and CJK brackets) and closing punctuation
// (” ’ » › … ‼ ⁉, CJK closing brackets, commas and stops, small kana and iteration marks: UAX #14
// NS, which also forbids a break before), the separators.
struct ExplicitClass {
    char32_t c;
    LineClass cls;
};
constexpr ExplicitClass kExplicitClasses[] = {
        {0x0085, LineClass::BK}, {0x00A0, LineClass::GL}, {0x00AB, LineClass::OP}, {0x00AD, LineClass::BA},
        {0x00BB, LineClass::CL}, {0x034F, LineClass::GL}, {0x2007, LineClass::GL}, {0x200B, LineClass::ZW},
        {0x2010, LineClass::HY}, {0x2011, LineClass::GL}, {0x2013, LineClass::HY}, {0x2014, LineClass::B2},
        {0x2018, LineClass::OP}, {0x2019, LineClass::CL}, {0x201A, LineClass::OP}, {0x201C, LineClass::OP},
        {0x201D, LineClass::CL}, {0x201E, LineClass::OP}, {0x2026, LineClass::CL}, {0x2028, LineClass::BK},
        {0x2029, LineClass::BK}, {0x202F, LineClass::GL}, {0x2039, LineClass::OP}, {0x203A, LineClass::CL},
        {0x203C, LineClass::CL}, {0x2049, LineClass::CL}, {0x2060, LineClass::GL}, {0x3001, LineClass::CL},
        {0x3002, LineClass::CL}, {0x3005, LineClass::CL}, {0x3008, LineClass::OP}, {0x3009, LineClass::CL},
        {0x300A, LineClass::OP}, {0x300B, LineClass::CL}, {0x300C, LineClass::OP}, {0x300D, LineClass::CL},
        {0x300E, LineClass::OP}, {0x300F, LineClass::CL}, {0x3010, LineClass::OP}, {0x3011, LineClass::CL},
        {0x3014, LineClass::OP}, {0x3015, LineClass::CL}, {0x3016, LineClass::OP}, {0x3017, LineClass::CL},
        {0x3018, LineClass::OP}, {0x3019, LineClass::CL}, {0x301A, LineClass::OP}, {0x301B, LineClass::CL},
        {0x303B, LineClass::CL}, {0x3041, LineClass::CL}, {0x3043, LineClass::CL}, {0x3045, LineClass::CL},
        {0x3047, LineClass::CL}, {0x3049, LineClass::CL}, {0x3063, LineClass::CL}, {0x3083, LineClass::CL},
        {0x3085, LineClass::CL}, {0x3087, LineClass::CL}, {0x308E, LineClass::CL}, {0x3095, LineClass::CL},
        {0x3096, LineClass::CL}, {0x309B, LineClass::CL}, {0x309C, LineClass::CL}, {0x309D, LineClass::CL},
        {0x309E, LineClass::CL}, {0x30A0, LineClass::CL}, {0x30A1, LineClass::CL}, {0x30A3, LineClass::CL},
        {0x30A5, LineClass::CL}, {0x30A7, LineClass::CL}, {0x30A9, LineClass::CL}, {0x30C3, LineClass::CL},
        {0x30E3, LineClass::CL}, {0x30E5, LineClass::CL}, {0x30E7, LineClass::CL}, {0x30EE, LineClass::CL},
        {0x30F5, LineClass::CL}, {0x30F6, LineClass::CL}, {0x30FB, LineClass::CL}, {0x30FC, LineClass::CL},
        {0x30FD, LineClass::CL}, {0x30FE, LineClass::CL}, {0xFEFF, LineClass::GL}, {0xFF01, LineClass::CL},
        {0xFF08, LineClass::OP}, {0xFF09, LineClass::CL}, {0xFF0C, LineClass::CL}, {0xFF0E, LineClass::CL},
        {0xFF1A, LineClass::CL}, {0xFF1B, LineClass::CL}, {0xFF1F, LineClass::CL}, {0xFF3B, LineClass::OP},
        {0xFF3D, LineClass::CL}, {0xFF5B, LineClass::OP}, {0xFF5D, LineClass::CL}, {0xFF61, LineClass::CL},
        {0xFF64, LineClass::CL},
};
static_assert(std::is_sorted(std::begin(kExplicitClasses), std::end(kExplicitClasses),
                             [](const ExplicitClass &a, const ExplicitClass &b) { return a.c < b.c; }));

constexpr std::array<LineClass, 128> kAsciiClasses = [] {
    std::array<LineClass, 128> t{};
    for (char32_t c = 0; c < 128; ++c) {
        LineClass cls = LineClass::AL;
        if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z'))
            cls = LineClass::Letter;
        else if (c >= '0' && c <= '9')
            cls = LineClass::NU;
        else if (c == ' ' || c == '\t')
            cls = LineClass::SP;
        else if (c >= 0x0A && c <= 0x0D)
            cls = LineClass::BK;
        else if (c == '(' || c == '[' || c == '{')
            cls = LineClass::OP;
        else if (c == ')' || c == ']' || c == '}' || c == '.' || c == ',' || c == ';' || c == ':' || c == '!'
                 || c == '?')
            cls = LineClass::CL;
        else if (c == '"' || c == '\'')
            cls = LineClass::QU;
        else if (c == '-')
            cls = LineClass::HY;
        else if (c == '/')
            cls = LineClass::SY;
        t[c] = cls;
    }
    return t;
}();

LineClass lineClassOf(char32_t c)
{
    if (c < 0x80)
        return kAsciiClasses[c];
    const ExplicitClass *it = std::lower_bound(std::begin(kExplicitClasses), std::end(kExplicitClasses), c,
                                               [](const ExplicitClass &e, char32_t v) { return e.c < v; });
    if (it != std::end(kExplicitClasses) && it->c == c)
        return it->cls;
    if (unicode::isHangingSpace(c))
        return LineClass::SP;
    if (c >= 0x31F0 && c <= 0x31FF) // small katakana (phonetic extensions): NS
        return LineClass::CL;
    if (unicode::isIdeographic(c))
        return LineClass::ID;
    // Emoji (approximation: pictographs from U+1F000 and the dingbats/miscellaneous symbols in
    // U+2600–27BF are ID, as most of them are in UAX #14; regional indicator flags too).
    if ((c >= 0x1F000 || (c >= 0x2600 && c <= 0x27BF)) && unicode::isExtendedPictographic(c))
        return LineClass::ID;
    if (c >= 0x1F1E6 && c <= 0x1F1FF)
        return LineClass::ID;
    const hb_unicode_general_category_t gc = unicode::generalCategory(c);
    if (unicode::isLetter(gc) || unicode::isMark(gc))
        return LineClass::Letter;
    if (gc == HB_UNICODE_GENERAL_CATEGORY_DECIMAL_NUMBER)
        return LineClass::NU;
    return LineClass::AL;
}

} // namespace

void detail::lineBreaks(std::u16string_view text, const std::vector<int> &graphemes, std::vector<BreakKind> &out)
{
    const int n = int(text.size());
    out.assign(std::size_t(n) + 1, BreakKind::None);
    out[std::size_t(n)] = BreakKind::Mandatory;
    if (n == 0 || graphemes.size() < 2)
        return;
    // Classes of the cluster before the position (prev), of the one before that (prevPrev, for
    // "a hyphen between letters"), and of the last cluster that is not a space (LB14: OP SP* ×).
    int i = 0;
    LineClass prev = lineClassOf(readCodepoint(text, i));
    LineClass prevPrev = LineClass::AL;
    LineClass lastNonSpace = prev == LineClass::SP ? LineClass::AL : prev;
    for (std::size_t g = 1; g + 1 < graphemes.size(); ++g) {
        const int pos = graphemes[g];
        int j = pos;
        const LineClass cur = lineClassOf(readCodepoint(text, j));
        BreakKind kind = BreakKind::None;
        if (prev == LineClass::BK)
            kind = BreakKind::Mandatory; // LB4, LB5
        else if (cur == LineClass::BK || cur == LineClass::SP)
            kind = BreakKind::None; // LB6, LB7
        else if (prev == LineClass::ZW)
            kind = BreakKind::Allowed; // LB8
        else if (cur == LineClass::CL || cur == LineClass::SY || cur == LineClass::ZW)
            kind = BreakKind::None; // LB13 (even after spaces), LB7
        else if (prev == LineClass::GL)
            kind = BreakKind::None; // LB12
        else if (prev == LineClass::SP)
            kind = lastNonSpace == LineClass::OP ? BreakKind::None : BreakKind::Allowed; // LB14, LB18
        else if (cur == LineClass::GL)
            kind = BreakKind::None; // LB12a
        else if (prev == LineClass::OP || prev == LineClass::QU || cur == LineClass::QU)
            kind = BreakKind::None; // LB14, LB19
        else if (prev == LineClass::HY)
            kind = prevPrev == LineClass::Letter && cur == LineClass::Letter ? BreakKind::Allowed : BreakKind::None;
        else if (prev == LineClass::B2 || cur == LineClass::B2)
            kind = prev == LineClass::B2 && cur == LineClass::B2 ? BreakKind::None : BreakKind::Allowed; // LB17
        else if (prev == LineClass::SY)
            kind = cur == LineClass::NU ? BreakKind::None : BreakKind::Allowed; // LB25: 1/2
        else if (prev == LineClass::BA)
            kind = BreakKind::Allowed; // LB21
        else if (prev == LineClass::ID || cur == LineClass::ID)
            kind = BreakKind::Allowed; // LB31 around ideographs and emoji
        out[std::size_t(pos)] = kind;
        prevPrev = prev;
        prev = cur;
        if (cur != LineClass::SP)
            lastNonSpace = cur;
    }
}

std::vector<BreakKind> lineBreaks(std::u16string_view text)
{
    std::vector<int> graphemes;
    detail::graphemeBoundaries(text, graphemes);
    std::vector<BreakKind> out;
    detail::lineBreaks(text, graphemes, out);
    return out;
}

} // namespace brulion::atrament
