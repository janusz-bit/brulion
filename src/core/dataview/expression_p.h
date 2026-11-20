// Standalone expressions (expression.h): the parsers of both syntaxes and the functions Bases
// expressions call. Internal to the dataview module.
#pragma once

#include "dataview/eval_p.h"
#include "dataview/expression.h"

#include <variant>

namespace brulion::dataview {

/// An expression parsed alone, with what Expression keeps of it.
struct ParsedExpression {
    ExprPtr expr;
    QStringList formulas; ///< Bases: the formulas it reads
    QString property;     ///< the property it is, when it is one name and nothing else
};

/// One Dataview expression (parse.cpp): the whole text, nothing after it.
std::variant<ParsedExpression, QString> parseDataviewExpression(const QString &source);
/// One Bases expression (bases.cpp), translated into Dataview's tree: methods and Bases' own
/// functions become calls of "b:" built-ins (findBuiltin() finds them; Dataview queries cannot
/// name them: ':' is no identifier character).
std::variant<ParsedExpression, QString> parseBasesExpression(const QString &source);
/// A "b:" built-in (bases.cpp), or nullptr.
const Builtin *findBasesBuiltin(const QString &name);

/// `a op b` as Dataview evaluates it (eval.cpp): + - * / % and the comparisons = != < <= > >=.
Value applyOperator(const QString &op, const Value &a, const Value &b, Context &ctx);

} // namespace brulion::dataview
