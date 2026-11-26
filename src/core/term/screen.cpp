#include "term/screen.h"

#include "term/charwidth.h"

#include <QHash>

#include <algorithm>
#include <iterator>
#include <utility>

namespace brulion::term {

namespace {

/// Sizes beyond any real window (8 MB of cells a screen at most).
constexpr int MaxColumns = 1000;
constexpr int MaxRows = 500;
/// Combining marks one cell keeps (UTF-16 units): a stream of them cannot grow a cell without end.
constexpr int MaxClusterLength = 32;
constexpr int MaxLinks = 4096;
constexpr int MaxLinkLength = 2048;

void appendCodePoint(QString &out, char32_t cp)
{
    if (cp < 0x10000) {
        out.append(QChar(char16_t(cp)));
    } else {
        out.append(QChar(QChar::highSurrogate(cp)));
        out.append(QChar(QChar::lowSurrogate(cp)));
    }
}

/// A half of a wide character that lost its other half: a space with the same colours.
void orphan(Line &line, int column)
{
    Cell &cell = line.cells[column];
    if (cell.attrs & ClusterFlag)
        line.clusters.remove(column);
    cell.ch = U' ';
    cell.attrs &= quint16(~(WideFlag | ClusterFlag));
}

/// The cells of `line` cut or padded to `columns`.
void resizeLine(Line &line, int columns)
{
    const int size = int(line.cells.size());
    if (columns < size) {
        if (line.cells[columns - 1].isWide())
            orphan(line, columns - 1);
        if (!line.clusters.isEmpty()) {
            for (auto it = line.clusters.begin(); it != line.clusters.end();) {
                if (it.key() >= columns)
                    it = line.clusters.erase(it);
                else
                    ++it;
            }
        }
    }
    line.cells.resize(columns);
}

/// The DEC Special Graphics character for `cp` (0x5f-0x7e), else `cp`.
char32_t decSpecialGraphics(char32_t cp)
{
    static constexpr char32_t table[] = {
            0x00A0, // _  blank
            0x25C6, // `  diamond
            0x2592, // a  checkerboard
            0x2409, // b  HT
            0x240C, // c  FF
            0x240D, // d  CR
            0x240A, // e  LF
            0x00B0, // f  degree
            0x00B1, // g  plus-minus
            0x2424, // h  NL
            0x240B, // i  VT
            0x2518, // j  lower right corner
            0x2510, // k  upper right corner
            0x250C, // l  upper left corner
            0x2514, // m  lower left corner
            0x253C, // n  crossing lines
            0x23BA, // o  scan line 1
            0x23BB, // p  scan line 3
            0x2500, // q  horizontal line (scan line 5)
            0x23BC, // r  scan line 7
            0x23BD, // s  scan line 9
            0x251C, // t  left tee
            0x2524, // u  right tee
            0x2534, // v  bottom tee
            0x252C, // w  top tee
            0x2502, // x  vertical line
            0x2264, // y  less or equal
            0x2265, // z  greater or equal
            0x03C0, // {  pi
            0x2260, // |  not equal
            0x00A3, // }  pound
            0x00B7, // ~  centred dot
    };
    if (cp >= 0x5f && cp <= 0x7e)
        return table[cp - 0x5f];
    return cp;
}

} // namespace

// ---- Line ---------------------------------------------------------------------------------------

QString Line::cellText(int column) const
{
    if (column < 0 || column >= cells.size())
        return {};
    const Cell &cell = cells[column];
    if (cell.ch == 0)
        return {};
    if (cell.attrs & ClusterFlag) {
        const auto it = clusters.constFind(column);
        if (it != clusters.constEnd())
            return *it;
    }
    QString out;
    appendCodePoint(out, cell.ch);
    return out;
}

QString Line::text(int from, int to) const
{
    QString out;
    from = std::max(0, from);
    to = std::min(to, int(cells.size()));
    if (from >= to)
        return out;
    out.reserve(to - from);
    for (int column = from; column < to; ++column) {
        const Cell &cell = cells[column];
        if (cell.ch == 0)
            continue;
        if (cell.attrs & ClusterFlag) {
            const auto it = clusters.constFind(column);
            if (it != clusters.constEnd()) {
                out.append(*it);
                continue;
            }
        }
        appendCodePoint(out, cell.ch);
    }
    return out;
}

QString Line::trimmedText() const
{
    QString out = text(0, int(cells.size()));
    qsizetype end = out.size();
    while (end > 0 && out.at(end - 1) == u' ')
        --end;
    out.truncate(end);
    return out;
}

// ---- Screen -------------------------------------------------------------------------------------

Screen::Screen(int columns, int rows, int scrollbackLimit)
    : m_columns(std::clamp(columns, 1, MaxColumns))
    , m_rows(std::clamp(rows, 1, MaxRows))
    , m_historyLimit(std::max(0, scrollbackLimit))
{
    const Line blank = blankLine();
    m_mainLines.fill(blank, m_rows);
    m_altLines.fill(blank, m_rows);
    m_bottom = m_rows - 1;
    resetTabStops(0);
    m_dirty.fill(false, m_rows);
}

void Screen::clearLine(Line &line) const
{
    line.cells.fill(Cell::blank(m_cursor.pen), m_columns);
    line.clusters.clear();
    line.wrapped = false;
}

Line Screen::blankLine() const
{
    Line line;
    line.cells.fill(Cell::blank(m_cursor.pen), m_columns);
    return line;
}

const Line &Screen::historyLine(int index) const
{
    static const Line empty;
    if (m_alternate || index < 0 || index >= m_historyCount)
        return empty;
    return m_history[(m_historyStart + index) % m_history.size()];
}

void Screen::setScrollbackLimit(int lines)
{
    lines = std::max(0, lines);
    if (lines == m_historyLimit)
        return;
    QList<Line> kept;
    const int keep = std::min(lines, m_historyCount);
    kept.reserve(keep);
    for (int i = m_historyCount - keep; i < m_historyCount; ++i)
        kept.append(std::move(m_history[(m_historyStart + i) % m_history.size()]));
    m_history = std::move(kept);
    m_historyStart = 0;
    m_historyCount = keep;
    m_historyLimit = lines;
    markAllDirty();
}

void Screen::clearHistory()
{
    m_history.clear();
    m_historyStart = 0;
    m_historyCount = 0;
    markAllDirty();
}

void Screen::clearKeepingCursorLine()
{
    clearHistory();
    if (m_alternate)
        return;
    const int row = m_cursor.row;
    if (row > 0) {
        // The cursor's row and the rows under it move up; what scrolled out above is gone.
        for (int i = 0; i + row < m_rows; ++i)
            m_mainLines[i] = std::move(m_mainLines[i + row]);
        m_cursor.row = 0;
        if (m_hasSavedMain)
            m_savedMain.row = std::max(0, m_savedMain.row - row);
    }
    for (int i = 1; i < m_rows; ++i) {
        Line &line = m_mainLines[i];
        line.cells.fill(Cell(), m_columns);
        line.clusters.clear();
        line.wrapped = false;
    }
    m_mainLines[0].wrapped = false;
    markAllDirty();
}

QString Screen::text() const
{
    QString out;
    for (int row = 0; row < m_rows; ++row) {
        if (row > 0)
            out.append(u'\n');
        out.append(line(row).trimmedText());
    }
    return out;
}

void Screen::pushHistory(Line &&line)
{
    if (m_historyLimit == 0)
        return;
    // Trailing plain blanks are not kept (a wrapped line keeps them: its text goes on).
    if (!line.wrapped) {
        qsizetype n = line.cells.size();
        while (n > 0 && line.cells[n - 1].isPlainBlank())
            --n;
        if (n < line.cells.size()) {
            line.cells.resize(n);
            if (line.cells.capacity() > 2 * n + 16)
                line.cells.squeeze();
        }
    }
    if (m_history.size() < m_historyLimit) {
        m_history.append(std::move(line));
        ++m_historyCount;
    } else {
        m_history[m_historyStart] = std::move(line);
        m_historyStart = (m_historyStart + 1) % int(m_history.size());
    }
    ++m_scrolledIntoHistory;
}

Line Screen::takeNewestHistory()
{
    const int at = (m_historyStart + m_historyCount - 1) % int(m_history.size());
    Line line = std::move(m_history[at]);
    if (m_historyStart == 0 && at == m_history.size() - 1) {
        m_history.removeLast();
    } else {
        // A full ring: make it linear first, so the newest line is the last element.
        QList<Line> linear;
        linear.reserve(m_historyCount - 1);
        for (int i = 0; i < m_historyCount - 1; ++i)
            linear.append(std::move(m_history[(m_historyStart + i) % m_history.size()]));
        m_history = std::move(linear);
        m_historyStart = 0;
    }
    --m_historyCount;
    return line;
}

void Screen::setCursorVisible(bool visible)
{
    m_cursorVisible = visible;
}

void Screen::setCursorStyle(CursorShape shape, bool blinking)
{
    m_cursorShape = shape;
    m_cursorBlinking = blinking;
}

void Screen::setAutoWrap(bool on)
{
    m_autoWrap = on;
    if (!on)
        m_cursor.pendingWrap = false;
}

void Screen::setOriginMode(bool on)
{
    m_cursor.originMode = on;
    moveTo(0, 0);
}

void Screen::setReverseVideo(bool on)
{
    if (m_reverseVideo == on)
        return;
    m_reverseVideo = on;
    markAllDirty();
}

void Screen::setAlternateScreen(bool on, bool clearAlternate)
{
    if (on == m_alternate)
        return;
    if (clearAlternate) {
        const Line blank = blankLine();
        for (Line &line : m_altLines)
            line = blank;
    }
    m_alternate = on;
    m_cursor.row = std::min(m_cursor.row, m_rows - 1);
    m_cursor.pendingWrap = false;
    markAllDirty();
}

void Screen::setScrollRegion(int top, int bottom)
{
    top = std::max(0, top);
    bottom = std::min(bottom, m_rows - 1);
    if (top >= bottom)
        return;
    m_top = top;
    m_bottom = bottom;
    moveTo(0, 0);
}

void Screen::designateCharset(int slot, Charset charset)
{
    if (slot == 0)
        m_cursor.g0 = charset;
    else if (slot == 1)
        m_cursor.g1 = charset;
}

char32_t Screen::mapCharset(char32_t cp) const
{
    if (cp >= 0x80)
        return cp;
    switch (m_cursor.shifted ? m_cursor.g1 : m_cursor.g0) {
    case Charset::Ascii: return cp;
    case Charset::DecSpecialGraphics: return decSpecialGraphics(cp);
    case Charset::Uk: return cp == U'#' ? char32_t(0x00A3) : cp;
    }
    return cp;
}

int Screen::clampColumn(int column) const
{
    return std::clamp(column, 0, m_columns - 1);
}

void Screen::markDirty(int row)
{
    m_dirty[row] = true;
    m_anyDirty = true;
}

void Screen::markDirty(int from, int to)
{
    for (int row = std::max(0, from); row <= to && row < m_rows; ++row)
        m_dirty[row] = true;
    m_anyDirty = true;
}

void Screen::clearDirty()
{
    std::fill(m_dirty.begin(), m_dirty.end(), false);
    m_allDirty = false;
    m_anyDirty = false;
    m_scrolledIntoHistory = 0;
}

void Screen::markAllDirty()
{
    m_allDirty = true;
    m_anyDirty = true;
}

void Screen::fixWideEdges(Line &line, int from, int to)
{
    const int size = int(line.cells.size());
    if (from > 0 && from < size && line.cells[from].isWideTail())
        orphan(line, from - 1);
    if (to > 0 && to < size && line.cells[to - 1].isWide() && line.cells[to].isWideTail())
        orphan(line, to);
}

void Screen::blankCells(Line &line, int from, int to)
{
    from = std::max(0, from);
    to = std::min(to, int(line.cells.size()));
    if (from >= to)
        return;
    fixWideEdges(line, from, to);
    const Cell blank = Cell::blank(m_cursor.pen);
    if (from == 0 && to == line.cells.size())
        line.clusters.clear();
    for (int column = from; column < to; ++column) {
        Cell &cell = line.cells[column];
        if ((cell.attrs & ClusterFlag) && !line.clusters.isEmpty())
            line.clusters.remove(column);
        cell = blank;
    }
}

void Screen::writeCell(Line &line, int column, char32_t cp, int width)
{
    fixWideEdges(line, column, column + width);
    Cell &cell = line.cells[column];
    if (cell.attrs & ClusterFlag)
        line.clusters.remove(column);
    const Pen &pen = m_cursor.pen;
    cell.ch = cp;
    cell.fg = pen.fg;
    cell.bg = pen.bg;
    cell.attrs = pen.attrs & AttributeMask;
    cell.link = pen.link;
    if (width == 2) {
        cell.attrs |= WideFlag;
        Cell &tail = line.cells[column + 1];
        if (tail.attrs & ClusterFlag)
            line.clusters.remove(column + 1);
        tail = cell;
        tail.ch = 0;
        tail.attrs &= quint16(~(WideFlag | ClusterFlag));
    }
}

void Screen::joinPrevious(char32_t cp)
{
    int column = m_cursor.pendingWrap ? m_cursor.column : m_cursor.column - 1;
    if (column < 0)
        return; // Nothing before it on this row: dropped.
    Line &line = cursorLine();
    if (line.cells[column].isWideTail() && column > 0)
        --column;
    Cell &cell = line.cells[column];
    if (cell.ch == 0)
        return;
    QString &cluster = line.clusters[column];
    if (!(cell.attrs & ClusterFlag)) {
        cluster.clear();
        appendCodePoint(cluster, cell.ch);
        cell.attrs |= ClusterFlag;
    }
    if (cluster.size() >= MaxClusterLength)
        return;
    appendCodePoint(cluster, cp);
    markDirty(m_cursor.row);
}

void Screen::print(char32_t cp)
{
    const char32_t mapped = mapCharset(cp);
    int width = charWidth(mapped);
    if (width == 0) {
        joinPrevious(mapped);
        return;
    }
    if (width == 2 && m_columns < 2)
        width = 1;
    CursorState &c = m_cursor;
    if (c.pendingWrap) {
        cursorLine().wrapped = true;
        c.column = 0;
        index();
    }
    if (width == 2 && c.column == m_columns - 1) {
        if (m_autoWrap) {
            // One cell left: it stays blank and the character goes to the next row.
            Line &line = cursorLine();
            blankCells(line, c.column, m_columns);
            line.wrapped = true;
            markDirty(c.row);
            c.column = 0;
            index();
        } else {
            c.column = m_columns - 2;
        }
    }
    if (m_insertMode)
        insertChars(width);
    writeCell(cursorLine(), c.column, mapped, width);
    m_lastChar = cp;
    markDirty(c.row);
    c.column += width;
    if (c.column >= m_columns) {
        c.column = m_columns - 1;
        c.pendingWrap = m_autoWrap;
    }
}

void Screen::carriageReturn()
{
    m_cursor.column = 0;
    m_cursor.pendingWrap = false;
}

void Screen::backspace()
{
    m_cursor.pendingWrap = false;
    if (m_cursor.column > 0)
        --m_cursor.column;
}

void Screen::lineFeed()
{
    index();
    if (m_newLineMode)
        carriageReturn();
}

void Screen::index()
{
    m_cursor.pendingWrap = false;
    if (m_cursor.row == m_bottom)
        scrollRegionUp(m_top, m_bottom, 1);
    else if (m_cursor.row < m_rows - 1)
        ++m_cursor.row;
}

void Screen::reverseIndex()
{
    m_cursor.pendingWrap = false;
    if (m_cursor.row == m_top)
        scrollRegionDown(m_top, m_bottom, 1);
    else if (m_cursor.row > 0)
        --m_cursor.row;
}

void Screen::nextLine()
{
    carriageReturn();
    index();
}

void Screen::tab(int count)
{
    m_cursor.pendingWrap = false;
    for (int n = 0; n < count && m_cursor.column < m_columns - 1; ++n) {
        int column = m_cursor.column + 1;
        while (column < m_columns - 1 && !m_tabStops[column])
            ++column;
        m_cursor.column = column;
    }
}

void Screen::backTab(int count)
{
    m_cursor.pendingWrap = false;
    for (int n = 0; n < count && m_cursor.column > 0; ++n) {
        int column = m_cursor.column - 1;
        while (column > 0 && !m_tabStops[column])
            --column;
        m_cursor.column = column;
    }
}

void Screen::setTabStop()
{
    m_tabStops[m_cursor.column] = true;
}

void Screen::clearTabStop(int mode)
{
    if (mode == 0)
        m_tabStops[m_cursor.column] = false;
    else if (mode == 3)
        std::fill(m_tabStops.begin(), m_tabStops.end(), false);
}

void Screen::resetTabStops(int from)
{
    m_tabStops.resize(m_columns);
    for (int column = std::max(0, from); column < m_columns; ++column)
        m_tabStops[column] = column > 0 && column % 8 == 0;
}

void Screen::moveTo(int row, int column)
{
    moveToRow(row);
    moveToColumn(column);
}

void Screen::moveToRow(int row)
{
    m_cursor.pendingWrap = false;
    if (m_cursor.originMode)
        m_cursor.row = std::clamp(row + m_top, m_top, m_bottom);
    else
        m_cursor.row = std::clamp(row, 0, m_rows - 1);
}

void Screen::moveToColumn(int column)
{
    m_cursor.pendingWrap = false;
    m_cursor.column = clampColumn(column);
}

void Screen::moveUp(int count)
{
    m_cursor.pendingWrap = false;
    const int limit = m_cursor.row >= m_top ? m_top : 0;
    m_cursor.row = std::max(m_cursor.row - std::max(count, 0), limit);
}

void Screen::moveDown(int count)
{
    m_cursor.pendingWrap = false;
    const int limit = m_cursor.row <= m_bottom ? m_bottom : m_rows - 1;
    m_cursor.row = std::min(m_cursor.row + std::max(count, 0), limit);
}

void Screen::moveForward(int count)
{
    m_cursor.pendingWrap = false;
    m_cursor.column = std::min(m_cursor.column + std::max(count, 0), m_columns - 1);
}

void Screen::moveBackward(int count)
{
    m_cursor.pendingWrap = false;
    m_cursor.column = std::max(m_cursor.column - std::max(count, 0), 0);
}

void Screen::eraseInDisplay(int mode)
{
    QList<Line> &lines = activeLines();
    switch (mode) {
    case 0:
        eraseInLine(0);
        for (int row = m_cursor.row + 1; row < m_rows; ++row) {
            blankCells(lines[row], 0, m_columns);
            lines[row].wrapped = false;
        }
        markDirty(m_cursor.row, m_rows - 1);
        break;
    case 1:
        for (int row = 0; row < m_cursor.row; ++row) {
            blankCells(lines[row], 0, m_columns);
            lines[row].wrapped = false;
        }
        eraseInLine(1);
        markDirty(0, m_cursor.row);
        break;
    case 2:
        for (Line &line : lines) {
            blankCells(line, 0, m_columns);
            line.wrapped = false;
        }
        m_cursor.pendingWrap = false;
        markAllDirty();
        break;
    case 3: clearHistory(); break;
    default: break;
    }
}

void Screen::eraseInLine(int mode)
{
    Line &line = cursorLine();
    m_cursor.pendingWrap = false;
    switch (mode) {
    case 0:
        blankCells(line, m_cursor.column, m_columns);
        line.wrapped = false;
        break;
    case 1: blankCells(line, 0, m_cursor.column + 1); break;
    case 2:
        blankCells(line, 0, m_columns);
        line.wrapped = false;
        break;
    default: return;
    }
    markDirty(m_cursor.row);
}

void Screen::insertChars(int count)
{
    m_cursor.pendingWrap = false;
    const int column = m_cursor.column;
    count = std::clamp(count, 0, m_columns - column);
    if (count == 0)
        return;
    Line &line = cursorLine();
    // A wide character split by the insertion, or losing its right half off the end, goes.
    if (line.cells[column].isWideTail()) {
        orphan(line, column - 1);
        orphan(line, column);
    }
    const int lastKept = m_columns - count - 1;
    if (lastKept >= column && line.cells[lastKept].isWide())
        orphan(line, lastKept);
    std::move_backward(line.cells.begin() + column, line.cells.begin() + (m_columns - count), line.cells.end());
    std::fill(line.cells.begin() + column, line.cells.begin() + column + count, Cell::blank(m_cursor.pen));
    if (!line.clusters.isEmpty()) {
        QHash<int, QString> moved;
        for (auto it = line.clusters.cbegin(); it != line.clusters.cend(); ++it) {
            const int key = it.key() < column ? it.key() : it.key() + count;
            if (key < m_columns)
                moved.insert(key, it.value());
        }
        line.clusters = std::move(moved);
    }
    markDirty(m_cursor.row);
}

void Screen::deleteChars(int count)
{
    m_cursor.pendingWrap = false;
    const int column = m_cursor.column;
    count = std::clamp(count, 0, m_columns - column);
    if (count == 0)
        return;
    Line &line = cursorLine();
    if (line.cells[column].isWideTail()) {
        orphan(line, column - 1);
        orphan(line, column);
    }
    const int end = column + count; // first cell kept after the deleted ones
    if (end < m_columns && line.cells[end].isWideTail())
        orphan(line, end);
    if (!line.clusters.isEmpty()) {
        QHash<int, QString> moved;
        for (auto it = line.clusters.cbegin(); it != line.clusters.cend(); ++it) {
            if (it.key() < column)
                moved.insert(it.key(), it.value());
            else if (it.key() >= end)
                moved.insert(it.key() - count, it.value());
        }
        line.clusters = std::move(moved);
    }
    std::move(line.cells.begin() + end, line.cells.end(), line.cells.begin() + column);
    std::fill(line.cells.end() - count, line.cells.end(), Cell::blank(m_cursor.pen));
    markDirty(m_cursor.row);
}

void Screen::eraseChars(int count)
{
    m_cursor.pendingWrap = false;
    count = std::clamp(count, 0, m_columns - m_cursor.column);
    if (count == 0)
        return;
    blankCells(cursorLine(), m_cursor.column, m_cursor.column + count);
    markDirty(m_cursor.row);
}

void Screen::insertLines(int count)
{
    if (m_cursor.row < m_top || m_cursor.row > m_bottom)
        return;
    scrollRegionDown(m_cursor.row, m_bottom, count);
    m_cursor.column = 0;
    m_cursor.pendingWrap = false;
}

void Screen::deleteLines(int count)
{
    if (m_cursor.row < m_top || m_cursor.row > m_bottom)
        return;
    // Deleted lines are gone: only lines scrolled off the top of the screen go into the scrollback.
    const int top = m_cursor.row;
    count = std::clamp(count, 0, m_bottom - top + 1);
    if (count > 0) {
        QList<Line> &lines = activeLines();
        std::rotate(lines.begin() + top, lines.begin() + top + count, lines.begin() + m_bottom + 1);
        for (int row = m_bottom - count + 1; row <= m_bottom; ++row)
            clearLine(lines[row]);
        markDirty(top, m_bottom);
    }
    m_cursor.column = 0;
    m_cursor.pendingWrap = false;
}

void Screen::scrollUp(int count)
{
    scrollRegionUp(m_top, m_bottom, count);
}

void Screen::scrollDown(int count)
{
    scrollRegionDown(m_top, m_bottom, count);
}

void Screen::scrollRegionUp(int top, int bottom, int count)
{
    count = std::clamp(count, 0, bottom - top + 1);
    if (count == 0)
        return;
    QList<Line> &lines = activeLines();
    const bool toHistory = top == 0 && !m_alternate;
    for (int i = 0; i < count; ++i) {
        if (toHistory)
            pushHistory(std::move(lines[top + i]));
    }
    std::rotate(lines.begin() + top, lines.begin() + top + count, lines.begin() + bottom + 1);
    // In place: a line moved into the scrollback left an empty one (one allocation), a rotated
    // line keeps its cells' storage (none).
    for (int row = bottom - count + 1; row <= bottom; ++row)
        clearLine(lines[row]);
    markDirty(top, bottom);
}

void Screen::scrollRegionDown(int top, int bottom, int count)
{
    count = std::clamp(count, 0, bottom - top + 1);
    if (count == 0)
        return;
    QList<Line> &lines = activeLines();
    std::rotate(lines.begin() + top, lines.begin() + (bottom + 1 - count), lines.begin() + bottom + 1);
    for (int row = top; row < top + count; ++row)
        clearLine(lines[row]);
    markDirty(top, bottom);
}

void Screen::repeatLast(int count)
{
    if (m_lastChar == 0)
        return;
    count = std::clamp(count, 0, m_columns * m_rows);
    const char32_t cp = m_lastChar;
    for (int n = 0; n < count; ++n)
        print(cp);
}

void Screen::alignmentTest()
{
    m_top = 0;
    m_bottom = m_rows - 1;
    m_cursor.originMode = false;
    Cell e;
    e.ch = U'E';
    for (Line &line : activeLines()) {
        line.cells.fill(e, m_columns);
        line.clusters.clear();
        line.wrapped = false;
    }
    m_cursor.row = 0;
    m_cursor.column = 0;
    m_cursor.pendingWrap = false;
    markAllDirty();
}

void Screen::saveCursor()
{
    if (m_alternate) {
        m_savedAlt = m_cursor;
        m_hasSavedAlt = true;
    } else {
        m_savedMain = m_cursor;
        m_hasSavedMain = true;
    }
}

void Screen::restoreCursor()
{
    const bool has = m_alternate ? m_hasSavedAlt : m_hasSavedMain;
    if (has) {
        m_cursor = m_alternate ? m_savedAlt : m_savedMain;
    } else {
        m_cursor = CursorState();
    }
    m_cursor.row = std::clamp(m_cursor.row, 0, m_rows - 1);
    if (m_cursor.column > m_columns - 1) {
        m_cursor.column = m_columns - 1;
        m_cursor.pendingWrap = false;
    }
    if (!m_autoWrap)
        m_cursor.pendingWrap = false;
}

void Screen::resize(int columns, int rows)
{
    columns = std::clamp(columns, 1, MaxColumns);
    rows = std::clamp(rows, 1, MaxRows);
    if (columns == m_columns && rows == m_rows)
        return;

    const int oldColumns = m_columns;
    const int oldRows = m_rows;
    const Pen pen = m_cursor.pen;
    m_cursor.pen = Pen(); // new lines are plain blanks
    m_columns = columns;

    // `shift`: how far the screen's content moved down (negative: up) — its cursors follow.
    auto resizeScreen = [&](QList<Line> &lines, bool main, int cursorRow) {
        for (Line &line : lines)
            resizeLine(line, columns);
        int shift = 0;
        if (rows < oldRows) {
            // The cursor's row stays on the screen: the rows above it go first (into the main
            // screen's scrollback), else the rows below.
            const int up = std::max(0, cursorRow - rows + 1);
            for (int i = 0; i < up; ++i) {
                if (main)
                    pushHistory(std::move(lines[i]));
            }
            lines.remove(0, up);
            lines.resize(rows);
            shift = -up;
        } else if (rows > oldRows) {
            int pulled = 0;
            if (main && !m_alternate) {
                const int want = rows - oldRows;
                while (pulled < want && m_historyCount > 0) {
                    Line line = takeNewestHistory();
                    resizeLine(line, columns);
                    lines.prepend(std::move(line));
                    ++pulled;
                }
            }
            const Line blank = blankLine();
            while (lines.size() < rows)
                lines.append(blank);
            shift = pulled;
        }
        return shift;
    };

    const int mainCursorRow = m_alternate ? m_savedMain.row : m_cursor.row;
    const int altCursorRow = m_alternate ? m_cursor.row : m_savedAlt.row;
    const int mainShift = resizeScreen(m_mainLines, true, mainCursorRow);
    const int altShift = resizeScreen(m_altLines, false, altCursorRow);
    m_rows = rows;

    auto clampCursor = [&](CursorState &c, int shift) {
        c.row = std::clamp(c.row + shift, 0, rows - 1);
        if (c.column > columns - 1)
            c.column = columns - 1;
        c.pendingWrap = false;
    };
    clampCursor(m_cursor, m_alternate ? altShift : mainShift);
    clampCursor(m_savedMain, mainShift);
    clampCursor(m_savedAlt, altShift);
    m_cursor.pen = pen;

    m_top = 0;
    m_bottom = rows - 1;
    resetTabStops(oldColumns);
    m_dirty.fill(false, rows);
    markAllDirty();
}

void Screen::reset()
{
    m_cursor = CursorState();
    m_savedMain = CursorState();
    m_savedAlt = CursorState();
    m_hasSavedMain = false;
    m_hasSavedAlt = false;
    m_cursorVisible = true;
    m_cursorShape = CursorShape::Block;
    m_cursorBlinking = true;
    m_autoWrap = true;
    m_insertMode = false;
    m_newLineMode = false;
    m_reverseVideo = false;
    m_alternate = false;
    m_top = 0;
    m_bottom = m_rows - 1;
    m_lastChar = 0;
    resetTabStops(0);
    const Line blank = blankLine();
    for (Line &line : m_mainLines)
        line = blank;
    for (Line &line : m_altLines)
        line = blank;
    markAllDirty();
}

void Screen::softReset()
{
    m_cursorVisible = true;
    m_autoWrap = true;
    m_insertMode = false;
    m_cursor.originMode = false;
    m_cursor.pen = Pen();
    m_cursor.shifted = false;
    m_cursor.g0 = Charset::Ascii;
    m_cursor.g1 = Charset::Ascii;
    m_cursor.pendingWrap = false;
    m_top = 0;
    m_bottom = m_rows - 1;
    if (m_alternate) {
        m_savedAlt = CursorState();
        m_hasSavedAlt = false;
    } else {
        m_savedMain = CursorState();
        m_hasSavedMain = false;
    }
}

quint16 Screen::hyperlinkId(const QString &uri)
{
    if (uri.isEmpty() || uri.size() > MaxLinkLength)
        return 0;
    const qsizetype at = m_links.indexOf(uri);
    if (at >= 0)
        return quint16(at + 1);
    if (m_links.size() >= MaxLinks)
        return 0;
    m_links.append(uri);
    return quint16(m_links.size());
}

QString Screen::hyperlink(quint16 id) const
{
    if (id == 0 || id > m_links.size())
        return {};
    return m_links.at(id - 1);
}

} // namespace brulion::term
