// A chart's numbers from its page (docs/SYNTAX.md §9 "Charts"): a table, a calc block or a query's
// groups; and its text alternative.
#include "chart/chart.h"
#include "chart/messages_p.h"

#include "calc/calc.h"
#include "markdown/blockclipboard.h"
#include "text/text.h"

#include <QRegularExpression>

#include <algorithm>
#include <cmath>
#include <limits>

using namespace Qt::StringLiterals;

namespace brulion::chart {

namespace {

constexpr double Nan = std::numeric_limits<double>::quiet_NaN();

QString capLabel(const QString &text)
{
    QString t = text.simplified();
    if (t.size() > limits::MaxLabelLength)
        t = detail::cutAt(t, limits::MaxLabelLength - 1).toString() + u'…';
    return t;
}

/// A table's text as a label (a header, a cell): what it shows — its inline Markdown reduced as
/// "Copy as plain text" does (markdown::stripInlineMarkup: links their labels, emphasis and code
/// marks and a highlight's `<mark>` tags gone, tags and dates kept), a line break `<br>` a space —
/// capped. (calc::tableCellValue() leaves out marks around the whole value only: "**Płytki** z
/// Castorama" kept its asterisks.)
QString plainLabel(const QString &text)
{
    static const QRegularExpression br(u"<br\\s*/?>"_s, QRegularExpression::CaseInsensitiveOption);
    QString t = text;
    if (t.contains(u'<'))
        t.replace(br, u" "_s);
    return capLabel(markdown::stripInlineMarkup(t));
}

/// The label of a table cell: a formula's result, else the cell's text as it shows.
QString cellLabel(const calc::TableModel &model, const calc::TableEval &eval, int row, int column)
{
    const QStringList &cells = model.rows.at(row);
    const QString cell = column < cells.size() ? cells.at(column).trimmed() : QString();
    if (cell.startsWith(u'='))
        return capLabel(calc::tableCellValue(model, eval, row, column));
    return plainLabel(cell);
}

/// A value of a chart: a finite number not beyond limits::MaxMagnitude, else NaN (a gap).
double valueOf(QStringView text, calc::CalcLocale locale)
{
    if (text.isEmpty() || text.size() > 200)
        return Nan;
    const auto n = calc::parseLocaleNumber(text, locale);
    if (!n || !std::isfinite(n->value) || std::abs(n->value) > limits::MaxMagnitude)
        return Nan;
    return n->value;
}

double clean(double v)
{
    return std::isfinite(v) && std::abs(v) <= limits::MaxMagnitude ? v : Nan;
}

/// The column named `name`: a header (case- and diacritic-insensitive, light Markdown left out),
/// else column letters ("B"); -1 when none.
int findColumn(const calc::TableModel &model, const QString &name)
{
    const QString key = text::foldKey(name);
    for (qsizetype c = 0; c < model.header.size(); ++c) {
        if (text::foldKey(calc::stripInlineMarkup(model.header.at(c))) == key)
            return int(c);
    }
    const int letters = calc::columnIndex(name.trimmed());
    return letters >= 0 && letters < model.header.size() ? letters : -1;
}

/// The calc variables of the calc blocks above `line` (PageSources::scopeAt unset).
calc::Scope scopeAbove(const PageSources &page, int line)
{
    if (page.scopeAt)
        return page.scopeAt(line);
    calc::Scope scope;
    for (const PageSources::Calc &c : page.calcs) {
        if (c.line < line)
            calc::evaluateCalc(c.source, scope, page.locale);
    }
    return scope;
}

/// `index` among `items` (0: the last one above `chartLine`), or nullptr.
template<typename T>
const T *pick(const QList<T> &items, int index, int chartLine)
{
    if (index > 0)
        return index <= items.size() ? &items.at(index - 1) : nullptr;
    const T *found = nullptr;
    for (const T &item : items) {
        if (item.line < chartLine)
            found = &item;
    }
    return found;
}

DataResult fail(Error::Code code, int line, QString detail = {})
{
    DataResult out;
    out.error = Error{code, line, std::move(detail)};
    return out;
}

DataResult fromTable(const Spec &spec, const PageSources &page)
{
    const PageSources::Table *table = pick(page.tables, spec.sourceIndex, page.chartLine);
    const std::optional<calc::TableModel> model = table ? calc::parseTable(table->text) : std::nullopt;
    if (!model || model->header.isEmpty())
        return fail(Error::Code::NoTable, spec.sourceLine,
                    spec.sourceIndex > 0 ? QString::number(spec.sourceIndex) : QString());
    const calc::TableSummaryRows summary = calc::tableSummaryRows(*model);
    const int first = summary.leading;
    const int last = int(model->rows.size()) - summary.trailing; // exclusive
    if (last - first > limits::MaxPoints)
        return fail(Error::Code::TooLarge, 0, u"points"_s);
    const bool formulas = std::ranges::any_of(model->rows, [](const QStringList &row) {
        return std::ranges::any_of(row, [](const QString &c) { return c.trimmed().startsWith(u'='); });
    });
    const calc::TableEval eval =
            calc::evaluateTable(*model, page.locale, formulas ? scopeAbove(page, table->line) : calc::Scope());
    const int columns = int(model->header.size());

    // The labels' column: `x`, else the first one (none when the table has only one column).
    int xColumn = columns > 1 ? 0 : -1;
    if (!spec.x.isEmpty()) {
        xColumn = findColumn(*model, spec.x);
        if (xColumn < 0)
            return fail(Error::Code::UnknownColumn, spec.xLine, spec.x);
    }
    QList<int> yColumns;
    if (!spec.y.isEmpty()) {
        for (const QString &name : spec.y) {
            const int c = findColumn(*model, name);
            if (c < 0)
                return fail(Error::Code::UnknownColumn, spec.yLine, name);
            if (!yColumns.contains(c))
                yColumns.append(c);
        }
        if (yColumns.size() > limits::MaxSeries)
            return fail(Error::Code::TooLarge, spec.yLine, u"series"_s);
    } else {
        // Every other column whose filled cells are mostly numbers.
        for (int c = 0; c < columns && yColumns.size() < limits::MaxSeries; ++c) {
            if (c == xColumn)
                continue;
            int filled = 0, numbers = 0;
            for (int r = first; r < last; ++r) {
                const QString v = calc::tableCellValue(*model, eval, r, c);
                if (v.isEmpty())
                    continue;
                ++filled;
                if (!std::isnan(valueOf(v, page.locale)))
                    ++numbers;
            }
            if (numbers > 0 && numbers * 2 >= filled)
                yColumns.append(c);
        }
    }
    if (yColumns.isEmpty())
        return fail(Error::Code::NoValues, 0);

    DataResult out;
    if (xColumn >= 0)
        out.data.labelsName = plainLabel(model->header.at(xColumn));
    for (int r = first; r < last; ++r)
        out.data.labels.append(xColumn >= 0 ? cellLabel(*model, eval, r, xColumn) : QString::number(r - first + 1));
    bool any = false;
    for (const int c : std::as_const(yColumns)) {
        Series s;
        s.name = plainLabel(model->header.at(c));
        for (int r = first; r < last; ++r) {
            s.values.append(valueOf(calc::tableCellValue(*model, eval, r, c), page.locale));
            any = any || !std::isnan(s.values.last());
        }
        out.data.series.append(std::move(s));
    }
    if (!any)
        return fail(Error::Code::NoValues, 0);
    return out;
}

bool isAggregateLine(QStringView input)
{
    static const QStringList words = {u"sum"_s, u"total"_s,   u"suma"_s, u"razem"_s,
                                      u"avg"_s, u"average"_s, u"mean"_s, u"srednia"_s};
    return words.contains(text::foldKey(input.trimmed()));
}

/// The text of a calc line without its comment, for a label.
QString lineLabel(const QString &input)
{
    QString t = input;
    for (const QStringView mark : {QStringView(u"//"), QStringView(u"#")}) {
        const qsizetype at = t.indexOf(mark);
        if (at > 0)
            t.truncate(at);
    }
    return t.trimmed();
}

DataResult fromCalc(const Spec &spec, const PageSources &page)
{
    const PageSources::Calc *block = pick(page.calcs, spec.sourceIndex, page.chartLine);
    if (!block)
        return fail(Error::Code::NoCalc, spec.sourceLine,
                    spec.sourceIndex > 0 ? QString::number(spec.sourceIndex) : QString());
    calc::Scope scope = scopeAbove(page, block->line);
    const QList<calc::CalcLineResult> results = calc::evaluateCalc(block->source, scope, page.locale);
    DataResult out;
    Series s;
    for (const calc::CalcLineResult &r : results) {
        if (r.hasError() || !r.numeric || isAggregateLine(r.input))
            continue;
        const double v = clean(*r.numeric);
        if (std::isnan(v))
            continue;
        if (out.data.labels.size() >= limits::MaxPoints)
            return fail(Error::Code::TooLarge, 0, u"points"_s);
        const QString label = !r.label.isEmpty() ? r.label : !r.assigned.isEmpty() ? r.assigned : lineLabel(r.input);
        out.data.labels.append(capLabel(label));
        s.values.append(v);
    }
    if (s.values.isEmpty())
        return fail(Error::Code::NoValues, 0);
    out.data.series.append(std::move(s));
    return out;
}

DataResult fromQuery(const Spec &spec, const PageSources &page)
{
    if (!page.runQuery)
        return fail(Error::Code::QueryUnavailable, spec.sourceLine);
    const QueryCounts counts = page.runQuery(spec.query);
    if (!counts.error.isEmpty())
        return fail(Error::Code::QueryFailed, spec.sourceLine, counts.error);
    if (counts.groups.size() > limits::MaxPoints)
        return fail(Error::Code::TooLarge, 0, u"points"_s);
    DataResult out;
    Series s;
    for (const auto &[label, value] : counts.groups) {
        out.data.labels.append(capLabel(label));
        s.values.append(clean(value));
    }
    if (std::ranges::all_of(s.values, [](double v) { return std::isnan(v); }))
        return fail(Error::Code::NoValues, 0);
    out.data.series.append(std::move(s));
    return out;
}

} // namespace

PageSources pageSources(QStringView content, int chartLine, calc::CalcLocale locale)
{
    PageSources page;
    page.chartLine = chartLine;
    page.locale = locale;
    for (const auto spans = calc::findTables(content); const calc::TableSpan &span : spans)
        page.tables.append({span.startLine, content.mid(span.from, span.to - span.from).toString()});
    for (const auto blocks = calc::findCalcBlocks(content); const calc::CalcBlock &block : blocks)
        page.calcs.append({block.startLine, block.source});
    page.scopeAt = [text = content.toString(), locale](int line) { return calc::pageScope(text, line, locale); };
    return page;
}

DataResult resolveData(const Spec &spec, const PageSources &page)
{
    DataResult out;
    switch (spec.source) {
    case SourceKind::Table: out = fromTable(spec, page); break;
    case SourceKind::Calc: out = fromCalc(spec, page); break;
    case SourceKind::Query: out = fromQuery(spec, page); break;
    }
    if (out.error || spec.type != Type::Pie)
        return out;
    // A pie draws the first series' values above zero.
    const QList<double> &values = out.data.series.constFirst().values;
    const auto positive = std::ranges::count_if(values, [](double v) { return v > 0; });
    if (positive == 0)
        return fail(Error::Code::NoPositiveValues, 0);
    if (positive > limits::MaxSlices)
        return fail(Error::Code::TooLarge, 0, u"slices"_s);
    return out;
}

QString describeChart(const Spec &spec, const Data &data, calc::CalcLocale locale, qsizetype maxLength)
{
    using detail::ChartMessages;
    QString out = spec.title.isEmpty() ? typeName(spec.type)
                                       : ChartMessages::tr("%1 “%2”").arg(typeName(spec.type), spec.title);
    const auto number = [&](double v) {
        return std::isnan(v) ? ChartMessages::tr("no value") : calc::formatNumber(v, locale);
    };
    const bool pie = spec.type == Type::Pie && !data.series.isEmpty();
    double total = 0;
    if (pie) {
        for (const double v : data.series.front().values)
            total += v > 0 ? v : 0;
    }
    QStringList points;
    QStringList values; // of one point, with several series
    qsizetype length = out.size();
    for (qsizetype i = 0; i < data.labels.size() && length < maxLength; ++i) {
        QString point;
        if (pie) {
            const double v = data.series.front().values.value(i, Nan);
            if (!(v > 0))
                continue;
            point = ChartMessages::tr("%1: %2 (%3%)")
                            .arg(data.labels.at(i), number(v),
                                 calc::formatNumber(std::round(v / total * 1000) / 10, locale));
        } else if (data.series.size() == 1) {
            point = ChartMessages::tr("%1: %2").arg(data.labels.at(i),
                                                    number(data.series.front().values.value(i, Nan)));
        } else {
            values.clear();
            for (const Series &s : data.series)
                values.append(ChartMessages::tr("%1 %2").arg(s.name, number(s.values.value(i, Nan))));
            point = ChartMessages::tr("%1: %2").arg(data.labels.at(i), values.join(u", "_s));
        }
        length += point.size() + 2;
        points.append(point);
    }
    if (!points.isEmpty())
        out += u": "_s + points.join(u"; "_s);
    if (out.size() > maxLength)
        return out.left(std::max<qsizetype>(1, maxLength - 1)) + u'…';
    return out + u'.';
}

Result render(QStringView source, const PageSources &page, const LayoutOptions &options)
{
    Result out;
    SpecResult spec = parseSpec(source);
    out.spec = std::move(spec.spec);
    out.error = std::move(spec.error);
    if (out.error)
        return out;
    DataResult data = resolveData(out.spec, page);
    if (data.error) {
        out.error = std::move(data.error);
        return out;
    }
    out.data = std::move(data.data);
    out.scene = layoutChart(out.spec, out.data, options);
    return out;
}

} // namespace brulion::chart
