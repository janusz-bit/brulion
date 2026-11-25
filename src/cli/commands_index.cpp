// Reading the notebook through its index: info, query (a view too), base, search, tags, tag,
// backlinks, links, unlinked, calendar.
#include "commands_p.h"

#include "attachments/documenttext.h"
#include "attachments/attachmentsearch.h"
#include "attachments/manifest.h"
#include "attachments/searchfilters.h"
#include "index/query.h"
#include "index/vaultindex.h"
#include "markdown/journal.h"
#include "notebook/json.h"
#include "notebook/pages.h"
#include "notebook/views.h"
#include "ai/localfiles.h"
#include "reminders/calendar.h"
#include "semantic/fusion.h"
#include "semantic/models.h"
#include "semantic/notes.h"
#include "semantic/semanticrunner.h"
#include "singleinstance.h"
#include "text/text.h"
#include "util/paths.h"
#include "util/unicode.h"
#include "vault/vault.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QStandardPaths>

#include <algorithm>

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

/// How the index was loaded (notebook::IndexLoader::Stats).
QJsonObject indexJson(const notebook::IndexLoader::Stats &s)
{
    return QJsonObject{{u"notes"_s, qint64(s.total - s.attachments)},
                       {u"attachments"_s, qint64(s.attachments)},
                       {u"fromCache"_s, qint64(s.fromCache)},
                       {u"parsed"_s, qint64(s.parsed)},
                       {u"workers"_s, s.workers},
                       {u"ms"_s, s.elapsedMs},
                       {u"cache"_s, s.cache}};
}

Result info(Call &call)
{
    notebook::Notebook *nb = call.nb();
    if (!nb)
        return call.failed();
    const Vault &v = nb->vault();
    const VaultIndex &index = nb->index();
    const int journals = int(index.journals().size());
    const QList<TaskRef> tasks = index.tasks();
    const int openTasks = int(std::ranges::count_if(tasks, [](const TaskRef &t) {
        return t.task.status == TaskStatus::Todo || t.task.status == TaskStatus::Doing;
    }));
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
                  {u"configFromObsidian"_s, QJsonArray::fromStringList(v.configAdoptedFromObsidian())},
                  {u"counts"_s, QJsonObject{{u"pages"_s, int(index.paths().size()) - journals},
                                            {u"journals"_s, journals},
                                            {u"attachments"_s, int(index.attachments().size())},
                                            {u"openTasks"_s, openTasks},
                                            {u"tags"_s, int(index.tags().size())}}},
                  {u"scan"_s, scanJson},
                  {u"index"_s, indexJson(nb->indexStats())},
                  {u"gui"_s, QJsonObject{{u"running"_s, app.isPrimaryRunning()}, {u"sameNotebook"_s, appNotebook}}}};
    return call.ok(o);
}

notebook::ViewRequest viewRequest(Call &call, const VaultPath &currentPath)
{
    notebook::ViewRequest r;
    r.currentPath = currentPath;
    r.now = call.ctx.now();
    r.weekStart = call.vault()->config().weekStartsOn;
    r.newPagesFolder = call.vault()->config().newPagesFolder;
    const Vault *vault = call.vault();
    r.stat = [vault](const VaultPath &p) { return vault->stat(p); };
    if (call.has(u"month"_s))
        r.month = call.date(call.str(u"month"_s)).value_or(QDate());
    return r;
}

/// An Obsidian base: a .base file or the first ```base block of a page (docs/SYNTAX.md §10).
Result base(Call &call)
{
    VaultIndex *index = call.index();
    if (!index)
        return call.failed();
    const QString ref = call.str(u"ref"_s);
    VaultPath path;
    QString yaml;
    if (ref.endsWith(u".base"_s, Qt::CaseInsensitive)) {
        const std::optional<VaultPath> p = call.vaultPath(ref);
        if (!p)
            return call.failed();
        path = *p;
        bool ok = false;
        yaml = call.vault()->readText(path, &ok);
        if (!ok)
            return call.fail(ExitNotFound, u"not_found"_s, tr("No such base: %1").arg(ref));
    } else {
        const auto page = call.page(ref, true);
        if (!page)
            return call.failed();
        path = page->path;
        const ParsedNote *note = index->note(path);
        const auto it = note ? std::ranges::find_if(
                                       note->blocks,
                                       [](const Block &b) { return b.kind == BlockKind::Code && b.lang == u"base"; })
                             : QList<Block>::const_iterator();
        if (!note || it == note->blocks.cend())
            return call.fail(ExitNotFound, u"not_found"_s, tr("The page has no ```base block: %1").arg(ref));
        // The block's own lines without its fences.
        QStringList lines = it->raw.toString().split(u'\n');
        if (!lines.isEmpty())
            lines.removeFirst();
        if (!lines.isEmpty() && lines.last().trimmed().startsWith(u"```"_s))
            lines.removeLast();
        yaml = lines.join(u'\n');
    }
    notebook::ViewRequest request = viewRequest(call, path);
    if (call.has(u"view"_s)) {
        bool number = false;
        const int n = call.str(u"view"_s).toInt(&number);
        if (number && n >= 1)
            request.view = n - 1;
        else
            request.viewName = call.str(u"view"_s);
    }
    const QJsonObject view = notebook::runView(*index, u"base"_s, yaml, request);
    if (!view.value(u"ok"_s).toBool())
        return call.fail(ExitParse, u"base_error"_s, view.value(u"error"_s).toString(), {{u"path"_s, path}});
    QString text = notebook::viewText(view);
    for (const QJsonValue &p : view.value(u"problems"_s).toArray())
        call.warn(u"base_problem"_s, p.toString());
    return call.ok(view, text);
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
    if (std::get<Query>(parsed).view != QueryView::None) {
        // A view (docs/SYNTAX.md §10 "Views"): its columns and cells, as the app's block shows them.
        const QJsonObject view = notebook::runView(*index, u"query"_s, source, viewRequest(call, options.currentPath));
        if (!view.value(u"ok"_s).toBool())
            return call.fail(ExitParse, u"query_error"_s, view.value(u"error"_s).toString(), {{u"query"_s, source}});
        return call.ok(view, notebook::viewText(view));
    }
    QJsonObject result = notebook::queryResultJson(*index, runQuery(std::get<Query>(parsed), *index, call.ctx.today(),
                                                                    call.vault()->config().weekStartsOn, options));
    addRefs(call, result);
    QString text;
    for (const QJsonArray items = result.value(u"items"_s).toArray(); const auto &v : items) {
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

/// The text of the notebook's PDFs, documents and pictures as the app read it on this device (docs/UI.md
/// "Searching attachments": <app cache>/attachment-text — the notebook's manifest and the records it
/// names), searched read-only: the command line reads no PDF and recognizes no text itself.
QJsonArray attachmentHits(Call &call, const QString &text, int limit, attachments::Scopes kinds, QString &out)
{
    QJsonArray hits;
    Vault *vault = call.vault();
    const QString cache = QStandardPaths::writableLocation(QStandardPaths::CacheLocation);
    if (!vault || cache.isEmpty())
        return hits;
    const QDir dir(QDir(cache).filePath(u"attachment-text"_s));
    QFile manifestFile(dir.filePath(attachments::manifestFileName(vault->id())));
    if (!manifestFile.open(QIODevice::ReadOnly))
        return hits;
    const std::optional<attachments::Manifest> manifest = attachments::decodeManifest(manifestFile.readAll());
    if (!manifest)
        return hits;
    attachments::AttachmentSearch index;
    QHash<QString, attachments::Record> records;
    for (auto it = manifest->cbegin(); it != manifest->cend(); ++it) {
        // A file changed since the app read it is not searched by its old text.
        const std::optional<FileStat> st = vault->stat(it.key());
        if (!st || st->size != it->size || st->mtime != it->mtime)
            continue;
        QFile f(dir.filePath(attachments::recordFileName(it->hash)));
        if (!f.open(QIODevice::ReadOnly))
            continue;
        std::optional<attachments::Record> record = attachments::decodeRecord(f.readAll());
        if (!record)
            continue;
        index.set(attachments::AttachmentSearch::prepare(it.key(), *record));
        records.insert(it.key(), std::move(*record));
    }
    const auto pageText = [&records](const QString &path, int page) -> std::optional<attachments::Page> {
        const auto it = records.constFind(path);
        if (it == records.constEnd() || page < 1 || page > it->pages.size())
            return std::nullopt;
        return it->pages.at(page - 1);
    };
    for (const attachments::AttachmentSearch::Hit &hit : index.search(text, limit, kinds, pageText)) {
        const bool pdf = hit.kind == attachments::Kind::Pdf;
        hits.append(QJsonObject{{u"path"_s, hit.path},
                                {u"kind"_s, attachments::kindName(hit.kind)},
                                {u"page"_s, hit.page},
                                {u"pageCount"_s, hit.pageCount},
                                {u"matchedPages"_s, hit.matchedPages},
                                {u"ocr"_s, hit.ocr},
                                {u"excerpt"_s, hit.excerpt},
                                {u"find"_s, hit.find}});
        out += hit.path + (pdf ? u"#page="_s + QString::number(hit.page) : QString()) + u"  "_s
               + hit.excerpt.simplified() + u'\n';
    }
    return hits;
}

/// A hit of search, found by its words or (search --semantic) by meaning alone.
struct FoundNote {
    SearchHit hit;
    bool byMeaning = false;
};

/// `found` (the full-text hits, best first) fused with the notes close in meaning to `text`, as the
/// app's search does it (semantic::hybridSearch over the vectors the app keeps on this device for the
/// notebook, docs/SEMANTIC.md): `found` as it is, with `info` saying why, when it cannot be — no
/// plugin in this build, the model not on this device, the notebook not read by meaning here yet. A
/// note changed since the app read it is found by its words alone.
QList<FoundNote> byMeaning(Call &call, const VaultIndex &index, const QString &text, const QList<SearchHit> &found,
                           int want, QJsonObject *info)
{
    QList<FoundNote> words;
    for (const SearchHit &hit : found)
        words.append({hit, false});
    const auto unavailable = [&](const QString &code, const QString &message) {
        info->insert(u"used"_s, false);
        info->insert(u"reason"_s, code);
        call.warn(code, message);
        return words;
    };
    const ai::LocalModel *model = semantic::semanticModel(QString());
    const QStringList dirs = pluginDirs();
    if (!model || !semantic::hasSemanticRunner(dirs))
        return unavailable(u"semantic_unavailable"_s, tr("This build searches by words only: it has no search by meaning."));
    const ai::LocalModelFiles files(QDir(call.ctx.appDataDir()).filePath(u"models/semantic"_s));
    if (!files.has(*model))
        return unavailable(u"semantic_no_model"_s,
                           tr("The model of search by meaning is not on this device: Brulion downloads it in "
                              "Settings → Search → Search by meaning."));
    const Vault *vault = call.vault();
    const QString cache = QStandardPaths::writableLocation(QStandardPaths::CacheLocation);
    QFile kept(QDir(cache).filePath(u"semantic/"_s + semantic::indexFileName(vault ? vault->id() : QString(), model->id)));
    std::optional<semantic::VectorIndex> vectors;
    if (vault && !cache.isEmpty() && kept.open(QIODevice::ReadOnly))
        vectors = semantic::VectorIndex::load(kept.readAll(), model->id);
    if (!vectors || vectors->size() == 0)
        return unavailable(u"semantic_not_indexed"_s,
                           tr("This notebook has not been read by meaning on this device yet: Brulion does it in "
                              "the background with search by meaning on."));
    // Notes changed since the app read them (or gone): by their words alone.
    int stale = 0;
    for (const QString &id : vectors->ids()) {
        const ParsedNote *note = index.note(id);
        // (The title as the app takes it: shell/semanticsearch.cpp.)
        const QString title = note && !note->title.isEmpty() ? note->title : paths::pageTitle(id);
        if (!note || semantic::noteKey(title, index.content(id)) != vectors->key(id)) {
            vectors->remove(id);
            ++stale;
        }
    }
    semantic::SemanticRunner::Options options;
    options.modelPath = files.pathOf(model->file);
    QString error;
    const std::unique_ptr<semantic::SemanticRunner> runner = semantic::loadSemanticRunner(dirs, options, &error);
    const semantic::Vector query = runner ? runner->embedQuery(text) : semantic::Vector();
    if (query.isEmpty())
        return unavailable(u"semantic_failed"_s, tr("Search by meaning failed: %1").arg(error));
    const semantic::Vector queryMean = semantic::queryMeanOf(model->id);
    if (!queryMean.isEmpty() && vectors->chunkCount() > 0)
        vectors->setCalibration(vectors->estimateCalibration(queryMean));

    QList<semantic::ScoredId> lexical;
    QHash<QString, SearchHit> byPath;
    for (const SearchHit &hit : found) {
        lexical.append({hit.path, hit.score});
        byPath.insert(hit.path, hit);
    }
    QList<FoundNote> out;
    for (const semantic::HybridHit &h :
         semantic::hybridSearch(lexical, *vectors, query, vectors->calibration().appliesTo(text), want)) {
        if (const auto it = byPath.constFind(h.id); it != byPath.cend()) {
            SearchHit hit = it.value();
            hit.score = h.score;
            out.append({hit, false});
            continue;
        }
        const ParsedNote *note = index.note(h.id);
        if (!note)
            continue;
        SearchHit hit;
        hit.path = h.id;
        hit.title = note->title.isEmpty() ? paths::pageTitle(h.id) : note->title;
        hit.line = h.line;
        hit.score = h.score;
        // Where its closest chunk starts.
        hit.excerpt = semantic::passageExcerpt(index.content(h.id), h.line, 200);
        out.append({hit, true});
    }
    info->insert(u"used"_s, true);
    info->insert(u"notes"_s, qint64(vectors->size()));
    info->insert(u"changedSince"_s, stale);
    return out;
}

Result search(Call &call)
{
    VaultIndex *index = call.index();
    if (!index)
        return call.failed();
    const QString query = call.str(u"text"_s).trimmed();
    if (query.isEmpty())
        return call.fail(ExitUsage, u"text_required"_s, tr("Search for what?"));
    // The search field's filters (in:pdf, w:dziennik …): their words are not searched.
    const attachments::SearchFilters filters = attachments::parseSearchFilters(query);
    const QString text = filters.filtered ? filters.text : query;
    if (text.isEmpty())
        return call.fail(ExitUsage, u"text_required"_s, tr("Search for what?"));
    const int limit = std::clamp(call.integer(u"limit"_s, 20), 1, 500);
    QJsonArray hits;
    QString out;
    QJsonObject meaning;
    if (filters.scopes & attachments::NoteScopes) {
        // (Asked for more when only some notes may come.)
        const bool some = (filters.scopes & attachments::NoteScopes) != attachments::NoteScopes;
        const int want = some ? std::min(1000, limit * 4) : limit;
        // By meaning too (--semantic): the words' hits fused with the notes close in meaning — more of
        // the words' hits first, as the app's search takes them, for the fusion to choose from.
        const bool semantic = call.flag(u"semantic"_s);
        const QList<SearchHit> found = index->search(text, semantic ? std::max(want, 100) : want);
        const QList<FoundNote> notes = semantic ? byMeaning(call, *index, text, found, std::max(want, 100), &meaning)
                                                : QList<FoundNote>();
        QList<FoundNote> listedNotes = notes;
        if (!semantic) {
            for (const SearchHit &hit : found)
                listedNotes.append({hit, false});
        }
        int listed = 0;
        for (const FoundNote &f : std::as_const(listedNotes)) {
            const SearchHit &hit = f.hit;
            const ParsedNote *note = index->note(hit.path);
            const bool journal = note && note->journalDate;
            if (!filters.scopes.testFlag(journal ? attachments::Scope::Journals : attachments::Scope::Pages))
                continue;
            if (listed++ >= limit)
                break;
            QJsonObject o = notebook::searchHitJson(hit, index->config());
            if (hit.line >= 0)
                o.insert(u"ref"_s, Call::lineRef(hit.path, noteText(call, hit.path), hit.line));
            if (semantic)
                o.insert(u"byMeaning"_s, f.byMeaning);
            hits.append(o);
            out += hit.path + (hit.line >= 0 ? u':' + QString::number(hit.line + 1) : QString()) + u"  "_s
                   + (f.byMeaning ? u"≈ "_s : QString()) + hit.excerpt.simplified() + u'\n';
        }
    }
    QJsonObject result{{u"query"_s, query}, {u"hits"_s, hits}};
    if (!meaning.isEmpty())
        result.insert(u"semantic"_s, meaning);
    // Attachments when asked for (--attachments) or named by a filter (in:pdf).
    const attachments::Scopes kinds = filters.scopes & attachments::AttachmentScopes;
    if (kinds && (call.flag(u"attachments"_s) || (filters.filtered && kinds == filters.scopes)))
        result.insert(u"attachments"_s, attachmentHits(call, text, limit, kinds, out));
    return call.ok(result, out);
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
    for (const QList<TagCount> all = index->tags(); const TagCount &t : all) {
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
    const QString key = unicode::nfc(name).toLower();
    QString display = name;
    int count = 0;
    QStringList subTags;
    for (const QList<TagCount> all = index->tags(); const TagCount &t : all) {
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
    for (const QJsonArray items = blocks.value(u"items"_s).toArray(); const auto &v : items) {
        const QJsonObject item = v.toObject();
        text += item.value(u"text"_s).toString().simplified() + u"  "_s + item.value(u"ref"_s).toString() + u'\n';
    }
    return call.ok(o, text);
}

Result backlinks(Call &call)
{
    const auto ref = call.page(call.str(u"ref"_s), false);
    if (!ref)
        return call.failed();
    const QJsonArray items = backlinksJson(call, ref->path);
    QString text;
    for (const auto &v : items) {
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
    for (const QList<ResolvedLink> found = index->outgoingLinks(ref->path); const ResolvedLink &l : found) {
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
        text += l.link.target + u"  →  "_s + (l.target.isEmpty() ? tr("(missing)") : l.target) + u'\n';
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
    for (const QList<UnlinkedRef> found = call.index()->unlinkedReferences(ref->path); const UnlinkedRef &u : found) {
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
    for (const QList<CalendarItem> items = calendarItems(*index, *from, *to); const CalendarItem &item : items) {
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
    out.append(make(
            u"query"_s,
            QT_TRANSLATE_NOOP("brulion::cli", "Run a query (docs/SYNTAX.md §10), the same results as a ```query block"),
            query,
            {positional(u"query"_s, ArgType::String,
                        QT_TRANSLATE_NOOP("brulion::cli",
                                          "type:tasks status:open due:<=+7d sort:due — keys also in Polish"),
                        true, true),
             option(u"page"_s, ArgType::Ref, QT_TRANSLATE_NOOP("brulion::cli", "The page page:this / ref:this mean")),
             option(u"month"_s, ArgType::Date,
                    QT_TRANSLATE_NOOP("brulion::cli", "view:calendar: the month shown (any day of it)"))}));
    out.append(
            make(u"base"_s,
                 QT_TRANSLATE_NOOP("brulion::cli",
                                   "Show an Obsidian base (a .base file, or a page's ```base block) as a table"),
                 base,
                 {positional(u"ref"_s, ArgType::String,
                             QT_TRANSLATE_NOOP("brulion::cli", "The .base file, or the page with the ```base block")),
                  option(u"view"_s, ArgType::String,
                         QT_TRANSLATE_NOOP("brulion::cli", "The view: its name or number (default the first)")),
                  option(u"month"_s, ArgType::Date,
                         QT_TRANSLATE_NOOP("brulion::cli", "A calendar view: the month shown (any day of it)"))}));
    out.append(make(
            u"search"_s,
            QT_TRANSLATE_NOOP("brulion::cli", "Full-text search, diacritic-insensitive (\"zolw\" finds \"żółw\")"),
            search,
            {positional(u"text"_s, ArgType::String,
                        QT_TRANSLATE_NOOP("brulion::cli", "Words to find (in:pdf, in:documents, in:pictures, "
                                                          "in:notes, in:journals narrow it)"),
                        true, true),
             option(u"limit"_s, ArgType::Int, QT_TRANSLATE_NOOP("brulion::cli", "At most this many hits (default 20)")),
             option(u"attachments"_s, ArgType::Bool,
                    QT_TRANSLATE_NOOP("brulion::cli", "Also the text of PDFs, documents and pictures the app "
                                                      "has read on this device")),
             option(u"semantic"_s, ArgType::Bool,
                    QT_TRANSLATE_NOOP("brulion::cli", "Also by meaning, as the app's search: the notes the app has "
                                                      "read by meaning on this device (its model and plugin)"))}));
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
