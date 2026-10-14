// Values and variable scopes of the calc engine. Part of the calc module — see calc/calc.h.
#pragma once

#include "calc/calclocale.h"
#include "calc/numbers.h"
#include "calc/units.h"

#include <QHash>
#include <QString>
#include <QStringList>

#include <memory>
#include <optional>
#include <vector>

namespace brulion::calc {

struct FunctionDef; // a user-defined function ("f(x) = x^2"), see calc/ast_p.h

/// A calculation result: a number (with optional unit, currency tag and percent flag), a
/// boolean, a string, a flat list (table ranges, `[1, 2]`) or a user-defined function.
///
/// Numbers are stored in SI base units together with the unit they are displayed in:
/// `Value::quantity(5000, Unit(findUnit(u"km")))` is "5 km". Percentages are fractions with
/// the percent flag ("15%" = 0.15). Currencies are tags, never converted.
class Value {
public:
    enum class Type { None, Number, Bool, String, List, Function };

    Value() = default;
    /// A plain (dimensionless) number.
    static Value number(double value);
    /// A quantity: `si` in SI base units, displayed in `unit`.
    static Value quantity(double si, Unit unit);
    /// A percentage given as a fraction (0.15 for 15%).
    static Value percent(double fraction);
    /// An amount of money.
    static Value money(double amount, Currency currency);
    static Value boolean(bool value);
    static Value string(QString text);
    static Value list(std::vector<Value> items);
    static Value function(std::shared_ptr<const FunctionDef> def);

    Type type() const { return m_type; }
    bool isNone() const { return m_type == Type::None; }
    bool isNumber() const { return m_type == Type::Number; }
    bool isBool() const { return m_type == Type::Bool; }
    bool isString() const { return m_type == Type::String; }
    bool isList() const { return m_type == Type::List; }
    bool isFunction() const { return m_type == Type::Function; }

    /// The number in SI base units (0 for non-numbers).
    double si() const { return m_si; }
    /// The number in its display unit (the fraction for percentages).
    double magnitude() const;
    const Unit &unit() const { return m_unit; }
    bool hasUnit() const { return !m_unit.isEmpty(); }
    /// The currency tag; invalid when none (or when operands had different currencies).
    const Currency &currency() const { return m_currency; }
    bool hasCurrency() const { return m_currency.isValid(); }
    /// Operands with different currencies were combined (the result shows no currency).
    bool isMixedCurrency() const { return m_mixedCurrency; }
    bool isPercent() const { return m_percent; }
    bool boolValue() const { return m_bool; }
    const QString &text() const { return m_text; }
    /// The items of a list (empty for other values). Copies of a list share its items.
    const std::vector<Value> &items() const;
    const std::shared_ptr<const FunctionDef> &functionDef() const { return m_function; }

    void setSi(double si) { m_si = si; }
    void setUnit(Unit unit) { m_unit = std::move(unit); }
    void setPercent(bool percent) { m_percent = percent; }
    void setCurrency(Currency currency);
    /// Mark as combined from different currencies (drops the tag).
    void setMixedCurrency();
    /// Copy the currency tag (or the mixed marker) of `other`.
    void copyCurrency(const Value &other);

    /// Display text: "12,5", "3,11 mi", "17 zł", "15%", "prawda", "[1; 2]", "f(x)"; empty for None.
    QString format(CalcLocale locale, NumberFormatOptions options = {}) const;
    /// Numeric magnitude in the display unit (fraction for percentages); nullopt for non-numbers.
    std::optional<double> numeric() const;

    friend bool operator==(const Value &a, const Value &b);

private:
    Type m_type = Type::None;
    double m_si = 0;
    bool m_bool = false;
    bool m_percent = false;
    bool m_mixedCurrency = false;
    Unit m_unit;
    Currency m_currency;
    QString m_text;
    std::shared_ptr<const std::vector<Value>> m_items; ///< shared: copying a Value is cheap
    std::shared_ptr<const FunctionDef> m_function;
};

/// Variables of a calculation (calc block, page, inline expression). Names are NFC-normalised
/// Unicode identifiers ("ilość", "cena_netto"); `ans` is stored as an ordinary variable.
/// Copies are cheap (implicitly shared).
class Scope {
public:
    bool contains(const QString &name) const;
    /// The variable's value, or nullptr. `name` must already be NFC (no normalisation).
    const Value *find(const QString &name) const;
    /// The variable's value, or a None value.
    Value value(const QString &name) const;
    void set(const QString &name, const Value &value);
    bool remove(const QString &name);
    void clear() { m_values.clear(); }
    /// Variable names, sorted.
    QStringList names() const;
    qsizetype size() const { return m_values.size(); }
    bool isEmpty() const { return m_values.isEmpty(); }

    /// Magnitude of a numeric variable in its display unit (fraction for percentages).
    std::optional<double> number(const QString &name) const;
    /// The variable's display text (empty when unknown).
    QString formatted(const QString &name, CalcLocale locale) const;
    /// The previous result (`ans`), when there is one.
    std::optional<Value> ans() const;

    friend bool operator==(const Scope &a, const Scope &b) { return a.m_values == b.m_values; }

private:
    QHash<QString, Value> m_values;
};

} // namespace brulion::calc
