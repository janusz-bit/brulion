// Evaluating Dataview expressions: names, fields, operators, lambdas.
#include "dataview/eval_p.h"
#include "dataview/messages_p.h"

#include "dates/local.h"
#include "util/paths.h"

#include <cmath>

using namespace Qt::StringLiterals;

namespace brulion::dataview {

namespace {

using Bound = QList<std::pair<QString, Value>>;

/// A row seen as an object: FLATTEN / GROUP BY names first (the latest wins), then the base.
class RowObject : public Object {
public:
    RowObject(Bound extra, Value base) : m_extra(std::move(extra)), m_base(std::move(base)) { }

    std::optional<Value> field(const QString &key) const override
    {
        if (const auto v = extra(key))
            return v;
        if (m_base.is(Value::Type::Object))
            return m_base.asObject().field(key);
        return std::nullopt;
    }
    QStringList keys() const override
    {
        QStringList out = m_base.is(Value::Type::Object) ? m_base.asObject().keys() : QStringList();
        for (const auto &e : m_extra) {
            if (!out.contains(e.first))
                out.append(e.first);
        }
        return out;
    }
    /// A name FLATTEN or GROUP BY set (also dotted ones: `FLATTEN file.tasks`).
    std::optional<Value> extra(const QString &name) const
    {
        for (qsizetype i = m_extra.size() - 1; i >= 0; --i) {
            if (m_extra.at(i).first == name)
                return m_extra.at(i).second;
        }
        return std::nullopt;
    }

private:
    Bound m_extra;
    Value m_base;
};

/// `file.name`, `a.b.c`: a chain of plain names — what FLATTEN names its values after.
bool isNameChain(const Expr &e)
{
    if (e.kind == Expr::Kind::Variable)
        return true;
    return e.kind == Expr::Kind::Field && !e.args.isEmpty() && isNameChain(*e.args.first());
}

/// The chain as one name ("file.tasks").
QString chainName(const Expr &e)
{
    if (e.kind == Expr::Kind::Variable)
        return e.name;
    return chainName(*e.args.first()) + u'.' + e.name;
}

DateValue shifted(const DateValue &d, const Duration &dur, int sign)
{
    DateValue out = d;
    out.at = d.at.addMonths(int(sign * dur.months)).addMSecs(sign * dur.ms);
    out.hasTime = d.hasTime || dur.ms % 86400000 != 0;
    return out;
}

Value arithmetic(const QString &op, const Value &a, const Value &b, Context &ctx)
{
    using T = Value::Type;
    if (a.isNull() || b.isNull())
        return Value();
    if (op == u"+") {
        if (a.is(T::Number) && b.is(T::Number))
            return Value::number(a.asNumber() + b.asNumber());
        if (a.is(T::String) || b.is(T::String))
            return Value::string(toText(a, ctx) + toText(b, ctx));
        if (a.is(T::Date) && b.is(T::Duration))
            return Value::date(shifted(a.asDate(), b.asDuration(), 1));
        if (a.is(T::Duration) && b.is(T::Date))
            return Value::date(shifted(b.asDate(), a.asDuration(), 1));
        if (a.is(T::Duration) && b.is(T::Duration))
            return Value::duration({a.asDuration().months + b.asDuration().months, a.asDuration().ms + b.asDuration().ms});
        if (a.is(T::List) && b.is(T::List))
            return Value::list(a.asList() + b.asList());
        return Value();
    }
    if (op == u"-") {
        if (a.is(T::Number) && b.is(T::Number))
            return Value::number(a.asNumber() - b.asNumber());
        if (a.is(T::Date) && b.is(T::Duration))
            return Value::date(shifted(a.asDate(), b.asDuration(), -1));
        if (a.is(T::Date) && b.is(T::Date))
            return Value::duration({0, b.asDate().at.msecsTo(a.asDate().at)});
        if (a.is(T::Duration) && b.is(T::Duration))
            return Value::duration({a.asDuration().months - b.asDuration().months, a.asDuration().ms - b.asDuration().ms});
        return Value();
    }
    if (op == u"*") {
        if (a.is(T::Number) && b.is(T::Number))
            return Value::number(a.asNumber() * b.asNumber());
        const auto scale = [](const Duration &d, double n) {
            return Value::duration({qint64(std::llround(double(d.months) * n)), qint64(std::llround(double(d.ms) * n))});
        };
        if (a.is(T::Duration) && b.is(T::Number))
            return scale(a.asDuration(), b.asNumber());
        if (a.is(T::Number) && b.is(T::Duration))
            return scale(b.asDuration(), a.asNumber());
        return Value();
    }
    if (op == u"/") {
        if (a.is(T::Number) && b.is(T::Number))
            return b.asNumber() == 0 ? Value() : Value::number(a.asNumber() / b.asNumber());
        if (a.is(T::Duration) && b.is(T::Number) && b.asNumber() != 0) {
            const double n = b.asNumber();
            return Value::duration({qint64(std::llround(double(a.asDuration().months) / n)),
                                    qint64(std::llround(double(a.asDuration().ms) / n))});
        }
        return Value();
    }
    if (op == u"%") {
        if (a.is(T::Number) && b.is(T::Number) && b.asNumber() != 0)
            return Value::number(std::fmod(a.asNumber(), b.asNumber()));
        return Value();
    }
    return Value();
}

Value comparison(const QString &op, const Value &a, const Value &b)
{
    if (op == u"=")
        return Value::boolean(equals(a, b));
    if (op == u"!=")
        return Value::boolean(!equals(a, b));
    const int c = compare(a, b);
    if (op == u"<")
        return Value::boolean(c < 0);
    if (op == u"<=")
        return Value::boolean(c <= 0);
    if (op == u">")
        return Value::boolean(c > 0);
    return Value::boolean(c >= 0); // >=
}

Value dateField(const DateValue &d, const QString &key)
{
    const QDate day = d.at.date();
    const QTime time = d.at.time();
    if (key == u"year")
        return Value::number(day.year());
    if (key == u"month")
        return Value::number(day.month());
    if (key == u"day")
        return Value::number(day.day());
    if (key == u"hour")
        return Value::number(time.hour());
    if (key == u"minute")
        return Value::number(time.minute());
    if (key == u"second")
        return Value::number(time.second());
    if (key == u"millisecond")
        return Value::number(time.msec());
    if (key == u"weekday")
        return Value::number(day.dayOfWeek());
    if (key == u"week" || key == u"weeknumber") {
        int year = 0;
        return Value::number(day.weekNumber(&year));
    }
    if (key == u"weekyear") {
        int year = 0;
        day.weekNumber(&year);
        return Value::number(year);
    }
    if (key == u"quarter")
        return Value::number((day.month() - 1) / 3 + 1);
    if (key == u"ordinal")
        return Value::number(day.dayOfYear());
    if (key == u"ts")
        return Value::number(double(d.at.toMSecsSinceEpoch()));
    return Value();
}

Value durationField(const Duration &d, const QString &key)
{
    const double days = double(d.months) * 30.0 + double(d.ms) / 86400000.0;
    if (key == u"years")
        return Value::number(double(d.months) / 12.0 + double(d.ms) / (365.0 * 86400000.0));
    if (key == u"months")
        return Value::number(double(d.months) + double(d.ms) / (30.0 * 86400000.0));
    if (key == u"weeks")
        return Value::number(days / 7.0);
    if (key == u"days")
        return Value::number(days);
    if (key == u"hours")
        return Value::number(days * 24.0);
    if (key == u"minutes")
        return Value::number(days * 1440.0);
    if (key == u"seconds")
        return Value::number(days * 86400.0);
    if (key == u"milliseconds")
        return Value::number(days * 86400000.0);
    return Value();
}

} // namespace

Value rowObject(const RowData &row)
{
    return Value::object(std::make_shared<RowObject>(row.extra, row.base));
}

Value fieldOf(const Value &value, const QString &key, Context &ctx)
{
    ctx.step();
    switch (value.type()) {
    case Value::Type::Object: return value.asObject().field(key).value_or(Value());
    case Value::Type::Link: {
        const LinkValue &l = value.asLink();
        if (l.resolved) {
            const Value page = ctx.page(l.path);
            if (page.is(Value::Type::Object)) {
                if (const auto v = page.asObject().field(key))
                    return *v;
            }
        }
        if (key == u"path")
            return Value::string(l.path);
        if (key == u"display")
            return l.display.isEmpty() ? Value() : Value::string(l.display);
        if (key == u"subpath")
            return l.subpath.isEmpty() ? Value() : Value::string(l.subpath);
        if (key == u"embed")
            return Value::boolean(l.embed);
        return Value();
    }
    case Value::Type::List: {
        ValueList out;
        for (const Value &item : value.asList()) {
            const Value v = fieldOf(item, key, ctx);
            if (v.is(Value::Type::List))
                out.append(v.asList());
            else
                out.append(v);
        }
        return Value::list(out);
    }
    case Value::Type::Date: return dateField(value.asDate(), key);
    case Value::Type::Duration: return durationField(value.asDuration(), key);
    default: return Value();
    }
}

Value callFunction(const Function &f, const ValueList &args, Context &ctx)
{
    Bound bound = f.bound;
    for (qsizetype i = 0; i < f.params.size(); ++i)
        bound.append({f.params.at(i), i < args.size() ? args.at(i) : Value()});
    return evaluate(*f.body, ctx, f.row, bound);
}

Value evaluate(const Expr &e, Context &ctx, const Value &row, const Bound &bound)
{
    ctx.step();
    switch (e.kind) {
    case Expr::Kind::Literal: return e.value;
    case Expr::Kind::DateWord: {
        const auto d = dateWord(e.name, ctx.options());
        return d ? Value::date(*d) : Value();
    }
    case Expr::Kind::Link: {
        const QString display = e.value.is(Value::Type::String) ? e.value.asString() : QString();
        return Value::link(ctx.resolveLink(e.name, display, e.raw, !e.params.isEmpty(), ctx.options().currentPath));
    }
    case Expr::Kind::Variable: {
        for (qsizetype i = bound.size() - 1; i >= 0; --i) {
            if (bound.at(i).first == e.name)
                return bound.at(i).second;
        }
        if (e.name == u"this")
            return ctx.thisPage();
        if (e.name == u"row")
            return row;
        return fieldOf(row, e.name, ctx);
    }
    case Expr::Kind::Field: {
        if (isNameChain(e) && row.is(Value::Type::Object)) {
            if (const auto *r = dynamic_cast<const RowObject *>(&row.asObject())) {
                if (const auto v = r->extra(chainName(e)))
                    return *v;
            }
        }
        return fieldOf(evaluate(*e.args.at(0), ctx, row, bound), e.name, ctx);
    }
    case Expr::Kind::Index: {
        const Value base = evaluate(*e.args.at(0), ctx, row, bound);
        const Value index = evaluate(*e.args.at(1), ctx, row, bound);
        if (index.is(Value::Type::String))
            return fieldOf(base, canonicalKey(index.asString()), ctx);
        if (index.is(Value::Type::Number) && base.is(Value::Type::List)) {
            const double n = index.asNumber();
            const ValueList &list = base.asList();
            if (n >= 0 && n < double(list.size()) && n == std::floor(n))
                return list.at(qsizetype(n));
        }
        return Value();
    }
    case Expr::Kind::Unary: {
        const Value v = evaluate(*e.args.at(0), ctx, row, bound);
        if (e.name == u"!")
            return Value::boolean(!v.truthy());
        if (v.is(Value::Type::Number))
            return Value::number(-v.asNumber());
        if (v.is(Value::Type::Duration))
            return Value::duration({-v.asDuration().months, -v.asDuration().ms});
        return Value();
    }
    case Expr::Kind::Binary: {
        if (e.name == u"and") {
            if (!evaluate(*e.args.at(0), ctx, row, bound).truthy())
                return Value::boolean(false);
            return Value::boolean(evaluate(*e.args.at(1), ctx, row, bound).truthy());
        }
        if (e.name == u"or") {
            if (evaluate(*e.args.at(0), ctx, row, bound).truthy())
                return Value::boolean(true);
            return Value::boolean(evaluate(*e.args.at(1), ctx, row, bound).truthy());
        }
        const Value a = evaluate(*e.args.at(0), ctx, row, bound);
        const Value b = evaluate(*e.args.at(1), ctx, row, bound);
        if (e.name == u"=" || e.name == u"!=" || e.name == u"<" || e.name == u"<=" || e.name == u">"
            || e.name == u">=")
            return comparison(e.name, a, b);
        return arithmetic(e.name, a, b, ctx);
    }
    case Expr::Kind::Call: {
        const Builtin *b = findBuiltin(e.name);
        ValueList args;
        args.reserve(e.args.size());
        for (const ExprPtr &arg : e.args)
            args.append(evaluate(*arg, ctx, row, bound));
        if (b)
            return b->call(args, ctx);
        // A name holding a lambda.
        const Value f = fieldOf(row, e.name, ctx);
        if (f.is(Value::Type::Function))
            return callFunction(f.asFunction(), args, ctx);
        throw EvalError{messages::notAFunction(e.name)};
    }
    case Expr::Kind::List: {
        ValueList items;
        for (const ExprPtr &arg : e.args)
            items.append(evaluate(*arg, ctx, row, bound));
        return Value::list(items);
    }
    case Expr::Kind::Lambda: {
        auto f = std::make_shared<Function>();
        f->params = e.params;
        f->body = e.args.at(0);
        f->row = row;
        f->bound = bound;
        return Value::function(f);
    }
    }
    return Value();
}

// ---- Text ------------------------------------------------------------------------------------------

QString formatNumber(double n)
{
    if (std::isnan(n))
        return u"NaN"_s;
    if (std::isinf(n))
        return n > 0 ? u"Infinity"_s : u"-Infinity"_s;
    if (n == std::floor(n) && std::fabs(n) < 1e15)
        return QString::number(qint64(n));
    return QString::number(n, 'g', 12);
}

QString formatDate(const DateValue &d)
{
    if (!d.hasTime)
        return d.at.date().toString(u"yyyy-MM-dd"_s);
    const QTime t = d.at.time();
    return d.at.toString(t.second() != 0 ? u"yyyy-MM-dd HH:mm:ss"_s : u"yyyy-MM-dd HH:mm"_s);
}

QString formatDuration(const Duration &d)
{
    if (d.months == 0 && d.ms == 0)
        return messages::seconds(0);
    const bool negative = d.months < 0 || (d.months == 0 && d.ms < 0);
    qint64 months = std::abs(d.months);
    qint64 ms = std::abs(d.ms);
    QStringList parts;
    if (const qint64 y = months / 12)
        parts.append(messages::years(int(y)));
    if (const qint64 m = months % 12)
        parts.append(messages::months(int(m)));
    const qint64 days = ms / 86400000;
    ms %= 86400000;
    if (days)
        parts.append(messages::days(int(days)));
    if (const qint64 h = ms / 3600000)
        parts.append(messages::hours(int(h)));
    ms %= 3600000;
    if (const qint64 m = ms / 60000)
        parts.append(messages::minutes(int(m)));
    ms %= 60000;
    if (const qint64 s = ms / 1000)
        parts.append(messages::seconds(int(s)));
    if (const qint64 rest = ms % 1000; rest && parts.isEmpty())
        parts.append(messages::milliseconds(int(rest)));
    return (negative ? u"-"_s : QString()) + parts.join(u", "_s);
}

QString toText(const Value &value, Context &ctx)
{
    switch (value.type()) {
    case Value::Type::Null: return u"null"_s;
    case Value::Type::Boolean: return value.asBool() ? u"true"_s : u"false"_s;
    case Value::Type::Number: return formatNumber(value.asNumber());
    case Value::Type::String: return value.asString();
    case Value::Type::Date: return formatDate(value.asDate());
    case Value::Type::Duration: return formatDuration(value.asDuration());
    case Value::Type::Link: {
        const LinkValue &l = value.asLink();
        QString target = l.resolved ? ctx.linkTarget(l.path) : l.path;
        if (!l.subpath.isEmpty())
            target += u'#' + l.subpath;
        return u"[["_s + target + (l.display.isEmpty() ? QString() : u'|' + l.display) + u"]]"_s;
    }
    case Value::Type::List: {
        QStringList parts;
        for (const Value &v : value.asList())
            parts.append(toText(v, ctx));
        return parts.join(u", "_s);
    }
    case Value::Type::Object: {
        QStringList parts;
        const Object &o = value.asObject();
        for (const QString &k : o.keys())
            parts.append(k + u": "_s + toText(o.field(k).value_or(Value()), ctx));
        return u"{ "_s + parts.join(u", "_s) + u" }"_s;
    }
    case Value::Type::Function: return u"<function>"_s;
    }
    return QString();
}

// ---- Date words ------------------------------------------------------------------------------------

bool isDateWord(const QString &word)
{
    static const QStringList words{u"today"_s, u"now"_s, u"tomorrow"_s, u"yesterday"_s, u"sow"_s,
                                   u"eow"_s,   u"som"_s, u"eom"_s,      u"soy"_s,       u"eoy"_s};
    return words.contains(word);
}

std::optional<DateValue> dateWord(const QString &word, const Options &options)
{
    const QDate today = options.now.date();
    const auto day = [](QDate d) { return DateValue{QDateTime(d, QTime(0, 0)), false}; };
    if (word == u"now")
        return DateValue{options.now, true};
    if (word == u"today")
        return day(today);
    if (word == u"tomorrow")
        return day(today.addDays(1));
    if (word == u"yesterday")
        return day(today.addDays(-1));
    if (word == u"sow")
        return day(dates::startOfWeek(today, options.weekStart));
    if (word == u"eow")
        return day(dates::endOfWeek(today, options.weekStart));
    if (word == u"som")
        return day(dates::startOfMonth(today));
    if (word == u"eom")
        return day(dates::endOfMonth(today));
    if (word == u"soy")
        return day(QDate(today.year(), 1, 1));
    if (word == u"eoy")
        return day(QDate(today.year(), 12, 31));
    return std::nullopt;
}

} // namespace brulion::dataview
