// Edits of a canvas (part of the canvas module, canvas.h): pure functions, canvas in → canvas out.
// Positions and sizes they set are whole numbers (the spec's integers); a node or edge an edit does
// not touch keeps its origin and is written back as it was read.
#pragma once

#include "canvas/canvas.h"
#include "canvas/geometry.h"

#include <QPointF>
#include <QRectF>
#include <QSet>
#include <QStringList>

#include <functional>

namespace brulion::canvas {

/// A new id as Obsidian makes them: 16 lower-case hex digits (random).
QString newId();
/// Where new ids come from (tests pass a counter).
using IdSource = std::function<QString()>;

/// A new node of `type` at `rect` (rounded), with a new id.
Node makeNode(QStringView type, const QRectF &rect, const IdSource &ids = newId);
Node makeTextNode(const QString &text, const QRectF &rect, const IdSource &ids = newId);
Node makeFileNode(const QString &path, const QString &subpath, const QRectF &rect, const IdSource &ids = newId);
Node makeLinkNode(const QString &url, const QRectF &rect, const IdSource &ids = newId);
/// The size Obsidian gives a new card: text 250×60, a note 400×400, a picture 400×300 (or its
/// aspect when known), a link 400×200 (a web page's card), other files 400×100.
QSizeF defaultSize(QStringView type, QStringView file = {});

/// `node` on top of the others.
Canvas addNode(Canvas c, Node node);
/// An edge from `from` to `to` (sides as given, "" to choose when drawn), arrow at its end.
Canvas connectNodes(Canvas c, const QString &from, const QString &fromSide, const QString &to, const QString &toSide,
                    QString *edgeId = nullptr, const IdSource &ids = newId);
/// The nodes and edges named removed, and the edges of removed nodes with them.
Canvas removeItems(Canvas c, const QSet<QString> &nodeIds, const QSet<QString> &edgeIds);
/// The nodes moved by `delta` (their new positions rounded).
Canvas moveNodes(Canvas c, const QSet<QString> &ids, const QPointF &delta);
/// One node's rectangle (rounded; at least MinNodeSize).
Canvas setNodeRect(Canvas c, const QString &id, const QRectF &rect);
Canvas setNodeText(Canvas c, const QString &id, const QString &text);
/// A file node's file and subpath.
Canvas setNodeFile(Canvas c, const QString &id, const QString &file, const QString &subpath);
Canvas setNodeUrl(Canvas c, const QString &id, const QString &url);
/// A group's label.
Canvas setGroupLabel(Canvas c, const QString &id, const QString &label);
/// A group's background picture and its style ("" none).
Canvas setGroupBackground(Canvas c, const QString &id, const QString &path, const QString &style);
/// The colour of nodes and edges (a canvasColor, "" none; anything else changes nothing).
Canvas setColor(Canvas c, const QSet<QString> &nodeIds, const QSet<QString> &edgeIds, const QString &color);
Canvas setEdgeLabel(Canvas c, const QString &id, const QString &label);
/// The arrowheads of edges: "none" / "arrow" for each end (written only where they differ from the
/// spec's defaults when the edge did not name them).
Canvas setEdgeEnds(Canvas c, const QSet<QString> &ids, bool arrowAtStart, bool arrowAtEnd);
/// The edge's sides ("" to choose when drawn).
Canvas setEdgeSides(Canvas c, const QString &id, const QString &fromSide, const QString &toSide);
/// The edge turned around.
Canvas reverseEdges(Canvas c, const QSet<QString> &ids);

/// Copies of the nodes (with the edges between them) moved by `offset`, on top, with new ids
/// (`newIds`: the copies' ids, in the canvas's order).
Canvas duplicate(Canvas c, const QSet<QString> &ids, const QPointF &offset, QStringList *newIds = nullptr,
                 const IdSource &ids_ = newId);
/// The fragment's nodes and edges added with new ids, their bounds' top-left at `at` (edges to
/// nodes the fragment lacks left out); `newIds` as in duplicate().
Canvas paste(Canvas c, const Canvas &fragment, const QPointF &at, QStringList *newIds = nullptr,
             const IdSource &ids = newId);
/// A group around the nodes (GroupPadding around their bounds), below the lowest of them.
Canvas group(Canvas c, const QSet<QString> &ids, const QString &label, QString *groupId = nullptr,
             const IdSource &idSource = newId);
/// The groups removed, their contents kept where they are.
Canvas ungroup(Canvas c, const QSet<QString> &groupIds);

enum class Align { Left, HCenter, Right, Top, VCenter, Bottom };
/// The nodes lined up on the edge (or middle) of their bounds.
Canvas align(Canvas c, const QSet<QString> &ids, Align how);
/// The nodes (three or more) spaced evenly between the first and the last, across or down.
Canvas distribute(Canvas c, const QSet<QString> &ids, bool horizontal);

enum class Order { Front, Back, Forward, Backward };
/// The nodes moved in the z-order (their order among themselves kept).
Canvas reorder(Canvas c, const QSet<QString> &ids, Order order);

} // namespace brulion::canvas
