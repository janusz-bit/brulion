// Internal: evaluator of calc ASTs and the arithmetic on Values. Not part of the public API.
#pragma once

#include "calc/ast_p.h"
#include "calc/errors.h"
#include "calc/parser_p.h"
#include "calc/value.h"

#include <QHash>
#include <QList>
#include <QSet>

#include <functional>
#include <optional>
#include <vector>

namespace brulion::calc {

/// Maximum length of one expression (a calc line, an inline calculation, a table cell).
inline constexpr qsizetype kMaxExpressionLength = 4000;
/// Maximum length of a string result.
inline constexpr qsizetype kMaxStringLength = 100000;
/// Maximum number of items of a list built by an expression (`[x, x, x]`; table ranges are
/// bounded by the table and the work budget instead).
inline constexpr qsizetype kMaxListLength = 20000;

/// Deterministic work budget, so that a note from anywhere (imports, sync) cannot freeze the app
/// with one expression: every evaluated node, list element, aggregated line result and table
/// dependency costs one unit, and so does every kStringChunk characters copied or compared.
struct WorkBudget {
    static constexpr qint64 kExpressionLimit = 1'000'000; ///< per expression
    static constexpr qint64 kTotalLimit = 5'000'000;      ///< per public call (block, table, page)
    static constexpr qint64 kStringChunk = 64;            ///< characters per unit of string work
    /// Units per element copied into a new list: list values are large, so this also bounds the
    /// memory one call can allocate for lists (5M / 50 = 100 000 values, ~17 MB).
    static constexpr qint64 kListElement = 50;

    qint64 expression = 0;
    qint64 total = 0;

    void startExpression() { expression = 0; }
    /// Account for work; false once a budget is exhausted.
    bool charge(qint64 units)
    {
        expression += units;
        total += units;
        return expression <= kExpressionLimit && total <= kTotalLimit;
    }
    /// Work that belongs to no single expression (table dependency analysis).
    bool chargeTotal(qint64 units)
    {
        total += units;
        return total <= kTotalLimit;
    }
};

enum class AggregateKind { Sum, Average };
/// sum/total/suma/razem -> Sum, avg/average/mean/średnia/srednia -> Average (case-insensitive).
std::optional<AggregateKind> aggregateWord(QStringView word);

/// What an identifier stands for, in resolution order.
enum class IdentKind { Variable, Aggregate, Constant, Unit, Currency, Unknown };

struct EvalEnv {
    CalcLocale locale = CalcLocale::Polish;
    Scope *scope = nullptr;
    WorkBudget *budget = nullptr;
    /// Running aggregate of a calc block (`suma`, `avg`); unset elsewhere.
    std::function<Value(AggregateKind, CalcError &)> aggregate;
    /// Value of a table reference (index into ParsedProgram::refs); unset outside tables.
    std::function<Value(int, CalcError &)> reference;
    /// When set, collects the names assigned in `scope` (page scopes record what changed).
    QSet<QString> *assigned = nullptr;
};

/// Evaluates expression trees. Errors never throw: the first error is kept and every further
/// evaluation returns a None value until clearError().
class Evaluator {
public:
    explicit Evaluator(EvalEnv env);

    Value evaluate(const Node &node);
    const CalcError &error() const { return m_error; }
    bool failed() const { return m_error.isError(); }
    void clearError() { m_error = {}; }

    IdentKind classify(const QString &name) const;
    const EvalEnv &env() const { return m_env; }

private:
    Value fail(CalcErrorCode code, const QString &arg = {});
    Value check(Value v, CalcError &err);

    Value eval(const Node &node);
    Value evalIdent(const QString &name);
    Value evalTernary(const Node &node);
    Value evalRef(const Node &node);
    Value evalUnary(const Node &node);
    Value evalBinary(const Node &node);
    /// `node`'s operator applied to the already evaluated left operand and its right child.
    Value applyBinary(const Node &node, const Value &left);
    Value evalLogical(const Node &node);
    Value evalCompare(const Node &node);
    Value evalList(const Node &node);
    Value evalConvert(const Node &node);
    Value evalPercentOf(const Node &node);
    Value evalCurrencyPrefix(const Node &node);
    Value evalCall(const Node &node);
    Value evalIf(const Node &call);
    /// Evaluate the arguments of a call into `args`; false on error.
    bool evaluateArguments(const Node &call, std::vector<Value> &args);
    Value callUserFunction(const FunctionDef &def, const std::vector<Value> &args);

    struct UnitTarget {
        Unit unit;
        Currency currency;
    };
    std::optional<UnitTarget> unitTarget(const Node &node);

    const Value *lookupVariable(const QString &name) const;

    EvalEnv m_env;
    CalcError m_error;
    int m_depth = 0;
    QList<QHash<QString, Value>> m_frames; ///< parameters of active user-function calls
};

/// Result of running all statements of one line.
struct RunResult {
    Value value;            ///< value of the last statement (None when there is nothing to show)
    QString assigned;       ///< variable/function assigned by the last assignment
    bool aggregate = false; ///< the line is a bare aggregate keyword (`suma`, `x = sum`)
    CalcError error;
};

/// Execute the statements of a parsed line: assignments go to `*evaluator.env().scope`. A bare
/// unit or currency word ("km", "zł") produces no value.
RunResult runProgram(ParsedProgram &program, Evaluator &evaluator);

namespace ops {

/// Number view of a value: Bool -> 0/1, numeric strings parsed; nullopt + error otherwise.
std::optional<Value> toNumber(const Value &v, CalcLocale locale, CalcError &err);
/// a + b; with `percentSugar`, `x + 10%` means x · 1.1 (and `10% + 5%` = 15%).
Value add(const Value &a, const Value &b, CalcError &err, bool percentSugar = true);
Value subtract(const Value &a, const Value &b, CalcError &err, bool percentSugar = true);
Value multiply(const Value &a, const Value &b, CalcError &err);
Value divide(const Value &a, const Value &b, CalcError &err);
/// Sum of numeric values (no percent sugar); 0 for an empty list.
Value sum(const std::vector<Value> &values, CalcError &err);
bool truthy(const Value &v);

} // namespace ops

} // namespace brulion::calc
