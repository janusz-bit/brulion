// Block structure parser (docs/SYNTAX.md §4), internal to the markdown module: a fast,
// line-based parser that splits a note into blocks — list items (outline tree by indentation),
// paragraphs, headings (ATX and setext), fenced code, tables, blockquotes, callouts (containers
// with child blocks), thematic breaks, HTML blocks and the frontmatter / leading page-property
// block. It only finds structure and line ranges; inline content is interpreted by parse.cpp.
// Every line is visited a constant number of times (quotes re-parse their lines once per
// nesting level, at most 16), so parsing is linear.
//
// Blockquotes that contain list items, fenced code, HTML or callouts are containers too (their
// content becomes child blocks, so tasks in quotes are tasks); other quotes stay leaf blocks.
//
// Deliberate differences from CommonMark, chosen for notes written by Obsidian / Logseq:
// indented code blocks are not recognised (indented lines are list items or text); a list item
// is a child of the previous item when its marker is indented past that item's marker (tab = 4
// columns); a GFM table ends at the first line without `|`.
#pragma once

#include "markdown/frontmatter.h"
#include "markdown/lines_p.h"
#include "types.h"

#include <QList>
#include <QString>
#include <QStringView>

#include <optional>
#include <utility>

namespace brulion::markdown::detail {

/// One line as seen by a container (container prefixes such as "> " removed).
struct LineView {
    int line = 0;         ///< 0-based document line
    qsizetype offset = 0; ///< document offset of the view's first character
    qsizetype length = 0;
};

/// How a block uses a line: Text (inline Markdown), Code (code / raw HTML, never scanned),
/// Fence (code fence delimiter), Blank (blank line between own lines), Meta (syntax only:
/// setext underline, delimiter row, standalone `^id`).
enum class LineRole { Text, Code, Fence, Blank, Meta };

struct OwnLine {
    LineView view;
    qsizetype start = 0; ///< index in the view where the block's content starts
    LineRole role = LineRole::Text;
};

/// Structural block node (an arena entry of Structure::nodes).
struct Node {
    BlockKind kind = BlockKind::Paragraph;
    int line = 0;
    int endLine = 0;
    int ownEndLine = 0;
    int depth = 0;
    QList<OwnLine> own;
    QList<int> children; ///< indices into Structure::nodes
    int markerCol = -1;  ///< list items: visual column of the marker
    int contentCol = -1; ///< list items: visual column of the content
    int headingLevel = 0;
    QString lang;
    QString calloutType;
    QString calloutFold;
    QString trailingId; ///< id from a standalone `^id` line after the block
    int trailingIdLine = -1;
    bool container = false; ///< a blockquote whose content is parsed into child blocks
};

/// Parsed document structure.
struct Structure {
    QString content;
    QString masked; ///< content with `%%comments%%` blanked (BOM blanked too)
    QList<text::Line> lines;
    QList<Range> comments; ///< document-level comment ranges, in order
    std::optional<FrontmatterBlock> frontmatter;
    QList<Node> nodes; ///< arena
    QList<int> roots;  ///< root nodes in document order
    /// A `$$` line of the body (not inside a quote) found no closing `$$` line before the end of
    /// the text (SYNTAX §11): with more lines after the text its formula could go on into them.
    bool displayMathOpenAtEnd = false;

    QStringView text(const LineView &v) const { return QStringView(content).sliced(v.offset, v.length); }
    QStringView maskedText(const LineView &v) const { return QStringView(masked).sliced(v.offset, v.length); }
    LineView lineView(int line) const { return LineView{line, lines[line].start, lines[line].text.size()}; }
};

/// What may start a document (used when a fragment of a document is parsed on its own).
struct StructureOptions {
    bool frontmatter = true;    ///< YAML frontmatter at the very start
    bool pageProperties = true; ///< leading `key:: value` lines are page properties
};

/// Parse the block structure of a whole document (or of a fragment, see StructureOptions).
Structure parseStructure(const QString &content, StructureOptions options = {});

/// Innermost node whose own lines include `line` (or whose range does, as a fallback); -1.
int nodeAtLine(const Structure &st, int line);

/// (node, parent) pairs in document pre-order (parent -1 for roots).
QList<std::pair<int, int>> preOrder(const Structure &st);

/// List item marker of a line (not a thematic break).
struct ListMarker {
    Indent indent;
    qsizetype markerFrom = 0;   ///< index of the marker
    qsizetype markerEnd = 0;    ///< index after the marker
    qsizetype contentStart = 0; ///< index where content starts (after the following whitespace)
    int contentCol = 0;         ///< visual column of the content
    bool ordered = false;
    QChar bullet;   ///< '-', '*', '+' or the ordered delimiter '.' / ')'
    int number = 0; ///< ordered lists: the number
};
std::optional<ListMarker> matchListMarker(QStringView t);

bool isThematicBreak(QStringView t);

/// ATX heading `^[ \t]{0,3}(#{1,6})(?:[ \t]+|$)`: level and length of the matched prefix.
struct AtxHeading {
    int level = 0;
    qsizetype prefixLength = 0;
};
std::optional<AtxHeading> matchAtxHeading(QStringView t);

/// Setext underline level (1 for `===`, 2 for `---`), 0 when not an underline.
int setextLevel(QStringView t);

/// Length of a blockquote marker `^[ \t]{0,3}>[ \t]?`, or -1.
qsizetype quoteMarkerLength(QStringView t);

/// Callout header `[!type]+/- Title` (text after the quote prefix).
struct CalloutHeader {
    QString type; ///< lower-cased
    QString fold; ///< "+", "-" or empty
    qsizetype titleStart = 0;
};
std::optional<CalloutHeader> matchCalloutHeader(QStringView t);

/// GFM table start: a header row with `|` followed by a matching delimiter row.
bool isTableStart(QStringView t, std::optional<QStringView> next);

/// A Logseq / Dataview property line `key:: value` (no indentation).
bool isPropertyLine(QStringView t);

} // namespace brulion::markdown::detail
