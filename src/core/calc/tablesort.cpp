// Sorting and filtering GFM tables (see tablesort.h).
#include "calc/tablesort.h"

#include "calc/evaluator_p.h"
#include "calc/lexer_p.h"
#include "calc/numbers.h"
#include "calc/parser_p.h"
#include "calc/util_p.h"

#include "dates/local.h"
#include "dates/natural.h"
#include "text/text.h"

#include <QDateTime>
#include <QRegularExpression>

#include <algorithm>
#include <climits>
#include <cmath>
#include <numeric>

using namespace Qt::StringLiterals;

namespace brulion::calc {

namespace {

/// What the summary rule needs to know of a formula's references.
struct FormulaReach {
    bool above = false; ///< `above`, a range of several rows, a whole column
    bool below = false; ///< `below`
};

FormulaReach formulaReach(QStringView cell, const QStringList &headers)
{
    FormulaReach out;
    const QStringView trimmed = cell.trimmed();
    if (!detail::isTableFormula(trimmed) || trimmed.size() > kMaxExpressionLength)
        return out;
    const QString source = detail::toNfc(trimmed.mid(1));
    QList<Token> tokens = lex(source);
    normalizeNumbers(tokens, source, CalcLocale::Polish);
    ParseOptions options;
    options.statements = false;
    options.tableRefs = true;
    options.headers = headers;
    const ParsedProgram program = parseProgram(tokens, source, options);
    for (const TableRef &ref : program.refs) {
        if (ref.kind == TableRef::Kind::Direction) {
            if (ref.direction == TableRef::Direction::Above)
                out.above = true;
            else if (ref.direction == TableRef::Direction::Below)
                out.below = true;
        } else if (ref.kind == TableRef::Kind::Range && (ref.row2 == INT_MAX || ref.row1 != ref.row2)) {
            out.above = true;
        }
    }
    return out;
}

/// Light Markdown of a cell left out for comparing: links show their text.
QString plainCell(QStringView cell)
{
    if (cell.isEmpty())
        return {};
    QString s = cell.toString();
    if (s.contains(u'[') || s.contains(u'<')) {
        static const QRegularExpression wiki(u"!?\\[\\[([^\\]|]*)(?:\\|([^\\]]*))?\\]\\]"_s);
        static const QRegularExpression link(u"!?\\[([^\\]]*)\\]\\([^)]*\\)"_s);
        static const QRegularExpression br(u"<br\\s*/?>"_s, QRegularExpression::CaseInsensitiveOption);
        QString out;
        qsizetype last = 0;
        for (auto it = wiki.globalMatch(s); it.hasNext();) {
            const auto m = it.next();
            out += QStringView(s).mid(last, m.capturedStart() - last);
            out += m.capturedLength(2) > 0 ? m.captured(2) : m.captured(1);
            last = m.capturedEnd();
        }
        out += QStringView(s).mid(last);
        s = std::move(out);
        s.replace(link, u"\\1"_s);
        s.replace(br, u" "_s);
    }
    return stripInlineMarkup(s);
}

std::optional<double> numberOf(QStringView value, CalcLocale locale)
{
    if (value.isEmpty() || value.size() > 200)
        return std::nullopt;
    const std::optional<ParsedNumber> n = parseLocaleNumber(value, locale);
    if (!n || !std::isfinite(n->value))
        return std::nullopt;
    return n->value;
}

/// A date (and time) written in a form Brulion reads: minutes since the epoch's day 0, so dates
/// with and without a time compare in one order.
std::optional<qint64> dateOf(QStringView value, const TableSortOptions &options)
{
    if (value.isEmpty() || value.size() > 80)
        return std::nullopt;
    QStringView v = value;
    if (v.startsWith(u"[[") && v.endsWith(u"]]")) // a link to a day (stripped already when shown)
        v = v.mid(2, v.size() - 4).trimmed();
    const auto minutes = [](const DateSpec &spec) {
        const QTime t = spec.time.value_or(QTime(0, 0));
        return spec.date.toJulianDay() * 1440 + t.hour() * 60 + t.minute();
    };
    if (const std::optional<DateSpec> iso = dates::parseDateSpec(v))
        return minutes(*iso);
    // Only explicit dates: words like "jutro" or "piątek" in a table are text.
    if (!options.today.isValid() || std::ranges::none_of(v, [](QChar c) { return c.isDigit(); }))
        return std::nullopt;
    const dates::UiLocale locale =
            options.locale == CalcLocale::English ? dates::UiLocale::English : dates::UiLocale::Polish;
    const std::optional<dates::NaturalDate> found =
            dates::parseNaturalDate(v, QDateTime(options.today, QTime(12, 0)), Qt::Monday, locale);
    if (!found || found->start != 0 || found->length != v.size() || !found->spec.isValid())
        return std::nullopt;
    return minutes(found->spec);
}

/// The values of a column's body cells (summary rows included).
QStringList columnValues(const TableModel &model, const TableEval &eval, int column)
{
    QStringList out;
    out.reserve(model.rows.size());
    for (qsizetype r = 0; r < model.rows.size(); ++r)
        out.append(tableCellValue(model, eval, int(r), column));
    return out;
}

SortKind kindOf(const QStringList &values, qsizetype from, qsizetype to, const TableSortOptions &options)
{
    bool any = false;
    bool numbers = true;
    for (qsizetype i = from; i < to && numbers; ++i) {
        if (values.at(i).isEmpty())
            continue;
        any = true;
        numbers = numberOf(values.at(i), options.locale).has_value();
    }
    if (!any)
        return SortKind::Text;
    if (numbers)
        return SortKind::Number;
    for (qsizetype i = from; i < to; ++i) {
        if (!values.at(i).isEmpty() && !dateOf(values.at(i), options))
            return SortKind::Text;
    }
    return SortKind::Date;
}

QStringList normalizedHeaders(const TableModel &model)
{
    QStringList headers;
    headers.reserve(model.header.size());
    for (const QString &h : model.header)
        headers.append(normalizeHeader(h));
    return headers;
}

/// A filter's operator and what it is followed by.
enum class Op { None, Less, LessEqual, Greater, GreaterEqual, Equal, NotEqual };

Op takeOperator(QStringView &filter)
{
    static const std::pair<QStringView, Op> ops[] = {
            {u">=", Op::GreaterEqual}, {u"<=", Op::LessEqual}, {u"!=", Op::NotEqual}, {u"<>", Op::NotEqual},
            {u"≥", Op::GreaterEqual},  {u"≤", Op::LessEqual},  {u"≠", Op::NotEqual},  {u">", Op::Greater},
            {u"<", Op::Less},          {u"=", Op::Equal},
    };
    for (const auto &[text, op] : ops) {
        if (filter.startsWith(text)) {
            filter = filter.mid(text.size()).trimmed();
            return op;
        }
    }
    return Op::None;
}

bool compares(Op op, int c)
{
    switch (op) {
    case Op::Less: return c < 0;
    case Op::LessEqual: return c <= 0;
    case Op::Greater: return c > 0;
    case Op::GreaterEqual: return c >= 0;
    case Op::Equal: return c == 0;
    case Op::NotEqual: return c != 0;
    case Op::None: break;
    }
    return true;
}

template<typename T>
int threeWay(T a, T b)
{
    return a < b ? -1 : b < a ? 1 : 0;
}

} // namespace

TableSummaryRows tableSummaryRows(const TableModel &model)
{
    TableSummaryRows out;
    const qsizetype n = model.rows.size();
    if (n == 0)
        return out;
    const QStringList headers = normalizedHeaders(model);
    const auto reach = [&](qsizetype r) {
        FormulaReach all;
        for (const QString &cell : model.rows.at(r)) {
            const FormulaReach f = formulaReach(cell, headers);
            all.above = all.above || f.above;
            all.below = all.below || f.below;
        }
        return all;
    };
    qsizetype last = n;
    while (last > 0 && reach(last - 1).above)
        --last;
    qsizetype first = 0;
    while (first < last && reach(first).below)
        ++first;
    out.leading = int(first);
    out.trailing = int(n - last);
    return out;
}

QString tableCellValue(const TableModel &model, const TableEval &eval, int row, int column)
{
    if (row < 0 || row >= model.rows.size() || column < 0)
        return {};
    const QStringList &cells = model.rows.at(row);
    const QString cell = column < cells.size() ? cells.at(column) : QString();
    if (detail::isTableFormula(QStringView(cell).trimmed()) && row < eval.values.size()
        && column < eval.values.at(row).size() && !eval.values.at(row).at(column).isNull())
        return eval.values.at(row).at(column).trimmed();
    return plainCell(QStringView(cell).trimmed());
}

SortKind tableColumnKind(const TableModel &model, const TableEval &eval, int column, const TableSortOptions &options)
{
    if (column < 0 || column >= model.header.size())
        return SortKind::Text;
    const QStringList values = columnValues(model, eval, column);
    const TableSummaryRows summary = tableSummaryRows(model);
    return kindOf(values, summary.leading, values.size() - summary.trailing, options);
}

QList<int> tableSortOrder(const TableModel &model, const TableEval &eval, int column, SortOrder order,
                          const TableSortOptions &options)
{
    const qsizetype n = model.rows.size();
    if (column < 0 || column >= model.header.size() || n > kMaxSortRows)
        return {};
    QList<int> rows(n);
    std::iota(rows.begin(), rows.end(), 0);
    const TableSummaryRows summary = tableSummaryRows(model);
    const qsizetype from = summary.leading;
    const qsizetype to = n - summary.trailing;
    if (to - from < 2)
        return rows;
    const QStringList values = columnValues(model, eval, column);
    const SortKind kind = kindOf(values, from, to, options);

    // Keys once per row; empty cells last in both directions.
    struct Key {
        bool empty = true;
        double number = 0;
        qint64 date = 0;
        QString name; ///< text::nameSortKey()
    };
    QList<Key> keys(n);
    for (qsizetype r = from; r < to; ++r) {
        const QString &v = values.at(r);
        Key &k = keys[r];
        if (v.isEmpty())
            continue;
        k.empty = false;
        switch (kind) {
        case SortKind::Number: k.number = numberOf(v, options.locale).value_or(0); break;
        case SortKind::Date: k.date = dateOf(v, options).value_or(0); break;
        case SortKind::Text: k.name = text::nameSortKey(v); break;
        }
    }
    const bool descending = order == SortOrder::Descending;
    std::stable_sort(rows.begin() + from, rows.begin() + to, [&](int a, int b) {
        const Key &ka = keys.at(a);
        const Key &kb = keys.at(b);
        if (ka.empty || kb.empty)
            return !ka.empty && kb.empty;
        int c = 0;
        switch (kind) {
        case SortKind::Number: c = threeWay(ka.number, kb.number); break;
        case SortKind::Date: c = threeWay(ka.date, kb.date); break;
        case SortKind::Text: c = text::compareNameKeys(ka.name, values.at(a), kb.name, values.at(b)); break;
        }
        return descending ? c > 0 : c < 0;
    });
    return rows;
}

std::optional<QString> sortTableText(QStringView table, int column, SortOrder order, const TableSortOptions &options,
                                     const Scope &scope)
{
    const std::optional<TableModel> model = parseTable(table);
    if (!model || column < 0 || column >= model->header.size() || model->rows.size() > kMaxSortRows)
        return std::nullopt;
    const TableEval eval = evaluateTable(*model, options.locale, scope);
    const QList<int> sorted = tableSortOrder(*model, eval, column, order, options);
    TableModel moved = *model;
    if (sorted.isEmpty() || !permuteTableRows(moved, sorted))
        return std::nullopt;

    QList<text::Line> lines = text::splitLines(table);
    qsizetype header = 0;
    while (header < lines.size() && QStringView(lines.at(header).text).trimmed().isEmpty())
        ++header;
    const qsizetype body = header + 2;
    const qsizetype n = model->rows.size();
    if (body + n > lines.size())
        return std::nullopt; // (parseTable() read the same lines)

    // Every formula as it was (only the rows moved): the lines move as they are.
    bool formulasKept = true;
    for (qsizetype i = 0; i < n && formulasKept; ++i)
        formulasKept = moved.rows.at(i) == model->rows.at(sorted.at(i));
    if (formulasKept) {
        QStringList texts;
        texts.reserve(n);
        for (const int old : sorted)
            texts.append(lines.at(body + old).text);
        for (qsizetype i = 0; i < n; ++i)
            lines[body + i].text = texts.at(i);
        return text::joinLines(lines);
    }
    // The references followed their rows: the table written anew, in the text's line ending.
    const QString eol = lines.at(header).eol.isEmpty() ? u"\n"_s : lines.at(header).eol;
    QString out;
    for (qsizetype i = 0; i < header; ++i)
        out += lines.at(i).text + lines.at(i).eol;
    out += serializeTable(moved).replace(u'\n', eol);
    const qsizetype last = body + n - 1;
    out += lines.at(last).eol;
    for (qsizetype i = last + 1; i < lines.size(); ++i)
        out += lines.at(i).text + lines.at(i).eol;
    return out;
}

bool tableValueMatches(QStringView value, QStringView filter, const TableSortOptions &options)
{
    filter = filter.trimmed();
    if (filter.isEmpty())
        return true;
    value = value.trimmed();
    QStringView operand = filter;
    const Op op = takeOperator(operand);
    if (op == Op::None) {
        if (filter.startsWith(u'!') && filter.size() > 1) {
            const QString needle = text::foldKey(filter.mid(1).trimmed());
            return needle.isEmpty() || !text::foldKey(value).contains(needle);
        }
        return text::foldKey(value).contains(text::foldKey(filter));
    }
    if (operand.isEmpty()) {
        if (op == Op::Equal)
            return value.isEmpty();
        if (op == Op::NotEqual)
            return !value.isEmpty();
        return true; // an operator still being typed
    }
    if (const auto b = numberOf(operand, options.locale)) {
        if (const auto a = numberOf(value, options.locale))
            return compares(op, threeWay(*a, *b));
    }
    if (const auto b = dateOf(operand, options)) {
        if (const auto a = dateOf(value, options))
            return compares(op, threeWay(*a, *b));
        if (op != Op::Equal && op != Op::NotEqual)
            return false;
    } else if (numberOf(operand, options.locale) && op != Op::Equal && op != Op::NotEqual) {
        return false; // `> 100` on a cell that is no number
    }
    if (op == Op::Equal || op == Op::NotEqual)
        return (text::foldKey(value) == text::foldKey(operand)) == (op == Op::Equal);
    if (value.isEmpty())
        return false;
    return compares(op, text::compareNames(value.toString(), operand.toString()));
}

QList<bool> tableRowsShown(const TableModel &model, const TableEval &eval, const QStringList &filters,
                           const TableSortOptions &options)
{
    const qsizetype n = model.rows.size();
    QList<bool> shown(n, true);
    const qsizetype columns = std::min(filters.size(), model.header.size());
    bool any = false;
    for (qsizetype c = 0; c < columns; ++c)
        any = any || !QStringView(filters.at(c)).trimmed().isEmpty();
    if (!any)
        return shown;
    const TableSummaryRows summary = tableSummaryRows(model);
    for (qsizetype r = summary.leading; r < n - summary.trailing; ++r) {
        for (qsizetype c = 0; c < columns && shown.at(r); ++c) {
            if (!QStringView(filters.at(c)).trimmed().isEmpty())
                shown[r] = tableValueMatches(tableCellValue(model, eval, int(r), int(c)), filters.at(c), options);
        }
    }
    return shown;
}

} // namespace brulion::calc
