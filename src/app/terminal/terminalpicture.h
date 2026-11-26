// TerminalPicture — everything one frame of a terminal shows, as TerminalView hands it to the
// renderer that draws it with Atrament (terminalatrament.h; docs/CODE-TERMINAL.md "Drawn by
// Atrament"): the screen and the place in its scrollback, the grid's metrics, the colours, the
// selection, the find bar's matches, the link under the pointer, the input method's preedit and
// the cursor's state. Plain data in the view's coordinates (logical pixels from its top-left);
// colours as QRgb with alpha. Pointers stay valid until the frame is painted (the GUI thread
// fills and paints it in one go).
#pragma once

#include "term/screentext.h"

#include <QList>
#include <QRgb>
#include <QString>
#include <QtGlobal>

#include <array>

namespace brulion::term {
class Screen;
}

namespace brulion::terminal {

/// The grid's size and where a row's text goes in it, in logical pixels.
struct TerminalCellMetrics {
    qreal cellWidth = 8;
    qreal cellHeight = 16;
    /// From a row's top to the baseline.
    qreal ascent = 12;
    /// Below the baseline, positive down; the strike-out line above it, positive up.
    qreal underlinePos = 1;
    qreal lineWidth = 1;
    qreal strikePos = 4;
};

struct TerminalPicture {
    const term::Screen *screen = nullptr;
    /// Lines scrolled back into the scrollback (TerminalView::scrollOffset).
    int scrollOffset = 0;
    qreal padding = 4;
    TerminalCellMetrics metrics;
    /// The family and pixel size the glyphs are drawn with.
    QString fontFamily;
    qreal fontPixelSize = 14;

    QRgb foreground = 0xffd3d8e2;
    QRgb background = 0xff16171b;
    QRgb cursor = 0xffff7a66;
    QRgb selection = 0x4d9fc3e6;
    QRgb matchColor = 0x52f2c94c;
    QRgb currentMatchColor = 0xb3ff8c42;
    /// The 16 ANSI colours.
    std::array<QRgb, 16> palette{};

    /// The selection in Screen::lineAt() lines, the stop column exclusive.
    bool hasSelection = false;
    term::CellPos selectionStart;
    term::CellPos selectionStop;
    /// The find bar's matches (in order, apart), the current one (-1: none).
    const QList<term::TextRange> *matches = nullptr;
    int currentMatch = -1;
    /// The link under the pointer while Ctrl is held, underlined.
    bool hasLink = false;
    term::TextRange link;

    /// The input method's text being composed, shown at the cursor.
    QString preedit;
    /// The view has the keyboard focus (a filled cursor, else an outline).
    bool focused = false;
    /// The blinking cursor's phase: shown.
    bool blinkOn = true;
    /// The program runs (no cursor before it starts or after it ended).
    bool running = false;
};

/// What a TerminalView gives its Atrament renderer each frame.
class TerminalPictureSource {
public:
    virtual ~TerminalPictureSource() = default;
    virtual void terminalPicture(TerminalPicture &picture) const = 0;
};

} // namespace brulion::terminal
