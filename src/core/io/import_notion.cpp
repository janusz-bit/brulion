// Notion importer: the "Markdown & CSV" export (a ZIP, possibly with nested part ZIPs). Notion
// appends a 32-hex id to every page / database / folder name; the ids are stripped, links are
// rewritten to wiki links, and CSV databases become pages with a GFM table.
#include "io/csv.h"
#include "io/htmlmarkdown.h"
#include "io/htmlutil_p.h"
#include "io/importers_p.h"
#include "io/importutil_p.h"
#include "util/paths.h"

#include <QRegularExpression>

namespace brulion::io::detail {

using namespace Qt::StringLiterals;

namespace {

const QRegularExpression &idSuffix()
{
    static const QRegularExpression re(u"^(.*?)\\s+([0-9a-fA-F]{32})(_all)?$"_s);
    return re;
}

/// A path segment without Notion's id: "Plan 0123…cdef.md" -> "Plan.md", "DB 0123…_all.csv" ->
/// "DB.csv". `id` receives the id (lower case).
QString stripSegment(const QString &segment, QString *id = nullptr)
{
    QString stem = segment;
    QString ext;
    const qsizetype dot = segment.lastIndexOf(u'.');
    if (dot > 0 && segment.size() - dot <= 10) {
        stem = segment.first(dot);
        ext = segment.sliced(dot);
    }
    const auto m = idSuffix().match(stem);
    if (!m.hasMatch() || m.capturedLength(1) == 0)
        return segment;
    if (id)
        *id = m.captured(2).toLower();
    return m.captured(1) + ext;
}

QString stripPath(const QString &path)
{
    QStringList segments = path.split(u'/');
    for (QString &s : segments)
        s = stripSegment(s);
    return segments.join(u'/');
}

bool hasNotionId(const QString &path)
{
    for (const QString &segment : path.split(u'/')) {
        if (stripSegment(segment) != segment)
            return true;
    }
    return false;
}

/// Leading "# Title" line removed when it repeats the page title.
QString withoutTitleHeading(const QString &content, const QString &title)
{
    qsizetype pos = 0;
    while (pos < content.size() && (content[pos] == u'\n' || content[pos] == u'\r'))
        ++pos;
    const qsizetype eol = content.indexOf(u'\n', pos);
    const QString first = content.sliced(pos, (eol < 0 ? content.size() : eol) - pos).trimmed();
    if (!first.startsWith(u"# "_s))
        return content;
    const QString heading = first.sliced(2).trimmed();
    if (heading.compare(title, Qt::CaseInsensitive) != 0
        && pageSegment(heading).compare(title, Qt::CaseInsensitive) != 0)
        return content;
    return eol < 0 ? QString() : content.sliced(eol + 1);
}

/// "Key: Value" lines at the top of a database row page -> frontmatter (removed from `body`).
QVariantMap takeRowProperties(QString &body)
{
    static const QRegularExpression prop(u"^([^:#>!\\[|*\\-\\s][^:\\n]{0,79}?):\\s+(.*)$"_s);
    QStringList lines = normalizeNewlines(body).split(u'\n');
    qsizetype i = 0;
    while (i < lines.size() && lines[i].trimmed().isEmpty())
        ++i;
    QVariantMap fm;
    qsizetype k = i;
    QList<std::pair<QString, QString>> found;
    for (; k < lines.size() && !lines[k].trimmed().isEmpty(); ++k) {
        const auto m = prop.match(lines[k]);
        if (!m.hasMatch())
            return {};
        found.append({m.captured(1).trimmed(), m.captured(2).trimmed()});
    }
    if (found.isEmpty())
        return {};
    for (const auto &[key, value] : std::as_const(found)) {
        const QString lower = key.toLower();
        if (lower == u"tags" || lower == u"tagi" || lower == u"labels" || lower == u"etykiety") {
            QStringList tags;
            for (const QString &t : value.split(u',', Qt::SkipEmptyParts)) {
                const QString name = tagName(t);
                if (!name.isEmpty())
                    tags.append(name);
            }
            if (!tags.isEmpty())
                fm.insert(u"tags"_s, tags);
        } else {
            fm.insert(key, value);
        }
    }
    lines = lines.mid(k);
    body = lines.join(u'\n');
    return fm;
}

QString tableCell(QString text)
{
    text.replace(u"\r\n"_s, u" "_s).replace(u'\n', u' ').replace(u'\r', u' ');
    text.replace(u'|', u"\\|"_s);
    return text.trimmed();
}

struct Entry {
    const InputFile *file = nullptr;
    QString source;   ///< input path
    QString stripped; ///< path without ids
    VaultPath path;
};

class NotionImporter final : public Importer {
public:
    QString id() const override { return u"notion"_s; }
    QString name() const override { return u"Notion"_s; }
    QStringList fileTypes() const override { return {u"*.zip"_s}; }

    int detect(const QList<InputFile> &files) const override
    {
        return anyPath(files,
                       [](const QString &p) {
                           const QString ext = paths::extension(p);
                           return (ext == u".md" || ext == u".csv") && hasNotionId(p);
                       })
                       ? 90
                       : 0;
    }

    ImportResult run(const QList<InputFile> &rawFiles, const ImportOptions &options) const override
    {
        ImportContext ctx(options, name());
        const QList<InputFile> files = stripCommonRoot(rawFiles);
        QList<Entry> pages;
        QList<Entry> databases;
        QList<Entry> attachments;
        QHash<QString, QString> sourceById; // id -> source path (pages and databases)
        QHash<QString, qsizetype> databaseByStripped;

        for (const InputFile &f : files) {
            if (paths::isIgnored(f.path))
                continue;
            Entry e;
            e.file = &f;
            e.source = f.path;
            e.stripped = stripPath(f.path);
            QString id;
            stripSegment(paths::basename(f.path), &id);
            const QString ext = paths::extension(f.path);
            if (ext == u".md") {
                if (!id.isEmpty())
                    sourceById.insert(id, f.path);
                pages.append(e);
            } else if (ext == u".csv" && hasNotionId(paths::basename(f.path))) {
                const QString key = pathKey(e.stripped);
                const bool all = paths::basename(f.path).chopped(4).endsWith(u"_all"_s);
                const auto it = databaseByStripped.constFind(key);
                if (it != databaseByStripped.constEnd()) {
                    if (all)
                        databases[*it] = e; // prefer the "_all" variant (every view's rows)
                    continue;
                }
                databaseByStripped.insert(key, databases.size());
                databases.append(e);
                if (!id.isEmpty())
                    sourceById.insert(id, f.path);
            } else {
                attachments.append(e);
            }
        }

        SourceTree tree;
        QHash<QString, int> titleCount; // page title key -> number of pages with that title
        for (Entry &p : pages) {
            p.path = ctx.claimPage(paths::stripMarkdownExtension(p.stripped));
            tree.add(p.source, p.path);
            ++titleCount[pathKey(paths::pageTitle(p.path))];
        }
        for (Entry &d : databases) {
            d.path = ctx.claimPage(d.stripped.first(d.stripped.size() - 4));
            tree.add(d.source, d.path);
            ++titleCount[pathKey(paths::pageTitle(d.path))];
        }
        // Rows link to the CSV under both names (with and without "_all").
        for (const InputFile &f : files) {
            if (paths::extension(f.path) == u".csv" && !tree.contains(f.path)) {
                const auto it = databaseByStripped.constFind(pathKey(stripPath(f.path)));
                if (it != databaseByStripped.constEnd())
                    tree.add(f.path, databases[*it].path);
            }
        }
        for (Entry &a : attachments) {
            a.path = ctx.claimAttachment(a.stripped);
            tree.add(a.source, a.path);
        }

        const auto linkTarget = [&](const VaultPath &path) {
            const QString title = paths::pageTitle(path);
            return titleCount.value(pathKey(title)) > 1 ? paths::stripMarkdownExtension(path) : title;
        };

        for (const Entry &p : std::as_const(pages)) {
            QString body = withoutTitleHeading(decodeText(p.file->data), paths::pageTitle(p.stripped));
            QVariantMap fm;
            if (databaseByStripped.contains(pathKey(paths::dirname(p.stripped) + u".csv"_s)))
                fm = takeRowProperties(body);
            body = notionUrlsToWiki(body, sourceById, tree, linkTarget);
            body = tree.rewrite(body, p.source, SourceTree::PageLinks::Wiki);
            ctx.addPage(p.path, noteWithFrontmatter(fm, {u"tags"_s}, body));
        }
        for (const Entry &d : std::as_const(databases))
            ctx.addPage(d.path, databasePage(d, pages, linkTarget));
        for (const Entry &a : std::as_const(attachments))
            ctx.addFile(a.path, a.file->data);
        return ctx.take();
    }

private:
    /// Links to notion.so pages that were exported too become wiki links.
    static QString notionUrlsToWiki(const QString &body, const QHash<QString, QString> &sourceById,
                                    const SourceTree &tree, const std::function<QString(const VaultPath &)> &linkTarget)
    {
        static const QRegularExpression notionUrl(
                u"^https?://(?:www\\.)?notion\\.(?:so|site)/.*?([0-9a-fA-F]{8}-?[0-9a-fA-F]{4}-?[0-9a-fA-F]{4}-?[0-9a-fA-F]{4}-?[0-9a-fA-F]{12})(?:[?#].*)?$"_s);
        return replaceLinks(body, [&](const FoundLink &l) -> std::optional<QString> {
            if (l.kind != LinkKind::Markdown || !l.hasUrlScheme || l.image)
                return std::nullopt;
            const auto m = notionUrl.match(l.target);
            if (!m.hasMatch())
                return std::nullopt;
            const QString source = sourceById.value(m.captured(1).remove(u'-').toLower());
            if (source.isEmpty())
                return std::nullopt;
            const QString target = linkTarget(tree.destination(source));
            QString label = l.display.trimmed();
            if (label.contains(u'|') || label.contains(u'[')
                || label.compare(paths::basename(target), Qt::CaseInsensitive) == 0)
                label.clear();
            return wikiLink(target, {}, {}, label);
        });
    }

    /// A database page: the CSV as a GFM table, the first column linking to the row pages.
    static QString databasePage(const Entry &db, const QList<Entry> &pages,
                                const std::function<QString(const VaultPath &)> &linkTarget)
    {
        const QList<QStringList> rows = parseCsv(decodeText(db.file->data));
        if (rows.isEmpty())
            return QString();
        const QString rowFolder = pathKey(db.stripped.first(db.stripped.size() - 4));
        QHash<QString, VaultPath> rowPages; // title key -> row page
        for (const Entry &p : pages) {
            if (pathKey(paths::dirname(p.stripped)) == rowFolder)
                rowPages.insert(pathKey(paths::pageTitle(p.stripped)), p.path);
        }
        qsizetype columns = 0;
        for (const QStringList &r : rows)
            columns = std::max(columns, r.size());
        QStringList lines;
        for (qsizetype r = 0; r < rows.size(); ++r) {
            QStringList cells;
            for (qsizetype c = 0; c < columns; ++c) {
                QString cell = c < rows[r].size() ? rows[r][c] : QString();
                if (r > 0 && c == 0) {
                    const auto it = rowPages.constFind(pathKey(pageSegment(cell)));
                    if (it != rowPages.constEnd() && !cell.trimmed().isEmpty()) {
                        cells.append(wikiLink(linkTarget(*it)));
                        continue;
                    }
                }
                cells.append(r == 0 ? tableCell(cell) : tableCell(escapeMarkdownText(cell, false)));
            }
            lines.append(u"| "_s + cells.join(u" | "_s) + u" |"_s);
            if (r == 0)
                lines.append(u'|' + QStringList(columns, u" --- "_s).join(u'|') + u'|');
        }
        // CSV cells are plain text: their dollars never make formulas ("$x$", "Cena $10, 15$").
        return escapeTextFormulas(lines.join(u'\n') + u'\n');
    }
};

} // namespace

std::unique_ptr<Importer> makeNotionImporter()
{
    return std::make_unique<NotionImporter>();
}

} // namespace brulion::io::detail
