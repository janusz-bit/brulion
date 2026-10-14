// Internal text helpers of the index module: NFC, Unicode code points, name comparison,
// whole-token search in folded text and excerpts. Not part of the public API.
#pragma once

#include <QString>
#include <QStringList>
#include <QStringView>

#include <utility>

namespace brulion::index_detail {

/// Default excerpt length (UTF-16 code units).
inline constexpr qsizetype ExcerptLength = 120;

/// NFC form of `s` (ASCII fast path).
QString nfc(QStringView s);

/// Code point starting at `i` and its length in UTF-16 units (a lone surrogate is itself).
std::pair<char32_t, qsizetype> codePointAt(QStringView s, qsizetype i);
/// Code point ending right before `i`, or 0 at the start.
char32_t codePointBefore(QStringView s, qsizetype i);

/// A character of a search token: a Unicode letter or number (code point).
bool isTokenChar(char32_t cp);

/// Words of a folded name: runs of token characters ("projekty/dom-jana" -> projekty, dom, jana).
QStringList words(QStringView folded);

/// Fold a user query for suggestions: trim, collapse whitespace, text::foldKey.
QString foldQuery(QStringView query);

/// Compare display names in Polish alphabetical order, the same on every platform (no ICU):
/// case-insensitive, Polish letters after their base letter (l < ł < m, z < ź < ż), other
/// accents ignored (é = e), digit runs by value ("Strona 2" < "Strona 10"); ties are broken by
/// code units so the order is total. Returns <0, 0 or >0.
int compareNames(const QString &a, const QString &b);

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
