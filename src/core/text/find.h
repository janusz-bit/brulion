// Finding and replacing literal text (docs/UI.md "Find and replace"): the matcher of the open note's
// find bar, of the notebook-wide replace (notebook/replace.h) and of the rendered blocks' highlights
// (InlineRenderer::withFindHighlight). Part of the text module — see text/text.h.
//
//   findMatches("Żółw i ZOLW", u"zolw")          -> [{0, 4}, {7, 4}]
//   findMatches(s, u"aa", {.matchCase = true})   -> left to right, not overlapping
//   replaceMatches(s, ranges, u"kot")            -> every byte outside the ranges kept
//
// Queries are literal and single-line, never patterns. Ranges are UTF-16 units of the ORIGINAL
// string: folding may change lengths (a letter and its combining marks), and a range never splits a
// surrogate pair or a letter from its marks.
#pragma once

#include <QList>
#include <QString>
#include <QStringView>

namespace brulion::text {

struct FindOptions {
    bool matchCase = false;       ///< off: "ZOLW" finds "żółw"
    bool matchDiacritics = false; ///< off: "gesla" finds "gęślą"; on: "ą" and "a" + U+0328 are one letter
    bool wholeWord = false;       ///< no word character (isWordChar: Polish letters too) next to the match

    friend bool operator==(const FindOptions &, const FindOptions &) = default;
};

/// [start, start + length) of a text, in UTF-16 units.
struct TextRange {
    qsizetype start = 0;
    qsizetype length = 0;

    qsizetype end() const { return start + length; }
    friend bool operator==(const TextRange &, const TextRange &) = default;
};

/// Every occurrence of `query` in `text`, left to right and not overlapping ("aa" in "aaa": one, at
/// 0), at most `maxHits` (< 0: all). Besides what `options` say, a no-break space (U+00A0, U+2007,
/// U+202F) equals a space: Polish notes and our renderer keep one-letter words with the next one.
/// An empty query, one with a line break or one that folds to nothing (only combining marks) finds
/// nothing.
QList<TextRange> findMatches(QStringView text, QStringView query, const FindOptions &options = {},
                             qsizetype maxHits = -1);

/// `text` with `replacement` instead of each of `ranges` (ascending, not overlapping: others are
/// skipped, as are ranges outside the text); everything else byte for byte, CRLF and BOM too.
QString replaceMatches(QStringView text, const QList<TextRange> &ranges, QStringView replacement);

} // namespace brulion::text
