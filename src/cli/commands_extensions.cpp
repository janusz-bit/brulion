// Extensions (docs/CLI.md "Extensions", docs/EXTENSIONS.md): what an AI agent needs to write one —
// the reference (extension docs), the extensions there are (list, show), the code tried in a
// sandbox without approval and without writing (check, run, eval: a fresh engine over a dry-run
// NotebookHost, writes reported as diffs), and the page written (write). Nothing here approves
// code: the app runs a page's new code only after the user approves it on that device.
#include "commands_p.h"

#include "extensions/builtin.h"
#include "extensions/engine.h"
#include "extensions/macros.h"
#include "extensions/notebookhost.h"
#include "extensions/pageextension.h"
#include "extensions/sandbox.h"
#include "extensions/share.h"
#include "index/vaultindex.h"
#include "markdown/journal.h"
#include "markdown/parse.h"
#include "notebook/itemref.h"
#include "notebook/pages.h"
#include "notebook/session.h"
#include "text/text.h"
#include "util/paths.h"
#include "vault/localstore.h"
#include "vault/vault.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QRegularExpression>
#include <QSet>

#include <algorithm>
#include <memory>

using namespace Qt::StringLiterals;

namespace brulion::cli {

namespace {

namespace ext = brulion::extensions;

const QString kConsole = u"*console*"_s;

// ---- the reference --------------------------------------------------------------------------------

/// docs/EXTENSIONS.md, bundled with the extensions module (:/extensions/EXTENSIONS.md).
QString reference()
{
    QFile f(u":/extensions/EXTENSIONS.md"_s);
    return f.open(QIODevice::ReadOnly) ? QString::fromUtf8(f.readAll()) : QString();
}

struct Heading {
    int line = 0;
    int level = 0;
    QString text;
};

QList<Heading> headings(const QStringList &lines)
{
    QList<Heading> out;
    QString fence; // the open code block's fence ("```", "````", "~~~"); "" = none
    for (int i = 0; i < lines.size(); ++i) {
        const QString &l = lines.at(i);
        const QChar c = l.isEmpty() ? QChar() : l.at(0);
        qsizetype run = 0;
        while ((c == u'`' || c == u'~') && run < l.size() && l.at(run) == c)
            ++run;
        if (run >= 3) {
            const QString rest = l.mid(run);
            // A fence (CommonMark): no backtick after a backtick run ("```` ```luau ````" is prose).
            if (fence.isEmpty() && !(c == u'`' && rest.contains(u'`'))) {
                fence = l.left(run);
                continue;
            }
            if (!fence.isEmpty() && c == fence.at(0) && run >= fence.size() && rest.trimmed().isEmpty()) {
                fence.clear();
                continue;
            }
        }
        if (!fence.isEmpty() || !l.startsWith(u'#'))
            continue;
        int level = 0;
        while (level < l.size() && l.at(level) == u'#')
            ++level;
        if (level > 6 || level >= l.size() || l.at(level) != u' ')
            continue;
        out.append({i, level, l.mid(level + 1).trimmed()});
    }
    return out;
}

Result extensionDocs(Call &call)
{
    const QString doc = reference();
    if (doc.isEmpty())
        return call.fail(ExitInternal, u"no_reference"_s, tr("The extension reference is missing from this build"));
    const QStringList lines = doc.split(u'\n');
    const QList<Heading> all = headings(lines);
    QJsonArray sections;
    for (const Heading &h : all)
        if (h.level >= 2 && h.level <= 3)
            sections.append(h.text);
    const QString wanted = text::foldKey(call.str(u"section"_s).trimmed());
    if (wanted.isEmpty())
        return call.ok(QJsonObject{{u"sections"_s, sections}, {u"text"_s, doc}}, doc);
    QStringList parts;
    QJsonArray found;
    for (qsizetype k = 0; k < all.size(); ++k) {
        const Heading &h = all.at(k);
        if (h.level < 2 || !text::foldKey(h.text).contains(wanted))
            continue;
        qsizetype end = lines.size();
        for (qsizetype n = k + 1; n < all.size(); ++n)
            if (all.at(n).level <= h.level) {
                end = all.at(n).line;
                break;
            }
        parts << lines.mid(h.line, end - h.line).join(u'\n').trimmed();
        found.append(h.text);
        // Its subsections are in it already.
        while (k + 1 < all.size() && all.at(k + 1).line < end)
            ++k;
    }
    if (parts.isEmpty())
        return call.fail(ExitNotFound, u"section_not_found"_s,
                         tr("No section of the extension reference is called %1").arg(call.str(u"section"_s)),
                         {{u"sections"_s, sections}});
    const QString text = parts.join(u"\n\n"_s) + u'\n';
    return call.ok(QJsonObject{{u"sections"_s, found}, {u"text"_s, text}}, text);
}

// ---- the extensions of the notebook ---------------------------------------------------------------

struct Trust {
    QSet<QString> approved;
    QSet<QString> off;
};

/// What the app approved and switched off on this device (read only: the app writes it).
Trust trust(Call &call)
{
    Trust t;
    Vault *v = call.vault();
    LocalStore *store = v ? v->store() : nullptr;
    if (!store)
        return t;
    for (const QJsonValue h : store->get(ext::approvedStoreKey()).toArray())
        t.approved.insert(h.toString());
    for (const QJsonValue id : store->get(ext::offStoreKey()).toArray())
        t.off.insert(id.toString());
    return t;
}

/// An extension: a page of the notebook or a built-in one.
struct Target {
    bool isPage = false;
    ext::PageExtension page;
    QString content;     ///< the page's text
    QString builtin;     ///< the built-in it is, or that the page overrides ("" = none)
    QString runId;       ///< the id it runs under (a page overriding a built-in: the built-in's)
    QString state;       ///< pages: approved | approval | off | problem | duplicate
    QString fileHash;    ///< the page file's hash (what write --expect-hash wants)
    bool exists = true;  ///< false: a page being checked before it is written
};

ext::BuiltinExtension builtinInfo(const QString &id)
{
    for (const ext::BuiltinExtension &b : ext::builtinExtensions())
        if (b.extension.id == id)
            return b;
    return {};
}

Target pageTarget(const VaultPath &path, const ParsedNote &note, const QString &content)
{
    Target t;
    t.isPage = true;
    t.page = ext::readPageExtension(path, note, content);
    t.content = content;
    t.builtin = ext::builtinFor(t.page.overrides.isEmpty() ? t.page.id : t.page.overrides);
    t.runId = t.builtin.isEmpty() ? t.page.id : t.builtin;
    return t;
}

/// Every extension page, by path, with its state on this device (as Settings → Extensions shows
/// it; a built-in switched off in the app's settings is not known here).
QList<Target> extensionPages(Call &call)
{
    QList<Target> out;
    VaultIndex *index = call.index();
    if (!index)
        return out;
    QStringList found;
    index->forEachNote([&](const VaultPath &path, const ParsedNote &note) {
        if (ext::isExtensionPage(note))
            found << path;
    });
    found.sort();
    const Trust t = trust(call);
    QSet<QString> taken;
    for (const QString &path : std::as_const(found)) {
        const ParsedNote *note = index->note(path);
        if (!note)
            continue;
        Target target = pageTarget(path, *note, index->content(path));
        target.fileHash = call.nb()->read(path).hash;
        if (target.page.blocks == 0)
            target.state = u"problem"_s;
        else if (taken.contains(target.runId))
            target.state = u"duplicate"_s;
        else if (t.off.contains(target.runId))
            target.state = u"off"_s;
        else
            target.state = t.approved.contains(target.page.hash) ? u"approved"_s : u"approval"_s;
        if (target.page.blocks > 0)
            taken.insert(target.runId);
        out.append(target);
    }
    return out;
}

/// The extension `name` names: a page by its id, title or path (the one that runs when several
/// share an id), else a built-in by its id or title; nullopt + call.error.
std::optional<Target> findExtension(Call &call, const QString &name, bool builtins = true)
{
    const QString raw = name.trimmed();
    if (raw.isEmpty()) {
        call.setError(ExitUsage, u"name_required"_s, tr("Which extension? (extension list shows them)"));
        return std::nullopt;
    }
    const QString id = ext::extensionId(raw);
    const QString folded = text::foldKey(raw);
    const QList<Target> pages = extensionPages(call);
    if (call.error)
        return std::nullopt;
    for (const Target &t : pages) {
        const QString path = t.page.path;
        if (t.page.id == id || text::foldKey(t.page.title) == folded || path.compare(raw, Qt::CaseInsensitive) == 0
            || paths::stripMarkdownExtension(path).compare(raw, Qt::CaseInsensitive) == 0)
            return t;
    }
    if (builtins) {
        for (const ext::BuiltinExtension &b : ext::builtinExtensions()) {
            if (b.extension.id == id || ext::extensionId(b.titleEn) == id || ext::extensionId(b.titlePl) == id) {
                Target t;
                t.builtin = t.runId = b.extension.id;
                t.state = u"builtin"_s;
                return t;
            }
        }
    }
    // A page of that name that is no extension says so.
    const QJsonArray warnings = call.warnings;
    const auto ref = call.page(raw, false);
    call.error.reset();
    call.warnings = warnings;
    if (ref && ref->exists)
        call.setError(ExitNotFound, u"not_an_extension"_s,
                      tr("%1 is no extension page: tag it #extension (extension write makes one)").arg(ref->path),
                      {{u"path"_s, ref->path}});
    else
        call.setError(ExitNotFound, u"extension_not_found"_s, tr("No extension %1 (extension list shows them)").arg(raw),
                      {{u"name"_s, raw}});
    return std::nullopt;
}

QJsonObject targetJson(const Target &t)
{
    if (!t.isPage) {
        const ext::BuiltinExtension b = builtinInfo(t.builtin);
        return {{u"id"_s, t.builtin},
                {u"builtin"_s, true},
                {u"title"_s, b.titleEn},
                {u"titlePl"_s, b.titlePl},
                {u"permissions"_s, QJsonArray::fromStringList(b.extension.permissions)},
                {u"state"_s, t.state}};
    }
    QJsonObject o{{u"id"_s, t.page.id},
                  {u"title"_s, t.page.title},
                  {u"permissions"_s, QJsonArray::fromStringList(t.page.permissions)},
                  {u"description"_s, t.page.description},
                  {u"blocks"_s, t.page.blocks},
                  {u"problems"_s, QJsonArray::fromStringList(t.page.problems)},
                  {u"codeHash"_s, t.page.hash}};
    if (t.exists) {
        o.insert(u"path"_s, t.page.path);
        o.insert(u"hash"_s, t.fileHash);
    }
    if (!t.state.isEmpty())
        o.insert(u"state"_s, t.state);
    if (!t.builtin.isEmpty())
        o.insert(u"overrides"_s, t.builtin);
    if (t.page.firstLine >= 0)
        o.insert(u"firstCodeLine"_s, t.page.firstLine + 1);
    return o;
}

/// The extension as the engine loads it.
ext::Extension loadable(const Target &t)
{
    if (!t.isPage)
        return ext::builtinExtension(t.builtin);
    ext::Extension x = t.page.extension();
    x.id = t.runId;
    if (!t.builtin.isEmpty())
        x.data = ext::builtinExtension(t.builtin).data;
    return x;
}

QString titleOf(const Target &t)
{
    return t.isPage ? t.page.title : t.builtin;
}

Result extensionList(Call &call)
{
    const QList<Target> pages = extensionPages(call);
    if (call.error)
        return call.failed();
    QJsonArray list;
    QString text;
    QHash<QString, QString> overriddenBy;
    for (const Target &t : pages) {
        list.append(targetJson(t));
        text += t.page.id + u"  "_s + t.state + u"  "_s + t.page.path + u'\n';
        if (!t.builtin.isEmpty() && t.state != u"duplicate" && !overriddenBy.contains(t.builtin))
            overriddenBy.insert(t.builtin, t.page.path);
    }
    QJsonArray builtins;
    for (const ext::BuiltinExtension &b : ext::builtinExtensions()) {
        QJsonObject o{{u"id"_s, b.extension.id},
                      {u"title"_s, b.titleEn},
                      {u"titlePl"_s, b.titlePl},
                      {u"permissions"_s, QJsonArray::fromStringList(b.extension.permissions)}};
        if (overriddenBy.contains(b.extension.id))
            o.insert(u"overriddenBy"_s, overriddenBy.value(b.extension.id));
        builtins.append(o);
        text += b.extension.id + u"  builtin\n"_s;
    }
    return call.ok(QJsonObject{{u"pages"_s, list}, {u"builtins"_s, builtins}}, text);
}

Result extensionShow(Call &call)
{
    const auto t = findExtension(call, call.str(u"name"_s));
    if (!t)
        return call.failed();
    QJsonObject o = targetJson(*t);
    if (t->isPage) {
        o.insert(u"code"_s, t->page.code);
        o.insert(u"content"_s, t->content);
        return call.ok(o, t->content);
    }
    const ext::Extension b = ext::builtinExtension(t->builtin);
    o.insert(u"code"_s, b.source);
    // The page that would run instead of it (the app's "Edit the code"): extension write --content.
    o.insert(u"overridePage"_s, ext::builtinSourcePage(b, call.ctx.language()));
    o.insert(u"data"_s, QJsonArray::fromStringList(b.data.keys()));
    return call.ok(o, b.source);
}

// ---- the sandbox ------------------------------------------------------------------------------------

/// A fresh engine over the notebook whose writes are only computed (NotebookHost's dry run).
struct Sandbox {
    std::unique_ptr<ext::NotebookHost> host;
    ext::Engine engine;
    QJsonArray printed;
    QJsonArray timers;
    QString error;
};

/// The notebook host of a sandbox: the run's clock and language, writes only computed.
ext::NotebookHost::Options hostOptions(const Call &call)
{
    ext::NotebookHost::Options options;
    const QDateTime now = call.ctx.now();
    options.now = [now] { return now; };
    options.language = call.ctx.language();
    options.uiLanguage = call.ctx.language();
    options.dryRun = true;
    return options;
}

bool startSandbox(Call &call, Sandbox &box)
{
    Vault *v = call.vault();
    VaultIndex *index = v ? call.index() : nullptr;
    if (!index)
        return false;
    const ext::NotebookHost::Options options = hostOptions(call);
    box.host = std::make_unique<ext::NotebookHost>(*v, *index, options);
    Sandbox *b = &box;
    box.host->onLog = [b](const QString &, const QString &text) { b->printed.append(text); };
    box.host->onExtensionTimer = [b](const QString &, const ext::TimerSpec &spec) {
        b->timers.append(spec.seconds > 0 ? QJsonObject{{u"seconds"_s, spec.seconds}, {u"label"_s, spec.label},
                                                        {u"message"_s, spec.message}}
                                          : QJsonObject{{u"stopped"_s, true}});
    };
    QString error;
    if (!box.engine.start(box.host.get(), ext::Limits{}, &error)) {
        call.setError(ExitInternal, u"engine_unavailable"_s, tr("The Luau engine did not start: %1").arg(error));
        return false;
    }
    return true;
}

QString statusName(ext::Status s)
{
    switch (s) {
    case ext::Status::Ok: return u"ok"_s;
    case ext::Status::Error: return u"error"_s;
    case ext::Status::Suspended: return u"waiting"_s;
    case ext::Status::Timeout: return u"timeout"_s;
    case ext::Status::OutOfMemory: return u"out_of_memory"_s;
    case ext::Status::StackOverflow: return u"stack_overflow"_s;
    case ext::Status::NotFound: return u"not_found"_s;
    }
    return u"error"_s;
}

/// {message, line?, traceback} of a failed call; `line`: the page's line (1-based) it names.
QJsonObject errorJson(const ext::Result &r, const QString &title)
{
    QString message = r.error;
    if (message.isEmpty())
        message = r.status == ext::Status::Timeout       ? u"the extension took too long (2 s)"_s
                  : r.status == ext::Status::OutOfMemory ? u"the extension ran out of memory (64 MB)"_s
                  : r.status == ext::Status::NotFound    ? u"no such command, view, action or hook"_s
                                                         : u"the extension failed"_s;
    QJsonObject o{{u"status"_s, statusName(r.status)}, {u"message"_s, message.section(u'\n', 0, 0)}};
    const int line = ext::errorLine(title, message);
    if (line >= 0)
        o.insert(u"line"_s, line + 1);
    if (message.contains(u'\n'))
        o.insert(u"traceback"_s, message);
    return o;
}

QJsonArray writesJson(const Sandbox &box)
{
    QJsonArray out;
    for (const ext::NotebookHost::DryWrite &w : box.host->dryWrites())
        out.append(QJsonObject{{u"path"_s, w.path},
                               {u"created"_s, !w.existed},
                               {u"diff"_s, unifiedDiff(w.existed ? w.before : QString(), w.after, w.path)}});
    return out;
}

/// The check of an extension (sandbox.h): loaded in a fresh sandbox, what it registered or why it
/// failed.
QJsonObject checkJson(Call &call, const Target &t, bool *loaded)
{
    QJsonObject o = targetJson(t);
    *loaded = false;
    Vault *v = call.vault();
    VaultIndex *index = v ? call.index() : nullptr;
    if (!index)
        return o;
    if (t.isPage && t.page.blocks == 0) {
        o.insert(u"loaded"_s, false);
        o.insert(u"error"_s, QJsonObject{{u"status"_s, u"error"_s}, {u"message"_s, u"no ```luau block"_s}});
        return o;
    }
    const QVariantMap r = ext::checkExtension(*v, *index, loadable(t), hostOptions(call));
    *loaded = r.value(u"ok"_s).toBool();
    o.insert(u"loaded"_s, *loaded);
    if (*loaded) {
        for (const QString &key : {u"commands"_s, u"views"_s, u"hooks"_s, u"macros"_s, u"variables"_s})
            o.insert(key, QJsonValue::fromVariant(r.value(key)).toArray());
    } else {
        QJsonObject error{{u"status"_s, r.value(u"status"_s).toString()}, {u"message"_s, r.value(u"error"_s).toString()}};
        if (r.contains(u"line"_s))
            error.insert(u"line"_s, r.value(u"line"_s).toInt());
        if (r.contains(u"traceback"_s))
            error.insert(u"traceback"_s, r.value(u"traceback"_s).toString());
        o.insert(u"error"_s, error);
    }
    o.insert(u"printed"_s, QJsonArray::fromStringList(r.value(u"printed"_s).toStringList()));
    QJsonArray writes; // top-level code that writes (never done here)
    for (const QVariant &w : r.value(u"writes"_s).toList()) {
        const QVariantMap m = w.toMap();
        const QString path = m.value(u"path"_s).toString();
        const bool created = m.value(u"created"_s).toBool();
        writes.append(QJsonObject{
                {u"path"_s, path},
                {u"created"_s, created},
                {u"diff"_s, unifiedDiff(created ? QString() : m.value(u"before"_s).toString(),
                                        m.value(u"after"_s).toString(), path)}});
    }
    if (!writes.isEmpty())
        o.insert(u"writes"_s, writes);
    return o;
}

/// A page text being checked (not saved): the page `name` would be, at `path`.
std::optional<Target> unsavedTarget(Call &call, const VaultPath &path, const QString &content)
{
    Vault *v = call.vault();
    if (!v)
        return std::nullopt;
    const ParsedNote note = markdown::parseNote(path, content, v->config());
    if (!ext::isExtensionPage(note)) {
        call.setError(ExitUsage, u"not_an_extension_page"_s,
                      tr("The text is no extension page: it needs the tag in its properties (tags: [extension])"));
        return std::nullopt;
    }
    Target t = pageTarget(path, note, content);
    t.exists = false;
    return t;
}

/// The folder new extension pages go to, in the notebook's language (as the app's New extension).
QString extensionFolder(const Call &call)
{
    return call.ctx.language() == u"en" ? u"Extensions"_s : u"Rozszerzenia"_s;
}

Result extensionCheck(Call &call)
{
    const QString name = call.str(u"name"_s);
    std::optional<Target> t;
    if (call.args.contains(u"content"_s) || call.ctx.globals.stdinText) {
        const QString content = call.text(u"content"_s);
        VaultPath path;
        if (const auto existing = findExtension(call, name, false))
            path = existing->page.path;
        call.error.reset();
        if (path.isEmpty()) {
            Vault *v = call.vault();
            if (!v)
                return call.failed();
            path = notebook::plannedPage(*v, name, extensionFolder(call)).path;
            if (path.isEmpty())
                return call.fail(ExitUsage, u"name_required"_s, tr("Which extension? Give its title"));
        }
        t = unsavedTarget(call, path, content);
    } else {
        t = findExtension(call, name);
    }
    if (!t)
        return call.failed();
    bool loaded = false;
    const QJsonObject o = checkJson(call, *t, &loaded);
    if (call.error)
        return call.failed();
    for (const QString &p : std::as_const(t->page.problems))
        call.warn(u"extension_problem"_s, p);
    if (!loaded)
        return call.fail(ExitParse, u"extension_error"_s,
                         tr("The extension does not load: %1")
                                 .arg(o.value(u"error"_s).toObject().value(u"message"_s).toString()),
                         o);
    return call.ok(o, tr("%1 loads: %2 commands, %3 views, %4 hooks\n")
                              .arg(titleOf(*t))
                              .arg(o.value(u"commands"_s).toArray().size())
                              .arg(o.value(u"views"_s).toArray().size())
                              .arg(o.value(u"hooks"_s).toArray().size()));
}

// ---- running ------------------------------------------------------------------------------------------

/// A JSON argument (`args`, `answers`) given as text; nullopt + call.error when it is no JSON.
std::optional<QJsonValue> jsonArg(Call &call, const QString &name, bool array)
{
    const QString raw = call.str(name).trimmed();
    if (raw.isEmpty())
        return array ? QJsonValue(QJsonArray()) : QJsonValue(QJsonObject());
    QJsonParseError error;
    const QJsonDocument doc = QJsonDocument::fromJson(raw.toUtf8(), &error);
    if (error.error != QJsonParseError::NoError || (array ? !doc.isArray() : !doc.isObject())) {
        call.setError(ExitUsage, u"invalid_json"_s,
                      array ? tr("--%1 is a JSON list: '[\"text\", {\"key\": 1}, null]'").arg(optionName(name).mid(2))
                            : tr("--%1 is a JSON object: '{\"key\": \"value\"}'").arg(optionName(name).mid(2)));
        return std::nullopt;
    }
    return array ? QJsonValue(doc.array()) : QJsonValue(doc.object());
}

/// A call run to its end: questions answered from `answers` in order, b.ai declined (it would cost
/// money and needs the app's key) — the script sees an error it can pcall.
struct Outcome {
    ext::Result result;
    QJsonArray questions;
    int aiRequests = 0;
    bool waiting = false; ///< a question had no answer left
};

Outcome finish(Sandbox &box, ext::Result r, const QJsonArray &answers)
{
    Outcome out;
    qsizetype next = 0;
    while (r.status == ext::Status::Suspended) {
        const QVariantMap q = r.value.toMap();
        const QString kind = q.value(u"kind"_s).toString();
        if (kind == u"ai") {
            ++out.aiRequests;
            r = box.engine.resume(r.call, QVariantMap{{u"error"_s, u"b.ai does not run in brulion-cli (no model is "
                                                                   u"asked from the command line): try it in the app"_s}});
            continue;
        }
        QJsonObject question = QJsonObject::fromVariantMap(q);
        if (next >= answers.size()) {
            out.questions.append(question);
            out.waiting = true;
            break;
        }
        const QJsonValue a = answers.at(next++);
        question.insert(u"answer"_s, a);
        out.questions.append(question);
        QVariant answer;
        if (kind == u"form")
            answer = a.isObject() ? QVariant(a.toObject().toVariantMap()) : QVariant();
        else if (kind == u"choose")
            answer = a.isNull() || a.isUndefined() ? QVariant() : QVariant(a.isString() ? a.toString() : a.toVariant().toString());
        else
            answer = a.isString() ? a.toString() : a.toVariant().toString();
        r = box.engine.resume(r.call, answer);
    }
    out.result = r;
    return out;
}

Result report(Call &call, Sandbox &box, const Outcome &out, QJsonObject o, const QString &title)
{
    o.insert(u"status"_s, out.waiting ? u"waiting"_s : statusName(out.result.status));
    if (out.result.ok())
        o.insert(u"value"_s, QJsonValue::fromVariant(out.result.value));
    if (!out.questions.isEmpty())
        o.insert(u"questions"_s, out.questions);
    o.insert(u"writes"_s, writesJson(box));
    o.insert(u"printed"_s, box.printed);
    if (!box.timers.isEmpty())
        o.insert(u"timers"_s, box.timers);
    if (out.aiRequests > 0) {
        o.insert(u"aiRequests"_s, out.aiRequests);
        call.warn(u"ai_not_run"_s, tr("b.ai was called: brulion-cli asks no model, the call got an error"));
    }
    if (out.waiting) {
        call.warn(u"question_unanswered"_s,
                  tr("The extension asked a question with no answer left: give it with --answers"));
        return call.ok(o);
    }
    if (!out.result.ok()) {
        const QJsonObject error = errorJson(out.result, title);
        o.insert(u"error"_s, error);
        return call.fail(ExitParse, u"extension_error"_s,
                         tr("The extension failed: %1").arg(error.value(u"message"_s).toString()), o);
    }
    const QVariant &v = out.result.value;
    const bool text = v.metaType().id() == QMetaType::QString;
    return call.ok(o, text ? v.toString() + u'\n' : QString());
}

/// Loads `t` into the sandbox; false + call.error when it does not load.
bool loadInto(Call &call, Sandbox &box, const Target &t, ext::Extension *loaded)
{
    *loaded = loadable(t);
    const ext::Result r = box.engine.load(*loaded);
    if (r.ok())
        return true;
    QJsonObject o = targetJson(t);
    o.insert(u"loaded"_s, false);
    o.insert(u"error"_s, errorJson(r, loaded->name.isEmpty() ? loaded->id : loaded->name));
    call.setError(ExitParse, u"extension_error"_s,
                  tr("The extension does not load: %1").arg(o.value(u"error"_s).toObject().value(u"message"_s).toString()),
                  o);
    return false;
}

Result extensionRun(Call &call)
{
    const auto t = findExtension(call, call.str(u"name"_s));
    if (!t)
        return call.failed();
    const QString command = call.str(u"command"_s).trimmed();
    const QString view = call.str(u"view"_s).trimmed();
    const QString action = call.str(u"action"_s).trimmed();
    const QString hook = call.str(u"hook"_s).trimmed();
    const QString macro = call.str(u"macro"_s).trimmed();
    const QString variable = call.str(u"variable"_s).trimmed();
    if (int(!command.isEmpty()) + int(!view.isEmpty()) + int(!hook.isEmpty()) + int(!macro.isEmpty())
                + int(!variable.isEmpty())
        != 1)
        return call.fail(ExitUsage, u"what_to_run"_s,
                         tr("Name one thing to run: a command, --view, --hook, --macro or --variable"));
    if (!action.isEmpty() && view.isEmpty())
        return call.fail(ExitUsage, u"what_to_run"_s, tr("--action is an action of a view: give --view too"));
    const auto args = jsonArg(call, u"args"_s, false);
    const auto answers = args ? jsonArg(call, u"answers"_s, true) : std::nullopt;
    if (!answers)
        return call.failed();
    // The page on screen (ctx.page, a hook's page) and the block under the caret (ctx.block).
    QString pageName;
    VaultPath pagePath;
    if (const QString p = call.str(u"page"_s).trimmed(); !p.isEmpty()) {
        const auto ref = call.page(p, true);
        if (!ref)
            return call.failed();
        pagePath = ref->path;
        const ParsedNote *note = call.index() ? call.index()->note(pagePath) : nullptr;
        pageName = note ? note->title : paths::pageTitle(pagePath);
    }
    Sandbox box;
    ext::Extension x;
    if (!startSandbox(call, box) || !loadInto(call, box, *t, &x))
        return call.failed();
    QVariantMap blockMap;
    if (const QString b = call.str(u"block"_s).trimmed(); !b.isEmpty()) {
        const auto ref = call.page(b, true);
        if (!ref)
            return call.failed();
        if (!ref->item)
            return call.fail(ExitUsage, u"item_ref_required"_s, tr("--block is a block's ref: Dom.md#L12@hash"));
        notebook::ItemRef item = *ref->item;
        item.page = ref->path;
        const notebook::ResolvedLine line =
                notebook::resolve(call.nb()->read(ref->path).text, item, call.vault()->config());
        if (line.status != notebook::ResolvedLine::Ok && line.status != notebook::ResolvedLine::Moved)
            return call.fail(ExitConflict, u"stale_ref"_s,
                             tr("The line changed since the ref was taken: read the page again"));
        blockMap = box.host->blockAt(ref->path, line.line);
        if (pagePath.isEmpty()) {
            pagePath = ref->path;
            const ParsedNote *note = call.index()->note(pagePath);
            pageName = note ? note->title : paths::pageTitle(pagePath);
        }
    }
    QJsonObject o{{u"extension"_s, x.id}};
    ext::Result r;
    if (!macro.isEmpty() || !variable.isEmpty()) {
        // As in a note (--page) or a template filled for it: ctx {args, text, page, path, date}.
        const QString text = call.str(u"text"_s).trimmed();
        QVariantList list;
        for (const QString &a : markdown::macroArguments(text))
            list << a;
        QVariantMap ctx{{u"args"_s, list}, {u"text"_s, text}, {u"page"_s, pageName}, {u"path"_s, QString(pagePath)}};
        if (const auto day = pagePath.isEmpty() ? std::nullopt
                                                : markdown::journalDateFromPath(pagePath, call.vault()->config()))
            ctx.insert(u"date"_s, day->toString(Qt::ISODate));
        const int limit = macro.isEmpty() ? 1000 : ext::MacroTimeoutMs;
        o.insert(u"ran"_s, QJsonObject{{macro.isEmpty() ? u"variable"_s : u"macro"_s, macro.isEmpty() ? variable : macro}});
        r = macro.isEmpty() ? box.engine.runVariable(x.id, variable, ctx, limit)
                            : box.engine.runMacro(x.id, macro, ctx, limit);
        if (r.ok() && !macro.isEmpty()) {
            // What the note would show, as text: the pieces, a link as [[page|label]].
            const markdown::MacroView shown = ext::macroViewFrom(r.value, box.host->language() == u"pl");
            o.insert(u"shows"_s, markdown::macroViewText(shown));
        }
    } else if (!command.isEmpty()) {
        // --args: the command's ctx.args ({} when none), as the app passes a command its arguments.
        QVariantMap ctx{{u"page"_s, pageName}, {u"selection"_s, QString()}, {u"args"_s, args->toObject().toVariantMap()}};
        if (!blockMap.isEmpty())
            ctx.insert(u"block"_s, blockMap);
        o.insert(u"ran"_s, QJsonObject{{u"command"_s, command}});
        r = box.engine.runCommand(x.id, command, ctx);
    } else if (!view.isEmpty()) {
        const QVariantMap map = args->toObject().toVariantMap();
        if (action.isEmpty()) {
            o.insert(u"ran"_s, QJsonObject{{u"view"_s, view}});
            r = box.engine.render(x.id, view, map);
        } else {
            o.insert(u"ran"_s, QJsonObject{{u"view"_s, view}, {u"action"_s, action}});
            r = box.engine.act(x.id, view, action, map);
        }
    } else {
        if (!ext::Engine::hookEvents().contains(hook))
            return call.fail(ExitUsage, u"unknown_event"_s,
                             tr("No event %1: %2").arg(hook, ext::Engine::hookEvents().join(u", "_s)));
        // The event as the app gives it (docs/EXTENSIONS.md "Hooks"); --args gives one of its own.
        const QString today = call.ctx.now().date().toString(Qt::ISODate);
        QVariantMap event;
        if (hook == u"save") {
            event = {{u"page"_s, pageName}, {u"path"_s, pagePath}};
        } else if (hook == u"change") {
            event = {{u"pages"_s, pageName.isEmpty() ? QStringList() : QStringList{pageName}},
                     {u"paths"_s, pagePath.isEmpty() ? QStringList() : QStringList{pagePath}},
                     {u"by"_s, QStringList()},
                     {u"sources"_s, QStringList{u"app"_s}}};
        } else if (hook == u"task") {
            event = blockMap.isEmpty() ? QVariantMap{{u"page"_s, pageName}, {u"path"_s, pagePath}} : blockMap;
            event.insert(u"task"_s, u"x"_s);
            event.insert(u"previous"_s, u" "_s);
            event.insert(u"source"_s, u"app"_s);
            event.insert(u"by"_s, QString());
        } else if (hook == u"journal") {
            event = {{u"date"_s, today}, {u"page"_s, today}, {u"path"_s, pagePath}, {u"source"_s, u"app"_s}, {u"by"_s, QString()}};
        } else if (hook == u"open") {
            event = {{u"date"_s, today}};
        } else if (hook == u"day") {
            event = {{u"date"_s, today}, {u"previous"_s, call.ctx.now().date().addDays(-1).toString(Qt::ISODate)}};
        } else if (hook == u"timer") {
            event = {{u"label"_s, QString()}, {u"seconds"_s, 0}};
        }
        if (const QVariantMap given = args->toObject().toVariantMap(); !given.isEmpty())
            event = given;
        o.insert(u"ran"_s, QJsonObject{{u"hook"_s, hook}});
        QList<ext::HookInfo> hooks;
        for (const ext::HookInfo &h : box.engine.hooks(hook))
            if (h.extension == x.id)
                hooks << h;
        if (hooks.isEmpty())
            return call.fail(ExitNotFound, u"no_hook"_s, tr("The extension registers no %1 hook").arg(hook));
        // Every hook of the event, in order; the first that fails ends the run.
        Outcome last;
        QJsonArray values;
        for (const ext::HookInfo &h : std::as_const(hooks)) {
            last = finish(box, box.engine.runHook(h.id, event), answers->toArray());
            if (!last.result.ok() || last.waiting)
                break;
            values.append(QJsonValue::fromVariant(last.result.value));
        }
        o.insert(u"hooks"_s, int(hooks.size()));
        o.insert(u"values"_s, values);
        return report(call, box, last, o, x.name);
    }
    return report(call, box, finish(box, r, answers->toArray()), o, x.name);
}

Result extensionEval(Call &call)
{
    const QString code = call.text(u"code"_s);
    if (code.trimmed().isEmpty())
        return call.fail(ExitUsage, u"text_required"_s, tr("What to run? Give Luau code (or --stdin)"));
    const auto answers = jsonArg(call, u"answers"_s, true);
    if (!answers)
        return call.failed();
    Sandbox box;
    if (!startSandbox(call, box))
        return call.failed();
    QString in = kConsole;
    QJsonObject o;
    if (const QString name = call.str(u"extension"_s).trimmed(); !name.isEmpty()) {
        const auto t = findExtension(call, name);
        ext::Extension x;
        if (!t || !loadInto(call, box, *t, &x))
            return call.failed();
        in = x.id;
        o.insert(u"extension"_s, x.id);
    } else {
        // As the app's Luau console: what is typed there is the user's own.
        ext::Extension console;
        console.id = kConsole;
        console.name = u"console"_s;
        console.permissions = {u"ai"_s, u"write"_s};
        if (const ext::Result r = box.engine.load(console); !r.ok())
            return call.fail(ExitInternal, u"engine_unavailable"_s, r.error);
    }
    return report(call, box, finish(box, box.engine.evaluate(in, code, u"eval"_s), answers->toArray()), o,
                  u"eval"_s);
}

// ---- writing ------------------------------------------------------------------------------------------

QStringList wordsOf(const QString &list)
{
    QString s = list;
    s.remove(u'[').remove(u']');
    return s.split(QRegularExpression(u"[,;\\s]+"_s), Qt::SkipEmptyParts);
}

Result extensionWrite(Call &call)
{
    Vault *v = call.vault();
    if (!v)
        return call.failed();
    const QString name = call.str(u"name"_s).trimmed();
    const bool hasContent = call.args.contains(u"content"_s) || call.ctx.globals.stdinText;
    const bool hasCode = call.args.contains(u"code"_s);
    if (hasContent == hasCode)
        return call.fail(ExitUsage, u"text_required"_s,
                         tr("Give either the whole page (--content, --stdin) or its code (--code)"));
    // The page: an extension page of that name, else a new one (in Extensions/ unless the name has
    // a folder or --folder says otherwise). A page of that name that is no extension is refused.
    std::optional<Target> existing = findExtension(call, name, false);
    const std::optional<Error> lookup = call.error;
    call.error.reset();
    if (!existing && lookup && lookup->code == u"not_an_extension")
        return failWith(Error{ExitConflict, lookup->code, lookup->message, lookup->details});
    if (!existing && lookup && lookup->code != u"extension_not_found")
        return failWith(*lookup);
    VaultPath path;
    if (existing) {
        path = existing->page.path;
        if (call.str(u"expectHash"_s).trimmed().isEmpty())
            return call.fail(ExitConflict, u"hash_required"_s,
                             tr("%1 exists: replacing it needs --expect-hash with its hash (extension show)").arg(path),
                             {{u"path"_s, path}});
    } else {
        QString folder = call.str(u"folder"_s).trimmed();
        if (folder.isEmpty() && !name.contains(u'/'))
            folder = extensionFolder(call);
        if (folder == u".")
            folder.clear();
        const notebook::CreatedPage planned = notebook::plannedPage(*v, name, folder);
        if (planned.path.isEmpty())
            return failWith(problemError(planned.problem));
        path = planned.path;
        if (planned.existing && call.nb()->read(path).exists)
            return call.fail(ExitConflict, u"not_an_extension"_s,
                             tr("%1 is no extension page: tag it #extension (extension write makes one)").arg(path),
                             {{u"path"_s, path}});
    }
    QString content;
    if (hasContent) {
        content = call.text(u"content"_s);
    } else {
        // From the code: the description and permissions of the page it replaces unless given.
        QString description = call.str(u"description"_s);
        QStringList permissions = wordsOf(call.str(u"permissions"_s));
        if (existing && !call.args.contains(u"description"_s))
            description = existing->page.description;
        if (existing && !call.args.contains(u"permissions"_s))
            permissions = existing->page.permissions;
        QStringList unknown;
        ext::permissionsFrom(permissions, &unknown);
        if (!unknown.isEmpty())
            return call.fail(ExitUsage, u"unknown_permission"_s,
                             tr("Unknown permission %1: write, ai").arg(unknown.join(u", "_s)));
        content = ext::extensionPageText(call.ctx.language(), description, permissions, call.str(u"code"_s));
        if (existing)
            call.warn(u"page_replaced"_s, tr("The whole page is written anew: give --content to keep its text"));
    }
    const auto target = unsavedTarget(call, path, content);
    if (!target)
        return call.failed();
    bool loaded = false;
    const QJsonObject check = checkJson(call, *target, &loaded);
    if (call.error)
        return call.failed();
    if (!loaded && !call.flag(u"force"_s))
        return call.fail(ExitParse, u"extension_error"_s,
                         tr("Not written: the extension does not load: %1 (--force writes it anyway)")
                                 .arg(check.value(u"error"_s).toObject().value(u"message"_s).toString()),
                         check);
    for (const QString &p : std::as_const(target->page.problems))
        call.warn(u"extension_problem"_s, p);
    notebook::Notebook::WriteOptions options;
    options.expectHash = call.str(u"expectHash"_s).trimmed().toLower();
    options.mayCreate = !existing;
    const auto written = call.edit(
            path,
            [&](const QString &, bool, QString *next) {
                *next = content;
                return true;
            },
            options);
    if (!written)
        return call.failed();
    QJsonObject o = call.writeJson(*written);
    o.insert(u"id"_s, target->page.id);
    o.insert(u"title"_s, target->page.title);
    o.insert(u"check"_s, check);
    const bool approved = trust(call).approved.contains(target->page.hash);
    o.insert(u"approved"_s, approved);
    if (!approved && !call.dryRun())
        call.warn(u"needs_approval"_s,
                  tr("The app runs this code once the user approves it on that device: Settings → Extensions, or Run "
                     "on the page's state line"));
    return call.ok(o, written->path + u'\n');
}

// ---- approval, settings, sharing ------------------------------------------------------------------

/// The code of an extension page against the version approved on this device (what the app's
/// "Read and run" shows): `lines` [{kind, text, line, count}], `added`, `removed`.
Result extensionDiff(Call &call)
{
    const auto t = findExtension(call, call.str(u"name"_s), false);
    if (!t)
        return call.failed();
    Vault *v = call.vault();
    const QVariantMap before = ext::approvedCode(v ? v->store() : nullptr, t->runId);
    const bool kept = before.contains(u"code"_s);
    const QString old = before.value(u"code"_s).toString();
    QJsonArray lines;
    QStringList text;
    int added = 0, removed = 0;
    for (const ext::CodeDiffLine &l : ext::codeDiff(kept ? old : QString(), t->page.code)) {
        static const QStringList kinds{u"same"_s, u"added"_s, u"removed"_s, u"skipped"_s};
        lines.append(QJsonObject{{u"kind"_s, kinds.value(int(l.kind))}, {u"text"_s, l.text}, {u"line"_s, l.line},
                                 {u"count"_s, l.count}});
        switch (l.kind) {
        case ext::CodeDiffLine::Same: text << u"  "_s + l.text; break;
        case ext::CodeDiffLine::Added: text << u"+ "_s + l.text; ++added; break;
        case ext::CodeDiffLine::Removed: text << u"- "_s + l.text; ++removed; break;
        case ext::CodeDiffLine::Skipped: text << tr("… unchanged lines: %1").arg(l.count); break;
        }
    }
    QJsonObject o = targetJson(*t);
    o.insert(u"approvedBefore"_s, !before.isEmpty());
    o.insert(u"approvedHash"_s, before.value(u"hash"_s).toString());
    o.insert(u"approvedPermissions"_s, QJsonArray::fromStringList(before.value(u"permissions"_s).toStringList()));
    o.insert(u"approved"_s, trust(call).approved.contains(t->page.hash));
    o.insert(u"added"_s, added);
    o.insert(u"removed"_s, removed);
    o.insert(u"lines"_s, lines);
    if (before.isEmpty())
        call.warn(u"never_approved"_s, tr("Nothing of this extension was approved on this device: all of its code is new"));
    else if (!kept)
        call.warn(u"approved_code_not_kept"_s, tr("The approved version was too long to keep: all of the code is shown"));
    return call.ok(o, text.join(u'\n') + u'\n');
}

/// The settings an extension declares (b.settings) with their values on this device; --set
/// key=value changes one, --reset puts them all back. The extension's code is loaded in a sandbox
/// to know them (its writes only computed); the values go into the notebook's device state as the
/// app keeps them.
Result extensionSettings(Call &call)
{
    const auto t = findExtension(call, call.str(u"name"_s));
    if (!t)
        return call.failed();
    Sandbox box;
    ext::Extension x;
    if (!startSandbox(call, box) || !loadInto(call, box, *t, &x))
        return call.failed();
    const QVariantList fields = box.engine.settingsOf(x.id);
    Vault *v = call.vault();
    LocalStore *store = v ? v->store() : nullptr;
    QVariantMap stored = store ? store->get(ext::settingsStoreKey(x.id)).toObject().toVariantMap() : QVariantMap();
    bool changed = false;
    if (call.flag(u"reset"_s)) {
        changed = !stored.isEmpty();
        stored.clear();
    }
    if (const QString set = call.str(u"set"_s).trimmed(); !set.isEmpty()) {
        const qsizetype eq = set.indexOf(u'=');
        const QString key = set.left(eq).trimmed(), value = eq < 0 ? QString() : set.mid(eq + 1).trimmed();
        QVariantMap field;
        for (const QVariant &f : fields)
            if (f.toMap().value(u"key"_s).toString() == key)
                field = f.toMap();
        if (eq < 0 || field.isEmpty()) {
            QStringList keys;
            for (const QVariant &f : fields)
                keys << f.toMap().value(u"key"_s).toString();
            return call.fail(ExitUsage, u"unknown_setting"_s,
                             tr("No setting %1 (--set key=value; the extension declares: %2)")
                                     .arg(key, keys.isEmpty() ? tr("none") : keys.join(u", "_s)),
                             {{u"settings"_s, QJsonArray::fromVariantList(fields)}});
        }
        const QString kind = field.value(u"kind"_s).toString();
        QVariant parsed;
        if (kind == u"number") {
            bool ok = false;
            const double n = QString(value).replace(u',', u'.').toDouble(&ok);
            if (!ok || !std::isfinite(n))
                return call.fail(ExitUsage, u"bad_value"_s, tr("%1 is a number").arg(key));
            parsed = n == std::floor(n) && std::fabs(n) < 1e15 ? QVariant(qint64(n)) : QVariant(n);
        } else if (kind == u"toggle") {
            const QString b = value.toLower();
            if (b != u"true" && b != u"false" && b != u"on" && b != u"off" && b != u"1" && b != u"0")
                return call.fail(ExitUsage, u"bad_value"_s, tr("%1 is true or false").arg(key));
            parsed = b == u"true" || b == u"on" || b == u"1";
        } else if (kind == u"choice") {
            const QStringList options = field.value(u"options"_s).toStringList();
            if (!options.contains(value))
                return call.fail(ExitUsage, u"bad_value"_s, tr("%1 is one of: %2").arg(key, options.join(u", "_s)));
            parsed = value;
        } else {
            if (value.size() > ext::QuestionLimits::settingText)
                return call.fail(ExitUsage, u"bad_value"_s, tr("%1: at most %2 characters").arg(key).arg(ext::QuestionLimits::settingText));
            parsed = value;
        }
        stored.insert(key, parsed);
        changed = true;
    }
    if (changed && !call.dryRun()) {
        if (!store)
            return call.fail(ExitInternal, u"no_device_state"_s, tr("This notebook keeps no settings here (--no-history)"));
        store->set(ext::settingsStoreKey(x.id),
                   stored.isEmpty() ? QJsonValue(QJsonValue::Undefined) : QJsonValue(QJsonObject::fromVariantMap(stored)));
        store->flush();
    }
    // The values as the extension reads them: a stored one that fits its setting, else its default.
    QJsonArray list;
    QStringList text;
    for (const QVariant &f : fields) {
        QVariantMap field = f.toMap();
        const QString key = field.value(u"key"_s).toString();
        const QVariant mine = stored.value(key);
        const QString kind = field.value(u"kind"_s).toString();
        const bool fits = mine.isValid()
                          && (kind == u"number"   ? mine.canConvert<double>() && mine.metaType().id() != QMetaType::QString
                              : kind == u"toggle" ? mine.metaType().id() == QMetaType::Bool
                              : kind == u"choice" ? field.value(u"options"_s).toStringList().contains(mine.toString())
                                                  : mine.metaType().id() == QMetaType::QString);
        field.insert(u"current"_s, fits ? mine : field.value(u"value"_s));
        field.insert(u"set"_s, fits);
        list.append(QJsonObject::fromVariantMap(field));
        text << u"%1 = %2%3 (%4)"_s.arg(key, field.value(u"current"_s).toString(),
                                        field.value(u"unit"_s).toString().isEmpty() ? QString()
                                                                                     : u" "_s + field.value(u"unit"_s).toString(),
                                        field.value(u"label"_s).toString());
    }
    QJsonObject o = targetJson(*t);
    o.insert(u"settings"_s, list);
    o.insert(u"changed"_s, changed);
    if (fields.isEmpty())
        call.warn(u"no_settings"_s, tr("The extension declares no settings (b.settings)"));
    return call.ok(o, text.isEmpty() ? QString() : text.join(u'\n') + u'\n');
}

/// An extension page — with the pages its code requires — copied into another notebook at the same
/// path (share.h). Nothing is approved there: that notebook's app asks as for any code.
Result extensionCopy(Call &call)
{
    const auto t = findExtension(call, call.str(u"name"_s), false);
    if (!t)
        return call.failed();
    if (!t->isPage)
        return call.fail(ExitUsage, u"not_a_page"_s, tr("A built-in extension is in every notebook: extension write its page"));
    const QString to = QDir::cleanPath(QFileInfo(call.str(u"to"_s).trimmed()).absoluteFilePath());
    if (call.str(u"to"_s).trimmed().isEmpty() || !QFileInfo(to).isDir())
        return call.fail(ExitUsage, u"no_notebook"_s, tr("--to is the folder of another notebook"));
    Vault *source = call.vault();
    if (source && QDir::cleanPath(source->rootPath()) == to)
        return call.fail(ExitUsage, u"same_notebook"_s, tr("--to is this notebook: choose another"));
    if (call.dryRun()) {
        // What would be copied, without opening the other notebook for writing.
        QStringList would{t->page.path};
        for (const QString &name : ext::requiredNames(t->page.code))
            if (const auto p = call.index()->resolveLink(name, t->page.path))
                would << *p;
        return call.ok(QJsonObject{{u"wouldCopy"_s, QJsonArray::fromStringList(would)}, {u"to"_s, to}},
                       would.join(u'\n') + u'\n');
    }
    Vault target(to, notebook::deviceStatePath(call.ctx.appDataDir(), to));
    QString error;
    if (!target.open(&error))
        return call.fail(ExitInternal, u"open_failed"_s, error.isEmpty() ? tr("The notebook %1 cannot be opened").arg(to) : error);
    const ext::CopyReport report = ext::copyExtension(*call.index(), t->page.path, target, call.flag(u"replace"_s));
    if (!report.error.isEmpty())
        return call.fail(ExitInternal, u"copy_failed"_s, report.error);
    QJsonObject o{{u"to"_s, to},
                  {u"written"_s, QJsonArray::fromStringList(report.written)},
                  {u"unchanged"_s, QJsonArray::fromStringList(report.unchanged)},
                  {u"conflicts"_s, QJsonArray::fromStringList(report.conflicts)}};
    if (!report.conflicts.isEmpty())
        return call.fail(ExitConflict, u"conflict"_s,
                         tr("A page there has other text: %1 (--replace writes over it; its text stays in that "
                            "notebook's history)")
                                 .arg(report.conflicts.join(u", "_s)),
                         o);
    if (!report.written.isEmpty())
        call.warn(u"needs_approval"_s,
                  tr("The app runs the copy once the user approves it on each device of that notebook"));
    return call.ok(o, report.written.join(u'\n') + (report.written.isEmpty() ? QString() : u"\n"_s));
}

Command make(const QString &id, const char *summary, Handler run, QList<Arg> args, const char *help = nullptr)
{
    Command c;
    c.id = id;
    c.summary = cliText(summary);
    if (help)
        c.help = cliText(help);
    c.run = std::move(run);
    c.args = std::move(args);
    return c;
}

} // namespace

void addExtensionCommands(QList<Command> &out)
{
    const Arg name = positional(
            u"name"_s, ArgType::String,
            QT_TRANSLATE_NOOP("brulion::cli",
                              "The extension: its page's title, id or path, or a built-in one (silownia, asystent)"));
    const Arg answers = option(
            u"answers"_s, ArgType::String,
            QT_TRANSLATE_NOOP("brulion::cli",
                              "Answers to its questions in order, a JSON list: text for b.ask and b.choose, an "
                              "object for b.form, null to cancel"));
    Command c = make(
            u"extension.docs"_s,
            QT_TRANSLATE_NOOP("brulion::cli", "The reference for writing extensions (Luau, the b API, recipes)"),
            extensionDocs,
            {positional(u"section"_s, ArgType::String,
                        QT_TRANSLATE_NOOP("brulion::cli", "Only the sections whose heading has these words (Views, "
                                                          "Recipes, Hooks …)"),
                        false, true)},
            QT_TRANSLATE_NOOP("brulion::cli", "docs/EXTENSIONS.md as this build has it; `sections` lists the headings."));
    c.needsVault = false;
    out.append(c);
    out.append(make(u"extension.list"_s,
                    QT_TRANSLATE_NOOP("brulion::cli",
                                      "The extension pages of the notebook (their state on this device) and the "
                                      "built-in ones"),
                    extensionList, {},
                    QT_TRANSLATE_NOOP("brulion::cli",
                                      "state: approved (runs in the app), approval (waits for the user to approve "
                                      "this code), off, problem (no ```luau block), duplicate (another page has the "
                                      "id).")));
    out.append(make(u"extension.show"_s,
                    QT_TRANSLATE_NOOP("brulion::cli", "An extension's code, page and properties (a built-in's source)"),
                    extensionShow, {name},
                    QT_TRANSLATE_NOOP("brulion::cli",
                                      "`hash` is what extension write --expect-hash needs; for a built-in, "
                                      "`overridePage` is the page that replaces it when written.")));
    out.append(make(
            u"extension.check"_s,
            QT_TRANSLATE_NOOP("brulion::cli",
                              "Load an extension in a sandbox: errors with the page's line, or what it registers"),
            extensionCheck,
            {name, stdinable(option(u"content"_s, ArgType::Text,
                                    QT_TRANSLATE_NOOP("brulion::cli",
                                                      "A page text to check instead of the saved page (nothing is "
                                                      "written)")))},
            QT_TRANSLATE_NOOP("brulion::cli",
                              "Nothing needs approving and nothing is written: the top-level code runs once (as when "
                              "the app loads it) over the notebook with writes only computed. Exit 6 when it does "
                              "not load.")));
    out.append(make(
            u"extension.run"_s,
            QT_TRANSLATE_NOOP("brulion::cli",
                              "Try an extension's command, view, view action or hook in a sandbox (writes shown as "
                              "diffs, never made)"),
            extensionRun,
            {name,
             positional(u"command"_s, ArgType::String, QT_TRANSLATE_NOOP("brulion::cli", "The command's name"), false),
             option(u"view"_s, ArgType::String, QT_TRANSLATE_NOOP("brulion::cli", "Draw this view instead: its node tree")),
             option(u"action"_s, ArgType::String,
                    QT_TRANSLATE_NOOP("brulion::cli", "With --view: call this action of the view")),
             option(u"args"_s, ArgType::String,
                    QT_TRANSLATE_NOOP("brulion::cli", "The view's params, the action's args or the command's ctx.args, a JSON object")),
             option(u"hook"_s, ArgType::String,
                    QT_TRANSLATE_NOOP("brulion::cli", "Call the extension's hooks of this event (save, change)")),
             option(u"macro"_s, ArgType::String,
                    QT_TRANSLATE_NOOP("brulion::cli", "Compute this macro instead ({{ext:extension/macro …}}): its value and what a note shows")),
             option(u"variable"_s, ArgType::String,
                    QT_TRANSLATE_NOOP("brulion::cli", "Fill this template variable instead ({{extension/variable …}})")),
             option(u"text"_s, ArgType::String,
                    QT_TRANSLATE_NOOP("brulion::cli", "With --macro or --variable: its arguments as written (\"Przysiad, 30\")")),
             option(u"page"_s, ArgType::Ref,
                    QT_TRANSLATE_NOOP("brulion::cli", "The page on screen: ctx.page, the hook's page, the note of a macro")),
             option(u"block"_s, ArgType::Ref,
                    QT_TRANSLATE_NOOP("brulion::cli", "The block under the caret (ctx.block): its ref, Dom.md#L12@hash")),
             answers},
            QT_TRANSLATE_NOOP("brulion::cli",
                              "A fresh engine each run, as the app's limits (2 s a call). Writes go to `writes` as "
                              "diffs, and later reads of the run see them; b.ai gets an error. Exit 6 when it "
                              "fails.")));
    out.append(make(
            u"extension.eval"_s,
            QT_TRANSLATE_NOOP("brulion::cli", "Run Luau code with the notebook's b, as the app's Luau console"),
            extensionEval,
            {stdinable(positional(u"code"_s, ArgType::Text,
                                  QT_TRANSLATE_NOOP("brulion::cli",
                                                    "Luau: an expression gives its value (b.today(), "
                                                    "#b.pages()), statements run"),
                                  true, true)),
             option(u"extension"_s, ArgType::String,
                    QT_TRANSLATE_NOOP("brulion::cli", "Run it inside this extension, after loading it (its globals)")),
             answers},
            QT_TRANSLATE_NOOP("brulion::cli", "Writes are only computed (`writes`), as in extension run.")));
    c = make(u"extension.write"_s,
             QT_TRANSLATE_NOOP("brulion::cli",
                               "Write an extension page (checked first; the app runs it once the user approves it)"),
             extensionWrite,
             {positional(u"name"_s, ArgType::String,
                         QT_TRANSLATE_NOOP("brulion::cli",
                                           "Its title (a new page goes to Extensions/), or an extension page to "
                                           "replace"),
                         true, true),
              stdinable(option(u"content"_s, ArgType::Text,
                               QT_TRANSLATE_NOOP("brulion::cli",
                                                 "The whole page: properties (tags: [extension]), text, ```luau "
                                                 "blocks"))),
              option(u"code"_s, ArgType::Text,
                     QT_TRANSLATE_NOOP("brulion::cli", "Only the Luau code: the page is made around it")),
              option(u"description"_s, ArgType::String,
                     QT_TRANSLATE_NOOP("brulion::cli", "With --code: the description Settings shows")),
              option(u"permissions"_s, ArgType::String,
                     QT_TRANSLATE_NOOP("brulion::cli", "With --code: write, ai (comma-separated)")),
              option(u"folder"_s, ArgType::Path,
                     QT_TRANSLATE_NOOP("brulion::cli", "The folder of a new page (default: Extensions/, "
                                                       "Rozszerzenia/ in a Polish notebook)")),
              option(u"expectHash"_s, ArgType::String,
                     QT_TRANSLATE_NOOP("brulion::cli", "Replacing a page: its hash (extension show), refused when it "
                                                       "changed")),
              flag(u"force"_s, QT_TRANSLATE_NOOP("brulion::cli", "Write it even when it does not load"))},
             QT_TRANSLATE_NOOP("brulion::cli",
                               "Refused when the code does not load (exit 6, the error with its line) or when a page "
                               "of that name is no extension. The old text is kept in the version history. The app "
                               "never runs code written here before the user approves it there."));
    c.writes = true;
    out.append(c);
    out.append(make(u"extension.diff"_s,
                    QT_TRANSLATE_NOOP("brulion::cli",
                                      "An extension page's code against the version approved on this device"),
                    extensionDiff, {name},
                    QT_TRANSLATE_NOOP("brulion::cli",
                                      "What the app's \"Read and run\" shows: the lines added and removed since the "
                                      "approval (all of them when nothing was approved here). Approves nothing.")));
    c = make(u"extension.settings"_s,
             QT_TRANSLATE_NOOP("brulion::cli", "An extension's settings (b.settings) and their values on this device"),
             extensionSettings,
             {name,
              option(u"set"_s, ArgType::String, QT_TRANSLATE_NOOP("brulion::cli", "Change one: key=value (rest=90)")),
              flag(u"reset"_s, QT_TRANSLATE_NOOP("brulion::cli", "Every setting back to its default"))},
             QT_TRANSLATE_NOOP("brulion::cli",
                               "The extension's code is loaded in a sandbox to know its settings (its writes only "
                               "computed); the values are this device's, as the app keeps them (never synced)."));
    c.writes = true;
    out.append(c);
    c = make(u"extension.copy"_s,
             QT_TRANSLATE_NOOP("brulion::cli",
                               "Copy an extension page, with the pages it requires, into another notebook"),
             extensionCopy,
             {name,
              option(u"to"_s, ArgType::String, QT_TRANSLATE_NOOP("brulion::cli", "The other notebook's folder")),
              flag(u"replace"_s,
                   QT_TRANSLATE_NOOP("brulion::cli", "Write over a page there with other text (kept in its history)"))},
             QT_TRANSLATE_NOOP("brulion::cli",
                               "The same paths there; a page with the same text is left as it is. Nothing is "
                               "approved: the app there asks as for any code that arrives. Exit 4 on a conflict."));
    c.writes = true;
    out.append(c);
}

} // namespace brulion::cli
