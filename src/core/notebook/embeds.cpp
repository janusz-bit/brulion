#include "notebook/embeds.h"

#include "markdown/blockdocument.h"
#include "markdown/inline.h"

#include <cstdlib>
#include <limits>

namespace brulion::notebook {

namespace {

/// Block `i` of `doc` embeds an attachment `embedsIt` accepts.
bool blockEmbeds(const markdown::BlockDocument &doc, int i, const std::function<bool(QStringView)> &embedsIt)
{
    const markdown::BlockEntry &e = doc.at(i);
    if (e.kind == BlockKind::Code || e.kind == BlockKind::Frontmatter || e.kind == BlockKind::Rule)
        return false;
    if (!e.body.contains(u"!["))
        return false;
    const markdown::InlineScan scan = markdown::extractInline(e.body);
    for (const Link &link : scan.links) {
        const bool embed = link.kind == LinkKind::Embed
                           || (link.kind == LinkKind::Markdown && link.from >= 0 && link.from < e.body.size()
                               && e.body.at(link.from) == u'!');
        if (embed && !link.target.isEmpty() && embedsIt(link.target))
            return true;
    }
    return false;
}

} // namespace

namespace {

EmbedInsertion insertBesideEmbed(const QString &content, int lineHint,
                                 const std::function<bool(QStringView target)> &embedsIt, const QString &blockBody,
                                 bool before)
{
    EmbedInsertion out;
    markdown::BlockDocument doc(content);
    int found = -1;
    if (embedsIt) {
        const int hinted = lineHint >= 0 ? doc.indexAtLine(lineHint) : -1;
        if (hinted >= 0 && blockEmbeds(doc, hinted, embedsIt)) {
            found = hinted;
        } else {
            int best = std::numeric_limits<int>::max();
            for (int i = 0; i < doc.count(); ++i) {
                const int distance = std::abs(doc.at(i).line - lineHint);
                // (Strictly nearer: on a tie the earlier block stays.)
                if (distance < best && blockEmbeds(doc, i, embedsIt)) {
                    best = distance;
                    found = i;
                }
            }
        }
    }
    markdown::EditResult edit;
    if (found >= 0) {
        const markdown::BlockEntry &e = doc.at(found);
        edit = e.kind == BlockKind::ListItem ? doc.insertAfter(found, blockBody, e.depth, true)
                                             : doc.insertAfter(found, blockBody, 0, false);
        out.afterEmbed = true;
        if (before && edit.focus >= 0 && edit.focus < doc.count())
            edit = doc.moveUp(edit.focus);
    } else {
        const int last = doc.count() - 1;
        const bool list = last < 0 || doc.at(last).kind == BlockKind::ListItem;
        edit = doc.insertAfter(last, blockBody, 0, list);
    }
    out.content = doc.toString();
    if (edit.focus >= 0 && edit.focus < doc.count())
        out.line = doc.at(edit.focus).line;
    return out;
}

} // namespace

EmbedInsertion insertBlockAfterEmbed(const QString &content, int lineHint,
                                     const std::function<bool(QStringView target)> &embedsIt, const QString &blockBody)
{
    return insertBesideEmbed(content, lineHint, embedsIt, blockBody, false);
}

EmbedInsertion insertBlockBeforeEmbed(const QString &content, int lineHint,
                                      const std::function<bool(QStringView target)> &embedsIt, const QString &blockBody)
{
    return insertBesideEmbed(content, lineHint, embedsIt, blockBody, true);
}

} // namespace brulion::notebook
