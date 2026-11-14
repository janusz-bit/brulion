#include "extensions/sandbox.h"

#include "extensions/builtin.h"
#include "extensions/pageextension.h"
#include "markdown/parse.h"
#include "vault/vault.h"

using namespace Qt::StringLiterals;

namespace brulion::extensions {

namespace {

thread_local int g_depth = 0; // checks running on this thread (an engine and its host live on one thread)

QString statusName(Status s)
{
    switch (s) {
    case Status::Ok: return u"ok"_s;
    case Status::Error: return u"error"_s;
    case Status::Suspended: return u"waiting"_s;
    case Status::Timeout: return u"timeout"_s;
    case Status::OutOfMemory: return u"out_of_memory"_s;
    case Status::StackOverflow: return u"stack_overflow"_s;
    case Status::NotFound: return u"not_found"_s;
    }
    return u"error"_s;
}

} // namespace

QVariantMap checkExtension(Vault &vault, VaultIndex &index, const Extension &extension, NotebookHost::Options host,
                           const Limits &limits)
{
    QVariantMap out{{u"ok"_s, false}};
    if (g_depth > 0) {
        out.insert(u"status"_s, u"error"_s);
        out.insert(u"error"_s, u"an extension cannot be checked while another check runs"_s);
        return out;
    }
    struct Depth {
        Depth() { ++g_depth; }
        ~Depth() { --g_depth; }
    } depth;
    host.dryRun = true;
    NotebookHost notebook(vault, index, host);
    QStringList printed;
    notebook.onLog = [&printed](const QString &, const QString &text) { printed << text; };
    Engine engine;
    QString error;
    if (!engine.start(&notebook, limits, &error)) {
        out.insert(u"status"_s, u"error"_s);
        out.insert(u"error"_s, QString(u"the Luau engine did not start: "_s + error));
        return out;
    }
    const Result r = engine.load(extension);
    out.insert(u"ok"_s, r.ok());
    if (r.ok()) {
        QVariantList commands, views;
        for (const CommandInfo &c : engine.commands())
            if (c.extension == extension.id)
                commands << QVariantMap{{u"name"_s, c.name}, {u"label"_s, c.label}, {u"block"_s, c.needsBlock}};
        for (const ViewInfo &v : engine.views())
            if (v.extension == extension.id)
                views << QVariantMap{
                        {u"name"_s, v.name}, {u"label"_s, v.label}, {u"icon"_s, v.icon}, {u"actions"_s, v.actions}};
        QStringList hooks;
        for (const HookInfo &h : engine.hooks())
            if (h.extension == extension.id)
                hooks << h.event;
        out.insert(u"commands"_s, commands);
        out.insert(u"views"_s, views);
        out.insert(u"hooks"_s, hooks);
    } else {
        QString message = r.error;
        if (message.isEmpty())
            message = r.status == Status::Timeout       ? u"the extension took too long"_s
                      : r.status == Status::OutOfMemory ? u"the extension ran out of memory"_s
                                                        : u"the extension failed"_s;
        out.insert(u"status"_s, statusName(r.status));
        out.insert(u"error"_s, message.section(u'\n', 0, 0));
        if (message.contains(u'\n'))
            out.insert(u"traceback"_s, message);
        const int line = errorLine(extension.name.isEmpty() ? extension.id : extension.name, message);
        if (line >= 0)
            out.insert(u"line"_s, line + 1);
    }
    out.insert(u"printed"_s, printed);
    QVariantList writes;
    for (const NotebookHost::DryWrite &w : notebook.dryWrites())
        writes << QVariantMap{
                {u"path"_s, w.path}, {u"created"_s, !w.existed}, {u"before"_s, w.before}, {u"after"_s, w.after}};
    if (!writes.isEmpty())
        out.insert(u"writes"_s, writes);
    return out;
}

QVariantMap checkExtensionPage(Vault &vault, VaultIndex &index, const VaultPath &path, const QString &content,
                               const NotebookHost::Options &host, const Limits &limits)
{
    const ParsedNote note = markdown::parseNote(path, content, vault.config());
    const PageExtension page = readPageExtension(path, note, content);
    const bool tagged = isExtensionPage(note);
    const QString builtin = builtinFor(page.overrides.isEmpty() ? page.id : page.overrides);
    QVariantMap out;
    if (!tagged) {
        out = {{u"ok"_s, false},
               {u"status"_s, u"error"_s},
               {u"error"_s, u"not an extension page: its properties need the tag (tags: [extension])"_s}};
    } else if (page.blocks == 0) {
        out = {{u"ok"_s, false}, {u"status"_s, u"error"_s}, {u"error"_s, u"no ```luau block"_s}};
    } else {
        Extension x = page.extension();
        if (!builtin.isEmpty()) {
            x.id = builtin;
            x.data = builtinExtension(builtin).data;
        }
        out = checkExtension(vault, index, x, host, limits);
    }
    out.insert(u"isExtension"_s, tagged);
    out.insert(u"id"_s, builtin.isEmpty() ? page.id : builtin);
    out.insert(u"title"_s, page.title);
    out.insert(u"path"_s, path);
    out.insert(u"permissions"_s, page.permissions);
    out.insert(u"description"_s, page.description);
    if (!builtin.isEmpty())
        out.insert(u"overrides"_s, builtin);
    out.insert(u"problems"_s, page.problems);
    out.insert(u"blocks"_s, page.blocks);
    out.insert(u"codeHash"_s, page.hash);
    return out;
}

} // namespace brulion::extensions
