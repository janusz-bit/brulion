#include "calc/numbers.h"

#include <QStringList>

#include <algorithm>
#include <array>
#include <cmath>

namespace brulion::calc {

namespace {

constexpr QChar kMinusSign = QChar(0x2212); // "−"

bool isAsciiDigit(QChar c)
{
    return c >= u'0' && c <= u'9';
}

bool allDigits(QStringView s)
{
    if (s.isEmpty())
        return false;
    for (const QChar c : s) {
        if (!isAsciiDigit(c))
            return false;
    }
    return true;
}

QChar decimalSeparator(CalcLocale locale)
{
    return locale == CalcLocale::Polish ? QLatin1Char(',') : QLatin1Char('.');
}

/// Group the digits of a non-negative integer: Polish from 5 digits (10 000), English from 4.
QString groupInteger(QStringView digits, CalcLocale locale, bool grouping)
{
    const qsizetype minDigits = locale == CalcLocale::Polish ? 5 : 4;
    if (!grouping || digits.size() < minDigits)
        return digits.toString();
    const QChar separator = locale == CalcLocale::Polish ? kNbsp : QLatin1Char(',');
    QString out;
    out.reserve(digits.size() + digits.size() / 3);
    for (qsizetype i = 0; i < digits.size(); ++i) {
        if (i > 0 && (digits.size() - i) % 3 == 0)
            out += separator;
        out += digits[i];
    }
    return out;
}

/// "-1234.5" -> "-1234,5" (pl) / "-1,234.5" (en).
QString localiseDecimal(QStringView plain, CalcLocale locale, bool grouping)
{
    const bool negative = plain.startsWith(u'-');
    const QStringView body = negative ? plain.mid(1) : plain;
    const qsizetype dot = body.indexOf(u'.');
    const QStringView integer = dot < 0 ? body : body.left(dot);
    const QStringView fraction = dot < 0 ? QStringView() : body.mid(dot + 1);
    QString out;
    if (negative)
        out += u'-';
    out += groupInteger(integer.isEmpty() ? QStringView(u"0") : integer, locale, grouping);
    if (!fraction.isEmpty()) {
        out += decimalSeparator(locale);
        out += fraction;
    }
    return out;
}

/// "12.500" -> "12.5", "3.000" -> "3", "120" -> "120".
QString trimFraction(QString s)
{
    if (!s.contains(u'.'))
        return s;
    qsizetype end = s.size();
    while (end > 0 && s.at(end - 1) == u'0')
        --end;
    if (end > 0 && s.at(end - 1) == u'.')
        --end;
    s.truncate(end);
    return s;
}

QString formatExponential(double value, CalcLocale locale)
{
    const QString s = QString::number(value, 'e', kSignificantDigits - 1); // "-1.250000000e-07"
    const qsizetype e = s.indexOf(u'e');
    QString mantissa = trimFraction(s.left(e));
    mantissa.replace(u'.', decimalSeparator(locale));
    const int exponent = QStringView(s).mid(e + 1).toInt();
    return mantissa + u'e' + QString::number(exponent);
}

/// Number of integer digits of a >= 1 (exact, no log10 rounding issues).
int integerDigits(double a)
{
    int digits = 1;
    double power = 10.0;
    while (digits < 17 && a >= power) {
        ++digits;
        power *= 10.0;
    }
    return digits;
}

/// Zeros between the decimal point and the first significant digit of 0 < a < 1.
int leadingZeros(double a)
{
    int zeros = 0;
    double power = 10.0;
    while (zeros < 20 && a * power < 1.0) {
        ++zeros;
        power *= 10.0;
    }
    return zeros;
}

bool isZeroText(QStringView plain)
{
    for (const QChar c : plain) {
        if (c != u'0' && c != u'.' && c != u'-')
            return false;
    }
    return true;
}

/// JavaScript Math.round: halves round towards +infinity.
double roundHalfUp(double x)
{
    const double f = std::floor(x);
    return x - f >= 0.5 ? f + 1.0 : f;
}

// --- parsing -----------------------------------------------------------------------------

struct CurrencyName {
    QStringView name;
    QStringView code;
};

constexpr std::array<CurrencyName, 9> kCurrencies = {{
    {u"zł", u"PLN"},
    {u"PLN", u"PLN"},
    {u"EUR", u"EUR"},
    {u"USD", u"USD"},
    {u"GBP", u"GBP"},
    {u"CHF", u"CHF"},
    {u"€", u"EUR"},
    {u"$", u"USD"},
    {u"£", u"GBP"},
}};

void skipLeadingSpaces(QStringView &v)
{
    while (!v.isEmpty() && v.front().isSpace())
        v = v.mid(1);
}

void skipTrailingSpaces(QStringView &v)
{
    while (!v.isEmpty() && v.back().isSpace())
        v.chop(1);
}

bool isMinus(QChar c)
{
    return c == u'-' || c == kMinusSign;
}

/// "1.234.567" with `separator` '.' -> true: 1-3 digits, then groups of exactly three.
bool isGrouped(QStringView s, QChar separator)
{
    const QList<QStringView> parts = s.split(separator);
    if (parts.size() < 2)
        return false;
    for (qsizetype i = 0; i < parts.size(); ++i) {
        const QStringView part = parts.at(i);
        if (!allDigits(part))
            return false;
        if (i == 0 ? part.size() > 3 : part.size() != 3)
            return false;
    }
    return true;
}

QString withoutChar(QStringView s, QChar c)
{
    QString out;
    out.reserve(s.size());
    for (const QChar ch : s) {
        if (ch != c)
            out += ch;
    }
    return out;
}

/// "12 345" / "1 234 567,5": groups separated by single whitespace characters.
std::optional<QString> mergeSpaceGroups(QStringView body)
{
    qsizetype fractionAt = -1;
    for (qsizetype i = 0; i < body.size(); ++i) {
        if (body.at(i) == u'.' || body.at(i) == u',') {
            fractionAt = i;
            break;
        }
    }
    const QStringView groups = fractionAt < 0 ? body : body.left(fractionAt);
    if (fractionAt >= 0 && !allDigits(body.mid(fractionAt + 1)))
        return std::nullopt;
    QString digits;
    qsizetype partStart = 0;
    int partIndex = 0;
    for (qsizetype i = 0; i <= groups.size(); ++i) {
        if (i < groups.size() && !groups.at(i).isSpace())
            continue;
        const QStringView part = groups.mid(partStart, i - partStart);
        if (!allDigits(part) || (partIndex == 0 ? part.size() > 3 : part.size() != 3))
            return std::nullopt;
        digits += part;
        ++partIndex;
        partStart = i + 1;
    }
    if (partIndex < 2)
        return std::nullopt;
    if (fractionAt >= 0)
        digits += body.mid(fractionAt);
    return digits;
}

/// Interpret the separators of an unsigned digit string with '.', ',' and spaces; returns a
/// plain "1234.5" string or nullopt.
std::optional<QString> normaliseDigits(QStringView body, CalcLocale locale)
{
    QString text = body.toString();
    bool hasSpace = false;
    for (const QChar c : body)
        hasSpace = hasSpace || c.isSpace();
    if (hasSpace) {
        const std::optional<QString> merged = mergeSpaceGroups(body);
        if (!merged)
            return std::nullopt;
        text = *merged;
    }
    const qsizetype dots = text.count(u'.');
    const qsizetype commas = text.count(u',');
    if (dots > 0 && commas > 0) {
        const QChar decimal = text.lastIndexOf(u',') > text.lastIndexOf(u'.') ? QLatin1Char(',') : QLatin1Char('.');
        const QChar group = decimal == u',' ? QLatin1Char('.') : QLatin1Char(',');
        if (text.count(decimal) != 1)
            return std::nullopt;
        const qsizetype at = text.indexOf(decimal);
        const QStringView integer = QStringView(text).left(at);
        if (!isGrouped(integer, group))
            return std::nullopt;
        return QString(withoutChar(integer, group) + u'.' + QStringView(text).mid(at + 1));
    }
    if (commas > 0) {
        const bool englishThousands = locale == CalcLocale::English && isGrouped(text, QLatin1Char(','));
        if (commas == 1 && !englishThousands)
            return text.replace(u',', u'.');
        if (isGrouped(text, QLatin1Char(',')))
            return withoutChar(text, QLatin1Char(','));
        return std::nullopt;
    }
    if (dots > 1)
        return isGrouped(text, QLatin1Char('.')) ? std::optional<QString>(withoutChar(text, QLatin1Char('.'))) : std::nullopt;
    return text;
}

/// `^\d*\.?\d+$|^\d+\.?$`
bool isPlainDecimal(QStringView s)
{
    if (s.isEmpty() || s == u".")
        return false;
    int dots = 0;
    for (const QChar c : s) {
        if (c == u'.')
            ++dots;
        else if (!isAsciiDigit(c))
            return false;
    }
    return dots <= 1;
}

/// Length of a trailing exponent "e-3" / "E12" in `s`, 0 when none.
qsizetype exponentSuffixLength(QStringView s)
{
    qsizetype i = s.size();
    while (i > 0 && isAsciiDigit(s.at(i - 1)))
        --i;
    if (i == s.size())
        return 0;
    if (i > 0 && (s.at(i - 1) == u'+' || s.at(i - 1) == u'-'))
        --i;
    if (i > 1 && (s.at(i - 1) == u'e' || s.at(i - 1) == u'E'))
        return s.size() - (i - 1);
    return 0;
}

/// `^\d[\d\s.,]*$` or `^[.,]\d+$`
bool looksNumeric(QStringView s)
{
    if (s.isEmpty())
        return false;
    if (s.front() == u'.' || s.front() == u',')
        return allDigits(s.mid(1));
    if (!isAsciiDigit(s.front()))
        return false;
    for (const QChar c : s) {
        if (!isAsciiDigit(c) && !c.isSpace() && c != u'.' && c != u',')
            return false;
    }
    return true;
}

} // namespace

QString currencyCode(QStringView symbol)
{
    for (const CurrencyName &c : kCurrencies) {
        if (c.name == symbol)
            return c.code.toString();
    }
    return {};
}

QString formatNumber(double value, CalcLocale locale, NumberFormatOptions options)
{
    if (std::isnan(value))
        return QStringLiteral("NaN");
    if (std::isinf(value))
        return value > 0 ? QStringLiteral("∞") : QStringLiteral("-∞");
    if (value == 0)
        return QStringLiteral("0");
    const double a = std::abs(value);
    if (a >= 1e15 || a < 1e-6) {
        const double rounded = QString::number(value, 'e', kSignificantDigits - 1).toDouble();
        if (std::abs(rounded) < 1e15 && std::abs(rounded) >= 1e-6)
            return formatNumber(rounded, locale, options);
        return formatExponential(value, locale);
    }
    int decimals = 0;
    if (a >= 1)
        decimals = std::max(0, kSignificantDigits - integerDigits(a));
    else
        decimals = std::min(100, leadingZeros(a) + kSignificantDigits);
    const QString plain = trimFraction(QString::number(value, 'f', decimals));
    if (isZeroText(plain))
        return QStringLiteral("0");
    return localiseDecimal(plain, locale, options.grouping);
}

QString formatUnitNumber(double value, CalcLocale locale, NumberFormatOptions options)
{
    const double a = std::abs(value);
    if (!std::isfinite(value) || value == 0 || a >= 1e15 || a < 1e-6)
        return formatNumber(value, locale, options);
    const int decimals = a >= 1 ? kUnitDecimals : leadingZeros(a) + kUnitDecimals;
    // Rounded in decimal text (like the display), then shown without trailing zeros.
    const double rounded = QString::number(value, 'f', decimals).toDouble();
    return formatNumber(rounded, locale, options);
}

QString formatFixed(double value, int fractionDigits, CalcLocale locale, NumberFormatOptions options)
{
    if (!std::isfinite(value) || std::abs(value) >= 1e15)
        return formatNumber(value, locale, options);
    QString plain = QString::number(value, 'f', std::clamp(fractionDigits, 0, 20));
    if (plain.startsWith(u'-') && isZeroText(plain))
        plain.remove(0, 1);
    return localiseDecimal(plain, locale, options.grouping);
}

QString formatMoney(double value, const Currency &currency, CalcLocale locale, NumberFormatOptions options)
{
    const double rounded = roundHalfUp(value * 100) / 100;
    const QString body = rounded == std::trunc(rounded) ? formatNumber(rounded, locale, options)
                                                        : formatFixed(rounded, 2, locale, options);
    if (currency.prefix) {
        if (body.startsWith(u'-'))
            return u'-' + currency.symbol + QStringView(body).mid(1);
        return currency.symbol + body;
    }
    return body + u' ' + currency.symbol;
}

QString stripInlineMarkup(QStringView text)
{
    static constexpr std::array<QStringView, 6> kDelimiters = {u"**", u"__", u"~~", u"*", u"_", u"`"};
    QStringView s = text.trimmed();
    bool changed = true;
    while (changed) {
        changed = false;
        for (const QStringView d : kDelimiters) {
            if (s.size() > 2 * d.size() && s.startsWith(d) && s.endsWith(d)) {
                s = s.mid(d.size(), s.size() - 2 * d.size()).trimmed();
                changed = true;
                break;
            }
        }
    }
    return s.toString();
}

std::optional<ParsedNumber> parseLocaleNumber(QStringView text, CalcLocale locale)
{
    const QString stripped = stripInlineMarkup(text.toString().normalized(QString::NormalizationForm_C));
    QStringView v(stripped);
    if (v.isEmpty())
        return std::nullopt;
    ParsedNumber out;
    double sign = 1;
    if (isMinus(v.front()) || v.front() == u'+') {
        if (v.front() != u'+')
            sign = -1;
        v = v.mid(1);
        skipLeadingSpaces(v);
    }
    for (const CurrencyName &c : kCurrencies) {
        if (v.startsWith(c.name)) {
            out.currency = {c.code.toString(), c.name.toString(), true};
            v = v.mid(c.name.size());
            skipLeadingSpaces(v);
            break;
        }
    }
    if (!out.currency.isValid()) {
        for (const CurrencyName &c : kCurrencies) {
            if (v.endsWith(c.name)) {
                out.currency = {c.code.toString(), c.name.toString(), false};
                v.chop(c.name.size());
                skipTrailingSpaces(v);
                break;
            }
        }
    } else if (sign > 0 && !v.isEmpty() && isMinus(v.front())) {
        sign = -1; // "$-5"
        v = v.mid(1);
        skipLeadingSpaces(v);
    }
    if (!out.currency.isValid() && v.endsWith(u'%')) {
        out.percent = true;
        v.chop(1);
        skipTrailingSpaces(v);
    }
    const qsizetype expLength = exponentSuffixLength(v);
    const QStringView exponent = v.right(expLength);
    QString mantissa = v.left(v.size() - expLength).toString();
    if (!looksNumeric(mantissa))
        return std::nullopt;
    if (mantissa.startsWith(u','))
        mantissa.prepend(u'0');
    const std::optional<QString> digits = normaliseDigits(mantissa, locale);
    if (!digits || !isPlainDecimal(*digits))
        return std::nullopt;
    bool ok = false;
    double value = QString(*digits + exponent).toDouble(&ok);
    if (!ok || !std::isfinite(value))
        return std::nullopt;
    value *= sign;
    if (out.percent)
        value /= 100;
    out.value = value;
    return out;
}

} // namespace brulion::calc
