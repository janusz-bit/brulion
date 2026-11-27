// A table block as PageView draws it (docs/ATRAMENT.md M2 step 5, tables; tablerows.cpp): the
// model's `tableData` role (BlockListModel: header, its rendering, alignments, rows of cells with
// their Markdown, what they show, their rendering, formula results and errors, the filters' rows)
// read once per change into plain values, and the columns' widths by TableBlockView's rule
// (BlockViews::tableColumnWidths: the widest line of the header in the header's font and of each
// cell's visible text in the cells' font, plus the padding on both sides, 56…280 px), with the
// text measured by Atrament's text layout instead of QFontMetrics. Pure: no Atrament types.
#pragma once

#include <QString>
#include <QList>
#include <QStringList>
#include <QVariantMap>

#include <functional>
#include <vector>

namespace brulion::editor::atr {

struct TableGridCell {
    QString text;    // the cell's Markdown (a formula's "=…"): what is edited
    QString display; // what it shows (a formula's result; a header's lines joined by spaces)
    QString html;    // its rendering (InlineRenderer; formula results and errors escaped)
    QString plain;   // its visible text (the column's width)
    QString error;   // a formula's error ("": none)
    bool formula = false;
};

enum class TableAlign { Default, Left, Center, Right };

struct TableGrid {
    std::vector<TableGridCell> header;
    std::vector<TableAlign> aligns;              // per column
    std::vector<std::vector<TableGridCell>> rows; // each as wide as the header
    std::vector<bool> shown;                     // per row: passes the filters
    int headerLines = 1;                         // a header's `<br>` lines (1…3)
    bool filtering = false;                      // the filter row is shown (the QML view's)

    int columnCount() const { return int(header.size()); }
    int rowCount() const { return int(rows.size()); }
    // Cell (r, c), r −1 the header; nullptr outside the table.
    const TableGridCell *cell(int r, int c) const;
    TableAlign align(int c) const;
    bool rowShown(int r) const;
    // The rows shown, in order: those passing the filters, and row `editRow`.
    std::vector<int> shownRows(int editRow) const;
    // The shown row after (`step` 1) or before (−1) row `r` (−1 the header): rowCount() after the
    // last, −1 before the first (BlockViews::tableNextShownRow).
    int nextShownRow(int r, int step) const;
    // The rows shown from row `r` down to the last one (`r` itself whatever the filters say):
    // a paste's rows (BlockViews::tableRowsFrom).
    QList<int> rowsFrom(int r) const;
};

// The `tableData` role as a grid; an empty grid for anything else.
TableGrid readTableGrid(const QVariantMap &tableData);

// How wide `text` is in the header's font (`header`) or the cells' (logical px).
using TableMeasure = std::function<float(QStringView text, bool header)>;

// The natural width of each column (TableBlockView's columnWidths before they are stretched to
// fill the table's width: tablerows.cpp stretches them by flex-grow in proportion to them).
std::vector<float> tableColumnWidths(const TableGrid &grid, const TableMeasure &measure, float padding);

// The cell's text is shown right-aligned (`--:`, or a formula in a column without alignment),
// centred (`:-:`) or left (TableBlockView's horizontalAlignment).
TableAlign shownAlign(const TableGrid &grid, int r, int c);

} // namespace brulion::editor::atr
