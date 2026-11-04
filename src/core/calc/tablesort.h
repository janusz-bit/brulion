// Sorting and filtering GFM tables (docs/SYNTAX.md §9 "Sorting and filtering tables"). Part of
// the calc module — see calc/calc.h for the overview.
//
// A column compares by what every non-empty cell of it is: numbers as people write them
// (parseLocaleNumber: "12,50 zł", "23%", "1 200"), else dates in the forms Brulion reads elsewhere
// ("2026-10-15", "15.10.2026", "15 października 2026", "[[2026-10-15]]", with a time too), else
// text in Polish alphabetical order (text::compareNames: case-insensitive, ą after a, ł after l,
// digit runs by value — the same on every platform, the browser included, where Qt has no ICU
// collation). A formula cell compares by its result. Empty cells go last in both directions; the
// sort is stable. Summary rows stay where they are: rows at the end of the table whose formulas
// add up the column (`above`/`powyżej`, a range of several rows, a whole column `B:B`) and rows at
// its start reading `below`/`poniżej` (tableSummaryRows()).
//
// sortTableText() is the content edit: the table's text in, the sorted text out. When no formula
// names a moved row by its A1 address, every row keeps its line byte for byte (padding, escaped
// pipes, the delimiter row and the line endings stay); otherwise the A1 references follow their
// rows (as moveTableRow() does) and the table is written anew by serializeTable().
//
// Filters only decide which rows are shown (nothing is written): tableValueMatches() reads one
// filter text, tableRowsShown() applies one filter per column (all must match).
#pragma once

#include "calc/calclocale.h"
#include "calc/table.h"
#include "calc/value.h"

#include <QDate>
#include <QList>
#include <QString>
#include <QStringList>
#include <QStringView>

#include <optional>

namespace brulion::calc {

enum class SortOrder { Ascending, Descending };

/// How the cells of a column compare (tableColumnKind()).
enum class SortKind { Number, Date, Text };

struct TableSortOptions {
    /// Numbers ("12,50" in Polish, "12.50" in English) and day-month order of dates ("10/11").
    CalcLocale locale = CalcLocale::Polish;
    /// Dates written without a year ("15 paź", "15.10") are their next occurrence after this day,
    /// as everywhere in Brulion; invalid: such cells are no dates.
    QDate today;
};

/// Bounds (tables come from anywhere): a longer table is not sorted.
inline constexpr int kMaxSortRows = 20000;

/// Rows the sort leaves in place: `leading` rows at the start of the body and `trailing` rows at
/// its end that summarise the others (see the file comment). leading + trailing <= rows.
struct TableSummaryRows {
    int leading = 0;
    int trailing = 0;
};
TableSummaryRows tableSummaryRows(const TableModel &model);

/// What a cell shows, for comparing: a formula's result (`eval`, when it has one for the cell),
/// else the cell's text without light Markdown (`**x**`, `[[Strona|alias]]` -> alias,
/// `[tekst](url)` -> tekst, `<br>` -> space), trimmed.
QString tableCellValue(const TableModel &model, const TableEval &eval, int row, int column);

/// The kind of column `column` (Text when it has no non-empty cell, or when out of range).
SortKind tableColumnKind(const TableModel &model, const TableEval &eval, int column, const TableSortOptions &options);

/// The rows in sorted order: element i is the old index of the row that goes to position i
/// (summary rows keep theirs). Empty when `column` is out of range or the table is too long.
QList<int> tableSortOrder(const TableModel &model, const TableEval &eval, int column, SortOrder order,
                          const TableSortOptions &options);

/// The table text `table` (a table block's text, parseTable() reads it) with its rows sorted by
/// `column` (see the file comment); `scope` gives the formulas their variables (pageScope()).
/// nullopt when `table` is no table, the column is out of range, the table is too long or the rows
/// are in that order already.
std::optional<QString> sortTableText(QStringView table, int column, SortOrder order, const TableSortOptions &options,
                                     const Scope &scope = Scope());

/// Whether a cell showing `value` passes the filter `filter` (docs/SYNTAX.md §9):
/// * empty: every cell;
/// * `> 100`, `>= 2026-01-01`, `< 5`, `<=`, `=`, `!=` (also `<>`, `≥`, `≤`, `≠`): numbers and dates
///   compare by value (both sides must be one), `=` / `!=` text as case- and diacritic-insensitive
///   equality, `<` / `>` text alphabetically; `=` alone: empty cells, `!=` alone: cells with text;
/// * `!tekst`: cells not containing it;
/// * any other text: cells containing it (case- and diacritic-insensitive: `zolw` finds `Żółw`).
bool tableValueMatches(QStringView value, QStringView filter, const TableSortOptions &options);

/// Which rows of `model` show under `filters` (one per column, "" = none; missing entries are
/// none): a row shows when every filter matches its cell. Summary rows (tableSummaryRows()) always
/// show. Indexed like model.rows.
QList<bool> tableRowsShown(const TableModel &model, const TableEval &eval, const QStringList &filters,
                           const TableSortOptions &options);

} // namespace brulion::calc
