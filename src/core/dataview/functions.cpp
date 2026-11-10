// The built-in functions of Dataview expressions (Dataview's own names and behaviour; the ones
// that would run regular expressions or read other files are left out on purpose).
#include "dataview/eval_p.h"

#include "text/text.h"
#include "util/paths.h"

#include <QLocale>
#include <QRegularExpression>

#include <algorithm>
#include <cmath>

using namespace Qt::StringLiterals;

namespace brulion::dataview {

namespace {

using T = Value::Type;
using Args = ValueList;

// ---- helpers ---------------------------------------------------------------------------------------

/// The values a function of "a list or many values" works on: min(1, 2, 3) or min(list).
const ValueList &spread(const Args &a)
{
    return a.size() == 1 && a.first().is(T::List) ? a.first().asList() : a;
}

bool callPredicate(const Value &fn, const Value &item, Context &ctx)
{
    return callFunction(fn.asFunction(), {item}, ctx).truthy();
}

int intArg(const Value &v, int fallback)
{
    if (!v.is(T::Number) || std::isnan(v.asNumber()))
        return fallback;
    return int(std::clamp(v.asNumber(), -1e9, 1e9));
}

Value dateOrNull(const std::optional<DateValue> &d)
{
    return d ? Value::date(*d) : Value();
}

/// contains() / icontains(): a substring of a string or of a link's path, an element of a list
/// (or a value containing it), a field of an object; else equality.
bool containsImpl(const Value &hay, const Value &needle, Qt::CaseSensitivity cs, Context &ctx)
{
    ctx.step();
    switch (hay.type()) {
    case T::String:
        ctx.stepText(hay.asString().size());
        return needle.is(T::String) && hay.asString().contains(needle.asString(), cs);
    case T::List:
        return std::ranges::any_of(hay.asList(),
                                   [&](const Value &item) { return containsImpl(item, needle, cs, ctx); });
    case T::Object: return needle.is(T::String) && hay.asObject().field(canonicalKey(needle.asString())).has_value();
    case T::Link:
        if (needle.is(T::Link))
            return equals(hay, needle);
        return needle.is(T::String) && hay.asLink().path.contains(needle.asString(), cs);
    default: return equals(hay, needle);
    }
}

/// Luxon's format tokens for dateformat(): yyyy, MM, dd, HH, mm, ss, MMMM, EEEE, …
QString formatLuxon(const DateValue &d, const QString &fmt)
{
    const QLocale c = QLocale::c();
    const QDate day = d.at.date();
    const QTime t = d.at.time();
    int weekYear = 0;
    const int week = day.weekNumber(&weekYear);
    const auto pad = [](int v, qsizetype w) { return QString::number(v).rightJustified(w, u'0'); };
    QString out;
    qsizetype i = 0;
    while (i < fmt.size()) {
        const QChar ch = fmt.at(i);
        if (ch == u'\'') {
            const qsizetype end = fmt.indexOf(u'\'', i + 1);
            out += QStringView(fmt).mid(i + 1, end < 0 ? -1 : end - i - 1);
            i = end < 0 ? fmt.size() : end + 1;
            continue;
        }
        if (!ch.isLetter()) {
            out += ch;
            ++i;
            continue;
        }
        qsizetype n = 1;
        while (i + n < fmt.size() && fmt.at(i + n) == ch)
            ++n;
        switch (ch.unicode()) {
        case 'y': out += n == 2 ? pad(day.year() % 100, 2) : pad(day.year(), n); break;
        case 'M':
        case 'L':
            out += n >= 4   ? c.monthName(day.month(), QLocale::LongFormat)
                   : n == 3 ? c.monthName(day.month(), QLocale::ShortFormat)
                            : pad(day.month(), n);
            break;
        case 'd': out += pad(day.day(), n); break;
        case 'E':
        case 'c':
            out += n >= 4 ? c.dayName(day.dayOfWeek(), QLocale::LongFormat)
                   : n == 3 ? c.dayName(day.dayOfWeek(), QLocale::ShortFormat)
                            : QString::number(day.dayOfWeek());
            break;
        case 'H': out += pad(t.hour(), n); break;
        case 'h': out += pad(t.hour() % 12 == 0 ? 12 : t.hour() % 12, n); break;
        case 'm': out += pad(t.minute(), n); break;
        case 's': out += pad(t.second(), n); break;
        case 'S': out += pad(t.msec(), 3); break;
        case 'a': out += t.hour() < 12 ? u"AM"_s : u"PM"_s; break;
        case 'W': out += pad(week, n); break;
        case 'k': out += pad(weekYear, n); break;
        case 'q': out += QString::number((day.month() - 1) / 3 + 1); break;
        case 'o': out += pad(day.dayOfYear(), n); break;
        default: out += QString(n, ch); break;
        }
        i += n;
    }
    return out;
}

/// date(text, format): Luxon tokens turned into a QDateTime format.
std::optional<DateValue> parseWithFormat(const QString &text, const QString &fmt)
{
    QString qt;
    bool hasTime = false;
    qsizetype i = 0;
    while (i < fmt.size()) {
        const QChar ch = fmt.at(i);
        qsizetype n = 1;
        while (i + n < fmt.size() && fmt.at(i + n) == ch)
            ++n;
        switch (ch.unicode()) {
        case 'y': qt += n == 2 ? u"yy"_s : u"yyyy"_s; break;
        case 'M':
        case 'L': qt += QString(std::min<qsizetype>(n, 4), u'M'); break;
        case 'd': qt += QString(std::min<qsizetype>(n, 2), u'd'); break;
        case 'H': qt += QString(std::min<qsizetype>(n, 2), u'H'); hasTime = true; break;
        case 'h': qt += QString(std::min<qsizetype>(n, 2), u'h'); hasTime = true; break;
        case 'm': qt += QString(std::min<qsizetype>(n, 2), u'm'); hasTime = true; break;
        case 's': qt += QString(std::min<qsizetype>(n, 2), u's'); hasTime = true; break;
        case 'a': qt += u"AP"_s; break;
        case '\'': {
            const qsizetype end = fmt.indexOf(u'\'', i + 1);
            qt += QStringView(fmt).mid(i, end < 0 ? -1 : end - i + 1);
            n = (end < 0 ? fmt.size() : end + 1) - i;
            break;
        }
        default:
            if (ch.isLetter())
                return std::nullopt;
            qt += QString(n, ch);
            break;
        }
        i += n;
    }
    const QDateTime at = QLocale::c().toDateTime(text, qt);
    if (!at.isValid())
        return std::nullopt;
    return DateValue{hasTime ? at : QDateTime(at.date(), QTime(0, 0)), hasTime};
}

Value toDate(const Value &v, Context &ctx)
{
    switch (v.type()) {
    case T::Date: return v;
    case T::String: {
        ctx.stepText(v.asString().size());
        const QString s = v.asString().trimmed();
        if (const auto d = dateWord(s.toLower(), ctx.options()))
            return Value::date(*d);
        return dateOrNull(parseDate(s));
    }
    case T::Link: {
        // A link to a daily note (or a page named by a date): its day.
        const Value day = fieldOf(fieldOf(v, u"file"_s, ctx), u"day"_s, ctx);
        return day.is(T::Date) ? day : dateOrNull(parseDate(paths::pageTitle(v.asLink().path)));
    }
    case T::Number: {
        // Milliseconds since the epoch; converting NaN or a value beyond qint64 (2^63) is undefined.
        const double ms = v.asNumber();
        if (std::isnan(ms) || std::fabs(ms) >= 0x1p63)
            return Value();
        return Value::date(DateValue{QDateTime::fromMSecsSinceEpoch(qint64(ms)), true});
    }
    default: return Value();
    }
}

// ---- constructors ----------------------------------------------------------------------------------

Value fObject(const Args &a, Context &ctx)
{
    QList<std::pair<QString, Value>> fields;
    for (qsizetype i = 0; i + 1 < a.size(); i += 2) {
        const QString key = canonicalKey(toText(a.at(i), ctx));
        ctx.stepText(key.size());
        fields.append({key, a.at(i + 1)});
    }
    return Value::map(fields);
}

Value fList(const Args &a, Context &ctx)
{
    return ctx.list(a);
}

Value fDate(const Args &a, Context &ctx)
{
    if (a.size() < 2)
        return toDate(a.at(0), ctx);
    if (!a.at(0).is(T::String) || !a.at(1).is(T::String))
        return Value();
    ctx.stepText(a.at(0).asString().size());
    return dateOrNull(parseWithFormat(a.at(0).asString(), a.at(1).asString()));
}

Value fDur(const Args &a, Context &)
{
    const Value &v = a.at(0);
    if (v.is(T::Duration))
        return v;
    if (v.is(T::String)) {
        const auto d = parseDuration(v.asString());
        return d ? Value::duration(*d) : Value();
    }
    return Value();
}

Value fNumber(const Args &a, Context &ctx)
{
    const Value &v = a.at(0);
    if (v.is(T::Number))
        return v;
    if (v.is(T::String)) {
        ctx.stepText(v.asString().size());
        static const QRegularExpression re(u"-?\\d+(?:\\.\\d+)?"_s);
        const QRegularExpressionMatch m = re.match(v.asString());
        return m.hasMatch() ? Value::number(m.captured().toDouble()) : Value();
    }
    return Value();
}

Value fString(const Args &a, Context &ctx)
{
    return ctx.text(toText(a.at(0), ctx));
}

Value makeLink(const Args &a, Context &ctx, bool embed)
{
    const Value &v = a.at(0);
    LinkValue l;
    if (v.is(T::Link)) {
        l = v.asLink();
    } else if (v.is(T::String)) {
        // "Page#Heading": the subpath after '#' (left(-1) is the whole text).
        const QString &target = v.asString();
        ctx.stepText(target.size());
        const qsizetype hash = target.indexOf(u'#');
        l = ctx.resolveLink(target.left(hash), QString(), hash < 0 ? QString() : target.mid(hash + 1), false,
                            ctx.options().currentPath);
    } else {
        return Value();
    }
    if (a.size() > 1 && a.at(1).is(T::String))
        l.display = a.at(1).asString();
    l.embed = embed;
    return Value::link(l);
}

Value fLink(const Args &a, Context &ctx)
{
    return makeLink(a, ctx, false);
}

Value fEmbed(const Args &a, Context &ctx)
{
    return makeLink({a.at(0)}, ctx, a.size() < 2 || a.at(1).truthy());
}

Value fElink(const Args &a, Context &ctx)
{
    const QString url = toText(a.at(0), ctx).trimmed();
    if (url.isEmpty() || url.contains(u' ') || url.contains(u')'))
        return Value();
    QString display = a.size() > 1 ? toText(a.at(1), ctx) : url;
    display.remove(u'[').remove(u']');
    return ctx.text(u'[' + display + u"]("_s + url + u')');
}

Value fTypeof(const Args &a, Context &)
{
    return Value::string(a.at(0).typeName());
}

// ---- numbers ---------------------------------------------------------------------------------------

Value fRound(const Args &a, Context &)
{
    if (!a.at(0).is(T::Number))
        return Value();
    const int digits = a.size() > 1 ? std::clamp(intArg(a.at(1), 0), 0, 15) : 0;
    const double p = std::pow(10.0, digits);
    return Value::number(std::round(a.at(0).asNumber() * p) / p);
}

Value fTrunc(const Args &a, Context &)
{
    return a.at(0).is(T::Number) ? Value::number(std::trunc(a.at(0).asNumber())) : Value();
}

Value fFloor(const Args &a, Context &)
{
    return a.at(0).is(T::Number) ? Value::number(std::floor(a.at(0).asNumber())) : Value();
}

Value fCeil(const Args &a, Context &)
{
    return a.at(0).is(T::Number) ? Value::number(std::ceil(a.at(0).asNumber())) : Value();
}

/// min() / max(): the smallest (sign -1) or largest (sign 1) value that is not null.
Value extreme(const Args &a, Context &ctx, int sign)
{
    Value best;
    for (const Value &v : spread(a)) {
        ctx.step();
        if (!v.isNull() && (best.isNull() || sign * compare(v, best) > 0))
            best = v;
    }
    return best;
}

Value fMin(const Args &a, Context &ctx)
{
    return extreme(a, ctx, -1);
}

Value fMax(const Args &a, Context &ctx)
{
    return extreme(a, ctx, 1);
}

/// The values (not empty) combined as the expression `a op b` does: reduce(list, "+"), sum(),
/// product(), average(). Null for an operator reduce() does not know.
Value fold(const ValueList &items, const QString &op, Context &ctx)
{
    Expr bin;
    bin.kind = Expr::Kind::Binary;
    if (op == u"&")
        bin.name = u"and"_s;
    else if (op == u"|")
        bin.name = u"or"_s;
    else if (op == u"+" || op == u"-" || op == u"*" || op == u"/")
        bin.name = op;
    else
        return Value();
    const auto lhs = std::make_shared<Expr>();
    const auto rhs = std::make_shared<Expr>();
    bin.args = {lhs, rhs};
    Value acc = items.first();
    for (qsizetype i = 1; i < items.size(); ++i) {
        lhs->value = acc;
        rhs->value = items.at(i);
        acc = evaluate(bin, ctx, Value());
    }
    return acc;
}

Value fSum(const Args &a, Context &ctx)
{
    const ValueList &items = spread(a);
    return items.isEmpty() ? Value::number(0) : fold(items, u"+"_s, ctx);
}

Value fProduct(const Args &a, Context &ctx)
{
    const ValueList &items = spread(a);
    return items.isEmpty() ? Value::number(1) : fold(items, u"*"_s, ctx);
}

Value fAverage(const Args &a, Context &ctx)
{
    const ValueList &items = spread(a);
    if (items.isEmpty())
        return Value();
    const Value total = fold(items, u"+"_s, ctx);
    return total.is(T::Number) ? Value::number(total.asNumber() / double(items.size())) : Value();
}

Value byKey(const Args &a, Context &ctx, int sign)
{
    if (!a.at(0).is(T::List) || !a.at(1).is(T::Function))
        return Value();
    Value best, bestKey;
    bool any = false;
    for (const Value &v : a.at(0).asList()) {
        const Value key = callFunction(a.at(1).asFunction(), {v}, ctx);
        if (!any || sign * compare(key, bestKey) > 0) {
            best = v;
            bestKey = key;
            any = true;
        }
    }
    return best;
}

Value fMinby(const Args &a, Context &ctx)
{
    return byKey(a, ctx, -1);
}

Value fMaxby(const Args &a, Context &ctx)
{
    return byKey(a, ctx, 1);
}

Value fReduce(const Args &a, Context &ctx)
{
    if (!a.at(0).is(T::List) || a.at(0).asList().isEmpty())
        return Value();
    const ValueList &items = a.at(0).asList();
    if (a.at(1).is(T::String))
        return fold(items, a.at(1).asString().trimmed(), ctx);
    if (!a.at(1).is(T::Function))
        return Value();
    Value acc = items.first();
    for (qsizetype i = 1; i < items.size(); ++i)
        acc = callFunction(a.at(1).asFunction(), {acc, items.at(i)}, ctx);
    return acc;
}

// ---- lists and objects -----------------------------------------------------------------------------

Value fContains(const Args &a, Context &ctx)
{
    return Value::boolean(containsImpl(a.at(0), a.at(1), Qt::CaseSensitive, ctx));
}

Value fIcontains(const Args &a, Context &ctx)
{
    return Value::boolean(containsImpl(a.at(0), a.at(1), Qt::CaseInsensitive, ctx));
}

/// econtains(): a list holds the value itself (not a value containing it); else as contains().
Value fEcontains(const Args &a, Context &ctx)
{
    if (!a.at(0).is(T::List))
        return fContains(a, ctx);
    ctx.step(a.at(0).asList().size());
    return Value::boolean(std::ranges::any_of(a.at(0).asList(), [&](const Value &v) { return equals(v, a.at(1)); }));
}

Value fContainsword(const Args &a, Context &ctx)
{
    if (!a.at(1).is(T::String))
        return Value::boolean(false);
    const QString word = a.at(1).asString().toLower();
    const auto one = [&](const Value &v) {
        if (!v.is(T::String))
            return false;
        ctx.stepText(v.asString().size());
        return text::indexOfWholeWord(v.asString().toLower(), word) >= 0;
    };
    if (a.at(0).is(T::List)) {
        for (const Value &v : a.at(0).asList()) {
            ctx.step();
            if (one(v))
                return Value::boolean(true);
        }
        return Value::boolean(false);
    }
    return Value::boolean(one(a.at(0)));
}

Value fExtract(const Args &a, Context &ctx)
{
    QList<std::pair<QString, Value>> fields;
    for (qsizetype i = 1; i < a.size(); ++i) {
        const QString key = canonicalKey(toText(a.at(i), ctx));
        ctx.stepText(key.size());
        fields.append({key, fieldOf(a.at(0), key, ctx)});
    }
    return Value::map(fields);
}

Value fSort(const Args &a, Context &ctx)
{
    if (!a.at(0).is(T::List))
        return a.at(0);
    ValueList items = a.at(0).asList();
    ctx.step(items.size());
    const auto less = [](const Value &x, const Value &y) { return compare(x, y) < 0; };
    if (a.size() > 1 && a.at(1).is(T::Function)) {
        const Function &f = a.at(1).asFunction();
        if (f.params.size() >= 2) {
            // A comparator: x comes first when it gives a negative number.
            std::ranges::stable_sort(items, [&](const Value &x, const Value &y) {
                const Value order = callFunction(f, {x, y}, ctx);
                return order.is(T::Number) && order.asNumber() < 0;
            });
        } else {
            QList<std::pair<Value, Value>> keyed;
            Kept kept;
            for (const Value &v : std::as_const(items)) {
                keyed.append({callFunction(f, {v}, ctx), v});
                kept.add(keyed.constLast().first);
            }
            std::ranges::stable_sort(keyed, less, &std::pair<Value, Value>::first);
            items.clear();
            for (const auto &k : std::as_const(keyed))
                items.append(k.second);
        }
    } else {
        std::ranges::stable_sort(items, less);
    }
    return ctx.list(items);
}

Value fReverse(const Args &a, Context &ctx)
{
    if (a.at(0).is(T::String)) {
        QString s = a.at(0).asString();
        std::ranges::reverse(s);
        return ctx.text(s);
    }
    if (!a.at(0).is(T::List))
        return a.at(0);
    ValueList items = a.at(0).asList();
    std::ranges::reverse(items);
    return ctx.list(items);
}

Value fUnique(const Args &a, Context &ctx)
{
    if (!a.at(0).is(T::List))
        return a.at(0);
    ValueList out;
    for (const Value &v : a.at(0).asList()) {
        ctx.step(out.size() + 1);
        if (std::none_of(out.cbegin(), out.cend(), [&](const Value &o) { return equals(o, v); }))
            out.append(v);
    }
    return ctx.list(out);
}

void flatten(const ValueList &items, int depth, ValueList &out, Context &ctx)
{
    for (const Value &v : items) {
        ctx.step();
        if (v.is(T::List) && depth > 0)
            flatten(v.asList(), depth - 1, out, ctx);
        else
            out.append(v);
    }
}

Value fFlat(const Args &a, Context &ctx)
{
    if (!a.at(0).is(T::List))
        return a.at(0);
    ValueList out;
    flatten(a.at(0).asList(), a.size() > 1 ? std::clamp(intArg(a.at(1), 1), 0, 64) : 1, out, ctx);
    return ctx.list(out);
}

Value fSlice(const Args &a, Context &ctx)
{
    if (!a.at(0).is(T::List))
        return Value();
    const ValueList &items = a.at(0).asList();
    const int size = int(items.size());
    const auto at = [size](int i) { return std::clamp(i < 0 ? size + i : i, 0, size); };
    const int from = a.size() > 1 ? at(intArg(a.at(1), 0)) : 0;
    const int to = a.size() > 2 ? at(intArg(a.at(2), size)) : size;
    return ctx.list(from < to ? items.mid(from, to - from) : ValueList());
}

Value fFilter(const Args &a, Context &ctx)
{
    if (!a.at(0).is(T::List) || !a.at(1).is(T::Function))
        return Value();
    ValueList out;
    for (const Value &v : a.at(0).asList()) {
        if (callPredicate(a.at(1), v, ctx))
            out.append(v);
    }
    return ctx.list(out);
}

Value fMap(const Args &a, Context &ctx)
{
    if (!a.at(0).is(T::List) || !a.at(1).is(T::Function))
        return Value();
    ValueList out;
    Kept kept;
    for (const Value &v : a.at(0).asList()) {
        out.append(callFunction(a.at(1).asFunction(), {v}, ctx));
        kept.add(out.constLast());
    }
    return ctx.list(out);
}

/// any / all / none: how many of the values — (list, predicate), (list) or values — pass, of how many.
std::pair<qsizetype, qsizetype> passing(const Args &a, Context &ctx)
{
    const bool predicate = a.size() == 2 && a.at(1).is(T::Function) && a.at(0).is(T::List);
    const ValueList &items = predicate ? a.at(0).asList() : spread(a);
    qsizetype hits = 0;
    for (const Value &v : items) {
        ctx.step();
        if (predicate ? callPredicate(a.at(1), v, ctx) : v.truthy())
            ++hits;
    }
    return {hits, items.size()};
}

Value fAny(const Args &a, Context &ctx)
{
    return Value::boolean(passing(a, ctx).first > 0);
}

Value fAll(const Args &a, Context &ctx)
{
    const auto [hits, total] = passing(a, ctx);
    return Value::boolean(hits == total);
}

Value fNone(const Args &a, Context &ctx)
{
    return Value::boolean(passing(a, ctx).first == 0);
}

Value fNonnull(const Args &a, Context &ctx)
{
    ValueList out = spread(a);
    ctx.step(out.size()); // (every value looked at, also those left out)
    out.removeIf([](const Value &v) { return v.isNull(); });
    return ctx.list(out);
}

Value fFirstvalue(const Args &a, Context &ctx)
{
    for (const Value &v : spread(a)) {
        ctx.step();
        if (!v.isNull())
            return v;
    }
    return Value();
}

Value fJoin(const Args &a, Context &ctx)
{
    if (!a.at(0).is(T::List))
        return ctx.text(toText(a.at(0), ctx));
    const QString sep = a.size() > 1 ? toText(a.at(1), ctx) : u", "_s;
    QStringList parts;
    qint64 length = 0;
    for (const Value &v : a.at(0).asList()) {
        ctx.step();
        parts.append(toText(v, ctx));
        length += parts.constLast().size() + sep.size(); // (a long separator between many values)
        checkSize(1, length);
    }
    return ctx.text(parts.join(sep));
}

Value fLength(const Args &a, Context &)
{
    const Value &v = a.at(0);
    switch (v.type()) {
    case T::List: return Value::number(double(v.asList().size()));
    case T::String: return Value::number(double(v.asString().size()));
    case T::Object: return Value::number(double(v.asObject().keys().size()));
    default: return Value::number(0);
    }
}

Value defaultOf(const Value &v, const Value &d, bool swizzle, Context &ctx)
{
    if (swizzle && v.is(T::List)) {
        ValueList out;
        for (const Value &item : v.asList())
            out.append(item.isNull() ? d : item);
        return ctx.list(out);
    }
    return v.isNull() ? d : v;
}

Value fDefault(const Args &a, Context &ctx)
{
    return defaultOf(a.at(0), a.at(1), true, ctx);
}

Value fLdefault(const Args &a, Context &ctx)
{
    return defaultOf(a.at(0), a.at(1), false, ctx);
}

Value fChoice(const Args &a, Context &)
{
    return a.at(0).truthy() ? a.at(1) : a.at(2);
}

// ---- strings ---------------------------------------------------------------------------------------

/// lower() / upper(): a string converted, the strings of a list (and of lists in it) too.
Value convertStrings(const Value &v, QString (*convert)(const QString &), Context &ctx)
{
    if (v.is(T::String))
        return ctx.text(convert(v.asString()));
    if (!v.is(T::List))
        return v;
    ValueList out;
    for (const Value &item : v.asList())
        out.append(convertStrings(item, convert, ctx));
    return ctx.list(out);
}

Value fLower(const Args &a, Context &ctx)
{
    return convertStrings(a.at(0), [](const QString &s) { return s.toLower(); }, ctx);
}

Value fUpper(const Args &a, Context &ctx)
{
    return convertStrings(a.at(0), [](const QString &s) { return s.toUpper(); }, ctx);
}

Value fStartswith(const Args &a, Context &ctx)
{
    return Value::boolean(!a.at(0).isNull() && toText(a.at(0), ctx).startsWith(toText(a.at(1), ctx)));
}

Value fEndswith(const Args &a, Context &ctx)
{
    return Value::boolean(!a.at(0).isNull() && toText(a.at(0), ctx).endsWith(toText(a.at(1), ctx)));
}

Value fReplace(const Args &a, Context &ctx)
{
    if (!a.at(0).is(T::String))
        return a.at(0);
    QString s = a.at(0).asString();
    const QString from = toText(a.at(1), ctx);
    if (from.isEmpty())
        return a.at(0);
    const QString to = toText(a.at(2), ctx);
    if (s.size() / from.size() * std::max<qsizetype>(1, to.size()) > 1000000)
        return a.at(0);
    return ctx.text(s.replace(from, to));
}

Value fSplit(const Args &a, Context &ctx)
{
    if (!a.at(0).is(T::String))
        return Value();
    const QString &s = a.at(0).asString();
    const QString sep = toText(a.at(1), ctx);
    checkSize(sep.isEmpty() ? s.size() : s.count(sep) + 1, s.size()); // (before making the parts)
    ctx.stepText(s.size());
    ValueList out;
    if (sep.isEmpty()) {
        for (const QChar c : s)
            out.append(Value::string(QString(c)));
    } else {
        const QStringList parts = s.split(sep);
        for (const QString &p : parts)
            out.append(Value::string(p));
    }
    if (a.size() > 2)
        out.resize(std::clamp<qsizetype>(intArg(a.at(2), int(out.size())), 0, out.size()));
    return ctx.list(out);
}

Value fSubstring(const Args &a, Context &ctx)
{
    if (!a.at(0).is(T::String))
        return Value();
    const QString &s = a.at(0).asString();
    const int size = int(s.size());
    int from = std::clamp(intArg(a.at(1), 0), 0, size);
    int to = a.size() > 2 ? std::clamp(intArg(a.at(2), size), 0, size) : size;
    if (from > to)
        std::swap(from, to);
    return ctx.text(s.mid(from, to - from));
}

Value pad(const Args &a, Context &ctx, bool left)
{
    const QString s = toText(a.at(0), ctx);
    const int width = std::clamp(intArg(a.at(1), 0), 0, 10000);
    const QString with = a.size() > 2 ? toText(a.at(2), ctx) : u" "_s;
    if (with.isEmpty() || s.size() >= width)
        return ctx.text(s);
    const qsizetype missing = width - s.size();
    const QString fill = with.repeated(missing / with.size() + 1).left(missing);
    return ctx.text(left ? QString(fill + s) : QString(s + fill));
}

Value fPadleft(const Args &a, Context &ctx)
{
    return pad(a, ctx, true);
}

Value fPadright(const Args &a, Context &ctx)
{
    return pad(a, ctx, false);
}

Value fTruncate(const Args &a, Context &ctx)
{
    if (!a.at(0).is(T::String))
        return a.at(0);
    const QString &s = a.at(0).asString();
    const int width = std::max(0, intArg(a.at(1), int(s.size())));
    if (s.size() <= width)
        return a.at(0);
    const QString suffix = a.size() > 2 ? toText(a.at(2), ctx) : u"..."_s;
    return ctx.text(s.left(std::max<qsizetype>(0, width - suffix.size())) + suffix);
}

// ---- dates, links, display -------------------------------------------------------------------------

Value fMeta(const Args &a, Context &)
{
    if (!a.at(0).is(T::Link))
        return Value();
    const LinkValue &l = a.at(0).asLink();
    const QString type = l.subpath.isEmpty() ? u"file"_s : l.subpath.startsWith(u'^') ? u"block"_s : u"header"_s;
    return Value::map({{u"display"_s, l.display.isEmpty() ? Value() : Value::string(l.display)},
                       {u"embed"_s, Value::boolean(l.embed)},
                       {u"path"_s, Value::string(l.path)},
                       {u"subpath"_s, l.subpath.isEmpty() ? Value() : Value::string(l.subpath)},
                       {u"type"_s, Value::string(type)}});
}

Value fDateformat(const Args &a, Context &)
{
    if (!a.at(0).is(T::Date) || !a.at(1).is(T::String))
        return Value();
    return Value::string(formatLuxon(a.at(0).asDate(), a.at(1).asString().left(200)));
}

Value fStriptime(const Args &a, Context &)
{
    if (!a.at(0).is(T::Date))
        return Value();
    return Value::date(a.at(0).asDate().at.date());
}

Value fLocaltime(const Args &a, Context &)
{
    return a.at(0).is(T::Date) ? a.at(0) : Value();
}

Value fDisplay(const Args &a, Context &ctx)
{
    ctx.step(a.at(0).extent().items); // (every value shown)
    return ctx.text(toPlainText(a.at(0)));
}

const QHash<QString, Builtin> &builtins()
{
    static const QHash<QString, Builtin> table = {
            {u"object"_s, {0, -1, fObject}},       {u"list"_s, {0, -1, fList}},
            {u"array"_s, {0, -1, fList}},          {u"date"_s, {1, 2, fDate}},
            {u"dur"_s, {1, 1, fDur}},              {u"number"_s, {1, 1, fNumber}},
            {u"string"_s, {1, 1, fString}},        {u"link"_s, {1, 2, fLink}},
            {u"embed"_s, {1, 2, fEmbed}},          {u"elink"_s, {1, 2, fElink}},
            {u"typeof"_s, {1, 1, fTypeof}},        {u"round"_s, {1, 2, fRound}},
            {u"trunc"_s, {1, 1, fTrunc}},          {u"floor"_s, {1, 1, fFloor}},
            {u"ceil"_s, {1, 1, fCeil}},            {u"min"_s, {1, -1, fMin}},
            {u"max"_s, {1, -1, fMax}},             {u"sum"_s, {1, -1, fSum}},
            {u"product"_s, {1, -1, fProduct}},     {u"average"_s, {1, -1, fAverage}},
            {u"minby"_s, {2, 2, fMinby}},          {u"maxby"_s, {2, 2, fMaxby}},
            {u"reduce"_s, {2, 2, fReduce}},        {u"contains"_s, {2, 2, fContains}},
            {u"icontains"_s, {2, 2, fIcontains}},  {u"econtains"_s, {2, 2, fEcontains}},
            {u"containsword"_s, {2, 2, fContainsword}}, {u"extract"_s, {1, -1, fExtract}},
            {u"sort"_s, {1, 2, fSort}},            {u"reverse"_s, {1, 1, fReverse}},
            {u"unique"_s, {1, 1, fUnique}},        {u"flat"_s, {1, 2, fFlat}},
            {u"slice"_s, {1, 3, fSlice}},          {u"filter"_s, {2, 2, fFilter}},
            {u"map"_s, {2, 2, fMap}},              {u"any"_s, {1, -1, fAny}},
            {u"all"_s, {1, -1, fAll}},             {u"none"_s, {1, -1, fNone}},
            {u"nonnull"_s, {0, -1, fNonnull}},     {u"firstvalue"_s, {1, -1, fFirstvalue}},
            {u"join"_s, {1, 2, fJoin}},            {u"length"_s, {1, 1, fLength}},
            {u"default"_s, {2, 2, fDefault}},      {u"ldefault"_s, {2, 2, fLdefault}},
            {u"choice"_s, {3, 3, fChoice}},        {u"lower"_s, {1, 1, fLower}},
            {u"upper"_s, {1, 1, fUpper}},          {u"startswith"_s, {2, 2, fStartswith}},
            {u"endswith"_s, {2, 2, fEndswith}},    {u"replace"_s, {3, 3, fReplace}},
            {u"split"_s, {2, 3, fSplit}},          {u"substring"_s, {2, 3, fSubstring}},
            {u"padleft"_s, {2, 3, fPadleft}},      {u"padright"_s, {2, 3, fPadright}},
            {u"truncate"_s, {2, 3, fTruncate}},    {u"meta"_s, {1, 1, fMeta}},
            {u"dateformat"_s, {2, 2, fDateformat}}, {u"striptime"_s, {1, 1, fStriptime}},
            {u"localtime"_s, {1, 1, fLocaltime}},  {u"display"_s, {1, 1, fDisplay}},
    };
    return table;
}

} // namespace

const Builtin *findBuiltin(const QString &name)
{
    const auto &table = builtins();
    const auto it = table.constFind(name.toLower());
    return it == table.constEnd() ? nullptr : &*it;
}

bool isUnsupportedFunction(const QString &name)
{
    static const QStringList names{u"regextest"_s, u"regexmatch"_s, u"regexreplace"_s, u"csv"_s};
    return names.contains(name.toLower());
}

} // namespace brulion::dataview
