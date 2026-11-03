// The edits of a drawing's elements (docs/UI.md "Drawing editor"): moving, resizing, rotating,
// mirroring, duplicating, deleting, recolouring, styling, grouping and reordering a selection, new
// shapes, polygons and text, labels of shapes — each a pure
// function of the drawing (the app passes the clock, random numbers and new ids in an EditStamp,
// and measures text with its fonts: TextMetrics) — and the diff of two element lists that the
// editor's undo history keeps (one step per gesture or action).
//
// Every changed element gets version + 1, a new versionNonce and `updated` (as Excalidraw does): it
// is then written from the model (excalidraw.h), and merges see it changed (merge.h). Labels (text
// with a containerId) belong to their shape: they move, are copied and deleted with it. Arrows
// bound to elements (Excalidraw's startBinding/endBinding) follow them when they move; Brulion
// never binds new arrows. Groups (groupIds) are selected, copied and reordered as a whole. Locked
// elements — and the labels of locked shapes (isLocked()) — are never touched.
#pragma once

#include "drawing/drawing.h"

#include <QPointF>
#include <QSet>
#include <QString>
#include <QStringList>
#include <QStringView>

#include <functional>
#include <optional>
#include <utility>

namespace brulion::drawing {

/// What an edit stamps on what it makes or changes.
struct EditStamp {
    /// Milliseconds since the epoch (`updated`).
    qint64 now = 0;
    /// A random positive number (seeds, versionNonce); unset: 1.
    std::function<quint32()> random;
    /// A new element or group id; unset: "e" + a counter (tests).
    std::function<QString()> newId;
};

/// A new element id as Excalidraw makes them (nanoid: 21 characters of its URL-safe alphabet).
/// Thread-safe.
QString newElementId();
/// The stamp of an edit made now (`now`, ms since the epoch) with real random numbers and
/// newElementId(): the drawing editor's, and new drawings made elsewhere (a drawing on a backdrop).
/// Thread-safe.
EditStamp randomEditStamp(qint64 now);

/// The width of a line of text in a font (the app's: QFontMetricsF); unset: 0.55 × fontSize a
/// character.
struct TextMetrics {
    std::function<double(QStringView line, int fontFamily, double fontSize)> width;
};

/// How new shapes are drawn: the colour, the stroke's width, rounded rectangles.
struct ShapeStyle {
    QString color = QStringLiteral("#1f2430");
    double width = 2;
    bool rounded = true;
};
/// How new text is written.
struct TextStyle {
    QString color = QStringLiteral("#1f2430");
    double fontSize = 20;
    /// Excalidraw's Helvetica: sans in Brulion and in excalidraw.com.
    int fontFamily = 2;
};

/// Excalidraw's padding of a label inside its shape (BOUND_TEXT_PADDING).
inline constexpr double LabelPadding = 5;
/// The line height of text Brulion writes (Excalidraw's for Helvetica).
inline constexpr double NewTextLineHeight = 1.15;

/// The index of the element `id` in the drawing (−1 none).
qsizetype indexOfElement(const Drawing &d, QStringView id);
/// The element is locked, or the label of a locked shape: never selected, moved, erased, recoloured
/// or edited.
bool isLocked(const Drawing &d, const Stroke &e);
/// The ids a selection of `ids` really takes, in drawing order: whole outermost groups, a shape's
/// label and a label's shape; locked elements and unknown ids left out.
QStringList selectionClosure(const Drawing &d, const QStringList &ids);
/// Move the elements `ids` by `delta` (clamped so nothing leaves CoordinateLimit); arrows bound to
/// moved elements follow: both ends moved — the arrow moves; one end — that end point moves (an
/// elbow arrow also moves the neighbouring corner along the segment's axis, keeping right angles);
/// the labels of arrows that move follow them.
void moveElements(Drawing &d, const QSet<QString> &ids, QPointF delta, const EditStamp &s);
/// Copies of `ids` above the topmost of them, moved by `offset`: new ids and seeds, group ids
/// renamed consistently, the labels of copied shapes copied too, bindings to elements not copied
/// dropped. The copies' ids, in drawing order.
QStringList duplicateElements(Drawing &d, const QStringList &ids, QPointF offset, const EditStamp &s);
/// Remove the elements `ids`, their labels and every reference to them (boundElements; arrow
/// bindings become null).
void deleteElements(Drawing &d, const QSet<QString> &ids, const EditStamp &s);
/// The colour of the elements `ids` (strokeColor; text colour); a highlighter stays a highlighter.
void recolorElements(Drawing &d, const QSet<QString> &ids, const QString &color, const EditStamp &s);
enum class Reorder { Forward, Backward, ToFront, ToBack };
/// Move the elements `ids` one step up or down, or to the top or the bottom. Labels stay right
/// after their shape; a group moves as a block; the leading locked elements (a backdrop) stay at
/// the bottom. The moved elements are stamped.
void reorderElements(Drawing &d, const QSet<QString> &ids, Reorder how, const EditStamp &s);
/// A new rectangle, ellipse, diamond, line or arrow dragged from `from` to `to` (`constrain`, Shift:
/// a square or a circle; lines and arrows at 15° steps).
Stroke newShape(ElementType type, QPointF from, QPointF to, bool constrain, const ShapeStyle &st, const EditStamp &s);
/// The closed shapes beyond a rectangle, an ellipse and a diamond. A file has no kinds of them:
/// each is Excalidraw's closed line (`polygon: true`, last point = first), so every program that
/// reads Excalidraw shows and edits it; once drawn it is a polygon like any other (no label).
enum class PolygonKind {
    Triangle,
    RightTriangle,
    Parallelogram,
    Trapezoid,
    Pentagon,
    Hexagon,
    Star,
    BlockArrow,
    Callout,
    Heart,
    Cloud
};
/// The kind a drawing tool's name stands for ("triangle", "rightTriangle", "parallelogram",
/// "trapezoid", "pentagon", "hexagon", "star", "blockArrow", "callout", "heart", "cloud").
std::optional<PolygonKind> polygonKindFromName(QStringView name);
/// The kind's corners in a unit square, spanning all of it (without the closing point).
QList<QPointF> polygonTemplate(PolygonKind kind);
/// A new polygon filling the box dragged from `from` to `to` (`constrain`, Shift: a square box),
/// always upright: straight sides, no fill.
Stroke newPolygon(PolygonKind kind, QPointF from, QPointF to, bool constrain, const ShapeStyle &st, const EditStamp &s);
/// A new text at `topLeft`, its box measured.
Stroke newText(QPointF topLeft, const QString &text, const TextStyle &st, const TextMetrics &m, const EditStamp &s);
/// The text an editor offers for the Text element `e`: as typed (originalText) for a label or a
/// text wrapped at its box, its lines (text) for one whose box follows it. setText() with it
/// changes nothing.
QString editableText(const Stroke &e);
/// The element holds a label inside it: a rectangle, an ellipse, a diamond or a polygon (a closed
/// line of three corners or more: newPolygon()). Excalidraw binds text only to the first three (and
/// to arrows): a polygon's label is stored as a text grouped with it (docs/SYNTAX.md §2 "Labels in
/// polygons", excalidraw.h).
bool holdsLabel(const Stroke &e);
/// The width a label wraps at in the element `container` (holdsLabel(); Excalidraw's
/// getBoundTextMaxWidth): a rectangle's width, the widest rectangle inside an ellipse (width / 2 ·
/// √2) or a diamond (width / 2), a polygon's room (labelCenter()) — less 2 × LabelPadding. The
/// label's editor is as wide.
double labelMaxWidth(const Stroke &container);
/// Where the middle of a label in `container` lies (scene coordinates, the container's rotation
/// applied): a shape's centre; in a polygon the middle of its widest band across, 40 % of its
/// height, between 30 % and 70 % of the way down (a triangle's lower half, a callout's body, a
/// star's middle).
QPointF labelCenter(const Stroke &container);
/// The text of the Text element `id` replaced: measured (autoResize), wrapped at its box's width,
/// or — a label — wrapped to labelMaxWidth() of its shape and centred in it (the shape grows to
/// hold it as Excalidraw grows it: computeContainerDimensionForBoundText). An empty text deletes
/// the element; the text it has (editableText()) changes nothing — no stamp, no new wrapping in
/// Brulion's fonts. Locked texts and labels of locked shapes stay.
void setText(Drawing &d, const QString &id, const QString &text, const TextMetrics &m, const EditStamp &s);
/// A label `text` for the shape, polygon or arrow `containerId`, right after it (its boundElements name it). The new
/// text's id ("" when there is no such shape or the text is empty).
QString addLabel(Drawing &d, const QString &containerId, const QString &text, const TextStyle &st, const TextMetrics &m,
                 const EditStamp &s);
/// `text` wrapped to lines no wider than `maxWidth` (at spaces; a longer word at characters);
/// the lines it has stay.
QString wrapText(const QString &text, double maxWidth, int fontFamily, double fontSize, const TextMetrics &m);
/// The size of a text's box: its widest line, its lines × fontSize × lineHeight.
QSizeF measureText(const QString &text, int fontFamily, double fontSize, double lineHeight, const TextMetrics &m);

/// A handle of a selection's frame.
enum class Handle { TopLeft, Top, TopRight, Right, BottomRight, Bottom, BottomLeft, Left };
/// The frame `from` with `handle` dragged by `delta` (the frame's own axes). The opposite corner or
/// edge stays; a side never passes it nor gets shorter than `minSize`. A side of no length stays so
/// (a straight line). `proportional`: the frame keeps its shape — a corner from that corner, an
/// edge about the middle of the edge opposite.
QRectF resizedFrame(const QRectF &from, Handle handle, QPointF delta, bool proportional, double minSize);
/// Scale the elements `ids` from the frame `from` to `to`, both in the frame's own axes: `angle`
/// (radians) about `from`'s centre, as a frame around one rotated element is. Points and boxes
/// scale, line widths stay. A text only scales when the scale is the same both ways (its size and
/// box with it) — otherwise it is only carried along. Labels: a shape's is wrapped again in its new
/// size, in a scale the same both ways also scaled, and the shape grows to hold it, as when it is
/// written (setText()); an arrow's stays at its middle. Bound arrows' ends follow. An element
/// turned relative to the frame scales along its own axes, so nothing is ever sheared. Changes
/// nothing when an element would leave CoordinateLimit or the frames are not usable.
void resizeElements(Drawing &d, const QSet<QString> &ids, const QRectF &from, const QRectF &to, double angle,
                    const TextMetrics &m, const EditStamp &s);
/// Turn the elements `ids` by `angle` radians (clockwise) about `pivot`: each about its own centre
/// and its centre about `pivot`; a shape's label with it, an arrow's stays level at its middle; an
/// elbow arrow only moves (rotated it would lose its right angles). Bound arrows' ends follow.
/// Changes nothing when an element would leave CoordinateLimit.
void rotateElements(Drawing &d, const QSet<QString> &ids, QPointF pivot, double angle, const EditStamp &s);
enum class Flip { Horizontal, Vertical };
/// Mirror the elements `ids` about the vertical (Horizontal) or the horizontal line (Vertical)
/// through `pivot`. Text stays readable: it only changes place. A picture is mirrored in its box.
void flipElements(Drawing &d, const QSet<QString> &ids, Flip flip, QPointF pivot, const EditStamp &s);
/// Which parts of a style the element has: a fill (a shape, a polygon), a thickness (anything drawn
/// with a line), dashes (shapes, lines and arrows), corners that may round (a rectangle, a diamond,
/// a line, an arrow, not an elbow one), arrowheads.
bool hasFill(const Stroke &e);
bool hasThickness(const Stroke &e);
bool hasDashes(const Stroke &e);
bool canRound(const Stroke &e);
bool hasHeads(const Stroke &e);
/// The stroke's width of a thickness (0 thin, 1 medium, 2 thick) on this element: Excalidraw's 1, 2,
/// 4 for shapes, as the tool bar's pen (0.5, 1, 2) and highlighter (3, 5, 8) for freehand lines.
double thicknessWidth(const Stroke &e, int thickness);
/// The thickness nearest to the element's width.
int thicknessOf(const Stroke &e);
/// What a style change sets; what is unset stays.
struct ElementStyle {
    /// A colour or "transparent".
    std::optional<QString> fill;
    std::optional<FillStyle> fillStyle;
    /// 0, 1, 2: thicknessWidth().
    std::optional<int> thickness;
    std::optional<StrokeStyle> strokeStyle;
    /// 0–100.
    std::optional<int> opacity;
    std::optional<bool> rounded;
    std::optional<double> fontSize;
    std::optional<Arrowhead> startArrowhead;
    std::optional<Arrowhead> endArrowhead;
};
/// Set the style on the elements `ids` that have that part of it; only those that change are
/// stamped. A new font size lays the text out again (setText()), so a label's shape may grow.
void styleElements(Drawing &d, const QSet<QString> &ids, const ElementStyle &style, const TextMetrics &m,
                   const EditStamp &s);
/// `ids` are two elements or more (a label counts with its shape) not all in one outermost group.
bool canGroup(const Drawing &d, const QSet<QString> &ids);
/// One of `ids` is in a group.
bool canUngroup(const Drawing &d, const QSet<QString> &ids);
/// A new outermost group of the elements `ids`; its id ("" when canGroup() is false).
QString groupElements(Drawing &d, const QSet<QString> &ids, const EditStamp &s);
/// The elements `ids` taken out of their outermost group (inner groups stay).
void ungroupElements(Drawing &d, const QSet<QString> &ids, const EditStamp &s);

/// How alignElements() lines the parts of a selection up: by their left edges, their middles across
/// or their right edges; by their top edges, their middles down or their bottom edges.
enum class Align { Left, Center, Right, Top, Middle, Bottom };
/// Along which axis distributeElements() spaces them.
enum class Distribute { Horizontal, Vertical };
/// How many parts of `ids` align and distribute move, each as a whole: an outermost group, a shape
/// or an arrow with its label, any other element (locked ones left out, as unknown ids).
int arrangeUnits(const Drawing &d, const QSet<QString> &ids);
/// Line the parts of `ids` (arrangeUnits()) up along an edge or the middle of the box all of them
/// cover, each by the box it covers (elementBounds(): a rotated element by its turned outline, a
/// group by all of its members) — Excalidraw's "Align". Moved as moveElements() moves them (labels
/// and bound arrows follow; nothing leaves CoordinateLimit). Two parts or more; parts already there
/// are not stamped.
void alignElements(Drawing &d, const QSet<QString> &ids, Align how, const EditStamp &s);
/// Space the parts of `ids` evenly between the two outermost ones, which stay (Excalidraw's
/// "Distribute"): the same gap between their boxes, sorted by their middles; when they overlap too
/// much for gaps, the same step between their middles. Three parts or more.
void distributeElements(Drawing &d, const QSet<QString> &ids, Distribute axis, const EditStamp &s);

/// What one edit changed: the elements removed (with their indices before), added (with their
/// indices after) and changed (before, after) — by mergeKey() and version — and, when the order of
/// the elements both lists have changed, that order before and after (their keys).
struct ElementDiff {
    QList<std::pair<qsizetype, Stroke>> removed;
    QList<std::pair<qsizetype, Stroke>> added;
    QList<std::pair<Stroke, Stroke>> changed;
    QStringList orderBefore;
    QStringList orderAfter;

    bool isEmpty() const { return removed.isEmpty() && added.isEmpty() && changed.isEmpty() && orderBefore.isEmpty(); }
};
ElementDiff diffElements(const QList<Stroke> &before, const QList<Stroke> &after);
/// The list `before` (forward) or `after` (back) of the diff made into the other one. With `restamp`
/// (undo and redo) every element the diff puts back is stamped as an edit made now, so versions
/// only go up and a merge by version (merge.h, the Excalidraw plugin's reconciling) takes the
/// undo as the newest edit: a version above the one it replaces and above its own + 1 (a save may
/// have written a removed element as a tombstone at version + 1), a new versionNonce, `updated`.
void applyDiff(QList<Stroke> &elements, const ElementDiff &diff, bool forward, const EditStamp *restamp = nullptr);

} // namespace brulion::drawing
