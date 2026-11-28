// canvas — whiteboards in the JSON Canvas format (https://jsoncanvas.org, spec 1.0; what Obsidian
// Canvas writes, docs/SYNTAX.md §2 "Canvases"). QtCore only; no QJson (it sorts keys and forgets
// the layout): an own reader that keeps where everything is (json_p.h).
//
//   canvas.h      the model (Canvas: nodes — text, file with subpath, link, group, any other type
//                 kept —, edges with sides, ends, colours and labels) and the file: readCanvas()
//                 / writeCanvas() — every key the model does not know kept as written, keys in
//                 their order, a node or edge Brulion did not change written back byte for byte,
//                 the file's layout (Obsidian's one node per line, a pretty-printed file, a BOM)
//                 kept; limits against huge or hostile files; the clipboard's fragments
//   edit.h        pure edits (canvas in → canvas out): add, remove, move, resize, text, colour,
//                 labels, ends, duplicate, group / ungroup, align, distribute, z-order; new ids
//   geometry.h    rectangles, the sides' anchors, edges as Bézier curves with arrowheads and
//                 label points, hit tests, groups' contents, bounds; the preset colours
//   merge.h       two changed versions merged node by node and field by field against their
//                 base (sync), or as a union (the sync center's conflict copies)
//   indextext.h   what the notebook's index reads of a canvas: a Markdown text of its cards (the
//                 links of file nodes and text nodes, the words to search), the ParsedNote, the
//                 card at a line; paths and links rewritten on a rename or move
//   svg.h         a canvas as an SVG picture (the export, the HTML export)
#pragma once

#include <QList>
#include <QSet>
#include <QString>
#include <QStringList>
#include <QStringView>

#include <memory>
#include <optional>

namespace brulion::canvas {

/// Largest canvas file read (characters; a canvas of a thousand cards is some 200 KB).
inline constexpr qsizetype MaxCanvasChars = 16 * 1024 * 1024;
/// Most nodes and edges read.
inline constexpr qsizetype MaxNodes = 10'000;
inline constexpr qsizetype MaxEdges = 20'000;
/// Coordinates and sizes beyond this are no position (a broken or hostile file): drawn clamped.
inline constexpr double CoordinateLimit = 10'000'000;

/// The node types of the spec (any other type is kept as read and drawn as a plain card).
inline constexpr QStringView TextType = u"text";
inline constexpr QStringView FileType = u"file";
inline constexpr QStringView LinkType = u"link";
inline constexpr QStringView GroupType = u"group";

namespace detail {
struct Origin;
struct Layout;
} // namespace detail

/// One node. The fields hold what the file says (absent optional keys are ""), the origin what
/// was read — a node whose fields equal it is written back as it was.
struct Node {
    QString id;
    QString type; ///< "text", "file", "link", "group", or another (kept)
    double x = 0;
    double y = 0;
    double width = 0;
    double height = 0;
    QString color;                                ///< "" none, "1"–"6" a preset, "#rrggbb" (canvasColor)
    QString text;                                 ///< text nodes: Markdown
    QString file;                                 ///< file nodes: the vault path
    QString subpath;                              ///< file nodes: "#Heading", "#^block" ("" none)
    QString url;                                  ///< link nodes
    QString label;                                ///< groups
    QString background;                           ///< groups: the vault path of a picture
    QString backgroundStyle;                      ///< groups: "cover", "ratio", "repeat" ("" the default)
    std::shared_ptr<const detail::Origin> origin; ///< as read (null: made here)

    bool isText() const { return type == TextType; }
    bool isFile() const { return type == FileType; }
    bool isLink() const { return type == LinkType; }
    bool isGroup() const { return type == GroupType; }
    /// Same fields (the origin is not compared).
    bool sameAs(const Node &o) const;
    /// The fields differ from what was read (true for a node made here).
    bool changed() const;
};

/// One edge (a connection between two nodes).
struct Edge {
    QString id;
    QString fromNode;
    QString toNode;
    QString fromSide; ///< "" (absent: chosen when drawn), "top", "right", "bottom", "left"
    QString toSide;
    QString fromEnd; ///< "" (absent: "none"), "none", "arrow"
    QString toEnd;   ///< "" (absent: "arrow"), "none", "arrow"
    QString color;
    QString label;
    std::shared_ptr<const detail::Origin> origin;

    bool sameAs(const Edge &o) const;
    bool changed() const;
    /// The ends as drawn: an arrow at the start only when `fromEnd` is "arrow", at the end unless
    /// `toEnd` is "none".
    bool arrowAtStart() const { return fromEnd == u"arrow"; }
    bool arrowAtEnd() const { return toEnd != u"none"; }
};

struct Canvas {
    /// In z-order: the first is drawn below the others.
    QList<Node> nodes;
    QList<Edge> edges;
    /// The file as read (its text, layout and other keys); null for a canvas made here.
    std::shared_ptr<const detail::Layout> layout;

    const Node *node(QStringView id) const;
    Node *node(QStringView id);
    qsizetype indexOfNode(QStringView id) const;
    const Edge *edge(QStringView id) const;
    qsizetype indexOfEdge(QStringView id) const;
    /// The same nodes and edges (fields and order), whatever the files' layouts.
    bool sameContent(const Canvas &o) const;
    bool isEmpty() const { return nodes.isEmpty() && edges.isEmpty(); }
};

/// Why a file is no canvas Brulion can show.
enum class ReadError {
    None,
    NotJson,   ///< not JSON (or broken)
    NotCanvas, ///< JSON, but its root is no object, `nodes` / `edges` no arrays of objects
    TooLarge,  ///< beyond MaxCanvasChars, MaxNodes, MaxEdges or the reader's limits
};

struct ReadResult {
    std::optional<Canvas> canvas;
    ReadError error = ReadError::None;
};

/// The canvas in a file's text (a BOM allowed). An empty file (or only whitespace) is an empty
/// canvas, as Obsidian makes them. Nodes without an id or with an id another node has get one of
/// their own (deterministic: from their position and text), so edits and merges can name them; an
/// edge to a node that does not exist is kept (and not drawn).
/// The Advanced Canvas plugin's look of a node (its "styleAttributes", kept in the file as written
/// and never changed here): "" for each value Brulion does not draw.
struct NodeStyle {
    QString shape;     ///< "pill", "circle", "diamond", "parallelogram", "predefined-process", "document", "database"
    QString border;    ///< "dashed", "dotted", "invisible"
    QString textAlign; ///< "center", "right"
};
NodeStyle nodeStyle(const Node &node);

ReadResult readCanvas(QStringView text);
/// The canvas as a file (UTF-16; the vault writes UTF-8). A canvas read from a file keeps its
/// layout: what lies outside the `nodes` and `edges` arrays byte for byte, each node and edge that
/// did not change as read, a changed one with its keys in their order (unknown keys as written, a
/// changed value in its place, a removed optional key left out, a new key after the others) and
/// the file's whitespace. A canvas made here is written as Obsidian writes one: tab indentation,
/// one compact node per line, no newline at the end.
QString writeCanvas(const Canvas &canvas);
/// A file of an empty canvas as Brulion creates one ("New canvas").
QString emptyCanvasText();

/// The selected nodes and the edges between them as a JSON Canvas text of their own (what Copy
/// puts on the clipboard; another canvas, Brulion's or Obsidian's, reads it).
QString fragmentText(const Canvas &canvas, const QSet<QString> &nodeIds, const QSet<QString> &edgeIds = {});
/// A clipboard text that is a canvas fragment (a JSON object with a `nodes` array of nodes, as
/// fragmentText() writes and Obsidian copies): its nodes and edges; nullopt for any other text.
std::optional<Canvas> parseFragment(QStringView text);

/// Is `path` a canvas file by its name (".canvas", any case)?
bool isCanvasPath(QStringView path);
/// "Plany/Tablica.canvas" -> "Tablica".
QString canvasTitle(QStringView path);

} // namespace brulion::canvas
