// Internal line, indentation and masking helpers of the markdown module (not part of the API).
//
// Masking replaces regions that must not be parsed (inline code spans, `%%comments%%`,
// `<!-- html comments -->`) by spaces. Masked strings keep the exact UTF-16 length and the line
// breaks of the input, so every offset found in a masked string is valid in the original one.
// Parsers detect syntax on the masked text and read values from the original.
#pragma once

#include "text/text.h"

#include <QList>
#include <QString>
#include <QStringView>

#include <optional>
#include <utility>

namespace brulion::markdown::detail {

/// Tab stop width used to compute visual columns (CommonMark uses 4).
inline constexpr int TabWidth = 4;

/// Half-open character range [first, second).
using Range = std::pair<qsizetype, qsizetype>;

inline bool isSpaceOrTab(QChar c)
{
    return c == u' ' || c == u'\t';
}

inline bool isLineBreak(QChar c)
{
    return c == u'\n' || c == u'\r';
}

inline bool isAsciiDigit(QChar c)
{
    return c >= u'0' && c <= u'9';
}

inline bool isAsciiLetter(QChar c)
{
    return (c >= u'a' && c <= u'z') || (c >= u'A' && c <= u'Z');
}

/// ASCII punctuation (the characters a backslash can escape in CommonMark).
inline bool isAsciiPunct(QChar c)
{
    const char16_t u = c.unicode();
    return (u >= 0x21 && u <= 0x2f) || (u >= 0x3a && u <= 0x40) || (u >= 0x5b && u <= 0x60) || (u >= 0x7b && u <= 0x7e);
}

/// Character at `i`, or U+0000 when out of range (keeps scanners free of bounds checks).
inline QChar charAt(QStringView s, qsizetype i)
{
    return i >= 0 && i < s.size() ? s[i] : QChar();
}

/// Leading whitespace of a line.
struct Indent {
    int col = 0;         ///< visual column of the first non-whitespace character (tabs to tab stops)
    qsizetype chars = 0; ///< number of leading space/tab characters
};

/// Measure the leading spaces and tabs of `s`.
Indent indentOf(QStringView s);

/// Number of leading whitespace characters to drop to remove `cols` visual columns of
/// indentation. A tab that crosses the target column is removed as a whole.
qsizetype charsForColumns(QStringView s, int cols);

/// Visual width (columns) of a whitespace-only string such as "\t  ".
int columnsOf(QStringView whitespace);

/// True when the string contains only spaces and tabs (or nothing).
bool isBlank(QStringView s);

/// Leading spaces/tabs of `s`.
inline QStringView leadingWhitespace(QStringView s)
{
    return s.first(indentOf(s).chars);
}

/// `s` without trailing spaces and tabs.
QStringView trimEndBlanks(QStringView s);

/// Logseq drawer lines: `:LOGBOOK:` (`^[ \t]*:[A-Z][A-Z_-]*:[ \t]*$`) and `:END:`.
bool isDrawerStart(QStringView line);
bool isDrawerEnd(QStringView line);

/// Replace every character except line breaks by a space (keeps UTF-16 length and offsets).
QString blankOut(QStringView s);

/// Options for maskInline().
struct MaskOptions {
    bool comments = true;     ///< mask `%%comments%%`
    bool htmlComments = true; ///< mask `<!-- html comments -->`
};

/// A region found by inlineSpans().
struct InlineSpan {
    enum class Kind { Code, Comment, HtmlComment };
    Kind kind = Kind::Code;
    qsizetype from = 0;
    qsizetype to = 0;
    qsizetype fence = 0; ///< Code: length of the backtick run on each side
};

/// Inline code spans (backtick runs of any length, CommonMark pairing), `%%comments%%` and
/// `<!-- html comments -->` in order. Unclosed comments and unmatched backticks are literal
/// text. Backslash escapes are honoured outside code spans. Amortised linear time.
QList<InlineSpan> inlineSpans(QStringView s, MaskOptions options = {});

/// `s` with its inlineSpans() blanked by spaces (same length, line breaks kept).
QString maskInline(QStringView s, MaskOptions options = {});

/// Opening code fence (``` or ~~~, any indentation) with its info string.
struct FenceOpen {
    QChar ch;
    qsizetype length = 0;
    QString info; ///< trimmed info string
};

/// Parse an opening code fence line, or nullopt. A backtick fence's info may not contain '`'.
std::optional<FenceOpen> fenceOpen(QStringView s);

/// True when `s` closes `open` (same character, at least as long, nothing else on the line).
bool isFenceClose(QStringView s, const FenceOpen &open);

/// Length of the blockquote prefix (`> `, `> > `, `>`) at the start of a line and its depth.
struct QuotePrefix {
    int depth = 0;
    qsizetype length = 0;
};
QuotePrefix quotePrefixOf(QStringView s);

/// Document-level `%%comment%%` ranges in document order. Comments may span lines; `%%` inside
/// fenced code (also fences in list items, quotes and callouts) or inline code does not count;
/// an unclosed `%%` is literal text. Linear time.
QList<Range> docCommentRanges(QStringView content, const QList<text::Line> &lines);

/// Blank `ranges` (sorted, non-overlapping) of `content`, keeping length and line breaks.
QString maskRanges(QStringView content, const QList<Range> &ranges);

/// Decode %XX escapes; runs that are not valid UTF-8 (and malformed escapes) stay as written.
QString safeDecode(QStringView s);

} // namespace brulion::markdown::detail
