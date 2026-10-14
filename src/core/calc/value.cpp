#include "calc/value.h"

#include "calc/ast_p.h"
#include "calc/errors.h"
#include "calc/util_p.h"

#include <cmath>

namespace brulion::calc {

namespace {

/// Longest text shown for one value; longer output is cut with an ellipsis.
constexpr qsizetype kMaxDisplayLength = 1000;

QString truncated(QString s)
{
    if (s.size() > kMaxDisplayLength) {
        s.truncate(kMaxDisplayLength - 1);
        s += QChar(0x2026);
    }
    return s;
}

QString formatQuantity(const Value &v, CalcLocale locale, NumberFormatOptions options)
{
    const double magnitude = v.magnitude();
    const Unit &unit = v.unit();
    if (v.isPercent() && unit.isEmpty())
        return formatNumber(magnitude * 100, locale, options) + u'%';
    if (v.hasCurrency() && std::isfinite(magnitude)) {
        const QString money = formatMoney(magnitude, v.currency(), locale, options);
        if (unit.isEmpty())
            return money;
        const QList<UnitFactor> &factors = unit.factors();
        if (factors.size() == 1 && factors.at(0).power < 0) { // 12 zł/kg
            const UnitFactor &f = factors.at(0);
            QString per = f.def->displaySymbol(locale).toString();
            if (f.power != -1)
                per += superscript(-f.power);
            return money + u'/' + per;
        }
        return money + u' ' + unit.toString(locale);
    }
    const QString number = formatNumber(magnitude, locale, options);
    if (unit.isEmpty())
        return number;
    const QString unitText = unit.toString(locale);
    if (unitText.startsWith(u'°'))
        return number + unitText;
    return number + u' ' + unitText;
}

QString formatList(const std::vector<Value> &items, CalcLocale locale, NumberFormatOptions options)
{
    // English lists use ',' between items, so numbers inside are not grouped ("1000, 2500.5").
    const QString separator = locale == CalcLocale::Polish ? QStringLiteral("; ") : QStringLiteral(", ");
    NumberFormatOptions inner = options;
    inner.grouping = locale != CalcLocale::English && options.grouping;
    QString out = QStringLiteral("[");
    for (std::size_t i = 0; i < items.size(); ++i) {
        if (i > 0)
            out += separator;
        out += items[i].format(locale, inner);
        if (out.size() > kMaxDisplayLength)
            return out;
    }
    out += u']';
    return out;
}

} // namespace

Value Value::number(double value)
{
    Value v;
    v.m_type = Type::Number;
    v.m_si = value;
    return v;
}

Value Value::quantity(double si, Unit unit)
{
    Value v = number(si);
    v.m_unit = std::move(unit);
    return v;
}

Value Value::percent(double fraction)
{
    Value v = number(fraction);
    v.m_percent = true;
    return v;
}

Value Value::money(double amount, Currency currency)
{
    Value v = number(amount);
    v.m_currency = std::move(currency);
    return v;
}

Value Value::boolean(bool value)
{
    Value v;
    v.m_type = Type::Bool;
    v.m_bool = value;
    v.m_si = value ? 1 : 0;
    return v;
}

Value Value::string(QString text)
{
    Value v;
    v.m_type = Type::String;
    v.m_text = std::move(text);
    return v;
}

Value Value::list(std::vector<Value> items)
{
    Value v;
    v.m_type = Type::List;
    v.m_items = std::make_shared<const std::vector<Value>>(std::move(items));
    return v;
}

Value Value::function(std::shared_ptr<const FunctionDef> def)
{
    Value v;
    v.m_type = Type::Function;
    v.m_function = std::move(def);
    return v;
}

const std::vector<Value> &Value::items() const
{
    static const std::vector<Value> kNoItems;
    return m_items ? *m_items : kNoItems;
}

double Value::magnitude() const
{
    if (m_unit.isEmpty())
        return m_si;
    return m_si / m_unit.scale();
}

void Value::setCurrency(Currency currency)
{
    m_currency = std::move(currency);
    m_mixedCurrency = false;
}

void Value::setMixedCurrency()
{
    m_currency = Currency();
    m_mixedCurrency = true;
}

void Value::copyCurrency(const Value &other)
{
    m_currency = other.m_currency;
    m_mixedCurrency = other.m_mixedCurrency;
}

QString Value::format(CalcLocale locale, NumberFormatOptions options) const
{
    switch (m_type) {
    case Type::None:
        return {};
    case Type::Number:
        return truncated(formatQuantity(*this, locale, options));
    case Type::Bool:
        return booleanWord(m_bool, locale);
    case Type::String:
        return truncated(m_text);
    case Type::List:
        return truncated(formatList(items(), locale, options));
    case Type::Function:
        if (!m_function)
            return QStringLiteral("ƒ");
        return m_function->name + u'(' + m_function->params.join(QStringLiteral(", ")) + u')';
    }
    return {};
}

std::optional<double> Value::numeric() const
{
    if (m_type != Type::Number)
        return std::nullopt;
    return magnitude();
}

bool operator==(const Value &a, const Value &b)
{
    if (a.m_type != b.m_type)
        return false;
    switch (a.m_type) {
    case Value::Type::None:
        return true;
    case Value::Type::Number:
        return a.m_si == b.m_si && a.m_unit == b.m_unit && a.m_percent == b.m_percent
               && a.m_currency == b.m_currency && a.m_mixedCurrency == b.m_mixedCurrency;
    case Value::Type::Bool:
        return a.m_bool == b.m_bool;
    case Value::Type::String:
        return a.m_text == b.m_text;
    case Value::Type::List:
        return a.m_items == b.m_items || a.items() == b.items();
    case Value::Type::Function:
        return a.m_function == b.m_function;
    }
    return false;
}

// --- Scope ---------------------------------------------------------------------------------

bool Scope::contains(const QString &name) const
{
    return m_values.contains(detail::toNfc(name));
}

const Value *Scope::find(const QString &name) const
{
    const auto it = m_values.constFind(name);
    return it == m_values.constEnd() ? nullptr : &it.value();
}

Value Scope::value(const QString &name) const
{
    return m_values.value(detail::toNfc(name));
}

void Scope::set(const QString &name, const Value &value)
{
    m_values.insert(detail::toNfc(name), value);
}

bool Scope::remove(const QString &name)
{
    return m_values.remove(detail::toNfc(name));
}

QStringList Scope::names() const
{
    QStringList names = m_values.keys();
    names.sort();
    return names;
}

std::optional<double> Scope::number(const QString &name) const
{
    const auto it = m_values.constFind(detail::toNfc(name));
    if (it == m_values.constEnd())
        return std::nullopt;
    return it->numeric();
}

QString Scope::formatted(const QString &name, CalcLocale locale) const
{
    return value(name).format(locale);
}

std::optional<Value> Scope::ans() const
{
    const Value *v = find(QStringLiteral("ans"));
    return v ? std::optional<Value>(*v) : std::nullopt;
}

} // namespace brulion::calc
