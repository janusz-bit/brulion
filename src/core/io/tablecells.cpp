#include "io/tablecells.h"

#include "io/csv.h"
#include "io/tablecsv.h"

#include <QRegularExpression>

#include <algorithm>

namespace brulion::io {

using namespace Qt::StringLiterals;

namespace {

/// A cell's text with `<br>` as a line break (the table's form of one).
QString plainCell(const QString &cell)
{
    if (!cell.contains(u'<'))
        return cell;
    static const QRegularExpression br(u"<br\\s*/?>"_s, QRegularExpression::CaseInsensitiveOption);
    return QString(cell).replace(br, u"\n"_s);
}

/// Cell (r, c) of the model to write (r -1 the header); a short row grows to it.
QString &cellRef(calc::TableModel &model, int r, int c)
{
    if (r < 0)
        return model.header[c];
    QStringList &row = model.rows[r];
    while (row.size() <= c)
        row.append(QString());
    return row[c];
}

bool validColumn(const calc::TableModel &model, int c)
{
    return c >= 0 && c < model.header.size();
}

/// The text of cell (r, c) (r -1 the header): a formula's result, else the text as written ("" past
/// the end of a short row).
QString shownCell(const calc::TableModel &model, const calc::TableEval &eval, int r, int c)
{
    if (r < 0)
        return model.header.value(c);
    if (r >= model.rows.size())
        return {};
    const QString text = model.rows.at(r).value(c);
    const QStringView trimmed = QStringView(text).trimmed();
    if (trimmed.startsWith(u'=') && !trimmed.mid(1).trimmed().isEmpty() && r < eval.values.size()
        && c < eval.values.at(r).size() && !eval.values.at(r).at(c).isNull())
        return eval.values.at(r).at(c);
    return text;
}

/// The valid part of a rectangle: its rows that exist and its columns clamped; false when empty.
bool clamp(const calc::TableModel &model, const TableCells &cells, QList<int> &rows, int &first, int &last)
{
    first = std::max(0, cells.firstColumn);
    last = std::min<int>(cells.lastColumn, int(model.header.size()) - 1);
    for (const int r : cells.rows) {
        if (r >= -1 && r < model.rows.size())
            rows.append(r);
    }
    return !rows.isEmpty() && first <= last;
}

QString tsvField(const QString &text)
{
    if (!text.contains(u'\t') && !text.contains(u'\n') && !text.contains(u'\r') && !text.contains(u'"'))
        return text;
    return u'"' + QString(text).replace(u"\""_s, u"\"\""_s) + u'"';
}

QString cut(QString value, bool &truncated)
{
    value = value.trimmed();
    value.replace(u"\r\n"_s, u"\n"_s).replace(u'\r', u'\n');
    if (value.size() > MaxCsvCellLength) {
        value = value.left(MaxCsvCellLength - 1) + u'…';
        truncated = true;
    }
    return value;
}

} // namespace

QString tableCellsToTsv(const calc::TableModel &model, const calc::TableEval &eval, const TableCells &cells)
{
    QList<int> rows;
    int first = 0;
    int last = 0;
    if (!clamp(model, cells, rows, first, last))
        return {};
    if (rows.size() == 1 && first == last)
        return plainCell(shownCell(model, eval, rows.first(), first)); // one cell: its text
    QStringList records;
    for (const int r : std::as_const(rows)) {
        QStringList fields;
        for (int c = first; c <= last; ++c)
            fields.append(tsvField(plainCell(shownCell(model, eval, r, c))));
        records.append(fields.join(u'\t'));
    }
    return records.join(u'\n');
}

QString tableCellsToMarkdown(const calc::TableModel &model, const calc::TableEval &eval, const TableCells &cells)
{
    QList<int> rows;
    int first = 0;
    int last = 0;
    if (!clamp(model, cells, rows, first, last))
        return {};
    calc::TableModel out;
    for (int c = first; c <= last; ++c) {
        out.header.append(model.header.at(c));
        out.aligns.append(c < model.aligns.size() ? model.aligns.at(c) : calc::TableAlign::None);
    }
    for (const int r : std::as_const(rows)) {
        if (r < 0)
            continue; // the header is the table's own
        QStringList row;
        for (int c = first; c <= last; ++c)
            row.append(shownCell(model, eval, r, c));
        out.rows.append(row);
    }
    return calc::serializeTable(out);
}

PastedCells parsePastedCells(QStringView text)
{
    PastedCells out;
    if (text.size() > MaxCsvTextLength) {
        out.truncated = true;
        return out;
    }
    if (text.startsWith(QChar(0xFEFF)))
        text = text.sliced(1);
    if (text.isEmpty())
        return out;
    QList<QStringList> records;
    bool escapeFormulas = true;
    const auto asTable = text.contains(u'|') ? calc::parseTable(text) : std::nullopt;
    if (asTable) {
        records.append(asTable->header);
        for (const QStringList &row : asTable->rows)
            records.append(row);
        escapeFormulas = false;
    } else if (text.contains(u'\t')) {
        records = parseCsv(text, u'\t');
    } else if (text.contains(u'\n') || text.contains(u'\r')) {
        records = parseCsv(text, detectCsvSeparator(text));
    } else {
        records.append({text.toString()});
        escapeFormulas = false;
    }
    if (records.size() > MaxCsvRows + 1) {
        records.resize(MaxCsvRows + 1);
        out.truncated = true;
    }
    for (QStringList &record : records) {
        if (record.size() > MaxCsvColumns) {
            record.resize(MaxCsvColumns);
            out.truncated = true;
        }
        for (QString &field : record) {
            field = cut(field, out.truncated);
            if (escapeFormulas && field.startsWith(u'='))
                field.prepend(u'\\');
        }
    }
    out.grid = records;
    return out;
}

PasteResult pasteTableCells(calc::TableModel &model, const PasteTarget &target, const QList<QStringList> &grid)
{
    PasteResult result;
    const int columns = int(model.header.size());
    if (grid.isEmpty() || target.rows.isEmpty() || !validColumn(model, target.column))
        return result;
    int gridColumns = 0;
    for (const QStringList &record : grid)
        gridColumns = std::max<int>(gridColumns, int(record.size()));
    if (gridColumns == 0)
        return result;
    const bool fill = grid.size() == 1 && gridColumns == 1 && (target.selectedRows > 1 || target.selectedColumns > 1);
    const int rowCount = fill ? std::min<int>(std::max(1, target.selectedRows), int(target.rows.size())) : int(grid.size());
    const int available = columns - target.column;
    const int columnCount = fill ? std::min(std::max(1, target.selectedColumns), available) : std::min(gridColumns, available);
    result.columnsCut = fill ? 0 : std::max(0, gridColumns - available);
    // The rows the paste writes: those of the target, then new ones below the table's last.
    const int added = std::min(std::max(0, rowCount - int(target.rows.size())), MaxCsvRows);
    QList<int> rows = target.rows.mid(0, rowCount);
    for (int i = 0; i < added; ++i) {
        model.rows.append(QStringList(columns));
        rows.append(int(model.rows.size()) - 1);
    }
    result.rowsAdded = added;
    result.rows = int(rows.size());
    result.columns = columnCount;
    for (int i = 0; i < int(rows.size()); ++i) {
        const int r = rows.at(i);
        if (r < -1 || r >= model.rows.size() || (r == -1 && i > 0))
            continue;
        for (int j = 0; j < columnCount; ++j) {
            const int c = target.column + j;
            const QString value = fill ? grid.first().first() : grid.at(i).value(j);
            QString &cell = cellRef(model, r, c);
            if (cell != value) {
                cell = value;
                ++result.cells;
            }
        }
    }
    return result;
}

int clearTableCells(calc::TableModel &model, const TableCells &cells)
{
    QList<int> rows;
    int first = 0;
    int last = 0;
    if (!clamp(model, cells, rows, first, last))
        return 0;
    int changed = 0;
    for (const int r : std::as_const(rows)) {
        for (int c = first; c <= last; ++c) {
            QString &cell = cellRef(model, r, c);
            if (!cell.isEmpty()) {
                cell.clear();
                ++changed;
            }
        }
    }
    return changed;
}

} // namespace brulion::io
