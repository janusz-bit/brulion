// Rows and columns added, deleted and moved, with the formulas' references following their cells
// (docs/SYNTAX.md §9). References are found by the formula parser itself: a [Header] named like a
// cell, a string or a function like LOG10 is never mistaken for one.
#include "calc/table.h"

#include "calc/evaluator_p.h"
#include "calc/parser_p.h"
#include "calc/util_p.h"

#include <algorithm>
#include <climits>
#include <ranges>
#include <tuple>
#include <utility>

namespace brulion::calc {

namespace {

/// What one edit does to positions along its axis (rows or columns).
struct AxisEdit {
    enum class Kind { Insert, Remove, Move, Permute };
    Kind kind = Kind::Insert;
    int at = 0;
    int to = 0; ///< Move: where `at` goes
    /// Permute: the new position of each old one (positions past its end stay).
    const QList<int> *newPositions = nullptr;

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
        case Kind::Permute: return v >= 0 && v < newPositions->size() ? newPositions->at(v) : v;
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
        case Kind::Permute: return std::pair(a, b); // a range over several keeps its place
        }
        return std::pair(a, b);
    }
};

enum class Axis { Rows, Columns };

/// `ref` after the edit; empty when the edit does not change it.
QString rewrittenRef(const TableRef &ref, const AxisEdit &edit, Axis axis)
{
    const bool cell = ref.kind == TableRef::Kind::Cell; // a range of one: row1 == row2, col1 == col2
    if (!cell && ref.kind != TableRef::Kind::Range)
        return {};
    const bool wholeColumns = !cell && ref.row2 == INT_MAX;
    if (wholeColumns && axis == Axis::Rows)
        return {}; // B:B holds whatever rows there are
    int r1 = std::min(ref.row1, ref.row2);
    int r2 = std::max(ref.row1, ref.row2);
    int c1 = std::min(ref.col1, ref.col2);
    int c2 = std::max(ref.col1, ref.col2);
    int &first = axis == Axis::Rows ? r1 : c1;
    int &last = axis == Axis::Rows ? r2 : c2;
    const std::optional<std::pair<int, int>> moved = edit.span(first, last);
    if (!moved)
        return QStringLiteral("REF!");
    if (*moved == std::pair(first, last))
        return {};
    std::tie(first, last) = *moved;
    if (cell)
        return cellName(r1, c1);
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
    // The references are no numbers: any locale reads them alike.
    const ParsedProgram program = detail::parseTableFormula(source, headers, CalcLocale::Polish);
    QString out = source;
    bool changed = false;
    for (const TableRef &ref : std::views::reverse(program.refs)) { // from the end: offsets stay right
        const QString replacement = rewrittenRef(ref, edit, axis);
        if (replacement.isEmpty())
            continue;
        out.replace(ref.pos, ref.length, replacement);
        changed = true;
    }
    return changed ? u'=' + out : text;
}

/// Every row and alignment as wide as the header, the header as wide as the widest row (a model
/// from anywhere; parseTable()'s already is).
void squared(TableModel &model)
{
    const qsizetype columns = detail::tableColumnCount(model);
    if (model.header.size() < columns)
        model.header.resize(columns);
    if (model.aligns.size() < columns)
        model.aligns.resize(columns, TableAlign::None);
    for (QStringList &row : model.rows) {
        if (row.size() < columns)
            row.resize(columns);
    }
}

void followReferences(TableModel &model, const AxisEdit &edit, Axis axis)
{
    const QStringList headers = detail::normalizedHeaders(model.header);
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
    model.rows.insert(at, QStringList(model.header.size()));
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

bool permuteTableRows(TableModel &model, const QList<int> &order)
{
    const qsizetype n = model.rows.size();
    if (order.size() != n)
        return false;
    QList<int> newPositions(n, -1);
    bool changed = false;
    for (qsizetype i = 0; i < n; ++i) {
        const int old = order.at(i);
        if (old < 0 || old >= n || newPositions.at(old) >= 0)
            return false;
        newPositions[old] = int(i);
        changed = changed || old != i;
    }
    if (!changed)
        return false;
    squared(model);
    followReferences(model, {.kind = AxisEdit::Kind::Permute, .newPositions = &newPositions}, Axis::Rows);
    QList<QStringList> rows;
    rows.reserve(n);
    for (const int old : order)
        rows.append(std::move(model.rows[old]));
    model.rows = std::move(rows);
    return true;
}

bool insertTableColumn(TableModel &model, int at, const QString &header)
{
    if (at < 0 || at > detail::tableColumnCount(model))
        return false;
    squared(model);
    followReferences(model, {AxisEdit::Kind::Insert, at}, Axis::Columns);
    model.header.insert(at, header);
    model.aligns.insert(at, TableAlign::None);
    for (QStringList &row : model.rows)
        row.insert(at, QString());
    return true;
}

bool removeTableColumn(TableModel &model, int column)
{
    const qsizetype columns = detail::tableColumnCount(model);
    if (column < 0 || column >= columns || columns < 2)
        return false;
    squared(model);
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
    const qsizetype columns = detail::tableColumnCount(model);
    if (from < 0 || from >= columns || to < 0 || to >= columns || from == to)
        return false;
    squared(model);
    followReferences(model, {AxisEdit::Kind::Move, from, to}, Axis::Columns);
    model.header.move(from, to);
    model.aligns.move(from, to);
    for (QStringList &row : model.rows)
        row.move(from, to);
    return true;
}

} // namespace brulion::calc
