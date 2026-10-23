#include "notebook/links.h"

#include "markdown/blockdocument.h"
#include "markdown/journal.h"
#include "markdown/linkformat.h"
#include "util/paths.h"

using namespace Qt::StringLiterals;

namespace brulion::notebook {

QString pageLinkTarget(const VaultPath &path, const VaultConfig &config,
                       const std::function<QString(const QString &)> &resolveTitle)
{
    if (path.isEmpty())
        return {};
    const QString title = paths::pageTitle(path);
    // A daily note by its file name ([[2026-10-22]] reads as the date), also before it exists.
    if (markdown::journalDateFromPath(path, config))
        return title;
    // The title, unless it leads to another page (the same name in another folder): then the
    // path. A title that leads nowhere yet (a new page) will lead here once the page is saved.
    const QString resolved = resolveTitle ? resolveTitle(title) : QString();
    if (resolved.isEmpty() || resolved == path)
        return title;
    return paths::stripMarkdownExtension(path);
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
    if (doc.at(row).headingLevel > 0) {
        QList<Heading> headings;
        int index = -1;
        for (int i = 0; i < doc.count(); ++i) {
            const markdown::BlockEntry &entry = doc.at(i);
            if (entry.headingLevel <= 0)
                continue;
            if (i == row)
                index = int(headings.size());
            headings.append(Heading{entry.headingLevel, entry.body.section(u'\n', 0, 0), entry.line});
        }
        const QString fragment = markdown::uniqueHeadingFragment(headings, index);
        if (!fragment.isEmpty()) {
            out.headingLink = markdown::formatReference(ReferenceKind::HeadingLink, pageTarget, fragment);
            out.embed = markdown::formatReference(ReferenceKind::HeadingEmbed, pageTarget, fragment);
        }
    }
    return out;
}

} // namespace brulion::notebook
