// Inline queries (`` `= this.file.name` ``, dataview.h evaluateInline()) and the cheap test of a
// CALENDAR query the editor asks for every block (isCalendarQuery()).
#include "dataview/dataview.h"
#include "dataview/eval_p.h"
#include "dataview/expression.h"
#include "dataview/messages_p.h"

#include <algorithm>

namespace brulion::dataview {

namespace {

/// Steps one inline query may take: it is evaluated while its text is drawn (a query block runs
/// with Options::maxSteps), so a costly one stops sooner — with the message of a query too costly.
constexpr qint64 InlineMaxSteps = 2000000;

} // namespace

bool isCalendarQuery(QStringView source)
{
    qsizetype start = 0;
    while (start < source.size()) {
        qsizetype end = source.indexOf(u'\n', start);
        if (end < 0)
            end = source.size();
        const QStringView line = source.sliced(start, end - start).trimmed();
        start = end + 1;
        if (line.isEmpty() || line.startsWith(u"//"))
            continue;
        constexpr QStringView word = u"calendar";
        return line.size() >= word.size() && line.first(word.size()).compare(word, Qt::CaseInsensitive) == 0
               && (line.size() == word.size() || !(line[word.size()].isLetterOrNumber() || line[word.size()] == u'_'));
    }
    return false;
}

std::optional<InlineValue> evaluateInline(const QString &expression, const VaultIndex &index, const Options &options)
{
    const std::variant<Expression, QString> parsed = parseExpression(expression, Syntax::Dataview);
    if (std::holds_alternative<QString>(parsed))
        return std::nullopt;
    Options opts = options;
    if (!opts.now.isValid())
        opts.now = QDateTime::currentDateTime();
    opts.maxSteps = std::min(opts.maxSteps, InlineMaxSteps);
    InlineValue out;
    try {
        Evaluator evaluator(index, opts);
        // The page's own fields by their bare names as well as through `this` (Dataview's inline
        // queries are evaluated for the page they are on).
        const Value row = evaluator.page(opts.currentPath);
        const std::variant<Value, QString> value = evaluator.evaluate(std::get<Expression>(parsed), row);
        if (const QString *error = std::get_if<QString>(&value)) {
            out.kind = InlineValue::Kind::Error;
            out.text = *error;
            return out;
        }
        const Value &v = std::get<Value>(value);
        if (v.isNull())
            return out;
        out.kind = InlineValue::Kind::Value;
        out.markdown = toMarkdown(v, index);
        out.text = toPlainText(v);
    } catch (const EvalError &e) {
        // (Showing a value can still be too large: a list of a whole notebook's texts.)
        out = InlineValue{InlineValue::Kind::Error, QString(), e.message.isEmpty() ? messages::tooCostly() : e.message};
    }
    return out;
}

} // namespace brulion::dataview
