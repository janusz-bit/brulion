#include "dataview/value.h"
#include "dataview/eval_p.h"
#include "dataview/messages_p.h"

#include "dates/local.h"
#include "markdown/properties.h"
#include "text/text.h"

#include <QRegularExpression>
#include <QtNumeric>

#include <algorithm>
#include <cmath>

using namespace Qt::StringLiterals;

namespace brulion::dataview {

// ---- Limits ----------------------------------------------------------------------------------------

void throwTooLarge()
{
    throw EvalError{messages::valueTooLarge(MaxValueItems, MaxTextLength)};
}

void Value::throwPastLimits(const Extent &e)
{
    if (e.depth > MaxValueDepth)
        throw EvalError{messages::valueTooDeep(MaxValueDepth)};
    throwTooLarge();
}

// ---- Value -----------------------------------------------------------------------------------------

struct ListNode {
    ValueList items;
    Extent extent;
};

Value Value::list(ValueList items)
{
    Extent e{1, 1, 0};
    for (const Value &item : std::as_const(items))
        e.add(item.extent());
    check(e);
    return of(std::make_shared<const ListNode>(std::move(items), e));
}

Value Value::map(const QList<std::pair<QString, Value>> &fields)
{
    return object(std::make_shared<MapObject>(fields));
}

Value Value::function(std::shared_ptr<const Function> f)
{
    if (!f)
        return Value();
    check({f->depth, 1, 0});
    return of(std::move(f));
}

const ValueList &Value::asList() const
{
    return std::get<std::shared_ptr<const ListNode>>(m)->items;
}

Extent Value::partsExtent() const
{
    if (is(Type::List))
        return std::get<std::shared_ptr<const ListNode>>(m)->extent;
    return {asFunction().depth, 1, 0}; // a lambda: shown as "<function>", compared by identity
}

bool Value::truthy() const
{
    switch (type()) {
    case Type::Null: return false;
    case Type::Boolean: return asBool();
    case Type::Number: return asNumber() != 0 && !std::isnan(asNumber());
    case Type::String: return !asString().isEmpty();
    case Type::List: return !asList().isEmpty();
    case Type::Object: return !asObject().keys().isEmpty();
    case Type::Duration: return asDuration().months != 0 || asDuration().ms != 0;
    case Type::Date:
    case Type::Link:
    case Type::Function: return true;
    }
    return false;
}

QString Value::typeName() const
{
    switch (type()) {
    case Type::Null: return u"null"_s;
    case Type::Boolean: return u"boolean"_s;
    case Type::Number: return u"number"_s;
    case Type::String: return u"string"_s;
    case Type::Date: return u"date"_s;
    case Type::Duration: return u"duration"_s;
    case Type::Link: return u"link"_s;
    case Type::List: return u"array"_s;
    case Type::Object: return u"object"_s;
    case Type::Function: return u"function"_s;
    }
    return u"null"_s;
}

// ---- MapObject -------------------------------------------------------------------------------------

MapObject::MapObject(QList<std::pair<QString, Value>> fields)
    : m_fields(std::move(fields)), m_extent(fieldsExtent(m_fields))
{ }

std::optional<Value> MapObject::field(const QString &key) const
{
    const auto it = std::ranges::find(m_fields, key, &std::pair<QString, Value>::first);
    if (it == m_fields.cend())
        return std::nullopt;
    return it->second;
}

QStringList MapObject::keys() const
{
    QStringList out;
    out.reserve(m_fields.size());
    for (const auto &field : m_fields)
        out.append(field.first);
    return out;
}

// ---- Ordering --------------------------------------------------------------------------------------

namespace {

/// The order of types in sorting (Dataview: null first).
int typeRank(Value::Type t)
{
    switch (t) {
    case Value::Type::Null: return 0;
    case Value::Type::Boolean: return 1;
    case Value::Type::Number: return 2;
    case Value::Type::Duration: return 3;
    case Value::Type::Date: return 4;
    case Value::Type::String: return 5;
    case Value::Type::Link: return 6;
    case Value::Type::List: return 7;
    case Value::Type::Object: return 8;
    case Value::Type::Function: return 9;
    }
    return 0;
}

template<typename T>
int cmp(const T &a, const T &b)
{
    return a < b ? -1 : (b < a ? 1 : 0);
}

/// A duration's length in milliseconds (a month as 30 days) — for ordering only.
double approxMs(Duration d)
{
    return double(d.months) * 30.0 * 86400000.0 + double(d.ms);
}

int compareStrings(const QString &a, const QString &b)
{
    if (a == b)
        return 0;
    const int c = text::compareNames(a, b);
    return c != 0 ? c : (a < b ? -1 : 1);
}

} // namespace

int compare(const Value &a, const Value &b)
{
    if (a.type() != b.type())
        return cmp(typeRank(a.type()), typeRank(b.type()));
    switch (a.type()) {
    case Value::Type::Null: return 0;
    case Value::Type::Boolean: return cmp(int(a.asBool()), int(b.asBool()));
    case Value::Type::Number: {
        const double x = a.asNumber(), y = b.asNumber();
        if (std::isnan(x) || std::isnan(y))
            return cmp(int(std::isnan(x)), int(std::isnan(y)));
        return cmp(x, y);
    }
    case Value::Type::String: return compareStrings(a.asString(), b.asString());
    case Value::Type::Date: return cmp(a.asDate().at, b.asDate().at);
    case Value::Type::Duration: return cmp(approxMs(a.asDuration()), approxMs(b.asDuration()));
    case Value::Type::Link: {
        const int c = compareStrings(a.asLink().path, b.asLink().path);
        return c != 0 ? c : compareStrings(a.asLink().subpath, b.asLink().subpath);
    }
    // (A list or an object equals itself at once: comparing a task object field by field would
    // follow its children as deep as the note's outline goes.)
    case Value::Type::List: {
        const ValueList &x = a.asList(), &y = b.asList();
        if (&x == &y)
            return 0;
        for (qsizetype i = 0; i < std::min(x.size(), y.size()); ++i) {
            if (const int c = compare(x.at(i), y.at(i)))
                return c;
        }
        return cmp(x.size(), y.size());
    }
    case Value::Type::Object: {
        if (&a.asObject() == &b.asObject())
            return 0;
        const QStringList kx = a.asObject().keys(), ky = b.asObject().keys();
        for (qsizetype i = 0; i < std::min(kx.size(), ky.size()); ++i) {
            if (const int c = compareStrings(kx.at(i), ky.at(i)))
                return c;
            if (const int c = compare(a.asObject().field(kx.at(i)).value_or(Value()),
                                      b.asObject().field(ky.at(i)).value_or(Value())))
                return c;
        }
        return cmp(kx.size(), ky.size());
    }
    case Value::Type::Function: return cmp(&a.asFunction(), &b.asFunction());
    }
    return 0;
}

bool equals(const Value &a, const Value &b)
{
    if (a.type() != b.type())
        return false;
    switch (a.type()) {
    case Value::Type::String: return a.asString() == b.asString();
    case Value::Type::Link: return a.asLink().path == b.asLink().path && a.asLink().subpath == b.asLink().subpath;
    case Value::Type::Duration: return a.asDuration() == b.asDuration();
    default: return compare(a, b) == 0;
    }
}

QString canonicalKey(QStringView key)
{
    QString out;
    out.reserve(key.size());
    bool blank = false;
    for (const QChar c : key.trimmed()) {
        if (c == u'*' || c == u'_' || c == u'~' || c == u'`')
            continue;
        if (c.isSpace()) {
            blank = true;
            continue;
        }
        if (blank && !out.isEmpty())
            out.append(u'-');
        blank = false;
        out.append(c);
    }
    return out.toLower();
}

// ---- Parsing values from text -------------------------------------------------------------------

namespace {

/// Milliseconds per unit word (lower-case, diacritics folded), months as -1, years as -12;
/// nullopt for a word that is no unit.
std::optional<qint64> unitMs(QStringView word)
{
    static const QHash<QString, qint64> units = [] {
        QHash<QString, qint64> u;
        const qint64 s = 1000, m = 60 * s, h = 60 * m, d = 24 * h, w = 7 * d;
        for (const char *k : {"ms", "msec", "msecs", "millisecond", "milliseconds"})
            u.insert(QString::fromLatin1(k), 1);
        for (const char *k : {"s", "sec", "secs", "second", "seconds", "sekunda", "sekundy", "sekund"})
            u.insert(QString::fromLatin1(k), s);
        for (const char *k : {"m", "min", "mins", "minute", "minutes", "minuta", "minuty", "minut"})
            u.insert(QString::fromLatin1(k), m);
        for (const char *k : {"h", "hr", "hrs", "hour", "hours", "godz", "godzina", "godziny", "godzin"})
            u.insert(QString::fromLatin1(k), h);
        for (const char *k : {"d", "day", "days", "dzien", "dni"})
            u.insert(QString::fromLatin1(k), d);
        for (const char *k : {"w", "wk", "wks", "week", "weeks", "tydzien", "tygodnie", "tygodni", "tyg"})
            u.insert(QString::fromLatin1(k), w);
        for (const char *k : {"mo", "month", "months", "miesiac", "miesiace", "miesiecy", "mies"})
            u.insert(QString::fromLatin1(k), -1);
        for (const char *k : {"y", "yr", "yrs", "year", "years", "rok", "lata", "lat"})
            u.insert(QString::fromLatin1(k), -12);
        return u;
    }();
    if (const auto it = units.constFind(text::foldKey(word)); it != units.constEnd())
        return *it;
    return std::nullopt;
}

} // namespace

std::optional<Duration> parseDuration(QStringView text)
{
    // "3 days", "1h 30m", "2 weeks, 1 day", "1.5 hours".
    static const QRegularExpression part(u"\\G[\\s,]*(?:and\\s+|i\\s+)?(\\d+(?:[.,]\\d+)?)\\s*(\\p{L}+)"_s);
    const QStringView s = text.trimmed();
    if (s.isEmpty() || s.size() > 200)
        return std::nullopt;
    Duration out;
    qsizetype at = 0;
    bool any = false;
    while (at < s.size()) {
        const QRegularExpressionMatch m = part.matchView(s, at);
        if (!m.hasMatch() || m.capturedLength() == 0)
            break;
        const std::optional<qint64> unit = unitMs(m.capturedView(2));
        if (!unit)
            return std::nullopt;
        const double n = m.captured(1).replace(u',', u'.').toDouble();
        if (n > 1e9)
            return std::nullopt;
        // (Each part fits; a text of many "999999999 weeks" would overflow the sum.)
        const bool months = *unit < 0;
        qint64 &total = months ? out.months : out.ms;
        if (qAddOverflow(total, qint64(std::llround(n * double(months ? -*unit : *unit))), &total))
            return std::nullopt;
        at = m.capturedEnd();
        any = true;
    }
    while (at < s.size() && (s.at(at).isSpace() || s.at(at) == u','))
        ++at;
    if (!any || at != s.size())
        return std::nullopt;
    return out;
}

std::optional<DateValue> parseDate(QStringView text)
{
    const QStringView s = text.trimmed();
    if (s.size() < 7 || s.size() > 40 || !s.at(0).isDigit())
        return std::nullopt;
    if (const auto spec = dates::parseDateSpec(s))
        return DateValue{spec->toLocalDateTime(), spec->hasTime()};
    // With seconds or a zone: "2026-10-15T09:30:15", "…Z", "…+02:00" (the wall-clock part is kept).
    static const QRegularExpression full(
            u"^(\\d{4}-\\d{2}-\\d{2})[T ](\\d{2}):(\\d{2})(?::(\\d{2})(?:\\.\\d+)?)?(?:Z|[+-]\\d{2}:?\\d{2})?$"_s);
    if (const QRegularExpressionMatch m = full.matchView(s); m.hasMatch()) {
        const auto day = dates::parseIsoDate(m.capturedView(1));
        const QTime time(m.capturedView(2).toInt(), m.capturedView(3).toInt(), m.capturedView(4).toInt());
        if (day && time.isValid())
            return DateValue{QDateTime(*day, time), true};
        return std::nullopt;
    }
    // "2026-10": the first of the month.
    static const QRegularExpression month(u"^(\\d{4})-(\\d{2})$"_s);
    if (const QRegularExpressionMatch m = month.matchView(s); m.hasMatch()) {
        const QDate d(m.capturedView(1).toInt(), m.capturedView(2).toInt(), 1);
        if (d.isValid())
            return DateValue{QDateTime(d, QTime(0, 0)), false};
    }
    return std::nullopt;
}

std::optional<Value> parseLinkText(QStringView text, const LinkResolver &resolve)
{
    QStringView s = text.trimmed();
    bool embed = false;
    if (s.startsWith(u'!')) {
        embed = true;
        s = s.sliced(1);
    }
    if (!s.startsWith(u"[[") || !s.endsWith(u"]]") || s.size() < 5)
        return std::nullopt;
    const QStringView inner = s.sliced(2, s.size() - 4);
    if (inner.contains(u"[[") || inner.contains(u"]]") || inner.contains(u'\n'))
        return std::nullopt;
    QStringView target = inner;
    QString display;
    if (const qsizetype bar = inner.indexOf(u'|'); bar >= 0) {
        target = inner.first(bar);
        display = inner.sliced(bar + 1).trimmed().toString();
    }
    QString subpath;
    if (const qsizetype hash = target.indexOf(u'#'); hash >= 0) {
        subpath = target.sliced(hash + 1).trimmed().toString();
        target = target.first(hash);
    }
    return Value::link(resolve(target.trimmed().toString(), display, subpath, embed));
}

Value parseFieldValue(const QString &key, QStringView raw, const LinkResolver &resolve)
{
    const QStringView s = raw.trimmed();
    const QString k = canonicalKey(key);
    if (k == u"tags" || k == u"tag" || k == u"aliases" || k == u"alias") {
        ValueList items;
        const bool tags = k.startsWith(u"tag");
        for (const QStringList names = markdown::namesFrom(s.toString(), tags); const QString &name : names)
            items.append(Value::string(tags ? u'#' + name : name));
        return Value::list(items);
    }
    if (s.isEmpty())
        return Value();
    if (const auto link = parseLinkText(s, resolve))
        return *link;
    if (const auto date = parseDate(s))
        return Value::date(*date);
    static const QRegularExpression number(u"^-?\\d+(?:\\.\\d+)?$"_s);
    if (number.matchView(s).hasMatch())
        return Value::number(s.toDouble());
    if (s.compare(u"true", Qt::CaseInsensitive) == 0)
        return Value::boolean(true);
    if (s.compare(u"false", Qt::CaseInsensitive) == 0)
        return Value::boolean(false);
    if (s.front().isDigit()) {
        if (const auto dur = parseDuration(s))
            return Value::duration(*dur);
    }
    // "[[a]], [[b]]": a list of links.
    if (s.contains(u',') && s.contains(u"[[")) {
        ValueList links;
        for (const QStringList parts = markdown::detail::splitTopLevelCommas(s); const QString &part : parts) {
            const auto link = parseLinkText(part, resolve);
            if (!link)
                return Value::string(s.toString());
            links.append(*link);
        }
        if (links.size() > 1)
            return Value::list(links);
    }
    return Value::string(s.toString());
}

} // namespace brulion::dataview
