// The terminal's screen model (term.h): the grid of the main and the alternate screen, the main
// screen's scrollback, the cursor and the modes that change how characters land — the operations
// the escape sequences name (Terminal translates them), each on the active screen. No parsing here.
//
//   Screen s(80, 24);
//   s.print(U'a');               // at the cursor, wrapping at the right margin when DECAWM is on
//   s.lineFeed(); s.carriageReturn();
//   s.line(0).trimmedText();     // "a"
//   s.isRowDirty(0);             // true until clearDirty()
//
// Rows and columns are 0-based here (the sequences' parameters are 1-based: Terminal converts).
// lineAt(i) spans the scrollback and the screen: -historySize() … -1 the scrollback (oldest first),
// 0 … rows() - 1 the screen. Only the main screen has a scrollback; lines scroll into it when they
// leave the top of a scroll region that starts at the top of the screen.
#pragma once

#include "term/cell.h"

#include <QList>
#include <QString>

namespace brulion::term {

/// What the cursor looks like (DECSCUSR).
enum class CursorShape { Block, Underline, Bar };

/// The 94-character sets the screen can map printable ASCII through (G0 / G1).
enum class Charset { Ascii, DecSpecialGraphics, Uk };

class Screen {
public:
    explicit Screen(int columns = 80, int rows = 24, int scrollbackLimit = 5000);

    int columns() const { return m_columns; }
    int rows() const { return m_rows; }

    // ---- Content ----------------------------------------------------------------------------
    /// Row `row` (0 … rows() - 1) of the active screen.
    const Line &line(int row) const { return activeLines()[row]; }
    /// Lines in the scrollback (0 while the alternate screen is shown: it has none).
    int historySize() const { return m_alternate ? 0 : m_historyCount; }
    /// The scrollback's line `index`, 0 the oldest.
    const Line &historyLine(int index) const;
    /// -historySize() … -1: the scrollback; 0 … rows() - 1: the screen.
    const Line &lineAt(int index) const { return index < 0 ? historyLine(historySize() + index) : line(index); }
    int scrollbackLimit() const { return m_historyLimit; }
    /// Fewer lines drop the oldest.
    void setScrollbackLimit(int lines);
    void clearHistory();
    /// The text of lineAt(index) without trailing spaces.
    QString lineText(int index) const { return lineAt(index).trimmedText(); }
    /// The screen's text, rows joined with "\n", trailing spaces of each row dropped.
    QString text() const;

    // ---- Cursor and pen ---------------------------------------------------------------------
    int cursorRow() const { return m_cursor.row; }
    int cursorColumn() const { return m_cursor.column; }
    /// A character was printed in the last column and the next one wraps first (DECAWM).
    bool pendingWrap() const { return m_cursor.pendingWrap; }
    bool cursorVisible() const { return m_cursorVisible; }
    void setCursorVisible(bool visible);
    CursorShape cursorShape() const { return m_cursorShape; }
    bool cursorBlinking() const { return m_cursorBlinking; }
    void setCursorStyle(CursorShape shape, bool blinking);
    Pen &pen() { return m_cursor.pen; }
    const Pen &pen() const { return m_cursor.pen; }

    // ---- Modes ------------------------------------------------------------------------------
    bool autoWrap() const { return m_autoWrap; }
    void setAutoWrap(bool on);
    bool originMode() const { return m_cursor.originMode; }
    /// DECOM: the cursor's row counts from the scroll region's top and stays inside it; homes it.
    void setOriginMode(bool on);
    bool insertMode() const { return m_insertMode; }
    void setInsertMode(bool on) { m_insertMode = on; }
    /// LNM: a line feed also returns the carriage.
    bool newLineMode() const { return m_newLineMode; }
    void setNewLineMode(bool on) { m_newLineMode = on; }
    /// DECSCNM: the default colours swapped on the whole screen.
    bool reverseVideo() const { return m_reverseVideo; }
    void setReverseVideo(bool on);
    bool alternateScreen() const { return m_alternate; }
    /// Switches between the main and the alternate screen (?47, ?1047). `clearAlternate` clears
    /// the alternate screen first when entering it, or when leaving it (?1047).
    void setAlternateScreen(bool on, bool clearAlternate);
    int scrollTop() const { return m_top; }
    int scrollBottom() const { return m_bottom; }
    /// DECSTBM with 0-based rows (bottom inclusive); an empty or inverted region is ignored. Homes
    /// the cursor.
    void setScrollRegion(int top, int bottom);
    /// G0 / G1 designation (ESC ( / ESC )) and SO / SI.
    void designateCharset(int slot, Charset charset);
    void shiftOut() { m_cursor.shifted = true; }
    void shiftIn() { m_cursor.shifted = false; }

    // ---- Printing and moving ----------------------------------------------------------------
    /// `cp` at the cursor through the active character set: a wide character takes two cells
    /// (wrapping first when only one is left), a zero-width one joins the cell before the cursor.
    void print(char32_t cp);
    void carriageReturn();
    void backspace();
    /// LF, VT, FF and IND: down a row, scrolling the region at its bottom (CR too in LNM).
    void lineFeed();
    /// IND without LNM's carriage return.
    void index();
    /// RI: up a row, scrolling the region down at its top.
    void reverseIndex();
    /// NEL.
    void nextLine();
    /// HT: to the next tab stop (or the last column), `count` times.
    void tab(int count = 1);
    /// CBT.
    void backTab(int count = 1);
    /// HTS.
    void setTabStop();
    /// TBC: 0 the tab stop at the cursor, 3 every tab stop.
    void clearTabStop(int mode);
    /// CUP / HVP with 0-based coordinates (rows relative to the region in origin mode).
    void moveTo(int row, int column);
    /// VPA with a 0-based row (relative to the region in origin mode).
    void moveToRow(int row);
    /// CHA / HPA with a 0-based column.
    void moveToColumn(int column);
    /// CUU / CUD (stopping at the region's edge when the cursor is inside it) / CUF / CUB.
    void moveUp(int count);
    void moveDown(int count);
    void moveForward(int count);
    void moveBackward(int count);

    // ---- Editing ----------------------------------------------------------------------------
    /// ED: 0 cursor to end, 1 start to cursor, 2 the screen, 3 the scrollback.
    void eraseInDisplay(int mode);
    /// EL: 0 cursor to end, 1 start to cursor, 2 the line.
    void eraseInLine(int mode);
    /// ICH / DCH / ECH at the cursor.
    void insertChars(int count);
    void deleteChars(int count);
    void eraseChars(int count);
    /// IL / DL at the cursor's row, inside the scroll region only.
    void insertLines(int count);
    void deleteLines(int count);
    /// SU / SD: the region's content up (into the scrollback when the region starts at the top) or
    /// down.
    void scrollUp(int count);
    void scrollDown(int count);
    /// REP: the last printed character again `count` times.
    void repeatLast(int count);
    /// DECALN: the screen full of 'E', margins reset, cursor home.
    void alignmentTest();

    // ---- State ------------------------------------------------------------------------------
    /// DECSC: position, pen, origin mode, pending wrap and character sets of the active screen.
    void saveCursor();
    /// DECRC (home with the default pen when nothing was saved).
    void restoreCursor();
    /// New size. Content is kept, not reflowed: lines are cut or padded, the cursor is clamped;
    /// a shorter main screen pushes its top rows into the scrollback when the cursor would fall off,
    /// a taller one takes lines back from it. Margins reset, both screens' saved cursors clamped.
    void resize(int columns, int rows);
    /// RIS's part: both screens cleared, modes and cursor to their defaults (the scrollback stays).
    void reset();
    /// DECSTR's part: modes and pen to their defaults, content kept.
    void softReset();

    // ---- Hyperlinks (OSC 8) ---------------------------------------------------------------
    /// The id of `uri` for Pen::link (0 when the table of 4096 links is full); the same URI gets
    /// the same id.
    quint16 hyperlinkId(const QString &uri);
    /// The URI of link `id` ("" for 0 or an unknown id).
    QString hyperlink(quint16 id) const;

    // ---- What changed -----------------------------------------------------------------------
    /// The row's cells changed since clearDirty() (the cursor's moves are not changes).
    bool isRowDirty(int row) const { return m_allDirty || m_dirty[row]; }
    bool anyDirty() const { return m_anyDirty; }
    /// Lines that went into the scrollback since clearDirty() (a view scrolled back keeps showing
    /// the same lines by moving up as many).
    int scrolledIntoHistory() const { return m_scrolledIntoHistory; }
    void clearDirty();
    void markAllDirty();

private:
    struct CursorState {
        int row = 0;
        int column = 0;
        bool pendingWrap = false;
        bool originMode = false;
        bool shifted = false; // SO: G1 in GL
        Charset g0 = Charset::Ascii;
        Charset g1 = Charset::Ascii;
        Pen pen;
    };

    QList<Line> &activeLines() { return m_alternate ? m_altLines : m_mainLines; }
    const QList<Line> &activeLines() const { return m_alternate ? m_altLines : m_mainLines; }
    Line &cursorLine() { return activeLines()[m_cursor.row]; }
    Line blankLine() const;
    void clearLine(Line &line) const;
    void blankCells(Line &line, int from, int to);
    void fixWideEdges(Line &line, int from, int to);
    void writeCell(Line &line, int column, char32_t cp, int width);
    void joinPrevious(char32_t cp);
    void scrollRegionUp(int top, int bottom, int count);
    void scrollRegionDown(int top, int bottom, int count);
    void pushHistory(Line &&line);
    Line takeNewestHistory();
    void resetTabStops(int from);
    void markDirty(int row);
    void markDirty(int from, int to);
    char32_t mapCharset(char32_t cp) const;
    int clampColumn(int column) const;

    int m_columns;
    int m_rows;
    QList<Line> m_mainLines;
    QList<Line> m_altLines;
    bool m_alternate = false;

    // The scrollback: a ring of m_historyLimit lines, m_historyStart the oldest.
    QList<Line> m_history;
    int m_historyStart = 0;
    int m_historyCount = 0;
    int m_historyLimit;

    CursorState m_cursor;
    CursorState m_savedMain;
    CursorState m_savedAlt;
    bool m_hasSavedMain = false;
    bool m_hasSavedAlt = false;
    bool m_cursorVisible = true;
    CursorShape m_cursorShape = CursorShape::Block;
    bool m_cursorBlinking = true;
    bool m_autoWrap = true;
    bool m_insertMode = false;
    bool m_newLineMode = false;
    bool m_reverseVideo = false;
    int m_top = 0;
    int m_bottom = 0;
    QList<bool> m_tabStops;

    // The last printed character (REP) and where it went (combining marks join it).
    char32_t m_lastChar = 0;

    QList<QString> m_links;

    QList<bool> m_dirty;
    bool m_allDirty = true;
    bool m_anyDirty = true;
    int m_scrolledIntoHistory = 0;
};

} // namespace brulion::term
