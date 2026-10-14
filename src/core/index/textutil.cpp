#include "index/textutil_p.h"

#include "text/text.h"

#include <QChar>

#include <algorithm>
#include <cmath>

namespace brulion::index_detail {

using namespace Qt::StringLiterals;

namespace {

bool isAscii(QStringView s)
{
    return std::all_of(s.begin(), s.end(), [](QChar c) { return c.unicode() < 0x80; });
}

bool isBlank(QChar c)
{
    return c == u' ' || c == u'\t';
}

bool isAsciiDigit(QChar c)
{
    return c >= u'0' && c <= u'9';
}

/// Block id character: [A-Za-z0-9-].
bool isIdChar(QChar c)
{
    return isAsciiDigit(c) || (c >= u'a' && c <= u'z') || (c >= u'A' && c <= u'Z') || c == u'-';
}

/// Primary weight of a lower-case code point in Polish alphabetical order: a < ą < b < c < ć …
/// l < ł … z < ź < ż; other accented letters weigh like their base letter (é = e).
quint32 collationWeight(char32_t cp)
{
    switch (cp) {
    case U'ą': return (U'a' << 2) | 1;
    case U'ć': return (U'c' << 2) | 1;
    case U'ę': return (U'e' << 2) | 1;
    case U'ł': return (U'l' << 2) | 1;
    case U'ń': return (U'n' << 2) | 1;
    case U'ó': return (U'o' << 2) | 1;
    case U'ś': return (U's' << 2) | 1;
    case U'ź': return (U'z' << 2) | 1;
    case U'ż': return (U'z' << 2) | 2;
    default: break;
    }
    if (cp < 0x80 || QChar::isSurrogate(cp))
        return quint32(cp) << 2;
    const QString decomposed = QString::fromUcs4(&cp, 1).normalized(QString::NormalizationForm_D);
    const char32_t base = decomposed.isEmpty() || decomposed[0].isSurrogate() ? cp : decomposed[0].unicode();
    return quint32(base) << 2;
}

/// Do not cut between the halves of a surrogate pair.
qsizetype safeCut(QStringView s, qsizetype i)
{
    return i > 0 && i < s.size() && s[i].isLowSurrogate() ? i + 1 : i;
}

/// Length of the leading container syntax of a line:
/// `^[ \t]*(?:(?:[-*+]|\d{1,9}[.)])[ \t]+(?:\[.\][ \t]+)?|#{1,6}[ \t]+|(?:>[ \t]?)+)?`.
qsizetype linePrefixLength(QStringView line)
{
    const qsizetype n = line.size();
    qsizetype i = 0;
    while (i < n && isBlank(line[i]))
        ++i;
    const qsizetype indent = i;
    auto skipBlanks = [&](qsizetype k) {
        while (k < n && isBlank(line[k]))
            ++k;
        return k;
    };
    // List marker (+ optional checkbox).
    qsizetype markerEnd = -1;
    if (i < n && (line[i] == u'-' || line[i] == u'*' || line[i] == u'+')) {
        markerEnd = i + 1;
    } else {
        qsizetype k = i;
        while (k < n && k - i < 9 && isAsciiDigit(line[k]))
            ++k;
        if (k > i && k < n && (line[k] == u'.' || line[k] == u')'))
            markerEnd = k + 1;
    }
    if (markerEnd >= 0 && markerEnd < n && isBlank(line[markerEnd])) {
        qsizetype k = skipBlanks(markerEnd);
        if (k + 3 < n && line[k] == u'[' && line[k + 2] == u']' && isBlank(line[k + 3]))
            k = skipBlanks(k + 3);
        return k;
    }
    // ATX heading.
    qsizetype h = i;
    while (h < n && line[h] == u'#' && h - i < 7)
        ++h;
    if (h > i && h - i <= 6 && h < n && isBlank(line[h]))
        return skipBlanks(h);
    // Block quote markers.
    qsizetype q = i;
    while (q < n && line[q] == u'>') {
        ++q;
        if (q < n && isBlank(line[q]))
            ++q;
    }
    return q > i ? q : indent;
}

/// Start of a trailing block id (` ^abc-123` incl. the blanks before it), or -1.
qsizetype trailingIdStart(QStringView line)
{
    qsizetype k = line.size();
    while (k > 0 && isBlank(line[k - 1]))
        --k;
    const qsizetype idEnd = k;
    while (k > 0 && isIdChar(line[k - 1]))
        --k;
    if (k == idEnd || k == 0 || line[k - 1] != u'^')
        return -1;
    --k;
    if (k == 0 || !isBlank(line[k - 1]))
        return -1;
    while (k > 0 && isBlank(line[k - 1]))
        --k;
    return k;
}

} // namespace

QString nfc(QStringView s)
{
    return isAscii(s) ? s.toString() : s.toString().normalized(QString::NormalizationForm_C);
}

std::pair<char32_t, qsizetype> codePointAt(QStringView s, qsizetype i)
{
    const QChar c = s[i];
    if (c.isHighSurrogate() && i + 1 < s.size() && s[i + 1].isLowSurrogate())
        return {QChar::surrogateToUcs4(c, s[i + 1]), 2};
    return {c.unicode(), 1};
}

char32_t codePointBefore(QStringView s, qsizetype i)
{
    if (i <= 0 || i > s.size())
        return 0;
    const QChar lo = s[i - 1];
    if (lo.isLowSurrogate() && i >= 2 && s[i - 2].isHighSurrogate())
        return QChar::surrogateToUcs4(s[i - 2], lo);
    return lo.unicode();
}

bool isTokenChar(char32_t cp)
{
    if (cp < 0x80)
        return (cp >= u'a' && cp <= u'z') || (cp >= u'A' && cp <= u'Z') || (cp >= u'0' && cp <= u'9');
    return !QChar::isSurrogate(cp) && QChar::isLetterOrNumber(cp);
}

QStringList words(QStringView folded)
{
    QStringList out;
    qsizetype start = -1;
    for (qsizetype i = 0; i < folded.size();) {
        const auto [cp, len] = codePointAt(folded, i);
        if (isTokenChar(cp)) {
            if (start < 0)
                start = i;
        } else if (start >= 0) {
            out.append(folded.sliced(start, i - start).toString());
            start = -1;
        }
        i += len;
    }
    if (start >= 0)
        out.append(folded.sliced(start).toString());
    return out;
}

QString foldQuery(QStringView query)
{
    return text::foldKey(squash(query));
}

int compareNames(const QString &a, const QString &b)
{
    const QString la = nfc(a).toLower();
    const QString lb = nfc(b).toLower();
    qsizetype i = 0;
    qsizetype j = 0;
    while (i < la.size() && j < lb.size()) {
        if (isAsciiDigit(la[i]) && isAsciiDigit(lb[j])) {
            // Digit runs compare by value ("2" < "10"), leading zeros ignored.
            qsizetype ie = i;
            while (ie < la.size() && isAsciiDigit(la[ie]))
                ++ie;
            qsizetype je = j;
            while (je < lb.size() && isAsciiDigit(lb[je]))
                ++je;
            qsizetype is = i;
            while (is + 1 < ie && la[is] == u'0')
                ++is;
            qsizetype js = j;
            while (js + 1 < je && lb[js] == u'0')
                ++js;
            if (ie - is != je - js)
                return ie - is < je - js ? -1 : 1;
            const int c = QStringView(la).sliced(is, ie - is).compare(QStringView(lb).sliced(js, je - js));
            if (c != 0)
                return c < 0 ? -1 : 1;
            i = ie;
            j = je;
            continue;
        }
        const auto [ca, na] = codePointAt(la, i);
        const auto [cb, nb] = codePointAt(lb, j);
        const quint32 wa = collationWeight(ca);
        const quint32 wb = collationWeight(cb);
        if (wa != wb)
            return wa < wb ? -1 : 1;
        i += na;
        j += nb;
    }
    if (i < la.size())
        return 1;
    if (j < lb.size())
        return -1;
    const int c = QString::compare(a, b, Qt::CaseSensitive);
    return c < 0 ? -1 : (c > 0 ? 1 : 0);
}

qsizetype indexOfToken(QStringView hay, QStringView term, qsizetype from)
{
    if (term.isEmpty())
        return -1;
    for (qsizetype i = hay.indexOf(term, std::max<qsizetype>(0, from)); i >= 0; i = hay.indexOf(term, i + 1)) {
        const qsizetype end = i + term.size();
        if (i > 0 && isTokenChar(codePointBefore(hay, i)))
            continue;
        if (end < hay.size() && isTokenChar(codePointAt(hay, end).first))
            continue;
        return i;
    }
    return -1;
}

qsizetype indexOfWordStart(QStringView hay, QStringView needle)
{
    if (needle.isEmpty())
        return -1;
    for (qsizetype i = hay.indexOf(needle); i >= 0; i = hay.indexOf(needle, i + 1)) {
        if (i == 0 || !text::isWordChar(codePointBefore(hay, i)))
            return i;
    }
    return -1;
}

bool globMatch(QStringView text, QStringView pattern, bool prefixOnly)
{
    // Greedy two-pointer matching: on a mismatch, retry from the most recent '*' with one more
    // character swallowed by it. Earlier stars never need revisiting, so this is O(n × m).
    const qsizetype n = text.size();
    const qsizetype m = pattern.size();
    qsizetype i = 0;
    qsizetype j = 0;
    qsizetype star = -1; // index of the last '*' seen in the pattern
    qsizetype mark = 0;  // text position that star currently matches up to
    while (i < n) {
        if (j < m && pattern[j] == u'*') {
            star = j++;
            mark = i;
        } else if (j < m && pattern[j] == text[i]) {
            ++i;
            ++j;
        } else if (j == m && prefixOnly) {
            return true;
        } else if (star >= 0) {
            j = star + 1;
            i = ++mark;
        } else {
            return false;
        }
    }
    while (j < m && pattern[j] == u'*')
        ++j;
    return j == m;
}

QString squash(QStringView s)
{
    QString out;
    out.reserve(s.size());
    bool pendingSpace = false;
    for (const QChar c : s) {
        if (c.isSpace()) {
            pendingSpace = !out.isEmpty();
            continue;
        }
        if (pendingSpace)
            out += u' ';
        pendingSpace = false;
        out += c;
    }
    return out;
}

QString excerptAround(QStringView line, qsizetype start, qsizetype end, qsizetype max)
{
    const qsizetype n = line.size();
    if (n <= max)
        return squash(line);
    start = std::clamp<qsizetype>(start, 0, n);
    end = std::clamp<qsizetype>(end, start, n);
    const qsizetype len = end - start;
    const auto lead = static_cast<qsizetype>(std::floor(double(max - std::min(len, max)) * 0.35));
    const qsizetype before = std::max<qsizetype>(0, std::min(start, lead));
    qsizetype a = start - before;
    qsizetype b = std::min(n, a + max);
    if (b - a < max)
        a = std::max<qsizetype>(0, b - max);
    if (a > 0) {
        // Start at the next word unless that skips too much or reaches the match.
        for (qsizetype k = a; k < start && k - a < 20; ++k) {
            if (line[k].isSpace()) {
                a = k + 1;
                break;
            }
        }
    }
    if (b < n) {
        const qsizetype tailStart = std::max(end, a);
        for (qsizetype k = b - 1; k >= tailStart && b - k < 20; --k) {
            if (line[k].isSpace()) {
                b = k;
                break;
            }
        }
    }
    a = safeCut(line, a);
    b = std::max(a, safeCut(line, b));
    QString out;
    if (a > 0)
        out += u'…';
    out += squash(line.sliced(a, b - a));
    if (b < n)
        out += u'…';
    return out;
}

QString lineExcerpt(QStringView line, qsizetype start, qsizetype end, qsizetype max)
{
    qsizetype cut = linePrefixLength(line);
    if (cut > start)
        cut = 0;
    QStringView body = line.sliced(cut);
    const qsizetype id = trailingIdStart(body);
    if (id >= 0 && id >= end - cut)
        body = body.first(id);
    return excerptAround(body, start - cut, end - cut, max);
}

QString excerptStart(QStringView text, qsizetype max)
{
    const QString s = squash(text);
    if (s.size() <= max)
        return s;
    qsizetype b = s.lastIndexOf(u' ', max);
    if (double(b) < double(max) * 0.6)
        b = max;
    return s.first(safeCut(s, b)) + u'…';
}

} // namespace brulion::index_detail
