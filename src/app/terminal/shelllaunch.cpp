#include "shelllaunch.h"

#include <QByteArray>

#include <algorithm>

using namespace Qt::StringLiterals;

namespace brulion::terminal {

ShellKind shellKind(const QString &shell)
{
    // (The name, not the path: QFileInfo of "C:\\…\\pwsh.exe" on Linux would not split it.)
    QString name = shell;
    const qsizetype slash = std::max(name.lastIndexOf(u'/'), name.lastIndexOf(u'\\'));
    if (slash >= 0)
        name = name.mid(slash + 1);
    name = name.toLower();
    if (name.endsWith(u".exe"_s))
        name.chop(4);
    if (name == u"pwsh"_s || name == u"powershell"_s)
        return ShellKind::PowerShell;
    if (name == u"cmd"_s)
        return ShellKind::Cmd;
    return ShellKind::Posix;
}

QString powerShellScript(const QString &command)
{
    // ($? first: any other expression would reset it.)
    return command + u"\nexit $(if ($?) { 0 } elseif ($LASTEXITCODE) { $LASTEXITCODE } else { 1 })"_s;
}

QString powerShellEncoded(const QString &script)
{
    QByteArray utf16;
    utf16.reserve(script.size() * 2);
    for (const QChar c : script) {
        utf16.append(char(c.unicode() & 0xff));
        utf16.append(char(c.unicode() >> 8));
    }
    return QString::fromLatin1(utf16.toBase64());
}

ShellLaunch shellLaunch(ShellKind kind, const QString &command)
{
    ShellLaunch launch;
    switch (kind) {
    case ShellKind::PowerShell:
        launch.arguments = {u"-NoLogo"_s};
        if (!command.isEmpty())
            launch.arguments << u"-EncodedCommand"_s << powerShellEncoded(powerShellScript(command));
        break;
    case ShellKind::Cmd:
        if (!command.isEmpty())
            launch.nativeArguments = u"/d /s /c \""_s + command + u'"';
        break;
    case ShellKind::Posix:
        if (command.isEmpty())
            launch.arguments = {u"-i"_s, u"-l"_s};
        else
            launch.arguments = {u"-lc"_s, command};
        break;
    }
    return launch;
}

} // namespace brulion::terminal
