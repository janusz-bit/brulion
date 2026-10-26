// brulion::cli::run — the program: Qt application object, arguments, one command, output.
#include "cli.h"
#include "cli_p.h"
#include "profile.h" // the app's names (BRULION_PROFILE)

#include <QCoreApplication>
#include <QFileInfo>
#include <QJsonDocument>
#include <QScopeGuard>

#include <atomic>
#include <cstdio>

#ifdef Q_OS_WIN
#include <fcntl.h>
#include <io.h>
#include <windows.h>
#include <shellapi.h>
#endif

using namespace Qt::StringLiterals;

namespace brulion::cli {

namespace {

std::atomic_bool g_verbose{false};

/// Qt's messages (the vault's log lines …) go to standard error with -v only: standard output is
/// the JSON, standard error stays quiet for agents.
void messageHandler(QtMsgType type, const QMessageLogContext &, const QString &message)
{
    if (!g_verbose && type != QtFatalMsg)
        return;
    const QByteArray line = message.toUtf8() + '\n';
    std::fwrite(line.constData(), 1, size_t(line.size()), stderr);
    std::fflush(stderr);
}

void print(const QString &text)
{
    const QByteArray bytes = text.toUtf8();
    std::fwrite(bytes.constData(), 1, size_t(bytes.size()), stdout);
    std::fflush(stdout);
}

/// The arguments after the program's name. Linux: argv, read as UTF-8 whatever the locale (LANG=C
/// must not mangle "żółw"). Windows: argv is in the ANSI code page, which lacks letters such as "ż"
/// on most systems, so the command line comes from the system as UTF-16; `brulion cli …` passes
/// argv from "cli" on, which the count tells.
QStringList commandLineArguments(int argc, char **argv)
{
#ifdef Q_OS_WIN
    int count = 0;
    if (LPWSTR *wide = ::CommandLineToArgvW(::GetCommandLineW(), &count)) {
        QStringList all;
        for (int i = 0; i < count; ++i)
            all.append(QString::fromWCharArray(wide[i]));
        ::LocalFree(wide);
        if (argc > 0 && count >= argc)
            return all.mid(count - argc + 1);
    }
#endif
    QStringList arguments;
    for (int i = 1; i < argc; ++i)
        arguments.append(QString::fromUtf8(argv[i]));
    return arguments;
}

} // namespace

int run(int argc, char **argv)
{
    const QStringList arguments = commandLineArguments(argc, argv);
#ifdef Q_OS_WIN
    // Bytes in and out as on Linux: no CRLF for LF and no end of input at Ctrl+Z (the C runtime's
    // text mode); UTF-8 also in a console window, set back to its code page at the end.
    // (A GUI program's streams may be closed: _fileno() < 0, which _setmode() must not get.)
    for (FILE *stream : {stdin, stdout, stderr}) {
        if (const int fd = _fileno(stream); fd >= 0)
            _setmode(fd, _O_BINARY);
    }
    const UINT consoleCodePage = ::GetConsoleOutputCP();
    if (consoleCodePage != 0)
        ::SetConsoleOutputCP(CP_UTF8);
    const auto restoreCodePage = qScopeGuard([consoleCodePage] {
        if (consoleCodePage != 0)
            ::SetConsoleOutputCP(consoleCodePage);
    });
#endif
    const QString programPath = argc > 0 ? QString::fromUtf8(argv[0]) : u"brulion-cli"_s;
    // `brulion cli …` hands over argv starting at "cli".
    const QString program = QFileInfo(programPath).fileName() == u"cli" ? u"brulion cli"_s : u"brulion-cli"_s;

    qInstallMessageHandler(messageHandler);
    static int qtArgc = 1;
    static char *qtArgv[] = {argc > 0 ? argv[0] : const_cast<char *>("brulion-cli"), nullptr};
    QCoreApplication app(qtArgc, qtArgv);
    // The app's names: the same settings file, app data (History, LocalStore) and socket — also
    // for a separate profile (BRULION_PROFILE, src/ipc/profile.h).
    brulion::ipc::applyAppNames();
    QCoreApplication::setApplicationVersion(QStringLiteral(BRULION_VERSION));

    const Invocation inv = parseArguments(arguments);
    g_verbose = inv.globals.verbose;
    Context ctx;
    ctx.globals = inv.globals;
    ctx.program = program;
    const auto output = [&](const Command *command, const Result &result) {
        if (result.raw) {
            print(result.text);
        } else if (ctx.globals.format == u"text") {
            print(renderText(command, result));
        } else {
            const QJsonDocument doc(envelope(ctx, command, result));
            print(QString::fromUtf8(
                    doc.toJson(ctx.globals.compact ? QJsonDocument::Compact : QJsonDocument::Indented)));
            if (ctx.globals.compact)
                print(u"\n"_s);
        }
        return exitCode(result);
    };
    if (inv.globals.help && !inv.error) {
        Result help;
        help.text = helpText(program, inv.command);
        help.raw = true;
        return output(inv.command, help);
    }
    if (inv.error) {
        Result failed;
        failed.error = inv.error;
        return output(inv.command, failed);
    }
    if (inv.command->id == u"mcp")
        return serveMcp(ctx, inv.globals.readOnly || inv.args.value(u"readOnly"_s).toBool(),
                        inv.globals.allowDestructive || inv.args.value(u"allowDestructive"_s).toBool(),
                        inv.args.value(u"appControl"_s).toBool());
    return output(inv.command, execute(ctx, *inv.command, inv.args));
}

} // namespace brulion::cli
