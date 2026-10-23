// About the command line itself and the running app: version, help, schema, completion, mcp,
// init, app open/today/capture.
#include "commands_p.h"

#include "appmessage.h"
#include "notebook/itemref.h"
#include "singleinstance.h"
#include "vault/vault.h"

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

/// Send `message` to the running app; {sent, gui: {running}}.
Result sendToApp(Call &call, const QByteArray &message, QJsonObject result)
{
    ipc::SingleInstance app(ipc::SingleInstance::keyFor(call.ctx.appDataDir()));
    if (!app.isPrimaryRunning())
        return call.fail(ExitUnavailable, u"app_not_running"_s,
                         tr("Brulion is not running on this desktop (the app must run in this user session)"),
                         {{u"gui"_s, QJsonObject{{u"running"_s, false}}}});
    if (!app.sendToPrimary(message))
        return call.fail(ExitUnavailable, u"app_not_reached"_s, tr("The running app did not take the message"),
                         {{u"gui"_s, QJsonObject{{u"running"_s, true}}}});
    result.insert(u"gui"_s, QJsonObject{{u"running"_s, true}});
    return call.ok(result);
}

Result appOpen(Call &call)
{
    const QString target = call.str(u"target"_s).trimmed();
    if (target.isEmpty() || target == u"today")
        return sendToApp(call, ipc::encodeMessage("today"), {{u"sent"_s, u"today"_s}});
    if (target == u"capture")
        return sendToApp(call, ipc::encodeMessage("capture"), {{u"sent"_s, u"capture"_s}});
    if (auto e = call.ctx.resolveVault())
        return failWith(*e);
    const auto ref = call.page(target, false);
    if (!ref)
        return call.failed();
    QJsonObject route;
    if (ref->journal && !ref->item) {
        route = {{u"view"_s, u"journal"_s}, {u"date"_s, ref->journal->toString(Qt::ISODate)}, {u"path"_s, ref->path}};
    } else {
        if (!ref->exists)
            return call.fail(ExitNotFound, u"page_not_found"_s, tr("No page %1").arg(ref->path),
                             {{u"path"_s, ref->path}});
        route = {{u"view"_s, u"page"_s}, {u"path"_s, ref->path}};
        if (ref->item && ref->item->hasBlock()) {
            route.insert(u"blockId"_s, ref->item->blockId);
        } else if (ref->item && ref->item->hasLine()) {
            notebook::ItemRef item = *ref->item;
            const notebook::ResolvedLine line =
                    notebook::resolve(call.nb()->read(ref->path).text, item, call.vault()->config());
            route.insert(u"line"_s, line.line >= 0 ? line.line : item.line);
        }
    }
    const QString current = call.ctx.settingsValue(u"vaults/current"_s);
    if (!current.isEmpty() && Vault::idForPath(current) != Vault::idForPath(call.ctx.vaultRoot()))
        call.warn(u"other_notebook"_s,
                  tr("The app's notebook is %1: it will not open a page of %2").arg(current, call.ctx.vaultRoot()));
    return sendToApp(call, ipc::openMessage(route, call.ctx.vaultRoot()),
                     {{u"sent"_s, u"open"_s}, {u"route"_s, route}});
}

Result appToday(Call &call)
{
    return sendToApp(call, ipc::encodeMessage("today"), {{u"sent"_s, u"today"_s}});
}

Result appCapture(Call &call)
{
    return sendToApp(call, ipc::encodeMessage("capture"), {{u"sent"_s, u"capture"_s}});
}

Command make(const QString &id, const QString &summary, Handler run, QList<Arg> args)
{
    Command c;
    c.id = id;
    c.summary = summary;
    c.run = std::move(run);
    c.args = std::move(args);
    c.needsVault = false;
    return c;
}

} // namespace

void addMetaCommands(QList<Command> &out)
{
    Command c = make(
            u"app.open"_s, u"Show the running app at a page, a daily note (today) or quick capture"_s, appOpen,
            {positional(u"target"_s, ArgType::Ref,
                        u"A page ref (Dom, Dom.md#L12@hash, ^id), a date (today, jutro), or capture"_s, false, true)});
    c.help = u"Desktop only, the app of this user session (exit 5 with gui.running false when none runs). The app "
             u"opens pages of the notebook it shows."_s;
    out.append(c);
    out.append(make(u"app.today"_s, u"Show the running app at today's daily note"_s, appToday, {}));
    out.append(make(u"app.capture"_s, u"Open quick capture in the running app"_s, appCapture, {}));
    c = make(u"init"_s, u"Create a notebook folder (the only command that creates one)"_s, init,
             {positional(u"path"_s, ArgType::String, u"The folder"_s)});
    c.writes = true;
    c.mcp = false;
    out.append(c);
    c = make(u"version"_s, u"The version of brulion-cli (and of Qt, the MCP protocol)"_s, version, {});
    out.append(c);
    c = make(u"help"_s, u"Help: all commands, or one (help page read); --json describes them for programs"_s, help,
             {positional(u"topic"_s, ArgType::String, u"A command, e.g. \"task add\""_s, false, true),
              flag(u"json"_s, u"The commands as JSON"_s)});
    c.mcp = false;
    out.append(c);
    c = make(u"schema"_s, u"JSON Schema of the commands' arguments (all, or one)"_s, schema,
             {positional(u"command"_s, ArgType::String, u"A command, e.g. \"task add\""_s, false, true)});
    c.mcp = false;
    out.append(c);
    c = make(u"completion"_s, u"A shell completion script"_s, completion,
             {choice(u"shell"_s, {u"bash"_s, u"fish"_s, u"zsh"_s}, u"The shell"_s, true, true)});
    c.mcp = false;
    out.append(c);
    c = make(u"mcp"_s, u"Serve the commands as MCP tools over standard input/output (for AI agents)"_s, mcpCommand,
             {flag(u"readOnly"_s, u"Offer only the tools that read"_s),
              flag(u"allowDestructive"_s, u"Also offer the destructive tools (page write, rename, move, delete …)"_s)});
    c.help = u"JSON-RPC 2.0 over stdio, tools only; the notebook stays open and watched, so calls after the first "
             u"are fast. Give the notebook with --vault."_s;
    c.mcp = false;
    out.append(c);
}

} // namespace brulion::cli
