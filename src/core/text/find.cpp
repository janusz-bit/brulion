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

/// Keeps diacritics: one piece per letter with its combining marks, composed (so "ą" and "a" +
/// U+0328 are the same letter), lower-cased unless `matchCase`. Every unit of a piece maps to the
/// piece's start, so a match can only begin and end at a whole letter (onBoundary()).
FoldedText foldKeepingDiacritics(QStringView s, bool matchCase)
{
    FoldedText out;
    out.text.reserve(s.size());
    out.map.reserve(s.size() + 1);
    QString piece; // emptied with resize(0), which keeps its buffer (clear() frees it)
    for (qsizetype i = 0; i < s.size();) {
        const qsizetype start = i;
        const char32_t base = nextCodePoint(s, i);
        piece.resize(0);
        piece.append(QChar::fromUcs4(matchCase ? base : QChar::toLower(base)));
        bool marks = false;
        while (i < s.size()) {
            qsizetype after = i;
            const char32_t mark = nextCodePoint(s, after);
            if (!QChar::isMark(mark))
                break;
            piece.append(QChar::fromUcs4(mark));
            marks = true;
            i = after;
        }
        if (marks)
            piece = piece.normalized(QString::NormalizationForm_C);
        out.text += piece;
        out.map.insert(out.map.size(), piece.size(), start);
    }
    out.map.append(s.size());
    return out;
}

FoldedText folded(QStringView s, FindOptions options)
{
    FoldedText out = options.matchDiacritics ? foldKeepingDiacritics(s, options.matchCase)
                     : options.matchCase     ? foldDiacriticsWithMap(s)
                                             : foldKeyWithMap(s);
    // Same length: the map stays right.
    const auto noBreakSpace = [](QChar c) { return c == u'\u00A0' || c == u'\u2007' || c == u'\u202F'; };
    std::ranges::replace_if(out.text, noBreakSpace, QChar(u' '));
    return out;
}

/// `at` lies between two characters of the original, not inside what one character folded to.
bool onBoundary(const FoldedText &f, qsizetype at)
{
    return at <= 0 || at >= f.text.size() || f.map.at(at) != f.map.at(at - 1);
}

} // namespace

bool isInsideLink(QStringView text, TextRange range)
{
    constexpr qsizetype Reach = 4096;
    const auto lineBreak = [](QChar c) { return c == u'\n' || c == u'\r'; };
    qsizetype lineStart = range.start;
    while (lineStart > 0 && range.start - lineStart < Reach && !lineBreak(text.at(lineStart - 1)))
        --lineStart;
    qsizetype lineEnd = range.end();
    while (lineEnd < text.size() && lineEnd - range.end() < Reach && !lineBreak(text.at(lineEnd)))
        ++lineEnd;
    const QStringView before = text.sliced(lineStart, range.start - lineStart);
    const QStringView after = text.sliced(range.end(), lineEnd - range.end());
    // In [[…]]: opened before the match and not closed since, closed after it.
    const qsizetype wiki = before.lastIndexOf(u"[[");
    if (wiki >= 0 && !before.sliced(wiki).contains(u"]]") && after.contains(u"]]"))
        return true;
    // In the target of [text](…): "](" before the match and no ")" since, a ")" after it.
    const qsizetype target = before.lastIndexOf(u"](");
    return target >= 0 && !before.sliced(target).contains(u')') && after.contains(u')');
}

QList<TextRange> findMatches(QStringView text, QStringView query, FindOptions options, qsizetype maxHits)
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
        const qsizetype start = hay.toOriginal(at);
        const qsizetype stop = hay.toOriginal(end);
        const bool hit =
                onBoundary(hay, at) && onBoundary(hay, end) && (!options.wholeWord || isWholeWord(text, start, stop));
        if (hit) {
            out.append({start, stop - start});
            if (maxHits > 0 && out.size() >= maxHits)
                break;
        }
        at = hay.text.indexOf(needle, hit ? end : at + 1); // after a hit: matches do not overlap
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
