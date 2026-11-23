// A rectangle of cells of one Markdown table — copied as TSV or as a Markdown table, cleared, and
// filled from pasted text (docs/UI.md "Tables": the cell selection, Ctrl+C / Ctrl+X / Ctrl+V /
// Delete). Pure functions over calc::TableModel: the editor writes the model back with
// calc::replaceTable() / serializeTable(), so line endings and untouched text are kept. Part of the
// io module — see io/io.h.
//
// Copy: a formula cell is its result (calc::TableEval), as in the CSV export; other cells their
// text as written (Markdown kept), `<br>` a line break. TSV: one record per line, fields joined by
// tabs, a field with a tab, a quote or a line break quoted (`"` doubled) as spreadsheets do; one
// cell is just its text. The Markdown copy is a table of its own: the header of the columns and the
// rows (a selection that starts at the header row has it, one that does not still names its
// columns), with the alignments of the columns.
// Paste: the text is a Markdown table (its delimiter row left out), TSV (tabs), CSV (detected
// separator, several records) or lines/one value; read as RFC 4180 with the bounds of tablecsv.h;
// cells are trimmed, and in TSV/CSV a value starting with `=` is escaped (`\=`) so it stays text,
// as the CSV import does (a single value and a Markdown table are written as they are).
#pragma once

#include "calc/table.h"

#include <QList>
#include <QString>
#include <QStringList>
#include <QStringView>

namespace brulion::io {

/// A rectangle of a table as it is shown: `rows` are the body rows (indexes into
/// TableModel::rows, -1 the header row) in display order — hidden (filtered) rows are left out by
/// the caller —, `firstColumn`..`lastColumn` (inclusive) the columns.
struct TableCells {
    QList<int> rows;
    int firstColumn = 0;
    int lastColumn = 0;
};

/// The rectangle's cells as TSV (see above). Empty when the rectangle names no cell.
QString tableCellsToTsv(const calc::TableModel &model, const calc::TableEval &eval, const TableCells &cells);
/// The rectangle as a Markdown table (calc::serializeTable()). Empty when it names no cell.
QString tableCellsToMarkdown(const calc::TableModel &model, const calc::TableEval &eval, const TableCells &cells);

/// Pasted text as a grid of cells (the records, each with its fields); empty when the text is
/// empty. Bounded by io::MaxCsvRows (+1), MaxCsvColumns and MaxCsvCellLength; `truncated` says that
/// something was cut.
struct PastedCells {
    QList<QStringList> grid;
    bool truncated = false;
};
PastedCells parsePastedCells(QStringView text);

/// Where a paste goes: the top-left cell is (`rows.first()`, `column`); `rows` are the rows from
/// there down, in display order (-1, the header, only first). `selectedRows` x `selectedColumns` is
/// the selection the paste was made over (1 x 1 for a single cell).
struct PasteTarget {
    QList<int> rows;
    int column = 0;
    int selectedRows = 1;
    int selectedColumns = 1;
};
struct PasteResult {
    int cells = 0;       ///< cells whose text changed
    int rowsAdded = 0;   ///< rows appended below the last one for the rest of the grid
    int columnsCut = 0;  ///< columns of the grid that did not fit (tables get no columns from a paste)
    int rows = 0;        ///< the rectangle the paste filled: rows (those appended included) …
    int columns = 0;     ///< … and columns, from the top-left cell
    bool changed() const { return cells > 0 || rowsAdded > 0; }
};
/// Write `grid` into `model` from the target's top-left cell on. One value over a bigger selection
/// fills all of it. Rows the grid needs below the last row are appended (at most MaxCsvRows of
/// them); columns past the table's last one are left out.
PasteResult pasteTableCells(calc::TableModel &model, const PasteTarget &target, const QList<QStringList> &grid);
/// Empty the rectangle's cells; how many had text.
int clearTableCells(calc::TableModel &model, const TableCells &cells);

} // namespace brulion::io
