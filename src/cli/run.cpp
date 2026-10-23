// brulion::cli::run — the program: Qt application object, arguments, one command, output.
#include "cli.h"
#include "cli_p.h"

#include <QCoreApplication>
#include <QFileInfo>
#include <QJsonDocument>

#include <atomic>
#include <cstdio>

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

} // namespace

int run(int argc, char **argv)
{
    // Arguments are UTF-8 whatever the locale (LANG=C must not mangle "żółw").
    QStringList arguments;
    for (int i = 1; i < argc; ++i)
        arguments.append(QString::fromUtf8(argv[i]));
    const QString programPath = argc > 0 ? QString::fromUtf8(argv[0]) : u"brulion-cli"_s;
    // `brulion cli …` hands over argv starting at "cli".
    const QString program = QFileInfo(programPath).fileName() == u"cli" ? u"brulion cli"_s : u"brulion-cli"_s;

    qInstallMessageHandler(messageHandler);
    static int qtArgc = 1;
    static char *qtArgv[] = {argc > 0 ? argv[0] : const_cast<char *>("brulion-cli"), nullptr};
    QCoreApplication app(qtArgc, qtArgv);
    // The app's names: the same settings file, app data (History, LocalStore) and socket.
    QCoreApplication::setOrganizationName(u"Brulion"_s);
    QCoreApplication::setApplicationName(u"Brulion"_s);
    QCoreApplication::setOrganizationDomain(u"januszbit.github.io"_s);
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
                        inv.globals.allowDestructive || inv.args.value(u"allowDestructive"_s).toBool());
    return output(inv.command, execute(ctx, *inv.command, inv.args));
}

} // namespace brulion::cli
