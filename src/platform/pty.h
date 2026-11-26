// A program on a pseudo-terminal (docs/CODE-TERMINAL.md "The pseudo-terminal"), for the Code mode's
// terminal panel: one backend per build (src/platform/CMakeLists.txt) —
//
//   pty_posix.cpp   Linux, macOS, the BSDs: openpty() + fork() + execve(); the child leads a session
//                   of its own with the pty as its controlling terminal; read through a
//                   QSocketNotifier on the non-blocking master; its exit seen through a pidfd (Linux)
//                   or by polling waitpid() — never a SIGCHLD handler, which belongs to QProcess
//   pty_conpty.cpp  Windows 10 1809 and later: a pseudo console (ConPTY, looked up at run time:
//                   available() is false on older Windows) with a reader and a writer thread on its
//                   pipes, the program in a job object (terminate() ends its whole tree), its exit
//                   seen through a QWinEventNotifier on the process handle
//   pty_none.cpp    Android, the browser: available() is false
//
//   platform::Pty pty;
//   connect(&pty, &Pty::dataReceived, terminal, &Terminal::feed);
//   pty.start(u"/bin/sh"_s, {}, home, Pty::defaultEnvironment(), 80, 24, &error);
//   pty.write("ls\r");  pty.resize(100, 30);  pty.terminate();
//
// POSIX: the program is execve()d as given, never through a shell string (a shell runs only when it
// is the program). Its environment is `environment` with TERM=xterm-256color, COLORTERM=truecolor
// and, when it names no locale at all, a UTF-8 one; COLUMNS and LINES are removed (the size is the
// pty's). It inherits no file descriptors but the pty, default signal dispositions and an empty
// signal mask. finished() comes once the child is reaped: no zombies.
//
// Windows: CreateProcessW of the program found by its full path (PATH searched with .exe / .com,
// never the current folder; a .bat or .cmd file is refused — run it through cmd.exe), the command
// line built from `arguments` by the rules CommandLineToArgvW and the C runtime read them back
// with, then nativeArguments() as it is. Its environment is `environment` (names in any case, the
// last entry of a name wins) with COLORTERM=truecolor; TERM, COLUMNS, LINES, TERMCAP and Windows
// Terminal's WT_SESSION / WT_PROFILE_ID are removed — the console is ConPTY's, as in Windows
// Terminal; its title starts as the program's name ("pwsh"). No handle is inherited. finished()
// comes once the program has ended and the pseudo console's last output is delivered; its exit code
// is the process's (an NTSTATUS such as 0xC000013A — ended by the console's close — as a negative
// number, `crashed` then).
#pragma once

#include <QByteArray>
#include <QObject>
#include <QString>
#include <QStringList>

#include <memory>

namespace brulion::platform {

class Pty : public QObject {
    Q_OBJECT

public:
    explicit Pty(QObject *parent = nullptr);
    /// Ends the program (SIGHUP, then SIGKILL after a short wait) and reaps it. Windows: the console
    /// closed (CTRL_CLOSE_EVENT to its programs), its job ended 200 ms later.
    ~Pty() override;

    /// Whether this build can run programs on a pseudo-terminal.
    static bool available();
    /// The process's environment as NAME=value entries (empty where there is no pty).
    static QStringList defaultEnvironment();
    /// The user's shell: $SHELL when it is an executable file, else the account's, else /bin/sh ("" without a
    /// pty). Windows: $SHELL when it names an existing .exe by its full path (Git Bash's
    /// C:\Program Files\Git\bin\bash.exe), else PowerShell 7 (pwsh.exe on PATH), else Windows
    /// PowerShell, else %ComSpec% (cmd.exe).
    static QString userShell();

    /// Starts `program` (a path, or a name looked up in the environment's PATH) with `arguments`
    /// in `workingDirectory` ("" or a missing folder: the home folder) on a new pty of `columns` ×
    /// `rows`. False with `error` set when there is no pty, one is running already, or the
    /// program cannot be started (the reason from exec() itself).
    bool start(const QString &program, const QStringList &arguments, const QString &workingDirectory,
               const QStringList &environment, int columns, int rows, QString *error = nullptr);
    bool isRunning() const;
    /// The child's process id (0 when none runs).
    qint64 processId() const;

    /// Input for the program; queued while the pty's buffer is full. False when none runs.
    bool write(const QByteArray &data);
    /// TIOCSWINSZ: the program gets SIGWINCH. Windows: ResizePseudoConsole.
    bool resize(int columns, int rows);
    /// SIGHUP (and SIGCONT) to the child's process group and the terminal's foreground group, SIGKILL
    /// after `graceMs` if it is still there. finished() follows. Windows: the pseudo console closed
    /// (its programs get CTRL_CLOSE_EVENT), the job — every process the program started — ended
    /// after `graceMs`.
    void terminate(int graceMs = 3000);

    /// Windows: text put as it is after the quoted arguments on the program's command line — for
    /// cmd.exe, whose /c takes the rest of the line by rules of its own (`/d /s /c "<command>"`).
    /// Kept for later starts; ignored on other systems.
    void setNativeArguments(const QString &arguments) { m_nativeArguments = arguments; }
    QString nativeArguments() const { return m_nativeArguments; }

    struct Private;

Q_SIGNALS:
    /// Output of the program (any split: UTF-8 and escape sequences may be cut).
    void dataReceived(const QByteArray &data);
    /// The child ended and was reaped: its exit code, or — `crashed` — the signal that ended it.
    /// Output it wrote before is delivered first.
    void finished(int exitCode, bool crashed);

private:
    std::unique_ptr<Private> d;
    QString m_nativeArguments;
};

} // namespace brulion::platform
