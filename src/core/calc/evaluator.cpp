#include "calc/evaluator_p.h"

#include "calc/numbers.h"

#include <QLocale>
#include <QVarLengthArray>

#include <algorithm>
#include <cmath>
#include <limits>
#include <numbers>

namespace brulion::calc {

namespace {

/// Deepest evaluation recursion (AST depth plus nested user-function calls).
constexpr int kMaxEvalDepth = 1000;
/// Deepest nesting of user-function calls.
constexpr int kMaxCallDepth = 100;
/// Largest factorial argument (171! overflows a double).
constexpr double kMaxFactorial = 170;
/// Largest |exponent| of a unit power and largest resulting power of a base unit (m^99).
constexpr double kMaxUnitPower = 99;

struct DepthGuard {
    explicit DepthGuard(int &depth)
        : m_depth(depth)
    {
        ++m_depth;
    }
    ~DepthGuard() { --m_depth; }
    DepthGuard(const DepthGuard &) = delete;
    DepthGuard &operator=(const DepthGuard &) = delete;

private:
    int &m_depth;
};

bool nearlyEqual(double a, double b)
{
    if (a == b)
        return true;
    if (!std::isfinite(a) || !std::isfinite(b))
        return false;
    const double diff = std::abs(a - b);
    if (diff < std::numeric_limits<double>::epsilon())
        return true;
    return diff <= std::max(std::abs(a), std::abs(b)) * 1e-12;
}

/// Finite operands that produced a non-finite result.
bool overflowed(double result, double a, double b)
{
    return !std::isfinite(result) && std::isfinite(a) && std::isfinite(b);
}

void combineCurrency(Value &result, const Value &a, const Value &b)
{
    if (a.isMixedCurrency() || b.isMixedCurrency()) {
        result.setMixedCurrency();
    } else if (a.hasCurrency() && b.hasCurrency()) {
        if (a.currency().code == b.currency().code)
            result.setCurrency(a.currency());
        else
            result.setMixedCurrency();
    } else if (a.hasCurrency()) {
        result.setCurrency(a.currency());
    } else if (b.hasCurrency()) {
        result.setCurrency(b.currency());
    }
}

Value addOrSubtract(const Value &a, const Value &b, CalcError &err, bool percentSugar, double sign)
{
    if (percentSugar && b.isPercent() && !a.isPercent() && !b.hasUnit()) {
        // x + 10% = x · 1.1 (keeps x's unit and currency)
        Value r = a;
        r.setSi(a.si() * (1 + sign * b.si()));
        if (overflowed(r.si(), a.si(), b.si()))
            err = {CalcErrorCode::TooBig, {}};
        return r;
    }
    if (a.unit().dimension() != b.unit().dimension()) {
        err = {CalcErrorCode::UnitsMismatch, {}};
        return {};
    }
    const double si = a.si() + sign * b.si();
    Value r = Value::quantity(si, a.hasUnit() ? a.unit() : b.unit());
    r.setPercent(a.isPercent() && b.isPercent());
    combineCurrency(r, a, b);
    if (overflowed(si, a.si(), b.si()))
        err = {CalcErrorCode::TooBig, {}};
    return r;
}

Value modulo(const Value &a, const Value &b, CalcError &err)
{
    if (b.si() == 0)
        return a;
    Value r = a;
    r.setPercent(false);
    if (b.hasUnit()) {
        if (a.unit().dimension() != b.unit().dimension()) {
            err = {CalcErrorCode::UnitsMismatch, {}};
            return {};
        }
        r.setSi(a.si() - b.si() * std::floor(a.si() / b.si()));
    } else {
        const double m = a.magnitude();
        const double rest = m - b.si() * std::floor(m / b.si());
        r.setSi(a.hasUnit() ? rest * a.unit().scale() : rest);
    }
    return r;
}

/// `unit` raised to the integer `n` keeps every base-unit power within ±kMaxUnitPower
/// (`(m^99)^99` would otherwise grow without bound and overflow an int).
bool unitPowerFits(const Unit &unit, int n)
{
    for (const UnitFactor &f : unit.factors()) {
        const qint64 p = qint64(f.power) * f.def->power * n; // km² is km^2
        if (std::abs(static_cast<double>(p)) > kMaxUnitPower)
            return false;
    }
    return true;
}

Value power(const Value &a, const Value &b, CalcError &err)
{
    if (b.hasUnit()) {
        err = {CalcErrorCode::UnitsMismatch, {}};
        return {};
    }
    const double e = b.si();
    Unit unit;
    if (a.hasUnit()) {
        if (e != std::trunc(e) || std::abs(e) > kMaxUnitPower || !unitPowerFits(a.unit(), static_cast<int>(e))) {
            err = {CalcErrorCode::UnitsMismatch, {}};
            return {};
        }
        unit = a.unit().power(static_cast<int>(e));
    }
    const double si = std::pow(a.si(), e);
    if (std::isnan(si) && !std::isnan(a.si()) && !std::isnan(e)) {
        err = {CalcErrorCode::Domain, QStringLiteral("^")};
        return {};
    }
    if (overflowed(si, a.si(), e) && a.si() != 0) {
        err = {CalcErrorCode::TooBig, {}};
        return {};
    }
    Value r = Value::quantity(si, unit);
    if (e == 1) {
        r.copyCurrency(a);
        r.setPercent(a.isPercent());
    }
    return r;
}

/// The value with `fn` applied to its displayed magnitude (percentages: to the percentage).
template<typename Fn>
Value mapMagnitude(const Value &v, Fn fn)
{
    double m = v.magnitude();
    if (v.isPercent())
        m *= 100;
    double r = fn(m);
    if (v.isPercent())
        r /= 100;
    Value out = v;
    out.setSi(v.hasUnit() ? r * v.unit().scale() : r);
    return out;
}

/// Decimal rounding, half away from zero, on the shortest decimal representation of `value`
/// (so round(2.345, 2) is 2.35 although 2.345 is stored as 2.34499…).
double roundDecimal(double value, int digits)
{
    if (!std::isfinite(value) || value == 0)
        return value;
    const QString s = QString::number(std::abs(value), 'e', QLocale::FloatingPointShortest); // "2.345e+00"
    const qsizetype e = s.indexOf(u'e');
    QString mantissa = s.left(e);
    mantissa.remove(u'.');
    const int exponent = QStringView(s).mid(e + 1).toInt();
    const qsizetype pointPos = exponent + 1; // digits before the decimal point
    const qsizetype keep = pointPos + digits;
    if (keep >= mantissa.size())
        return value;
    if (keep < 0)
        return 0;
    QString kept = mantissa.left(keep);
    if (mantissa.at(keep) >= u'5') {
        qsizetype i = kept.size() - 1;
        while (i >= 0 && kept.at(i) == u'9') {
            kept[i] = u'0';
            --i;
        }
        if (i < 0)
            kept.prepend(u'1');
        else
            kept[i] = QChar(kept.at(i).unicode() + 1);
    }
    if (kept.isEmpty())
        return 0;
    const qsizetype shift = pointPos - keep;
    const double rounded = QString(kept + u'e' + QString::number(shift)).toDouble();
    return value < 0 ? -rounded : rounded;
}

// --- builtin functions ----------------------------------------------------------------------

enum class Builtin {
    Sum, Avg, Min, Max, Count, Median,
    Round, Floor, Ceil, Trunc, Abs, Sign,
    Sqrt, Cbrt, Pow, Exp, Ln, Log, Log10, Log2,
    Sin, Cos, Tan, Asin, Acos, Atan, Atan2,
    Mod, If, Concat, Factorial,
};

struct BuiltinInfo {
    Builtin id = Builtin::Sum;
    int minArgs = 0;
    int maxArgs = -1; ///< -1: unlimited
};

const QHash<QString, BuiltinInfo> &builtinTable()
{
    static const QHash<QString, BuiltinInfo> table = [] {
        struct Entry {
            QStringView name;
            BuiltinInfo info;
        };
        static constexpr Entry entries[] = {
            {u"sum", {Builtin::Sum, 0, -1}},       {u"suma", {Builtin::Sum, 0, -1}},
            {u"avg", {Builtin::Avg, 0, -1}},       {u"average", {Builtin::Avg, 0, -1}},
            {u"mean", {Builtin::Avg, 0, -1}},      {u"średnia", {Builtin::Avg, 0, -1}},
            {u"srednia", {Builtin::Avg, 0, -1}},   {u"min", {Builtin::Min, 0, -1}},
            {u"max", {Builtin::Max, 0, -1}},       {u"count", {Builtin::Count, 0, -1}},
            {u"ile", {Builtin::Count, 0, -1}},     {u"median", {Builtin::Median, 0, -1}},
            {u"mediana", {Builtin::Median, 0, -1}},
            {u"round", {Builtin::Round, 1, 2}},    {u"zaokr", {Builtin::Round, 1, 2}},
            {u"zaokrąglij", {Builtin::Round, 1, 2}},
            {u"floor", {Builtin::Floor, 1, 1}},    {u"ceil", {Builtin::Ceil, 1, 1}},
            {u"trunc", {Builtin::Trunc, 1, 1}},    {u"abs", {Builtin::Abs, 1, 1}},
            {u"sign", {Builtin::Sign, 1, 1}},
            {u"sqrt", {Builtin::Sqrt, 1, 1}},      {u"pierwiastek", {Builtin::Sqrt, 1, 1}},
            {u"cbrt", {Builtin::Cbrt, 1, 1}},
            {u"pow", {Builtin::Pow, 2, 2}},        {u"potęga", {Builtin::Pow, 2, 2}},
            {u"exp", {Builtin::Exp, 1, 1}},        {u"ln", {Builtin::Ln, 1, 1}},
            {u"log", {Builtin::Log, 1, 2}},        {u"log10", {Builtin::Log10, 1, 1}},
            {u"log2", {Builtin::Log2, 1, 1}},
            {u"sin", {Builtin::Sin, 1, 1}},        {u"cos", {Builtin::Cos, 1, 1}},
            {u"tan", {Builtin::Tan, 1, 1}},        {u"asin", {Builtin::Asin, 1, 1}},
            {u"acos", {Builtin::Acos, 1, 1}},      {u"atan", {Builtin::Atan, 1, 1}},
            {u"atan2", {Builtin::Atan2, 2, 2}},
            {u"mod", {Builtin::Mod, 2, 2}},
            {u"if", {Builtin::If, 2, 3}},          {u"jeżeli", {Builtin::If, 2, 3}},
            {u"jezeli", {Builtin::If, 2, 3}},      {u"jeśli", {Builtin::If, 2, 3}},
            {u"jesli", {Builtin::If, 2, 3}},
            {u"concat", {Builtin::Concat, 1, -1}},
            {u"factorial", {Builtin::Factorial, 1, 1}}, {u"silnia", {Builtin::Factorial, 1, 1}},
        };
        QHash<QString, BuiltinInfo> h;
        for (const Entry &e : entries)
            h.insert(e.name.toString(), e.info);
        return h;
    }();
    return table;
}

/// The builtin called `name` (case-insensitive). Not inlined: keeps the table's initialisation
/// out of the recursive evalCall() frame.
Q_NEVER_INLINE std::optional<BuiltinInfo> findBuiltin(const QString &name)
{
    const QHash<QString, BuiltinInfo> &table = builtinTable();
    const auto it = table.constFind(name.toLower());
    if (it == table.constEnd())
        return std::nullopt;
    return *it;
}

/// Number of values in the arguments, lists flattened.
qint64 flattenedCount(const std::vector<Value> &args)
{
    qint64 count = 0;
    for (const Value &a : args)
        count += a.isList() ? static_cast<qint64>(a.items().size()) : 1;
    return count;
}

/// Numbers among the arguments, lists flattened; text, booleans and empty values are skipped
/// (like spreadsheet functions over ranges). Points into `args`: nothing is copied.
std::vector<const Value *> numericArguments(const std::vector<Value> &args)
{
    std::vector<const Value *> out;
    for (const Value &a : args) {
        if (a.isList()) {
            for (const Value &item : a.items()) {
                if (item.isNumber())
                    out.push_back(&item);
            }
        } else if (a.isNumber()) {
            out.push_back(&a);
        }
    }
    return out;
}

/// Sum of numeric values (no percent sugar); 0 for none.
Value sumOf(const std::vector<const Value *> &values, CalcError &err)
{
    if (values.empty())
        return Value::number(0);
    Value acc = *values.front();
    for (std::size_t i = 1; i < values.size() && !err; ++i)
        acc = ops::add(acc, *values[i], err, false);
    return acc;
}

double factorial(double n)
{
    double r = 1;
    for (int i = 2; i <= static_cast<int>(n); ++i)
        r *= i;
    return r;
}

std::optional<double> constantValue(const QString &name)
{
    if (name == u"pi" || name == u"π")
        return std::numbers::pi;
    if (name == u"e")
        return std::numbers::e;
    if (name == u"∞")
        return std::numeric_limits<double>::infinity();
    return std::nullopt;
}

std::optional<bool> booleanConstant(const QString &name)
{
    if (name == u"true" || name == u"prawda")
        return true;
    if (name == u"false" || name == u"fałsz")
        return false;
    return std::nullopt;
}

Currency currencyFor(const QString &symbol, bool prefix)
{
    return Currency{currencyCode(symbol), symbol, prefix};
}

/// Report `code` through `err` and return no value.
Value failWith(CalcError &err, CalcErrorCode code, const QString &arg = {})
{
    err = {code, arg};
    return {};
}

// The helpers applying operators and builtins are never inlined into the recursive evaluator
// functions: their locals would otherwise sit on the stack of every level of a deep evaluation.

/// A unary operator (`-`, `+`, `not`, `!`, `%`, `√`) applied to its evaluated operand.
Q_NEVER_INLINE Value applyUnary(NodeKind kind, const Value &operand, CalcLocale locale, CalcError &err)
{
    if (kind == NodeKind::Not)
        return Value::boolean(!ops::truthy(operand));
    std::optional<Value> n = ops::toNumber(operand, locale, err);
    if (!n)
        return {};
    switch (kind) {
    case NodeKind::Negate:
        n->setSi(-n->si());
        return *n;
    case NodeKind::Plus:
        return *n;
    case NodeKind::Percent:
        if (n->hasUnit())
            return failWith(err, CalcErrorCode::UnitsMismatch);
        return Value::percent(n->magnitude() / 100);
    case NodeKind::Factorial: {
        const double x = n->si();
        if (n->hasUnit() || x < 0 || x != std::trunc(x))
            return failWith(err, CalcErrorCode::Domain, QStringLiteral("!"));
        if (x > kMaxFactorial)
            return failWith(err, CalcErrorCode::TooBig);
        return Value::number(factorial(x));
    }
    case NodeKind::Sqrt: {
        if (n->si() < 0)
            return failWith(err, CalcErrorCode::Domain, QStringLiteral("√"));
        const std::optional<Unit> root = n->unit().root(2);
        if (!root)
            return failWith(err, CalcErrorCode::UnitsMismatch);
        return Value::quantity(std::sqrt(n->si()), *root);
    }
    default:
        return failWith(err, CalcErrorCode::Generic);
    }
}

/// A binary operator applied to evaluated operands.
Q_NEVER_INLINE Value combineBinary(NodeKind kind, const Value &left, const Value &right, CalcLocale locale, CalcError &err)
{
    const std::optional<Value> a = ops::toNumber(left, locale, err);
    if (!a)
        return {};
    const std::optional<Value> b = ops::toNumber(right, locale, err);
    if (!b)
        return {};
    switch (kind) {
    case NodeKind::Add:
        return ops::add(*a, *b, err);
    case NodeKind::Sub:
        return ops::subtract(*a, *b, err);
    case NodeKind::Mul:
    case NodeKind::ImplicitMul:
        return ops::multiply(*a, *b, err);
    case NodeKind::Div:
        return ops::divide(*a, *b, err);
    case NodeKind::Mod:
        return modulo(*a, *b, err);
    case NodeKind::Pow:
        return power(*a, *b, err);
    case NodeKind::AsPercentOf: {
        const Value r = ops::divide(*a, *b, err);
        if (!err && r.hasUnit())
            return failWith(err, CalcErrorCode::UnitsMismatch);
        return Value::percent(r.si());
    }
    default:
        return failWith(err, CalcErrorCode::Generic);
    }
}

/// One comparison of a (chained) comparison: strings compare as text, numbers with a relative
/// tolerance, two temperatures as absolute temperatures. nullopt + `err` on error.
Q_NEVER_INLINE std::optional<bool> compareValues(CompareOp op, const Value &left, const Value &right, CalcLocale locale,
                                                 CalcError &err)
{
    if (left.isString() && right.isString()) {
        const int c = QString::compare(left.text(), right.text());
        switch (op) {
        case CompareOp::Equal: return c == 0;
        case CompareOp::NotEqual: return c != 0;
        case CompareOp::Less: return c < 0;
        case CompareOp::LessEqual: return c <= 0;
        case CompareOp::Greater: return c > 0;
        case CompareOp::GreaterEqual: return c >= 0;
        }
        return false;
    }
    const std::optional<Value> a = ops::toNumber(left, locale, err);
    const std::optional<Value> b = a ? ops::toNumber(right, locale, err) : std::nullopt;
    if (!a || !b)
        return std::nullopt;
    if (a->unit().dimension() != b->unit().dimension()) {
        err = {CalcErrorCode::UnitsMismatch, {}};
        return std::nullopt;
    }
    double x = a->si();
    double y = b->si();
    const UnitDef *ua = a->unit().single();
    const UnitDef *ub = b->unit().single();
    if (ua && ub && ua->isTemperature() && ub->isTemperature()) { // absolute temperatures
        x += ua->offset;
        y += ub->offset;
    }
    const bool equal = nearlyEqual(x, y);
    switch (op) {
    case CompareOp::Equal: return equal;
    case CompareOp::NotEqual: return !equal;
    case CompareOp::Less: return x < y && !equal;
    case CompareOp::LessEqual: return x < y || equal;
    case CompareOp::Greater: return x > y && !equal;
    case CompareOp::GreaterEqual: return x > y || equal;
    }
    return false;
}

/// `value to <unit>` / `value to <currency>`; affine only between two temperatures.
Q_NEVER_INLINE Value convertValue(const Value &value, const Unit &unit, const Currency &currency, CalcLocale locale,
                                  CalcError &err)
{
    std::optional<Value> n = ops::toNumber(value, locale, err);
    if (!n)
        return {};
    Value r = *n;
    r.setPercent(false);
    if (currency.isValid()) {
        if (n->isMixedCurrency() || (n->hasCurrency() && n->currency().code != currency.code))
            return failWith(err, CalcErrorCode::CurrencyConversion);
        r.setCurrency(currency);
    } else if (n->hasCurrency() && unit.isEmpty()) {
        return failWith(err, CalcErrorCode::CurrencyConversion);
    }
    if (!unit.isEmpty() || n->hasUnit()) {
        if (n->unit().dimension() != unit.dimension())
            return failWith(err, CalcErrorCode::UnitsMismatch);
        double si = n->si();
        const UnitDef *from = n->unit().single();
        const UnitDef *to = unit.single();
        if (from && to && from->isTemperature() && to->isTemperature())
            si += from->offset - to->offset; // affine: 20°C -> 68°F
        r.setSi(si);
        r.setUnit(unit);
    }
    return r;
}

/// `20% of x`: x scaled by the percentage, keeping x's unit and currency. Only a percentage may
/// precede the word (`word` as written): "5 z 10" is not 50.
Q_NEVER_INLINE Value percentOf(const Value &percentage, const Value &baseValue, const QString &word, CalcLocale locale,
                               CalcError &err)
{
    if (!percentage.isPercent())
        return failWith(err, CalcErrorCode::UnexpectedPart, word);
    const std::optional<Value> p = ops::toNumber(percentage, locale, err);
    const std::optional<Value> base = p ? ops::toNumber(baseValue, locale, err) : std::nullopt;
    if (!p || !base)
        return {};
    Value r = *base;
    r.setPercent(false);
    r.setSi(base->si() * p->si());
    if (overflowed(r.si(), base->si(), p->si()))
        err = {CalcErrorCode::TooBig, {}};
    return r;
}

/// `$12` / `PLN 12`; `rate` is the variable named like a written currency code (`EUR = 4,30`
/// makes `EUR 100` an exchange), nullptr when there is none.
Q_NEVER_INLINE Value currencyPrefixed(const Value &operand, const QString &symbol, bool codeForm, const Value *rate,
                                      CalcLocale locale, CalcError &err)
{
    const std::optional<Value> n = ops::toNumber(operand, locale, err);
    if (!n)
        return {};
    Value tag = Value::money(1, currencyFor(symbol, !codeForm));
    if (rate) {
        const std::optional<Value> number = ops::toNumber(*rate, locale, err);
        if (!number)
            return {};
        tag = *number;
    }
    return ops::multiply(tag, *n, err);
}

} // namespace

// --- ops -------------------------------------------------------------------------------------

namespace ops {

std::optional<Value> toNumber(const Value &v, CalcLocale locale, CalcError &err)
{
    switch (v.type()) {
    case Value::Type::Number:
        return v;
    case Value::Type::Bool:
        return Value::number(v.boolValue() ? 1 : 0);
    case Value::Type::String: {
        const std::optional<ParsedNumber> parsed = parseLocaleNumber(v.text(), locale);
        if (!parsed) {
            err = {CalcErrorCode::NotANumber, v.text()};
            return std::nullopt;
        }
        Value r = parsed->percent ? Value::percent(parsed->value) : Value::number(parsed->value);
        if (parsed->currency.isValid())
            r.setCurrency(parsed->currency);
        return r;
    }
    case Value::Type::List:
        err = {CalcErrorCode::ListNotAllowed, {}};
        return std::nullopt;
    case Value::Type::Function:
        err = {CalcErrorCode::WrongType, v.functionDef() ? v.functionDef()->name : QString()};
        return std::nullopt;
    case Value::Type::None:
        break;
    }
    err = {CalcErrorCode::ValueExpected, {}};
    return std::nullopt;
}

Value add(const Value &a, const Value &b, CalcError &err, bool percentSugar)
{
    return addOrSubtract(a, b, err, percentSugar, 1);
}

Value subtract(const Value &a, const Value &b, CalcError &err, bool percentSugar)
{
    return addOrSubtract(a, b, err, percentSugar, -1);
}

Value multiply(const Value &a, const Value &b, CalcError &err)
{
    const double si = a.si() * b.si();
    Value r = Value::quantity(si, Unit::product(a.unit(), b.unit()));
    combineCurrency(r, a, b);
    if (overflowed(si, a.si(), b.si()))
        err = {CalcErrorCode::TooBig, {}};
    return r;
}

Value divide(const Value &a, const Value &b, CalcError &err)
{
    const double si = a.si() / b.si();
    Value r = Value::quantity(si, Unit::quotient(a.unit(), b.unit()));
    // zł / zł is a ratio; a number per currency has no representation: no tag either way.
    if (!b.hasCurrency() && !b.isMixedCurrency())
        r.copyCurrency(a);
    if (b.si() != 0 && overflowed(si, a.si(), b.si()))
        err = {CalcErrorCode::TooBig, {}};
    return r;
}

Value sum(const std::vector<Value> &values, CalcError &err)
{
    if (values.empty())
        return Value::number(0);
    Value acc = values.front();
    for (std::size_t i = 1; i < values.size() && !err; ++i)
        acc = add(acc, values[i], err, false);
    return acc;
}

bool truthy(const Value &v)
{
    switch (v.type()) {
    case Value::Type::Bool:
        return v.boolValue();
    case Value::Type::Number:
        return v.si() != 0 && !std::isnan(v.si());
    case Value::Type::String:
        return !v.text().trimmed().isEmpty();
    case Value::Type::List:
        return !v.items().empty();
    case Value::Type::Function:
        return true;
    case Value::Type::None:
        break;
    }
    return false;
}

} // namespace ops

std::optional<AggregateKind> aggregateWord(QStringView word)
{
    const QString lower = word.toString().toLower();
    if (lower == u"sum" || lower == u"total" || lower == u"suma" || lower == u"razem")
        return AggregateKind::Sum;
    if (lower == u"avg" || lower == u"average" || lower == u"mean" || lower == u"średnia" || lower == u"srednia")
        return AggregateKind::Average;
    return std::nullopt;
}

// --- Evaluator -------------------------------------------------------------------------------

Evaluator::Evaluator(EvalEnv env)
    : m_env(std::move(env))
{
}

Value Evaluator::fail(CalcErrorCode code, const QString &arg)
{
    if (!m_error)
        m_error = CalcError{code, arg};
    return {};
}

Value Evaluator::check(Value v, CalcError &err)
{
    if (err)
        return fail(err.code, err.arg);
    return v;
}

Value Evaluator::evaluate(const Node &node)
{
    if (failed())
        return {};
    return eval(node);
}

const Value *Evaluator::lookupVariable(const QString &name) const
{
    if (!m_frames.isEmpty()) {
        const QHash<QString, Value> &frame = m_frames.last();
        const auto it = frame.constFind(name);
        if (it != frame.constEnd())
            return &it.value();
    }
    return m_env.scope ? m_env.scope->find(name) : nullptr;
}

IdentKind Evaluator::classify(const QString &name) const
{
    if (lookupVariable(name))
        return IdentKind::Variable;
    if (m_env.aggregate && aggregateWord(name))
        return IdentKind::Aggregate;
    if (constantValue(name) || booleanConstant(name))
        return IdentKind::Constant;
    if (findUnit(name))
        return IdentKind::Unit;
    if (!currencyCode(name).isEmpty())
        return IdentKind::Currency;
    return IdentKind::Unknown;
}

Value Evaluator::eval(const Node &node)
{
    if (failed())
        return {};
    if (m_depth >= kMaxEvalDepth)
        return fail(CalcErrorCode::Recursion);
    const DepthGuard guard(m_depth);
    if (m_env.budget && !m_env.budget->charge(1))
        return fail(CalcErrorCode::TooComplex);
    switch (node.kind) {
    case NodeKind::Number:
        return Value::number(node.number);
    case NodeKind::String:
        return Value::string(node.text);
    case NodeKind::Ident:
        return evalIdent(node.text);
    case NodeKind::Negate:
    case NodeKind::Plus:
    case NodeKind::Not:
    case NodeKind::Sqrt:
    case NodeKind::Factorial:
    case NodeKind::Percent:
        return evalUnary(node);
    case NodeKind::Add:
    case NodeKind::Sub:
    case NodeKind::Mul:
    case NodeKind::ImplicitMul:
    case NodeKind::Div:
    case NodeKind::Mod:
    case NodeKind::Pow:
    case NodeKind::AsPercentOf:
        return evalBinary(node);
    case NodeKind::And:
    case NodeKind::Or:
    case NodeKind::Xor:
        return evalLogical(node);
    case NodeKind::Compare:
        return evalCompare(node);
    case NodeKind::Ternary:
        return evalTernary(node);
    case NodeKind::Call:
        return evalCall(node);
    case NodeKind::List:
        return evalList(node);
    case NodeKind::Convert:
        return evalConvert(node);
    case NodeKind::PercentOf:
        return evalPercentOf(node);
    case NodeKind::CurrencyPrefix:
        return evalCurrencyPrefix(node);
    case NodeKind::CurrencyUnit:
        return Value::money(1, currencyFor(node.text, false));
    case NodeKind::Ref:
        return evalRef(node);
    }
    return fail(CalcErrorCode::Generic);
}

Value Evaluator::evalIdent(const QString &name)
{
    if (const Value *v = lookupVariable(name))
        return *v;
    if (m_env.aggregate) {
        if (const std::optional<AggregateKind> kind = aggregateWord(name)) {
            CalcError err;
            Value v = m_env.aggregate(*kind, err);
            return check(std::move(v), err);
        }
    }
    if (const std::optional<double> c = constantValue(name))
        return Value::number(*c);
    if (const std::optional<bool> b = booleanConstant(name))
        return Value::boolean(*b);
    if (const UnitDef *def = findUnit(name))
        return Value::quantity(def->factor, Unit(def));
    const QString code = currencyCode(name);
    if (!code.isEmpty())
        return Value::money(1, Currency{code, name, false});
    return fail(CalcErrorCode::UndefinedSymbol, name);
}

Value Evaluator::evalTernary(const Node &node)
{
    const bool condition = ops::truthy(eval(*node.children[0]));
    if (failed())
        return {};
    return eval(*node.children[condition ? 1 : 2]);
}

Value Evaluator::evalRef(const Node &node)
{
    if (!m_env.reference)
        return fail(CalcErrorCode::UndefinedSymbol, node.text);
    CalcError err;
    Value v = m_env.reference(node.ref, err);
    return check(std::move(v), err);
}

Value Evaluator::evalUnary(const Node &node)
{
    // The operand is evaluated in this small frame; the operator is applied in a non-recursive
    // helper, so deep nesting (`-(-(-…))`) needs little stack.
    const Value operand = eval(*node.children[0]);
    if (failed())
        return {};
    CalcError err;
    Value result = applyUnary(node.kind, operand, m_env.locale, err);
    return check(std::move(result), err);
}

Value Evaluator::evalBinary(const Node &node)
{
    // A left-associative chain (`1 + 2 - 3 + …`) is folded in a loop along its left spine, so a
    // long line of terms costs no recursion (Node::depth does not count it either).
    QVarLengthArray<const Node *, 8> spine{&node};
    while (isLeftChain(spine.back()->kind) && isLeftChain(spine.back()->children[0]->kind))
        spine.append(spine.back()->children[0].get());
    if (m_env.budget && !m_env.budget->charge(static_cast<qint64>(spine.size()) - 1))
        return fail(CalcErrorCode::TooComplex);
    Value acc = eval(*spine.back()->children[0]);
    for (auto it = spine.rbegin(); it != spine.rend() && !failed(); ++it)
        acc = applyBinary(**it, acc);
    return failed() ? Value() : acc;
}

Value Evaluator::applyBinary(const Node &node, const Value &left)
{
    const Node &rightNode = *node.children[1];
    Value right;
    if (node.kind == NodeKind::ImplicitMul && isNumberLiteral(*node.children[0]) && rightNode.kind == NodeKind::Ident
        && classify(rightNode.text) == IdentKind::Unknown) {
        // "5 a": units whose names are common variable names count only after a number.
        if (const UnitDef *def = findQuantityUnit(rightNode.text))
            right = Value::quantity(def->factor, Unit(def));
    }
    if (right.isNone())
        right = eval(rightNode);
    if (failed())
        return {};
    CalcError err;
    Value result = combineBinary(node.kind, left, right, m_env.locale, err);
    return check(std::move(result), err);
}

Value Evaluator::evalLogical(const Node &node)
{
    const Value a = eval(*node.children[0]);
    if (failed())
        return {};
    const bool left = ops::truthy(a);
    if (node.kind == NodeKind::And && !left)
        return Value::boolean(false);
    if (node.kind == NodeKind::Or && left)
        return Value::boolean(true);
    const Value b = eval(*node.children[1]);
    if (failed())
        return {};
    const bool right = ops::truthy(b);
    if (node.kind == NodeKind::Xor)
        return Value::boolean(left != right);
    return Value::boolean(right);
}

Value Evaluator::evalCompare(const Node &node)
{
    Value left = eval(*node.children[0]);
    if (failed())
        return {};
    for (qsizetype i = 0; i < node.ops.size(); ++i) {
        Value right = eval(*node.children[static_cast<std::size_t>(i) + 1]);
        if (failed())
            return {};
        if (left.isString() && right.isString() && m_env.budget
            && !m_env.budget->charge(std::min(left.text().size(), right.text().size()) / WorkBudget::kStringChunk))
            return fail(CalcErrorCode::TooComplex);
        CalcError err;
        const std::optional<bool> result = compareValues(node.ops.at(i), left, right, m_env.locale, err);
        if (!result)
            return fail(err.code, err.arg);
        if (!*result)
            return Value::boolean(false);
        left = std::move(right);
    }
    return Value::boolean(true);
}

Value Evaluator::evalList(const Node &node)
{
    // Nested lists are flattened. Every element is checked and charged before it is copied, so
    // `[y, y, y, …]` with a big `y` fails before allocating anything large.
    std::vector<Value> items;
    items.reserve(node.children.size());
    for (const NodePtr &child : node.children) {
        Value v = eval(*child);
        if (failed())
            return {};
        const auto count = static_cast<qsizetype>(v.isList() ? v.items().size() : 1);
        if (static_cast<qsizetype>(items.size()) + count > kMaxListLength)
            return fail(CalcErrorCode::TooLarge, QString::number(kMaxListLength));
        if (m_env.budget && !m_env.budget->charge(count * WorkBudget::kListElement))
            return fail(CalcErrorCode::TooComplex);
        if (v.isList())
            items.insert(items.end(), v.items().begin(), v.items().end());
        else
            items.push_back(std::move(v));
    }
    return Value::list(std::move(items));
}

std::optional<Evaluator::UnitTarget> Evaluator::unitTarget(const Node &node)
{
    if (m_depth >= kMaxEvalDepth) { // `to m*m*m*…`: a long chain is a deep recursion here
        fail(CalcErrorCode::Recursion);
        return std::nullopt;
    }
    const DepthGuard guard(m_depth);
    switch (node.kind) {
    case NodeKind::Ident: {
        if (const UnitDef *def = findQuantityUnit(node.text))
            return UnitTarget{Unit(def), {}};
        if (!currencyCode(node.text).isEmpty())
            return UnitTarget{{}, currencyFor(node.text, false)};
        fail(CalcErrorCode::NotAUnit, node.text);
        return std::nullopt;
    }
    case NodeKind::CurrencyUnit:
        return UnitTarget{{}, currencyFor(node.text, false)};
    case NodeKind::Mul:
    case NodeKind::ImplicitMul:
    case NodeKind::Div: {
        const std::optional<UnitTarget> a = unitTarget(*node.children[0]);
        const std::optional<UnitTarget> b = a ? unitTarget(*node.children[1]) : std::nullopt;
        if (!a || !b)
            return std::nullopt;
        if (node.kind == NodeKind::Div)
            return UnitTarget{Unit::quotient(a->unit, b->unit), a->currency};
        return UnitTarget{Unit::product(a->unit, b->unit), a->currency.isValid() ? a->currency : b->currency};
    }
    case NodeKind::Pow: {
        const std::optional<UnitTarget> base = unitTarget(*node.children[0]);
        if (!base)
            return std::nullopt;
        const Node &exponent = *node.children[1];
        if (exponent.kind != NodeKind::Number || exponent.number != std::trunc(exponent.number)
            || std::abs(exponent.number) > kMaxUnitPower
            || !unitPowerFits(base->unit, static_cast<int>(exponent.number))) {
            fail(CalcErrorCode::UnitsMismatch);
            return std::nullopt;
        }
        return UnitTarget{base->unit.power(static_cast<int>(exponent.number)), base->currency};
    }
    case NodeKind::Number:
        if (node.number == 1)
            return UnitTarget{};
        fail(CalcErrorCode::NotAUnit, node.text);
        return std::nullopt;
    default:
        fail(CalcErrorCode::UnitsMismatch);
        return std::nullopt;
    }
}

Value Evaluator::evalConvert(const Node &node)
{
    const Value value = eval(*node.children[0]);
    if (failed())
        return {};
    const std::optional<UnitTarget> target = unitTarget(*node.children[1]);
    if (!target)
        return {};
    CalcError err;
    Value result = convertValue(value, target->unit, target->currency, m_env.locale, err);
    return check(std::move(result), err);
}

Value Evaluator::evalPercentOf(const Node &node)
{
    const Value percentage = eval(*node.children[0]);
    if (failed())
        return {};
    const Value base = eval(*node.children[1]);
    if (failed())
        return {};
    CalcError err;
    Value result = percentOf(percentage, base, node.text, m_env.locale, err);
    return check(std::move(result), err);
}

Value Evaluator::evalCurrencyPrefix(const Node &node)
{
    const bool codeForm = node.number == 1; // "PLN 12" (a code), not "$12" (a symbol)
    const Value operand = eval(*node.children[0]);
    if (failed())
        return {};
    const Value *rate = codeForm ? lookupVariable(node.text) : nullptr; // EUR = 4,30
    CalcError err;
    Value result = currencyPrefixed(operand, node.text, codeForm, rate, m_env.locale, err);
    return check(std::move(result), err);
}

namespace {

// --- builtin implementations (pure functions of evaluated arguments, never recursive) --------

/// sum avg min max count median over the numeric arguments (lists flattened, text skipped).
Value aggregateBuiltin(Builtin id, const std::vector<Value> &args, WorkBudget *budget, CalcError &err)
{
    if (budget && !budget->charge(flattenedCount(args))) // before collecting anything
        return failWith(err, CalcErrorCode::TooComplex);
    std::vector<const Value *> values = numericArguments(args);
    if (id == Builtin::Count)
        return Value::number(static_cast<double>(values.size()));
    if (id == Builtin::Sum)
        return sumOf(values, err);
    if (values.empty())
        return failWith(err, CalcErrorCode::NoValues);
    const Dimension dim = values.front()->unit().dimension();
    for (const Value *v : values) {
        if (v->unit().dimension() != dim)
            return failWith(err, CalcErrorCode::UnitsMismatch);
    }
    if (id == Builtin::Avg) {
        const Value total = sumOf(values, err);
        Value r = ops::divide(total, Value::number(static_cast<double>(values.size())), err);
        r.setPercent(total.isPercent());
        return r;
    }
    if (id == Builtin::Median) {
        std::sort(values.begin(), values.end(), [](const Value *a, const Value *b) { return a->si() < b->si(); });
        const std::size_t mid = values.size() / 2;
        if (values.size() % 2 == 1)
            return *values[mid];
        const Value total = ops::add(*values[mid - 1], *values[mid], err, false);
        Value r = ops::divide(total, Value::number(2), err);
        r.setPercent(total.isPercent());
        return r;
    }
    const bool wantMax = id == Builtin::Max;
    return **std::max_element(values.begin(), values.end(), [&](const Value *a, const Value *b) {
        return wantMax ? a->si() < b->si() : a->si() > b->si();
    });
}

Value concatBuiltin(const std::vector<Value> &args, CalcLocale locale, WorkBudget *budget, CalcError &err)
{
    QString text;
    for (const Value &a : args) {
        text += a.isString() ? a.text() : a.format(locale);
        if (text.size() > kMaxStringLength)
            return failWith(err, CalcErrorCode::TooLarge, QString::number(kMaxStringLength));
    }
    if (budget && !budget->charge(text.size() / WorkBudget::kStringChunk))
        return failWith(err, CalcErrorCode::TooComplex);
    return Value::string(text);
}

/// A plain number from a math function: NaN from a finite argument is a domain error, an
/// infinity from a finite argument an overflow.
Value mathResult(double v, double x, const QString &name, CalcError &err)
{
    if (std::isnan(v) && !std::isnan(x))
        return failWith(err, CalcErrorCode::Domain, name);
    if (!std::isfinite(v) && std::isfinite(x))
        return failWith(err, CalcErrorCode::TooBig);
    return Value::number(v);
}

Value roundBuiltin(const std::vector<Value> &nums, const QString &name, CalcError &err)
{
    int digits = 0;
    if (nums.size() > 1) {
        if (nums[1].hasUnit())
            return failWith(err, CalcErrorCode::UnitsMismatch);
        if (std::isnan(nums[1].si())) // casting NaN to int is undefined behaviour
            return failWith(err, CalcErrorCode::Domain, name);
        digits = static_cast<int>(std::clamp(std::trunc(nums[1].si()), -15.0, 15.0));
    }
    return mapMagnitude(nums.front(), [digits](double m) { return roundDecimal(m, digits); });
}

Value rootBuiltin(const Value &x, bool square, const QString &name, CalcError &err)
{
    if (square && x.si() < 0)
        return failWith(err, CalcErrorCode::Domain, name);
    const std::optional<Unit> root = x.unit().root(square ? 2 : 3);
    if (!root)
        return failWith(err, CalcErrorCode::UnitsMismatch);
    return Value::quantity(square ? std::sqrt(x.si()) : std::cbrt(x.si()), *root);
}

Value logarithmBuiltin(Builtin id, const std::vector<Value> &nums, const QString &name, CalcError &err)
{
    const double v = nums.front().si();
    if (v <= 0)
        return failWith(err, CalcErrorCode::Domain, name);
    if (id == Builtin::Ln)
        return mathResult(std::log(v), v, name, err);
    if (id == Builtin::Log2)
        return mathResult(std::log2(v), v, name, err);
    if (id == Builtin::Log && nums.size() > 1) {
        const double base = nums[1].si();
        if (nums[1].hasUnit() || base <= 0 || base == 1)
            return failWith(err, CalcErrorCode::Domain, name);
        return mathResult(std::log(v) / std::log(base), v, name, err);
    }
    return mathResult(std::log10(v), v, name, err);
}

Value trigonometryBuiltin(Builtin id, const Value &x, const QString &name, CalcError &err)
{
    const double v = x.si(); // radians for sin/cos/tan
    switch (id) {
    case Builtin::Sin:
        return mathResult(std::sin(v), v, name, err);
    case Builtin::Cos:
        return mathResult(std::cos(v), v, name, err);
    case Builtin::Tan:
        return mathResult(std::tan(v), v, name, err);
    case Builtin::Asin:
    case Builtin::Acos:
        if (std::abs(v) > 1)
            return failWith(err, CalcErrorCode::Domain, name);
        return mathResult(id == Builtin::Asin ? std::asin(v) : std::acos(v), v, name, err);
    default:
        return mathResult(std::atan(v), v, name, err);
    }
}

/// Functions of numbers (arguments already converted with ops::toNumber()).
Value numericBuiltin(Builtin id, const std::vector<Value> &nums, const QString &name, CalcError &err)
{
    const Value &x = nums.front();
    const Dimension angle = {0, 0, 0, 0, 0, 1};
    switch (id) {
    case Builtin::Round:
        return roundBuiltin(nums, name, err);
    case Builtin::Floor:
        return mapMagnitude(x, [](double m) { return std::floor(m); });
    case Builtin::Ceil:
        return mapMagnitude(x, [](double m) { return std::ceil(m); });
    case Builtin::Trunc:
        return mapMagnitude(x, [](double m) { return std::trunc(m); });
    case Builtin::Abs:
        return mapMagnitude(x, [](double m) { return std::abs(m); });
    case Builtin::Sign:
        return Value::number(x.si() > 0 ? 1 : x.si() < 0 ? -1 : 0);
    case Builtin::Sqrt:
    case Builtin::Cbrt:
        return rootBuiltin(x, id == Builtin::Sqrt, name, err);
    case Builtin::Pow:
        return power(x, nums[1], err);
    case Builtin::Mod:
        if (nums[1].hasUnit() && x.unit().dimension() != nums[1].unit().dimension())
            return failWith(err, CalcErrorCode::UnitsMismatch);
        return modulo(x, nums[1], err);
    case Builtin::Factorial:
        if (x.hasUnit() || x.si() < 0 || x.si() != std::trunc(x.si()))
            return failWith(err, CalcErrorCode::Domain, name);
        if (x.si() > kMaxFactorial)
            return failWith(err, CalcErrorCode::TooBig);
        return Value::number(factorial(x.si()));
    case Builtin::Atan2:
        if (x.unit().dimension() != nums[1].unit().dimension())
            return failWith(err, CalcErrorCode::UnitsMismatch);
        return Value::number(std::atan2(x.si(), nums[1].si()));
    case Builtin::Sin:
    case Builtin::Cos:
    case Builtin::Tan:
        if (x.hasUnit() && x.unit().dimension() != angle)
            return failWith(err, CalcErrorCode::UnitsMismatch);
        return trigonometryBuiltin(id, x, name, err);
    default:
        break;
    }
    // The rest take a plain number.
    if (x.hasUnit())
        return failWith(err, CalcErrorCode::UnitsMismatch);
    switch (id) {
    case Builtin::Exp:
        return mathResult(std::exp(x.si()), x.si(), name, err);
    case Builtin::Ln:
    case Builtin::Log10:
    case Builtin::Log2:
    case Builtin::Log:
        return logarithmBuiltin(id, nums, name, err);
    case Builtin::Asin:
    case Builtin::Acos:
    case Builtin::Atan:
        return trigonometryBuiltin(id, x, name, err);
    default:
        return failWith(err, CalcErrorCode::Generic);
    }
}

/// The parameters of a user-function call.
Q_NEVER_INLINE QHash<QString, Value> parameterFrame(const FunctionDef &def, const std::vector<Value> &args)
{
    QHash<QString, Value> frame;
    for (qsizetype i = 0; i < def.params.size() && i < static_cast<qsizetype>(args.size()); ++i)
        frame.insert(def.params.at(i), args[static_cast<std::size_t>(i)]);
    return frame;
}

/// A builtin other than `if` applied to its evaluated arguments.
Q_NEVER_INLINE Value callBuiltin(Builtin id, const QString &name, const std::vector<Value> &args, CalcLocale locale,
                  WorkBudget *budget, CalcError &err)
{
    switch (id) {
    case Builtin::Sum:
    case Builtin::Avg:
    case Builtin::Min:
    case Builtin::Max:
    case Builtin::Count:
    case Builtin::Median:
        return aggregateBuiltin(id, args, budget, err);
    case Builtin::Concat:
        return concatBuiltin(args, locale, budget, err);
    default:
        break;
    }
    std::vector<Value> nums;
    nums.reserve(args.size());
    for (const Value &a : args) {
        std::optional<Value> n = ops::toNumber(a, locale, err);
        if (!n)
            return {};
        nums.push_back(std::move(*n));
    }
    return numericBuiltin(id, nums, name.toLower(), err);
}

} // namespace

bool Evaluator::evaluateArguments(const Node &call, std::vector<Value> &args)
{
    args.reserve(call.children.size());
    for (const NodePtr &child : call.children) {
        args.push_back(eval(*child));
        if (failed())
            return false;
    }
    return true;
}

/// `if(cond, a[, b])`: only the chosen branch is evaluated.
Value Evaluator::evalIf(const Node &call)
{
    const bool condition = ops::truthy(eval(*call.children[0]));
    if (failed())
        return {};
    if (condition)
        return eval(*call.children[1]);
    return call.children.size() > 2 ? eval(*call.children[2]) : Value::boolean(false);
}

Value Evaluator::evalCall(const Node &node)
{
    const QString &name = node.text;
    std::vector<Value> args;
    if (const Value *variable = lookupVariable(name)) {
        if (!variable->isFunction())
            return fail(CalcErrorCode::NotAFunction, name);
        const std::shared_ptr<const FunctionDef> def = variable->functionDef();
        if (!evaluateArguments(node, args))
            return {};
        return callUserFunction(*def, args);
    }
    const std::optional<BuiltinInfo> info = findBuiltin(name);
    if (!info)
        return fail(CalcErrorCode::UndefinedFunction, name);
    const qsizetype count = static_cast<qsizetype>(node.children.size());
    if (count < info->minArgs)
        return fail(CalcErrorCode::TooFewArgs, name);
    if (info->maxArgs >= 0 && count > info->maxArgs)
        return fail(CalcErrorCode::TooManyArgs, name);
    if (info->id == Builtin::If)
        return evalIf(node);
    if (!evaluateArguments(node, args))
        return {};
    CalcError err;
    Value result = callBuiltin(info->id, name, args, m_env.locale, m_env.budget, err);
    return check(std::move(result), err);
}

Value Evaluator::callUserFunction(const FunctionDef &def, const std::vector<Value> &args)
{
    const qsizetype count = static_cast<qsizetype>(args.size());
    if (count < def.params.size())
        return fail(CalcErrorCode::TooFewArgs, def.name);
    if (count > def.params.size())
        return fail(CalcErrorCode::TooManyArgs, def.name);
    if (m_frames.size() >= kMaxCallDepth || !def.body)
        return fail(CalcErrorCode::Recursion);
    m_frames.append(parameterFrame(def, args));
    Value r = eval(*def.body);
    m_frames.removeLast();
    return r;
}

// --- statements ------------------------------------------------------------------------------

RunResult runProgram(ParsedProgram &program, Evaluator &evaluator)
{
    RunResult result;
    const EvalEnv &env = evaluator.env();
    const auto assign = [&env](const QString &name, const Value &value) {
        if (env.scope)
            env.scope->set(name, value);
        if (env.assigned)
            env.assigned->insert(name);
    };
    for (Statement &s : program.statements) {
        result.aggregate = false;
        result.assigned.clear();
        switch (s.kind) {
        case Statement::Kind::Expression: {
            const Node &e = *s.expr;
            if (e.kind == NodeKind::Ident) {
                const IdentKind kind = evaluator.classify(e.text);
                if (kind == IdentKind::Unit || kind == IdentKind::Currency) {
                    result.value = {}; // a bare "km" or "zł" shows nothing
                    continue;
                }
                result.aggregate = kind == IdentKind::Aggregate;
            } else if (e.kind == NodeKind::CurrencyUnit) {
                result.value = {};
                continue;
            }
            result.value = evaluator.evaluate(e);
            break;
        }
        case Statement::Kind::Assignment: {
            const Node &e = *s.expr;
            result.aggregate = e.kind == NodeKind::Ident && evaluator.classify(e.text) == IdentKind::Aggregate;
            Value v = evaluator.evaluate(e);
            if (evaluator.failed())
                break;
            if (v.isNone()) {
                result.error = CalcError{CalcErrorCode::ValueExpected, {}};
                result.value = {};
                return result;
            }
            assign(s.name, v);
            result.value = std::move(v);
            result.assigned = s.name;
            break;
        }
        case Statement::Kind::FunctionDefinition: {
            auto def = std::make_shared<FunctionDef>();
            def->name = s.name;
            def->params = s.params;
            def->body = std::move(s.expr);
            Value v = Value::function(std::move(def));
            assign(s.name, v);
            result.value = std::move(v);
            result.assigned = s.name;
            break;
        }
        }
        if (evaluator.failed()) {
            result.error = evaluator.error();
            result.value = {};
            result.assigned.clear();
            return result;
        }
    }
    return result;
}

} // namespace brulion::calc
