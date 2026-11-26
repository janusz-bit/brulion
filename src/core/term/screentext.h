// The text of the screen and its scrollback as the user reads it (term.h): rows joined where the
// program's text wrapped at the right margin (Line::wrapped), with the way back from a position in
// that text to its cell — what searching the scrollback (the panel's find bar) and recognising links
// in the output (links.h) work on.
//
//   const LogicalLine l = logicalLine(screen, -3, true);   // the line holding scrollback line -3
//   l.text;                                                // "make: *** [Makefile:12] Error 1"
//   l.cells.at(5);                                         // {line, column} of the 6th character
//   findText(screen, u"error", Qt::CaseInsensitive, 1000); // every match, oldest first
//
// Lines are counted as Screen::lineAt() counts them: -historySize() … -1 the scrollback, 0 …
// rows() - 1 the screen. A right half of a wide character adds no text; a cell with combining marks
// adds its whole cluster (each of its UTF-16 units maps to that cell).
#pragma once

#include <QList>
#include <QString>
#include <QStringView>

namespace brulion::term {

class Screen;

/// A cell: `line` as Screen::lineAt() counts, its column.
struct CellPos {
    int line = 0;
    int column = 0;
    friend bool operator==(CellPos, CellPos) = default;
    friend bool operator<(CellPos a, CellPos b) { return a.line < b.line || (a.line == b.line && a.column < b.column); }
};

/// A span of text: from `start` to `end` (exclusive), possibly over several rows.
struct TextRange {
    CellPos start;
    CellPos end;
    friend bool operator==(const TextRange &, const TextRange &) = default;
};

/// Rows of one line of text: the row `first` and those it wrapped into, up to `last`.
struct LogicalLine {
    int first = 0;
    int last = 0;
    /// Their text; the last row's trailing spaces dropped.
    QString text;
    /// The cell of each UTF-16 unit of `text`, and its width in columns (filled only when asked
    /// for).
    QList<CellPos> cells;
    QList<quint8> widths;
    /// Where a range whose last unit is `index` ends: the column after that unit's cell.
    CellPos endOf(qsizetype index) const { return {cells.at(index).line, cells.at(index).column + widths.at(index)}; }
    /// The units [from, to) of `text` as cells (cells filled, from < to).
    TextRange rangeOf(qsizetype from, qsizetype to) const { return {cells.at(from), endOf(to - 1)}; }
    /// The unit of `text` at `cell` (-1: none — a blank after the text, another line).
    qsizetype indexAt(CellPos cell) const;
};

/// The line of text that holds `line` (rows before it that wrapped into it included). `withCells`:
/// LogicalLine::cells too. `line` out of range: an empty line at it.
LogicalLine logicalLine(const Screen &screen, int line, bool withCells);

/// Every occurrence of `needle` in the scrollback and the screen, oldest first, at most `limit`
/// (more are not looked for). Matches do not overlap; one may span wrapped rows, never two lines
/// of text. An empty needle finds nothing.
QList<TextRange> findText(const Screen &screen, QStringView needle, Qt::CaseSensitivity sensitivity, int limit);

} // namespace brulion::term
