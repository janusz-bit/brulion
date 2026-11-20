// Standalone expressions (expression.h): parsing either syntax and evaluating row by row.
#include "dataview/expression_p.h"
#include "dataview/messages_p.h"

namespace brulion::dataview {

Expression::Expression() = default;
Expression::~Expression() = default;
Expression::Expression(const Expression &) = default;
Expression &Expression::operator=(const Expression &) = default;

std::variant<Expression, QString> parseExpression(const QString &text, Syntax syntax)
{
    const std::variant<ParsedExpression, QString> parsed =
            syntax == Syntax::Bases ? parseBasesExpression(text) : parseDataviewExpression(text);
    if (const QString *error = std::get_if<QString>(&parsed))
        return *error;
    const ParsedExpression &p = std::get<ParsedExpression>(parsed);
    Expression e;
    e.m_expr = p.expr;
    e.m_text = text;
    e.m_formulas = p.formulas;
    e.m_property = p.property;
    return e;
}

struct Evaluator::Private {
    Private(const VaultIndex &index, Options opts) : options(std::move(opts)), ctx(index, options) { }
    Options options; // (before ctx: the context keeps a reference to it)
    Context ctx;
    bool exhausted = false;
};

Evaluator::Evaluator(const VaultIndex &index, Options options)
    : d(std::make_unique<Private>(index, std::move(options)))
{ }

Evaluator::~Evaluator() = default;

Value Evaluator::page(const VaultPath &path)
{
    return d->ctx.page(path);
}

Value Evaluator::block(const VaultPath &path, int blockIndex)
{
    return d->ctx.item(path, blockIndex);
}

Value Evaluator::file(const VaultPath &path)
{
    return d->ctx.fileRow(path);
}

std::variant<Value, QString> Evaluator::evaluate(const Expression &expression, const Value &row,
                                                 const QList<std::pair<QString, Value>> &bound)
{
    if (expression.isNull())
        return Value();
    if (d->exhausted)
        return messages::tooCostly();
    try {
        return dataview::evaluate(*expression.m_expr, d->ctx, row, bound);
    } catch (const EvalError &e) {
        if (e.message.isEmpty()) {
            d->exhausted = true;
            return messages::tooCostly();
        }
        return e.message;
    }
}

bool Evaluator::exhausted() const
{
    return d->exhausted;
}

} // namespace brulion::dataview
