#include "automation/plan.h"

#include "automation/chatlink.h"
#include "dates/local.h"
#include "index/vaultindex.h"
#include "markdown/journal.h"
#include "notebook/capture.h"
#include "notebook/journals.h"
#include "notebook/pages.h"
#include "text/text.h"
#include "util/paths.h"
#include "vault/vault.h"

#include <QDir>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>

using namespace Qt::StringLiterals;

namespace brulion::automation {

namespace {

const QStringList kPageKeys{u"page"_s, u"file"_s, u"path"_s};
const QStringList kWhereKeys{u"date"_s, u"daily"_s, u"heading"_s, u"block"_s, u"open"_s};

/// The views an address may open (docs/UI.md "Navigation"), with the parameters each may take.
struct ViewInfo {
    QStringView name;
    QStringList keys;
};

const QList<ViewInfo> &views()
{
    static const QList<ViewInfo> list{
            {u"journals", {u"date"_s}},
            {u"calendar", {u"date"_s, u"mode"_s}},
            {u"tasks", {u"query"_s}},
            {u"habits", {}},
            {u"graph", {}},
            {u"search", {u"query"_s, u"folder"_s}},
            {u"allPages", {u"folder"_s}},
            {u"tags", {}},
            {u"sync", {u"tab"_s}},
            {u"importExport", {u"tab"_s}},
            {u"trash", {}},
            {u"cleanup", {}},
            {u"settings", {u"section"_s}},
            {u"logs", {}},
            {u"chat", {}},
    };
    return list;
}

/// The view `name` names (also "journal", "home", "pages"), case aside.
const ViewInfo *viewNamed(QStringView rawName)
{
    QStringView name = rawName.trimmed();
    if (name.compare(u"journal", Qt::CaseInsensitive) == 0 || name.compare(u"home", Qt::CaseInsensitive) == 0)
        name = u"journals";
    else if (name.compare(u"pages", Qt::CaseInsensitive) == 0 || name.compare(u"all-pages", Qt::CaseInsensitive) == 0)
        name = u"allPages";
    else if (name.compare(u"import", Qt::CaseInsensitive) == 0 || name.compare(u"export", Qt::CaseInsensitive) == 0)
        name = u"importExport";
    for (const ViewInfo &v : views()) {
        if (name.compare(v.name, Qt::CaseInsensitive) == 0)
            return &v;
    }
    return nullptr;
}

/// A yes/no parameter: "1", "yes", "true", "on", "tak" (and "" when the key is given bare) are yes.
bool yes(const Request &request, QStringView key, bool fallback = false)
{
    if (!request.has({key}))
        return fallback;
    const QString v = request.value({key}).trimmed().toLower();
    if (v.isEmpty() || v == u"1" || v == u"yes" || v == u"true" || v == u"on" || v == u"tak")
        return true;
    if (v == u"0" || v == u"no" || v == u"false" || v == u"off" || v == u"nie")
        return false;
    return fallback;
}

/// A page reference split like a wiki link: "Dom#Zakupy" -> ("Dom", heading "Zakupy"),
/// "Dom#^abc" -> ("Dom", block "abc").
struct PageRef {
    QString name;
    QString heading;
    QString blockId;
};

PageRef splitRef(const QString &raw)
{
    PageRef out;
    const QString t = raw.trimmed();
    const qsizetype hash = t.indexOf(u'#');
    if (hash <= 0) {
        out.name = t;
        return out;
    }
    out.name = t.left(hash).trimmed();
    const QString fragment = t.mid(hash + 1).trimmed();
    if (fragment.startsWith(u'^'))
        out.blockId = fragment.mid(1).trimmed();
    else
        out.heading = fragment;
    return out;
}

struct Resolved {
    VaultPath path;
    bool exists = false;
    std::optional<QDate> day;
    QString error;
    QString detail;
};

Resolved journalOf(Vault &vault, QDate day)
{
    Resolved out;
    out.day = day;
    out.path = notebook::journalFileFor(vault, day);
    out.exists = vault.exists(out.path) || QFileInfo(vault.absolutePath(out.path)).isFile();
    if (out.path.isEmpty())
        out.error = u"failed"_s;
    return out;
}

/// The page `name` names, as brulion-cli reads a reference: a path (with or without ".md", any
/// letter case), "today" or an ISO date, a page name or an alias as [[links]] read it, else a day
/// a date phrase names. `mayBeNew`: a name nothing has is the path a new page of that name gets.
Resolved resolvePage(const QString &rawName, Vault &vault, const VaultIndex &index, const Context &context,
                     bool mayBeNew)
{
    Resolved out;
    QString name = rawName.trimmed();
    while (name.startsWith(u'/'))
        name.remove(0, 1);
    if (name.isEmpty()) {
        out.error = u"missing_parameter"_s;
        out.detail = u"page"_s;
        return out;
    }
    // 1. A path of a file.
    const VaultPath path = paths::normalize(name);
    if (path.isEmpty() || path.startsWith(u"../") || path == u"..") {
        out.error = u"not_a_page"_s;
        out.detail = name.left(200);
        return out;
    }
    // Never a file the notebook hides (.trash/, .brulion/, .obsidian/, .git/ …).
    if (paths::isIgnored(path)) {
        out.error = u"not_a_page"_s;
        out.detail = name.left(200);
        return out;
    }
    QStringList candidates{path};
    if (!paths::isMarkdown(path))
        candidates.append(path + u".md"_s);
    for (const QString &c : std::as_const(candidates)) {
        const VaultPath spelled = vault.existingCase(c);
        if (const auto st = vault.stat(spelled); st && !st->isDir) {
            if (!paths::isMarkdown(spelled)) {
                out.error = u"not_a_page"_s;
                out.detail = spelled;
                return out;
            }
            out.path = spelled;
            out.exists = true;
            out.day = markdown::journalDateFromPath(spelled, vault.config());
            return out;
        }
    }
    for (const QString &c : std::as_const(candidates)) {
        if (paths::isMarkdown(c) && QFileInfo(vault.absolutePath(c)).isFile()) {
            out.path = c; // just arrived on disk (the vault has not listed it yet)
            out.exists = true;
            out.day = markdown::journalDateFromPath(c, vault.config());
            return out;
        }
    }
    const Qt::DayOfWeek weekStart = vault.config().weekStartsOn;
    // 2. A day named unmistakably.
    if (!name.contains(u'/')) {
        if (const auto iso = dates::parseIsoDate(name))
            return journalOf(vault, *iso);
        if (name.compare(u"today", Qt::CaseInsensitive) == 0)
            return journalOf(vault, context.now.date());
    }
    // 3. A page name or an alias, as links read it.
    if (const auto linked = index.resolveLink(notebook::withoutMdExtension(name)); linked && paths::isMarkdown(*linked)) {
        out.path = *linked;
        out.exists = true;
        out.day = markdown::journalDateFromPath(*linked, vault.config());
        return out;
    }
    // 4. A day a phrase names ("wczoraj", "w piątek").
    if (!name.contains(u'/')) {
        if (const auto day = dayOf(name, context.now, weekStart, context.locale))
            return journalOf(vault, *day);
    }
    if (mayBeNew) {
        out.path = notebook::newPagePath(vault, name);
        if (out.path.isEmpty() || paths::isIgnored(out.path)) {
            out.path.clear();
            out.error = u"not_a_page"_s;
            out.detail = name.left(200);
        }
        return out;
    }
    out.error = u"page_not_found"_s;
    out.detail = name.left(200);
    return out;
}

/// A template named by `name`: a page of the templates folder (its name or a path inside it), else
/// any page the name resolves to inside it.
std::optional<VaultPath> templateNamed(const QString &rawName, Vault &vault, const VaultIndex &index)
{
    const QString name = notebook::withoutMdExtension(rawName.trimmed());
    if (name.isEmpty())
        return std::nullopt;
    const QString folder = paths::normalize(vault.config().templatesFolder);
    const QString key = text::foldKey(name);
    std::optional<VaultPath> byTitle;
    for (const QList<FileStat> files = vault.markdownFiles(); const FileStat &f : files) {
        const bool inside = folder.isEmpty() || f.path.startsWith(folder + u'/');
        if (!inside)
            continue;
        const QString relative = paths::stripMarkdownExtension(folder.isEmpty() ? f.path : f.path.mid(folder.size() + 1));
        if (text::foldKey(relative) == key)
            return f.path;
        if (!byTitle && text::foldKey(paths::pageTitle(f.path)) == key)
            byTitle = f.path;
    }
    if (byTitle)
        return byTitle;
    if (const auto linked = index.resolveLink(name); linked && paths::isMarkdown(*linked)
                                                     && (folder.isEmpty() || linked->startsWith(folder + u'/')))
        return linked;
    return std::nullopt;
}

Plan failed(Plan plan, const QString &error, const QString &detail)
{
    plan.error = error;
    plan.detail = detail;
    return plan;
}

/// Where append, prepend and task write: a page, a day, today's note; its heading or block.
Plan target(Plan plan, const Request &request, Vault &vault, const VaultIndex &index, const Context &context)
{
    const QString pageValue = request.value({u"page", u"file", u"path"});
    const QString dateValue = request.value({u"date"});
    if (!pageValue.trimmed().isEmpty() && !dateValue.trimmed().isEmpty())
        return failed(plan, u"unknown_parameter"_s, u"page and date"_s);
    Resolved where;
    PageRef ref;
    if (!pageValue.trimmed().isEmpty()) {
        ref = splitRef(pageValue);
        where = resolvePage(ref.name, vault, index, context, yes(request, u"create"));
    } else if (!dateValue.trimmed().isEmpty()) {
        const auto day = dayOf(dateValue, context.now, vault.config().weekStartsOn, context.locale);
        if (!day)
            return failed(plan, u"date_not_understood"_s, u"date"_s);
        where = journalOf(vault, *day);
    } else {
        where = journalOf(vault, context.now.date());
    }
    if (!where.error.isEmpty())
        return failed(plan, where.error, where.detail);
    plan.path = where.path;
    plan.exists = where.exists;
    plan.journal = where.day.has_value();
    if (where.day)
        plan.day = *where.day;
    if (!plan.exists && plan.journal)
        plan.initialText = notebook::newJournalText(vault, *where.day, context.now, context.locale);
    const QString heading = request.value({u"heading"});
    const QString block = request.value({u"block"});
    if ((!heading.isEmpty() || !block.isEmpty()) && (!ref.heading.isEmpty() || !ref.blockId.isEmpty()))
        return failed(plan, u"unknown_parameter"_s, u"heading or block given twice"_s);
    plan.insert.heading = heading.isEmpty() ? ref.heading : heading;
    plan.insert.blockId = block.isEmpty() ? ref.blockId : block;
    if (!plan.insert.heading.trimmed().isEmpty() && !plan.insert.blockId.trimmed().isEmpty())
        return failed(plan, u"unknown_parameter"_s, u"heading and block"_s);
    plan.open = yes(request, u"open");
    return plan;
}

QJsonObject pageRoute(const VaultPath &path, std::optional<QDate> day)
{
    if (day)
        return {{u"view"_s, u"journal"_s}, {u"date"_s, day->toString(Qt::ISODate)}, {u"path"_s, path}};
    return {{u"view"_s, u"page"_s}, {u"path"_s, path}};
}

} // namespace

QStringList knownParameters(Action action)
{
    switch (action) {
    case Action::Open: return kPageKeys + QStringList{u"heading"_s, u"block"_s, u"line"_s};
    case Action::Daily: return {u"date"_s};
    case Action::New:
        return {u"title"_s, u"name"_s, u"page"_s, u"folder"_s, u"template"_s, u"text"_s, u"content"_s, u"mode"_s,
                u"open"_s};
    case Action::Append:
    case Action::Prepend: return kPageKeys + kWhereKeys + QStringList{u"text"_s, u"content"_s, u"create"_s};
    case Action::Task:
        return kPageKeys + kWhereKeys
               + QStringList{u"text"_s,  u"due"_s,   u"scheduled"_s, u"start"_s, u"remind"_s,
                             u"alarm"_s, u"every"_s, u"priority"_s,  u"parse"_s, u"create"_s};
    case Action::Capture: return {u"text"_s, u"open"_s};
    case Action::Search: return {u"query"_s, u"q"_s, u"text"_s};
    case Action::View: {
        QStringList keys{u"name"_s, u"view"_s};
        for (const ViewInfo &v : views()) {
            for (const QString &k : v.keys) {
                if (!keys.contains(k))
                    keys.append(k);
            }
        }
        return keys;
    }
    case Action::Run: return {u"extension"_s, u"command"_s, u"args"_s, u"page"_s};
    case Action::Pair: return {u"url"_s, u"address"_s, u"code"_s};
    case Action::Chat: return {u"prompt"_s, u"partner"_s, u"page"_s};
    }
    return {};
}

Plan plan(const Request &request, Vault &vault, const VaultIndex &index, const Context &context)
{
    Plan out;
    out.action = request.action;
    out.access = accessOf(request);
    if (const QStringList unknown = request.unknownKeys(knownParameters(request.action)); !unknown.isEmpty())
        return failed(out, u"unknown_parameter"_s, unknown.join(u", "_s).left(200));
    const Qt::DayOfWeek weekStart = vault.config().weekStartsOn;
    const QString text = request.value({u"text", u"content"});

    switch (request.action) {
    case Action::Open: {
        const PageRef ref = splitRef(request.value({u"page", u"file", u"path"}));
        if (ref.name.isEmpty())
            return failed(out, u"missing_parameter"_s, u"page"_s);
        const Resolved page = resolvePage(ref.name, vault, index, context, false);
        if (!page.error.isEmpty())
            return failed(out, page.error, page.detail);
        out.path = page.path;
        out.exists = page.exists;
        out.route = pageRoute(page.path, page.day);
        if (page.day && !page.exists)
            return out; // a day without a note: its empty day
        QString heading = request.value({u"heading"});
        QString block = request.value({u"block"});
        if (heading.isEmpty())
            heading = ref.heading;
        if (block.isEmpty())
            block = ref.blockId;
        if (!heading.trimmed().isEmpty() || !block.trimmed().isEmpty() || request.has({u"line"}))
            out.route = {{u"view"_s, u"page"_s}, {u"path"_s, page.path}};
        if (!heading.trimmed().isEmpty())
            out.route.insert(u"heading"_s, heading.trimmed());
        if (!block.trimmed().isEmpty()) {
            QString id = block.trimmed();
            if (id.startsWith(u"((") && id.endsWith(u"))"))
                id = id.mid(2, id.size() - 4);
            if (id.startsWith(u'^'))
                id.remove(0, 1);
            out.route.insert(u"blockId"_s, id.trimmed());
        }
        if (request.has({u"line"})) {
            bool ok = false;
            const int line = request.value({u"line"}).trimmed().toInt(&ok);
            if (!ok || line < 1)
                return failed(out, u"missing_parameter"_s, u"line (a number from 1)"_s);
            out.route.insert(u"line"_s, line - 1);
        }
        return out;
    }
    case Action::Daily: {
        const QString date = request.value({u"date"});
        const auto day = date.trimmed().isEmpty() ? std::optional<QDate>(context.now.date())
                                                  : dayOf(date, context.now, weekStart, context.locale);
        if (!day)
            return failed(out, u"date_not_understood"_s, u"date"_s);
        const Resolved journal = journalOf(vault, *day);
        out.path = journal.path;
        out.exists = journal.exists;
        out.journal = true;
        out.day = *day;
        out.route = {{u"view"_s, u"journal"_s}, {u"date"_s, day->toString(Qt::ISODate)}};
        return out;
    }
    case Action::Search:
        out.route = {{u"view"_s, u"search"_s}, {u"query"_s, request.value({u"query", u"q", u"text"}).trimmed()}};
        return out;
    case Action::Pair: {
        // Only the dialog, filled in: the user pairs (a link never adds a computer by itself).
        const QString url = request.value({u"url", u"address"}).trimmed();
        if (url.isEmpty())
            return failed(out, u"missing_parameter"_s, u"url"_s);
        out.route = {{u"view"_s, u"settings"_s},
                     {u"section"_s, u"extensions"_s},
                     {u"pairUrl"_s, url.left(2000)},
                     {u"pairCode"_s, request.value({u"code"}).trimmed().left(64)}};
        return out;
    }
    case Action::Chat: {
        // A new conversation's draft: the prompt in the field, never sent (the user presses Send),
        // without what nobody sees — and the view says so.
        const CleanedPrompt prompt = cleanPrompt(request.value({u"prompt"}));
        out.route = {{u"view"_s, u"chat"_s}, {u"new"_s, true}, {u"fromLink"_s, true}};
        if (!prompt.text.isEmpty())
            out.route.insert(u"draft"_s, prompt.text);
        if (prompt.hidden > 0)
            out.route.insert(u"hidden"_s, prompt.hidden);
        if (prompt.capped)
            out.route.insert(u"capped"_s, true);
        // Who answers: the app keeps it only when that partner is set up on the device.
        if (const QString partner = cleanPrompt(request.value({u"partner"}), 200).text; !partner.isEmpty())
            out.route.insert(u"partner"_s, partner);
        // The page it is about, when the notebook has it.
        if (const QString name = cleanPrompt(request.value({u"page"}), 500).text; !name.isEmpty()) {
            const Resolved page = resolvePage(splitRef(name).name, vault, index, context, false);
            if (page.error.isEmpty() && page.exists)
                out.route.insert(u"page"_s, paths::pageTitle(page.path));
            else
                out.route.insert(u"pageMissing"_s, name);
        }
        return out;
    }
    case Action::View: {
        const QString name = request.value({u"name", u"view"});
        if (name.trimmed().isEmpty())
            return failed(out, u"missing_parameter"_s, u"name"_s);
        const ViewInfo *view = viewNamed(name);
        if (!view)
            return failed(out, u"unknown_view"_s, name.trimmed().left(64));
        out.route = {{u"view"_s, view->name.toString()}};
        for (const auto &[key, value] : request.params) {
            if (key == u"name" || key == u"view")
                continue;
            if (!view->keys.contains(key))
                return failed(out, u"unknown_parameter"_s, key);
            QString v = value.trimmed();
            if (key == u"date") {
                const auto day = dayOf(v, context.now, weekStart, context.locale);
                if (!day)
                    return failed(out, u"date_not_understood"_s, u"date"_s);
                v = day->toString(Qt::ISODate);
            }
            // allPages and search take the folder as `path` / `folder` (docs/UI.md "Navigation").
            out.route.insert(key == u"folder" && view->name == u"allPages" ? u"path"_s : key, v);
        }
        return out;
    }
    case Action::Capture: {
        if (text.trimmed().isEmpty()) {
            out.route = {{u"view"_s, u"capture"_s}};
            return out;
        }
        const notebook::CaptureLine line = notebook::buildCaptureLine(text, context.now, weekStart, context.locale);
        if (line.kind == notebook::CaptureLine::Kind::Empty)
            return failed(out, u"missing_text"_s, u"text"_s);
        Plan where = target(out, Request{}, vault, index, context); // today's note
        if (!where.ok())
            return where;
        where.insert.block = line.block;
        where.kind = line.kind == notebook::CaptureLine::Kind::Reminder ? u"reminder"_s
                     : line.kind == notebook::CaptureLine::Kind::Due    ? u"due"_s
                                                                        : u"note"_s;
        if (line.kind == notebook::CaptureLine::Kind::Reminder)
            where.reminder = line.when;
        where.open = yes(request, u"open");
        return where;
    }
    case Action::Append:
    case Action::Prepend: {
        if (text.trimmed().isEmpty())
            return failed(out, u"missing_text"_s, u"text"_s);
        Plan where = target(out, request, vault, index, context);
        if (!where.ok())
            return where;
        where.insert.block = notebook::blockFromText(text);
        where.insert.place = request.action == Action::Prepend ? Place::Start : Place::End;
        where.kind = u"note"_s;
        return where;
    }
    case Action::Task: {
        TaskSpec spec;
        spec.text = text;
        spec.due = request.value({u"due"});
        spec.scheduled = request.value({u"scheduled"});
        spec.start = request.value({u"start"});
        spec.remind = request.value({u"remind"});
        spec.every = request.value({u"every"});
        spec.priority = request.value({u"priority"});
        spec.parseText = yes(request, u"parse", true);
        // alarm=yes makes the reminder an alarm; alarm=<a date> is the alarm itself.
        if (request.has({u"alarm"})) {
            const QString alarm = request.value({u"alarm"}).trimmed();
            const QString lower = alarm.toLower();
            const bool flag = alarm.isEmpty() || lower == u"1" || lower == u"yes" || lower == u"true" || lower == u"on"
                              || lower == u"tak";
            if (flag) {
                spec.alarm = true;
            } else if (!(lower == u"0" || lower == u"no" || lower == u"false" || lower == u"off" || lower == u"nie")) {
                if (!spec.remind.trimmed().isEmpty())
                    return failed(out, u"unknown_parameter"_s, u"remind and alarm"_s);
                spec.remind = alarm;
                spec.alarm = true;
            }
        }
        const BuiltTask task =
                buildTask(spec, context.now, weekStart, context.locale, vault.config().defaultReminderTime);
        if (!task.error.isEmpty())
            return failed(out, task.error, task.detail);
        Plan where = target(out, request, vault, index, context);
        if (!where.ok())
            return where;
        where.insert.block = task.block;
        where.reminder = task.reminder;
        where.kind = task.reminder.isValid() ? (task.alarm ? u"alarm"_s : u"reminder"_s)
                     : task.due              ? u"due"_s
                                             : u"task"_s;
        return where;
    }
    case Action::New: {
        const QString title = notebook::withoutMdExtension(request.value({u"title", u"name", u"page"}).trimmed());
        if (title.isEmpty())
            return failed(out, u"missing_title"_s, u"title"_s);
        const QString folder = request.has({u"folder"}) ? request.value({u"folder"}).trimmed() : QString();
        const notebook::CreatedPage planned = notebook::plannedPage(vault, title, folder);
        if (planned.path.isEmpty())
            return failed(out, u"not_a_page"_s, title.left(200));
        out.path = planned.path;
        if (planned.existing || vault.exists(out.path) || QFileInfo(vault.absolutePath(out.path)).isFile()) {
            out.exists = true;
            out.route = pageRoute(out.path, std::nullopt);
            return failed(out, u"exists"_s, out.path);
        }
        QString content;
        if (const QString name = request.value({u"template"}); !name.trimmed().isEmpty()) {
            const auto tpl = templateNamed(name, vault, index);
            if (!tpl)
                return failed(out, u"template_not_found"_s, name.trimmed().left(200));
            content = notebook::templateText(vault, *tpl, context.now, paths::pageTitle(out.path), context.locale);
        }
        if (!text.trimmed().isEmpty()) {
            if (content.trimmed().isEmpty()) {
                content = text;
            } else {
                const QString eol = text::detectEol(content);
                QString base = content;
                while (base.endsWith(u'\n') || base.endsWith(u'\r'))
                    base.chop(1);
                content = base + eol + eol + QString(text).replace(u"\n"_s, eol);
            }
            if (!content.endsWith(u'\n'))
                content += text::detectEol(content);
        }
        std::optional<NoteMode> mode;
        if (const QString m = request.value({u"mode"}).trimmed().toLower(); !m.isEmpty()) {
            if (m == u"outline" || m == u"konspekt")
                mode = NoteMode::Outline;
            else if (m == u"document" || m == u"dokument")
                mode = NoteMode::Document;
            else
                return failed(out, u"unknown_parameter"_s, u"mode (outline or document)"_s);
        }
        out.initialText = notebook::newPageText(vault, out.path, mode, content);
        out.newPage = true;
        out.kind = u"page"_s;
        out.open = yes(request, u"open", true);
        out.route = pageRoute(out.path, std::nullopt);
        return out;
    }
    case Action::Run: {
        out.extension = request.value({u"extension"}).trimmed();
        out.command = request.value({u"command"}).trimmed();
        // "silownia/rekord" in one parameter.
        if (out.command.isEmpty() && out.extension.contains(u'/')) {
            out.command = out.extension.section(u'/', 1);
            out.extension = out.extension.section(u'/', 0, 0);
        } else if (out.extension.isEmpty() && out.command.contains(u'/')) {
            out.extension = out.command.section(u'/', 0, 0);
            out.command = out.command.section(u'/', 1);
        }
        if (out.extension.isEmpty())
            return failed(out, u"missing_parameter"_s, u"extension"_s);
        if (out.command.isEmpty())
            return failed(out, u"missing_parameter"_s, u"command"_s);
        if (const QString args = request.value({u"args"}).trimmed(); !args.isEmpty()) {
            QJsonParseError error;
            const QJsonDocument doc = QJsonDocument::fromJson(args.toUtf8(), &error);
            if (!doc.isObject())
                return failed(out, u"bad_args"_s, u"args must be a JSON object"_s);
            out.args = doc.object();
        }
        if (const QString page = request.value({u"page"}); !page.trimmed().isEmpty()) {
            const Resolved resolved = resolvePage(splitRef(page).name, vault, index, context, false);
            if (!resolved.error.isEmpty())
                return failed(out, resolved.error, resolved.detail);
            out.page = resolved.path;
        }
        return out;
    }
    }
    return failed(out, u"unknown_action"_s, actionName(request.action));
}

std::optional<QString> planned(const Plan &plan, const QString &current, bool exists, QString *error, int *line)
{
    const auto fail = [error](const QString &code) -> std::optional<QString> {
        if (error)
            *error = code;
        return std::nullopt;
    };
    if (plan.newPage) {
        if (exists)
            return fail(u"exists"_s);
        if (line)
            *line = 0;
        return plan.initialText;
    }
    const QString base = exists ? current : plan.initialText;
    const Inserted inserted = insertBlock(base, plan.insert);
    if (!inserted.ok)
        return fail(inserted.error);
    if (line)
        *line = inserted.line;
    return inserted.content;
}

Outcome failure(const QString &error, const QString &message, Action action)
{
    Outcome out;
    out.action = actionName(action);
    out.error = error;
    out.message = message;
    return out;
}

Outcome execute(const Plan &plan, Writer &writer, const QString &requestId)
{
    Outcome out;
    out.action = actionName(plan.action);
    out.id = requestId;
    out.path = plan.path;
    out.route = plan.route;
    if (!plan.ok()) {
        out.error = plan.error;
        out.message = plan.detail;
        return out;
    }
    if (plan.access == Access::Open) {
        out.ok = true;
        return out;
    }
    if (plan.access == Access::Run) {
        out.error = u"needs_app"_s;
        out.message = u"extension commands run in the app"_s;
        return out;
    }
    notebook::Notebook::WriteOptions options;
    options.mayCreate = plan.newPage || plan.journal || !plan.exists;
    options.initialText = plan.newPage ? QString() : plan.initialText;
    int line = -1;
    QString why;
    const notebook::Notebook::WriteResult written = writer.edit(
            plan.path,
            [&](const QString &current, bool exists, QString *next) {
                why.clear();
                const auto text = planned(plan, current, exists, &why, &line);
                if (!text)
                    return false;
                *next = *text;
                return true;
            },
            options);
    using W = notebook::Notebook::WriteResult;
    switch (written.status) {
    case W::Written:
    case W::Unchanged:
    case W::DryRun:
        out.ok = true;
        out.created = written.created;
        out.line = line;
        out.block = plan.newPage ? QString() : plan.insert.block;
        out.kind = plan.kind;
        out.reminder = plan.reminder;
        out.alarm = plan.kind == u"alarm";
        if (!plan.newPage && line >= 0)
            out.route = QJsonObject{{u"view"_s, u"page"_s}, {u"path"_s, plan.path}, {u"line"_s, line}};
        return out;
    case W::Refused: out.error = why.isEmpty() ? u"failed"_s : why; break;
    case W::NotFound: out.error = u"page_not_found"_s; break;
    case W::HashMismatch:
    case W::Conflict: out.error = u"conflict"_s; break;
    case W::Encoding: out.error = u"not_utf8"_s; break;
    case W::Failed: out.error = u"failed"_s; break;
    }
    out.message = written.error.isEmpty() ? plan.path : written.error;
    return out;
}

QJsonObject Outcome::toJson() const
{
    QJsonObject o{{u"ok"_s, ok}, {u"action"_s, action}};
    const auto put = [&o](const QString &key, const QString &value) {
        if (!value.isEmpty())
            o.insert(key, value);
    };
    put(u"error"_s, error);
    put(u"message"_s, message);
    put(u"path"_s, path);
    if (line >= 0)
        o.insert(u"line"_s, line + 1);
    if (created)
        o.insert(u"created"_s, true);
    put(u"block"_s, block);
    put(u"kind"_s, kind);
    if (reminder.isValid())
        o.insert(u"reminder"_s, reminder.toString());
    if (alarm)
        o.insert(u"alarm"_s, true);
    if (!route.isEmpty())
        o.insert(u"route"_s, route);
    put(u"id"_s, id);
    return o;
}

QList<std::pair<QString, QString>> Outcome::callbackParams() const
{
    QList<std::pair<QString, QString>> out;
    if (!ok) {
        out.append({u"errorCode"_s, error});
        out.append({u"errorMessage"_s, message});
    }
    if (!path.isEmpty())
        out.append({u"path"_s, path});
    if (line >= 0)
        out.append({u"line"_s, QString::number(line + 1)});
    if (!id.isEmpty())
        out.append({u"id"_s, id});
    return out;
}

NotebookMatch matchNotebook(const QString &rawWanted, const QList<KnownNotebook> &known)
{
    NotebookMatch out;
    const QString wanted = rawWanted.trimmed();
    if (wanted.isEmpty() || known.isEmpty()) {
        out.error = u"unknown_notebook"_s;
        return out;
    }
    if (QDir::isAbsolutePath(wanted) || wanted.startsWith(u'~')) {
        const QString path = QDir::cleanPath(wanted.startsWith(u'~') ? QDir::homePath() + wanted.mid(1) : wanted);
        for (const Qt::CaseSensitivity cs : {Qt::CaseSensitive, Qt::CaseInsensitive}) {
            QList<int> hits;
            for (int i = 0; i < known.size(); ++i) {
                if (QDir::cleanPath(known.at(i).root).compare(path, cs) == 0)
                    hits.append(i);
            }
            if (hits.size() == 1) {
                out.index = hits.first();
                return out;
            }
        }
        out.error = u"unknown_notebook"_s;
        return out;
    }
    const QString key = text::foldKey(wanted);
    QList<int> hits;
    for (int i = 0; i < known.size(); ++i) {
        const KnownNotebook &n = known.at(i);
        if (text::foldKey(n.name) == key || text::foldKey(QFileInfo(n.root).fileName()) == key)
            hits.append(i);
    }
    if (hits.size() == 1) {
        out.index = hits.first();
        return out;
    }
    if (hits.isEmpty()) {
        out.error = u"unknown_notebook"_s;
        return out;
    }
    out.error = u"ambiguous_notebook"_s;
    for (int i : std::as_const(hits))
        out.names.append(known.at(i).root);
    return out;
}

} // namespace brulion::automation
