// What the terminal's input sends to the program (term.h): keys, pastes, mouse and focus events
// as xterm encodes them — pure functions of the event and the modes the program set. Qt's key codes
// and modifiers (Qt::Key, Qt::KeyboardModifiers) live in QtCore, so this stays GUI-free; the view
// passes QKeyEvent::key(), modifiers() and text().
//
//   encodeKey(Qt::Key_Up, Qt::NoModifier, {}, modes)       // "\e[A", or "\eOA" with DECCKM
//   encodeKey(Qt::Key_C, Qt::ControlModifier, {}, modes)   // "\x03"
//   encodePaste(u"ls\n"_s, true)                           // "\e[200~ls\r\e[201~"
//
// Modifiers are Qt's: on macOS Qt::ControlModifier is the Command key and Qt::MetaModifier the
// Control key — the view swaps them there before calling (Control+C is ^C on every platform).
#pragma once

#include "term/terminal.h"

#include <QByteArray>
#include <QString>
#include <QtCore/qnamespace.h>

namespace brulion::term {

/// The bytes a key press sends ("" for keys that send nothing: modifiers alone, unknown keys):
///
/// * arrows, Home, End: CSI A/B/C/D/H/F, SS3 … with application cursor keys, CSI 1;m X with
///   modifiers (m = 1 + Shift 1 + Alt 2 + Ctrl 4 + Meta 8);
/// * Insert, Delete, Page Up, Page Down, F5-F12: CSI n ~ (CSI n;m ~); F1-F4: SS3 P/Q/R/S (CSI 1;m P);
/// * Enter: CR; Backspace: DEL (Ctrl: BS); Tab: HT; Shift+Tab (Qt::Key_Backtab): CSI Z; Escape: ESC;
/// * Ctrl with a letter or @ [ \ ] ^ _ ? (and xterm's digits 2-8, Space): the C0 control code;
/// * Alt: ESC before what the key sends without it;
/// * anything else: the UTF-8 of `text`.
QByteArray encodeKey(int key, Qt::KeyboardModifiers modifiers, const QString &text, const InputModes &modes);

/// A paste: line breaks (CR LF, LF) become CR, as typed; with bracketed paste between CSI 200 ~ and
/// CSI 201 ~, every ESC of the text removed so the text cannot end the bracket itself.
QByteArray encodePaste(const QString &text, bool bracketed);

/// Committed text of an input method (or a plain key's text): its UTF-8, control characters but
/// TAB, CR and LF dropped.
QByteArray encodeText(const QString &text);

/// Focus in / out (CSI I / CSI O) when the program asked for it (?1004), else "".
QByteArray encodeFocus(bool focused, const InputModes &modes);

/// A mouse event for the program.
struct MouseEvent {
    enum Kind { Press, Release, Move, WheelUp, WheelDown, WheelLeft, WheelRight };
    Kind kind = Press;
    /// 0 left, 1 middle, 2 right; -1 none (a move without a button).
    int button = 0;
    /// The cell, 0-based.
    int column = 0;
    int row = 0;
    Qt::KeyboardModifiers modifiers;
};

/// Whether the program wants `event` reported (its MouseTracking mode).
bool mouseWanted(const MouseEvent &event, const InputModes &modes);
/// The report of `event`: SGR (CSI < b ; x ; y M/m) with ?1006, else xterm's CSI M b x y with
/// coordinates up to 223 ("" beyond). "" when the program does not want it.
QByteArray encodeMouse(const MouseEvent &event, const InputModes &modes);

} // namespace brulion::term
