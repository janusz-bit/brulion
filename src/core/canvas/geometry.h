// The geometry of a canvas (part of the canvas module, canvas.h): node rectangles, where an edge
// leaves and enters a node, edges as cubic Bézier curves with arrowheads and their labels' places,
// hit tests, the nodes inside a group, bounds — and the preset colours. Units are the canvas's
// own (Obsidian's pixels at 100 %).
#pragma once

#include "canvas/canvas.h"

#include <QList>
#include <QPointF>
#include <QRectF>
#include <QSet>

namespace brulion::canvas {

/// The smallest card a resize leaves.
inline constexpr double MinNodeSize = 40;
/// How far around a group's members the frame "Group" makes lies.
inline constexpr double GroupPadding = 30;
/// The room a group's label takes above its frame (the frame stays where the members are).
inline constexpr double GroupLabelHeight = 32;
/// An arrowhead's length and width (canvas units).
inline constexpr double ArrowLength = 16;
inline constexpr double ArrowWidth = 12;

/// The node's rectangle (sizes at least 1, coordinates within CoordinateLimit).
QRectF nodeRect(const Node &n);
/// The rectangle of all nodes (empty for an empty canvas).
QRectF bounds(const Canvas &c);
QRectF bounds(const Canvas &c, const QSet<QString> &nodeIds);

enum class Side { Top, Right, Bottom, Left };
/// "top", "right", … → Side; nullopt for "" or anything else.
std::optional<Side> sideFrom(QStringView name);
QString sideName(Side side);
/// The middle of a side of `r`.
QPointF anchor(const QRectF &r, Side side);
/// The direction a side faces (unit vector out of the rectangle).
QPointF outward(Side side);
/// The side of `r` nearest to `p` (a point dropped on a card connects there).
Side nearestSide(const QRectF &r, const QPointF &p);

/// An edge as it is drawn: a cubic Bézier from `start` to `end` (shortened where an arrowhead
/// sits), the arrowheads' triangles (tip first) and the label's point.
struct EdgeShape {
    bool valid = false; ///< both nodes exist
    QPointF start, c1, c2, end;
    QList<QPointF> startArrow; ///< empty: none
    QList<QPointF> endArrow;
    QPointF labelPoint;
    Side fromSide = Side::Right;
    Side toSide = Side::Left;
};
/// The edge between two rectangles. Sides the edge does not name are chosen facing each other
/// (by the centres' offset). The curve leaves and enters along the sides' directions, bending as
/// far as half the distance (at least 40, at most 300).
EdgeShape edgeShape(const Edge &e, const QRectF &from, const QRectF &to);
EdgeShape edgeShape(const Canvas &c, const Edge &e);
/// A curve to a loose point (an edge being dragged out of a side).
EdgeShape looseShape(const QRectF &from, Side fromSide, const QPointF &to);
/// The point of the curve at `t` (0…1).
QPointF bezierPoint(const EdgeShape &s, double t);
/// The curve as a polyline of `segments` pieces.
QList<QPointF> polyline(const EdgeShape &s, int segments = 32);
/// The distance from `p` to the curve.
double distanceTo(const EdgeShape &s, const QPointF &p);
/// The curve as an SVG path ("M x y C …") in canvas units.
QString svgPath(const EdgeShape &s);

/// The topmost node whose rectangle holds `p` (groups only where nothing else is, and only on
/// their label or frame when `groupsByFrame`), or -1.
qsizetype nodeAt(const Canvas &c, const QPointF &p, bool groupsByFrame = false, double frame = 12);
/// The edge whose curve passes within `tolerance` of `p` (the topmost: the last), or -1.
qsizetype edgeAt(const Canvas &c, const QPointF &p, double tolerance);
/// The nodes whose rectangles intersect `r` (a rubber band: `contained` — only those inside it).
QSet<QString> nodesIn(const Canvas &c, const QRectF &r, bool contained);
/// The nodes a group holds: those whose rectangle lies inside the group's (groups inside it and
/// their nodes too), as Obsidian moves them with it.
QSet<QString> groupContents(const Canvas &c, const QString &groupId);
/// `ids` with the contents of each group among them.
QSet<QString> withGroupContents(const Canvas &c, const QSet<QString> &ids);

/// The colour of a canvasColor: a preset ("1" red, "2" orange, "3" yellow, "4" green, "5" cyan,
/// "6" purple — as Obsidian shows them, lighter in the dark theme) or "#rgb" / "#rrggbb"; ""
/// for no colour or anything else (never a note's CSS).
QString colorHex(QStringView canvasColor, bool dark);
/// A preset's number 1–6, or 0 (a hex colour, none).
int presetIndex(QStringView canvasColor);
/// `color` normalised as a canvasColor: "1"–"6", "#rrggbb" (lower case); "" when it is neither.
QString normalizeColor(QStringView color);

} // namespace brulion::canvas
