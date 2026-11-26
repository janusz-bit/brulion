// The terminal emulator (term.h): a Parser feeding a Screen — what each escape sequence means
// (docs/CODE-TERMINAL.md "Supported sequences"), the modes a program sets for its input (cursor
// keys, mouse reporting, bracketed paste), the window title and working directory it reports, and
// the replies it asks for (cursor position, device attributes), sent back through reply().
//
//   Terminal t(80, 24);
//   QObject::connect(&t, &Terminal::reply, pty, &Pty::write);
//   t.feed(bytesFromTheProgram);       // any split: UTF-8 and sequences continue across calls
//   t.screen().line(0).trimmedText();
//
// Never touches the clipboard: OSC 52 (set / query the clipboard) is ignored, so a program — or a
// file shown with cat — can neither read nor replace what the user copied.
#pragma once

#include "term/parser.h"
#include "term/screen.h"

#include <QByteArray>
#include <QObject>
#include <QString>

namespace brulion::term {

/// Which mouse events the program asked for (DECSET 9, 1000, 1002, 1003).
enum class MouseTracking { Off, X10, Click, Drag, Motion };

/// The modes that change what the terminal's input sends (keys.h).
struct InputModes {
    /// DECCKM (?1): cursor keys send SS3 A … instead of CSI A ….
    bool applicationCursorKeys = false;
    /// DECKPAM / DECKPNM: stored; the keypad sends what its keys say either way.
    bool applicationKeypad = false;
    /// ?2004: pastes come between CSI 200 ~ and CSI 201 ~.
    bool bracketedPaste = false;
    /// ?1004: focus in and out are reported (CSI I, CSI O).
    bool focusEvents = false;
    MouseTracking mouseTracking = MouseTracking::Off;
    /// ?1006: mouse reports as CSI < b ; x ; y M/m (no limit on coordinates).
    bool mouseSgr = false;
    /// ?2026: the program draws a frame; the view may wait for its end before painting.
    bool synchronizedOutput = false;

    friend bool operator==(const InputModes &, const InputModes &) = default;
};

class Terminal : public QObject, private ParserHandler {
    Q_OBJECT

public:
    explicit Terminal(int columns = 80, int rows = 24, int scrollbackLimit = 5000, QObject *parent = nullptr);
    ~Terminal() override;

    /// Output of the program.
    void feed(QByteArrayView data);

    Screen &screen() { return m_screen; }
    const Screen &screen() const { return m_screen; }
    const InputModes &modes() const { return m_modes; }
    /// The title the program set (OSC 0 / 2), "" when none.
    QString title() const { return m_title; }
    /// The working directory the shell reported (OSC 7 with a file: URL), "" when none.
    QString workingDirectory() const { return m_workingDirectory; }

    void resize(int columns, int rows);
    /// Everything back to a new terminal's state: the screens, the scrollback, modes, title.
    void reset();
    /// The colours OSC 10 / 11 / 12 queries are answered with (0xRRGGBB); none are answered before.
    void setReportedColors(quint32 foreground, quint32 background, quint32 cursor);

Q_SIGNALS:
    /// Bytes for the program's input: answers to its queries (DSR, DA, DECRQM, OSC colour queries).
    void reply(const QByteArray &data);
    void bell();
    void titleChanged(const QString &title);
    void workingDirectoryChanged(const QString &path);
    /// InputModes changed.
    void modesChanged();

private:
    void print(char32_t cp) override;
    void execute(char control) override;
    void escDispatch(QByteArrayView intermediates, char final) override;
    void csiDispatch(const CsiParams &params, QByteArrayView intermediates, char prefix, char final) override;
    void oscDispatch(const QByteArray &data) override;

    void selectGraphicRendition(const CsiParams &params);
    void setPrivateMode(int mode, bool on);
    void setAnsiMode(int mode, bool on);
    int privateModeState(int mode) const;
    void setModes(const InputModes &modes);
    void sendReply(const QByteArray &data);
    void setTitle(const QString &title);

    Screen m_screen;
    Parser m_parser;
    InputModes m_modes;
    QString m_title;
    QString m_workingDirectory;
    bool m_hasColors = false;
    quint32 m_colors[3] = {0, 0, 0};
};

} // namespace brulion::term
