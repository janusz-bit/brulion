// Inline syntax (docs/SYNTAX.md §2-3): wiki links, embeds, block references, Markdown links and
// images, autolinks, bare URLs and tags. Part of the markdown module — see markdown/markdown.h.
#pragma once

#include "types.h"

#include <QList>
#include <QString>
#include <QStringList>
#include <QStringView>

namespace brulion::markdown {

/// Links and tags of a piece of inline Markdown.
struct InlineScan {
    QList<Link> links; ///< in document order
    QStringList tags;  ///< without '#', NFC, deduplicated case-insensitively (first spelling kept)
};

/// Extract links and tags from inline Markdown `text` (one or more lines). Inline code,
/// `%%comments%%` and HTML comments are skipped, and so is what starts inside a formula (`$…$`,
/// `$$…$$`: SYNTAX §11), as the editor shows it. `baseOffset` / `line` position the text in its
/// document: Link::from/to are absolute offsets, Link::line is 0-based. Same-page links
/// (`[[#^id]]`) have an empty target. Linear time, never fails.
InlineScan extractInline(QStringView text, qsizetype baseOffset = 0, int line = 0);

/// The destination of one Markdown link or picture: its range in the link's source text
/// (without the angle brackets of `<…>`), as written — escapes and percent-encoding kept.
struct MarkdownDestination {
    qsizetype from = -1; ///< -1: `source` is no Markdown link
    qsizetype to = -1;
    bool angle = false; ///< written as `<…>`
};

/// Where the destination of the Markdown link `source` (`[text](dest "title")`, `![alt](<dest>)`
/// — exactly a link's source, Link::from to Link::to of extractInline()) is written, by the rules
/// the parser read it with: a title, a `](` in the title or the URL repeated in the text are
/// never taken for it. {-1, -1} when `source` is no complete Markdown link.
MarkdownDestination markdownDestination(QStringView source);

/// One `#tag` occurrence (see findTags()).
struct TagSpan {
    QString name;       ///< without '#', NFC, as in InlineScan::tags (not deduplicated)
    qsizetype from = 0; ///< range in the text: from the '#' to the end of the tag (trailing
    qsizetype to = 0;   ///< '/' excluded, the `]]` of `#[[…]]` included)
};

/// Every tag occurrence of inline Markdown `text`, in order, by exactly the rules of
/// extractInline() (SYNTAX §3) — for renderers and exporters that need the positions.
QList<TagSpan> findTags(QStringView text);

/// The tag that names `name` as it is written (SYNTAX §3): "#name" when the name is a valid
/// simple tag body ("praca", "żółw", "Remont/Łazienka"), else "#[[name]]" ("Jan Kowalski",
/// "C++", "2026", "Dr. Nowak"). The name is trimmed and NFC-normalized; trailing '/' are dropped
/// ("a/" -> "#a"). Empty for an empty name and for one no tag can hold (`[`, `]`, `|`, a line
/// break). Shared by completion, renaming and copying.
QString tagSyntax(QStringView name);

/// Mask inline code spans, `%%comments%%` and `<!-- html comments -->` by spaces (same length,
/// line breaks kept). Unmatched backticks and unclosed comments stay literal.
QString maskInlineCode(QStringView text);

/// Deduplicate tag names case-insensitively, keeping the first spelling.
QStringList uniqueTags(const QStringList &names);

} // namespace brulion::markdown
