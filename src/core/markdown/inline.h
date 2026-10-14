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
/// `%%comments%%` and HTML comments are skipped. `baseOffset` / `line` position the text in its
/// document: Link::from/to are absolute offsets, Link::line is 0-based. Same-page links
/// (`[[#^id]]`) have an empty target. Linear time, never fails.
InlineScan extractInline(QStringView text, qsizetype baseOffset = 0, int line = 0);

/// Mask inline code spans, `%%comments%%` and `<!-- html comments -->` by spaces (same length,
/// line breaks kept). Unmatched backticks and unclosed comments stay literal.
QString maskInlineCode(QStringView text);

/// Deduplicate tag names case-insensitively, keeping the first spelling.
QStringList uniqueTags(const QStringList &names);

} // namespace brulion::markdown
