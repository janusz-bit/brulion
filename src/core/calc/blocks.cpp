#include "calc/blocks_p.h"

#include "calc/util_p.h"

#include <algorithm>

namespace brulion::calc {

namespace {

/// Length of a list marker ("-", "*", "+", "12.", "3)") at the start of `s`, 0 when none.
qsizetype markerLength(QStringView s, qsizetype maxDigits)
{
    if (s.isEmpty())
        return 0;
    if (s.front() == u'-' || s.front() == u'*' || s.front() == u'+')
        return 1;
    qsizetype digits = 0;
    while (digits < s.size() && detail::isAsciiDigit(s.at(digits)))
        ++digits;
    if (digits == 0 || digits > maxDigits || digits >= s.size())
        return 0;
    return s.at(digits) == u'.' || s.at(digits) == u')' ? digits + 1 : 0;
}

/// The first index from `i` on that is not whitespace (QChar::isSpace()).
qsizetype skipSpaces(QStringView line, qsizetype i)
{
    while (i < line.size() && line.at(i).isSpace())
        ++i;
    return i;
}

} // namespace

std::optional<FenceOpen> parseFenceOpen(QStringView line)
{
    qsizetype i = skipSpaces(line, 0);
    // Optional list marker followed by whitespace.
    const qsizetype marker = markerLength(line.mid(i), 1000);
    if (marker > 0 && i + marker < line.size() && line.at(i + marker).isSpace())
        i = skipSpaces(line, i + marker);
    if (i >= line.size() || (line.at(i) != u'`' && line.at(i) != u'~'))
        return std::nullopt;
    const QChar fenceChar = line.at(i);
    qsizetype run = 0;
    while (i + run < line.size() && line.at(i + run) == fenceChar)
        ++run;
    if (run < 3)
        return std::nullopt;
    const QStringView info = line.mid(i + run).trimmed();
    if (fenceChar == u'`' && info.contains(u'`'))
        return std::nullopt;
    qsizetype wordEnd = 0;
    while (wordEnd < info.size() && !info.at(wordEnd).isSpace())
        ++wordEnd;
    return FenceOpen{.marker = fenceChar, .length = run, .indent = i, .lang = info.left(wordEnd).toString().toLower()};
}

bool isFenceClose(QStringView line, const FenceOpen &open)
{
    const QStringView t = line.trimmed();
    if (t.size() < open.length || t.size() < 3)
        return false;
    return std::ranges::all_of(t, [&open](QChar c) { return c == open.marker; });
}

qsizetype listItemPrefixLength(QStringView line)
{
    const qsizetype indent = leadingWhitespace(line).size();
    const qsizetype marker = markerLength(line.mid(indent), 9);
    if (marker == 0)
        return -1;
    const qsizetype gap = leadingWhitespace(line.mid(indent + marker)).size();
    const qsizetype end = indent + marker + gap;
    if (gap == 0 || end >= line.size() || line.at(end).isSpace())
        return -1;
    return end;
}

int columnWidth(QStringView prefix)
{
    int w = 0;
    for (const QChar c : prefix)
        w += c == u'\t' ? 4 - (w % 4) : 1;
    return w;
}

QStringView leadingWhitespace(QStringView line)
{
    qsizetype i = 0;
    while (i < line.size() && (line.at(i) == u' ' || line.at(i) == u'\t'))
        ++i;
    return line.left(i);
}

QStringView dedent(QStringView line, qsizetype n)
{
    return line.mid(std::min(n, leadingWhitespace(line).size()));
}

} // namespace brulion::calc
