#include "text/text.h"

#include <QSet>

#include <algorithm>
#include <atomic>

using namespace Qt::StringLiterals;

namespace brulion::text {

namespace {

constexpr char32_t ReplacementChar = 0xFFFD;

/// Code point starting at `i` (a lone surrogate is returned as is) and its length in units.
std::pair<char32_t, qsizetype> codePointAt(QStringView s, qsizetype i)
{
    const QChar c = s.at(i);
    if (c.isHighSurrogate() && i + 1 < s.size() && s.at(i + 1).isLowSurrogate())
        return {QChar::surrogateToUcs4(c, s.at(i + 1)), 2};
    return {c.unicode(), 1};
}

/// Code point ending right before `i`, or 0 at the start of the string.
char32_t codePointBefore(QStringView s, qsizetype i)
{
    if (i <= 0)
        return 0;
    const QChar c = s.at(i - 1);
    if (c.isLowSurrogate() && i >= 2 && s.at(i - 2).isHighSurrogate())
        return QChar::surrogateToUcs4(s.at(i - 2), c);
    return c.unicode();
}

bool isAscii(QStringView s)
{
    return std::all_of(s.begin(), s.end(), [](QChar c) { return c.unicode() < 0x80; });
}

void appendCodePoint(QString &out, char32_t cp)
{
    if (QChar::requiresSurrogates(cp)) {
        out += QChar(QChar::highSurrogate(cp));
        out += QChar(QChar::lowSurrogate(cp));
    } else {
        out += QChar(char16_t(cp));
    }
}

/// Append `s` without combining marks, replacing lone surrogates by U+FFFD.
void appendWithoutMarks(QStringView s, QString &out)
{
    for (qsizetype i = 0; i < s.size();) {
        const auto [cp, len] = codePointAt(s, i);
        i += len;
        if (QChar::isMark(cp))
            continue;
        appendCodePoint(out, QChar::isSurrogate(cp) ? ReplacementChar : cp);
    }
}

/// ł/Ł have no canonical decomposition, so NFD alone would keep them.
void replaceStrokeL(QString &s)
{
    s.replace(u'ł', u'l');
    s.replace(u'Ł', u'L');
}

/// Fold a single code point the slow way and append the result (possibly nothing): Qt's full
/// lower-case mapping when `lower` (İ -> i + U+0307), ł/Ł -> l/L, canonical decomposition,
/// combining marks dropped; a lone surrogate becomes U+FFFD.
void appendFoldedSlow(char32_t cp, bool lower, QString &out)
{
    if (QChar::isSurrogate(cp)) {
        out += QChar(char16_t(ReplacementChar));
        return;
    }
    QString folded;
    appendCodePoint(folded, cp); // not QString::fromUcs4(): it drops U+FEFF as a byte order mark
    if (lower)
        folded = std::move(folded).toLower();
    replaceStrokeL(folded);
    appendWithoutMarks(folded.normalized(QString::NormalizationForm_D), out);
}

// Folding works one code point at a time: NFD decomposes every code point on its own and then
// only reorders combining marks, which folding drops (tst_text checks the equivalence with
// the whole-string fold on every BMP code point and on random strings). Normalising a whole
// long text was quadratic: Qt decomposes in place and moves the tail of the string once per
// decomposed character (a 500 KB Polish note took seconds).
//
// Folded forms of the BMP code units, filled on first use (so non-ASCII text needs no
// allocation per character): the low half of an entry for foldKey(), the high half for
// foldDiacritics(). 0 = not computed yet; FoldsToNothing; NeedsSlowPath (more than one unit);
// else the folded unit. Neither marker is a possible one-unit result (a lone surrogate folds to
// U+FFFD). Threads racing on an entry store the same value. Surrogate units are never looked up.
// 256 KB of zero-initialised static data; only the pages of the scripts in use are touched.
constexpr char16_t FoldsToNothing = 0xD800;
constexpr char16_t NeedsSlowPath = 0xD801;
std::atomic<quint32> foldTable[0x10000 - 0x80];

char16_t tableHalf(char16_t c, bool lower)
{
    QString folded;
    appendFoldedSlow(c, lower, folded);
    if (folded.isEmpty())
        return FoldsToNothing;
    return folded.size() == 1 ? folded.front().unicode() : NeedsSlowPath;
}

/// Folded form of the non-ASCII, non-surrogate unit `c`: a unit, FoldsToNothing or NeedsSlowPath.
char16_t foldedUnit(char16_t c, bool lower)
{
    std::atomic<quint32> &entry = foldTable[c - 0x80];
    quint32 v = entry.load(std::memory_order_relaxed);
    if (v == 0) {
        v = quint32(tableHalf(c, true)) | (quint32(tableHalf(c, false)) << 16);
        entry.store(v, std::memory_order_relaxed);
    }
    return char16_t(lower ? v & 0xFFFF : v >> 16);
}

/// foldKey() (`Lower`) or foldDiacritics() of `s`, one code point at a time. `WithMap`: the
/// offset in `s` of the code point that produced each folded unit is appended to `map`.
template<bool Lower, bool WithMap>
QString foldPerCodePoint(QStringView s, QList<qsizetype> *map)
{
    // Output (and map) sized for one unit per input unit, written in place; grown only when a
    // code point folds to more units than it has (Hangul syllables), trimmed at the end.
    QString out(s.size(), Qt::Uninitialized);
    QChar *d = out.data();
    [[maybe_unused]] const qsizetype mapBase = WithMap ? map->size() : 0;
    [[maybe_unused]] qsizetype *m = nullptr;
    if constexpr (WithMap) {
        map->resizeForOverwrite(mapBase + s.size());
        m = map->data() + mapBase;
    }
    qsizetype n = 0;
    QString slow;
    for (qsizetype i = 0; i < s.size();) {
        const char16_t c = s[i].unicode();
        char16_t unit = c;
        if (c >= 0x80)
            unit = QChar::isSurrogate(c) ? NeedsSlowPath : foldedUnit(c, Lower);
        else if (Lower && c >= u'A' && c <= u'Z')
            unit = char16_t(c + 32);
        if (unit == NeedsSlowPath) {
            const auto [cp, len] = codePointAt(s, i);
            slow.clear();
            appendFoldedSlow(cp, Lower, slow);
            // Room for this result and one unit per input unit still to come.
            const qsizetype need = n + slow.size() + (s.size() - i - len);
            if (need > out.size()) {
                const qsizetype size = std::max(need, out.size() + out.size() / 2);
                out.resize(size);
                d = out.data();
                if constexpr (WithMap) {
                    map->resizeForOverwrite(mapBase + size);
                    m = map->data() + mapBase;
                }
            }
            for (const QChar u : std::as_const(slow)) {
                if constexpr (WithMap)
                    m[n] = i;
                d[n++] = u;
            }
            i += len;
            continue;
        }
        if (unit != FoldsToNothing) {
            if constexpr (WithMap)
                m[n] = i;
            d[n++] = QChar(unit);
        }
        ++i;
    }
    out.resize(n);
    if constexpr (WithMap)
        map->resize(mapBase + n);
    return out;
}

} // namespace

// ---------------------------------------------------------------------------------------------
// Folding
// ---------------------------------------------------------------------------------------------

QString foldDiacritics(QStringView s)
{
    if (isAscii(s))
        return s.toString();
    return foldPerCodePoint<false, false>(s, nullptr);
}

QString foldKey(QStringView s)
{
    if (isAscii(s))
        return s.toString().toLower();
    return foldPerCodePoint<true, false>(s, nullptr);
}

qsizetype FoldedText::toOriginal(qsizetype foldedOffset) const
{
    if (map.isEmpty())
        return 0;
    return map.at(std::clamp<qsizetype>(foldedOffset, 0, map.size() - 1));
}

FoldedText foldKeyWithMap(QStringView s)
{
    FoldedText folded;
    folded.map.reserve(s.size() + 1);
    folded.text = foldPerCodePoint<true, true>(s, &folded.map);
    folded.map.append(s.size());
    return folded;
}

// ---------------------------------------------------------------------------------------------
// Words
// ---------------------------------------------------------------------------------------------

bool isWordChar(char32_t codePoint)
{
    if (codePoint == u'_')
        return true;
    if (QChar::isSurrogate(codePoint))
        return false;
    return QChar::isLetterOrNumber(codePoint) || QChar::isMark(codePoint);
}

bool isWordChar(QChar c)
{
    return isWordChar(char32_t(c.unicode()));
}

bool isWholeWord(QStringView s, qsizetype start, qsizetype end)
{
    if (start < 0 || end > s.size() || start > end)
        return false;
    const char32_t before = codePointBefore(s, start);
    if (before != 0 && isWordChar(before))
        return false;
    return end >= s.size() || !isWordChar(codePointAt(s, end).first);
}

qsizetype indexOfWholeWord(QStringView haystack, QStringView needle, qsizetype from, Qt::CaseSensitivity cs)
{
    if (needle.isEmpty())
        return -1;
    for (qsizetype i = haystack.indexOf(needle, std::max<qsizetype>(from, 0), cs); i >= 0;
         i = haystack.indexOf(needle, i + 1, cs)) {
        if (isWholeWord(haystack, i, i + needle.size()))
            return i;
    }
    return -1;
}

QString wordStartAssertion()
{
    return uR"((?<![\p{L}\p{N}\p{M}_]))"_s;
}

QString wordEndAssertion()
{
    return uR"((?![\p{L}\p{N}\p{M}_]))"_s;
}

QString wholeWordPattern(QStringView literal)
{
    return wordStartAssertion() + QRegularExpression::escape(literal) + wordEndAssertion();
}

QString regexAlternation(const QStringList &words)
{
    QStringList unique;
    QSet<QString> seen;
    for (const QString &w : words) {
        if (!w.isEmpty() && !seen.contains(w)) {
            seen.insert(w);
            unique.append(w);
        }
    }
    std::sort(unique.begin(), unique.end(), [](const QString &a, const QString &b) {
        return a.size() != b.size() ? a.size() > b.size() : a < b;
    });
    QStringList escaped;
    escaped.reserve(unique.size());
    for (const QString &w : std::as_const(unique))
        escaped.append(QRegularExpression::escape(w));
    return escaped.join(u'|');
}

QRegularExpression unicodeRegex(const QString &pattern, QRegularExpression::PatternOptions options)
{
    QRegularExpression re(pattern, options | QRegularExpression::UseUnicodePropertiesOption);
    Q_ASSERT_X(re.isValid(), "brulion::text::unicodeRegex",
               qPrintable(QString(re.errorString() + u": "_s + pattern)));
    re.optimize();
    return re;
}

// ---------------------------------------------------------------------------------------------
// Lines
// ---------------------------------------------------------------------------------------------

QList<Line> splitLines(QStringView content)
{
    QList<Line> lines;
    qsizetype start = 0;
    const qsizetype n = content.size();
    for (qsizetype i = 0; i < n; ++i) {
        const char16_t c = content.at(i).unicode();
        if (c != u'\n' && c != u'\r')
            continue;
        Line line{content.sliced(start, i - start).toString(), {}, start};
        if (c == u'\r' && i + 1 < n && content.at(i + 1) == u'\n') {
            line.eol = u"\r\n"_s;
            ++i;
        } else {
            line.eol = QString(QChar(c));
        }
        lines.append(std::move(line));
        start = i + 1;
    }
    lines.append(Line{content.sliced(start).toString(), {}, start});
    return lines;
}

QString joinLines(const QList<Line> &lines)
{
    qsizetype size = 0;
    for (const Line &l : lines)
        size += l.text.size() + l.eol.size();
    QString out;
    out.reserve(size);
    for (const Line &l : lines) {
        out += l.text;
        out += l.eol;
    }
    return out;
}

QString detectEol(QStringView content)
{
    const qsizetype i = std::find_if(content.begin(), content.end(),
                                     [](QChar c) { return c == u'\n' || c == u'\r'; })
                        - content.begin();
    if (i >= content.size())
        return u"\n"_s;
    if (content.at(i) == u'\n')
        return u"\n"_s;
    return i + 1 < content.size() && content.at(i + 1) == u'\n' ? u"\r\n"_s : u"\r"_s;
}

QString preferredEol(const QList<Line> &lines, qsizetype index)
{
    if (index >= 0 && index < lines.size() && !lines.at(index).eol.isEmpty())
        return lines.at(index).eol;
    for (const Line &l : lines) {
        if (!l.eol.isEmpty())
            return l.eol;
    }
    return u"\n"_s;
}

// ---------------------------------------------------------------------------------------------
// Hashing
// ---------------------------------------------------------------------------------------------

quint64 fnv1a64(QByteArrayView data)
{
    constexpr quint64 offsetBasis = 0xcbf29ce484222325ULL;
    constexpr quint64 prime = 0x100000001b3ULL;
    quint64 hash = offsetBasis;
    for (const char byte : data) {
        hash ^= quint64(quint8(byte));
        hash *= prime;
    }
    return hash;
}

QString fnv1a64Hex(QByteArrayView data)
{
    return QString::number(fnv1a64(data), 16).rightJustified(16, u'0');
}

QString contentHash(QStringView text)
{
    return fnv1a64Hex(text.toUtf8());
}

} // namespace brulion::text
