#include "term/keys.h"

#include <algorithm>

namespace brulion::term {

namespace {

constexpr char Esc = '\x1b';

/// xterm's modifier parameter: 1 + Shift 1 + Alt 2 + Ctrl 4 + Meta 8.
int modifierParam(Qt::KeyboardModifiers m)
{
    int value = 1;
    if (m & Qt::ShiftModifier)
        value += 1;
    if (m & Qt::AltModifier)
        value += 2;
    if (m & Qt::ControlModifier)
        value += 4;
    if (m & Qt::MetaModifier)
        value += 8;
    return value;
}

/// CSI X, SS3 X (application mode) or CSI 1 ; m X.
QByteArray cursorKey(char letter, int modifier, bool ss3)
{
    if (modifier > 1)
        return "\x1b[1;" + QByteArray::number(modifier) + letter;
    return QByteArray(ss3 ? "\x1bO" : "\x1b[") + letter;
}

/// CSI n ~ or CSI n ; m ~.
QByteArray tildeKey(int n, int modifier)
{
    QByteArray out = "\x1b[" + QByteArray::number(n);
    if (modifier > 1)
        out += ';' + QByteArray::number(modifier);
    out += '~';
    return out;
}

/// Ctrl with `key`: its C0 code, -1 for none.
int controlCode(int key)
{
    if (key >= Qt::Key_A && key <= Qt::Key_Z)
        return key - Qt::Key_A + 1;
    switch (key) {
    case Qt::Key_At:
    case Qt::Key_Space:
    case Qt::Key_2: return 0x00;
    case Qt::Key_BracketLeft:
    case Qt::Key_3: return 0x1b;
    case Qt::Key_Backslash:
    case Qt::Key_4: return 0x1c;
    case Qt::Key_BracketRight:
    case Qt::Key_5: return 0x1d;
    case Qt::Key_AsciiCircum:
    case Qt::Key_6: return 0x1e;
    case Qt::Key_Underscore:
    case Qt::Key_Minus:
    case Qt::Key_Slash:
    case Qt::Key_7: return 0x1f;
    case Qt::Key_Question:
    case Qt::Key_8: return 0x7f;
    default: return -1;
    }
}

/// `text` as UTF-8 with CR LF and LF as CR, without control characters other than TAB and CR (C0
/// with ESC, DEL, C1).
QByteArray cleanText(const QString &text)
{
    QString out;
    out.reserve(text.size());
    for (qsizetype i = 0; i < text.size(); ++i) {
        const char16_t c = text.at(i).unicode();
        if (c == u'\r') {
            out.append(u'\r');
            if (i + 1 < text.size() && text.at(i + 1) == u'\n')
                ++i;
            continue;
        }
        if (c == u'\n') {
            out.append(u'\r');
            continue;
        }
        if (c == u'\t') {
            out.append(u'\t');
            continue;
        }
        if (c < 0x20 || c == 0x7f || (c >= 0x80 && c < 0xa0))
            continue;
        out.append(QChar(c));
    }
    return out.toUtf8();
}

} // namespace

QByteArray encodeKey(int key, Qt::KeyboardModifiers modifiers, const QString &text, const InputModes &modes)
{
    const Qt::KeyboardModifiers m =
            modifiers & (Qt::ShiftModifier | Qt::ControlModifier | Qt::AltModifier | Qt::MetaModifier);
    const int modifier = modifierParam(m);
    const bool alt = m & Qt::AltModifier;
    const bool ctrl = m & Qt::ControlModifier;
    const bool shift = m & Qt::ShiftModifier;
    const bool app = modes.applicationCursorKeys;
    const QByteArray altPrefix = alt ? QByteArray(1, Esc) : QByteArray();

    switch (key) {
    case Qt::Key_Up: return cursorKey('A', modifier, app);
    case Qt::Key_Down: return cursorKey('B', modifier, app);
    case Qt::Key_Right: return cursorKey('C', modifier, app);
    case Qt::Key_Left: return cursorKey('D', modifier, app);
    case Qt::Key_Home: return cursorKey('H', modifier, app);
    case Qt::Key_End: return cursorKey('F', modifier, app);
    case Qt::Key_Insert: return tildeKey(2, modifier);
    case Qt::Key_Delete: return tildeKey(3, modifier);
    case Qt::Key_PageUp: return tildeKey(5, modifier);
    case Qt::Key_PageDown: return tildeKey(6, modifier);
    case Qt::Key_F1: return cursorKey('P', modifier, true);
    case Qt::Key_F2: return cursorKey('Q', modifier, true);
    case Qt::Key_F3: return cursorKey('R', modifier, true);
    case Qt::Key_F4: return cursorKey('S', modifier, true);
    case Qt::Key_F5: return tildeKey(15, modifier);
    case Qt::Key_F6: return tildeKey(17, modifier);
    case Qt::Key_F7: return tildeKey(18, modifier);
    case Qt::Key_F8: return tildeKey(19, modifier);
    case Qt::Key_F9: return tildeKey(20, modifier);
    case Qt::Key_F10: return tildeKey(21, modifier);
    case Qt::Key_F11: return tildeKey(23, modifier);
    case Qt::Key_F12: return tildeKey(24, modifier);
    case Qt::Key_Return:
    case Qt::Key_Enter: return altPrefix + '\r';
    case Qt::Key_Backspace: return altPrefix + (ctrl ? '\x08' : '\x7f');
    case Qt::Key_Tab:
        if (ctrl)
            return {}; // Ctrl+Tab moves the focus out (the view does not take it)
        if (shift)
            return "\x1b[Z";
        return altPrefix + '\t';
    case Qt::Key_Backtab:
        if (ctrl)
            return {};
        return "\x1b[Z";
    case Qt::Key_Escape: return altPrefix + Esc;
    case Qt::Key_Shift:
    case Qt::Key_Control:
    case Qt::Key_Alt:
    case Qt::Key_AltGr:
    case Qt::Key_Meta:
    case Qt::Key_Super_L:
    case Qt::Key_Super_R:
    case Qt::Key_Hyper_L:
    case Qt::Key_Hyper_R:
    case Qt::Key_CapsLock:
    case Qt::Key_NumLock:
    case Qt::Key_ScrollLock: return {};
    default: break;
    }

    if (ctrl) {
        const int code = controlCode(key);
        if (code >= 0)
            return altPrefix + char(code);
        // Another layout's letter that gives a control character as its text.
        if (text.size() == 1 && text.at(0).unicode() < 0x20)
            return altPrefix + char(text.at(0).unicode());
    }
    QByteArray bytes = cleanText(text);
    if (bytes.isEmpty())
        return {};
    return altPrefix + bytes;
}

QByteArray encodePaste(const QString &text, bool bracketed)
{
    const QByteArray body = cleanText(text);
    if (!bracketed)
        return body;
    return "\x1b[200~" + body + "\x1b[201~";
}

QByteArray encodeText(const QString &text)
{
    return cleanText(text);
}

QByteArray encodeFocus(bool focused, const InputModes &modes)
{
    if (!modes.focusEvents)
        return {};
    return focused ? QByteArray("\x1b[I") : QByteArray("\x1b[O");
}

bool mouseWanted(const MouseEvent &event, const InputModes &modes)
{
    const bool wheel = event.kind >= MouseEvent::WheelUp;
    switch (modes.mouseTracking) {
    case MouseTracking::Off: return false;
    case MouseTracking::X10: return event.kind == MouseEvent::Press || wheel;
    case MouseTracking::Click: return event.kind != MouseEvent::Move;
    case MouseTracking::Drag: return event.kind != MouseEvent::Move || event.button >= 0;
    case MouseTracking::Motion: return true;
    }
    return false;
}

QByteArray encodeMouse(const MouseEvent &event, const InputModes &modes)
{
    if (!mouseWanted(event, modes))
        return {};
    const bool sgr = modes.mouseSgr;
    const int button = std::clamp(event.button, -1, 2);
    int code = 0;
    switch (event.kind) {
    case MouseEvent::Press:
        if (button < 0)
            return {};
        code = button;
        break;
    case MouseEvent::Release: code = sgr && button >= 0 ? button : 3; break;
    case MouseEvent::Move: code = (button < 0 ? 3 : button) + 32; break;
    case MouseEvent::WheelUp: code = 64; break;
    case MouseEvent::WheelDown: code = 65; break;
    case MouseEvent::WheelLeft: code = 66; break;
    case MouseEvent::WheelRight: code = 67; break;
    }
    if (modes.mouseTracking != MouseTracking::X10) {
        if (event.modifiers & Qt::ShiftModifier)
            code += 4;
        if (event.modifiers & (Qt::AltModifier | Qt::MetaModifier))
            code += 8;
        if (event.modifiers & Qt::ControlModifier)
            code += 16;
    }
    const int x = std::max(0, event.column) + 1;
    const int y = std::max(0, event.row) + 1;
    if (sgr) {
        return "\x1b[<" + QByteArray::number(code) + ';' + QByteArray::number(x) + ';' + QByteArray::number(y)
               + (event.kind == MouseEvent::Release ? 'm' : 'M');
    }
    if (x > 223 || y > 223)
        return {};
    QByteArray out = "\x1b[M";
    out += char(32 + code);
    out += char(32 + x);
    out += char(32 + y);
    return out;
}

} // namespace brulion::term
