// How a terminal session starts Windows' shells (src/app/terminal/shelllaunch.h), pure, so tested
// on every platform: the kind of a shell by its name (paths of both systems, any case, with and
// without .exe), PowerShell's -EncodedCommand (Microsoft's own example, Polish letters, characters
// beyond the BMP, quotes and line breaks survive) and the line that keeps a command's exit code,
// cmd.exe's /d /s /c with the command as it is, a POSIX shell's -lc and its interactive -i -l.
#include "shelllaunch.h"

#include <QTest>

using namespace Qt::StringLiterals;
using brulion::terminal::ShellKind;
using brulion::terminal::ShellLaunch;
using brulion::terminal::powerShellEncoded;
using brulion::terminal::powerShellScript;
using brulion::terminal::shellKind;
using brulion::terminal::shellLaunch;

Q_DECLARE_METATYPE(ShellKind)

namespace {

/// -EncodedCommand's text back: base64 → UTF-16LE.
QString decoded(const QString &encoded)
{
    const QByteArray bytes = QByteArray::fromBase64(encoded.toLatin1());
    QString out;
    for (qsizetype i = 0; i + 1 < bytes.size(); i += 2)
        out += QChar(char16_t(quint8(bytes.at(i)) | (quint8(bytes.at(i + 1)) << 8)));
    return out;
}

} // namespace

class TestTerminalShell : public QObject {
    Q_OBJECT

private Q_SLOTS:
    void kind_data();
    void kind();
    void powerShellEncoding();
    void powerShellLaunch_data();
    void powerShellLaunch();
    void cmdLaunch_data();
    void cmdLaunch();
    void posixLaunch();
};

void TestTerminalShell::kind_data()
{
    QTest::addColumn<QString>("shell");
    QTest::addColumn<ShellKind>("kind");
    QTest::newRow("pwsh") << u"pwsh"_s << ShellKind::PowerShell;
    QTest::newRow("pwsh.exe") << u"pwsh.exe"_s << ShellKind::PowerShell;
    QTest::newRow("pwsh path") << u"C:\\Program Files\\PowerShell\\7\\pwsh.exe"_s << ShellKind::PowerShell;
    QTest::newRow("Windows PowerShell") << u"C:\\Windows\\System32\\WindowsPowerShell\\v1.0\\POWERSHELL.EXE"_s
                                        << ShellKind::PowerShell;
    QTest::newRow("pwsh on Linux") << u"/usr/bin/pwsh"_s << ShellKind::PowerShell;
    QTest::newRow("cmd") << u"C:\\Windows\\system32\\cmd.exe"_s << ShellKind::Cmd;
    QTest::newRow("CMD.EXE, slashes") << u"C:/Windows/System32/CMD.EXE"_s << ShellKind::Cmd;
    QTest::newRow("cmd alone") << u"cmd"_s << ShellKind::Cmd;
    QTest::newRow("Git Bash") << u"C:\\Program Files\\Git\\bin\\bash.exe"_s << ShellKind::Posix;
    QTest::newRow("sh") << u"/bin/sh"_s << ShellKind::Posix;
    QTest::newRow("zsh") << u"zsh"_s << ShellKind::Posix;
    QTest::newRow("not cmd") << u"C:\\tools\\cmdx.exe"_s << ShellKind::Posix;
    QTest::newRow("a folder named pwsh") << u"C:\\pwsh\\nu.exe"_s << ShellKind::Posix;
    QTest::newRow("Polish path") << u"C:\\Użytkownicy\\Łucja\\bin\\pwsh.exe"_s << ShellKind::PowerShell;
    QTest::newRow("empty") << QString() << ShellKind::Posix;
}

void TestTerminalShell::kind()
{
    QFETCH(QString, shell);
    QFETCH(ShellKind, kind);
    QCOMPARE(shellKind(shell), kind);
}

void TestTerminalShell::powerShellEncoding()
{
    // about_PowerShell_exe's example.
    QCOMPARE(powerShellEncoded(u"dir \"c:\\program files\" "_s),
             u"ZABpAHIAIAAiAGMAOgBcAHAAcgBvAGcAcgBhAG0AIABmAGkAbABlAHMAIgAgAA=="_s);
    QCOMPARE(powerShellEncoded(QString()), QString());
    const QString text = u"Write-Output 'zażółć gęślą jaźń 🦖'\r\n\"$env:PATH\" `n & | ;"_s;
    QCOMPARE(decoded(powerShellEncoded(text)), text);
}

void TestTerminalShell::powerShellLaunch_data()
{
    QTest::addColumn<QString>("command");
    QTest::newRow("npm") << u"npm run dev"_s;
    QTest::newRow("Polish and quotes") << u"Write-Output \"zażółć\"; echo 'gęślą'"_s;
    QTest::newRow("a comment at the end") << u"npm test # wszystkie"_s;
    QTest::newRow("cmd's characters") << u"cmd /c \"echo a & echo b | more\""_s;
    QTest::newRow("percent and backslashes") << u"echo %PATH% C:\\temp\\ \\\\server\\share\\"_s;
}

void TestTerminalShell::powerShellLaunch()
{
    QFETCH(QString, command);
    const ShellLaunch launch = shellLaunch(ShellKind::PowerShell, command);
    QCOMPARE(launch.nativeArguments, QString());
    QCOMPARE(launch.arguments.size(), 3);
    QCOMPARE(launch.arguments.at(0), u"-NoLogo"_s);
    QCOMPARE(launch.arguments.at(1), u"-EncodedCommand"_s);
    // Base64 only: nothing in it for a command line's quoting to touch.
    for (const QChar c : launch.arguments.at(2))
        QVERIFY(c.isLetterOrNumber() || c == u'+' || c == u'/' || c == u'=');
    const QString script = decoded(launch.arguments.at(2));
    QCOMPARE(script, powerShellScript(command));
    // The command as it is, then — on a line of its own, so a comment cannot hide it — the exit.
    QVERIFY(script.startsWith(command + u'\n'));
    const QString last = script.mid(command.size() + 1);
    QVERIFY(!last.contains(u'\n'));
    QVERIFY(last.startsWith(u"exit $(if ($?)"_s));
    QVERIFY(last.contains(u"$LASTEXITCODE"_s));

    // By itself: an interactive shell without the banner.
    QCOMPARE(shellLaunch(ShellKind::PowerShell, QString()).arguments, QStringList{u"-NoLogo"_s});
}

void TestTerminalShell::cmdLaunch_data()
{
    QTest::addColumn<QString>("command");
    QTest::newRow("npm") << u"npm run dev"_s;
    QTest::newRow("quotes and operators") << u"echo \"zażółć gęślą\" & exit 3"_s;
    QTest::newRow("starts and ends with quotes") << u"\"C:\\Program Files\\nodejs\\node.exe\" \"skrypt.js\""_s;
    QTest::newRow("percent") << u"echo %USERPROFILE%"_s;
}

void TestTerminalShell::cmdLaunch()
{
    QFETCH(QString, command);
    const ShellLaunch launch = shellLaunch(ShellKind::Cmd, command);
    QVERIFY(launch.arguments.isEmpty());
    // /s: cmd takes what is between the first and the last quote as it is.
    QCOMPARE(launch.nativeArguments, u"/d /s /c \""_s + command + u'"');
    const ShellLaunch shell = shellLaunch(ShellKind::Cmd, QString());
    QVERIFY(shell.arguments.isEmpty());
    QVERIFY(shell.nativeArguments.isEmpty());
}

void TestTerminalShell::posixLaunch()
{
    const ShellLaunch command = shellLaunch(ShellKind::Posix, u"echo \"zażółć\" && exit 3"_s);
    QCOMPARE(command.arguments, (QStringList{u"-lc"_s, u"echo \"zażółć\" && exit 3"_s}));
    QVERIFY(command.nativeArguments.isEmpty());
    QCOMPARE(shellLaunch(ShellKind::Posix, QString()).arguments, (QStringList{u"-i"_s, u"-l"_s}));
}

QTEST_GUILESS_MAIN(TestTerminalShell)
#include "tst_terminal_shell.moc"
