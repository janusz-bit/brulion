// Painting drawings (library brulion_drawing; docs/UI.md "Drawing editor"): what the drawing
// editor's paper (DrawingCanvas) and the previews of drawings (DrawingImageProvider: embeds in
// notes, the picture view) share — the paper with its lines or grid, the elements (freehand lines,
// shapes, lines, arrows, text, pictures: core drawing/geometry.h) in the colours of the light or
// the dark theme (core drawing/palette.h), and Excalidraw's dark filter per pixel for pictures
// Brulion does not draw itself when their colours cannot be mapped one by one (drawing::darkSvg).
//
//   const bool dark = drawing::rendersDark(d.theme, appIsDark);
//   QImage preview = renderDrawing(d, QSize(640, 320), dark);
//
// Text is drawn in the app's fonts (setTextFonts(): QML's DrawingFonts.set from Theme.fontUi and
// Theme.fontMono): Excalidraw's monospace families (3, 8) in the monospace one, all others in the
// UI one, the size the element's fontSize; each line at the baseline of drawing::textLines(), the
// same in the SVG. Pictures as decoded from the scene's files (missing: a light grey placeholder);
// in the dark theme they keep their colours — but the backdrop (core drawing/backdrop.h: a slide or a
// PDF page under the ink) is shown "document dark", through darkImage(), so the ink mapped light
// stays readable on it. Previews decode a backdrop at the size they show it.
//
// Thread-safe (the fonts behind a lock, no other shared state): the image provider calls them in
// Qt Quick's image reader thread.
#pragma once

#include "drawing/drawing.h"
#include "drawing/edit.h"
#include "drawing/geometry.h"
#include "drawing/palette.h"

#include <QColor>
#include <QFont>
#include <QHash>
#include <QImage>
#include <QJsonObject>
#include <QLineF>
#include <QPainterPath>
#include <QPointF>
#include <QSizeF>
#include <QTransform>

class QPainter;

namespace brulion::drawingview {

/// An ink's colour with its opacity in the alpha.
QColor toQColor(const drawing::Ink &ink);
/// A colour of palette.h ("#rgb", "#rrggbb" or "#rrggbbaa": CSS order, the alpha last — QColor's
/// own parser reads nine characters as #aarrggbb); invalid when malformed.
QColor toQColor(QStringView hex);
/// The outline of a stroke as a path in scene coordinates: the curves of the SVG path
/// (perfect-freehand's getSvgPathFromStroke, quadratic segments through the midpoints), so the
/// screen and the file show the same outline.
QPainterPath outlinePath(const QList<QPointF> &outline);
QPainterPath strokePath(const drawing::Stroke &stroke);

/// The fonts text is drawn in: `sans` for every Excalidraw family but the monospace ones, `mono`
/// for those (process-wide; empty: the app's font, the system's fixed font).
void setTextFonts(const QString &sans, const QString &mono);
/// The font family Brulion draws Excalidraw's font family `family` in.
QString textFontFamily(int family);
/// The font of a text element of Excalidraw's `family` at `pixelSize`.
QFont textFont(int family, double pixelSize);
/// The width of a line of text in those fonts (core drawing/edit.h measures and wraps with it).
drawing::TextMetrics textMetrics();

/// An element's paths ready to paint (scene coordinates, its rotation applied: drawing::
/// elementShape()) and its colours in either theme.
struct Prepared {
    /// drawing::mergeKey() and the version, versionNonce and `updated` it was made of
    /// (DrawingCanvas reuses it while the element's stay: another device's edit of the same version
    /// has another nonce).
    QString key;
    int version = 0;
    int nonce = 0;
    qint64 updated = 0;
    QPainterPath area;
    QPainterPath ink;
    QPainterPath outline;
    QList<QLineF> hatch;
    double hatchWidth = 0;
    QList<QPainterPath> heads;
    QList<bool> headFilled;
    QRectF bounds;
    /// Freehand lines: the ink with its opacity (drawing::displayInk); others: the stroke's and the
    /// fill's colour (invalid: none), light and dark.
    QColor light;
    QColor dark;
    QColor fillLight;
    QColor fillDark;
};
/// A core path as a QPainterPath.
QPainterPath toPainterPath(const drawing::Path &path);
/// The element prepared for painting, with at most `hatchLines` hatch lines (drawing::elementShape:
/// 0 — a hatched fill drawn solid; callers count a drawing's lines against
/// drawing::DrawingHatchLines).
Prepared prepare(const drawing::Stroke &element, qsizetype hatchLines = drawing::MaxHatchLines);
/// The picture of an image element: decoded from its data URL in `files` (null when it has none
/// Brulion draws: drawing::safeDataUrl()); a larger raster than `atMost` (valid) decoded at that size,
/// aspect kept (a preview's backdrop).
QImage decodePicture(const QJsonObject &files, const QString &fileId, const QSize &atMost = QSize());
/// Pictures already decoded, by fileId (a null image: none); a backdrop's dark twin under
/// darkPictureKey().
using PictureCache = QHash<QString, QImage>;
QString darkPictureKey(const QString &fileId);
/// Paint an element into `painter` (which maps scene coordinates); `pictures` caches the decoded
/// pictures of `files`. `backdrop`: the element is the drawing's backdrop (drawing::backdropIndex) —
/// in the dark its picture is drawn through darkImage().
void paintElement(QPainter &painter, const drawing::Stroke &element, const Prepared &prepared, bool dark,
                  const QJsonObject &files, PictureCache &pictures, bool backdrop = false);

/// Paint the paper of an item of `size` that shows the scene through `sceneToItem`: its colour
/// (drawing::displayPaper, opaque: it replaces whatever the image held) and the lines or the grid
/// at the scene's multiples of their spacing, one device pixel wide at the device pixel ratio `dpr`
/// (none when they would be closer than 6 px), in drawing::displayRule.
void paintPaper(QPainter &painter, const drawing::Drawing &drawing, bool dark, const QTransform &sceneToItem,
                const QSizeF &size, qreal dpr);
/// The part `frame` of the scene (empty: the whole picture, drawing::pictureRect — what Brulion's
/// SVG of it shows) fitted into an image of `pixels` with its aspect ratio kept, centred on the
/// paper; the elements in the colours of the light or the dark theme. Null for an empty size.
QImage renderDrawing(const drawing::Drawing &drawing, QSize pixels, bool dark, const QRectF &frame = QRectF());
/// Excalidraw's dark filter (drawing::darkRgba: invert 93 %, hue-rotate 180°) on every pixel, the
/// alpha kept: how the dark theme shows a picture Brulion does not draw itself (an Excalidraw
/// drawing with frames) when drawing::darkSvg cannot map its colours one by one.
QImage darkImage(const QImage &light);

} // namespace brulion::drawingview
