// The look and the hit tests of the elements of a drawing (docs/SYNTAX.md §2 "Drawings", docs/UI.md
// "Drawing editor"): one description of what every element draws, which the SVG writer turns into
// paths and the app into QPainterPaths, so the file and the screen show the same; and the tests the
// editor's tools ask (a tap, the eraser, the lasso).
//
// Brulion draws every element clean (Excalidraw's roughness 0, whatever the element says):
//  * rectangle — sharp, or with rounded corners as Excalidraw rounds them (quadratic corners,
//    cornerRadius()); ellipse — four cubic arcs; diamond — its four points, rounded by quadratic
//    corners when it has roundness;
//  * line, arrow — straight segments through the points (with roundness and three points or more:
//    a Catmull-Rom curve as cubic Béziers; a polygon closes and may be filled); arrowheads at the
//    ends in the direction of the end segment, sized as Excalidraw's (an arrow 25 at 20° — 30° when
//    the segment is short —, a diamond 12, others 15 at 25°; at most half the segment);
//  * freedraw — its perfect-freehand outline (stroke.h), the polygon of its points filled under it
//    when it has a background colour;
//  * fills — solid: the interior; hachure, cross-hatch (and zigzag, drawn as hachure): clean
//    parallel lines at −41° (cross-hatch: also at +49°), max(4, 4 × strokeWidth) apart, half the
//    stroke's width, clipped exactly to the shape — at most MaxHatchLines an element (a bigger
//    shape gets them further apart) and DrawingHatchLines a drawing (the hatched fills beyond it
//    drawn solid), so a scene of huge hatched shapes stays small and quick;
//  * text — one line per "\n" at font-independent baselines (textLines()); the box is the
//    element's; pictures — their box.
// Rotation (`angle`, clockwise) about the element's centre: its box's, or its points' bounding
// box's.
//
// Pure functions (QtCore types only); no fonts — text is measured by the app (edit.h TextMetrics).
#pragma once

#include "drawing/drawing.h"

#include <QLineF>
#include <QList>
#include <QPointF>
#include <QRectF>
#include <QString>

namespace brulion::drawing {

/// One step of a path: Move/Line to `a`; Quad with control `a` to `b`; Cubic with controls `a`, `b`
/// to `c`; Close.
struct PathOp {
    enum Kind : quint8 { Move, Line, Quad, Cubic, Close };
    Kind kind = Move;
    QPointF a;
    QPointF b;
    QPointF c;
};
using Path = QList<PathOp>;

/// What an element draws, in scene coordinates with its rotation applied, in painting order:
/// `area` (filled with the element's fill), `hatch` (lines in the fill's colour, `hatchWidth`
/// wide), `ink` (filled with its colour: a freehand line's outline), `outline` (stroked with its
/// colour, width and dashes), `heads` (stroked; the `headFilled` ones filled with its colour too).
struct ElementShape {
    Path area;
    QList<QLineF> hatch;
    double hatchWidth = 0;
    Path ink;
    Path outline;
    QList<Path> heads;
    QList<bool> headFilled;
    /// Everything it draws (strokes' half widths and arrowheads included; a text's or a
    /// picture's rotated box); null for a freehand line without points.
    QRectF bounds;
};

/// The most hatch lines one element draws (both directions together).
inline constexpr qsizetype MaxHatchLines = 2000;
/// The most hatch lines a drawing draws (the editor, the previews, the SVG each count them in
/// drawing order and give each element at most what is left; with nothing left a hatched fill is
/// drawn solid).
inline constexpr qsizetype DrawingHatchLines = 200'000;

/// The element's shape (see the header); `rotated` false: without its rotation (the SVG writer
/// rotates it with a transform). `hatchLines`: at most so many hatch lines (up to MaxHatchLines;
/// 0: a hatched fill drawn solid).
ElementShape elementShape(const Stroke &e, bool rotated = true, qsizetype hatchLines = MaxHatchLines);
/// elementShape(e).bounds — for text and pictures without building the paths.
QRectF elementBounds(const Stroke &e);
/// The centre the element rotates about.
QPointF elementCenter(const Stroke &e);
/// The bounding box of the element's points (an empty rect at the first point for one point).
QRectF pointsRect(const QList<StrokePoint> &points);
/// `p` rotated by `angle` radians (clockwise on screen) about `center`.
QPointF rotatePoint(QPointF p, QPointF center, double angle);
/// The dash pattern of the element's outline in scene units: {} solid; dashed {8, 8 + width};
/// dotted {1.5, 6 + width}.
QList<double> dashPattern(const Stroke &e);
/// The radius of a rounded rectangle's corners: Excalidraw's adaptive roundness (type 3) 25 % of
/// the shorter side up to 128, else 32; proportional and legacy (1, 2): 25 %; none: 0.
double cornerRadius(const Stroke &e);
/// The arrowhead `head` at `tip`, the end of a segment from `from`, for a stroke `width` wide
/// (empty when the segment has no length); `filled` set for the kinds that are filled.
Path arrowhead(Arrowhead head, QPointF tip, QPointF from, double width, bool *filled = nullptr);

/// A line of a text element: its text and where it starts (`baseline`: the left end, the middle or
/// the right end of the line on its baseline, by the element's textAlign).
struct TextLine {
    QString text;
    QPointF baseline;
};
/// The lines of a text element, unrotated: `text` split at "\n" ("\r" dropped); line i's baseline
/// at box.top + i·fontSize·lineHeight + 0.5·fontSize·lineHeight + 0.35·fontSize (the same in the
/// SVG and on the screen, whatever the font); x at the box's left, centre or right.
QList<TextLine> textLines(const Stroke &e);

/// An SVG path's `d` of a path moved by (dx, dy), numbers with at most two decimals.
QString pathToSvg(const Path &path, double dx = 0, double dy = 0);
/// The path as polylines (curves flattened to within about `tolerance`); a closed subpath ends
/// with its first point again.
QList<QList<QPointF>> flatten(const Path &path, double tolerance = 0.5);

/// The shortest distance between the segments ab and cd (0 when they cross).
double segmentDistance(QPointF a, QPointF b, QPointF c, QPointF d);
/// `p` is inside the polygon (even-odd; the last point joins the first).
bool pointInPolygon(QPointF p, const QList<QPointF> &polygon);

/// A tap or click at `scene` hits the element: an unfilled shape, a line or an arrow within
/// `tolerance` of what it draws (Excalidraw: the inside of an unfilled shape is no hit), a filled
/// one inside too, a freehand line near its points, a text or a picture inside its box (all
/// rotated). Locked elements are never hit.
bool hitTest(const Stroke &e, QPointF scene, double tolerance);
/// The eraser's move from `a` to `b` (a disc of radius `reach`) touches the element (as hitTest();
/// a freehand line by its points, `reach` + its outline's half width). Locked: never.
bool touchesSegment(const Stroke &e, QPointF a, QPointF b, double reach);
/// The lasso (a closed polygon of three points or more, even-odd) takes the element: at least half
/// of its samples inside — a freehand line's points (at most 64), a line's or an arrow's points
/// every 8 units (at most 64), a box's corners, edge middles and centre (rotated). Locked: never.
bool lassoSelects(const Stroke &e, const QList<QPointF> &lasso);
/// The samples lassoSelects() takes of the element.
QList<QPointF> lassoSamples(const Stroke &e);

} // namespace brulion::drawing
