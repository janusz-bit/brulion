#include "term/screentext.h"

#include "term/screen.h"

#include <algorithm>

namespace brulion::term {

namespace {

/// Row `index`'s text appended to `out` (to `cells` / `widths` too when given); `trim`: without its
/// trailing spaces.
void appendRow(const Line &line, int index, bool trim, QString &out, QList<CellPos> *cells, QList<quint8> *widths)
{
    int end = int(line.cells.size());
    if (trim) {
        while (end > 0 && line.cells.at(end - 1).ch == U' ')
            --end;
    }
    for (int column = 0; column < end; ++column) {
        const Cell &cell = line.cells.at(column);
        if (cell.isWideTail())
            continue;
        const qsizetype before = out.size();
        if (cell.attrs & ClusterFlag)
            out += line.cellText(column);
        else if (cell.ch > 0xffff) {
            out += QChar::highSurrogate(cell.ch);
            out += QChar::lowSurrogate(cell.ch);
        } else
            out += QChar(char16_t(cell.ch));
        if (cells) {
            const quint8 width = cell.isWide() ? 2 : 1;
            for (qsizetype i = before; i < out.size(); ++i) {
                cells->append({index, column});
                widths->append(width);
            }
        }
    }
}

} // namespace

qsizetype LogicalLine::indexAt(CellPos cell) const
{
    // Units are in cell order: the first unit at or after the cell, if it is that cell (or the
    // wide character the cell is the right half of).
    const auto it = std::lower_bound(cells.cbegin(), cells.cend(), cell);
    if (it != cells.cend() && *it == cell)
        return it - cells.cbegin();
    if (it != cells.cbegin()) {
        const qsizetype before = (it - cells.cbegin()) - 1;
        const CellPos end = endOf(before);
        if (cells.at(before).line == cell.line && cell.column < end.column)
            return before;
    }
    return -1;
}

LogicalLine logicalLine(const Screen &screen, int line, bool withCells)
{
    LogicalLine out;
    const int top = -screen.historySize();
    const int bottom = screen.rows() - 1;
    if (line < top || line > bottom) {
        out.first = out.last = line;
        return out;
    }
    int first = line;
    while (first > top && screen.lineAt(first - 1).wrapped)
        --first;
    int last = line;
    while (last < bottom && screen.lineAt(last).wrapped)
        ++last;
    out.first = first;
    out.last = last;
    for (int index = first; index <= last; ++index)
        appendRow(screen.lineAt(index), index, index == last, out.text, withCells ? &out.cells : nullptr,
                  withCells ? &out.widths : nullptr);
    return out;
}

QList<TextRange> findText(const Screen &screen, QStringView needle, Qt::CaseSensitivity sensitivity, int limit)
{
    QList<TextRange> out;
    if (needle.isEmpty() || limit <= 0)
        return out;
    const int bottom = screen.rows() - 1;
    for (int line = -screen.historySize(); line <= bottom && out.size() < limit;) {
        // The text alone first: the map of cells only for the lines that hold a match.
        LogicalLine text = logicalLine(screen, line, false);
        const int next = text.last + 1;
        qsizetype at = text.text.indexOf(needle, 0, sensitivity);
        if (at >= 0) {
            const LogicalLine mapped = logicalLine(screen, line, true);
            while (at >= 0 && out.size() < limit) {
                out.append(mapped.rangeOf(at, at + needle.size()));
                at = mapped.text.indexOf(needle, at + needle.size(), sensitivity);
            }
        }
        line = next;
    }
    return out;
}

} // namespace brulion::term
