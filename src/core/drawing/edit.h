// The edits of a drawing's elements (docs/UI.md "Drawing editor"): moving, duplicating, deleting,
// recolouring and reordering a selection, new shapes and text, labels of shapes — each a pure
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
Stroke newShape(ElementType type, QPointF from, QPointF to, bool constrain, const ShapeStyle &st,
                const EditStamp &s);
/// A new text at `topLeft`, its box measured.
Stroke newText(QPointF topLeft, const QString &text, const TextStyle &st, const TextMetrics &m, const EditStamp &s);
/// The text an editor offers for the Text element `e`: as typed (originalText) for a label or a
/// text wrapped at its box, its lines (text) for one whose box follows it. setText() with it
/// changes nothing.
QString editableText(const Stroke &e);
/// The width a label wraps at in the shape `container` (Excalidraw's getBoundTextMaxWidth): a
/// rectangle's width, the widest rectangle inside an ellipse (width / 2 · √2) or a diamond
/// (width / 2), less 2 × LabelPadding. The label's editor is as wide.
double labelMaxWidth(const Stroke &container);
/// The text of the Text element `id` replaced: measured (autoResize), wrapped at its box's width,
/// or — a label — wrapped to labelMaxWidth() of its shape and centred in it (the shape grows to
/// hold it as Excalidraw grows it: computeContainerDimensionForBoundText). An empty text deletes
/// the element; the text it has (editableText()) changes nothing — no stamp, no new wrapping in
/// Brulion's fonts. Locked texts and labels of locked shapes stay.
void setText(Drawing &d, const QString &id, const QString &text, const TextMetrics &m, const EditStamp &s);
/// A label `text` for the shape `containerId`, right after it (its boundElements name it). The new
/// text's id ("" when there is no such shape or the text is empty).
QString addLabel(Drawing &d, const QString &containerId, const QString &text, const TextStyle &st,
                 const TextMetrics &m, const EditStamp &s);
/// `text` wrapped to lines no wider than `maxWidth` (at spaces; a longer word at characters);
/// the lines it has stay.
QString wrapText(const QString &text, double maxWidth, int fontFamily, double fontSize, const TextMetrics &m);
/// The size of a text's box: its widest line, its lines × fontSize × lineHeight.
QSizeF measureText(const QString &text, int fontFamily, double fontSize, double lineHeight, const TextMetrics &m);

/// What one edit changed: the elements removed (with their indices before), added (with their
/// indices after) and changed (before, after) — by mergeKey() and version — and, when the order of
/// the elements both lists have changed, that order before and after (their keys).
struct ElementDiff {
    QList<std::pair<qsizetype, Stroke>> removed;
    QList<std::pair<qsizetype, Stroke>> added;
    QList<std::pair<Stroke, Stroke>> changed;
    QStringList orderBefore;
    QStringList orderAfter;

    bool isEmpty() const
    {
        return removed.isEmpty() && added.isEmpty() && changed.isEmpty() && orderBefore.isEmpty();
    }
};
ElementDiff diffElements(const QList<Stroke> &before, const QList<Stroke> &after);
/// The list `before` (forward) or `after` (back) of the diff made into the other one.
void applyDiff(QList<Stroke> &elements, const ElementDiff &diff, bool forward);

} // namespace brulion::drawing
