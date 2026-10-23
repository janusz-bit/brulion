// Commands on pages and folders: page list/read/create/append/write/rename/move/delete, folder
// list/create/move/rename/delete.
#include "commands_p.h"

#include "index/vaultindex.h"
#include "markdown/journal.h"
#include "markdown/notemode.h"
#include "markdown/parse.h"
#include "notebook/journals.h"
#include "notebook/json.h"
#include "notebook/pages.h"
#include "text/text.h"
#include "util/paths.h"
#include "vault/vault.h"

#include <QDateTime>
#include <QFileInfo>

using namespace Qt::StringLiterals;

namespace brulion::cli {

namespace {

QString isoOf(qint64 mtime)
{
    return mtime > 0 ? QDateTime::fromMSecsSinceEpoch(mtime).toString(u"yyyy-MM-dd HH:mm"_s) : QString();
}

QJsonObject relocationJson(const VaultPath &from, const notebook::RelocationImpact &impact,
                           const notebook::Relocation *moved)
{
    QJsonObject o{{u"from"_s, from},
                  {u"path"_s, moved ? moved->path : impact.path},
                  {u"links"_s, impact.links},
                  {u"tags"_s, impact.tags},
                  {u"queries"_s, impact.queries},
                  {u"notes"_s, impact.notes},
                  {u"settings"_s, QJsonArray::fromStringList(impact.specials)}};
    if (!moved) {
        // The dry run: every note that would be rewritten, as a diff (a moved one from its old
        // path to its new one).
        QJsonArray changes;
        QString diff;
        for (const notebook::PlannedRewrite &r : impact.rewrites) {
            const QString d = unifiedDiff(r.before, r.after, r.path, r.newPath == r.path ? QString() : r.newPath);
            if (d.isEmpty())
                continue;
            changes.append(QJsonObject{{u"path"_s, r.path}, {u"newPath"_s, r.newPath}, {u"diff"_s, d}});
            diff += d;
        }
        o.insert(u"dryRun"_s, true);
        o.insert(u"changes"_s, changes);
        o.insert(u"diff"_s, diff);
        return o;
    }
    o.insert(u"rewritten"_s, QJsonArray::fromStringList(moved->rewritten));
    QJsonArray rebased;
    for (const notebook::RebasedSetting &r : moved->rebased)
        rebased.append(QJsonObject{{u"setting"_s, r.key}, {u"value"_s, r.value}});
    o.insert(u"rebased"_s, rebased);
    if (!moved->configError.isEmpty())
        o.insert(u"configError"_s, moved->configError);
    return o;
}

/// Move or rename `from` to `to` (a relocation the core plans), dry-run aware.
Result relocate(Call &call, const VaultPath &from, const std::function<notebook::Relocation()> &run,
                const VaultPath &to)
{
    VaultIndex *index = call.index(); // every linking note known
    if (!index || !call.lock())
        return call.failed();
    const notebook::RelocationImpact impact = notebook::relocationImpact(*call.vault(), index, from, to);
    if (!impact.ok)
        return failWith(problemError(impact.problem));
    if (call.dryRun())
        return call.ok(relocationJson(from, impact, nullptr));
    const notebook::Relocation moved = run();
    if (moved.path.isEmpty())
        return failWith(problemError(moved.problem));
    if (!moved.configError.isEmpty())
        call.warn(u"settings_not_saved"_s, tr("The notebook settings could not be updated: %1").arg(moved.configError));
    return call.ok(relocationJson(from, impact, &moved));
}

/// A folder argument that must be an existing folder ("." or "/" = the notebook root when allowed).
std::optional<VaultPath> existingFolder(Call &call, const QString &raw, bool rootAllowed)
{
    const QString t = raw.trimmed();
    if (rootAllowed && (t == u"." || t == u"/" || t.isEmpty()))
        return VaultPath();
    const auto path = call.vaultPath(t);
    if (!path)
        return std::nullopt;
    Vault *v = call.vault();
    if (!v)
        return std::nullopt;
    const VaultPath spelled = v->existingCase(*path);
    const auto st = v->stat(spelled);
    if (!st) {
        call.setError(ExitNotFound, u"folder_not_found"_s, tr("No folder %1").arg(*path), {{u"path"_s, *path}});
        return std::nullopt;
    }
    if (!st->isDir) {
        call.setError(ExitUsage, u"not_a_folder"_s, tr("%1 is a file, not a folder").arg(*path), {{u"path"_s, *path}});
        return std::nullopt;
    }
    return spelled;
}

QJsonObject folderJson(const Vault &vault, const VaultPath &path)
{
    const notebook::FolderInfo info = notebook::folderInfo(vault, path);
    QJsonArray specials;
    for (const notebook::SpecialPath &s : info.specials)
        specials.append(QJsonObject{{u"special"_s, s.special}, {u"setting"_s, s.key}, {u"path"_s, s.path}});
    return {{u"path"_s, path},
            {u"name"_s, paths::basename(path)},
            {u"pages"_s, info.pages},
            {u"attachments"_s, info.attachments},
            {u"folders"_s, info.folders},
            {u"specials"_s, specials}};
}

// ---- page -------------------------------------------------------------------------------------

Result pageList(Call &call)
{
    Vault *v = call.vault();
    if (!v)
        return call.failed();
    QString folder;
    if (call.has(u"folder"_s)) {
        const auto f = existingFolder(call, call.str(u"folder"_s), true);
        if (!f)
            return call.failed();
        folder = *f;
    }
    const VaultConfig config = v->config();
    QList<FileStat> files = v->markdownFiles();
    files.removeIf([&](const FileStat &f) { return !folder.isEmpty() && !f.path.startsWith(folder + u'/'); });
    const QString sort = call.has(u"sort"_s) ? call.str(u"sort"_s) : u"title"_s;
    std::stable_sort(files.begin(), files.end(), [&](const FileStat &a, const FileStat &b) {
        if (sort == u"modified")
            return a.mtime > b.mtime;
        if (sort == u"path")
            return text::compareNames(a.path, b.path) < 0;
        const int c = text::compareNames(paths::pageTitle(a.path), paths::pageTitle(b.path));
        return c != 0 ? c < 0 : a.path < b.path;
    });
    const int limit = call.integer(u"limit"_s, 0);
    QJsonArray out;
    QString text;
    for (const FileStat &f : std::as_const(files)) {
        if (limit > 0 && out.size() >= limit)
            break;
        const auto journal = markdown::journalDateFromPath(f.path, config);
        const QString kind = paths::isConflictCopy(f.path) ? u"conflict"_s : journal ? u"journal"_s : u"page"_s;
        out.append(QJsonObject{{u"path"_s, f.path},
                               {u"title"_s, paths::pageTitle(f.path)},
                               {u"folder"_s, paths::dirname(f.path)},
                               {u"kind"_s, kind},
                               {u"journal"_s, journal ? journal->toString(Qt::ISODate) : QString()},
                               {u"size"_s, f.size},
                               {u"modified"_s, isoOf(f.mtime)}});
        text += f.path + u'\n';
    }
    return call.ok(QJsonObject{{u"total"_s, files.size()}, {u"pages"_s, out}}, text);
}

Result pageRead(Call &call)
{
    const auto ref = call.page(call.str(u"ref"_s), true);
    if (!ref)
        return call.failed();
    Vault &v = *call.vault();
    const notebook::Notebook::Current current = call.nb()->read(ref->path);
    const VaultConfig config = v.config();
    QJsonObject o{{u"path"_s, ref->path},
                  {u"title"_s, paths::pageTitle(ref->path)},
                  {u"exists"_s, current.exists},
                  {u"journal"_s, ref->journal ? ref->journal->toString(Qt::ISODate) : QString()}};
    if (!current.exists) {
        if (!ref->journal)
            return call.fail(ExitNotFound, u"page_not_found"_s, tr("No page %1").arg(ref->path),
                             {{u"path"_s, ref->path}});
        o.insert(u"content"_s, QString());
        o.insert(u"hash"_s, QString());
        return call.ok(o, QString());
    }
    const ParsedNote note = markdown::parseNote(ref->path, current.text, config);
    o.insert(u"hash"_s, current.hash);
    o.insert(u"size"_s, current.bytes.size());
    if (const auto st = v.stat(ref->path))
        o.insert(u"modified"_s, isoOf(st->mtime));
    o.insert(u"mode"_s, markdown::noteModeName(
                                markdown::resolveNoteMode(current.text, note.journalDate.has_value(), config).mode));
    o.insert(u"content"_s, current.text);
    o.insert(u"frontmatter"_s, QJsonObject::fromVariantMap(note.frontmatter));
    o.insert(u"tags"_s, QJsonArray::fromStringList(note.tags));
    o.insert(u"aliases"_s, QJsonArray::fromStringList(note.aliases));
    if (!current.validUtf8)
        call.warn(u"invalid_utf8"_s, tr("The page is not valid UTF-8: it cannot be changed from the command line"));
    if (ref->item) {
        const notebook::ResolvedLine line = notebook::resolve(current.text, *ref->item, config);
        static const char *const names[] = {"ok", "moved", "stale", "notFound"};
        QJsonObject focus{{u"status"_s, QString::fromLatin1(names[line.status])}, {u"line"_s, line.line}};
        if (line.line >= 0)
            focus.insert(u"ref"_s, Call::lineRef(ref->path, current.text, line.line));
        o.insert(u"focus"_s, focus);
    }
    if (call.flag(u"blocks"_s)) {
        QJsonArray blocks;
        for (const Block &b : note.blocks) {
            QJsonObject bo = notebook::blockJson(b);
            bo.insert(u"ref"_s, b.id.isEmpty() ? Call::lineRef(ref->path, current.text, b.line)
                                               : notebook::formatBlockRef(ref->path, b.id));
            blocks.append(bo);
        }
        o.insert(u"blocks"_s, blocks);
    }
    if (call.flag(u"backlinks"_s))
        o.insert(u"backlinks"_s, backlinksJson(call, ref->path));
    return call.ok(o, current.text);
}

Result pageCreate(Call &call)
{
    Vault *v = call.vault();
    if (!v)
        return call.failed();
    const QString title = call.str(u"title"_s);
    QString folder; // null: the notebook's folder for new pages
    if (call.has(u"folder"_s)) {
        const QString f = call.str(u"folder"_s).trimmed();
        if (f == u"." || f == u"/") {
            folder = u""_s;
        } else {
            const auto p = call.vaultPath(f);
            if (!p)
                return call.failed();
            folder = *p;
        }
    }
    std::optional<NoteMode> mode;
    if (call.has(u"mode"_s))
        mode = markdown::parseNoteMode(call.str(u"mode"_s));
    const QString content = call.text(u"content"_s);
    const notebook::CreatedPage planned = notebook::plannedPage(*v, title, folder);
    if (planned.path.isEmpty())
        return failWith(problemError(planned.problem));
    if (planned.existing) {
        call.warn(u"existing_page"_s, tr("%1 is the page %2 (names that differ only in letter case are one page)")
                                              .arg(planned.wanted, planned.path));
        return call.ok(QJsonObject{{u"path"_s, planned.path}, {u"created"_s, false}, {u"existing"_s, true}});
    }
    if (call.dryRun())
        return call.ok(
                QJsonObject{{u"path"_s, planned.path},
                            {u"created"_s, false},
                            {u"dryRun"_s, true},
                            {u"diff"_s, unifiedDiff(QString(), notebook::newPageText(*v, planned.path, mode, content),
                                                    planned.path)}});
    if (!call.lock())
        return call.failed();
    const notebook::CreatedPage page = notebook::createPage(*v, title, folder, mode, content, ChangeSource::User);
    if (page.path.isEmpty())
        return failWith(problemError(page.problem));
    if (page.path != planned.path)
        call.warn(u"name_taken"_s, tr("%1 exists: the new page is %2").arg(planned.path, page.path));
    return call.ok(QJsonObject{{u"path"_s, page.path},
                               {u"created"_s, !page.existing},
                               {u"existing"_s, page.existing},
                               {u"hash"_s, call.nb()->read(page.path).hash}});
}

Result pageAppend(Call &call)
{
    const QString block = notebook::blockFromText(call.text(u"text"_s));
    if (block.isEmpty())
        return call.fail(ExitUsage, u"text_required"_s, tr("Nothing to append: give the text (or --stdin)"));
    const auto ref = call.page(call.str(u"ref"_s), false);
    if (!ref)
        return call.failed();
    if (!ref->exists && !ref->journal)
        return call.fail(ExitNotFound, u"page_not_found"_s, tr("No page %1 (page create makes one)").arg(ref->path),
                         {{u"path"_s, ref->path}});
    const QString under = call.str(u"under"_s);
    int inserted = -1;
    const notebook::Notebook::WriteOptions options = creatingJournal(call, ref->path);
    const auto written = call.edit(
            ref->path,
            [&](const QString &current, bool, QString *next) {
                if (under.trimmed().isEmpty()) {
                    *next = notebook::appendBlock(current, block);
                    inserted = appendedLine(*next, block);
                    return true;
                }
                const auto placed = insertUnder(call, current, under, block, &inserted);
                if (!placed)
                    return false;
                *next = *placed;
                return true;
            },
            options);
    if (!written)
        return call.failed();
    QJsonObject o = call.writeJson(*written);
    o.insert(u"line"_s, inserted);
    o.insert(u"ref"_s, Call::lineRef(ref->path, written->after, inserted));
    return call.ok(o);
}

Result pageWrite(Call &call)
{
    const auto ref = call.page(call.str(u"ref"_s), true);
    if (!ref)
        return call.failed();
    // An empty --content (or MCP content: "") is a text too: the page becomes empty.
    if (!call.args.contains(u"content"_s) && !call.ctx.globals.stdinText)
        return call.fail(ExitUsage, u"text_required"_s, tr("Give the new text with --content or --stdin"));
    const QString content = call.text(u"content"_s);
    notebook::Notebook::WriteOptions options;
    options.expectHash = call.str(u"expectHash"_s).trimmed().toLower();
    const auto written = call.edit(
            ref->path,
            [&](const QString &, bool, QString *next) {
                *next = content;
                return true;
            },
            options);
    if (!written)
        return call.failed();
    return call.ok(call.writeJson(*written));
}

Result pageRename(Call &call)
{
    const auto ref = call.page(call.str(u"ref"_s), true);
    if (!ref)
        return call.failed();
    const QString title = call.str(u"title"_s);
    const VaultPath to = notebook::renamedPath(ref->path, title, false, false);
    if (to.isEmpty())
        return failWith(problemError({notebook::PathProblem::NoName, {}, {}}));
    return relocate(
            call, ref->path,
            [&] {
                return notebook::renamePage(*call.vault(), call.index(), ref->path, title, false, ChangeSource::User);
            },
            to);
}

Result pageMove(Call &call)
{
    const auto ref = call.page(call.str(u"ref"_s), true);
    if (!ref)
        return call.failed();
    const QString target = call.str(u"folder"_s).trimmed();
    VaultPath folder;
    if (!(target == u"." || target == u"/")) {
        const auto p = call.vaultPath(target);
        if (!p)
            return call.failed();
        folder = *p;
    }
    const QString name = paths::basename(ref->path);
    return relocate(
            call, ref->path,
            [&] { return notebook::movePath(*call.vault(), call.index(), ref->path, folder, ChangeSource::User); },
            folder.isEmpty() ? name : folder + u'/' + name);
}

Result deletePath(Call &call, const VaultPath &path)
{
    if (call.dryRun()) {
        // What would go to the trash: the files (a folder: everything in it), a page as a diff.
        Vault &v = *call.vault();
        const auto st = v.stat(path);
        const bool folder = st && st->isDir;
        QStringList files;
        for (const FileStat &f : v.files()) {
            if (f.path == path || (folder && f.path.startsWith(path + u'/')))
                files.append(f.path);
        }
        QJsonObject o{{u"path"_s, path},
                      {u"folder"_s, folder},
                      {u"files"_s, QJsonArray::fromStringList(files)},
                      {u"dryRun"_s, true}};
        if (!folder && paths::isMarkdown(path))
            o.insert(u"diff"_s, unifiedDiff(call.nb()->read(path).text, QString(), path, u"/dev/null"_s));
        return call.ok(o);
    }
    if (!call.lock())
        return call.failed();
    const notebook::DeletedPath deleted = notebook::deletePath(*call.vault(), path, ChangeSource::User);
    if (deleted.trashPath.isEmpty())
        return failWith(problemError(deleted.problem));
    return call.ok(QJsonObject{{u"path"_s, path}, {u"trashPath"_s, deleted.trashPath}, {u"folder"_s, deleted.folder}});
}

Result pageDelete(Call &call)
{
    const auto ref = call.page(call.str(u"ref"_s), true);
    if (!ref)
        return call.failed();
    return deletePath(call, ref->path);
}

// ---- folder -----------------------------------------------------------------------------------

Result folderList(Call &call)
{
    Vault *v = call.vault();
    if (!v)
        return call.failed();
    QStringList folders = notebook::folderPaths(*v);
    if (call.has(u"folder"_s)) {
        const auto parent = existingFolder(call, call.str(u"folder"_s), true);
        if (!parent)
            return call.failed();
        const QString prefix = parent->isEmpty() ? QString() : *parent + u'/';
        folders.removeIf(
                [&](const QString &f) { return !f.startsWith(prefix) || f.mid(prefix.size()).contains(u'/'); });
    }
    QJsonArray out;
    for (const QString &f : std::as_const(folders))
        out.append(folderJson(*v, f));
    return call.ok(QJsonObject{{u"folders"_s, out}}, folders.join(u'\n'));
}

Result folderCreate(Call &call)
{
    Vault *v = call.vault();
    if (!v)
        return call.failed();
    const auto path = call.vaultPath(call.str(u"path"_s));
    if (!path)
        return call.failed();
    if (call.dryRun())
        return call.ok(QJsonObject{{u"path"_s, *path}, {u"dryRun"_s, true}});
    if (!call.lock())
        return call.failed();
    const bool existed = v->stat(v->existingCase(*path)).has_value();
    const notebook::CreatedFolder folder = notebook::createFolder(*v, *path);
    if (folder.path.isEmpty())
        return failWith(problemError(folder.problem));
    return call.ok(QJsonObject{{u"path"_s, folder.path}, {u"created"_s, !existed}});
}

Result folderMove(Call &call)
{
    const auto from = existingFolder(call, call.str(u"path"_s), false);
    if (!from)
        return call.failed();
    const QString target = call.str(u"target"_s).trimmed();
    VaultPath folder;
    if (!(target == u"." || target == u"/")) {
        const auto p = call.vaultPath(target);
        if (!p)
            return call.failed();
        folder = *p;
    }
    const QString name = paths::basename(*from);
    return relocate(
            call, *from,
            [&] { return notebook::movePath(*call.vault(), call.index(), *from, folder, ChangeSource::User); },
            folder.isEmpty() ? name : folder + u'/' + name);
}

Result folderRename(Call &call)
{
    const auto from = existingFolder(call, call.str(u"path"_s), false);
    if (!from)
        return call.failed();
    const QString name = call.str(u"name"_s);
    if (const QString hidden = notebook::hiddenTypedSegment(name, true, false); !hidden.isEmpty())
        return failWith(problemError({notebook::PathProblem::HiddenFolder, hidden, {}}));
    const VaultPath to = notebook::renamedPath(*from, name, true, false);
    if (to.isEmpty())
        return failWith(problemError({notebook::PathProblem::NoName, {}, {}}));
    return relocate(
            call, *from,
            [&] { return notebook::renameFolder(*call.vault(), call.index(), *from, name, false, ChangeSource::User); },
            to);
}

Result folderDelete(Call &call)
{
    const auto path = existingFolder(call, call.str(u"path"_s), false);
    if (!path)
        return call.failed();
    return deletePath(call, *path);
}

Command command(const QString &id, const char *summary, Handler run, QList<Arg> args = {})
{
    Command c;
    c.id = id;
    c.summary = cliText(summary);
    c.run = std::move(run);
    c.args = std::move(args);
    return c;
}

} // namespace

void addPageCommands(QList<Command> &out)
{
    const Arg ref =
            positional(u"ref"_s, ArgType::Ref,
                       QT_TRANSLATE_NOOP("brulion::cli",
                                         "The page: a path (Projekty/Dom.md), a name as [[links]] read it (Dom), a "
                                         "date (today, wczoraj, 2026-10-15) or an item ref (Dom.md#L12@hash, ^id)"));
    Command c = command(u"page.list"_s,
                        QT_TRANSLATE_NOOP("brulion::cli", "List the pages (Markdown files) of the notebook"), pageList,
                        {option(u"folder"_s, ArgType::Path,
                                QT_TRANSLATE_NOOP("brulion::cli", "Only pages in this folder (and below)")),
                         choice(u"sort"_s, {u"title"_s, u"path"_s, u"modified"_s},
                                QT_TRANSLATE_NOOP("brulion::cli", "Order (default: title)")),
                         option(u"limit"_s, ArgType::Int, QT_TRANSLATE_NOOP("brulion::cli", "At most this many"))});
    out.append(c);
    c = command(u"page.read"_s,
                QT_TRANSLATE_NOOP("brulion::cli", "Read a page: its text, hash, frontmatter, tags, mode"), pageRead,
                {ref, flag(u"blocks"_s, QT_TRANSLATE_NOOP("brulion::cli", "Also every block with its ref")),
                 flag(u"backlinks"_s, QT_TRANSLATE_NOOP("brulion::cli", "Also the links and tags naming the page"))});
    c.help = cliText(QT_TRANSLATE_NOOP(
            "brulion::cli",
            "The `hash` is what page write --expect-hash needs. A missing daily note reads as exists: false."));
    out.append(c);
    c = command(u"page.create"_s,
                QT_TRANSLATE_NOOP("brulion::cli", "Create a page (a name differing only in letter case is that page)"),
                pageCreate,
                {positional(u"title"_s, ArgType::String,
                            QT_TRANSLATE_NOOP("brulion::cli", "Title; \"Projekty/Dom\" creates folders"), true, true),
                 option(u"folder"_s, ArgType::Path,
                        QT_TRANSLATE_NOOP("brulion::cli",
                                          "Folder (default: the notebook's folder for new pages; . = root)")),
                 choice(u"mode"_s, {u"outline"_s, u"document"_s},
                        QT_TRANSLATE_NOOP("brulion::cli", "Note mode (default: the notebook's)")),
                 stdinable(option(u"content"_s, ArgType::Text, QT_TRANSLATE_NOOP("brulion::cli", "Its text")))});
    c.writes = true;
    out.append(c);
    c = command(
            u"page.append"_s,
            QT_TRANSLATE_NOOP("brulion::cli", "Append a block to a page (a daily note is created from its template)"),
            pageAppend,
            {ref,
             stdinable(positional(u"text"_s, ArgType::Text,
                                  QT_TRANSLATE_NOOP("brulion::cli", "The block's text (more lines: continuation)"),
                                  true, true)),
             option(u"under"_s, ArgType::String,
                    QT_TRANSLATE_NOOP("brulion::cli", "Put it at the end of this heading's section (\"## Zakupy\") or "
                                                      "as the last child of a block (^id)"))});
    c.writes = true;
    out.append(c);
    c = command(
            u"page.write"_s, QT_TRANSLATE_NOOP("brulion::cli", "Replace a page's whole text (needs its current hash)"),
            pageWrite,
            {ref,
             stdinable(option(u"content"_s, ArgType::Text, QT_TRANSLATE_NOOP("brulion::cli", "The new text"), true)),
             option(u"expectHash"_s, ArgType::String,
                    QT_TRANSLATE_NOOP("brulion::cli", "The hash page read gave: refused when the page changed"),
                    true)});
    c.writes = c.destructive = true;
    out.append(c);
    c = command(u"page.rename"_s,
                QT_TRANSLATE_NOOP("brulion::cli", "Rename a page; every link and tag naming it follows"), pageRename,
                {ref, positional(u"title"_s, ArgType::String,
                                 QT_TRANSLATE_NOOP("brulion::cli",
                                                   "The new title (in the same folder; page move changes the folder)"),
                                 true, true)});
    c.writes = c.destructive = true;
    out.append(c);
    c = command(
            u"page.move"_s,
            QT_TRANSLATE_NOOP("brulion::cli", "Move a page to another folder; every link and tag naming it follows"),
            pageMove,
            {ref, positional(u"folder"_s, ArgType::Path,
                             QT_TRANSLATE_NOOP("brulion::cli", "The folder (. = the notebook root)"))});
    c.writes = c.destructive = true;
    out.append(c);
    c = command(u"page.delete"_s, QT_TRANSLATE_NOOP("brulion::cli", "Move a page to the trash (.trash/)"), pageDelete,
                {ref});
    c.writes = c.destructive = true;
    out.append(c);

    c = command(
            u"folder.list"_s,
            QT_TRANSLATE_NOOP("brulion::cli", "List folders (with a folder: its subfolders) and what they hold"),
            folderList,
            {positional(u"folder"_s, ArgType::Path,
                        QT_TRANSLATE_NOOP("brulion::cli", "List the subfolders of this folder (. = root)"), false)});
    out.append(c);
    c = command(
            u"folder.create"_s,
            QT_TRANSLATE_NOOP("brulion::cli", "Create a folder (names sanitised, hidden names refused)"), folderCreate,
            {positional(u"path"_s, ArgType::Path, QT_TRANSLATE_NOOP("brulion::cli", "The folder, e.g. Projekty/Dom"))});
    c.writes = true;
    out.append(c);
    c = command(u"folder.move"_s,
                QT_TRANSLATE_NOOP("brulion::cli",
                                  "Move a folder; links and tags follow, special folders update the settings"),
                folderMove,
                {positional(u"path"_s, ArgType::Path, QT_TRANSLATE_NOOP("brulion::cli", "The folder")),
                 positional(u"target"_s, ArgType::Path,
                            QT_TRANSLATE_NOOP("brulion::cli", "Into this folder (. = the notebook root)"))});
    c.writes = c.destructive = true;
    out.append(c);
    c = command(
            u"folder.rename"_s, QT_TRANSLATE_NOOP("brulion::cli", "Rename a folder; links and tags follow"),
            folderRename,
            {positional(u"path"_s, ArgType::Path, QT_TRANSLATE_NOOP("brulion::cli", "The folder")),
             positional(u"name"_s, ArgType::String, QT_TRANSLATE_NOOP("brulion::cli", "The new name"), true, true)});
    c.writes = c.destructive = true;
    out.append(c);
    c = command(u"folder.delete"_s,
                QT_TRANSLATE_NOOP("brulion::cli", "Move a folder and everything in it to the trash"), folderDelete,
                {positional(u"path"_s, ArgType::Path, QT_TRANSLATE_NOOP("brulion::cli", "The folder"))});
    c.writes = c.destructive = true;
    out.append(c);
}

} // namespace brulion::cli
