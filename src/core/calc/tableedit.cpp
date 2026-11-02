// Rows and columns added, deleted and moved, with the formulas' references following their cells
// (docs/SYNTAX.md §9). References are found by the formula parser itself: a [Header] named like a
// cell, a string or a function like LOG10 is never mistaken for one.
#include "calc/table.h"

#include "calc/evaluator_p.h"
#include "calc/lexer_p.h"
#include "calc/parser_p.h"
#include "calc/util_p.h"

#include <algorithm>
#include <climits>
#include <utility>

namespace brulion::calc {

namespace {

/// What one edit does to positions along its axis (rows or columns).
struct AxisEdit {
    enum class Kind { Insert, Remove, Move };
    Kind kind = Kind::Insert;
    int at = 0;
    int to = 0; ///< Move: where `at` goes

    /// Where position `v` goes; nullopt when it is deleted.
    std::optional<int> point(int v) const
    {
        switch (kind) {
        case Kind::Insert:
            return v >= at ? v + 1 : v;
        case Kind::Remove:
            if (v == at)
                return std::nullopt;
            return v > at ? v - 1 : v;
        case Kind::Move:
            if (v == at)
                return to;
            if (at < v && v <= to)
                return v - 1;
            if (to <= v && v < at)
                return v + 1;
            return v;
        }
        return v;
    }
    /// Where the positions a..b (a <= b) go; nullopt when all of them are deleted.
    std::optional<std::pair<int, int>> span(int a, int b) const
    {
        if (a == b) {
            const std::optional<int> p = point(a);
            return p ? std::optional(std::pair(*p, *p)) : std::nullopt;
        }
        switch (kind) {
        case Kind::Insert:
            return std::pair(a >= at ? a + 1 : a, b >= at ? b + 1 : b);
        case Kind::Remove:
            return std::pair(a > at ? a - 1 : a, b >= at ? b - 1 : b);
        case Kind::Move:
            return std::pair(a, b); // a range over several keeps its place
        }
        return std::pair(a, b);
    }
};

enum class Axis { Rows, Columns };

/// `ref` after the edit; empty when the edit does not change it.
QString rewrittenRef(const TableRef &ref, const AxisEdit &edit, Axis axis)
{
    const QString deleted = QStringLiteral("REF!");
    if (ref.kind == TableRef::Kind::Cell) {
        const int before = axis == Axis::Rows ? ref.row1 : ref.col1;
        const std::optional<int> p = edit.point(before);
        if (!p)
            return deleted;
        if (*p == before)
            return {};
        return axis == Axis::Rows ? cellName(*p, ref.col1) : cellName(ref.row1, *p);
    }
    if (ref.kind != TableRef::Kind::Range)
        return {};

    const bool wholeColumns = ref.row2 == INT_MAX;
    if (wholeColumns && axis == Axis::Rows)
        return {}; // B:B holds whatever rows there are
    int r1 = std::min(ref.row1, ref.row2);
    int r2 = std::max(ref.row1, ref.row2);
    int c1 = std::min(ref.col1, ref.col2);
    int c2 = std::max(ref.col1, ref.col2);
    const bool byRow = axis == Axis::Rows;
    const std::optional<std::pair<int, int>> moved = byRow ? edit.span(r1, r2) : edit.span(c1, c2);
    if (!moved)
        return deleted;
    if (*moved == (byRow ? std::pair(r1, r2) : std::pair(c1, c2)))
        return {};
    (byRow ? r1 : c1) = moved->first;
    (byRow ? r2 : c2) = moved->second;
    if (wholeColumns)
        return columnName(c1) + u':' + columnName(c2);
    return cellName(r1, c1) + u':' + cellName(r2, c2);
}

/// The formula `text` (a cell starting with `=`) after the edit; `text` itself when no reference
/// of it changes. The references a formula with a syntax error names up to the error are followed.
QString rewrittenFormula(const QString &text, const QStringList &headers, const AxisEdit &edit, Axis axis)
{
    const QStringView expression = QStringView(text).trimmed().mid(1);
    if (expression.size() > kMaxExpressionLength)
        return text;
    const QString source = detail::toNfc(expression);
    QList<Token> tokens = lex(source);
    normalizeNumbers(tokens, source, CalcLocale::Polish); // the references are no numbers
    ParseOptions options;
    options.statements = false;
    options.tableRefs = true;
    options.headers = headers;
    const ParsedProgram program = parseProgram(tokens, source, options);

    QString out = source;
    bool changed = false;
    for (qsizetype i = program.refs.size() - 1; i >= 0; --i) { // from the end: offsets stay right
        const TableRef &ref = program.refs.at(i);
        const QString replacement = rewrittenRef(ref, edit, axis);
        if (replacement.isEmpty())
            continue;
        out.replace(ref.pos, ref.length, replacement);
        changed = true;
    }
    return changed ? u'=' + out : text;
}

qsizetype columnCount(const TableModel &model)
{
    qsizetype columns = model.header.size();
    for (const QStringList &row : model.rows)
        columns = std::max(columns, row.size());
    return columns;
}

/// Every row and alignment as wide as the header, the header as wide as the widest row (a model
/// from anywhere; parseTable()'s already is).
void squared(TableModel &model)
{
    const qsizetype columns = columnCount(model);
    while (model.header.size() < columns)
        model.header.append(QString());
    while (model.aligns.size() < columns)
        model.aligns.append(TableAlign::None);
    for (QStringList &row : model.rows) {
        while (row.size() < columns)
            row.append(QString());
    }
}

void followReferences(TableModel &model, const AxisEdit &edit, Axis axis)
{
    QStringList headers;
    headers.reserve(model.header.size());
    for (const QString &header : model.header)
        headers.append(normalizeHeader(header));
    for (QStringList &row : model.rows) {
        for (QString &cell : row) {
            if (detail::isTableFormula(QStringView(cell).trimmed()))
                cell = rewrittenFormula(cell, headers, edit, axis);
        }
    }
}

} // namespace

bool insertTableRow(TableModel &model, int at)
{
    if (at < 0 || at > model.rows.size())
        return false;
    squared(model);
    followReferences(model, {AxisEdit::Kind::Insert, at}, Axis::Rows);
    model.rows.insert(at, QStringList(model.header.size(), QString()));
    return true;
}

bool removeTableRow(TableModel &model, int row)
{
    if (row < 0 || row >= model.rows.size())
        return false;
    squared(model);
    model.rows.removeAt(row);
    followReferences(model, {AxisEdit::Kind::Remove, row}, Axis::Rows);
    return true;
}

bool moveTableRow(TableModel &model, int from, int to)
{
    if (from < 0 || from >= model.rows.size() || to < 0 || to >= model.rows.size() || from == to)
        return false;
    squared(model);
    followReferences(model, {AxisEdit::Kind::Move, from, to}, Axis::Rows);
    model.rows.move(from, to);
    return true;
}

bool insertTableColumn(TableModel &model, int at, const QString &header)
{
    squared(model);
    if (at < 0 || at > model.header.size())
        return false;
    followReferences(model, {AxisEdit::Kind::Insert, at}, Axis::Columns);
    model.header.insert(at, header);
    model.aligns.insert(at, TableAlign::None);
    for (QStringList &row : model.rows)
        row.insert(at, QString());
    return true;
}

bool removeTableColumn(TableModel &model, int column)
{
    squared(model);
    if (column < 0 || column >= model.header.size() || model.header.size() < 2)
        return false;
    // The headers of before: a formula's [Header] is read before its column goes.
    followReferences(model, {AxisEdit::Kind::Remove, column}, Axis::Columns);
    model.header.removeAt(column);
    model.aligns.removeAt(column);
    for (QStringList &row : model.rows)
        row.removeAt(column);
    return true;
}

bool moveTableColumn(TableModel &model, int from, int to)
{
    squared(model);
    const qsizetype columns = model.header.size();
    if (from < 0 || from >= columns || to < 0 || to >= columns || from == to)
        return false;
    followReferences(model, {AxisEdit::Kind::Move, from, to}, Axis::Columns);
    model.header.move(from, to);
    model.aligns.move(from, to);
    for (QStringList &row : model.rows)
        row.move(from, to);
    return true;
}

} // namespace brulion::calc
