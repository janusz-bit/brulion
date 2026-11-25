// A program on a pseudo-terminal (docs/CODE-TERMINAL.md "The pseudo-terminal"), for the Code mode's
// terminal panel: one backend per build (src/platform/CMakeLists.txt) —
//
//   pty_posix.cpp  Linux, macOS, the BSDs: openpty() + fork() + execve(); the child leads a session
//                  of its own with the pty as its controlling terminal; read through a
//                  QSocketNotifier on the non-blocking master; its exit seen through a pidfd (Linux)
//                  or by polling waitpid() — never a SIGCHLD handler, which belongs to QProcess
//   pty_none.cpp   Windows (ConPTY would go here), Android, the browser: available() is false
//
//   platform::Pty pty;
//   connect(&pty, &Pty::dataReceived, terminal, &Terminal::feed);
//   pty.start(u"/bin/sh"_s, {}, home, Pty::defaultEnvironment(), 80, 24, &error);
//   pty.write("ls\r");  pty.resize(100, 30);  pty.terminate();
//
// The program is execve()d as given, never through a shell string (a shell runs only when it is
// the program). Its environment is `environment` with TERM=xterm-256color, COLORTERM=truecolor and,
// when it names no locale at all, a UTF-8 one; COLUMNS and LINES are removed (the size is the
// pty's). It inherits no file descriptors but the pty, default signal dispositions and an empty
// signal mask. finished() comes once the child is reaped: no zombies.
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
    /// Ends the program (SIGHUP, then SIGKILL after a short wait) and reaps it.
    ~Pty() override;

    /// Whether this build can run programs on a pseudo-terminal.
    static bool available();
    /// The process's environment as NAME=value entries (empty where there is no pty).
    static QStringList defaultEnvironment();
    /// The user's shell: $SHELL when it is an executable file, else /bin/sh ("" without a pty).
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
    /// TIOCSWINSZ: the program gets SIGWINCH.
    bool resize(int columns, int rows);
    /// SIGHUP (and SIGCONT) to the child's process group and the terminal's foreground group, SIGKILL
    /// after `graceMs` if it is still there. finished() follows.
    void terminate(int graceMs = 3000);

    struct Private;

Q_SIGNALS:
    /// Output of the program (any split: UTF-8 and escape sequences may be cut).
    void dataReceived(const QByteArray &data);
    /// The child ended and was reaped: its exit code, or — `crashed` — the signal that ended it.
    /// Output it wrote before is delivered first.
    void finished(int exitCode, bool crashed);

private:
    std::unique_ptr<Private> d;
};

} // namespace brulion::platform
