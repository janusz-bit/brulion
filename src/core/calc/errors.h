// Calculation errors with Polish/English messages. Part of the calc module — see calc/calc.h.
#pragma once

#include "calc/calclocale.h"

#include <QString>

namespace brulion::calc {

/// What went wrong while evaluating an expression. Most codes take one argument (`CalcError::arg`):
/// a name, an operator, a fragment of the input, a cell name, ...
enum class CalcErrorCode {
    None,
    Generic,
    UndefinedSymbol,   ///< arg: variable name
    UndefinedFunction, ///< arg: function name
    UnitsMismatch,
    UnexpectedEnd,
    ParenExpected, ///< arg: the missing bracket
    ValueExpected,
    UnexpectedOperator, ///< arg: operator
    UnexpectedPart,     ///< arg: fragment
    CannotAssign,       ///< arg: name
    NotANumber,         ///< arg: text
    WrongType,          ///< arg: function or operator name
    TooManyArgs,        ///< arg: function name
    TooFewArgs,         ///< arg: function name
    Recursion,
    TooLong,
    TooLarge, ///< arg: limit
    TooComplex,
    TooBig,
    NotAFunction, ///< arg: name
    CurrencyConversion,
    NoValues,
    Cycle,
    RefError,   ///< arg: cell name ("B1")
    RefOutside, ///< arg: reference as written ("A99")
    Domain,     ///< arg: function name
    UnterminatedString,
    NotAUnit, ///< arg: name
    ListNotAllowed,
    /// arg: function name (`round(2,5)`: a decimal comma as well as an argument separator)
    AmbiguousComma,
};

/// An evaluation error: a code plus an optional argument, translated on demand.
struct CalcError {
    CalcErrorCode code = CalcErrorCode::None;
    QString arg;

    bool isError() const { return code != CalcErrorCode::None; }
    explicit operator bool() const { return isError(); }
    /// Short, user-facing message ("Nieznana zmienna: x" / "Unknown variable: x").
    QString message(CalcLocale locale) const;

    friend bool operator==(const CalcError &a, const CalcError &b)
    {
        return a.code == b.code && a.arg == b.arg;
    }
};

/// The localised word for a boolean value ("prawda"/"fałsz", "true"/"false").
QString booleanWord(bool value, CalcLocale locale);

} // namespace brulion::calc
