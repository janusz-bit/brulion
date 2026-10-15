// Joplin importer: JEX archives / RAW exports (one "<id>.md" per item with a metadata footer:
// notes type 1, notebooks 2, resources 4, tags 5, note-tag links 6) and the "MD - Markdown +
// Front Matter" export (notebook folders, _resources/).
#include "io/importers_p.h"
#include "io/importutil_p.h"
#include "io/messages_p.h"
#include "markdown/frontmatter.h"
#include "markdown/tokens.h"
#include "util/paths.h"

#include <QRegularExpression>
#include <QSet>

namespace brulion::io::detail {

using namespace Qt::StringLiterals;

namespace {

enum ItemType { NoteType = 1, FolderType = 2, ResourceType = 4, TagType = 5, NoteTagType = 6 };

struct Item {
    QString id;
    int type = 0;
    QString title;
    QString body;
    QHash<QString, QString> meta;
};

const QRegularExpression &rawName()
{
    static const QRegularExpression re(u"(^|/)[0-9a-fA-F]{32}\\.md$"_s);
    return re;
}

/// Parse a RAW item: title line, blank line, body, blank line, `key: value` metadata.
std::optional<Item> parseItem(const QString &content)
{
    static const QRegularExpression metaLine(u"^([a-z_]+): ?(.*)$"_s);
    const QStringList lines = QString(content).replace(u"\r\n"_s, u"\n"_s).split(u'\n');
    qsizetype end = lines.size();
    while (end > 0 && lines[end - 1].trimmed().isEmpty())
        --end;
    qsizetype start = end;
    Item item;
    while (start > 0) {
        const auto m = metaLine.match(lines[start - 1]);
        if (!m.hasMatch())
            break;
        item.meta.insert(m.captured(1), m.captured(2));
        --start;
    }
    if (!item.meta.contains(u"id"_s) || !item.meta.contains(u"type_"_s))
        return std::nullopt;
    item.id = item.meta.value(u"id"_s).trimmed().toLower();
    item.type = item.meta.value(u"type_"_s).toInt();
    if (start > 0) {
        item.title = lines.first().trimmed();
        // Body: after the title and its blank line, up to the blank line before the metadata.
        qsizetype bodyEnd = start;
        if (bodyEnd > 0 && lines[bodyEnd - 1].trimmed().isEmpty())
            --bodyEnd;
        item.body = bodyEnd > 2 ? lines.mid(2, bodyEnd - 2).join(u'\n') : QString();
    }
    return item;
}

QDateTime isoTime(const QString &value)
{
    QString v = value.trimmed();
    if (v.size() > 10 && v[10] == u' ')
        v[10] = u'T';
    return QDateTime::fromString(v, Qt::ISODateWithMs);
}

/// A note or notebook in Joplin's trash (deleted_time set, Joplin 3).
bool isDeleted(const Item &item)
{
    return item.meta.value(u"deleted_time"_s).trimmed().toLongLong() > 0;
}

QDateTime msTime(const QString &value)
{
    const qint64 ms = value.trimmed().toLongLong();
    return ms > 0 ? QDateTime::fromMSecsSinceEpoch(ms, QTimeZone::UTC) : QDateTime();
}

QString taskLine(const QString &title, bool done, const QDateTime &due, const QDateTime &completed,
                 const QTimeZone &zone)
{
    QString line = (done ? u"- [x] "_s : u"- [ ] "_s) + title;
    if (due.isValid())
        line += u' ' + markdown::tokens::Reminder.toString() + u' ' + wallClock(due, zone);
    if (done && completed.isValid())
        line += u' ' + markdown::tokens::Done.toString() + u' '
                + completed.toTimeZone(zone).date().toString(u"yyyy-MM-dd"_s);
    return line;
}

class JoplinImporter final : public Importer {
public:
    QString id() const override { return u"joplin"_s; }
    QString name() const override { return u"Joplin"_s; }
    QStringList fileTypes() const override { return {u"*.jex"_s, u"*.zip"_s, u"*.md"_s}; }

    int detect(const QList<InputFile> &files) const override
    {
        for (const InputFile &f : files) {
            if (rawName().match(f.path).hasMatch() && f.data.contains("\ntype_: "))
                return 95;
        }
        const bool resources = anyPath(
                files, [](const QString &p) { return p.startsWith(u"_resources/") || p.contains(u"/_resources/"); });
        return resources && anyPath(files, [](const QString &p) { return paths::isMarkdown(p); }) ? 60 : 0;
    }

    ImportResult run(const QList<InputFile> &files, const ImportOptions &options) const override
    {
        ImportContext ctx(options, name());
        QList<Item> items;
        for (const InputFile &f : files) {
            if (!rawName().match(f.path).hasMatch())
                continue;
            if (auto item = parseItem(decodeText(f.data)))
                items.append(std::move(*item));
        }
        if (items.isEmpty())
            importMarkdownExport(files, ctx);
        else
            importRaw(items, files, ctx);
        return ctx.take();
    }

private:
    // --- RAW / JEX -------------------------------------------------------------------------------

    static void importRaw(const QList<Item> &items, const QList<InputFile> &files, ImportContext &ctx)
    {
        const QTimeZone zone = ctx.options().timeZone;
        QHash<QString, const Item *> folders;
        QHash<QString, const Item *> resources;
        QHash<QString, QString> tagNames;
        QHash<QString, QStringList> noteTags;
        QList<const Item *> notes;
        QSet<QString> deletedFolders; // Joplin 3 trash
        for (const Item &it : items) {
            if (it.type != FolderType)
                continue;
            folders.insert(it.id, &it);
            if (isDeleted(it))
                deletedFolders.insert(it.id);
        }
        // A note in the trash, or in a notebook (or sub-notebook) in the trash.
        const auto inTrash = [&](const Item &it) {
            if (isDeleted(it))
                return true;
            QSet<QString> seen;
            for (QString id = it.meta.value(u"parent_id"_s).trimmed().toLower(); !id.isEmpty() && !seen.contains(id);
                 id = folders.contains(id) ? folders.value(id)->meta.value(u"parent_id"_s).trimmed().toLower()
                                           : QString()) {
                if (deletedFolders.contains(id))
                    return true;
                seen.insert(id);
            }
            return false;
        };
        for (const Item &it : items) {
            switch (it.type) {
            case NoteType:
                if (!inTrash(it))
                    notes.append(&it);
                break;
            case FolderType: break; // collected above
            case ResourceType: resources.insert(it.id, &it); break;
            case TagType: tagNames.insert(it.id, it.title); break;
            default: break;
            }
        }
        for (const Item &it : items) {
            if (it.type == NoteTagType)
                noteTags[it.meta.value(u"note_id"_s).trimmed().toLower()].append(
                        it.meta.value(u"tag_id"_s).trimmed().toLower());
        }
        // Resource data files: "resources/<id>.<ext>" (or without extension).
        QHash<QString, const InputFile *> resourceFiles;
        for (const InputFile &f : files) {
            const QString base = paths::basename(f.path);
            const QString stem = base.section(u'.', 0, 0).toLower();
            if (resources.contains(stem) && !rawName().match(f.path).hasMatch())
                resourceFiles.insert(stem, &f);
        }
        const auto folderPath = [&](const QString &folderId) {
            QStringList segments;
            QSet<QString> seen;
            QString id = folderId.trimmed().toLower();
            while (!id.isEmpty() && folders.contains(id) && !seen.contains(id)) {
                seen.insert(id);
                segments.prepend(pageSegment(folders.value(id)->title));
                id = folders.value(id)->meta.value(u"parent_id"_s).trimmed().toLower();
            }
            return segments.join(u'/');
        };

        // Pages first, so links between notes can be resolved.
        QHash<QString, VaultPath> notePaths;
        QHash<QString, int> titleCount;
        for (const Item *n : std::as_const(notes)) {
            const QString folder = folderPath(n->meta.value(u"parent_id"_s));
            const QString title = pageSegment(n->title.isEmpty() ? IoMessages::tr("Untitled") : n->title);
            const VaultPath path = ctx.claimPage(folder.isEmpty() ? title : folder + u'/' + title);
            notePaths.insert(n->id, path);
            ++titleCount[pathKey(paths::pageTitle(path))];
        }
        QHash<QString, VaultPath> resourcePaths;
        const auto resourcePath = [&](const QString &id) -> std::optional<VaultPath> {
            if (const auto it = resourcePaths.constFind(id); it != resourcePaths.constEnd())
                return *it;
            const Item *res = resources.value(id);
            const InputFile *file = resourceFiles.value(id);
            if (!res || !file)
                return std::nullopt;
            QString fileName = res->meta.value(u"filename"_s).trimmed();
            if (fileName.isEmpty())
                fileName = res->title;
            const QString ext = res->meta.value(u"file_extension"_s).trimmed();
            if (fileName.isEmpty())
                fileName = id + (ext.isEmpty() ? extensionForMime(res->meta.value(u"mime"_s)) : u'.' + ext);
            else if (paths::extension(fileName).isEmpty() && !ext.isEmpty())
                fileName += u'.' + ext;
            const VaultPath path = ctx.addAttachment(paths::basename(fileName), file->data);
            resourcePaths.insert(id, path);
            return path;
        };
        const auto noteTarget = [&](const VaultPath &path) {
            const QString title = paths::pageTitle(path);
            return titleCount.value(pathKey(title)) > 1 ? paths::stripMarkdownExtension(path) : title;
        };

        static const QRegularExpression mdLink(u"(!?)\\[([^\\]\\n]*)\\]\\(:/([0-9a-fA-F]{32})\\)"_s);
        static const QRegularExpression htmlImg(u"<img\\b[^>]*\\bsrc=[\"']:/([0-9a-fA-F]{32})[\"'][^>]*>"_s,
                                                QRegularExpression::CaseInsensitiveOption);
        bool missing = false;
        for (const Item *n : std::as_const(notes)) {
            QString body = n->body;
            QString out;
            qsizetype last = 0;
            for (auto it = mdLink.globalMatch(body); it.hasNext();) {
                const auto m = it.next();
                out += QStringView(body).sliced(last, m.capturedStart() - last);
                last = m.capturedEnd();
                const QString id = m.captured(3).toLower();
                const QString label = m.captured(2);
                if (const auto note = notePaths.constFind(id); note != notePaths.constEnd()) {
                    const QString target = noteTarget(*note);
                    const bool sameLabel = label.isEmpty()
                                           || label.compare(paths::basename(target), Qt::CaseInsensitive) == 0
                                           || label.contains(u'|');
                    out += wikiLink(target, {}, {}, sameLabel ? QString() : label);
                } else if (const auto res = resourcePath(id)) {
                    const bool sameLabel = label.isEmpty() || label.contains(u'|')
                                           || label.compare(paths::basename(*res), Qt::CaseInsensitive) == 0;
                    out += m.captured(1).isEmpty() ? wikiLink(*res, {}, {}, sameLabel ? QString() : label)
                                                   : attachmentLink(*res);
                } else {
                    missing = true;
                    out += label;
                }
            }
            out += QStringView(body).sliced(last);
            body = out;
            out.clear();
            last = 0;
            for (auto it = htmlImg.globalMatch(body); it.hasNext();) {
                const auto m = it.next();
                out += QStringView(body).sliced(last, m.capturedStart() - last);
                last = m.capturedEnd();
                if (const auto res = resourcePath(m.captured(1).toLower()))
                    out += attachmentLink(*res);
                else
                    missing = true;
            }
            out += QStringView(body).sliced(last);
            body = normalizeNewlines(out);

            if (n->meta.value(u"is_todo"_s).trimmed() == u"1") {
                const QDateTime completed = msTime(n->meta.value(u"todo_completed"_s));
                body = taskLine(n->title, completed.isValid(), msTime(n->meta.value(u"todo_due"_s)), completed, zone)
                       + (body.isEmpty() ? QString() : u"\n\n"_s + body);
            }
            QVariantMap fm;
            if (const QString c = wallClock(
                        isoTime(n->meta.value(u"user_created_time"_s, n->meta.value(u"created_time"_s))), zone);
                !c.isEmpty())
                fm.insert(u"created"_s, c);
            if (const QString u = wallClock(
                        isoTime(n->meta.value(u"user_updated_time"_s, n->meta.value(u"updated_time"_s))), zone);
                !u.isEmpty())
                fm.insert(u"updated"_s, u);
            QStringList tags;
            for (const QString &tagId : noteTags.value(n->id)) {
                const QString name = tagName(tagNames.value(tagId));
                if (!name.isEmpty())
                    tags.append(name);
            }
            if (!tags.isEmpty())
                fm.insert(u"tags"_s, tags);
            if (const QString src = n->meta.value(u"source_url"_s).trimmed(); !src.isEmpty())
                fm.insert(u"source"_s, src);
            if (const QString author = n->meta.value(u"author"_s).trimmed(); !author.isEmpty())
                fm.insert(u"author"_s, author);
            addTitleAlias(fm, n->title);
            ctx.addPage(
                    notePaths.value(n->id),
                    noteWithFrontmatter(
                            fm, {u"aliases"_s, u"created"_s, u"updated"_s, u"tags"_s, u"source"_s, u"author"_s}, body));
        }
        if (missing)
            ctx.warn(IoMessages::tr("Some attachments referenced by notes are missing in the export."));
    }

    // --- MD + Front Matter -----------------------------------------------------------------------

    static void importMarkdownExport(const QList<InputFile> &files, ImportContext &ctx)
    {
        const QTimeZone zone = ctx.options().timeZone;
        TreeImport tree;
        tree.attachmentRelative = [](const QString &p) {
            const qsizetype k = p.indexOf(u"_resources/"_s);
            return k >= 0 ? p.sliced(k + 11) : p;
        };
        tree.convert = [&](const QString &content, const QString &) {
            const markdown::FrontmatterResult fm = markdown::parseFrontmatter(content);
            if (fm.data.isEmpty())
                return content;
            QVariantMap out;
            QStringList order;
            QString task;
            const QString completed = fm.data.value(u"completed?"_s).toString().trimmed().toLower();
            for (const QString &key : fm.keys) {
                const QVariant value = fm.data.value(key);
                if (key == u"title" || key == u"completed?" || key == u"due")
                    continue;
                if (key == u"created" || key == u"updated") {
                    const QString wc = wallClock(isoTime(value.toString()), zone);
                    out.insert(key, wc.isEmpty() ? value : QVariant(wc));
                } else if ((key == u"latitude" || key == u"longitude" || key == u"altitude") && value.toDouble() == 0) {
                    continue;
                } else {
                    out.insert(key, value);
                }
                order.append(key);
            }
            if (!completed.isEmpty()) {
                const bool done = completed == u"yes" || completed == u"true";
                task = taskLine(fm.data.value(u"title"_s).toString(), done, isoTime(fm.data.value(u"due"_s).toString()),
                                QDateTime(), zone);
            }
            QString body = normalizeNewlines(content.sliced(fm.bodyStart));
            if (!task.isEmpty())
                body = task + u"\n\n"_s + body;
            return noteWithFrontmatter(out, order, body);
        };
        importTree(stripCommonRoot(files), ctx, tree);
    }
};

} // namespace

std::unique_ptr<Importer> makeJoplinImporter()
{
    return std::make_unique<JoplinImporter>();
}

} // namespace brulion::io::detail
