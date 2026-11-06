// A block placed right after (or before) the block that embeds an attachment (docs/UI.md "PDF
// viewer": "Draw on this page" puts the new drawing under the PDF it was drawn on, in a note that
// is not open in an editor; "Drawing editor": the drawing of the next or the previous PDF page beside
// the current one's). Pure: content in, content out — untouched bytes and line endings stay as they were. Part
// of the notebook module — see notebook/notebook.h.
//
//   insertBlockAfterEmbed("- Wykład ![[Fizyka.pdf]]\n- Dalej\n", 0, isFizyka, "![[Fizyka s. 4.svg]]")
//     -> "- Wykład ![[Fizyka.pdf]]\n- ![[Fizyka s. 4.svg]]\n- Dalej\n", line 1, afterEmbed
#pragma once

#include <QString>
#include <QStringView>

#include <functional>

namespace brulion::notebook {

struct EmbedInsertion {
    QString content;         ///< the new text (untouched bytes and line endings preserved)
    int line = -1;           ///< first line of the inserted block
    bool afterEmbed = false; ///< false: no block embeds the file (any more) — appended at the end
};

/// `blockBody` (e.g. "![[Wykład 3 s. 4.svg]]") as a block of its own right after the block that
/// embeds the attachment — `embedsIt(target)` decides for each `![[…]]` / `![…](…)` target of a
/// block (as the parser reads it: fragments such as `#page=3` split off, a Markdown destination
/// percent-decoded): the block at `lineHint` when it still embeds it, else the nearest such block
/// (ties: the earlier one), else appended after the last block. The new block takes the style of
/// the one it follows: after a list item a list item at the same depth (markdown::BlockDocument::
/// insertAfter — after the item's children, its marker style as its siblings'; in an ordered list
/// the later items are numbered on, as the editor's Enter does), after any other block a paragraph
/// (after a paragraph or a quote a blank line between them, as the document mode writes; a heading
/// needs none). Appended: a list item after a list, else a paragraph; an empty note gets a list item.
/// Embeds inside code blocks and the frontmatter never count (nor inside inline code or formulas:
/// markdown::extractInline skips them).
EmbedInsertion insertBlockAfterEmbed(const QString &content, int lineHint,
                                     const std::function<bool(QStringView target)> &embedsIt, const QString &blockBody);
/// The same right before that block (the drawing editor's "Previous PDF page" puts the drawing
/// of the previous page above the current one's): inserted after it as above, then moved above it
/// (markdown::BlockDocument::moveUp: the block with its children swapped with the new one); with no
/// block embedding it, appended as above (`afterEmbed` false).
EmbedInsertion insertBlockBeforeEmbed(const QString &content, int lineHint,
                                      const std::function<bool(QStringView target)> &embedsIt,
                                      const QString &blockBody);

} // namespace brulion::notebook
