#include "text/find.h"

#include "text/text.h"

#include <algorithm>

namespace brulion::text {

namespace {

constexpr char32_t ReplacementChar = 0xFFFD;

/// The code point at `i` (moved past it); a lone surrogate reads as U+FFFD.
char32_t nextCodePoint(QStringView s, qsizetype &i)
{
    const QChar c = s.at(i++);
    if (c.isHighSurrogate() && i < s.size() && s.at(i).isLowSurrogate())
        return QChar::surrogateToUcs4(c, s.at(i++));
    return c.isSurrogate() ? ReplacementChar : char32_t(c.unicode());
}

void appendUcs4(QString &out, char32_t cp)
{
    if (QChar::requiresSurrogates(cp)) {
        out += QChar(QChar::highSurrogate(cp));
        out += QChar(QChar::lowSurrogate(cp));
    } else {
        out += QChar(char16_t(cp));
    }
}

/// Keeps diacritics: one piece per letter with its combining marks, composed (so "ą" and "a" +
/// U+0328 are the same letter), lower-cased unless `matchCase`. Every unit of a piece maps to the
/// piece's start, so a match can only begin and end at a whole letter (onBoundary()).
FoldedText foldKeepingDiacritics(QStringView s, bool matchCase)
{
    FoldedText out;
    out.text.reserve(s.size());
    out.map.reserve(s.size() + 1);
    QString piece;
    for (qsizetype i = 0; i < s.size();) {
        const qsizetype start = i;
        const char32_t base = nextCodePoint(s, i);
        piece.clear();
        appendUcs4(piece, matchCase ? base : QChar::toLower(base));
        bool marks = false;
        while (i < s.size()) {
            qsizetype after = i;
            const char32_t mark = nextCodePoint(s, after);
            if (!QChar::isMark(mark))
                break;
            appendUcs4(piece, mark);
            marks = true;
            i = after;
        }
        if (marks)
            piece = std::move(piece).normalized(QString::NormalizationForm_C);
        out.text += piece;
        out.map.insert(out.map.size(), piece.size(), start);
    }
    out.map.append(s.size());
    return out;
}

FoldedText folded(QStringView s, const FindOptions &options)
{
    FoldedText out = options.matchDiacritics ? foldKeepingDiacritics(s, options.matchCase)
                     : options.matchCase     ? foldDiacriticsWithMap(s)
                                             : foldKeyWithMap(s);
    // Same length: the map stays right.
    for (QChar &c : out.text) {
        const char16_t u = c.unicode();
        if (u == 0x00A0 || u == 0x2007 || u == 0x202F)
            c = u' ';
    }
    return out;
}

/// `at` lies between two characters of the original, not inside what one character folded to.
bool onBoundary(const FoldedText &f, qsizetype at)
{
    return at <= 0 || at >= f.text.size() || f.map.at(at) != f.map.at(at - 1);
}

/// The last place of the two characters `a`, `b` in [from, to) of `text`, -1 when none.
qsizetype lastPair(QStringView text, qsizetype from, qsizetype to, QChar a, QChar b)
{
    for (qsizetype i = to - 2; i >= from; --i) {
        if (text.at(i) == a && text.at(i + 1) == b)
            return i;
    }
    return -1;
}

qsizetype firstChar(QStringView text, qsizetype from, qsizetype to, QChar c)
{
    for (qsizetype i = from; i < to; ++i) {
        if (text.at(i) == c)
            return i;
    }
    return -1;
}

} // namespace

bool isInsideLink(QStringView text, const TextRange &range)
{
    constexpr qsizetype Reach = 4096;
    const auto lineBreak = [](QChar c) { return c == u'\n' || c == u'\r'; };
    qsizetype lineStart = range.start;
    while (lineStart > 0 && range.start - lineStart < Reach && !lineBreak(text.at(lineStart - 1)))
        --lineStart;
    qsizetype lineEnd = range.end();
    while (lineEnd < text.size() && lineEnd - range.end() < Reach && !lineBreak(text.at(lineEnd)))
        ++lineEnd;
    const qsizetype wiki = lastPair(text, lineStart, range.start, u'[', u'[');
    if (wiki >= 0 && lastPair(text, wiki, range.start, u']', u']') < 0
        && lastPair(text, range.end(), lineEnd, u']', u']') >= 0)
        return true;
    const qsizetype target = lastPair(text, lineStart, range.start, u']', u'(');
    return target >= 0 && firstChar(text, target, range.start, u')') < 0
           && firstChar(text, range.end(), lineEnd, u')') >= 0;
}

QList<TextRange> findMatches(QStringView text, QStringView query, const FindOptions &options, qsizetype maxHits)
{
    QList<TextRange> out;
    if (text.isEmpty() || query.isEmpty() || maxHits == 0 || query.contains(u'\n') || query.contains(u'\r'))
        return out;
    const QString needle = folded(query, options).text;
    if (needle.isEmpty())
        return out;
    const FoldedText hay = folded(text, options);
    for (qsizetype at = hay.text.indexOf(needle); at >= 0;) {
        const qsizetype end = at + needle.size();
        if (onBoundary(hay, at) && onBoundary(hay, end)) {
            const qsizetype start = hay.toOriginal(at);
            const qsizetype stop = hay.toOriginal(end);
            if (!options.wholeWord || isWholeWord(text, start, stop)) {
                out.append({start, stop - start});
                if (maxHits > 0 && out.size() >= maxHits)
                    break;
                at = hay.text.indexOf(needle, end);
                continue;
            }
        }
        at = hay.text.indexOf(needle, at + 1);
    }
    return out;
}

QString replaceMatches(QStringView text, const QList<TextRange> &ranges, QStringView replacement)
{
    QString out;
    out.reserve(text.size());
    qsizetype done = 0;
    for (const TextRange &range : ranges) {
        if (range.start < done || range.length < 0 || range.end() > text.size())
            continue;
        out.append(text.sliced(done, range.start - done));
        out.append(replacement);
        done = range.end();
    }
    out.append(text.sliced(done));
    return out;
}

} // namespace brulion::text
