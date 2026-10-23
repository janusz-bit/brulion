// Copying blocks of a BlockDocument — the block selection of the editor (docs/UI.md "Selecting and
// copying"): what goes to the clipboard as Markdown and as plain text, and the block ids a paste
// must not duplicate. Part of the markdown module — see markdown/markdown.h for the module
// overview. Pure functions, no clipboard access (the editor puts the text there).
#pragma once

#include "markdown/blockdocument.h"

#include <QList>
#include <QString>

#include <functional>

namespace brulion::markdown {

/// The rows of `rows` (any order; duplicates and invalid rows ignored) that have no ancestor in
/// `rows`, ascending: each stands for its whole subtree. Frontmatter / page-property rows are left
/// out — they are never copied.
QList<int> topRows(const BlockDocument &doc, const QList<int> &rows);

/// The Markdown of the blocks `tops` (top rows, see topRows()) with their whole subtrees, folded
/// children included:
///   * every block exactly as written — markers, numbering ("3."), task checkboxes and their ✅
///     dates, code fences, tables, `$$` formulas, `^id` and `key:: value` lines, no-break spaces
///     and every other character —, and the blank lines between blocks; none after the last;
///   * each top item re-based to column 0: its subtree loses the item's indentation, measured in
///     visual columns (tabs to tab stops of 4; a tab that crosses the edge leaves the rest of its
///     width as spaces), so mixed tabs and spaces keep their nesting. In a range that rises through
///     levels every top item starts at column 0 — leftover indentation would be an indented code
///     block in other apps;
///   * line breaks "\n" (CRLF and CR become LF), never the BOM.
/// Tops that are not adjacent in the document are separated as they were, with a blank line when
/// they would merge otherwise (a paragraph after a list item). "" for no tops.
QString blocksMarkdown(const BlockDocument &doc, const QList<int> &tops);

/// Options of blocksPlainText().
struct PlainTextOptions {
    /// Text of the block a `((id))` reference names; "" when unknown (the reference stays).
    std::function<QString(const QString &id)> blockText;
};

/// The blocks `tops` with their subtrees as plain text ("Kopiuj jako tekst"): one group of lines
/// per block, two spaces per level below its top item; list items with "• " (numbered ones with
/// their number as written), tasks with ☐ (☑ done, ☒ cancelled); headings and quotes without
/// their markers, a callout's title without its `[!type]`, code without its fences (the code
/// verbatim), tables as written; inline markup reduced to what it shows (stripInlineMarkup():
/// links become their labels, `[[a|b]]` b, emphasis marks go; tags and dates stay), `((id))`
/// references their block's text, block ids (`^id`, `id::`) left out. A blank line where the
/// document has one between two blocks. "" for no tops.
QString blocksPlainText(const BlockDocument &doc, const QList<int> &tops, const PlainTextOptions &options = {});

/// Inline Markdown as the text it shows: `[[a|b]]` -> b, `[[a]]` -> a, `[label](url)` and
/// `![alt](url)` -> label / alt, emphasis and code marks (`**` `__` `~~` `==` `` ` ``, single `*`
/// and `_` around words) removed. Tags, dates and everything else stay. Lines are kept.
QString stripInlineMarkup(const QString &text);

/// `markdown` without the block ids `drop` returns true for — a ` ^id` at the end of a block's
/// line and `id:: …` property lines —, everything else as it was (a paste must not duplicate an id
/// that already resolves elsewhere: `((id))` references are vault-wide). Ids inside code are code,
/// not ids.
QString withoutBlockIds(const QString &markdown, const std::function<bool(const QString &id)> &drop);

/// The block ids of the blocks of `markdown` (in order, without duplicates).
QStringList blockIdsOf(const QString &markdown);

} // namespace brulion::markdown
