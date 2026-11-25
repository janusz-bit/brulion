// TerminalSession — one terminal of the Code mode's terminal panel (docs/CODE-TERMINAL.md): a
// program on a pseudo-terminal (platform::Pty) and the emulator its output goes through
// (term::Terminal: the screen, the scrollback, the modes). TerminalView shows it and sends it keys.
//
//   TerminalSession { id: shell; folder: "/home/me/project" }              // the user's shell
//   TerminalSession { folder: root.folder; command: "npm test"; label: qsTr("Tests") }
//   shell.start(); shell.paste(text); shell.stop()
//
// `command` empty: the user's shell ($SHELL, else the account's, else /bin/sh) as an interactive,
// non-login shell. Otherwise the command runs through that shell as `$SHELL -lc command` — the shell
// is the program, never a shell string of our own — and the session ends with it. start() after the
// end runs it again on a clean screen. The child inherits the app's environment (with the pty's
// TERM; pty.h) minus the AppImage runtime's own variables. Where there is no pty (`available`
// false: Windows, Android, the browser) start() does nothing.
#pragma once

#include "term/terminal.h"

#include <QObject>
#include <QString>

namespace brulion::platform {
class Pty;
}

namespace brulion::terminal {

class TerminalSession : public QObject {
    Q_OBJECT
    /// The working directory the program starts in ("": the home folder).
    Q_PROPERTY(QString folder READ folder WRITE setFolder NOTIFY folderChanged FINAL)
    /// What runs: "" the user's shell, else a command line for it.
    Q_PROPERTY(QString command READ command WRITE setCommand NOTIFY commandChanged FINAL)
    /// A name for the tab when the program sets no title (the panel's run(command, label)).
    Q_PROPERTY(QString label READ label WRITE setLabel NOTIFY labelChanged FINAL)
    /// The program's title (OSC 0 / 2), else `label`, else the command, else the shell's name.
    Q_PROPERTY(QString title READ title NOTIFY titleChanged FINAL)
    /// The directory the shell reported last (OSC 7), "" when it says nothing.
    Q_PROPERTY(QString workingDirectory READ workingDirectory NOTIFY workingDirectoryChanged FINAL)
    Q_PROPERTY(bool running READ isRunning NOTIFY runningChanged FINAL)
    /// It ran and ended (the panel's "Process exited with code N" and its Restart).
    Q_PROPERTY(bool hasExited READ hasExited NOTIFY runningChanged FINAL)
    /// The exit code; 128 + the signal's number when a signal ended it; -1 before it ended.
    Q_PROPERTY(int exitCode READ exitCode NOTIFY runningChanged FINAL)
    /// Why the last start() failed ("" when it did not).
    Q_PROPERTY(QString errorString READ errorString NOTIFY errorStringChanged FINAL)
    /// Whether this device can run a terminal at all (platform::Pty::available()).
    Q_PROPERTY(bool available READ available CONSTANT FINAL)
    Q_PROPERTY(int columns READ columns NOTIFY sizeChanged FINAL)
    Q_PROPERTY(int rows READ rows NOTIFY sizeChanged FINAL)

public:
    explicit TerminalSession(QObject *parent = nullptr);
    ~TerminalSession() override;

    QString folder() const { return m_folder; }
    void setFolder(const QString &folder);
    QString command() const { return m_command; }
    void setCommand(const QString &command);
    QString label() const { return m_label; }
    void setLabel(const QString &label);
    QString title() const;
    QString workingDirectory() const { return m_terminal.workingDirectory(); }
    bool isRunning() const { return m_running; }
    bool hasExited() const { return m_exited; }
    int exitCode() const { return m_exitCode; }
    QString errorString() const { return m_error; }
    static bool available();
    int columns() const { return m_terminal.screen().columns(); }
    int rows() const { return m_terminal.screen().rows(); }

    /// Starts the program (again, on a clean screen, after it ended). False when it cannot start
    /// (errorString says why) or there is no terminal here; true when it runs already.
    Q_INVOKABLE bool start();
    /// Hangs up on the program (SIGHUP to it and its jobs, SIGKILL a little later).
    Q_INVOKABLE void stop();
    /// `text` for the program as it is (UTF-8): what a script types.
    Q_INVOKABLE void write(const QString &text);
    /// `text` as a paste: line breaks as Enter, control characters dropped, between the bracketed
    /// paste marks when the program asked for them.
    Q_INVOKABLE void paste(const QString &text);
    /// The grid's size (TerminalView sets it from its own): the screen and the pty.
    Q_INVOKABLE void resize(int columns, int rows);

    /// Raw input bytes (keys already encoded by term::encodeKey()).
    void writeBytes(const QByteArray &bytes);
    term::Terminal &terminal() { return m_terminal; }
    const term::Terminal &terminal() const { return m_terminal; }
    /// The program's command line as started ("" before).
    QStringList commandLine() const { return m_commandLine; }

Q_SIGNALS:
    void folderChanged();
    void commandChanged();
    void labelChanged();
    void titleChanged();
    void workingDirectoryChanged();
    void runningChanged();
    void errorStringChanged();
    void sizeChanged();
    /// The program rang the bell.
    void bell();
    void started();
    /// The program ended: exitCode as the property says.
    void exited(int exitCode);
    /// Output changed the screen (term::Screen's dirty rows say where).
    void contentChanged();
    /// The input modes changed (term::InputModes: cursor keys, mouse, bracketed paste).
    void modesChanged();

private:
    void onData(const QByteArray &data);
    void onFinished(int exitCode, bool crashed);
    void setError(const QString &error);
    QString programName() const;

    term::Terminal m_terminal;
    platform::Pty *m_pty = nullptr;
    QString m_folder;
    QString m_command;
    QString m_label;
    QString m_error;
    QStringList m_commandLine;
    bool m_running = false;
    bool m_exited = false;
    int m_exitCode = -1;
};

} // namespace brulion::terminal
