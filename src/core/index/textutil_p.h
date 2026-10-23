// Internal text helpers of the index module: NFC, Unicode code points, name comparison,
// whole-token search in folded text and excerpts. Not part of the public API.
#pragma once

#include "text/text.h"

#include <QString>
#include <QStringList>
#include <QStringView>

#include <utility>

namespace brulion::index_detail {

/// Default excerpt length (UTF-16 code units).
inline constexpr qsizetype ExcerptLength = 120;

/// NFC form of `s` (ASCII fast path).
QString nfc(QStringView s);
/// `s`.toLower() with a fast path for ASCII text (most names and paths; Qt converts every
/// character after the first upper-case one through the Unicode tables).
QString lower(const QString &s);

/// Code point starting at `i` and its length in UTF-16 units (a lone surrogate is itself).
std::pair<char32_t, qsizetype> codePointAt(QStringView s, qsizetype i);
/// Code point ending right before `i`, or 0 at the start.
char32_t codePointBefore(QStringView s, qsizetype i);

/// A character of a search token: a Unicode letter or number (code point).
bool isTokenChar(char32_t cp);

/// Words of a folded name: runs of token characters ("projekty/dom-jana" -> projekty, dom, jana).
QStringList words(QStringView folded);

/// Call `f(QStringView word)` for each word of words(folded), in order, without allocating;
/// stops when `f` returns false. Returns false when stopped.
template<typename F>
bool forEachWord(QStringView folded, F &&f)
{
    qsizetype start = -1;
    for (qsizetype i = 0; i < folded.size();) {
        const auto [cp, len] = codePointAt(folded, i);
        if (isTokenChar(cp)) {
            if (start < 0)
                start = i;
        } else if (start >= 0) {
            if (!f(folded.sliced(start, i - start)))
                return false;
            start = -1;
        }
        i += len;
    }
    return start < 0 || f(folded.sliced(start));
}

/// Fold a user query for suggestions: trim, collapse whitespace, text::foldKey.
QString foldQuery(QStringView query);

/// Polish alphabetical order of display names (text::compareNames(): no ICU, digit runs by
/// value, ties by code units).
inline int compareNames(const QString &a, const QString &b)
{
    return text::compareNames(a, b);
}

/// text::nameSortKey(): the part of compareNames() that depends on one name only.
inline QString nameSortKey(const QString &name)
{
    return text::nameSortKey(name);
}
/// text::compareNameKeys(): compareNames(a, b) given precomputed keys.
inline int compareNameKeys(const QString &keyA, const QString &a, const QString &keyB, const QString &b)
{
    return text::compareNameKeys(keyA, a, keyB, b);
}

/// Index of `term` in folded `hay` as a whole token (no token character on either side), or -1.
qsizetype indexOfToken(QStringView hay, QStringView term, qsizetype from = 0);

/// Index of the first occurrence of `needle` in `hay` that starts a word, or -1.
qsizetype indexOfWordStart(QStringView hay, QStringView needle);

/// Wildcard match of `text` against `pattern`, where `*` stands for any run of characters
/// (everything else is literal, compared by code units). Anchored at both ends, or only at the
/// start when `prefixOnly` (as if the pattern ended with `*`). O(text × pattern) in the worst
/// case — never exponential, unlike a backtracking regular expression built from the pattern
/// (`*a*a*a*…` from a note must not freeze the app).
bool globMatch(QStringView text, QStringView pattern, bool prefixOnly = false);

/// A query's word for "the page containing the query" (`page:this`, `ref:ta`): this, current, ta,
/// ten, bieżąca, bieżący, tutaj — case- and diacritic-insensitive.
bool isThisPageValue(QStringView value);

/// Collapse whitespace runs (tabs, newlines) into single spaces and trim.
QString squash(QStringView s);

/// Excerpt of `line` showing [start, end): the whole squashed line when short, otherwise about
/// `max` characters around the match cut at word boundaries and marked with "…".
QString excerptAround(QStringView line, qsizetype start, qsizetype end, qsizetype max = ExcerptLength);

/// excerptAround() without the line's Markdown container syntax (indentation, `- [ ]`, `## `,
/// `> `) and without a trailing block id (` ^abc123`).
QString lineExcerpt(QStringView line, qsizetype start, qsizetype end, qsizetype max = ExcerptLength);

/// Excerpt of the start of a text (for hits without a located match).
QString excerptStart(QStringView text, qsizetype max = ExcerptLength);

} // namespace brulion::index_detail
