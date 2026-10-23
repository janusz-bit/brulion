// About the command line itself: version, help, schema, completion, mcp, init (the running app:
// commands_app.cpp).
#include "commands_p.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>

using namespace Qt::StringLiterals;

namespace brulion::cli {

namespace {

QString argTypeName(ArgType type)
{
    switch (type) {
    case ArgType::String: return u"string"_s;
    case ArgType::Text: return u"text"_s;
    case ArgType::Int: return u"integer"_s;
    case ArgType::Bool: return u"boolean"_s;
    case ArgType::Enum: return u"enum"_s;
    case ArgType::Ref: return u"ref"_s;
    case ArgType::Date: return u"date"_s;
    case ArgType::Path: return u"path"_s;
    }
    return {};
}

Result version(Call &call)
{
    return call.ok(QJsonObject{{u"version"_s, QCoreApplication::applicationVersion()},
                               {u"qt"_s, QString::fromLatin1(qVersion())},
                               {u"mcpProtocol"_s, mcpProtocolVersion()}},
                   u"brulion-cli "_s + QCoreApplication::applicationVersion());
}

Result help(Call &call)
{
    const QString topic = call.str(u"topic"_s).trimmed();
    const Command *command = nullptr;
    if (!topic.isEmpty()) {
        command = findCommand(QString(topic).replace(u' ', u'.'));
        if (!command)
            return call.fail(ExitUsage, u"unknown_command"_s, tr("Unknown command: %1").arg(topic));
    }
    if (!call.flag(u"json"_s)) {
        Result r = call.ok(QJsonValue());
        r.text = helpText(call.ctx.program, command);
        r.raw = true;
        return r;
    }
    QJsonArray list;
    for (const Command &c : commands()) {
        if (command && &c != command)
            continue;
        QJsonArray args;
        for (const Arg &a : c.args) {
            QJsonObject ao{{u"name"_s, a.name},
                           {u"type"_s, argTypeName(a.type)},
                           {u"help"_s, a.help},
                           {u"required"_s, a.required},
                           {u"positional"_s, a.positional},
                           {u"option"_s, a.positional ? QString() : optionName(a.name)}};
            if (!a.values.isEmpty())
                ao.insert(u"values"_s, QJsonArray::fromStringList(a.values));
            if (a.fromStdin)
                ao.insert(u"stdin"_s, true);
            args.append(ao);
        }
        QJsonObject co{{u"id"_s, c.id},
                       {u"command"_s, c.words().join(u' ')},
                       {u"summary"_s, c.summary},
                       {u"usage"_s, c.usage(call.ctx.program)},
                       {u"args"_s, args},
                       {u"writes"_s, c.writes},
                       {u"destructive"_s, c.destructive},
                       {u"needsVault"_s, c.needsVault},
                       {u"appControl"_s, c.appControl},
                       {u"mcpTool"_s, c.mcp ? toolName(c) : QString()}};
        if (!c.help.isEmpty())
            co.insert(u"help"_s, c.help);
        if (!c.aliasOf.isEmpty())
            co.insert(u"aliasOf"_s, c.aliasOf);
        list.append(co);
    }
    return call.ok(QJsonObject{{u"program"_s, call.ctx.program}, {u"commands"_s, list}});
}

Result schema(Call &call)
{
    const QString topic = call.str(u"command"_s).trimmed();
    if (topic.isEmpty())
        return call.ok(schemaDocument());
    const Command *command = findCommand(QString(topic).replace(u' ', u'.'));
    if (!command)
        return call.fail(ExitUsage, u"unknown_command"_s, tr("Unknown command: %1").arg(topic));
    QJsonObject s = inputSchema(*command, false);
    s.insert(u"$schema"_s, u"https://json-schema.org/draft/2020-12/schema"_s);
    s.insert(u"title"_s, command->words().join(u' '));
    s.insert(u"description"_s, command->summary);
    return call.ok(s);
}

Result completion(Call &call)
{
    const QString shell = call.str(u"shell"_s);
    Result r = call.ok(QJsonValue());
    r.text = completionScript(shell, call.ctx.program == u"brulion cli" ? u"brulion-cli"_s : call.ctx.program);
    r.raw = true;
    return r;
}

Result mcpCommand(Call &call)
{
    // `brulion-cli mcp` is started by run() itself (it serves until standard input ends).
    return call.fail(ExitUsage, u"not_a_tool"_s, tr("mcp starts the MCP server; it is no tool of its own"));
}

Result init(Call &call)
{
    const QString raw = call.str(u"path"_s).trimmed();
    const QString path = QDir::cleanPath(
            QFileInfo(raw.startsWith(u"~/"_s) ? QDir::homePath() + raw.mid(1) : raw).absoluteFilePath());
    const QFileInfo info(path);
    if (info.exists() && !info.isDir())
        return call.fail(ExitUsage, u"not_a_folder"_s, tr("%1 is a file").arg(path), {{u"path"_s, path}});
    const bool existed = info.exists();
    if (call.dryRun())
        return call.ok(QJsonObject{{u"path"_s, path}, {u"created"_s, false}, {u"dryRun"_s, true}});
    if (!existed && !QDir().mkpath(path))
        return call.fail(ExitUnavailable, u"cannot_create"_s, tr("Cannot create the folder %1").arg(path),
                         {{u"path"_s, path}});
    return call.ok(QJsonObject{{u"path"_s, path}, {u"created"_s, !existed}}, path);
}

Command make(const QString &id, const char *summary, Handler run, QList<Arg> args)
{
    Command c;
    c.id = id;
    c.summary = cliText(summary);
    c.run = std::move(run);
    c.args = std::move(args);
    c.needsVault = false;
    return c;
}

} // namespace

void addMetaCommands(QList<Command> &out)
{
    Command c = make(u"init"_s,
                     QT_TRANSLATE_NOOP("brulion::cli", "Create a notebook folder (the only command that creates one)"),
                     init, {positional(u"path"_s, ArgType::String, QT_TRANSLATE_NOOP("brulion::cli", "The folder"))});
    c.writes = true;
    c.mcp = false;
    out.append(c);
    c = make(u"version"_s,
             QT_TRANSLATE_NOOP("brulion::cli", "The version of brulion-cli (and of Qt, the MCP protocol)"), version,
             {});
    out.append(c);
    c = make(u"help"_s,
             QT_TRANSLATE_NOOP("brulion::cli",
                               "Help: all commands, or one (help page read); --json describes them for programs"),
             help,
             {positional(u"topic"_s, ArgType::String, QT_TRANSLATE_NOOP("brulion::cli", "A command, e.g. \"task add\""),
                         false, true),
              flag(u"json"_s, QT_TRANSLATE_NOOP("brulion::cli", "The commands as JSON"))});
    c.mcp = false;
    out.append(c);
    c = make(u"schema"_s, QT_TRANSLATE_NOOP("brulion::cli", "JSON Schema of the commands' arguments (all, or one)"),
             schema,
             {positional(u"command"_s, ArgType::String,
                         QT_TRANSLATE_NOOP("brulion::cli", "A command, e.g. \"task add\""), false, true)});
    c.mcp = false;
    out.append(c);
    c = make(u"completion"_s, QT_TRANSLATE_NOOP("brulion::cli", "A shell completion script"), completion,
             {choice(u"shell"_s, {u"bash"_s, u"fish"_s, u"zsh"_s}, QT_TRANSLATE_NOOP("brulion::cli", "The shell"), true,
                     true)});
    c.mcp = false;
    out.append(c);
    c = make(u"mcp"_s,
             QT_TRANSLATE_NOOP("brulion::cli",
                               "Serve the commands as MCP tools over standard input/output (for AI agents)"),
             mcpCommand,
             {flag(u"readOnly"_s, QT_TRANSLATE_NOOP("brulion::cli", "Offer only the tools that read")),
              flag(u"allowDestructive"_s,
                   QT_TRANSLATE_NOOP("brulion::cli",
                                     "Also offer the destructive tools (page write, rename, move, delete …)")),
              flag(u"appControl"_s,
                   QT_TRANSLATE_NOOP("brulion::cli",
                                     "Also offer the tools that drive the running app (app state, click, type, "
                                     "screenshot …; its remote control must be on)"))});
    c.help = cliText(QT_TRANSLATE_NOOP("brulion::cli",
                                       "JSON-RPC 2.0 over stdio, tools only; the notebook stays open and watched, so "
                                       "calls after the first are fast. Give the notebook with --vault."));
    c.mcp = false;
    out.append(c);
}

} // namespace brulion::cli
