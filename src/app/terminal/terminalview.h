// TerminalView — a TerminalSession's screen drawn and its keyboard, mouse and input method
// (docs/CODE-TERMINAL.md "The view"). A QQuickPaintedItem that repaints only the rows that changed:
// output marks rows of the screen dirty, the view asks for those rows' rectangles and the scene
// graph paints once a frame whatever piled up, so `yes | head -100000` or a full-screen program's
// redraws cost one paint per frame, not one per read.
//
//   TerminalView {
//       session: shell                   // a TerminalSession
//       dark: Theme.dark                 // the default palette: dark or light
//       font.family: Theme.fontMono; font.pixelSize: Theme.textBase
//       background: Theme.bgSunken       // the colours have defaults; set any of them, reset()
//   }
//
// Keys: everything goes to the program (Ctrl+C is ^C) except Ctrl+Shift+C / Ctrl+Shift+V (copy,
// paste; Cmd+C / Cmd+V on macOS), Shift+Insert (paste), Shift+Page Up / Page Down / Home / End
// (the scrollback) and Ctrl+Tab / Ctrl+Shift+Tab (the focus moves on); other Ctrl+Shift chords are
// left to the app's shortcuts. Mouse: drag selects, a double click a word, a triple click a line;
// releasing copies to the primary selection (X11, Wayland), the middle button pastes it; the wheel
// scrolls back — or goes to the program when it asked for the mouse (Shift bypasses that), or
// becomes arrow keys on the alternate screen (less, man). Ctrl+click on an OSC 8 link:
// linkActivated(). The input method's committed text is typed; its preedit shows at the cursor.
//
// Links without OSC 8 (term/links.h): with Ctrl held, the URL or file path under the pointer is
// underlined (looked for in that line only, a path only once the file is found: relative to the
// session's current directory) and Ctrl+click opens it — linkActivated(url), fileActivated(path,
// line, column). The terminal's own keys besides copy and paste: Ctrl+Shift+A selects all,
// Ctrl+Shift+F asks for the find bar (findRequested()), Ctrl+Shift+Home / End jump to the start /
// the latest output. `appShortcuts` are chords the app's shortcuts keep even while a program runs
// (the panel's toggle, split, next terminal…). find() highlights every match in the scrollback and
// the screen (found again a moment after output), findOlder() / findNewer() step through them. A
// right click the program does not take: contextMenuRequested().
#pragma once

#include <QColor>
#include <QFont>
#include <QKeyCombination>
#include <QList>
#include <QPointer>
#include <QQuickPaintedItem>
#include <QString>
#include <QStringList>
#include <QTimer>

#include "terminalsession.h"

#include "term/screentext.h"

namespace brulion::term {
class Screen;
struct Line;
} // namespace brulion::term

namespace brulion::terminal {

class TerminalView : public QQuickPaintedItem {
    Q_OBJECT
    Q_PROPERTY(brulion::terminal::TerminalSession *session READ session WRITE setSession NOTIFY sessionChanged FINAL)
    /// A monospace font (the default: the system's fixed font).
    Q_PROPERTY(QFont font READ font WRITE setFont NOTIFY fontChanged FINAL)
    /// The default colours: the dark palette, else the light one.
    Q_PROPERTY(bool dark READ dark WRITE setDark NOTIFY colorsChanged FINAL)
    Q_PROPERTY(QColor foreground READ foreground WRITE setForeground RESET resetForeground NOTIFY colorsChanged FINAL)
    Q_PROPERTY(QColor background READ background WRITE setBackground RESET resetBackground NOTIFY colorsChanged FINAL)
    Q_PROPERTY(
            QColor cursorColor READ cursorColor WRITE setCursorColor RESET resetCursorColor NOTIFY colorsChanged FINAL)
    /// Drawn over selected cells (a translucent colour keeps the text's colours readable).
    Q_PROPERTY(QColor selectionColor READ selectionColor WRITE setSelectionColor RESET resetSelectionColor NOTIFY
                       colorsChanged FINAL)
    /// The 16 ANSI colours (black, red, green, yellow, blue, magenta, cyan, white, then the bright
    /// ones); fewer than 16: the default palette's.
    Q_PROPERTY(QList<QColor> ansiColors READ ansiColors WRITE setAnsiColors RESET resetAnsiColors NOTIFY colorsChanged
                       FINAL)
    /// Space around the grid.
    Q_PROPERTY(qreal padding READ padding WRITE setPadding NOTIFY layoutChanged FINAL)
    /// The grid that fits (what the session's pty is resized to).
    Q_PROPERTY(int columns READ columns NOTIFY layoutChanged FINAL)
    Q_PROPERTY(int rows READ rows NOTIFY layoutChanged FINAL)
    Q_PROPERTY(qreal cellWidth READ cellWidth NOTIFY layoutChanged FINAL)
    Q_PROPERTY(qreal cellHeight READ cellHeight NOTIFY layoutChanged FINAL)
    /// Lines scrolled back into the scrollback (0: the screen).
    Q_PROPERTY(int scrollOffset READ scrollOffset WRITE setScrollOffset NOTIFY scrollChanged FINAL)
    Q_PROPERTY(int historySize READ historySize NOTIFY scrollChanged FINAL)
    Q_PROPERTY(bool hasSelection READ hasSelection NOTIFY selectionChanged FINAL)
    Q_PROPERTY(QString selectedText READ selectedText NOTIFY selectionChanged FINAL)
    /// The visible text, rows joined with line breaks, for assistive technology (kept up to date
    /// only while one is active, at most a few times a second).
    Q_PROPERTY(QString accessibleText READ accessibleText NOTIFY accessibleTextChanged FINAL)
    /// The program asked for mouse events (the wheel and clicks go to it; Shift selects anyway).
    Q_PROPERTY(bool mouseReporting READ mouseReporting NOTIFY modesChanged FINAL)
    /// Key chords (portable text: "Ctrl+Alt+]") left to the app's shortcuts while a program runs.
    Q_PROPERTY(QStringList appShortcuts READ appShortcuts WRITE setAppShortcuts NOTIFY appShortcutsChanged FINAL)
    /// What a Ctrl+click opens now: the URL or the file's path under the pointer ("": none).
    Q_PROPERTY(QString hoveredLink READ hoveredLink NOTIFY hoveredLinkChanged FINAL)
    /// The find bar's text ("": no search) and its matches: how many, the current one (-1: none).
    Q_PROPERTY(QString findText READ findText NOTIFY findChanged FINAL)
    Q_PROPERTY(int findCount READ findCount NOTIFY findChanged FINAL)
    Q_PROPERTY(int findIndex READ findIndex NOTIFY findChanged FINAL)

public:
    explicit TerminalView(QQuickItem *parent = nullptr);
    ~TerminalView() override;

    TerminalSession *session() const { return m_session; }
    void setSession(TerminalSession *session);
    QFont font() const { return m_font; }
    void setFont(const QFont &font);
    bool dark() const { return m_dark; }
    void setDark(bool dark);
    QColor foreground() const;
    void setForeground(const QColor &color);
    void resetForeground();
    QColor background() const;
    void setBackground(const QColor &color);
    void resetBackground();
    QColor cursorColor() const;
    void setCursorColor(const QColor &color);
    void resetCursorColor();
    QColor selectionColor() const;
    void setSelectionColor(const QColor &color);
    void resetSelectionColor();
    QList<QColor> ansiColors() const;
    void setAnsiColors(const QList<QColor> &colors);
    void resetAnsiColors();
    qreal padding() const { return m_padding; }
    void setPadding(qreal padding);
    int columns() const { return m_columns; }
    int rows() const { return m_rows; }
    qreal cellWidth() const { return m_cellWidth; }
    qreal cellHeight() const { return m_cellHeight; }
    int scrollOffset() const { return m_scrollOffset; }
    void setScrollOffset(int lines);
    int historySize() const;
    bool hasSelection() const { return m_hasSelection; }
    QString selectedText() const;
    QString accessibleText() const { return m_accessibleText; }
    bool mouseReporting() const;
    QStringList appShortcuts() const { return m_appShortcuts; }
    void setAppShortcuts(const QStringList &sequences);
    QString hoveredLink() const { return m_link.valid ? m_link.target : QString(); }
    QString findText() const { return m_findText; }
    int findCount() const { return int(m_matches.size()); }
    int findIndex() const { return m_findCurrent; }

    /// Matches looked for at most.
    static constexpr int MaxMatches = 10000;

    /// The selection to the clipboard.
    Q_INVOKABLE void copy();
    /// The clipboard as a paste.
    Q_INVOKABLE void paste();
    Q_INVOKABLE void selectAll();
    Q_INVOKABLE void clearSelection();
    Q_INVOKABLE void scrollToBottom() { setScrollOffset(0); }
    /// Back (positive) or forward (negative) through the scrollback.
    Q_INVOKABLE void scrollLines(int lines) { setScrollOffset(m_scrollOffset + lines); }
    /// The rows in view, joined with "\n" (trailing spaces dropped).
    Q_INVOKABLE QString visibleText() const;
    /// To the oldest line of the scrollback.
    Q_INVOKABLE void scrollToTop() { setScrollOffset(historySize()); }
    /// The session's "Clear": its scrollback and screen (TerminalSession::clear()).
    Q_INVOKABLE void clear();
    /// Highlights every occurrence of `text` (case-insensitive unless `caseSensitive`) and shows
    /// the newest; their number. "" ends the search.
    Q_INVOKABLE int find(const QString &text, bool caseSensitive);
    /// The match before the current one (up, older output; wrapping), the one after it.
    Q_INVOKABLE void findOlder();
    Q_INVOKABLE void findNewer();
    Q_INVOKABLE void clearFind() { find(QString(), false); }
    /// For the chat (the panel's "Add to chat"): the selection, else the rows in view, as a fenced
    /// block under a line naming the terminal and its folder; "" when there is no text.
    Q_INVOKABLE QString chatText() const;
    /// The block chatText() makes of `text` (trailing blank lines dropped; a fence longer than any
    /// run of backticks in it).
    static QString chatBlock(const QString &title, const QString &folder, const QString &text);

    /// The 16 default ANSI colours of the dark or the light palette.
    static QList<QColor> defaultAnsiColors(bool dark);
    /// The colour of xterm's palette entry 16-255 (the 6x6x6 cube and the greys).
    static QColor xtermColor(int index);

    void paint(QPainter *painter) override;
    QVariant inputMethodQuery(Qt::InputMethodQuery query) const override;

Q_SIGNALS:
    void sessionChanged();
    void fontChanged();
    void colorsChanged();
    void layoutChanged();
    void scrollChanged();
    void selectionChanged();
    void accessibleTextChanged();
    void modesChanged();
    /// Ctrl+click on a hyperlink the program printed (OSC 8) or on a web address in the text.
    void linkActivated(const QString &url);
    /// Ctrl+click on a path of an existing file in the text (or a file: URL): its absolute path,
    /// line and column (1-based; 0: none given).
    void fileActivated(const QString &path, int line, int column);
    /// A right click (or a long press) the program does not take, at (x, y) in the view.
    void contextMenuRequested(qreal x, qreal y);
    /// Ctrl+Shift+F (Cmd+F): the owner opens its find bar.
    void findRequested();
    void appShortcutsChanged();
    void hoveredLinkChanged();
    void findChanged();

protected:
    bool event(QEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;
    void inputMethodEvent(QInputMethodEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseDoubleClickEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void keyReleaseEvent(QKeyEvent *event) override;
    void hoverMoveEvent(QHoverEvent *event) override;
    void hoverLeaveEvent(QHoverEvent *event) override;
    void wheelEvent(QWheelEvent *event) override;
    void focusInEvent(QFocusEvent *event) override;
    void focusOutEvent(QFocusEvent *event) override;
    void geometryChange(const QRectF &newGeometry, const QRectF &oldGeometry) override;
    void itemChange(ItemChange change, const ItemChangeData &value) override;
    void updatePolish() override;

private:
    /// A position in the text: `line` as Screen::lineAt() counts (negative: the scrollback).
    struct TextPos {
        int line = 0;
        int column = 0;
        friend bool operator<(TextPos a, TextPos b)
        {
            return a.line < b.line || (a.line == b.line && a.column < b.column);
        }
        friend bool operator==(TextPos a, TextPos b) = default;
    };
    enum class SelectionUnit { Character, Word, Line };
    /// The link under the pointer (Ctrl held), in Screen::lineAt() lines.
    struct HoverLink {
        bool valid = false;
        bool file = false;
        term::TextRange range;
        QString target; // the URL, or the file's absolute path
        int line = 0;
        int column = 0;
    };

    const term::Screen *screen() const;
    void onContentChanged();
    void onModesChanged();
    void updateMetrics();
    void updateLayout();
    void updateRows(int first, int last);
    void updateCursorArea();
    void updateBlinking();
    void scheduleAccessibleText();
    void resetBlink();
    bool handleShortcut(QKeyEvent *event, bool dryRun);
    QRectF rowRect(int viewRow) const;
    QRectF cursorRect() const;
    TextPos posAt(QPointF point, bool clampToGrid) const;
    QPoint cellAt(QPointF point) const;
    bool sendMouse(int kind, int button, QPointF point, Qt::KeyboardModifiers modifiers);
    void pressAt(QPointF point, Qt::KeyboardModifiers modifiers, ulong timestamp);
    void extendSelection(TextPos to);
    void setSelection(TextPos anchor, TextPos end, SelectionUnit unit);
    TextPos wordStart(TextPos at) const;
    TextPos wordEnd(TextPos at) const;
    TextPos lineStart(TextPos at) const;
    TextPos lineEnd(TextPos at) const;
    void copyToSelectionClipboard();
    void updateShown();
    bool isAppShortcut(const QKeyEvent *event) const;
    void updateLink(QPointF point);
    void clearLink();
    bool linkAt(QPointF point, HoverLink *link) const;
    void activate(const HoverLink &link);
    void updateLines(int first, int last);
    void runFind(bool keepPlace);
    void revealMatch(int index);
    void onCleared();
    void paintMatches(QPainter *painter, int lineIndex, qreal top);
    void applyColorsToSession();
    QColor resolve(quint32 rawColor, bool foreground) const;
    void paintRow(QPainter *painter, int viewRow, const term::Line &line, int lineIndex);
    void paintCursor(QPainter *painter);

    QPointer<TerminalSession> m_session;
    QFont m_font;
    QFont m_fonts[4]; // regular, bold, italic, bold italic: letter-spaced to the cell width
    qreal m_ascent = 0;
    qreal m_underlinePos = 1;
    qreal m_lineWidth = 1;
    qreal m_strikePos = 0;
    bool m_dark = true;
    QColor m_foreground;
    QColor m_background;
    QColor m_cursorColor;
    QColor m_selectionColor;
    QList<QColor> m_ansi;
    QList<QColor> m_palette; // resolved: 16 entries
    qreal m_padding = 4;
    qreal m_cellWidth = 8;
    qreal m_cellHeight = 16;
    int m_columns = 0;
    int m_rows = 0;
    int m_scrollOffset = 0;
    int m_lastHistorySize = 0;

    // Selection, in Screen::lineAt() lines.
    bool m_hasSelection = false;
    bool m_selecting = false;
    TextPos m_selAnchor;
    TextPos m_selEnd;
    TextPos m_selStart; // normalised
    TextPos m_selStop;  // normalised, exclusive column
    SelectionUnit m_selUnit = SelectionUnit::Character;
    int m_clickCount = 0;
    ulong m_lastClickTime = 0;
    QPoint m_lastClickCell{-1, -1};
    int m_reportedButton = -1; // the button the program got a press of
    qreal m_wheelPixels = 0;
    int m_wheelAngle = 0;

    // Cursor and its blinking.
    QTimer m_blinkTimer;
    bool m_blinkOn = true;
    QRectF m_paintedCursor;
    QString m_preedit;

    QTimer m_syncTimer; // the end of a synchronized update (?2026) awaited at most this long
    bool m_flushingSync = false;
    QTimer m_accessibleTimer; // accessibleText at most a few times a second
    QString m_accessibleText;

    bool m_shown = false; // the session counts this view as showing it
    QStringList m_appShortcuts;
    QList<QKeyCombination> m_appKeys;

    // The link under the pointer while Ctrl is held.
    bool m_hovering = false;
    QPointF m_hoverPoint;
    HoverLink m_link;
    term::TextRange m_noLink{{1, 0}, {0, 0}}; // the last word found to be no link (no stat again)

    // The find bar's search.
    QString m_findText;
    bool m_findCase = false;
    QList<term::TextRange> m_matches;
    int m_findCurrent = -1;
    QTimer m_findTimer; // found again a moment after output
};

} // namespace brulion::terminal
