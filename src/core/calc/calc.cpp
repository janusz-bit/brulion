#include "calc/calc.h"

#include "calc/blocks_p.h"
#include "calc/evaluator_p.h"
#include "calc/lexer_p.h"
#include "calc/parser_p.h"
#include "calc/util_p.h"

#include "text/text.h"

#include <algorithm>

namespace brulion::calc {

namespace {

struct LabelSplit {
    QString label;
    qsizetype restIndex = 0; ///< first token after the ':'
};

/// `Label: expression` — the text before the first top-level ':' is a label when it starts with
/// a letter and contains no assignment, comparison, '?' or string.
std::optional<LabelSplit> splitLabel(const QList<Token> &tokens, QStringView source)
{
    int depth = 0;
    qsizetype colon = -1;
    for (qsizetype i = 0; i < tokens.size() && colon < 0; ++i) {
        const Token &t = tokens.at(i);
        if (t.kind == TokenKind::LParen || t.kind == TokenKind::LBracket)
            ++depth;
        else if (t.kind == TokenKind::RParen || t.kind == TokenKind::RBracket)
            --depth;
        else if (depth == 0 && t.isOperator(u":"))
            colon = i;
    }
    if (colon <= 0)
        return std::nullopt;
    const Token &first = tokens.front();
    qsizetype length = 0;
    if (first.kind != TokenKind::Ident || first.text.isEmpty()
        || !QChar::isLetter(detail::codePointAt(first.text, 0, &length))) // 𝒜 is a surrogate pair
        return std::nullopt;
    const auto notLabel = [](const Token &t) {
        return t.kind == TokenKind::String
               || (t.kind == TokenKind::Operator && (t.text.contains(u'=') || t.text == u"?"));
    };
    if (std::ranges::any_of(tokens.cbegin(), tokens.cbegin() + colon, notLabel))
        return std::nullopt;
    return LabelSplit{source.left(tokens.at(colon).pos).trimmed().toString(), colon + 1};
}

/// Sum or average of the results of a group of calc lines. Mixed units fall back to the plain
/// numbers; currencies label the result when they agree. Every value costs one unit of `budget`:
/// a `sum` on every line of a long block is quadratic work.
Value aggregateGroup(AggregateKind kind, const std::vector<Value> &group, WorkBudget &budget, CalcError &err)
{
    if (!budget.charge(1 + static_cast<qint64>(group.size()))) {
        err = {CalcErrorCode::TooComplex, {}};
        return {};
    }
    std::vector<const Value *> values;
    for (const Value &v : group) {
        if (v.isNumber() && !v.isPercent())
            values.push_back(&v);
    }
    if (values.empty()) {
        if (kind == AggregateKind::Sum)
            return Value::number(0);
        err = {CalcErrorCode::NoValues, {}};
        return {};
    }
    CalcError sumError;
    Value total = ops::sum(values, sumError);
    if (sumError) {
        std::erase_if(values, [](const Value *v) { return v->hasUnit(); });
        if (values.empty()) {
            err = sumError;
            return {};
        }
        total = ops::sum(values, err);
        if (err)
            return {};
    }
    if (kind == AggregateKind::Sum)
        return total;
    return ops::divide(total, Value::number(static_cast<double>(values.size())), err);
}

struct LineState {
    Scope &scope;
    std::vector<Value> group;
    CalcLocale locale;
    WorkBudget &budget;
    QSet<QString> *assigned = nullptr; ///< names set in `scope` (for page scopes)
};

void showValue(CalcLineResult &res, const Value &value, bool aggregate, LineState &state)
{
    if (value.isNone())
        return;
    res.value = value.format(state.locale);
    res.numeric = value.numeric();
    // Percentages are rates, not results: they neither become `ans` nor join the group.
    if (value.isFunction() || value.isPercent())
        return;
    state.scope.set(QStringLiteral("ans"), value);
    if (state.assigned)
        state.assigned->insert(QStringLiteral("ans"));
    if (!aggregate)
        state.group.push_back(value);
}

/// Evaluate one non-blank, non-comment line into `res`.
void evaluateLine(QStringView line, CalcLineResult &res, LineState &state)
{
    state.budget.startExpression();
    if (line.size() > kMaxExpressionLength) {
        res.error = CalcError{CalcErrorCode::TooLong, {}}.message(state.locale);
        return;
    }
    const QString source = detail::toNfc(line);
    QList<Token> tokens = lex(source);
    if (const std::optional<LabelSplit> split = splitLabel(tokens, source)) {
        res.label = split->label;
        tokens = tokens.mid(split->restIndex);
    }
    normalizeNumbers(tokens, source, state.locale);
    if (tokens.isEmpty()) {
        // "Razem:" — a label that is itself an aggregate word.
        const std::optional<AggregateKind> kind = aggregateWord(res.label);
        if (kind && !state.scope.contains(res.label)) {
            CalcError err;
            const Value v = aggregateGroup(*kind, state.group, state.budget, err);
            if (err)
                res.error = err.message(state.locale);
            else
                showValue(res, v, true, state);
        }
        return;
    }
    ParsedProgram program = parseProgram(tokens, source, ParseOptions{});
    if (program.error) {
        res.error = program.error.message(state.locale);
        return;
    }
    const auto aggregate = [&state](AggregateKind kind, CalcError &err) {
        return aggregateGroup(kind, state.group, state.budget, err);
    };
    Evaluator evaluator({.locale = state.locale,
                         .scope = &state.scope,
                         .budget = &state.budget,
                         .aggregate = aggregate,
                         .assigned = state.assigned});
    const RunResult run = runProgram(program, evaluator);
    if (run.error) {
        res.error = run.error.message(state.locale);
        return;
    }
    res.assigned = run.assigned;
    showValue(res, run.value, run.aggregate, state);
}

} // namespace

QList<CalcLineResult> evaluateCalcInto(QStringView source, Scope &scope, CalcLocale locale, WorkBudget &budget,
                                       QSet<QString> *assigned)
{
    LineState state{scope, {}, locale, budget, assigned};
    QList<CalcLineResult> results;
    const QList<text::Line> lines = text::splitLines(source);
    results.reserve(lines.size());
    for (qsizetype i = 0; i < lines.size(); ++i) {
        CalcLineResult res;
        res.line = static_cast<int>(i);
        res.input = lines.at(i).text;
        const QStringView trimmed = QStringView(res.input).trimmed();
        if (trimmed.isEmpty())
            state.group.clear();
        else if (!trimmed.startsWith(u'#') && !trimmed.startsWith(u"//"))
            evaluateLine(trimmed, res, state);
        results.append(std::move(res));
    }
    return results;
}

QList<CalcLineResult> evaluateCalc(QStringView source, Scope &scope, CalcLocale locale)
{
    WorkBudget budget;
    return evaluateCalcInto(source, scope, locale, budget);
}

CalcLineResult evaluateInline(QStringView expression, const Scope &scope, CalcLocale locale)
{
    CalcLineResult res;
    res.input = expression.toString();
    QStringView text = expression.trimmed();
    if (text.startsWith(u'='))
        text = text.mid(1).trimmed();
    if (text.isEmpty())
        return res;
    if (text.size() > kMaxExpressionLength) {
        res.error = CalcError{CalcErrorCode::TooLong, {}}.message(locale);
        return res;
    }
    const QString source = detail::toNfc(text);
    QList<Token> tokens = lex(source);
    normalizeNumbers(tokens, source, locale);
    if (tokens.isEmpty())
        return res;
    ParsedProgram program = parseProgram(tokens, source, ParseOptions{});
    if (program.error) {
        res.error = program.error.message(locale);
        return res;
    }
    Scope local = scope; // assignments stay local
    WorkBudget budget;
    Evaluator evaluator({.locale = locale, .scope = &local, .budget = &budget});
    const RunResult run = runProgram(program, evaluator);
    if (run.error) {
        res.error = run.error.message(locale);
        return res;
    }
    res.assigned = run.assigned;
    if (!run.value.isNone()) {
        res.value = run.value.format(locale);
        res.numeric = run.value.numeric();
    }
    return res;
}

} // namespace brulion::calc
