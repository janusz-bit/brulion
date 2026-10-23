// Reading the notebook through its index: info, query, search, tags, tag, backlinks, links,
// unlinked, calendar.
#include "commands_p.h"

#include "index/query.h"
#include "index/vaultindex.h"
#include "markdown/journal.h"
#include "notebook/json.h"
#include "notebook/pages.h"
#include "reminders/calendar.h"
#include "singleinstance.h"
#include "text/text.h"
#include "util/paths.h"
#include "vault/vault.h"

#include <QCoreApplication>

using namespace Qt::StringLiterals;

namespace brulion::cli {

namespace {

QString scanStateName(VaultScanStatus::State state)
{
    switch (state) {
    case VaultScanStatus::State::Ok: return u"ok"_s;
    case VaultScanStatus::State::RootMissing: return u"rootMissing"_s;
    case VaultScanStatus::State::StorageChanged: return u"storageChanged"_s;
    case VaultScanStatus::State::Unreadable: return u"unreadable"_s;
    }
    return {};
}

Result info(Call &call)
{
    notebook::Notebook *nb = call.nb();
    if (!nb)
        return call.failed();
    Vault &v = nb->vault();
    VaultIndex &index = nb->index();
    int journals = int(index.journals().size());
    int openTasks = 0;
    for (const TaskRef &t : index.tasks()) {
        if (t.task.status == TaskStatus::Todo || t.task.status == TaskStatus::Doing)
            ++openTasks;
    }
    const VaultScanStatus scan = v.scanStatus();
    QJsonObject scanJson{{u"state"_s, scanStateName(scan.state)}, {u"message"_s, scan.message}};
    if (!scan.unreadableFolders.isEmpty())
        scanJson.insert(u"unreadableFolders"_s, QJsonArray::fromStringList(scan.unreadableFolders));
    if (!scan.isOk())
        call.warn(u"scan_incomplete"_s, tr("Not every folder of the notebook could be read: %1").arg(scan.message));
    const ipc::SingleInstance app(ipc::SingleInstance::keyFor(call.ctx.appDataDir()));
    const QString current = call.ctx.settingsValue(u"vaults/current"_s);
    const bool appNotebook = !current.isEmpty() && Vault::idForPath(current) == Vault::idForPath(v.rootPath());
    QJsonObject o{{u"version"_s, QCoreApplication::applicationVersion()},
                  {u"vault"_s, QJsonObject{{u"path"_s, v.rootPath()},
                                           {u"name"_s, paths::basename(v.rootPath())},
                                           {u"id"_s, v.id()},
                                           {u"source"_s, call.ctx.vaultSource()}}},
                  {u"stateDir"_s, call.ctx.stateDir()},
                  {u"language"_s, call.ctx.language()},
                  {u"config"_s, notebook::vaultConfigJson(v.config())},
                  {u"configWarnings"_s, QJsonArray::fromStringList(v.configWarnings())},
                  {u"counts"_s, QJsonObject{{u"pages"_s, int(index.paths().size()) - journals},
                                            {u"journals"_s, journals},
                                            {u"attachments"_s, int(index.attachments().size())},
                                            {u"openTasks"_s, openTasks},
                                            {u"tags"_s, int(index.tags().size())}}},
                  {u"scan"_s, scanJson},
                  {u"gui"_s, QJsonObject{{u"running"_s, app.isPrimaryRunning()}, {u"sameNotebook"_s, appNotebook}}}};
    return call.ok(o);
}

Result query(Call &call)
{
    VaultIndex *index = call.index();
    if (!index)
        return call.failed();
    const QString source = call.str(u"query"_s);
    const std::variant<Query, QString> parsed = parseQuery(source);
    if (const QString *message = std::get_if<QString>(&parsed))
        return call.fail(ExitParse, u"query_error"_s, *message, {{u"query"_s, source}});
    QueryOptions options;
    options.now = call.ctx.now().time();
    if (call.has(u"page"_s)) {
        const auto ref = call.page(call.str(u"page"_s), false);
        if (!ref)
            return call.failed();
        options.currentPath = ref->path;
    }
    QJsonObject result = notebook::queryResultJson(*index, runQuery(std::get<Query>(parsed), *index, call.ctx.today(),
                                                                    call.vault()->config().weekStartsOn, options));
    addRefs(call, result);
    QString text;
    for (const QJsonValue &v : result.value(u"items"_s).toArray()) {
        const QJsonObject o = v.toObject();
        QString line = o.value(u"text"_s).toString().simplified();
        if (o.contains(u"task"_s))
            line.prepend(u'[' + o.value(u"task"_s).toObject().value(u"status"_s).toString() + u"] "_s);
        if (o.contains(u"reminder"_s))
            line.prepend(o.value(u"reminder"_s).toObject().value(u"at"_s).toString() + u"  "_s);
        text += line + u"  "_s + (o.contains(u"ref"_s) ? o.value(u"ref"_s).toString() : o.value(u"path"_s).toString())
                + u'\n';
    }
    return call.ok(result, text);
}

Result search(Call &call)
{
    VaultIndex *index = call.index();
    if (!index)
        return call.failed();
    const QString text = call.str(u"text"_s).trimmed();
    if (text.isEmpty())
        return call.fail(ExitUsage, u"text_required"_s, tr("Search for what?"));
    const int limit = std::clamp(call.integer(u"limit"_s, 20), 1, 500);
    QJsonArray hits;
    QString out;
    for (const SearchHit &hit : index->search(text, limit)) {
        QJsonObject o = notebook::searchHitJson(hit, index->config());
        if (hit.line >= 0)
            o.insert(u"ref"_s, Call::lineRef(hit.path, noteText(call, hit.path), hit.line));
        hits.append(o);
        out += hit.path + (hit.line >= 0 ? u':' + QString::number(hit.line + 1) : QString()) + u"  "_s
               + hit.excerpt.simplified() + u'\n';
    }
    return call.ok(QJsonObject{{u"query"_s, text}, {u"hits"_s, hits}}, out);
}

Result tags(Call &call)
{
    VaultIndex *index = call.index();
    if (!index)
        return call.failed();
    QString prefix = call.str(u"prefix"_s).trimmed();
    if (prefix.startsWith(u'#'))
        prefix.remove(0, 1);
    const QString key = text::foldKey(prefix);
    QJsonArray out;
    QString text;
    for (const TagCount &t : index->tags()) {
        if (!key.isEmpty() && !text::foldKey(t.tag).startsWith(key))
            continue;
        const auto page = index->resolveTag(t.tag);
        out.append(QJsonObject{{u"tag"_s, t.tag},
                               {u"display"_s, t.display.isEmpty() ? t.tag : t.display},
                               {u"count"_s, t.count},
                               {u"page"_s, page.value_or(QString())}});
        text += u'#' + (t.display.isEmpty() ? t.tag : t.display) + u"  "_s + QString::number(t.count) + u'\n';
    }
    return call.ok(QJsonObject{{u"tags"_s, out}}, text);
}

Result tag(Call &call)
{
    VaultIndex *index = call.index();
    if (!index)
        return call.failed();
    QString name = call.str(u"name"_s).trimmed();
    if (name.startsWith(u'#'))
        name.remove(0, 1);
    if (name.startsWith(u"[["_s) && name.endsWith(u"]]"_s))
        name = name.mid(2, name.size() - 4).trimmed();
    if (name.isEmpty())
        return call.fail(ExitUsage, u"name_required"_s, tr("Which tag?"));
    const QString key = name.normalized(QString::NormalizationForm_C).toLower();
    QString display = name;
    int count = 0;
    QStringList subTags;
    for (const TagCount &t : index->tags()) {
        if (t.tag == key) {
            count = t.count;
            if (!t.display.isEmpty())
                display = t.display;
        } else if (t.tag.startsWith(key + u'/')) {
            subTags.append(t.tag);
        }
    }
    // The blocks carrying it (with sub-tags, as the query language's tag: does).
    Query q;
    q.type = QueryType::Blocks;
    q.explicitType = true;
    QueryFilter f;
    f.kind = QueryFilterKind::Tag;
    f.values = {key};
    q.filters = {f};
    QueryOptions options;
    options.now = call.ctx.now().time();
    QJsonObject blocks = notebook::queryResultJson(*index, runQuery(q, *index, call.ctx.today(), Qt::Monday, options));
    addRefs(call, blocks);
    const auto page = index->resolveTag(key);
    QJsonObject o{{u"tag"_s, key},
                  {u"display"_s, display},
                  {u"count"_s, count},
                  {u"page"_s, page.value_or(QString())},
                  {u"pageExists"_s, page.has_value()},
                  {u"subTags"_s, QJsonArray::fromStringList(subTags)},
                  {u"total"_s, blocks.value(u"total"_s)},
                  {u"items"_s, blocks.value(u"items"_s)}};
    if (!page)
        o.insert(u"newPage"_s, notebook::newPagePath(*call.vault(), display)); // where a click on it creates it
    QString text;
    for (const QJsonValue &v : blocks.value(u"items"_s).toArray())
        text += v.toObject().value(u"text"_s).toString().simplified() + u"  "_s
                + v.toObject().value(u"ref"_s).toString() + u'\n';
    return call.ok(o, text);
}

Result backlinks(Call &call)
{
    const auto ref = call.page(call.str(u"ref"_s), false);
    if (!ref)
        return call.failed();
    const QJsonArray items = backlinksJson(call, ref->path);
    QString text;
    for (const QJsonValue &v : items) {
        const QJsonObject o = v.toObject();
        text += (o.value(u"kind"_s).toString() == u"tag" ? u"#  "_s : u"[[ "_s) + o.value(u"text"_s).toString()
                + u"  "_s + o.value(u"path"_s).toString() + u'\n';
    }
    return call.ok(QJsonObject{{u"path"_s, ref->path}, {u"exists"_s, ref->exists}, {u"items"_s, items}}, text);
}

Result links(Call &call)
{
    const auto ref = call.page(call.str(u"ref"_s), true);
    if (!ref)
        return call.failed();
    VaultIndex *index = call.index();
    QJsonArray out;
    QString text;
    for (const ResolvedLink &l : index->outgoingLinks(ref->path)) {
        QJsonObject o{{u"kind"_s, notebook::linkKindName(l.link.kind)},
                      {u"target"_s, l.link.target},
                      {u"line"_s, l.link.line},
                      {u"resolved"_s, l.target},
                      {u"exists"_s, !l.target.isEmpty()}};
        if (!l.link.display.isEmpty())
            o.insert(u"display"_s, l.link.display);
        if (!l.link.heading.isEmpty())
            o.insert(u"heading"_s, l.link.heading);
        if (!l.link.blockId.isEmpty())
            o.insert(u"blockId"_s, l.link.blockId);
        out.append(o);
        text += l.link.target + u"  →  "_s + (l.target.isEmpty() ? u"(missing)"_s : l.target) + u'\n';
    }
    return call.ok(QJsonObject{{u"path"_s, ref->path}, {u"links"_s, out}}, text);
}

Result unlinked(Call &call)
{
    const auto ref = call.page(call.str(u"ref"_s), true);
    if (!ref)
        return call.failed();
    QJsonArray out;
    QString text;
    for (const UnlinkedRef &u : call.index()->unlinkedReferences(ref->path)) {
        out.append(QJsonObject{{u"path"_s, u.path},
                               {u"title"_s, paths::pageTitle(u.path)},
                               {u"line"_s, u.line},
                               {u"excerpt"_s, u.excerpt},
                               {u"ref"_s, Call::lineRef(u.path, noteText(call, u.path), u.line)}});
        text += u.path + u':' + QString::number(u.line + 1) + u"  "_s + u.excerpt + u'\n';
    }
    return call.ok(QJsonObject{{u"path"_s, ref->path}, {u"items"_s, out}}, text);
}

Result calendar(Call &call)
{
    VaultIndex *index = call.index();
    if (!index)
        return call.failed();
    const auto from = call.date(call.str(u"from"_s));
    if (!from)
        return call.failed();
    const auto to = call.has(u"to"_s) ? call.date(call.str(u"to"_s)) : std::optional<QDate>(from->addDays(6));
    if (!to)
        return call.failed();
    if (*to < *from || from->daysTo(*to) > 400)
        return call.fail(ExitUsage, u"bad_range"_s, tr("--to must follow --from, at most 400 days later"));
    QJsonArray out;
    QString text;
    for (const CalendarItem &item : calendarItems(*index, *from, *to)) {
        QJsonObject o = notebook::calendarItemJson(item);
        if (item.kind != CalendarItem::Journal)
            o.insert(u"ref"_s, Call::lineRef(item.path, noteText(call, item.path), item.line));
        out.append(o);
        text += item.date.toString(Qt::ISODate) + (item.time ? u' ' + item.time->toString(u"HH:mm"_s) : QString())
                + u"  "_s + notebook::calendarKindName(item.kind) + u"  "_s + item.title + u'\n';
    }
    return call.ok(QJsonObject{{u"from"_s, from->toString(Qt::ISODate)},
                               {u"to"_s, to->toString(Qt::ISODate)},
                               {u"items"_s, out}},
                   text);
}

Command make(const QString &id, const char *summary, Handler run, QList<Arg> args)
{
    Command c;
    c.id = id;
    c.summary = cliText(summary);
    c.run = std::move(run);
    c.args = std::move(args);
    return c;
}

} // namespace

void addIndexCommands(QList<Command> &out)
{
    const Arg ref = positional(u"ref"_s, ArgType::Ref,
                               QT_TRANSLATE_NOOP("brulion::cli", "The page: a path, a name, a date or an item ref"));
    out.append(make(u"info"_s,
                    QT_TRANSLATE_NOOP("brulion::cli",
                                      "The notebook: folder, settings, counts, scan state, whether the app runs"),
                    info, {}));
    Command c = make(
            u"query"_s,
            QT_TRANSLATE_NOOP("brulion::cli", "Run a query (docs/SYNTAX.md §10), the same results as a ```query block"),
            query,
            {positional(u"query"_s, ArgType::String,
                        QT_TRANSLATE_NOOP("brulion::cli",
                                          "type:tasks status:open due:<=+7d sort:due — keys also in Polish"),
                        true, true),
             option(u"page"_s, ArgType::Ref, QT_TRANSLATE_NOOP("brulion::cli", "The page page:this / ref:this mean"))});
    out.append(c);
    out.append(make(
            u"search"_s,
            QT_TRANSLATE_NOOP("brulion::cli", "Full-text search, diacritic-insensitive (\"zolw\" finds \"żółw\")"),
            search,
            {positional(u"text"_s, ArgType::String, QT_TRANSLATE_NOOP("brulion::cli", "Words to find"), true, true),
             option(u"limit"_s, ArgType::Int,
                    QT_TRANSLATE_NOOP("brulion::cli", "At most this many hits (default 20)"))}));
    out.append(make(
            u"tags"_s, QT_TRANSLATE_NOOP("brulion::cli", "Every tag, most used first, with the page it names"), tags,
            {option(u"prefix"_s, ArgType::String, QT_TRANSLATE_NOOP("brulion::cli", "Only tags starting with this"))}));
    out.append(make(
            u"tag"_s,
            QT_TRANSLATE_NOOP("brulion::cli", "One tag: the page it names, its sub-tags and the blocks carrying it"),
            tag,
            {positional(u"name"_s, ArgType::String, QT_TRANSLATE_NOOP("brulion::cli", "The tag, with or without #"),
                        true, true)}));
    out.append(make(u"backlinks"_s,
                    QT_TRANSLATE_NOOP("brulion::cli", "What names a page: links to it and tags naming it"), backlinks,
                    {ref}));
    out.append(make(u"links"_s, QT_TRANSLATE_NOOP("brulion::cli", "The links of a page and where they lead"), links,
                    {ref}));
    out.append(make(u"unlinked"_s,
                    QT_TRANSLATE_NOOP("brulion::cli", "Plain-text mentions of a page's title in other notes"), unlinked,
                    {ref}));
    out.append(
            make(u"calendar"_s,
                 QT_TRANSLATE_NOOP("brulion::cli", "Calendar entries: daily notes, task dates, reminders, done dates"),
                 calendar,
                 {option(u"from"_s, ArgType::Date, QT_TRANSLATE_NOOP("brulion::cli", "From this day (default today)")),
                  option(u"to"_s, ArgType::Date, QT_TRANSLATE_NOOP("brulion::cli", "To this day (default: a week)"))}));
}

} // namespace brulion::cli
