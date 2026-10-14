#include "calc/blocks_p.h"

#include "calc/util_p.h"

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

} // namespace

std::optional<FenceOpen> parseFenceOpen(QStringView line)
{
    qsizetype i = 0;
    while (i < line.size() && line.at(i).isSpace())
        ++i;
    // Optional list marker followed by whitespace.
    const qsizetype marker = markerLength(line.mid(i), 1000);
    if (marker > 0 && i + marker < line.size() && line.at(i + marker).isSpace()) {
        qsizetype j = i + marker;
        while (j < line.size() && line.at(j).isSpace())
            ++j;
        i = j;
    }
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
    FenceOpen open;
    open.marker = fenceChar;
    open.length = run;
    open.indent = i;
    open.lang = info.left(wordEnd).toString().toLower();
    return open;
}

bool isFenceClose(QStringView line, const FenceOpen &open)
{
    const QStringView t = line.trimmed();
    if (t.size() < open.length || t.size() < 3)
        return false;
    for (const QChar c : t) {
        if (c != open.marker)
            return false;
    }
    return true;
}

qsizetype listItemPrefixLength(QStringView line)
{
    qsizetype i = 0;
    while (i < line.size() && (line.at(i) == u' ' || line.at(i) == u'\t'))
        ++i;
    const qsizetype marker = markerLength(line.mid(i), 9);
    if (marker == 0)
        return -1;
    qsizetype j = i + marker;
    const qsizetype afterMarker = j;
    while (j < line.size() && (line.at(j) == u' ' || line.at(j) == u'\t'))
        ++j;
    if (j == afterMarker || j >= line.size() || line.at(j).isSpace())
        return -1;
    return j;
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
    qsizetype i = 0;
    while (i < n && i < line.size() && (line.at(i) == u' ' || line.at(i) == u'\t'))
        ++i;
    return line.mid(i);
}

} // namespace brulion::calc
