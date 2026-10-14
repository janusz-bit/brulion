// GFM tables: parsing, pretty serialisation, locating tables in a page and evaluating table
// formulas (docs/SYNTAX.md §9). Part of the calc module — see calc/calc.h for the overview.
#pragma once

#include "calc/calclocale.h"
#include "calc/value.h"

#include <QList>
#include <QString>
#include <QStringList>
#include <QStringView>

#include <optional>

namespace brulion::calc {

/// Column alignment from the delimiter row: `---`, `:--`, `:-:`, `--:`.
enum class TableAlign { None, Left, Center, Right };

/// A parsed GFM table. Cells are trimmed and unescaped (`\|` -> `|`).
struct TableModel {
    QStringList header;
    QList<TableAlign> aligns;
    QList<QStringList> rows;

    friend bool operator==(const TableModel &a, const TableModel &b)
    {
        return a.header == b.header && a.aligns == b.aligns && a.rows == b.rows;
    }
};

/// Where a table is in a document. `content.mid(from, to - from)` is the table text (parseable
/// with parseTable()); replaceTable() writes an edited model back in its place.
struct TableSpan {
    int startLine = 0; ///< 0-based line of the header row
    int endLine = 0;   ///< line of the last row (the delimiter row for a table without body)
    qsizetype from = 0; ///< offset of the header row's first character (after indentation and
                        ///< a list marker, for Logseq-style `- | a | b |`)
    qsizetype to = 0;   ///< offset just past the last row (before its line break)
    QString indent;     ///< leading whitespace of the rows after the header ("  " in a list item)

    friend bool operator==(const TableSpan &a, const TableSpan &b)
    {
        return a.startLine == b.startLine && a.endLine == b.endLine && a.from == b.from && a.to == b.to
               && a.indent == b.indent;
    }
};

/// Cells of a table row: trimmed, outer pipes optional, `\|` unescaped.
QStringList splitTableRow(QStringView line);
/// Alignments of a delimiter row, or nullopt when the line is not one.
std::optional<QList<TableAlign>> parseDelimiterRow(QStringView line);
/// Parse a GFM table: header row with '|', delimiter row with as many cells, body rows until a
/// blank line (leading blank lines are skipped). Short rows are padded with empty cells, long
/// rows truncated to the header width. nullopt when the text does not start with a table.
std::optional<TableModel> parseTable(QStringView markdown);
/// Pretty GFM: outer pipes, columns padded to equal display width (at least 3), alignment
/// colons, pipes escaped, line breaks in cells as `<br>`; rows longer than the header add
/// columns. Lines joined by '\n'; empty string for a table without columns.
QString serializeTable(const TableModel &model);
/// Tables outside fenced code and frontmatter (also indented in list items and starting on a
/// list item line); offsets are UTF-16 offsets into `content`.
QList<TableSpan> findTables(QStringView content);
/// `content` with the table at `span` (from findTables(content)) replaced by
/// serializeTable(model): rows after the first are indented with `span.indent` and joined with
/// the line ending the table already uses (CRLF notes stay CRLF). Other text is untouched.
QString replaceTable(QStringView content, const TableSpan &span, const TableModel &model);
/// Approximate monospace display width: CJK and emoji count 2, combining marks 0.
int displayWidth(QStringView text);

/// Shown for a formula cell whose evaluation failed (the message is in TableEval::errors).
inline constexpr QStringView kErrorMarker = u"#ERR";
/// Shown for a formula cell that is part of a reference cycle.
inline constexpr QStringView kCycleMarker = u"#CYCLE";

/// Results of evaluateTable(), indexed like TableModel::rows: `values[row][column]`.
/// Cells that are not formulas hold null strings (QString::isNull()).
struct TableEval {
    QList<QStringList> values; ///< formatted result, "#ERR" or "#CYCLE" for formula cells
    QList<QStringList> errors; ///< translated error message for failed formula cells
};

/// Evaluate the body cells starting with '=' (`=sum(above)`, `=[Cena]*[Ilość]`, `=B2*C2`).
/// References: A1 (column letter + data row, row 1 = first row under the header), ranges A1:B3,
/// whole columns B:B, [Header] (current row, case-insensitive), above/below/left/right
/// (powyżej/poniżej). Plain cells are read with parseLocaleNumber ("12,50 zł", "23%"); empty
/// cells count as 0, ranges skip text. Formulas are evaluated in dependency order; cycles give
/// "#CYCLE", other failures "#ERR". `scope` (e.g. pageScope()) provides variables. Work is
/// bounded (huge tables end in "too complex" errors). Never throws.
TableEval evaluateTable(const TableModel &model, CalcLocale locale = CalcLocale::Polish, const Scope &scope = Scope());

/// 0 -> "A", 25 -> "Z", 26 -> "AA".
QString columnName(int index);
/// "A" -> 0, "AA" -> 26 (case-insensitive ASCII letters); -1 for an empty string, any other
/// character, or an index that does not fit an int.
int columnIndex(QStringView letters);
/// Spreadsheet name of a body cell: (row 0, column 1) -> "B1".
QString cellName(int row, int column);

} // namespace brulion::calc
