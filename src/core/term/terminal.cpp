#include "term/terminal.h"

#include <QUrl>

#include <algorithm>

namespace brulion::term {

namespace {

constexpr int MaxTitleLength = 256;

QByteArray number(int n)
{
    return QByteArray::number(n);
}

/// "rgb:rrrr/gggg/bbbb" of 0xRRGGBB (XParseColor's form, as xterm answers).
QByteArray xColor(quint32 rgb)
{
    auto channel = [](int v) { return QByteArray::number(v * 257, 16).rightJustified(4, '0'); };
    return "rgb:" + channel(int((rgb >> 16) & 0xff)) + '/' + channel(int((rgb >> 8) & 0xff)) + '/'
           + channel(int(rgb & 0xff));
}

/// The extended colour (38 / 48 / 58) starting at params[i]: its value and the index of its last
/// parameter. False when it is malformed (the rest of the SGR is then ignored, as xterm does).
bool extendedColor(const CsiParams &p, int i, Color *color, int *last)
{
    if (i + 1 < p.count && p.sub[i + 1]) {
        // 38:5:n, 38:2:cs:r:g:b or 38:2:r:g:b (sub-parameters).
        int j = i + 1;
        int subs[6];
        int n = 0;
        while (j < p.count && p.sub[j]) {
            if (n < 6)
                subs[n++] = p.values[j];
            ++j;
        }
        *last = j - 1;
        if (n >= 2 && subs[0] == 5) {
            if (subs[1] < 0 || subs[1] > 255)
                return false;
            *color = Color::indexed(subs[1]);
            return true;
        }
        if (subs[0] == 2 && (n == 4 || n >= 5)) {
            const int at = n >= 5 ? 2 : 1;
            const int r = std::max(0, subs[at]), g = std::max(0, subs[at + 1]), b = std::max(0, subs[at + 2]);
            if (r > 255 || g > 255 || b > 255)
                return false;
            *color = Color::rgb(r, g, b);
            return true;
        }
        return false;
    }
    // 38;5;n or 38;2;r;g;b.
    if (i + 1 >= p.count)
        return false;
    const int kind = p.values[i + 1];
    if (kind == 5) {
        if (i + 2 >= p.count || p.values[i + 2] < 0 || p.values[i + 2] > 255)
            return false;
        *color = Color::indexed(p.values[i + 2]);
        *last = i + 2;
        return true;
    }
    if (kind == 2) {
        if (i + 4 >= p.count)
            return false;
        const int r = std::max(0, p.values[i + 2]), g = std::max(0, p.values[i + 3]), b = std::max(0, p.values[i + 4]);
        if (r > 255 || g > 255 || b > 255)
            return false;
        *color = Color::rgb(r, g, b);
        *last = i + 4;
        return true;
    }
    return false;
}

} // namespace

Terminal::Terminal(int columns, int rows, int scrollbackLimit, QObject *parent)
    : QObject(parent), m_screen(columns, rows, scrollbackLimit), m_parser(this)
{ }

Terminal::~Terminal() = default;

void Terminal::feed(QByteArrayView data)
{
    m_parser.feed(data);
}

void Terminal::resize(int columns, int rows)
{
    m_screen.resize(columns, rows);
}

void Terminal::reset()
{
    m_parser.reset();
    m_screen.reset();
    m_screen.clearHistory();
    setModes(InputModes());
    setTitle(QString());
    if (!m_workingDirectory.isEmpty()) {
        m_workingDirectory.clear();
        Q_EMIT workingDirectoryChanged(m_workingDirectory);
    }
}

void Terminal::setReportedColors(quint32 foreground, quint32 background, quint32 cursor)
{
    m_hasColors = true;
    m_colors[0] = foreground & 0xffffff;
    m_colors[1] = background & 0xffffff;
    m_colors[2] = cursor & 0xffffff;
}

void Terminal::setModes(const InputModes &modes)
{
    if (modes == m_modes)
        return;
    m_modes = modes;
    Q_EMIT modesChanged();
}

void Terminal::sendReply(const QByteArray &data)
{
    Q_EMIT reply(data);
}

void Terminal::setTitle(const QString &title)
{
    if (title == m_title)
        return;
    m_title = title;
    Q_EMIT titleChanged(m_title);
}

void Terminal::print(char32_t cp)
{
    // C1 controls arriving as UTF-8 characters are neither shown nor obeyed.
    if (cp >= 0x80 && cp < 0xa0)
        return;
    m_screen.print(cp);
}

void Terminal::execute(char control)
{
    switch (control) {
    case 0x07: Q_EMIT bell(); break;
    case 0x08: m_screen.backspace(); break;
    case 0x09: m_screen.tab(1); break;
    case 0x0a:
    case 0x0b:
    case 0x0c: m_screen.lineFeed(); break;
    case 0x0d: m_screen.carriageReturn(); break;
    case 0x0e: m_screen.shiftOut(); break;
    case 0x0f: m_screen.shiftIn(); break;
    default: break;
    }
}

void Terminal::escDispatch(QByteArrayView intermediates, char final)
{
    if (intermediates.isEmpty()) {
        switch (final) {
        case '7': m_screen.saveCursor(); break;
        case '8': m_screen.restoreCursor(); break;
        case 'D': m_screen.index(); break;
        case 'E': m_screen.nextLine(); break;
        case 'H': m_screen.setTabStop(); break;
        case 'M': m_screen.reverseIndex(); break;
        case 'Z': // DECID: as DA
            sendReply("\x1b[?62;22c");
            break;
        case 'c': { // RIS (the scrollback, title and working directory stay)
            m_screen.reset();
            setModes(InputModes());
            break;
        }
        case '=': {
            InputModes modes = m_modes;
            modes.applicationKeypad = true;
            setModes(modes);
            break;
        }
        case '>': {
            InputModes modes = m_modes;
            modes.applicationKeypad = false;
            setModes(modes);
            break;
        }
        default: // ST (\), SS2, SS3 and the rest
            break;
        }
        return;
    }
    if (intermediates.size() != 1)
        return;
    const char i = intermediates[0];
    if (i == '#' && final == '8') {
        m_screen.alignmentTest();
        return;
    }
    if (i == '(' || i == ')') {
        const int slot = i == '(' ? 0 : 1;
        switch (final) {
        case '0': m_screen.designateCharset(slot, Charset::DecSpecialGraphics); break;
        case 'A': m_screen.designateCharset(slot, Charset::Uk); break;
        case 'B': m_screen.designateCharset(slot, Charset::Ascii); break;
        default: break;
        }
    }
}

void Terminal::csiDispatch(const CsiParams &p, QByteArrayView intermediates, char prefix, char final)
{
    Screen &s = m_screen;
    if (!intermediates.isEmpty()) {
        if (intermediates.size() != 1)
            return;
        const char i = intermediates[0];
        if (i == ' ' && final == 'q' && prefix == 0) { // DECSCUSR
            switch (p.at(0, 0, false)) {
            case 0:
            case 1: s.setCursorStyle(CursorShape::Block, true); break;
            case 2: s.setCursorStyle(CursorShape::Block, false); break;
            case 3: s.setCursorStyle(CursorShape::Underline, true); break;
            case 4: s.setCursorStyle(CursorShape::Underline, false); break;
            case 5: s.setCursorStyle(CursorShape::Bar, true); break;
            case 6: s.setCursorStyle(CursorShape::Bar, false); break;
            default: break;
            }
        } else if (i == '!' && final == 'p' && prefix == 0) { // DECSTR
            s.softReset();
            InputModes modes = m_modes;
            modes.applicationCursorKeys = false;
            modes.applicationKeypad = false;
            setModes(modes);
        } else if (i == '$' && final == 'p') { // DECRQM
            const int mode = p.at(0, 0, false);
            int state = 0;
            if (prefix == '?')
                state = privateModeState(mode);
            else if (prefix == 0 && (mode == 4 || mode == 20))
                state = (mode == 4 ? s.insertMode() : s.newLineMode()) ? 1 : 2;
            sendReply("\x1b[" + QByteArray(prefix == '?' ? "?" : "") + number(mode) + ';' + number(state) + "$y");
        }
        return;
    }

    if (prefix == '?') {
        switch (final) {
        case 'h':
        case 'l':
            for (int i = 0; i < p.count; ++i) {
                if (!p.sub[i] && p.values[i] >= 0)
                    setPrivateMode(p.values[i], final == 'h');
            }
            break;
        case 'n': // DECXCPR
            if (p.at(0, 0, false) == 6) {
                const int row = s.cursorRow() - (s.originMode() ? s.scrollTop() : 0);
                sendReply("\x1b[?" + number(row + 1) + ';' + number(s.cursorColumn() + 1) + 'R');
            }
            break;
        case 'J': // DECSED, DECSEL: as ED, EL (no protected cells)
            s.eraseInDisplay(p.at(0, 0, false));
            break;
        case 'K': s.eraseInLine(p.at(0, 0, false)); break;
        default: break;
        }
        return;
    }
    if (prefix == '>') {
        if (final == 'c' && p.at(0, 0, false) == 0) // DA2: a VT220, firmware 10
            sendReply("\x1b[>1;10;0c");
        return;
    }
    if (prefix != 0)
        return;

    switch (final) {
    case '@': s.insertChars(p.at(0, 1)); break;
    case 'A': s.moveUp(p.at(0, 1)); break;
    case 'B': s.moveDown(p.at(0, 1)); break;
    case 'C':
    case 'a': s.moveForward(p.at(0, 1)); break;
    case 'D': s.moveBackward(p.at(0, 1)); break;
    case 'E':
        s.moveDown(p.at(0, 1));
        s.carriageReturn();
        break;
    case 'F':
        s.moveUp(p.at(0, 1));
        s.carriageReturn();
        break;
    case 'G':
    case '`': s.moveToColumn(p.at(0, 1) - 1); break;
    case 'H':
    case 'f': s.moveTo(p.at(0, 1) - 1, p.at(1, 1) - 1); break;
    case 'I': s.tab(p.at(0, 1)); break;
    case 'J': s.eraseInDisplay(p.at(0, 0, false)); break;
    case 'K': s.eraseInLine(p.at(0, 0, false)); break;
    case 'L': s.insertLines(p.at(0, 1)); break;
    case 'M': s.deleteLines(p.at(0, 1)); break;
    case 'P': s.deleteChars(p.at(0, 1)); break;
    case 'S': s.scrollUp(p.at(0, 1)); break;
    case 'T':
        if (p.count <= 1) // five parameters: xterm's mouse highlight tracking
            s.scrollDown(p.at(0, 1));
        break;
    case 'X': s.eraseChars(p.at(0, 1)); break;
    case 'Z': s.backTab(p.at(0, 1)); break;
    case 'b': s.repeatLast(p.at(0, 1)); break;
    case 'c':
        if (p.at(0, 0, false) == 0) // DA: a VT220 with ANSI colour
            sendReply("\x1b[?62;22c");
        break;
    case 'd': s.moveToRow(p.at(0, 1) - 1); break;
    case 'e': s.moveDown(p.at(0, 1)); break;
    case 'g': s.clearTabStop(p.at(0, 0, false)); break;
    case 'h':
    case 'l':
        for (int i = 0; i < p.count; ++i) {
            if (!p.sub[i] && p.values[i] >= 0)
                setAnsiMode(p.values[i], final == 'h');
        }
        break;
    case 'm': selectGraphicRendition(p); break;
    case 'n':
        switch (p.at(0, 0, false)) {
        case 5: sendReply("\x1b[0n"); break;
        case 6: {
            const int row = s.cursorRow() - (s.originMode() ? s.scrollTop() : 0);
            sendReply("\x1b[" + number(row + 1) + ';' + number(s.cursorColumn() + 1) + 'R');
            break;
        }
        default: break;
        }
        break;
    case 'r': s.setScrollRegion(p.at(0, 1) - 1, p.at(1, s.rows()) - 1); break;
    case 's':
        if (p.count == 0)
            s.saveCursor();
        break;
    case 't':
        if (p.at(0, 0, false) == 18)
            sendReply("\x1b[8;" + number(s.rows()) + ';' + number(s.columns()) + 't');
        break;
    case 'u':
        if (p.count == 0)
            s.restoreCursor();
        break;
    default: break;
    }
}

void Terminal::selectGraphicRendition(const CsiParams &p)
{
    Pen &pen = m_screen.pen();
    if (p.count == 0) {
        pen.fg = Color();
        pen.bg = Color();
        pen.attrs = 0;
        return;
    }
    for (int i = 0; i < p.count; ++i) {
        if (p.sub[i])
            continue; // a sub-parameter of something not taking any
        const int v = std::max(0, p.values[i]);
        switch (v) {
        case 0:
            pen.fg = Color();
            pen.bg = Color();
            pen.attrs = 0;
            break;
        case 1: pen.attrs |= Bold; break;
        case 2: pen.attrs |= Dim; break;
        case 3: pen.attrs |= Italic; break;
        case 4:
            pen.attrs &= quint16(~(Underline | DoubleUnderline));
            if (i + 1 < p.count && p.sub[i + 1]) {
                // 4:0 none, 4:1 single, 4:2 double, 4:3 curly / 4:4 dotted / 4:5 dashed (single here).
                const int style = p.values[++i];
                if (style == 2)
                    pen.attrs |= DoubleUnderline;
                else if (style != 0)
                    pen.attrs |= Underline;
                while (i + 1 < p.count && p.sub[i + 1])
                    ++i;
            } else {
                pen.attrs |= Underline;
            }
            break;
        case 5:
        case 6: pen.attrs |= Blink; break;
        case 7: pen.attrs |= Inverse; break;
        case 8: pen.attrs |= Invisible; break;
        case 9: pen.attrs |= Strikethrough; break;
        case 21: pen.attrs = quint16((pen.attrs & ~Underline) | DoubleUnderline); break;
        case 22: pen.attrs &= quint16(~(Bold | Dim)); break;
        case 23: pen.attrs &= quint16(~Italic); break;
        case 24: pen.attrs &= quint16(~(Underline | DoubleUnderline)); break;
        case 25: pen.attrs &= quint16(~Blink); break;
        case 27: pen.attrs &= quint16(~Inverse); break;
        case 28: pen.attrs &= quint16(~Invisible); break;
        case 29: pen.attrs &= quint16(~Strikethrough); break;
        case 38:
        case 48:
        case 58: {
            Color color;
            int last = i;
            if (!extendedColor(p, i, &color, &last))
                return;
            if (v == 38)
                pen.fg = color;
            else if (v == 48)
                pen.bg = color;
            // 58: the underline's colour — not drawn in its own colour.
            i = last;
            break;
        }
        case 39: pen.fg = Color(); break;
        case 49: pen.bg = Color(); break;
        case 53: pen.attrs |= Overline; break;
        case 55: pen.attrs &= quint16(~Overline); break;
        default:
            if (v >= 30 && v <= 37)
                pen.fg = Color::indexed(v - 30);
            else if (v >= 40 && v <= 47)
                pen.bg = Color::indexed(v - 40);
            else if (v >= 90 && v <= 97)
                pen.fg = Color::indexed(v - 90 + 8);
            else if (v >= 100 && v <= 107)
                pen.bg = Color::indexed(v - 100 + 8);
            break;
        }
    }
}

void Terminal::setAnsiMode(int mode, bool on)
{
    if (mode == 4)
        m_screen.setInsertMode(on);
    else if (mode == 20)
        m_screen.setNewLineMode(on);
}

void Terminal::setPrivateMode(int mode, bool on)
{
    Screen &s = m_screen;
    InputModes modes = m_modes;
    auto mouse = [&](MouseTracking tracking) {
        if (on)
            modes.mouseTracking = tracking;
        else if (modes.mouseTracking == tracking)
            modes.mouseTracking = MouseTracking::Off;
    };
    switch (mode) {
    case 1: modes.applicationCursorKeys = on; break;
    case 5: s.setReverseVideo(on); break;
    case 6: s.setOriginMode(on); break;
    case 7: s.setAutoWrap(on); break;
    case 9: mouse(MouseTracking::X10); break;
    case 12: s.setCursorStyle(s.cursorShape(), on); break;
    case 25: s.setCursorVisible(on); break;
    case 47: s.setAlternateScreen(on, false); break;
    case 1000: mouse(MouseTracking::Click); break;
    case 1002: mouse(MouseTracking::Drag); break;
    case 1003: mouse(MouseTracking::Motion); break;
    case 1004: modes.focusEvents = on; break;
    case 1006: modes.mouseSgr = on; break;
    case 1047:
        // Cleared when leaving it.
        s.setAlternateScreen(on, !on);
        break;
    case 1048:
        if (on)
            s.saveCursor();
        else
            s.restoreCursor();
        break;
    case 1049:
        if (on) {
            s.saveCursor();
            s.setAlternateScreen(true, true);
        } else {
            s.setAlternateScreen(false, false);
            s.restoreCursor();
        }
        break;
    case 2004: modes.bracketedPaste = on; break;
    case 2026: modes.synchronizedOutput = on; break;
    default: break;
    }
    setModes(modes);
}

int Terminal::privateModeState(int mode) const
{
    const Screen &s = m_screen;
    auto state = [](bool on) { return on ? 1 : 2; };
    switch (mode) {
    case 1: return state(m_modes.applicationCursorKeys);
    case 5: return state(s.reverseVideo());
    case 6: return state(s.originMode());
    case 7: return state(s.autoWrap());
    case 9: return state(m_modes.mouseTracking == MouseTracking::X10);
    case 12: return state(s.cursorBlinking());
    case 25: return state(s.cursorVisible());
    case 47:
    case 1047:
    case 1049: return state(s.alternateScreen());
    case 1000: return state(m_modes.mouseTracking == MouseTracking::Click);
    case 1002: return state(m_modes.mouseTracking == MouseTracking::Drag);
    case 1003: return state(m_modes.mouseTracking == MouseTracking::Motion);
    case 1004: return state(m_modes.focusEvents);
    case 1006: return state(m_modes.mouseSgr);
    case 2004: return state(m_modes.bracketedPaste);
    case 2026: return state(m_modes.synchronizedOutput);
    default: return 0;
    }
}

void Terminal::oscDispatch(const QByteArray &data)
{
    const qsizetype semicolon = data.indexOf(';');
    const QByteArray head = semicolon < 0 ? data : data.left(semicolon);
    const QByteArray payload = semicolon < 0 ? QByteArray() : data.mid(semicolon + 1);
    bool ok = false;
    const int command = head.toInt(&ok);
    if (!ok)
        return;
    switch (command) {
    case 0:
    case 2: {
        QString title = QString::fromUtf8(payload);
        title.removeIf([](QChar c) {
            return c.unicode() < 0x20 || c.unicode() == 0x7f || (c.unicode() >= 0x80 && c.unicode() < 0xa0);
        });
        if (title.size() > MaxTitleLength)
            title.truncate(MaxTitleLength);
        setTitle(title);
        break;
    }
    case 7: {
        // file://host/path (percent-encoded); other schemes are other programs' business.
        const QUrl url(QString::fromUtf8(payload), QUrl::StrictMode);
        if (!url.isValid() || url.scheme() != u"file")
            break;
        const QString path = url.path(QUrl::FullyDecoded);
        if (path.isEmpty() || path == m_workingDirectory)
            break;
        m_workingDirectory = path;
        Q_EMIT workingDirectoryChanged(m_workingDirectory);
        break;
    }
    case 8: {
        // OSC 8 ; params ; URI — an empty URI ends the link.
        const qsizetype at = payload.indexOf(';');
        if (at < 0)
            break;
        const QString uri = QString::fromUtf8(payload.mid(at + 1));
        m_screen.pen().link = uri.isEmpty() ? 0 : m_screen.hyperlinkId(uri);
        break;
    }
    case 10:
    case 11:
    case 12:
        // Only queries are answered; programs cannot change the view's colours.
        if (payload == "?" && m_hasColors)
            sendReply("\x1b]" + number(command) + ';' + xColor(m_colors[command - 10]) + "\x1b\\");
        break;
    case 52:
        // The clipboard: never read or written for the program (docs/CODE-TERMINAL.md "Security").
        break;
    default: break;
    }
}

} // namespace brulion::term
