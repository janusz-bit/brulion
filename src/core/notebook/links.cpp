#include "notebook/links.h"

#include "index/vaultindex.h"
#include "markdown/blockdocument.h"
#include "markdown/journal.h"
#include "markdown/linkformat.h"
#include "markdown/tasks.h"
#include "util/paths.h"

using namespace Qt::StringLiterals;

namespace brulion::notebook {

namespace {

/// "## Title" -> 2: an ATX heading marker (up to three spaces before it, a space or the end after).
int atxLevel(QStringView line)
{
    qsizetype k = 0;
    while (k < line.size() && k < 3 && line[k] == u' ')
        ++k;
    int level = 0;
    while (k < line.size() && line[k] == u'#' && level < 7) {
        ++level;
        ++k;
    }
    if (level == 0 || level > 6 || (k < line.size() && line[k] != u' ' && line[k] != u'\t'))
        return 0;
    return level;
}

/// A block as a heading of the page, as the editor lists them (documentHeadings(), shapeOf()): a
/// Markdown heading, or a list item whose text — after a task checkbox — starts with "## "
/// (Logseq writes headings inside list items); its level (0: no heading) and first line.
int headingOf(const markdown::BlockEntry &e, QString *name)
{
    if (e.kind == BlockKind::Heading) {
        *name = e.body.section(u'\n', 0, 0);
        return e.headingLevel;
    }
    if (e.kind != BlockKind::ListItem)
        return 0;
    qsizetype start = 0;
    if (const auto marker = markdown::matchTaskMarker(e.body, 0))
        start = marker->contentStart;
    const QString first = e.body.mid(start).section(u'\n', 0, 0);
    const int level = atxLevel(first);
    if (level > 0)
        *name = first;
    return level;
}

} // namespace

QString pageLinkTarget(const VaultPath &path, const VaultConfig &config,
                       const std::function<QString(const VaultPath &)> &shortestTarget,
                       const std::function<QString(const QString &)> &resolveTitle)
{
    if (path.isEmpty())
        return {};
    const QString title = paths::pageTitle(path);
    // A daily note by its file name ([[2026-10-22]] reads as the date), also before it exists.
    if (markdown::journalDateFromPath(path, config))
        return title;
    // The shortest target that reaches the page ("Dom", "Projekty/Dom" next to another Dom).
    if (const QString shortest = shortestTarget ? shortestTarget(path) : QString(); !shortest.isEmpty())
        return shortest;
    // Not indexed yet (a new page): the title, unless it leads to another page (the same name in
    // another folder) — then the path. A title that leads nowhere yet will lead here once the
    // page is saved.
    const QString resolved = resolveTitle ? resolveTitle(title) : QString();
    if (resolved.isEmpty() || resolved == path)
        return title;
    return paths::stripMarkdownExtension(path);
}

QString pageLinkTarget(const VaultPath &path, const VaultConfig &config, const VaultIndex *index)
{
    if (!index)
        return pageLinkTarget(path, config, {}, {});
    return pageLinkTarget(
            path, config, [index](const VaultPath &p) { return index->linkTarget(p).value_or(QString()); },
            [index](const QString &title) { return index->resolveLink(title).value_or(QString()); });
}

std::optional<BlockLinks> blockLinks(const QString &content, int line, const QString &pageTarget,
                                     const std::function<bool(const QString &)> &isTaken)
{
    using markdown::ReferenceKind;
    markdown::BlockDocument doc(content);
    const int row = doc.indexAtLine(line);
    if (row < 0)
        return std::nullopt;
    const markdown::BlockIdResult ensured = doc.ensureBlockId(row, isTaken);
    if (ensured.id.isEmpty())
        return std::nullopt;
    BlockLinks out;
    out.id = ensured.id;
    out.content = doc.toString();
    out.added = out.content != content;
    out.line = doc.at(row).line;
    out.ref = markdown::formatReference(ReferenceKind::BlockRef, {}, {}, out.id);
    out.wikiLink = markdown::formatReference(ReferenceKind::BlockLink, pageTarget, {}, out.id);
    out.embed = markdown::formatReference(ReferenceKind::BlockEmbed, {}, {}, out.id);
    // A heading: its name, or a heading path when another heading has that name too (the editor's
    // documentHeadings(): every heading-shaped block, its first line).
    QString name;
    if (headingOf(doc.at(row), &name) > 0) {
        QList<Heading> headings;
        int index = -1;
        for (int i = 0; i < doc.count(); ++i) {
            const int level = headingOf(doc.at(i), &name);
            if (level <= 0)
                continue;
            if (i == row)
                index = int(headings.size());
            headings.append(Heading{level, name, doc.at(i).line});
        }
        const QString fragment = markdown::uniqueHeadingFragment(headings, index);
        if (!fragment.isEmpty()) {
            out.headingLink = markdown::formatReference(ReferenceKind::HeadingLink, pageTarget, fragment);
            out.embed = markdown::formatReference(ReferenceKind::HeadingEmbed, pageTarget, fragment);
        } else {
            // A heading's section embeds through its page ("![[Dom#^id]]"), as the editor copies it.
            out.embed = markdown::formatReference(ReferenceKind::BlockLinkEmbed, pageTarget, {}, out.id);
        }
    }
    return out;
}

} // namespace brulion::notebook
