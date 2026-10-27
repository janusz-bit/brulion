// Text helpers shared by every core module: diacritic folding, Unicode-aware word boundaries,
// EOL-preserving line splitting and a stable content hash.
//
// API overview (namespace brulion::text):
//
//   Folding (case/diacritic-insensitive search and compare)
//     foldDiacritics("Zażółć Łódź") == "Zazolc Lodz"      NFD, drop combining marks, ł/Ł -> l/L
//     foldKey("ŻÓŁW")               == "zolw"             lower-case + foldDiacritics
//     foldKeyWithMap(s)  -> FoldedText{text, map}         foldKey plus offsets back into `s`
//
//   Words (Polish letters are word characters)
//     isWordChar(QChar / char32_t)                        letter, number, combining mark or '_'
//     isWholeWord(s, start, end), indexOfWholeWord(hay, needle, from, cs)
//     wordStartAssertion() / wordEndAssertion()           PCRE2 look-arounds replacing `\b`
//     wholeWordPattern(literal), regexAlternation(words)  pattern builders
//     unicodeRegex(pattern)                               QRegularExpression with Unicode `\s` `\w` `\b`
//
//   Lines (lossless round trip)
//     splitLines(content) -> QList<Line{text, eol, start}>, joinLines(lines) == content
//     detectEol(content), preferredEol(lines, index)
//
//   Names (Polish alphabetical order, the same on every platform: no ICU)
//     compareNames("Łódź", "Mława") < 0, compareNames("Rozdział 2", "Rozdział 10") < 0
//     nameSortKey(name) + compareNameKeys(...)          the same order with keys computed once
//
//   Hashing
//     fnv1a64(bytes), fnv1a64Hex(bytes) ("cbf29ce484222325" for empty input),
//     contentHash(text) == fnv1a64Hex(text.toUtf8())
//
//   Decoding files of unknown origin: decode.h — decodeText(bytes) (UTF-8 / UTF-16 BOM /
//     windows-1250 or -1252)
//
// Regular expressions and Unicode — verified against Qt 6.11 / PCRE2 10.48 (see tst_text):
// QRegularExpression compiles patterns in UTF mode, so `\p{L}` and friends always work, but
// without QRegularExpression::UseUnicodePropertiesOption (PCRE2_UCP) the shorthands `\b`, `\w`,
// `\d` and `\s` are ASCII-only: `\bżółw\b` does not match "żółw" and `\s` does not match a
// no-break space (common after Polish one-letter words: "o 9", "w piątek"). Either use
// unicodeRegex() (UCP on) or the explicit look-arounds below. Note also that `$` matches before
// a final "\n"; use `\z` for "end of subject". A subject containing a lone UTF-16 surrogate
// never matches at all, which is why the folding functions replace lone surrogates by U+FFFD.
#pragma once

#include <QByteArrayView>
#include <QList>
#include <QRegularExpression>
#include <QString>
#include <QStringList>
#include <QStringView>

namespace brulion::text {

// ---------------------------------------------------------------------------------------------
// Folding
// ---------------------------------------------------------------------------------------------

/// Remove diacritics but keep the case: canonical decomposition (NFD), combining marks dropped,
/// and the letters without a decomposition ł/Ł mapped to l/L. "Zażółć Łódź" -> "Zazolc Lodz".
/// Lone surrogates become U+FFFD.
QString foldDiacritics(QStringView s);

/// Search / comparison key: lower-case, then foldDiacritics(). "ŚRODĘ" -> "srode".
QString foldKey(QStringView s);

/// Folded text with a map back to the original string.
struct FoldedText {
    QString text;
    /// map[i] = offset in the original string of the character that produced folded unit i;
    /// map[text.size()] = length of the original string (so ranges map with map[start]/map[end]).
    QList<qsizetype> map;

    /// Original offset for a folded offset in [0, text.size()] (clamped).
    qsizetype toOriginal(qsizetype foldedOffset) const;
};

/// foldKey() keeping an offset map, folding one code point at a time (combining marks fold to
/// nothing and so belong to the preceding character). `result.text == foldKey(s)`.
FoldedText foldKeyWithMap(QStringView s);

// ---------------------------------------------------------------------------------------------
// Words
// ---------------------------------------------------------------------------------------------

/// A character that belongs to a word: any Unicode letter (incl. ąćęłńóśźż), number,
/// combining mark, or '_'. Surrogate halves are not word characters (use the char32_t overload).
bool isWordChar(QChar c);
/// Code-point version of isWordChar().
bool isWordChar(char32_t codePoint);

/// True when [start, end) of `s` is a whole word: no word character directly before `start` or
/// at `end` (surrogate-pair aware).
bool isWholeWord(QStringView s, qsizetype start, qsizetype end);

/// Index of the first whole-word occurrence of `needle` in `haystack` at or after `from`,
/// or -1. An empty needle never matches.
qsizetype indexOfWholeWord(QStringView haystack, QStringView needle, qsizetype from = 0,
                           Qt::CaseSensitivity cs = Qt::CaseSensitive);

/// PCRE2 look-behind asserting that no word character (see isWordChar) precedes the position:
/// `(?<![\p{L}\p{N}\p{M}_])`. Works with or without UseUnicodePropertiesOption.
QString wordStartAssertion();
/// PCRE2 look-ahead asserting that no word character follows the position.
QString wordEndAssertion();
/// `literal` escaped and wrapped in word assertions: a Unicode-safe `\bliteral\b`.
QString wholeWordPattern(QStringView literal);

/// Regex alternation of escaped `words`, longest first (then alphabetical) so that a prefix
/// never shadows a longer word; duplicates and empty words are dropped. {"a","ab"} -> "ab|a".
QString regexAlternation(const QStringList &words);

/// QRegularExpression with UseUnicodePropertiesOption added (Unicode `\s`, `\w`, `\b`, `\d`),
/// already optimized (compiled). An invalid pattern is a programming error (Q_ASSERT).
QRegularExpression unicodeRegex(const QString &pattern, QRegularExpression::PatternOptions options = {});

// ---------------------------------------------------------------------------------------------
// Lines
// ---------------------------------------------------------------------------------------------

/// One line of a document and the terminator that followed it.
struct Line {
    QString text;       ///< without the terminator
    QString eol;        ///< "\n", "\r\n", "\r" or "" (only the last line has "")
    qsizetype start = 0; ///< offset of the line's first character in the document

    friend bool operator==(const Line &, const Line &) = default;
};

/// Split `content` into lines remembering each terminator exactly. Line endings are "\n",
/// "\r\n" and a lone "\r" (as in CommonMark). Always returns at least one line; a trailing
/// terminator yields a final empty line. joinLines(splitLines(s)) == s for every s.
QList<Line> splitLines(QStringView content);

/// Inverse of splitLines(): concatenates text + eol of every line.
QString joinLines(const QList<Line> &lines);

/// The first line terminator used in `content`, or "\n" when it has none.
QString detectEol(QStringView content);

/// Terminator for a line inserted next to line `index`: that line's own terminator, else the
/// first terminator used in the document, else "\n".
QString preferredEol(const QList<Line> &lines, qsizetype index);

// ---------------------------------------------------------------------------------------------
// Names
// ---------------------------------------------------------------------------------------------

/// Compare display names (page titles, folders, tags) in Polish alphabetical order, the same on
/// every platform (no ICU, so also in the browser): case-insensitive, Polish letters after their
/// base letter (l < ł < m, z < ź < ż), other accents ignored (é = e), digit runs by value
/// ("Rozdział 2" < "Rozdział 10", leading zeros ignored), NFC; ties are broken by code units so
/// the order is total. Returns <0, 0 or >0 (0 only for identical strings).
int compareNames(const QString &a, const QString &b);

/// The part of compareNames() that depends on one name only (NFC, lower case): sorts compute it
/// once per name and compare with compareNameKeys().
QString nameSortKey(const QString &name);
/// compareNames(a, b) given keyA = nameSortKey(a) and keyB = nameSortKey(b).
int compareNameKeys(const QString &keyA, const QString &a, const QString &keyB, const QString &b);

// ---------------------------------------------------------------------------------------------
// Hashing
// ---------------------------------------------------------------------------------------------

/// FNV-1a 64-bit hash of raw bytes (stable, non-cryptographic).
quint64 fnv1a64(QByteArrayView data);
/// fnv1a64() as 16 lower-case hex digits.
QString fnv1a64Hex(QByteArrayView data);
/// Hash of a text over its UTF-8 bytes: fnv1a64Hex(text.toUtf8()).
QString contentHash(QStringView text);

} // namespace brulion::text
