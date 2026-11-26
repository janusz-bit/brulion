#include "terminalview.h"

#include "terminalsession.h"

#include "term/charwidth.h"
#include "term/keys.h"
#include "term/links.h"
#include "term/screen.h"

#include <QAccessible>
#include <QClipboard>
#include <QDir>
#include <QFileInfo>
#include <QKeySequence>
#include <QUrl>
#include <QFontDatabase>
#include <QFontMetricsF>
#include <QGuiApplication>
#include <QInputMethodEvent>
#include <QKeyEvent>
#include <QPainter>
#include <QQuickWindow>
#include <QStyleHints>
#include <QtMath>

#include <algorithm>
#include <cmath>

using namespace Qt::StringLiterals;

namespace brulion::terminal {

namespace {

constexpr int BlinkInterval = 530;
constexpr int SyncTimeout = 150;
constexpr int AccessibleDelay = 300;
/// The find bar's matches are found again this long after output (a flood: a few times a second).
constexpr int FindDelay = 250;
/// Wheel events a notch reports to a program, at most.
constexpr int MaxWheelReports = 10;

QColor defaultForeground(bool dark)
{
    return dark ? QColor(0xd3, 0xd8, 0xe2) : QColor(0x1f, 0x24, 0x30);
}

QColor defaultBackground(bool dark)
{
    return dark ? QColor(0x16, 0x17, 0x1b) : QColor(0xfb, 0xf7, 0xee);
}

QColor defaultCursor(bool dark)
{
    return dark ? QColor(0xff, 0x7a, 0x66) : QColor(0xc4, 0x42, 0x2f);
}

QColor defaultSelection(bool dark)
{
    QColor c(0x9f, 0xc3, 0xe6);
    c.setAlphaF(dark ? 0.30f : 0.55f);
    return c;
}

/// The find bar's matches, and the current one over them.
QColor matchColor(bool dark)
{
    QColor c(0xf2, 0xc9, 0x4c);
    c.setAlphaF(dark ? 0.32f : 0.45f);
    return c;
}

QColor currentMatchColor(bool dark)
{
    QColor c(0xff, 0x8c, 0x42);
    c.setAlphaF(dark ? 0.70f : 0.60f);
    return c;
}

/// `a` moved towards `b` by `t` (0 … 1).
QColor mix(const QColor &a, const QColor &b, qreal t)
{
    return QColor::fromRgbF(float(a.redF() + (b.redF() - a.redF()) * t),
                            float(a.greenF() + (b.greenF() - a.greenF()) * t),
                            float(a.blueF() + (b.blueF() - a.blueF()) * t));
}

/// Characters a double click takes as part of a word: letters, digits and what paths and
/// addresses are made of.
bool isWordChar(QChar c)
{
    return c.isLetterOrNumber() || QStringView(u"_-.~/:@%+#&=?$").contains(c);
}

constexpr quint16 Decorations = term::Underline | term::DoubleUnderline | term::Strikethrough | term::Overline;

} // namespace

TerminalView::TerminalView(QQuickItem *parent) : QQuickPaintedItem(parent)
{
    setFlag(ItemAcceptsInputMethod, true);
    setActiveFocusOnTab(true);
    setAcceptedMouseButtons(Qt::LeftButton | Qt::MiddleButton | Qt::RightButton);
    setCursor(Qt::IBeamCursor);
    setOpaquePainting(true);
    m_font = QFontDatabase::systemFont(QFontDatabase::FixedFont);
    m_palette = defaultAnsiColors(m_dark);
    setFillColor(background());

    m_blinkTimer.setInterval(BlinkInterval);
    connect(&m_blinkTimer, &QTimer::timeout, this, [this] {
        m_blinkOn = !m_blinkOn;
        updateCursorArea();
    });
    m_syncTimer.setSingleShot(true);
    m_syncTimer.setInterval(SyncTimeout);
    connect(&m_syncTimer, &QTimer::timeout, this, [this] {
        m_flushingSync = true;
        onContentChanged();
        m_flushingSync = false;
    });
    m_findTimer.setSingleShot(true);
    m_findTimer.setInterval(FindDelay);
    connect(&m_findTimer, &QTimer::timeout, this, [this] { runFind(true); });
    // (Hover events: links under Ctrl, and the programs that asked for every motion.)
    setAcceptHoverEvents(true);
    m_accessibleTimer.setSingleShot(true);
    m_accessibleTimer.setInterval(AccessibleDelay);
    connect(&m_accessibleTimer, &QTimer::timeout, this, [this] {
        const QString text = visibleText();
        if (text != m_accessibleText) {
            m_accessibleText = text;
            Q_EMIT accessibleTextChanged();
        }
    });
    updateMetrics();
}

TerminalView::~TerminalView()
{
    if (m_session && m_shown)
        m_session->setShown(false);
}

const term::Screen *TerminalView::screen() const
{
    return m_session ? &m_session->terminal().screen() : nullptr;
}

// ---- Properties ---------------------------------------------------------------------------------

void TerminalView::setSession(TerminalSession *session)
{
    if (session == m_session)
        return;
    if (m_session) {
        disconnect(m_session, nullptr, this, nullptr);
        if (m_shown)
            m_session->setShown(false);
    }
    m_shown = false;
    m_session = session;
    m_scrollOffset = 0;
    m_hasSelection = false;
    m_selecting = false;
    m_reportedButton = -1;
    m_preedit.clear();
    m_link = HoverLink();
    m_noLink = {{1, 0}, {0, 0}};
    m_matches.clear();
    m_findCurrent = -1;
    if (session) {
        connect(session, &TerminalSession::contentChanged, this, &TerminalView::onContentChanged);
        connect(session, &TerminalSession::modesChanged, this, &TerminalView::onModesChanged);
        connect(session, &TerminalSession::cleared, this, &TerminalView::onCleared);
        connect(session, &TerminalSession::runningChanged, this, [this] {
            updateBlinking();
            updateCursorArea();
        });
        connect(session, &QObject::destroyed, this, [this] {
            m_session = nullptr;
            m_shown = false;
            m_link = HoverLink();
            m_matches.clear();
            m_findCurrent = -1;
            update();
            Q_EMIT sessionChanged();
            Q_EMIT scrollChanged();
            Q_EMIT findChanged();
        });
        if (m_columns > 0 && m_rows > 0)
            session->resize(m_columns, m_rows);
        applyColorsToSession();
        session->terminal().screen().markAllDirty();
        m_lastHistorySize = session->terminal().screen().historySize();
    }
    onModesChanged();
    updateBlinking();
    updateShown();
    if (!m_findText.isEmpty())
        runFind(false);
    update();
    Q_EMIT sessionChanged();
    Q_EMIT scrollChanged();
    Q_EMIT selectionChanged();
    Q_EMIT hoveredLinkChanged();
    Q_EMIT findChanged();
    scheduleAccessibleText();
}

void TerminalView::updateShown()
{
    // Shown: in a window and visible (a panel hidden, a tab not current, Code mode left: not).
    const bool shown = m_session && isVisible() && window();
    if (shown == m_shown)
        return;
    m_shown = shown;
    m_session->setShown(shown);
}

void TerminalView::setAppShortcuts(const QStringList &sequences)
{
    if (sequences == m_appShortcuts)
        return;
    m_appShortcuts = sequences;
    m_appKeys.clear();
    for (const QString &text : sequences) {
        const QKeySequence sequence = QKeySequence::fromString(text, QKeySequence::PortableText);
        if (sequence.count() == 1)
            m_appKeys.append(sequence[0]);
    }
    Q_EMIT appShortcutsChanged();
}

bool TerminalView::isAppShortcut(const QKeyEvent *event) const
{
    if (m_appKeys.isEmpty())
        return false;
    const Qt::KeyboardModifiers mods =
            event->modifiers() & (Qt::ShiftModifier | Qt::ControlModifier | Qt::AltModifier | Qt::MetaModifier);
    const QKeyCombination pressed(mods, Qt::Key(event->key()));
    return m_appKeys.contains(pressed);
}

void TerminalView::setFont(const QFont &font)
{
    if (font == m_font)
        return;
    m_font = font;
    updateMetrics();
    Q_EMIT fontChanged();
}

void TerminalView::setDark(bool dark)
{
    if (dark == m_dark)
        return;
    m_dark = dark;
    if (m_ansi.size() < 16)
        m_palette = defaultAnsiColors(m_dark);
    setFillColor(background());
    applyColorsToSession();
    update();
    Q_EMIT colorsChanged();
}

QColor TerminalView::foreground() const
{
    return m_foreground.isValid() ? m_foreground : defaultForeground(m_dark);
}

void TerminalView::setForeground(const QColor &color)
{
    if (color == m_foreground)
        return;
    m_foreground = color;
    applyColorsToSession();
    update();
    Q_EMIT colorsChanged();
}

void TerminalView::resetForeground()
{
    setForeground(QColor());
}

QColor TerminalView::background() const
{
    return m_background.isValid() ? m_background : defaultBackground(m_dark);
}

void TerminalView::setBackground(const QColor &color)
{
    if (color == m_background)
        return;
    m_background = color;
    setFillColor(background());
    applyColorsToSession();
    update();
    Q_EMIT colorsChanged();
}

void TerminalView::resetBackground()
{
    setBackground(QColor());
}

QColor TerminalView::cursorColor() const
{
    return m_cursorColor.isValid() ? m_cursorColor : defaultCursor(m_dark);
}

void TerminalView::setCursorColor(const QColor &color)
{
    if (color == m_cursorColor)
        return;
    m_cursorColor = color;
    applyColorsToSession();
    updateCursorArea();
    Q_EMIT colorsChanged();
}

void TerminalView::resetCursorColor()
{
    setCursorColor(QColor());
}

QColor TerminalView::selectionColor() const
{
    return m_selectionColor.isValid() ? m_selectionColor : defaultSelection(m_dark);
}

void TerminalView::setSelectionColor(const QColor &color)
{
    if (color == m_selectionColor)
        return;
    m_selectionColor = color;
    if (m_hasSelection)
        update();
    Q_EMIT colorsChanged();
}

void TerminalView::resetSelectionColor()
{
    setSelectionColor(QColor());
}

QList<QColor> TerminalView::ansiColors() const
{
    return m_palette;
}

void TerminalView::setAnsiColors(const QList<QColor> &colors)
{
    if (colors == m_ansi)
        return;
    m_ansi = colors;
    m_palette = m_ansi.size() >= 16 ? m_ansi.first(16) : defaultAnsiColors(m_dark);
    update();
    Q_EMIT colorsChanged();
}

void TerminalView::resetAnsiColors()
{
    setAnsiColors({});
}

void TerminalView::setPadding(qreal padding)
{
    padding = std::max<qreal>(0, padding);
    if (qFuzzyCompare(padding, m_padding))
        return;
    m_padding = padding;
    updateLayout();
    update();
    Q_EMIT layoutChanged();
}

void TerminalView::setScrollOffset(int lines)
{
    lines = std::clamp(lines, 0, historySize());
    if (lines == m_scrollOffset)
        return;
    m_scrollOffset = lines;
    update();
    Q_EMIT scrollChanged();
}

int TerminalView::historySize() const
{
    const term::Screen *s = screen();
    return s ? s->historySize() : 0;
}

bool TerminalView::mouseReporting() const
{
    return m_session && m_session->terminal().modes().mouseTracking != term::MouseTracking::Off;
}

void TerminalView::applyColorsToSession()
{
    if (m_session)
        m_session->terminal().setReportedColors(foreground().rgb() & 0xffffff, background().rgb() & 0xffffff,
                                                cursorColor().rgb() & 0xffffff);
}

QList<QColor> TerminalView::defaultAnsiColors(bool dark)
{
    // Readable on the default backgrounds (#16171b, #fbf7ee): the dark palette's colours are light,
    // the light palette's dark, "white" in the light one a mid grey.
    static const char *const darkColors[16] = {"#2a2c33", "#e5737b", "#98c379", "#e5c07b", "#6cb0f0", "#c88ae0",
                                               "#5fc0cb", "#c9ced8", "#5c6270", "#ff8c8c", "#b5e08f", "#f2d49a",
                                               "#8fc5f7", "#dba8ef", "#85d6df", "#f2f4f8"};
    static const char *const lightColors[16] = {"#2a2c33", "#c0392b", "#2e7d32", "#9a6700", "#1f5fbf", "#8e44ad",
                                                "#00838f", "#8c8f99", "#5c6270", "#e04a3a", "#3f9a44", "#b07d00",
                                                "#3a7be0", "#a35bc4", "#1a9aa6", "#b4b7bf"};
    QList<QColor> out;
    out.reserve(16);
    for (const char *name : dark ? darkColors : lightColors)
        out.append(QColor::fromString(QLatin1StringView(name)));
    return out;
}

QColor TerminalView::xtermColor(int index)
{
    if (index >= 16 && index < 232) {
        const int i = index - 16;
        auto level = [](int v) { return v == 0 ? 0 : 55 + v * 40; };
        return QColor(level(i / 36), level((i / 6) % 6), level(i % 6));
    }
    if (index >= 232 && index < 256) {
        const int grey = 8 + (index - 232) * 10;
        return QColor(grey, grey, grey);
    }
    return QColor();
}

QColor TerminalView::resolve(quint32 rawColor, bool isForeground) const
{
    const quint32 kind = rawColor >> 24;
    if (kind == term::Color::Indexed) {
        const int index = int(rawColor & 0xff);
        return index < 16 ? m_palette.value(index) : xtermColor(index);
    }
    if (kind == term::Color::Rgb)
        return QColor::fromRgb(QRgb(0xff000000u | (rawColor & 0xffffff)));
    const term::Screen *s = screen();
    const bool swapped = s && s->reverseVideo();
    return isForeground != swapped ? foreground() : background();
}

// ---- Metrics and layout -------------------------------------------------------------------------

void TerminalView::updateMetrics()
{
    const qreal dpr = window() ? window()->effectiveDevicePixelRatio() : 1.0;
    QFont base = m_font;
    base.setKerning(false);
    base.setStyleHint(QFont::Monospace, QFont::PreferDefault);
    base.setLetterSpacing(QFont::AbsoluteSpacing, 0);
    const QFontMetricsF metrics(base);
    qreal advance = metrics.horizontalAdvance(u'M');
    if (advance <= 0)
        advance = metrics.averageCharWidth();
    m_cellWidth = std::max<qreal>(1, std::round(advance * dpr) / dpr);
    m_cellHeight = std::max<qreal>(1, std::ceil(metrics.height() * dpr) / dpr);
    m_ascent = std::round((metrics.ascent() + (m_cellHeight - metrics.height()) / 2) * dpr) / dpr;
    m_underlinePos = std::max<qreal>(1, metrics.underlinePos());
    m_lineWidth = std::max<qreal>(1, metrics.lineWidth());
    m_strikePos = metrics.strikeOutPos();
    for (int i = 0; i < 4; ++i) {
        QFont font = base;
        font.setBold(i & 1);
        font.setItalic(i & 2);
        const qreal own = QFontMetricsF(font).horizontalAdvance(u'M');
        // Every glyph's advance becomes the cell's width: a run of text lines up with the grid.
        font.setLetterSpacing(QFont::AbsoluteSpacing, m_cellWidth - own);
        m_fonts[i] = font;
    }
    updateLayout();
    update();
    Q_EMIT layoutChanged();
}

void TerminalView::updateLayout()
{
    const int columns = int(std::floor((width() - 2 * m_padding) / m_cellWidth));
    const int rows = int(std::floor((height() - 2 * m_padding) / m_cellHeight));
    // Too small to show a cell (hidden, being laid out): the program keeps its size.
    if (columns < 1 || rows < 1)
        return;
    if (columns == m_columns && rows == m_rows)
        return;
    m_columns = columns;
    m_rows = rows;
    if (m_session)
        m_session->resize(columns, rows);
    update();
    Q_EMIT layoutChanged();
}

void TerminalView::geometryChange(const QRectF &newGeometry, const QRectF &oldGeometry)
{
    QQuickPaintedItem::geometryChange(newGeometry, oldGeometry);
    if (newGeometry.size() == oldGeometry.size())
        return;
    // Once a frame however often a window resize asks: the program redraws on each SIGWINCH.
    if (window())
        polish();
    else
        updateLayout();
}

void TerminalView::updatePolish()
{
    updateLayout();
}

void TerminalView::itemChange(ItemChange change, const ItemChangeData &value)
{
    QQuickPaintedItem::itemChange(change, value);
    if (change == ItemDevicePixelRatioHasChanged || change == ItemSceneChange)
        updateMetrics();
    if (change == ItemVisibleHasChanged || change == ItemSceneChange)
        updateShown();
}

QRectF TerminalView::rowRect(int viewRow) const
{
    return QRectF(0, m_padding + viewRow * m_cellHeight, width(), m_cellHeight);
}

QRectF TerminalView::cursorRect() const
{
    const term::Screen *s = screen();
    if (!s)
        return {};
    const int viewRow = s->cursorRow() + m_scrollOffset;
    if (viewRow >= s->rows())
        return {};
    const int column = s->cursorColumn();
    const term::Line &line = s->line(s->cursorRow());
    const bool wide = column < line.cells.size() && line.cells.at(column).isWide();
    return QRectF(m_padding + column * m_cellWidth, m_padding + viewRow * m_cellHeight, m_cellWidth * (wide ? 2 : 1),
                  m_cellHeight);
}

void TerminalView::updateRows(int first, int last)
{
    if (first > last)
        return;
    update(QRectF(0, m_padding + first * m_cellHeight, width(), (last - first + 1) * m_cellHeight).toAlignedRect());
}

void TerminalView::updateCursorArea()
{
    QRectF area = m_paintedCursor;
    const QRectF now = cursorRect();
    if (!now.isEmpty()) {
        // The preedit text may be wider than the cell.
        const QRectF row(0, now.top(), width(), now.height());
        area = area.isEmpty() ? (m_preedit.isEmpty() ? now : row) : area.united(m_preedit.isEmpty() ? now : row);
    }
    if (!area.isEmpty())
        update(area.adjusted(-1, -1, 1, 1).toAlignedRect());
}

void TerminalView::updateBlinking()
{
    const term::Screen *s = screen();
    const bool blinking = hasActiveFocus() && s && s->cursorBlinking() && s->cursorVisible() && m_session->isRunning();
    if (blinking && !m_blinkTimer.isActive()) {
        m_blinkOn = true;
        m_blinkTimer.start();
    } else if (!blinking && m_blinkTimer.isActive()) {
        m_blinkTimer.stop();
        m_blinkOn = true;
        updateCursorArea();
    }
}

void TerminalView::resetBlink()
{
    m_blinkOn = true;
    if (m_blinkTimer.isActive())
        m_blinkTimer.start();
    updateCursorArea();
}

void TerminalView::scheduleAccessibleText()
{
    if (QAccessible::isActive() && !m_accessibleTimer.isActive())
        m_accessibleTimer.start();
}

// ---- Output -------------------------------------------------------------------------------------

void TerminalView::onContentChanged()
{
    if (!m_session)
        return;
    term::Screen &s = m_session->terminal().screen();
    if (m_session->terminal().modes().synchronizedOutput && !m_flushingSync) {
        // The program is drawing a frame (?2026): paint when it says it is done, or a little later.
        if (!m_syncTimer.isActive())
            m_syncTimer.start();
        return;
    }
    const int scrolled = s.scrolledIntoHistory();
    const int history = s.historySize();
    bool all = false;
    if (m_hasSelection) {
        if (scrolled > 0) {
            // The selected text moved up with the lines.
            m_selAnchor.line -= scrolled;
            m_selEnd.line -= scrolled;
            m_selStart.line -= scrolled;
            m_selStop.line -= scrolled;
            if (m_selStart.line < -history)
                clearSelection();
        } else {
            // Output over the selected text ends the selection.
            for (int row = std::max(0, m_selStart.line); row <= std::min(s.rows() - 1, m_selStop.line); ++row) {
                if (s.isRowDirty(row)) {
                    clearSelection();
                    break;
                }
            }
        }
    }
    if (m_link.valid) {
        // A link under the pointer whose text moved or changed: looked for again on the next move.
        bool changed = scrolled > 0;
        for (int row = std::max(0, m_link.range.start.line); !changed && row <= std::min(s.rows() - 1, m_link.range.end.line);
             ++row)
            changed = s.isRowDirty(row);
        if (changed)
            clearLink();
    }
    if (scrolled > 0 || s.isRowDirty(std::clamp(m_noLink.start.line, 0, s.rows() - 1)))
        m_noLink = {{1, 0}, {0, 0}};
    if (!m_findText.isEmpty()) {
        // The matches move up with their lines until they are found again (a moment later).
        if (scrolled > 0) {
            for (term::TextRange &match : m_matches) {
                match.start.line -= scrolled;
                match.end.line -= scrolled;
            }
            qsizetype gone = 0;
            while (gone < m_matches.size() && m_matches.at(gone).start.line < -history)
                ++gone;
            if (gone > 0) {
                m_matches.remove(0, gone);
                m_findCurrent = m_matches.isEmpty() ? -1 : std::max(0, m_findCurrent - int(gone));
                Q_EMIT findChanged();
            }
        }
        if (!m_findTimer.isActive())
            m_findTimer.start();
    }
    if (m_scrollOffset > 0) {
        // Scrolled back: the view keeps showing the same lines while new ones arrive below.
        const int offset = std::min(m_scrollOffset + scrolled, history);
        m_scrollOffset = offset;
        all = true;
    }
    if (history != m_lastHistorySize || all) {
        m_lastHistorySize = history;
        Q_EMIT scrollChanged();
    }
    if (all) {
        update();
    } else {
        int first = -1;
        for (int row = 0; row < s.rows(); ++row) {
            if (s.isRowDirty(row)) {
                if (first < 0)
                    first = row;
            } else if (first >= 0) {
                updateRows(first, row - 1);
                first = -1;
            }
        }
        if (first >= 0)
            updateRows(first, s.rows() - 1);
    }
    s.clearDirty();
    updateCursorArea();
    updateBlinking();
    scheduleAccessibleText();
}

void TerminalView::onModesChanged()
{
    if (m_session && !m_session->terminal().modes().synchronizedOutput && m_syncTimer.isActive()) {
        m_syncTimer.stop();
        onContentChanged();
    }
    Q_EMIT modesChanged();
}

// ---- Painting -----------------------------------------------------------------------------------

void TerminalView::paint(QPainter *painter)
{
    m_paintedCursor = QRectF();
    const term::Screen *s = screen();
    if (!s)
        return;
    const QRectF clip = painter->hasClipping() ? painter->clipBoundingRect() : QRectF(0, 0, width(), height());
    const int first = std::max(0, int(std::floor((clip.top() - m_padding) / m_cellHeight)));
    const int last = std::min(s->rows() - 1, int(std::floor((clip.bottom() - m_padding) / m_cellHeight)));
    for (int viewRow = first; viewRow <= last; ++viewRow) {
        const int lineIndex = viewRow - m_scrollOffset;
        if (lineIndex < -s->historySize())
            continue;
        paintRow(painter, viewRow, s->lineAt(lineIndex), lineIndex);
    }
    paintCursor(painter);
}

void TerminalView::paintRow(QPainter *painter, int viewRow, const term::Line &line, int lineIndex)
{
    const term::Screen *s = screen();
    const qreal top = m_padding + viewRow * m_cellHeight;
    const qreal baseline = top + m_ascent;
    const int columns = std::min(int(line.cells.size()), s->columns());
    const QColor rowBackground = resolve(0, false);
    if (rowBackground != fillColor())
        painter->fillRect(QRectF(m_padding, top, s->columns() * m_cellWidth, m_cellHeight), rowBackground);

    auto colorsOf = [this](const term::Cell &cell, QColor *fg, QColor *bg) {
        *fg = resolve(cell.fg.raw(), true);
        *bg = resolve(cell.bg.raw(), false);
        if (cell.attrs & term::Inverse)
            std::swap(*fg, *bg);
        if (cell.attrs & term::Dim)
            *fg = mix(*fg, *bg, 0.45);
        if (cell.attrs & term::Invisible)
            *fg = *bg;
    };

    // Backgrounds, a run of equal ones at a time.
    for (int column = 0; column < columns;) {
        QColor fg, bg;
        colorsOf(line.cells.at(column), &fg, &bg);
        int end = column + 1;
        while (end < columns) {
            QColor nextFg, nextBg;
            colorsOf(line.cells.at(end), &nextFg, &nextBg);
            if (nextBg != bg)
                break;
            ++end;
        }
        if (bg != rowBackground)
            painter->fillRect(QRectF(m_padding + column * m_cellWidth, top, (end - column) * m_cellWidth, m_cellHeight),
                              bg);
        column = end;
    }

    // The find bar's matches, then the selection over them.
    if (!m_matches.isEmpty())
        paintMatches(painter, lineIndex, top);
    if (m_hasSelection && lineIndex >= m_selStart.line && lineIndex <= m_selStop.line) {
        const int from = lineIndex == m_selStart.line ? m_selStart.column : 0;
        const int to = lineIndex == m_selStop.line ? m_selStop.column : s->columns();
        if (to > from)
            painter->fillRect(QRectF(m_padding + from * m_cellWidth, top, (to - from) * m_cellWidth, m_cellHeight),
                              selectionColor());
    }

    auto decorate = [&](qreal x, qreal width, quint16 attrs, const QColor &color) {
        if (!(attrs & Decorations))
            return;
        if (attrs & term::Underline)
            painter->fillRect(QRectF(x, baseline + m_underlinePos, width, m_lineWidth), color);
        if (attrs & term::DoubleUnderline) {
            painter->fillRect(QRectF(x, baseline + m_underlinePos, width, m_lineWidth), color);
            painter->fillRect(QRectF(x, baseline + m_underlinePos + 2 * m_lineWidth, width, m_lineWidth), color);
        }
        if (attrs & term::Strikethrough)
            painter->fillRect(QRectF(x, baseline - m_strikePos, width, m_lineWidth), color);
        if (attrs & term::Overline)
            painter->fillRect(QRectF(x, top, width, m_lineWidth), color);
    };

    // Text: runs of narrow Latin cells in one style drawn at once (the fonts' advance is the cell's
    // width), anything else — wide characters, clusters, other scripts — a cell at a time.
    QString run;
    int runStart = 0;
    bool runInk = false;
    QColor runColor;
    int runFont = 0;
    quint16 runAttrs = 0;
    auto flush = [&] {
        if (run.isEmpty())
            return;
        if (runInk) {
            painter->setFont(m_fonts[runFont]);
            painter->setPen(runColor);
            painter->drawText(QPointF(m_padding + runStart * m_cellWidth, baseline), run);
        }
        decorate(m_padding + runStart * m_cellWidth, run.size() * m_cellWidth, runAttrs, runColor);
        run.clear();
        runInk = false;
    };
    for (int column = 0; column < columns; ++column) {
        const term::Cell &cell = line.cells.at(column);
        if (cell.isWideTail())
            continue;
        QColor fg, bg;
        colorsOf(cell, &fg, &bg);
        const int font = ((cell.attrs & term::Bold) ? 1 : 0) | ((cell.attrs & term::Italic) ? 2 : 0);
        const quint16 attrs = cell.attrs & Decorations;
        const bool simple = !(cell.attrs & (term::WideFlag | term::ClusterFlag)) && cell.ch >= 0x20 && cell.ch < 0x250
                            && cell.ch != 0x7f && term::charWidth(cell.ch) == 1;
        if (simple) {
            if (!run.isEmpty()
                && (runStart + run.size() != column || fg != runColor || font != runFont || attrs != runAttrs))
                flush();
            if (run.isEmpty()) {
                runStart = column;
                runColor = fg;
                runFont = font;
                runAttrs = attrs;
            }
            run.append(QChar(char16_t(cell.ch)));
            runInk = runInk || cell.ch != U' ';
            continue;
        }
        flush();
        const qreal x = m_padding + column * m_cellWidth;
        const qreal cellWidth = m_cellWidth * (cell.isWide() ? 2 : 1);
        painter->setFont(m_fonts[font]);
        painter->setPen(fg);
        painter->drawText(QPointF(x, baseline), line.cellText(column));
        decorate(x, cellWidth, attrs, fg);
    }
    flush();

    // The link under the pointer (Ctrl held), underlined.
    if (m_link.valid && lineIndex >= m_link.range.start.line && lineIndex <= m_link.range.end.line) {
        const int from = lineIndex == m_link.range.start.line ? m_link.range.start.column : 0;
        const int to = lineIndex == m_link.range.end.line ? m_link.range.end.column : s->columns();
        if (to > from)
            painter->fillRect(QRectF(m_padding + from * m_cellWidth, baseline + m_underlinePos,
                                     (to - from) * m_cellWidth, m_lineWidth),
                              cursorColor());
    }
}

void TerminalView::paintMatches(QPainter *painter, int lineIndex, qreal top)
{
    const term::Screen *s = screen();
    // The first match that ends on this line or later (the matches are in order and apart).
    const auto first = std::lower_bound(m_matches.cbegin(), m_matches.cend(), lineIndex,
                                        [](const term::TextRange &m, int line) { return m.end.line < line; });
    for (auto it = first; it != m_matches.cend() && it->start.line <= lineIndex; ++it) {
        const int from = lineIndex == it->start.line ? it->start.column : 0;
        const int to = lineIndex == it->end.line ? it->end.column : s->columns();
        if (to <= from)
            continue;
        const bool current = it - m_matches.cbegin() == m_findCurrent;
        painter->fillRect(QRectF(m_padding + from * m_cellWidth, top, (to - from) * m_cellWidth, m_cellHeight),
                          current ? currentMatchColor(m_dark) : matchColor(m_dark));
    }
}

void TerminalView::paintCursor(QPainter *painter)
{
    const term::Screen *s = screen();
    if (!s)
        return;
    const QRectF rect = cursorRect();
    if (rect.isEmpty())
        return;
    const bool focused = hasActiveFocus();
    if (!m_preedit.isEmpty()) {
        // The input method's text being composed, underlined at the cursor.
        int cells = 0;
        for (const char32_t cp : m_preedit.toUcs4())
            cells += term::charWidth(cp);
        const QRectF area(rect.left(), rect.top(), std::max(1, cells) * m_cellWidth, m_cellHeight);
        painter->fillRect(area, background());
        painter->setFont(m_fonts[0]);
        painter->setPen(foreground());
        painter->drawText(QPointF(area.left(), area.top() + m_ascent), m_preedit);
        painter->fillRect(QRectF(area.left(), area.top() + m_ascent + m_underlinePos, area.width(), m_lineWidth),
                          foreground());
        m_paintedCursor = QRectF(0, rect.top(), width(), rect.height());
        return;
    }
    // No cursor once the program ended (nothing reads the keys any more) or before it started.
    if (!s->cursorVisible() || !m_session->isRunning())
        return;
    m_paintedCursor = rect;
    if (!focused) {
        painter->setPen(QPen(cursorColor(), 1));
        painter->setBrush(Qt::NoBrush);
        painter->drawRect(rect.adjusted(0.5, 0.5, -0.5, -0.5));
        return;
    }
    if (s->cursorBlinking() && !m_blinkOn)
        return;
    switch (s->cursorShape()) {
    case term::CursorShape::Block: {
        painter->fillRect(rect, cursorColor());
        const term::Line &line = s->line(s->cursorRow());
        const int column = s->cursorColumn();
        if (column < line.cells.size()) {
            const term::Cell &cell = line.cells.at(column);
            const int font = ((cell.attrs & term::Bold) ? 1 : 0) | ((cell.attrs & term::Italic) ? 2 : 0);
            painter->setFont(m_fonts[font]);
            painter->setPen(background());
            painter->drawText(QPointF(rect.left(), rect.top() + m_ascent), line.cellText(column));
        }
        break;
    }
    case term::CursorShape::Underline:
        painter->fillRect(QRectF(rect.left(), rect.bottom() - 2, rect.width(), 2), cursorColor());
        break;
    case term::CursorShape::Bar:
        painter->fillRect(QRectF(rect.left(), rect.top(), 2, rect.height()), cursorColor());
        break;
    }
}

// ---- Keyboard and input method ------------------------------------------------------------------

bool TerminalView::handleShortcut(QKeyEvent *event, bool dryRun)
{
    const Qt::KeyboardModifiers mods =
            event->modifiers() & (Qt::ShiftModifier | Qt::ControlModifier | Qt::AltModifier | Qt::MetaModifier);
    const int key = event->key();
#ifdef Q_OS_MACOS
    if (event->matches(QKeySequence::Copy)) {
        if (!dryRun)
            copy();
        return true;
    }
    if (event->matches(QKeySequence::Paste)) {
        if (!dryRun)
            paste();
        return true;
    }
    if (event->matches(QKeySequence::SelectAll)) {
        if (!dryRun)
            selectAll();
        return true;
    }
    if (event->matches(QKeySequence::Find)) {
        if (!dryRun)
            Q_EMIT findRequested();
        return true;
    }
#endif
    // The terminal's own Ctrl+Shift chords (T3 Code's on Linux and Windows): select all, find, the
    // start and the end of the scrollback. (Ctrl+A, Ctrl+F stay the shell's.)
    if (mods == (Qt::ControlModifier | Qt::ShiftModifier)) {
        switch (key) {
        case Qt::Key_A:
            if (!dryRun)
                selectAll();
            return true;
        case Qt::Key_F:
            if (!dryRun)
                Q_EMIT findRequested();
            return true;
        case Qt::Key_Home:
            if (!dryRun)
                scrollToTop();
            return true;
        case Qt::Key_End:
            if (!dryRun)
                scrollToBottom();
            return true;
        default: break;
        }
    }
    if (mods == (Qt::ControlModifier | Qt::ShiftModifier) && key == Qt::Key_C) {
        if (!dryRun)
            copy();
        return true;
    }
    if ((mods == (Qt::ControlModifier | Qt::ShiftModifier) && key == Qt::Key_V)
        || (mods == Qt::ShiftModifier && key == Qt::Key_Insert)) {
        if (!dryRun)
            paste();
        return true;
    }
    if (mods == Qt::ShiftModifier && historySize() > 0) {
        const int page = std::max(1, (screen() ? screen()->rows() : 1) - 1);
        switch (key) {
        case Qt::Key_PageUp:
            if (!dryRun)
                scrollLines(page);
            return true;
        case Qt::Key_PageDown:
            if (!dryRun)
                scrollLines(-page);
            return true;
        case Qt::Key_Home:
            if (!dryRun)
                setScrollOffset(historySize());
            return true;
        case Qt::Key_End:
            if (!dryRun)
                scrollToBottom();
            return true;
        default: break;
        }
    }
    return false;
}

bool TerminalView::event(QEvent *event)
{
    if (event->type() == QEvent::ShortcutOverride && m_session) {
        auto *key = static_cast<QKeyEvent *>(event);
        const Qt::KeyboardModifiers mods = key->modifiers();
        bool wanted = true;
        if (handleShortcut(key, true))
            wanted = true;
        else if (isAppShortcut(key))
            wanted = false; // the panel's own commands (toggle, split, next terminal…)
        else if (!m_session->isRunning())
            wanted = false; // nothing reads the keys: the app's shortcuts work
        else if ((mods & Qt::ControlModifier) && (mods & Qt::ShiftModifier))
            wanted = false; // the app's Ctrl+Shift chords
        else if ((mods & Qt::ControlModifier) && (key->key() == Qt::Key_Tab || key->key() == Qt::Key_Backtab))
            wanted = false;
#ifdef Q_OS_MACOS
        else if (mods & Qt::ControlModifier)
            wanted = false; // Command chords are the app's; Control is the terminal's
#endif
        if (wanted) {
            key->accept();
            return true;
        }
    }
    return QQuickPaintedItem::event(event);
}

void TerminalView::keyPressEvent(QKeyEvent *event)
{
    if (!m_session) {
        event->ignore();
        return;
    }
    if (event->key() == Qt::Key_Control || event->key() == Qt::Key_Meta) {
        // Ctrl pressed over a link: underlined at once, before the pointer moves.
        if (m_hovering)
            updateLink(m_hoverPoint);
        event->ignore();
        return;
    }
    if (handleShortcut(event, false)) {
        event->accept();
        return;
    }
    if (!m_session->isRunning()) {
        event->ignore();
        return;
    }
    Qt::KeyboardModifiers mods = event->modifiers();
#ifdef Q_OS_MACOS
    // Qt's Control is the Command key on macOS and its Meta the Control key: Control+C is ^C there too.
    const bool command = mods & Qt::ControlModifier;
    const bool control = mods & Qt::MetaModifier;
    mods &= ~(Qt::ControlModifier | Qt::MetaModifier);
    if (control)
        mods |= Qt::ControlModifier;
    if (command)
        mods |= Qt::MetaModifier;
    // Option composes characters (ą, ż on a Polish layout): with a character it is no Alt.
    if ((mods & Qt::AltModifier) && !event->text().isEmpty() && event->text().at(0).unicode() >= 0x20)
        mods &= ~Qt::AltModifier;
#endif
    const QByteArray bytes = term::encodeKey(event->key(), mods, event->text(), m_session->terminal().modes());
    if (bytes.isEmpty()) {
        event->ignore();
        return;
    }
    m_session->writeBytes(bytes);
    scrollToBottom();
    resetBlink();
    event->accept();
}

void TerminalView::keyReleaseEvent(QKeyEvent *event)
{
    if (event->key() == Qt::Key_Control || event->key() == Qt::Key_Meta)
        clearLink();
    QQuickPaintedItem::keyReleaseEvent(event);
}

void TerminalView::inputMethodEvent(QInputMethodEvent *event)
{
    if (!m_session) {
        event->ignore();
        return;
    }
    const QString commit = event->commitString();
    if (!commit.isEmpty()) {
        m_session->writeBytes(term::encodeText(commit));
        scrollToBottom();
    }
    if (event->preeditString() != m_preedit) {
        updateCursorArea();
        m_preedit = event->preeditString();
        updateCursorArea();
    }
    event->accept();
}

QVariant TerminalView::inputMethodQuery(Qt::InputMethodQuery query) const
{
    switch (query) {
    case Qt::ImEnabled: return true;
    case Qt::ImCursorRectangle:
    case Qt::ImAnchorRectangle: {
        const QRectF rect = cursorRect();
        return rect.isEmpty() ? QRectF(m_padding, m_padding, m_cellWidth, m_cellHeight) : rect;
    }
    case Qt::ImHints: return int(Qt::ImhNoPredictiveText | Qt::ImhNoAutoUppercase | Qt::ImhMultiLine);
    case Qt::ImFont: return m_font;
    case Qt::ImCursorPosition:
    case Qt::ImAnchorPosition: return 0;
    case Qt::ImSurroundingText:
    case Qt::ImCurrentSelection: return QString();
    default: return QQuickPaintedItem::inputMethodQuery(query);
    }
}

void TerminalView::focusInEvent(QFocusEvent *event)
{
    QQuickPaintedItem::focusInEvent(event);
    if (m_session)
        m_session->writeBytes(term::encodeFocus(true, m_session->terminal().modes()));
    updateBlinking();
    resetBlink();
}

void TerminalView::focusOutEvent(QFocusEvent *event)
{
    QQuickPaintedItem::focusOutEvent(event);
    if (m_session)
        m_session->writeBytes(term::encodeFocus(false, m_session->terminal().modes()));
    updateBlinking();
    updateCursorArea();
}

// ---- Mouse and selection ------------------------------------------------------------------------

QPoint TerminalView::cellAt(QPointF point) const
{
    const term::Screen *s = screen();
    const int columns = s ? s->columns() : std::max(1, m_columns);
    const int rows = s ? s->rows() : std::max(1, m_rows);
    const int column = std::clamp(int(std::floor((point.x() - m_padding) / m_cellWidth)), 0, columns - 1);
    const int row = std::clamp(int(std::floor((point.y() - m_padding) / m_cellHeight)), 0, rows - 1);
    return QPoint(column, row);
}

TerminalView::TextPos TerminalView::posAt(QPointF point, bool clampToGrid) const
{
    const term::Screen *s = screen();
    const int columns = s ? s->columns() : std::max(1, m_columns);
    const int rows = s ? s->rows() : std::max(1, m_rows);
    int row = int(std::floor((point.y() - m_padding) / m_cellHeight));
    if (clampToGrid)
        row = std::clamp(row, 0, rows - 1);
    // Between cells: the boundary nearest to the pointer.
    const int column = std::clamp(int(std::lround((point.x() - m_padding) / m_cellWidth)), 0, columns);
    TextPos pos{row - m_scrollOffset, column};
    const int history = s ? s->historySize() : 0;
    pos.line = std::clamp(pos.line, -history, rows - 1);
    return pos;
}

bool TerminalView::sendMouse(int kind, int button, QPointF point, Qt::KeyboardModifiers modifiers)
{
    if (!m_session)
        return false;
    const QPoint cell = cellAt(point);
    term::MouseEvent event;
    event.kind = term::MouseEvent::Kind(kind);
    event.button = button;
    event.column = cell.x();
    event.row = cell.y() - m_scrollOffset;
    event.modifiers = modifiers;
    const term::InputModes &modes = m_session->terminal().modes();
    if (!term::mouseWanted(event, modes))
        return false;
    m_session->writeBytes(term::encodeMouse(event, modes));
    return true;
}

void TerminalView::mousePressEvent(QMouseEvent *event)
{
    if (!m_session) {
        event->ignore();
        return;
    }
    forceActiveFocus(Qt::MouseFocusReason);
    const Qt::KeyboardModifiers mods = event->modifiers();
    const int button = event->button() == Qt::LeftButton     ? 0
                       : event->button() == Qt::MiddleButton ? 1
                       : event->button() == Qt::RightButton  ? 2
                                                             : -1;
    if (mouseReporting() && !(mods & Qt::ShiftModifier) && button >= 0) {
        if (sendMouse(term::MouseEvent::Press, button, event->position(), mods)) {
            m_reportedButton = button;
            event->accept();
            return;
        }
    }
    if (event->button() == Qt::MiddleButton) {
        QClipboard *clipboard = QGuiApplication::clipboard();
        const QString text =
                clipboard->text(clipboard->supportsSelection() ? QClipboard::Selection : QClipboard::Clipboard);
        if (!text.isEmpty()) {
            m_session->paste(text);
            scrollToBottom();
        }
        event->accept();
        return;
    }
    if (event->button() == Qt::RightButton) {
        // (A selection made by the press stays: the menu's Copy and Add to chat use it.)
        Q_EMIT contextMenuRequested(event->position().x(), event->position().y());
        event->accept();
        return;
    }
    if (event->button() != Qt::LeftButton) {
        event->ignore();
        return;
    }
    if (mods & (Qt::ControlModifier | Qt::MetaModifier)) {
        const QPoint cell = cellAt(event->position());
        const term::Screen *s = screen();
        const term::Line &line = s->lineAt(std::clamp(cell.y() - m_scrollOffset, -s->historySize(), s->rows() - 1));
        if (cell.x() < line.cells.size()) {
            const QString url = s->hyperlink(line.cells.at(cell.x()).link);
            if (!url.isEmpty()) {
                Q_EMIT linkActivated(url);
                event->accept();
                return;
            }
        }
        // An address or a file path in the text.
        HoverLink link;
        if (linkAt(event->position(), &link)) {
            activate(link);
            event->accept();
            return;
        }
    }
    pressAt(event->position(), mods, event->timestamp());
    event->accept();
}

void TerminalView::mouseDoubleClickEvent(QMouseEvent *event)
{
    // Qt sends the second press too; a platform that sends only this counts it here.
    if (event->button() == Qt::LeftButton && event->timestamp() != m_lastClickTime && m_reportedButton < 0 && m_session)
        pressAt(event->position(), event->modifiers(), event->timestamp());
    event->accept();
}

void TerminalView::pressAt(QPointF point, Qt::KeyboardModifiers modifiers, ulong timestamp)
{
    const QPoint cell = cellAt(point);
    const ulong interval = ulong(QGuiApplication::styleHints()->mouseDoubleClickInterval());
    if (m_clickCount > 0 && timestamp - m_lastClickTime <= interval && cell == m_lastClickCell)
        m_clickCount = m_clickCount % 3 + 1;
    else
        m_clickCount = 1;
    m_lastClickTime = timestamp;
    m_lastClickCell = cell;
    const TextPos at = posAt(point, true);
    const TextPos onCell{at.line, cell.x()};
    switch (m_clickCount) {
    case 1:
        if ((modifiers & Qt::ShiftModifier) && m_hasSelection) {
            extendSelection(at);
        } else {
            clearSelection();
            m_selAnchor = at;
            m_selEnd = at;
            m_selUnit = SelectionUnit::Character;
        }
        break;
    case 2: setSelection(wordStart(onCell), wordEnd(onCell), SelectionUnit::Word); break;
    default: setSelection(lineStart(onCell), lineEnd(onCell), SelectionUnit::Line); break;
    }
    m_selecting = true;
}

void TerminalView::mouseMoveEvent(QMouseEvent *event)
{
    if (m_reportedButton >= 0) {
        sendMouse(term::MouseEvent::Move, m_reportedButton, event->position(), event->modifiers());
        event->accept();
        return;
    }
    if (!m_selecting) {
        event->ignore();
        return;
    }
    // Dragging past the top or bottom scrolls.
    if (event->position().y() < 0)
        scrollLines(1);
    else if (event->position().y() > height())
        scrollLines(-1);
    extendSelection(posAt(event->position(), true));
    event->accept();
}

void TerminalView::mouseReleaseEvent(QMouseEvent *event)
{
    if (m_reportedButton >= 0) {
        sendMouse(term::MouseEvent::Release, m_reportedButton, event->position(), event->modifiers());
        m_reportedButton = -1;
        event->accept();
        return;
    }
    if (m_selecting) {
        m_selecting = false;
        if (m_hasSelection)
            copyToSelectionClipboard();
    }
    event->accept();
}

void TerminalView::hoverMoveEvent(QHoverEvent *event)
{
    if (mouseReporting())
        sendMouse(term::MouseEvent::Move, -1, event->position(), event->modifiers());
    m_hovering = true;
    m_hoverPoint = event->position();
    // Links are looked for only while Ctrl is held: the pointer moving costs nothing otherwise.
    if (event->modifiers() & (Qt::ControlModifier | Qt::MetaModifier))
        updateLink(m_hoverPoint);
    else if (m_link.valid)
        clearLink();
    QQuickPaintedItem::hoverMoveEvent(event);
}

void TerminalView::hoverLeaveEvent(QHoverEvent *event)
{
    m_hovering = false;
    clearLink();
    QQuickPaintedItem::hoverLeaveEvent(event);
}

// ---- Links in the text --------------------------------------------------------------------------

bool TerminalView::linkAt(QPointF point, HoverLink *out) const
{
    const term::Screen *s = screen();
    if (!s || !m_session)
        return false;
    const QPoint cell = cellAt(point);
    const term::CellPos at{cell.y() - m_scrollOffset, cell.x()};
    if (at.line < -s->historySize() || at.line >= s->rows())
        return false;
    // The word found to be no link just now: no file system look again.
    if (!(m_noLink.end < m_noLink.start) && !(at < m_noLink.start) && at < m_noLink.end)
        return false;
    const term::LogicalLine line = term::logicalLine(*s, at.line, true);
    const qsizetype index = line.indexAt(at);
    if (index < 0)
        return false;
    const QList<term::TextLink> links = term::findLinks(line.text);
    const qsizetype found = term::linkAt(links, index);
    if (found < 0)
        return false;
    const term::TextLink &link = links.at(found);
    HoverLink result;
    result.range = line.rangeOf(link.start, link.start + link.length);
    result.line = link.line;
    result.column = link.column;
    if (link.kind == term::TextLink::Url) {
        const QUrl url(link.target);
        if (url.isLocalFile()) {
            const QString path = url.toLocalFile();
            if (!QFileInfo(path).isFile()) {
                const_cast<TerminalView *>(this)->m_noLink = result.range;
                return false;
            }
            result.file = true;
            result.target = QDir::cleanPath(path);
        } else {
            result.target = link.target;
        }
    } else {
        const QString path = term::resolvePath(link.target, m_session->currentDirectory(), QDir::homePath());
        if (path.isEmpty()) {
            const_cast<TerminalView *>(this)->m_noLink = result.range;
            return false;
        }
        result.file = true;
        result.target = path;
    }
    result.valid = true;
    *out = result;
    return true;
}

void TerminalView::updateLines(int first, int last)
{
    const term::Screen *s = screen();
    if (!s)
        return;
    const int from = std::max(0, first + m_scrollOffset);
    const int to = std::min(s->rows() - 1, last + m_scrollOffset);
    updateRows(from, to);
}

void TerminalView::updateLink(QPointF point)
{
    if (m_link.valid) {
        // Still over the same link: nothing to look for.
        const QPoint cell = cellAt(point);
        const term::CellPos at{cell.y() - m_scrollOffset, cell.x()};
        if (!(at < m_link.range.start) && at < m_link.range.end)
            return;
    }
    HoverLink link;
    if (!linkAt(point, &link)) {
        clearLink();
        return;
    }
    if (m_link.valid)
        updateLines(m_link.range.start.line, m_link.range.end.line);
    m_link = link;
    updateLines(m_link.range.start.line, m_link.range.end.line);
    setCursor(Qt::PointingHandCursor);
    Q_EMIT hoveredLinkChanged();
}

void TerminalView::clearLink()
{
    if (!m_link.valid)
        return;
    updateLines(m_link.range.start.line, m_link.range.end.line);
    m_link = HoverLink();
    setCursor(Qt::IBeamCursor);
    Q_EMIT hoveredLinkChanged();
}

void TerminalView::activate(const HoverLink &link)
{
    if (link.file)
        Q_EMIT fileActivated(link.target, link.line, link.column);
    else
        Q_EMIT linkActivated(link.target);
}

// ---- The find bar's search ----------------------------------------------------------------------

int TerminalView::find(const QString &text, bool caseSensitive)
{
    m_findTimer.stop();
    const bool same = text == m_findText && caseSensitive == m_findCase;
    m_findText = text;
    m_findCase = caseSensitive;
    if (!same || m_matches.isEmpty())
        runFind(false);
    return int(m_matches.size());
}

void TerminalView::runFind(bool keepPlace)
{
    const term::Screen *s = screen();
    const bool had = !m_matches.isEmpty();
    const term::CellPos place = m_findCurrent >= 0 && m_findCurrent < m_matches.size()
                                        ? m_matches.at(m_findCurrent).start
                                        : term::CellPos{s ? s->rows() : 0, 0};
    if (m_findText.isEmpty() || !s) {
        m_matches.clear();
        m_findCurrent = -1;
    } else {
        m_matches = term::findText(*s, m_findText, m_findCase ? Qt::CaseSensitive : Qt::CaseInsensitive, MaxMatches);
        if (m_matches.isEmpty()) {
            m_findCurrent = -1;
        } else if (keepPlace) {
            // The same match as before, else the one nearest to where it was.
            const auto it = std::lower_bound(m_matches.cbegin(), m_matches.cend(), place,
                                             [](const term::TextRange &m, term::CellPos p) { return m.start < p; });
            m_findCurrent = std::clamp(int(it - m_matches.cbegin()), 0, int(m_matches.size()) - 1);
        } else {
            // The newest: a terminal is read from its end.
            m_findCurrent = int(m_matches.size()) - 1;
            revealMatch(m_findCurrent);
        }
    }
    if (had || !m_matches.isEmpty())
        update();
    Q_EMIT findChanged();
}

void TerminalView::revealMatch(int index)
{
    const term::Screen *s = screen();
    if (!s || index < 0 || index >= m_matches.size())
        return;
    const int line = m_matches.at(index).start.line;
    const int row = line + m_scrollOffset;
    if (row >= 0 && row < s->rows())
        return;
    // In the middle of the view when it has to scroll.
    setScrollOffset(s->rows() / 2 - line);
}

void TerminalView::findOlder()
{
    if (m_matches.isEmpty())
        return;
    const int n = int(m_matches.size());
    m_findCurrent = ((m_findCurrent - 1) % n + n) % n;
    revealMatch(m_findCurrent);
    update();
    Q_EMIT findChanged();
}

void TerminalView::findNewer()
{
    if (m_matches.isEmpty())
        return;
    m_findCurrent = (m_findCurrent + 1) % int(m_matches.size());
    revealMatch(m_findCurrent);
    update();
    Q_EMIT findChanged();
}

// ---- Clear, the chat ----------------------------------------------------------------------------

void TerminalView::clear()
{
    if (m_session)
        m_session->clear();
}

void TerminalView::onCleared()
{
    clearSelection();
    clearLink();
    m_scrollOffset = 0;
    m_lastHistorySize = 0;
    if (!m_findText.isEmpty())
        runFind(false);
    update();
    Q_EMIT scrollChanged();
}

QString TerminalView::chatText() const
{
    if (!m_session)
        return {};
    const QString text = m_hasSelection ? selectedText() : visibleText();
    return chatBlock(m_session->title(), m_session->currentDirectory(), text);
}

QString TerminalView::chatBlock(const QString &title, const QString &folder, const QString &text)
{
    QStringList lines = text.split(u'\n');
    for (QString &line : lines) {
        qsizetype end = line.size();
        while (end > 0 && line.at(end - 1).isSpace())
            --end;
        line.truncate(end);
    }
    while (!lines.isEmpty() && lines.first().isEmpty())
        lines.removeFirst();
    while (!lines.isEmpty() && lines.last().isEmpty())
        lines.removeLast();
    if (lines.isEmpty())
        return {};
    const QString body = lines.join(u'\n');
    // A fence the text cannot close: longer than any run of backticks in it.
    int longest = 0;
    int run = 0;
    for (const QChar c : body) {
        run = c == u'`' ? run + 1 : 0;
        longest = std::max(longest, run);
    }
    const QString fence(std::max(3, longest + 1), u'`');
    QString where = folder;
    const QString home = QDir::homePath();
    if (!home.isEmpty() && (where == home || where.startsWith(home + u'/')))
        where = u'~' + where.mid(home.size());
    const QString heading = where.isEmpty() ? tr("Terminal output (%1):").arg(title)
                                            : tr("Terminal output (%1, in %2):").arg(title, where);
    return heading + u'\n' + fence + u"console\n"_s + body + u'\n' + fence + u'\n';
}

void TerminalView::wheelEvent(QWheelEvent *event)
{
    const term::Screen *s = screen();
    if (!s) {
        event->ignore();
        return;
    }
    int steps = 0; // notches: positive away from the user (up)
    int lines = 0;
    if (!event->pixelDelta().isNull()) {
        m_wheelPixels += event->pixelDelta().y();
        lines = int(m_wheelPixels / m_cellHeight);
        m_wheelPixels -= lines * m_cellHeight;
        steps = lines;
    } else {
        m_wheelAngle += event->angleDelta().y();
        steps = m_wheelAngle / 120;
        m_wheelAngle -= steps * 120;
        lines = steps * 3;
    }
    if (steps == 0) {
        event->accept();
        return;
    }
    const bool reporting = mouseReporting() && !(event->modifiers() & Qt::ShiftModifier);
    if (reporting) {
        const int kind = steps > 0 ? term::MouseEvent::WheelUp : term::MouseEvent::WheelDown;
        for (int i = 0; i < std::min(std::abs(steps), MaxWheelReports); ++i)
            sendMouse(kind, -1, event->position(), event->modifiers());
    } else if (s->alternateScreen()) {
        // less, man, vim without the mouse: the wheel as the arrow keys.
        const QByteArray key = term::encodeKey(lines > 0 ? Qt::Key_Up : Qt::Key_Down, Qt::NoModifier, QString(),
                                               m_session->terminal().modes());
        for (int i = 0; i < std::min(std::abs(lines), 3 * MaxWheelReports); ++i)
            m_session->writeBytes(key);
    } else {
        scrollLines(lines);
    }
    event->accept();
}

void TerminalView::setSelection(TextPos anchor, TextPos end, SelectionUnit unit)
{
    m_selAnchor = anchor;
    m_selEnd = end;
    m_selUnit = unit;
    m_selStart = std::min(anchor, end);
    m_selStop = std::max(anchor, end);
    m_hasSelection = !(m_selStart == m_selStop);
    update();
    Q_EMIT selectionChanged();
}

void TerminalView::extendSelection(TextPos to)
{
    const TextPos a = m_selAnchor;
    const TextPos b = m_selEnd;
    TextPos start, stop;
    const term::Screen *s = screen();
    const int columns = s ? s->columns() : 1;
    const TextPos onCell{to.line, std::min(to.column, columns - 1)};
    switch (m_selUnit) {
    case SelectionUnit::Character:
        start = std::min(a, to);
        stop = std::max(a, to);
        break;
    case SelectionUnit::Word:
        if (to < a) {
            start = wordStart(onCell);
            stop = b;
        } else {
            start = a;
            stop = std::max(b, wordEnd(onCell));
        }
        break;
    case SelectionUnit::Line:
        if (to < a) {
            start = lineStart(onCell);
            stop = b;
        } else {
            start = a;
            stop = std::max(b, lineEnd(onCell));
        }
        break;
    }
    const bool has = !(start == stop);
    if (has == m_hasSelection && start == m_selStart && stop == m_selStop)
        return;
    m_selStart = start;
    m_selStop = stop;
    m_hasSelection = has;
    update();
    Q_EMIT selectionChanged();
}

TerminalView::TextPos TerminalView::wordStart(TextPos at) const
{
    const term::Screen *s = screen();
    if (!s)
        return at;
    const term::Line &line = s->lineAt(at.line);
    auto charAt = [&line](int column) {
        if (column > 0 && column < line.cells.size() && line.cells.at(column).isWideTail())
            --column;
        const QString text = line.cellText(column);
        return text.isEmpty() ? QChar(u' ') : text.at(0);
    };
    int column = std::clamp(at.column, 0, std::max(0, int(line.cells.size()) - 1));
    if (!isWordChar(charAt(column)))
        return TextPos{at.line, column};
    while (column > 0 && isWordChar(charAt(column - 1)))
        --column;
    return TextPos{at.line, column};
}

TerminalView::TextPos TerminalView::wordEnd(TextPos at) const
{
    const term::Screen *s = screen();
    if (!s)
        return at;
    const term::Line &line = s->lineAt(at.line);
    const int size = int(line.cells.size());
    auto charAt = [&line](int column) {
        if (column > 0 && column < line.cells.size() && line.cells.at(column).isWideTail())
            --column;
        const QString text = line.cellText(column);
        return text.isEmpty() ? QChar(u' ') : text.at(0);
    };
    int column = std::clamp(at.column, 0, std::max(0, size - 1));
    if (!isWordChar(charAt(column)))
        return TextPos{at.line, std::min(column + 1, s->columns())};
    while (column + 1 < size && isWordChar(charAt(column + 1)))
        ++column;
    return TextPos{at.line, std::min(column + 1, s->columns())};
}

TerminalView::TextPos TerminalView::lineStart(TextPos at) const
{
    const term::Screen *s = screen();
    if (!s)
        return at;
    int line = at.line;
    while (line > -s->historySize() && s->lineAt(line - 1).wrapped)
        --line;
    return TextPos{line, 0};
}

TerminalView::TextPos TerminalView::lineEnd(TextPos at) const
{
    const term::Screen *s = screen();
    if (!s)
        return at;
    int line = at.line;
    while (line < s->rows() - 1 && s->lineAt(line).wrapped)
        ++line;
    return TextPos{line, s->columns()};
}

QString TerminalView::selectedText() const
{
    const term::Screen *s = screen();
    if (!s || !m_hasSelection)
        return {};
    QString out;
    for (int index = m_selStart.line; index <= m_selStop.line; ++index) {
        const term::Line &line = s->lineAt(index);
        const int from = index == m_selStart.line ? m_selStart.column : 0;
        const int to = index == m_selStop.line ? m_selStop.column : s->columns();
        QString text = line.text(from, to);
        const bool last = index == m_selStop.line;
        const bool joined = line.wrapped && !last;
        if (!joined) {
            qsizetype end = text.size();
            while (end > 0 && text.at(end - 1) == u' ')
                --end;
            text.truncate(end);
        }
        out += text;
        if (!last && !joined)
            out += u'\n';
    }
    return out;
}

void TerminalView::clearSelection()
{
    if (!m_hasSelection)
        return;
    m_hasSelection = false;
    update();
    Q_EMIT selectionChanged();
}

void TerminalView::selectAll()
{
    const term::Screen *s = screen();
    if (!s)
        return;
    setSelection(TextPos{-s->historySize(), 0}, TextPos{s->rows() - 1, s->columns()}, SelectionUnit::Line);
}

void TerminalView::copy()
{
    if (!m_hasSelection)
        return;
    QGuiApplication::clipboard()->setText(selectedText());
}

void TerminalView::paste()
{
    if (!m_session)
        return;
    const QString text = QGuiApplication::clipboard()->text();
    if (text.isEmpty())
        return;
    m_session->paste(text);
    scrollToBottom();
}

void TerminalView::copyToSelectionClipboard()
{
    QClipboard *clipboard = QGuiApplication::clipboard();
    if (clipboard->supportsSelection())
        clipboard->setText(selectedText(), QClipboard::Selection);
}

QString TerminalView::visibleText() const
{
    const term::Screen *s = screen();
    if (!s)
        return {};
    QString out;
    for (int row = 0; row < s->rows(); ++row) {
        if (row > 0)
            out += u'\n';
        const int index = row - m_scrollOffset;
        if (index >= -s->historySize())
            out += s->lineText(index);
    }
    return out;
}

} // namespace brulion::terminal
