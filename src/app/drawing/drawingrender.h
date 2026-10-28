// Painting drawings (library brulion_drawing; docs/UI.md "Drawing editor"): what the drawing
// editor's paper (DrawingCanvas) and the previews of drawings (DrawingImageProvider: embeds in
// notes, the picture view) share — the paper with its lines or grid, the strokes in the colours of
// the light or the dark theme (core drawing/palette.h), and Excalidraw's dark filter for pictures
// Brulion does not draw itself.
//
//   const bool dark = drawing::rendersDark(d.theme, appIsDark);
//   QImage preview = renderDrawing(d, QSize(640, 320), dark);
//
// Thread-safe (no shared state): the image provider calls them in Qt Quick's image reader thread.
#pragma once

#include "drawing/drawing.h"
#include "drawing/palette.h"

#include <QColor>
#include <QImage>
#include <QPainterPath>
#include <QPointF>
#include <QSizeF>
#include <QTransform>

class QPainter;

namespace brulion::drawingview {

/// An ink's colour with its opacity in the alpha.
QColor toQColor(const drawing::Ink &ink);
/// The outline of a stroke as a path in scene coordinates: the curves of the SVG path
/// (perfect-freehand's getSvgPathFromStroke, quadratic segments through the midpoints), so the
/// screen and the file show the same outline.
QPainterPath outlinePath(const QList<QPointF> &outline);
QPainterPath strokePath(const drawing::Stroke &stroke);

/// Paint the paper of an item of `size` that shows the scene through `sceneToItem`: its colour
/// (drawing::displayPaper) and the lines or the grid at the scene's multiples of their spacing,
/// one device pixel wide at the device pixel ratio `dpr` (none when they would be closer than
/// 6 px), in drawing::displayRule.
void paintPaper(QPainter &painter, const drawing::Drawing &drawing, bool dark, const QTransform &sceneToItem,
                const QSizeF &size, qreal dpr);
/// The whole picture of the drawing (drawing::pictureRect: what its SVG shows) fitted into an image
/// of `pixels` with its aspect ratio kept, centred on the paper; the strokes in the colours of the
/// light or the dark theme. Null for an empty size.
QImage renderDrawing(const drawing::Drawing &drawing, QSize pixels, bool dark);
/// Excalidraw's dark filter (drawing::darkRgba: invert 93 %, hue-rotate 180°) on every pixel, the
/// alpha kept: how the dark theme shows a picture Brulion does not draw itself (an Excalidraw
/// drawing with shapes, a picture in a scene).
QImage darkImage(const QImage &light);

} // namespace brulion::drawingview
