// How a TerminalSession starts the user's shell on Windows (docs/CODE-TERMINAL.md "TerminalSession"):
// by itself, or with a command line — a project's script — run through it. On Linux and macOS the
// session passes `-lc command` to $SHELL; Windows' shells take a command in three different ways:
//
//   PowerShell (pwsh, powershell)  -NoLogo -EncodedCommand <base64 of UTF-16LE>: no quoting at all
//                                  between the user's text and PowerShell's parser; the script ends
//                                  with a line that exits with the command's own code (-Command
//                                  alone makes every failure a 1)
//   cmd.exe                        /d /s /c "<command>" as native arguments (Pty::setNativeArguments):
//                                  /s takes the line between the outer quotes as it is, so quotes,
//                                  & and | in the command are cmd's, as if typed; /d skips AutoRun
//   anything else (Git Bash …)     -lc <command>, as on Linux (MSYS reads its command line with the
//                                  rules CommandLineToArgvW has, which the Pty quotes by)
//
// The command is the user's own (a project's script, typed by them): it is run as their shell
// would run it typed, never parsed or built into a shell string by Brulion. Pure: tested on every
// platform (tst_terminal_shell).
#pragma once

#include <QString>
#include <QStringList>

namespace brulion::terminal {

enum class ShellKind { Posix, PowerShell, Cmd };

/// By the program's file name, in any case, with or without ".exe": pwsh, powershell → PowerShell;
/// cmd → Cmd; anything else (bash, sh, zsh, fish, nu …) → Posix.
ShellKind shellKind(const QString &shell);

struct ShellLaunch {
    /// After the program on its command line (quoted by the Pty).
    QStringList arguments;
    /// After those, as they are (Pty::setNativeArguments()).
    QString nativeArguments;
};

/// What the shell `kind` gets: `command` "" — the interactive shell: PowerShell -NoLogo, a POSIX
/// shell -i -l (Git Bash's own start), cmd nothing; else the command run through it (above).
ShellLaunch shellLaunch(ShellKind kind, const QString &command);

/// The script PowerShell runs for `command`: the command, then a line that exits with its code —
/// 0 when it succeeded, else a native program's $LASTEXITCODE, else (a cmdlet failed) 1.
QString powerShellScript(const QString &command);
/// `script` as -EncodedCommand takes it: base64 of its UTF-16LE.
QString powerShellEncoded(const QString &script);

} // namespace brulion::terminal
