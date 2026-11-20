// What the notebook's index reads of a canvas (part of the canvas module, canvas.h; docs/SYNTAX.md
// §2 "Canvases"): its cards as one Markdown text — each text node's Markdown, a file node as a
// link to its file (`[[Projekty/Plan.md#Cele]]`), a link node's URL, a group's label, an edge's
// label, one after another in the canvas's order with a blank line between them —, so the links
// count as the canvas's links (backlinks, the graph, unreferenced files), its words are found by
// search, and a line of it leads back to its card. The index keeps this text as the canvas's
// content; nothing else of a note applies to a canvas (no tasks, reminders, properties, aliases,
// block ids or journal days: those of a card are its text only, never armed or written).
//
// On a rename or move (docs/SYNTAX.md §2 "Rewriting on rename and move") the canvas's file paths
// and its text nodes' links are rewritten in the file itself: rewriteCanvas().
#pragma once

#include "canvas/canvas.h"
#include "types.h"

#include <functional>
#include <optional>

namespace brulion::canvas {

/// The lines of the index text that come from one node or edge.
struct IndexSegment {
    int firstLine = 0;
    int lastLine = 0;
    QString nodeId; ///< "" for an edge's label
    QString edgeId; ///< "" for a node
};

struct IndexText {
    QString text;
    QList<IndexSegment> segments;
};

/// The canvas's cards as the Markdown text the index reads (above). A text node's text is kept as
/// written but for what would leak into the next card: line endings as LF, an unclosed code fence
/// or `$$` block closed, an unclosed comment ended; the text never starts with frontmatter.
IndexText indexText(const Canvas &c);
/// indexText() of a canvas file ("" for a file that is no canvas).
IndexText indexText(QStringView fileText);
/// The link a file node is written as in the index text: `[[path#subpath]]`, or `[name](path)`
/// (percent-encoded) for a path a wiki link cannot hold (`[`, `]`, `|`, `#`, `^`).
QString fileLink(const QString &path, const QString &subpath);

/// The index's ParsedNote of a canvas: parseNote() of its index text with everything but blocks,
/// links, headings and tags left out (no tasks, reminders, properties, frontmatter, aliases, block
/// ids, journal day); the title is the file's name without ".canvas".
ParsedNote indexNote(const VaultPath &path, const QString &indexText, const VaultConfig &config = VaultConfig());

/// The node (or the edge, `edgeId`) a 0-based line of the index text comes from; "" for none.
QString nodeAtLine(const IndexText &text, int line, QString *edgeId = nullptr);

/// A canvas file with each text node's text passed through `text` and each file path (file nodes,
/// groups' backgrounds) through `path` (return the argument for no change); every other byte kept.
/// `paths` counts the paths changed. The file's text as it is when nothing changes; nullopt when it
/// is no canvas.
std::optional<QString> rewriteCanvas(const QString &fileText, const std::function<QString(const QString &)> &text,
                                     const std::function<QString(const QString &)> &path, int *paths = nullptr);

} // namespace brulion::canvas
