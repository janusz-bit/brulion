#pragma once

// Text segmentation for editing and wrapping (docs/ATRAMENT.md D8), on UTF-16:
//
//   grapheme clusters   UAX #29's extended grapheme clusters for what Brulion meets: CR LF, a base
//                       with combining marks (Mn, Mc, Me), Hangul syllable sequences, emoji with
//                       variation selectors (U+FE0E/FE0F), skin-tone modifiers (U+1F3FB–1F3FF),
//                       ZWJ sequences (👨‍👩‍👧), tag sequences (U+E0020–E007F), flags (regional
//                       indicator pairs), keycaps (U+20E3); a surrogate pair is never split
//   words               for Ctrl+arrows and double clicks: runs of letters, marks and digits
//                       (with apostrophes and periods between letters/digits, as UAX #29 WB6/7
//                       and WB11/12), each other non-space character a word of its own; spaces
//                       are not words
//   line breaks         UAX #14's opportunities for Latin, Cyrillic, Greek, CJK and emoji text:
//                       after spaces (the spaces stay on the line they end), after a hyphen or an
//                       en dash between letters, after a slash, before and after CJK ideographs
//                       and kana (not before closing punctuation), around emoji; no break before
//                       closing punctuation (.,;:!?)]}”’») or after opening punctuation ([({“‘«),
//                       none inside a number ("1,5", "10.000"), none at U+00A0 / U+2007 / U+202F
//                       / U+2060 (no-break); a mandatory break after LF, CR, CR LF, U+2028, U+2029
//
// Character properties come from HarfBuzz's Unicode functions (general category, script).

#include <cstdint>
#include <string_view>
#include <vector>

namespace brulion::atrament {

// Reads one code point at `i` (a lone surrogate reads as itself); `i` moves past it.
char32_t readCodepoint(std::u16string_view text, int &i);

// Grapheme cluster boundaries: the start of the next cluster after `offset` (text.size() at the
// end), the start of the cluster before `offset` (0 at the start). `offset` inside a cluster
// moves to that cluster's end / start.
int nextGraphemeBoundary(std::u16string_view text, int offset);
int previousGraphemeBoundary(std::u16string_view text, int offset);
bool isGraphemeBoundary(std::u16string_view text, int offset);

// Word movement as editors do it: the end of the word at or after `offset` (skipping spaces
// first), the start of the word before `offset` (skipping spaces first).
int nextWordEnd(std::u16string_view text, int offset);
int previousWordStart(std::u16string_view text, int offset);
// The word containing `offset` (for a double click); a space run or a punctuation character
// is a range of its own. Gives {start, end}.
struct WordRange {
    int start = 0;
    int end = 0;
};
WordRange wordAt(std::u16string_view text, int offset);

enum class BreakKind : std::uint8_t { None, Allowed, Mandatory };

// For each offset 0..text.size(): may a line end there (before the code unit at that offset)?
// Offset 0 is always None; offset text.size() is Mandatory.
std::vector<BreakKind> lineBreaks(std::u16string_view text);

} // namespace brulion::atrament
