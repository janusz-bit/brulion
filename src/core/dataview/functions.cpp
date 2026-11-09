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
ValueList spread(const Args &a)
{
    if (a.size() == 1 && a.first().is(T::List))
        return a.first().asList();
    return a;
}

Value mapList(const Value &v, Value (*f)(const Value &, Context &), Context &ctx)
{
    if (!v.is(T::List))
        return f(v, ctx);
    ValueList out;
    for (const Value &item : v.asList())
        out.append(mapList(item, f, ctx));
    return Value::list(out);
}

bool callPredicate(const Value &fn, const Value &item, Context &ctx)
{
    return callFunction(fn.asFunction(), {item}, ctx).truthy();
}

bool isFn(const Value &v)
{
    return v.is(T::Function);
}

double num(const Value &v, double fallback = 0)
{
    return v.is(T::Number) ? v.asNumber() : fallback;
}

/// The plain (Markdown-free) text of a value, for string functions given a link or a number.
QString str(const Value &v, Context &ctx)
{
    return v.is(T::String) ? v.asString() : toText(v, ctx);
}

int intArg(const Value &v, int fallback)
{
    if (!v.is(T::Number) || std::isnan(v.asNumber()))
        return fallback;
    return int(std::clamp(v.asNumber(), -1e9, 1e9));
}

bool containsImpl(const Value &hay, const Value &needle, bool caseInsensitive, bool exact, Context &ctx)
{
    ctx.step();
    switch (hay.type()) {
    case T::String:
        if (!needle.is(T::String))
            return false;
        return hay.asString().contains(needle.asString(), caseInsensitive ? Qt::CaseInsensitive : Qt::CaseSensitive);
    case T::List:
        for (const Value &item : hay.asList()) {
            if (exact ? equals(item, needle) : containsImpl(item, needle, caseInsensitive, false, ctx))
                return true;
            if (exact && caseInsensitive && item.is(T::String) && needle.is(T::String)
                && item.asString().compare(needle.asString(), Qt::CaseInsensitive) == 0)
                return true;
        }
        return false;
    case T::Object:
        return needle.is(T::String) && hay.asObject().field(canonicalKey(needle.asString())).has_value();
    case T::Link:
        if (needle.is(T::Link))
            return equals(hay, needle);
        if (needle.is(T::String))
            return hay.asLink().path.contains(needle.asString(), caseInsensitive ? Qt::CaseInsensitive : Qt::CaseSensitive);
        return false;
    default: return equals(hay, needle);
    }
}

/// Luxon's format tokens for dateformat(): yyyy, MM, dd, HH, mm, ss, MMMM, EEEE, …
QString formatLuxon(const DateValue &d, const QString &fmt)
{
    const QLocale c = QLocale::c();
    const QDate day = d.at.date();
    const QTime t = d.at.time();
    QString out;
    qsizetype i = 0;
    while (i < fmt.size()) {
        const QChar ch = fmt.at(i);
        if (ch == u'\'') {
            const qsizetype end = fmt.indexOf(u'\'', i + 1);
            out += fmt.mid(i + 1, end < 0 ? -1 : end - i - 1);
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
        const auto pad = [](int v, int w) { return QString::number(v).rightJustified(w, u'0'); };
        int weekYear = 0;
        const int week = day.weekNumber(&weekYear);
        switch (ch.unicode()) {
        case 'y': out += n == 2 ? pad(day.year() % 100, 2) : pad(day.year(), int(std::max<qsizetype>(n, 1))); break;
        case 'M':
        case 'L':
            out += n >= 4 ? c.monthName(day.month(), QLocale::LongFormat)
                   : n == 3 ? c.monthName(day.month(), QLocale::ShortFormat)
                            : pad(day.month(), int(n));
            break;
        case 'd': out += pad(day.day(), int(n)); break;
        case 'E':
        case 'c':
            out += n >= 4 ? c.dayName(day.dayOfWeek(), QLocale::LongFormat)
                   : n == 3 ? c.dayName(day.dayOfWeek(), QLocale::ShortFormat)
                            : QString::number(day.dayOfWeek());
            break;
        case 'H': out += pad(t.hour(), int(n)); break;
        case 'h': out += pad(t.hour() % 12 == 0 ? 12 : t.hour() % 12, int(n)); break;
        case 'm': out += pad(t.minute(), int(n)); break;
        case 's': out += pad(t.second(), int(n)); break;
        case 'S': out += pad(t.msec(), 3); break;
        case 'a': out += t.hour() < 12 ? u"AM"_s : u"PM"_s; break;
        case 'W': out += pad(week, int(n)); break;
        case 'k': out += pad(weekYear, int(n)); break;
        case 'q': out += QString::number((day.month() - 1) / 3 + 1); break;
        case 'o': out += pad(day.dayOfYear(), int(n)); break;
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
            qt += fmt.mid(i, end < 0 ? -1 : end - i + 1);
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
        const QString s = v.asString().trimmed();
        if (isDateWord(s.toLower())) {
            if (const auto d = dateWord(s.toLower(), ctx.options()))
                return Value::date(*d);
        }
        if (const auto d = parseDate(s))
            return Value::date(*d);
        return Value();
    }
    case T::Link: {
        // A link to a daily note (or a page named by a date): its day.
        const Value day = fieldOf(fieldOf(v, u"file"_s, ctx), u"day"_s, ctx);
        if (day.is(T::Date))
            return day;
        if (const auto d = parseDate(paths::pageTitle(v.asLink().path)))
            return Value::date(*d);
        return Value();
    }
    case T::Number: return Value::date(DateValue{QDateTime::fromMSecsSinceEpoch(qint64(v.asNumber())), true});
    default: return Value();
    }
}

// ---- constructors ----------------------------------------------------------------------------------

Value fObject(const Args &a, Context &ctx)
{
    QList<std::pair<QString, Value>> fields;
    for (qsizetype i = 0; i + 1 < a.size(); i += 2)
        fields.append({canonicalKey(str(a.at(i), ctx)), a.at(i + 1)});
    return Value::map(fields);
}

Value fList(const Args &a, Context &)
{
    return Value::list(a);
}

Value fDate(const Args &a, Context &ctx)
{
    if (a.size() == 2) {
        if (!a.at(0).is(T::String) || !a.at(1).is(T::String))
            return Value();
        const auto d = parseWithFormat(a.at(0).asString(), a.at(1).asString());
        return d ? Value::date(*d) : Value();
    }
    return toDate(a.at(0), ctx);
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

Value fNumber(const Args &a, Context &)
{
    const Value &v = a.at(0);
    if (v.is(T::Number))
        return v;
    if (v.is(T::String)) {
        static const QRegularExpression re(u"-?\\d+(?:\\.\\d+)?"_s);
        const QRegularExpressionMatch m = re.match(v.asString());
        return m.hasMatch() ? Value::number(m.captured().toDouble()) : Value();
    }
    return Value();
}

Value fString(const Args &a, Context &ctx)
{
    return Value::string(toText(a.at(0), ctx));
}

Value makeLink(const Args &a, Context &ctx, bool embed)
{
    const Value &v = a.at(0);
    LinkValue l;
    if (v.is(T::Link)) {
        l = v.asLink();
    } else if (v.is(T::String)) {
        QString target = v.asString();
        QString subpath;
        if (const qsizetype hash = target.indexOf(u'#'); hash >= 0) {
            subpath = target.mid(hash + 1);
            target = target.left(hash);
        }
        l = ctx.resolveLink(target, QString(), subpath, false, ctx.options().currentPath);
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
    if (a.size() > 1 && !a.at(1).truthy())
        return makeLink({a.at(0)}, ctx, false);
    return makeLink({a.at(0)}, ctx, true);
}

Value fElink(const Args &a, Context &ctx)
{
    const QString url = str(a.at(0), ctx).trimmed();
    if (url.isEmpty() || url.contains(u' ') || url.contains(u')'))
        return Value();
    QString display = a.size() > 1 ? str(a.at(1), ctx) : url;
    display.remove(u'[').remove(u']');
    return Value::string(u'[' + display + u"]("_s + url + u')');
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

Value extreme(const Args &a, Context &ctx, int sign)
{
    Value best;
    bool any = false;
    for (const Value &v : spread(a)) {
        ctx.step();
        if (v.isNull())
            continue;
        if (!any || sign * compare(v, best) > 0) {
            best = v;
            any = true;
        }
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

Value fold(const ValueList &items, const QString &op, Context &ctx);

Value fSum(const Args &a, Context &ctx)
{
    const ValueList items = spread(a);
    if (items.isEmpty())
        return Value::number(0);
    return fold(items, u"+"_s, ctx);
}

Value fProduct(const Args &a, Context &ctx)
{
    const ValueList items = spread(a);
    if (items.isEmpty())
        return Value::number(1);
    return fold(items, u"*"_s, ctx);
}

Value fAverage(const Args &a, Context &ctx)
{
    const ValueList items = spread(a);
    if (items.isEmpty())
        return Value();
    const Value total = fold(items, u"+"_s, ctx);
    if (total.is(T::Number))
        return Value::number(total.asNumber() / double(items.size()));
    return Value();
}

Value byKey(const Args &a, Context &ctx, int sign)
{
    if (!a.at(0).is(T::List) || !isFn(a.at(1)))
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
    if (!isFn(a.at(1)))
        return Value();
    Value acc = items.first();
    for (qsizetype i = 1; i < items.size(); ++i)
        acc = callFunction(a.at(1).asFunction(), {acc, items.at(i)}, ctx);
    return acc;
}

// ---- lists and objects -----------------------------------------------------------------------------

Value fContains(const Args &a, Context &ctx)
{
    return Value::boolean(containsImpl(a.at(0), a.at(1), false, false, ctx));
}

Value fIcontains(const Args &a, Context &ctx)
{
    return Value::boolean(containsImpl(a.at(0), a.at(1), true, false, ctx));
}

Value fEcontains(const Args &a, Context &ctx)
{
    return Value::boolean(containsImpl(a.at(0), a.at(1), false, true, ctx));
}

Value fContainsword(const Args &a, Context &ctx)
{
    if (!a.at(1).is(T::String))
        return Value::boolean(false);
    const QString word = a.at(1).asString().toLower();
    const auto one = [&](const Value &v) {
        return v.is(T::String) && text::indexOfWholeWord(v.asString().toLower(), word) >= 0;
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
        const QString key = canonicalKey(str(a.at(i), ctx));
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
    if (a.size() > 1 && isFn(a.at(1))) {
        const Function &f = a.at(1).asFunction();
        if (f.params.size() >= 2) {
            std::ranges::stable_sort(
                    items, [&](const Value &x, const Value &y) { return num(callFunction(f, {x, y}, ctx)) < 0; });
        } else {
            QList<std::pair<Value, Value>> keyed;
            for (const Value &v : std::as_const(items))
                keyed.append({callFunction(f, {v}, ctx), v});
            std::ranges::stable_sort(keyed, [](const auto &x, const auto &y) { return compare(x.first, y.first) < 0; });
            items.clear();
            for (const auto &k : std::as_const(keyed))
                items.append(k.second);
        }
    } else {
        std::ranges::stable_sort(items, [](const Value &x, const Value &y) { return compare(x, y) < 0; });
    }
    return Value::list(items);
}

Value fReverse(const Args &a, Context &)
{
    if (a.at(0).is(T::String)) {
        QString s = a.at(0).asString();
        std::ranges::reverse(s);
        return Value::string(s);
    }
    if (!a.at(0).is(T::List))
        return a.at(0);
    ValueList items = a.at(0).asList();
    std::ranges::reverse(items);
    return Value::list(items);
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
    return Value::list(out);
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
    return Value::list(out);
}

Value fSlice(const Args &a, Context &)
{
    if (!a.at(0).is(T::List))
        return Value();
    const ValueList &items = a.at(0).asList();
    const int size = int(items.size());
    const auto at = [size](int i) { return std::clamp(i < 0 ? size + i : i, 0, size); };
    const int from = a.size() > 1 ? at(intArg(a.at(1), 0)) : 0;
    const int to = a.size() > 2 ? at(intArg(a.at(2), size)) : size;
    return Value::list(from < to ? items.mid(from, to - from) : ValueList());
}

Value fFilter(const Args &a, Context &ctx)
{
    if (!a.at(0).is(T::List) || !isFn(a.at(1)))
        return Value();
    ValueList out;
    for (const Value &v : a.at(0).asList()) {
        if (callPredicate(a.at(1), v, ctx))
            out.append(v);
    }
    return Value::list(out);
}

Value fMap(const Args &a, Context &ctx)
{
    if (!a.at(0).is(T::List) || !isFn(a.at(1)))
        return Value();
    ValueList out;
    for (const Value &v : a.at(0).asList())
        out.append(callFunction(a.at(1).asFunction(), {v}, ctx));
    return Value::list(out);
}

/// any / all / none: (list, predicate), (list) or values.
Value quantifier(const Args &a, Context &ctx, int kind)
{
    ValueList items;
    Value fn;
    if (a.size() == 2 && isFn(a.at(1)) && a.at(0).is(T::List)) {
        items = a.at(0).asList();
        fn = a.at(1);
    } else {
        items = spread(a);
    }
    int hits = 0;
    for (const Value &v : std::as_const(items)) {
        ctx.step();
        if (fn.isNull() ? v.truthy() : callPredicate(fn, v, ctx))
            ++hits;
    }
    switch (kind) {
    case 0: return Value::boolean(hits > 0);                  // any
    case 1: return Value::boolean(hits == int(items.size())); // all
    default: return Value::boolean(hits == 0);                // none
    }
}

Value fAny(const Args &a, Context &ctx)
{
    return quantifier(a, ctx, 0);
}

Value fAll(const Args &a, Context &ctx)
{
    return quantifier(a, ctx, 1);
}

Value fNone(const Args &a, Context &ctx)
{
    return quantifier(a, ctx, 2);
}

Value fNonnull(const Args &a, Context &)
{
    ValueList out;
    for (const Value &v : spread(a)) {
        if (!v.isNull())
            out.append(v);
    }
    return Value::list(out);
}

Value fFirstvalue(const Args &a, Context &)
{
    for (const Value &v : spread(a)) {
        if (!v.isNull())
            return v;
    }
    return Value();
}

Value fJoin(const Args &a, Context &ctx)
{
    const QString sep = a.size() > 1 ? str(a.at(1), ctx) : u", "_s;
    if (!a.at(0).is(T::List))
        return Value::string(toText(a.at(0), ctx));
    QStringList parts;
    for (const Value &v : a.at(0).asList()) {
        ctx.step();
        parts.append(toText(v, ctx));
    }
    return Value::string(parts.join(sep));
}

Value fLength(const Args &a, Context &)
{
    const Value &v = a.at(0);
    switch (v.type()) {
    case T::List: return Value::number(double(v.asList().size()));
    case T::String: return Value::number(double(v.asString().size()));
    case T::Object: return Value::number(double(v.asObject().keys().size()));
    case T::Null: return Value::number(0);
    default: return Value::number(0);
    }
}

Value defaultOf(const Value &v, const Value &d, bool swizzle)
{
    if (swizzle && v.is(T::List)) {
        ValueList out;
        for (const Value &item : v.asList())
            out.append(item.isNull() ? d : item);
        return Value::list(out);
    }
    return v.isNull() ? d : v;
}

Value fDefault(const Args &a, Context &)
{
    return defaultOf(a.at(0), a.at(1), true);
}

Value fLdefault(const Args &a, Context &)
{
    return defaultOf(a.at(0), a.at(1), false);
}

Value fChoice(const Args &a, Context &)
{
    return a.at(0).truthy() ? a.at(1) : a.at(2);
}

// ---- strings ---------------------------------------------------------------------------------------

Value lowerOne(const Value &v, Context &)
{
    return v.is(T::String) ? Value::string(v.asString().toLower()) : v;
}

Value upperOne(const Value &v, Context &)
{
    return v.is(T::String) ? Value::string(v.asString().toUpper()) : v;
}

Value fLower(const Args &a, Context &ctx)
{
    return mapList(a.at(0), lowerOne, ctx);
}

Value fUpper(const Args &a, Context &ctx)
{
    return mapList(a.at(0), upperOne, ctx);
}

Value fStartswith(const Args &a, Context &ctx)
{
    if (a.at(0).isNull())
        return Value::boolean(false);
    return Value::boolean(str(a.at(0), ctx).startsWith(str(a.at(1), ctx)));
}

Value fEndswith(const Args &a, Context &ctx)
{
    if (a.at(0).isNull())
        return Value::boolean(false);
    return Value::boolean(str(a.at(0), ctx).endsWith(str(a.at(1), ctx)));
}

Value fReplace(const Args &a, Context &ctx)
{
    if (!a.at(0).is(T::String))
        return a.at(0);
    QString s = a.at(0).asString();
    const QString from = str(a.at(1), ctx);
    if (from.isEmpty())
        return a.at(0);
    const QString to = str(a.at(2), ctx);
    if (s.size() / from.size() * std::max<qsizetype>(1, to.size()) > 1000000)
        return a.at(0);
    return Value::string(s.replace(from, to));
}

Value fSplit(const Args &a, Context &ctx)
{
    if (!a.at(0).is(T::String))
        return Value();
    const QString sep = str(a.at(1), ctx);
    QStringList parts = sep.isEmpty() ? QStringList() : a.at(0).asString().split(sep);
    if (sep.isEmpty()) {
        for (const QChar c : a.at(0).asString())
            parts.append(QString(c));
    }
    if (a.size() > 2) {
        const int limit = std::max(0, intArg(a.at(2), int(parts.size())));
        parts = parts.mid(0, limit);
    }
    ValueList out;
    for (const QString &p : std::as_const(parts))
        out.append(Value::string(p));
    return Value::list(out);
}

Value fSubstring(const Args &a, Context &)
{
    if (!a.at(0).is(T::String))
        return Value();
    const QString &s = a.at(0).asString();
    const int size = int(s.size());
    int from = std::clamp(intArg(a.at(1), 0), 0, size);
    int to = a.size() > 2 ? std::clamp(intArg(a.at(2), size), 0, size) : size;
    if (from > to)
        std::swap(from, to);
    return Value::string(s.mid(from, to - from));
}

Value pad(const Args &a, Context &ctx, bool left)
{
    const QString s = str(a.at(0), ctx);
    const int width = std::clamp(intArg(a.at(1), 0), 0, 10000);
    const QString with = a.size() > 2 ? str(a.at(2), ctx) : u" "_s;
    if (with.isEmpty() || s.size() >= width)
        return Value::string(s);
    QString fill;
    while (fill.size() < width - s.size())
        fill += with;
    fill.truncate(width - s.size());
    return Value::string(left ? QString(fill + s) : QString(s + fill));
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
    const QString suffix = a.size() > 2 ? str(a.at(2), ctx) : u"..."_s;
    if (s.size() <= width)
        return a.at(0);
    return Value::string(s.left(std::max<qsizetype>(0, width - suffix.size())) + suffix);
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

Value fDisplay(const Args &a, Context &)
{
    return Value::string(toPlainText(a.at(0)));
}

Value fold(const ValueList &items, const QString &op, Context &ctx)
{
    // The operators of reduce(list, "+"): what the expression `a op b` does.
    Value acc = items.first();
    Expr lhs, rhs, bin;
    lhs.kind = rhs.kind = Expr::Kind::Literal;
    bin.kind = Expr::Kind::Binary;
    if (op == u"&")
        bin.name = u"and"_s;
    else if (op == u"|")
        bin.name = u"or"_s;
    else if (QStringList{u"+"_s, u"-"_s, u"*"_s, u"/"_s}.contains(op))
        bin.name = op;
    else
        return Value();
    auto l = std::make_shared<Expr>(lhs);
    auto r = std::make_shared<Expr>(rhs);
    for (qsizetype i = 1; i < items.size(); ++i) {
        l->value = acc;
        r->value = items.at(i);
        bin.args = {l, r};
        acc = evaluate(bin, ctx, Value());
    }
    return acc;
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
