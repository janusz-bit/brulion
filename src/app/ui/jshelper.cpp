#include "jshelper.h"

#include <QDateTime>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonValue>
#include <QLocale>
#include <QUrl>

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <limits>
#include <numeric>

using namespace Qt::StringLiterals;

namespace brulion::ui {

namespace {

constexpr double kNaN = std::numeric_limits<double>::quiet_NaN();

/// The white space of the QML engine's String.prototype.trim(): QChar::isSpace() (TAB, LF, VT,
/// FF, CR, the Unicode separators, also U+0085) and the byte order mark U+FEFF.
bool isJsSpace(QChar c)
{
    return c.isSpace() || c.unicode() == 0xFEFF;
}

qsizetype jsTrimStartIndex(QStringView s)
{
    return std::ranges::find_if_not(s, isJsSpace) - s.begin();
}

qsizetype jsTrimEndIndex(QStringView s)
{
    return std::ranges::find_if_not(s.rbegin(), s.rend(), isJsSpace).base() - s.begin();
}

/// The value of an ASCII digit or letter as a digit of base 36 (a and A = 10 … z and Z = 35);
/// 99, more than any base allows, for every other character.
int digitValue(char16_t c)
{
    if (c >= u'0' && c <= u'9')
        return c - u'0';
    if (c >= u'a' && c <= u'z')
        return c - u'a' + 10;
    if (c >= u'A' && c <= u'Z')
        return c - u'A' + 10;
    return 99;
}

/// A relative index of slice() / splice(): negative counts from the end, clamped to [0, size].
qsizetype relativeIndex(qsizetype i, qsizetype size)
{
    return i < 0 ? std::max<qsizetype>(0, size + i) : std::min(i, size);
}

QString padding(qsizetype count, const QString &fill)
{
    if (count <= 0 || fill.isEmpty())
        return {};
    QString out = fill.repeated((count + fill.size() - 1) / fill.size());
    out.truncate(count);
    return out;
}

/// A JavaScript pattern for PCRE: \uXXXX and \u{X…} become \x{…} (PCRE has no \u); the rest is
/// the same syntax (classes, groups, (?<name>…), lookarounds, \d \w \s \b).
QString pcrePattern(const QString &pattern)
{
    QString out;
    out.reserve(pattern.size());
    for (qsizetype i = 0; i < pattern.size(); ++i) {
        const QChar c = pattern.at(i);
        if (c == u'\\' && i + 1 < pattern.size()) {
            const QChar n = pattern.at(i + 1);
            if (n == u'u' && i + 2 < pattern.size() && pattern.at(i + 2) == u'{') {
                const qsizetype close = pattern.indexOf(u'}', i + 3);
                if (close > 0) {
                    out += u"\\x{"_s + pattern.mid(i + 3, close - i - 3) + u'}';
                    i = close;
                    continue;
                }
            } else if (n == u'u' && i + 5 < pattern.size()) {
                const QString hex = pattern.mid(i + 2, 4);
                bool ok = false;
                hex.toUInt(&ok, 16);
                if (ok) {
                    out += u"\\x{"_s + hex + u'}';
                    i += 5;
                    continue;
                }
            }
            out += c;
            out += n;
            ++i;
            continue;
        }
        out += c;
    }
    return out;
}

/// The replacement of one match with JavaScript's patterns ($$, $&, $`, $', $n, $nn; like the QML
/// engine, no $<name>).
QString expandReplacement(const QString &replacement, const QRegularExpressionMatch &m, const QString &s)
{
    if (!replacement.contains(u'$'))
        return replacement;
    const int groups = m.regularExpression().captureCount();
    QString out;
    for (qsizetype i = 0; i < replacement.size(); ++i) {
        const QChar c = replacement.at(i);
        if (c != u'$' || i + 1 >= replacement.size()) {
            out += c;
            continue;
        }
        const QChar n = replacement.at(i + 1);
        if (n == u'$') {
            out += u'$';
            ++i;
        } else if (n == u'&') {
            out += m.capturedView(0);
            ++i;
        } else if (n == u'`') {
            out += QStringView(s).left(m.capturedStart(0));
            ++i;
        } else if (n == u'\'') {
            out += QStringView(s).mid(m.capturedEnd(0));
            ++i;
        } else if (const int first = digitValue(n.unicode()); first < 10) {
            // $nn when group nn exists, else $n (ECMAScript's GetSubstitution).
            const int second = i + 2 < replacement.size() ? digitValue(replacement.at(i + 2).unicode()) : 99;
            if (const int two = first * 10 + second; second < 10 && two >= 1 && two <= groups) {
                out += m.capturedView(two);
                i += 2;
            } else if (first >= 1 && first <= groups) {
                out += m.capturedView(first);
                ++i;
            } else {
                out += c;
            }
        } else {
            out += c;
        }
    }
    return out;
}

/// ECMAScript Number::toString(x) from the shortest round-trip digits.
QString jsNumberToString(double x)
{
    if (std::isnan(x))
        return u"NaN"_s;
    if (x == 0)
        return u"0"_s;
    if (x < 0)
        return u'-' + jsNumberToString(-x);
    if (std::isinf(x))
        return u"Infinity"_s;
    // "d.ddde±XX": the digits and the exponent of the shortest representation.
    const QString e = QString::number(x, 'e', QLocale::FloatingPointShortest);
    const qsizetype ePos = e.indexOf(u'e');
    QString digits = e.left(ePos);
    digits.remove(u'.');
    const int k = int(digits.size());
    const int n = QStringView(e).sliced(ePos + 1).toInt() + 1;
    if (k <= n && n <= 21)
        return digits + QString(n - k, u'0');
    if (0 < n && n <= 21)
        return digits.left(n) + u'.' + digits.mid(n);
    if (-6 < n && n <= 0)
        return u"0."_s + QString(-n, u'0') + digits;
    QString out = digits.left(1);
    if (k > 1)
        out += u'.' + digits.mid(1);
    return out + (n > 0 ? u"e+"_s : u"e-"_s) + QString::number(std::abs(n - 1));
}

} // namespace

JsHelper::JsHelper(QObject *parent) : QObject(parent) { }

// --- String.prototype ---

int JsHelper::indexOf(const QString &s, const QString &search) const
{
    return int(s.indexOf(search));
}

int JsHelper::indexOfFrom(const QString &s, const QString &search, int from) const
{
    const qsizetype start = std::clamp<qsizetype>(from, 0, s.size());
    if (search.isEmpty())
        return int(start);
    return int(s.indexOf(search, start));
}

int JsHelper::lastIndexOf(const QString &s, const QString &search) const
{
    if (search.isEmpty())
        return int(s.size());
    return int(s.lastIndexOf(search));
}

bool JsHelper::includes(const QString &s, const QString &search) const
{
    return s.contains(search);
}

bool JsHelper::startsWith(const QString &s, const QString &prefix) const
{
    return s.startsWith(prefix);
}

bool JsHelper::startsWithAt(const QString &s, const QString &prefix, int position) const
{
    const qsizetype start = std::clamp<qsizetype>(position, 0, s.size());
    return QStringView(s).mid(start).startsWith(prefix);
}

bool JsHelper::endsWith(const QString &s, const QString &suffix) const
{
    return s.endsWith(suffix);
}

QString JsHelper::sliceFrom(const QString &s, int start) const
{
    return s.mid(relativeIndex(start, s.size()));
}

QString JsHelper::slice(const QString &s, int start, int end) const
{
    const qsizetype from = relativeIndex(start, s.size());
    const qsizetype to = relativeIndex(end, s.size());
    if (to <= from)
        return {};
    return s.mid(from, to - from);
}

QString JsHelper::substring(const QString &s, int start, int end) const
{
    qsizetype from = std::clamp<qsizetype>(start, 0, s.size());
    qsizetype to = std::clamp<qsizetype>(end, 0, s.size());
    if (from > to)
        std::swap(from, to);
    return s.mid(from, to - from);
}

QString JsHelper::substringFrom(const QString &s, int start) const
{
    return s.mid(std::clamp<qsizetype>(start, 0, s.size()));
}

QString JsHelper::charAt(const QString &s, int i) const
{
    if (i < 0 || i >= s.size())
        return {};
    return QString(s.at(i));
}

double JsHelper::charCodeAt(const QString &s, int i) const
{
    if (i < 0 || i >= s.size())
        return kNaN;
    return s.at(i).unicode();
}

QString JsHelper::toLowerCase(const QString &s) const
{
    return s.toLower();
}

QString JsHelper::toUpperCase(const QString &s) const
{
    return s.toUpper();
}

QString JsHelper::trim(const QString &s) const
{
    const qsizetype from = jsTrimStartIndex(s);
    const qsizetype to = jsTrimEndIndex(s);
    return to <= from ? QString() : s.mid(from, to - from);
}

QString JsHelper::trimStart(const QString &s) const
{
    return s.mid(jsTrimStartIndex(s));
}

QString JsHelper::trimEnd(const QString &s) const
{
    return s.left(jsTrimEndIndex(s));
}

QStringList JsHelper::split(const QString &s, const QString &separator) const
{
    // An empty separator: Qt's parts are "", every UTF-16 code unit and "" again.
    return s.split(separator, separator.isEmpty() ? Qt::SkipEmptyParts : Qt::KeepEmptyParts);
}

QString JsHelper::replace(const QString &s, const QString &search, const QString &replacement) const
{
    const qsizetype at = s.indexOf(search);
    if (at < 0)
        return s;
    QString out = s;
    out.replace(at, search.size(), replacement);
    return out;
}

QString JsHelper::replaceAll(const QString &s, const QString &search, const QString &replacement) const
{
    if (search.isEmpty()) {
        // JavaScript inserts the replacement between all code units and at both ends.
        QString out = replacement;
        for (const QChar c : s) {
            out += c;
            out += replacement;
        }
        return out;
    }
    QString out = s;
    out.replace(search, replacement);
    return out;
}

QString JsHelper::repeat(const QString &s, int count) const
{
    if (count <= 0)
        return {};
    return s.repeated(count);
}

QString JsHelper::padStart(const QString &s, int length, const QString &fill) const
{
    return padding(length - s.size(), fill) + s;
}

QString JsHelper::padEnd(const QString &s, int length, const QString &fill) const
{
    return s + padding(length - s.size(), fill);
}

QString JsHelper::encodeURIComponent(const QString &s) const
{
    return QString::fromLatin1(QUrl::toPercentEncoding(s, QByteArrayLiteral("!*'()")));
}

QString JsHelper::decodeURIComponent(const QString &s) const
{
    // (QByteArray::fromPercentEncoding() also decodes escapes without two hex digits.)
    const QByteArray in = s.toUtf8();
    const auto hex = [&in](qsizetype at) { return digitValue(uchar(in.at(at))); };
    QByteArray out;
    out.reserve(in.size());
    for (qsizetype i = 0; i < in.size(); ++i) {
        if (in.at(i) == '%' && i + 2 < in.size() && hex(i + 1) < 16 && hex(i + 2) < 16) {
            out.append(char(hex(i + 1) * 16 + hex(i + 2)));
            i += 2;
        } else {
            out.append(in.at(i));
        }
    }
    return QString::fromUtf8(out);
}

QString JsHelper::arg2(const QString &format, const QString &a, const QString &b) const
{
    return format.arg(a, b);
}

// --- Numbers ---

double JsHelper::toNumber(const QString &s) const
{
    const QString t = trim(s);
    if (t.isEmpty())
        return 0;
    if (t == u"Infinity" || t == u"+Infinity")
        return std::numeric_limits<double>::infinity();
    if (t == u"-Infinity")
        return -std::numeric_limits<double>::infinity();
    if (t.size() > 2 && t.at(0) == u'0') {
        int base = 0;
        switch (t.at(1).toLower().unicode()) {
        case u'x': base = 16; break;
        case u'o': base = 8; break;
        case u'b': base = 2; break;
        }
        if (base) {
            double value = 0;
            for (qsizetype i = 2; i < t.size(); ++i) {
                const int d = digitValue(t.at(i).unicode());
                if (d >= base)
                    return kNaN;
                value = value * base + d;
            }
            return value;
        }
    }
    static const QRegularExpression decimal(u"^[+-]?(?:[0-9]+\\.?[0-9]*|\\.[0-9]+)(?:[eE][+-]?[0-9]+)?$"_s);
    if (!decimal.match(t).hasMatch())
        return kNaN;
    // An overflow ("1e400") gives ±Infinity and an underflow 0, as in JavaScript: QLocale returns
    // them (and reports a failure). Not std::strtod(): it reads the decimal point of the locale
    // Qt applies from the environment on Unix ("1.5e400" was 1 under pl_PL).
    return QLocale::c().toDouble(t);
}

double JsHelper::parseInt(const QString &s) const
{
    qsizetype i = jsTrimStartIndex(s);
    const double sign = i < s.size() && s.at(i) == u'-' ? -1 : 1;
    if (i < s.size() && (s.at(i) == u'+' || s.at(i) == u'-'))
        ++i;
    int base = 10;
    if (i + 1 < s.size() && s.at(i) == u'0' && (s.at(i + 1) == u'x' || s.at(i + 1) == u'X')) {
        base = 16;
        i += 2;
    }
    double value = 0;
    bool any = false;
    for (; i < s.size(); ++i) {
        const int d = digitValue(s.at(i).unicode());
        if (d >= base)
            break;
        value = value * base + d;
        any = true;
    }
    return any ? sign * value : kNaN;
}

double JsHelper::parseFloat(const QString &s) const
{
    const QString t = trimStart(s);
    static const QRegularExpression number(u"^[+-]?(?:Infinity|(?:[0-9]+\\.?[0-9]*|\\.[0-9]+)(?:[eE][+-]?[0-9]+)?)"_s);
    const QRegularExpressionMatch m = number.match(t);
    if (!m.hasMatch())
        return kNaN;
    return toNumber(m.captured(0));
}

bool JsHelper::isNaN(double x) const
{
    return std::isnan(x);
}

bool JsHelper::isFinite(double x) const
{
    return std::isfinite(x);
}

QString JsHelper::toFixed(double x, int digits) const
{
    if (std::isnan(x))
        return u"NaN"_s;
    if (std::abs(x) >= 1e21)
        return jsNumberToString(x);
    return QString::number(x == 0 ? 0.0 : x, 'f', std::clamp(digits, 0, 100)); // -0 prints as "0"
}

QString JsHelper::numberToString(double x) const
{
    return jsNumberToString(x);
}

double JsHelper::atan2(double y, double x) const
{
    return std::atan2(y, x);
}

double JsHelper::dateNow() const
{
    return double(QDateTime::currentMSecsSinceEpoch());
}

// --- Regular expressions ---

const QRegularExpression &JsHelper::regExp(const QString &pattern, const QString &flags) const
{
    const QString key = flags + u'/' + pattern;
    if (const auto it = m_regExps.constFind(key); it != m_regExps.constEnd())
        return *it;
    QRegularExpression::PatternOptions options = QRegularExpression::NoPatternOption;
    if (flags.contains(u'i'))
        options |= QRegularExpression::CaseInsensitiveOption;
    if (flags.contains(u'm'))
        options |= QRegularExpression::MultilineOption;
    if (flags.contains(u's'))
        options |= QRegularExpression::DotMatchesEverythingOption;
    QRegularExpression re(pcrePattern(pattern), options);
    re.optimize();
    return *m_regExps.insert(key, re);
}

bool JsHelper::test(const QString &s, const QString &pattern, const QString &flags) const
{
    return regExp(pattern, flags).match(s).hasMatch();
}

int JsHelper::search(const QString &s, const QString &pattern, const QString &flags) const
{
    const QRegularExpressionMatch m = regExp(pattern, flags).match(s);
    return m.hasMatch() ? int(m.capturedStart(0)) : -1;
}

QStringList JsHelper::match(const QString &s, const QString &pattern, const QString &flags) const
{
    const QRegularExpression &re = regExp(pattern, flags);
    QStringList out;
    if (flags.contains(u'g')) {
        for (const QRegularExpressionMatch &m : re.globalMatch(s))
            out.append(m.captured(0));
        return out;
    }
    const QRegularExpressionMatch m = re.match(s);
    if (!m.hasMatch())
        return out;
    for (int i = 0; i <= re.captureCount(); ++i)
        out.append(m.captured(i));
    return out;
}

QString JsHelper::replaceRegExp(const QString &s, const QString &pattern, const QString &flags,
                                const QString &replacement) const
{
    const QRegularExpression &re = regExp(pattern, flags);
    QString out;
    qsizetype last = 0;
    auto apply = [&](const QRegularExpressionMatch &m) {
        out += QStringView(s).mid(last, m.capturedStart(0) - last);
        out += expandReplacement(replacement, m, s);
        last = m.capturedEnd(0);
    };
    if (flags.contains(u'g')) {
        QRegularExpressionMatchIterator it = re.globalMatch(s);
        if (!it.hasNext())
            return s;
        while (it.hasNext())
            apply(it.next());
    } else {
        const QRegularExpressionMatch m = re.match(s);
        if (!m.hasMatch())
            return s;
        apply(m);
    }
    out += QStringView(s).mid(last);
    return out;
}

QStringList JsHelper::splitRegExp(const QString &s, const QString &pattern, const QString &flags) const
{
    const QRegularExpression &re = regExp(pattern, flags);
    QStringList out;
    if (s.isEmpty()) {
        if (!re.match(s, 0, QRegularExpression::NormalMatch, QRegularExpression::AnchorAtOffsetMatchOption).hasMatch())
            out.append(s);
        return out;
    }
    // ECMAScript's @@split: a match must end after the last cut and start before the end.
    qsizetype p = 0;
    qsizetype q = 0;
    while (q < s.size()) {
        const QRegularExpressionMatch m =
                re.match(s, q, QRegularExpression::NormalMatch, QRegularExpression::AnchorAtOffsetMatchOption);
        if (!m.hasMatch() || m.capturedEnd(0) == p) {
            ++q;
            continue;
        }
        out.append(s.mid(p, q - p));
        for (int i = 1; i <= re.captureCount(); ++i)
            out.append(m.captured(i));
        p = m.capturedEnd(0);
        q = p == q ? q + 1 : p;
    }
    out.append(s.mid(p));
    return out;
}

// --- Lists ---

QVariantList JsHelper::appended(const QVariantList &list, const QVariant &value) const
{
    QVariantList out = list;
    out.append(value);
    return out;
}

QVariantList JsHelper::concat(const QVariantList &list, const QVariantList &other) const
{
    return list + other;
}

QVariantList JsHelper::removedAt(const QVariantList &list, int index) const
{
    QVariantList out = list;
    if (index >= 0 && index < out.size())
        out.removeAt(index);
    return out;
}

QVariantList JsHelper::insertedAt(const QVariantList &list, int index, const QVariant &value) const
{
    QVariantList out = list;
    out.insert(std::clamp<qsizetype>(index, 0, out.size()), value);
    return out;
}

QVariantList JsHelper::reversed(const QVariantList &list) const
{
    QVariantList out = list;
    std::ranges::reverse(out);
    return out;
}

QStringList JsHelper::appendedString(const QStringList &list, const QString &value) const
{
    QStringList out = list;
    out.append(value);
    return out;
}

QStringList JsHelper::concatStrings(const QStringList &list, const QStringList &other) const
{
    return list + other;
}

QStringList JsHelper::removedStringAt(const QStringList &list, int index) const
{
    QStringList out = list;
    if (index >= 0 && index < out.size())
        out.removeAt(index);
    return out;
}

QStringList JsHelper::insertedStringAt(const QStringList &list, int index, const QString &value) const
{
    QStringList out = list;
    out.insert(std::clamp<qsizetype>(index, 0, out.size()), value);
    return out;
}

QStringList JsHelper::withoutString(const QStringList &list, const QString &value) const
{
    QStringList out = list;
    out.removeAll(value);
    return out;
}

QStringList JsHelper::sortedStrings(const QStringList &list) const
{
    QStringList out = list;
    // QString's operator< compares UTF-16 code units, like Array.prototype.sort().
    std::ranges::stable_sort(out);
    return out;
}

QStringList JsHelper::uniqueStrings(const QStringList &list) const
{
    QStringList out = list;
    out.removeDuplicates(); // keeps the first of each
    return out;
}

QStringList JsHelper::reversedStrings(const QStringList &list) const
{
    QStringList out = list;
    std::ranges::reverse(out);
    return out;
}

QStringList JsHelper::nonEmptyStrings(const QStringList &list) const
{
    QStringList out = list;
    out.removeIf([](const QString &s) { return s.isEmpty(); });
    return out;
}

QString JsHelper::joinNonEmpty(const QStringList &parts, const QString &separator) const
{
    return nonEmptyStrings(parts).join(separator);
}

QStringList JsHelper::trimmedStrings(const QStringList &list) const
{
    QStringList out;
    out.reserve(list.size());
    for (const QString &s : list)
        out.append(trim(s));
    return out;
}

QVariantList JsHelper::fromStrings(const QStringList &strings) const
{
    return QVariantList(strings.cbegin(), strings.cend());
}

QVariantList JsHelper::filled(int count, const QVariant &value) const
{
    return QVariantList(std::max(count, 0), value);
}

QList<int> JsHelper::range(int count) const
{
    QList<int> out(std::max(count, 0));
    std::iota(out.begin(), out.end(), 0);
    return out;
}

// --- Objects ---

QStringList JsHelper::keys(const QVariantMap &map) const
{
    return map.keys();
}

QVariantMap JsHelper::merged(const QVariantMap &map, const QVariantMap &changes) const
{
    QVariantMap out = map;
    out.insert(changes); // the values of `changes` win
    return out;
}

QVariantMap JsHelper::withKey(const QVariantMap &map, const QString &key, const QVariant &value) const
{
    QVariantMap out = map;
    out.insert(key, value);
    return out;
}

QVariantMap JsHelper::withoutKey(const QVariantMap &map, const QString &key) const
{
    QVariantMap out = map;
    out.remove(key);
    return out;
}

bool JsHelper::hasKey(const QVariantMap &map, const QString &key) const
{
    return map.contains(key);
}

QString JsHelper::stringify(const QVariant &value) const
{
    if (!value.isValid())
        return {};
    const QByteArray json = QJsonDocument(QJsonArray{QJsonValue::fromVariant(value)}).toJson(QJsonDocument::Compact);
    return QString::fromUtf8(QByteArrayView(json).sliced(1, json.size() - 2)); // "[…]" → "…"
}

} // namespace brulion::ui
