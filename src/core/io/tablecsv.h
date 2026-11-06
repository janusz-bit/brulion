// One Markdown table as CSV and back (docs/SYNTAX.md §9 "CSV"): the table menu's "Copy as CSV" /
// "Save as CSV…" and the editor's "/Table from CSV…". Part of the io module — see io/io.h.
//
// Export: RFC 4180 with `,`, CRLF records, the header row first; a formula cell is its result
// (TableEval), other cells their text as written (Markdown kept, `<br>` a line break, `\|` a
// pipe); values a spreadsheet would run as a formula get an apostrophe (csvSafeText).
// Import: the separator is detected (`,`, `;` or tab — Excel's `sep=;` first line too), quotes,
// doubled quotes, line breaks in quotes, CRLF/LF/CR and a UTF-8 BOM are read as RFC 4180 says; the
// first record is the header. Cells go into the table as text: a value starting with `=` is
// escaped (`\=`) so it stays text, pipes and line breaks are escaped by serializeTable(). Bounded:
// at most MaxCsvRows rows, MaxCsvColumns columns and MaxCsvCellLength characters a cell (cut with
// "…"); more is dropped and reported (`truncated`).
#pragma once

#include "calc/table.h"

#include <QChar>
#include <QString>
#include <QStringView>

#include <optional>

namespace brulion::io {

inline constexpr int MaxCsvRows = 2000;
inline constexpr int MaxCsvColumns = 64;
inline constexpr int MaxCsvCellLength = 10000;
/// Larger texts are not read at all (UTF-16 code units).
inline constexpr qsizetype MaxCsvTextLength = 16 * 1024 * 1024;

/// The CSV of a table: `eval` gives formula cells their results (cells it has no result for are
/// written as they are). Records end with CRLF.
QString tableToCsv(const calc::TableModel &model, const calc::TableEval &eval, QChar separator = u',');

/// `,`, `;` or tab: the one splitting the first records into the same number (at least two) of
/// fields most consistently; `,` when none does.
QChar detectCsvSeparator(QStringView text);

struct CsvTable {
    calc::TableModel model;
    QChar separator = u',';
    bool truncated = false; ///< rows, columns or cell text were left out (the bounds above)
};

/// The table of a CSV text; nullopt when it holds no field with text (or is too large to read).
std::optional<CsvTable> tableFromCsv(QStringView text);

} // namespace brulion::io
