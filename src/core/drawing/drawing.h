// Drawings in notes (module "drawing"; deps: none but QtCore — docs/SYNTAX.md §2 "Drawings",
// docs/UI.md "Drawing editor").
//
// A drawing is an ordinary SVG attachment (`![[Rysunek 2026-10-27 14.32.svg]]`) that carries its
// editable data inside: an Excalidraw scene, embedded the way Excalidraw itself writes "Export →
// SVG → Embed scene" (excalidraw.h). Every program shows the file as a picture (Obsidian, Logseq,
// browsers, file managers); excalidraw.com opens it as an editable scene; Brulion's drawing editor
// edits its handwriting.
//
// API overview (namespace brulion::drawing)
//
//   StrokePoint, Stroke, Background, Drawing   the model: freehand strokes in scene coordinates
//   PaperTheme                                 how the drawing is shown: as the app, light, dark
//   bounds(drawing), strokeBounds(stroke)      what the strokes cover (their outlines)
//   editable(drawing)                          only strokes Brulion draws itself (no shapes, text,
//                                              pictures, rotated or filled freehand lines)
//   normalizeStroke(stroke)                    its points rounded as the scene stores them
//   readDrawing(bytes, &error)                 an SVG with an embedded scene, or a `.excalidraw`
//                                              file → Drawing (nullopt: not a drawing / broken)
//   toSvg(drawing), pictureRect(drawing)       the file Brulion writes (svgwrite.h)
//   darkColor(), displayInk(), displayPaper()  the colours of the dark theme (palette.h)
//   strokeOutline(stroke), outlineToSvgPath()  the outline of a stroke (stroke.h)
//   parseScene(), sceneJson(), svgPayload(),   the Excalidraw format (excalidraw.h)
//   encodePayload(), looksLikeExcalidrawSvg()
//
// Pure functions of their input: no clock, no random numbers (the app passes ids, seeds and the
// time of new strokes), no files.
#pragma once

#include <QJsonArray>
#include <QJsonObject>
#include <QList>
#include <QRectF>
#include <QString>

#include <optional>

namespace brulion::drawing {

/// A point of a stroke in scene coordinates (1 unit = 1 px at zoom 1) with the pen's pressure
/// (0–1; 0.5 when the device reports none).
struct StrokePoint {
    double x = 0;
    double y = 0;
    double pressure = 0.5;

    friend bool operator==(const StrokePoint &, const StrokePoint &) = default;
};

/// What drew a stroke: a pen (opaque, pressure-sensitive) or a highlighter (wide, translucent, an
/// even width). Stored as the element's `customData.brulionTool` (Excalidraw keeps customData).
enum class Tool { Pen, Highlighter };

/// One freehand stroke — an Excalidraw `freedraw` element.
struct Stroke {
    /// The element's id (unique in the scene).
    QString id;
    /// The points in scene coordinates (the element's `x`/`y` + its relative `points`).
    QList<StrokePoint> points;
    /// "#rrggbb".
    QString color = QStringLiteral("#1f2430");
    /// Excalidraw's `strokeWidth`; the outline is about 1.4 × 4.25 × width wide at middle pressure.
    double width = 1;
    /// 0–100, as Excalidraw's `opacity`.
    int opacity = 100;
    /// No real pressure (a mouse, a finger): the outline thins with the speed instead.
    bool simulatePressure = true;
    Tool tool = Tool::Pen;
    int seed = 1;
    int version = 1;
    int versionNonce = 0;
    /// Milliseconds since the epoch of the last change (`updated`).
    qint64 updated = 0;
    /// The element as it was read: written back unchanged (every field Brulion does not model
    /// kept as it was). Empty for a stroke drawn in Brulion.
    QJsonObject element;
};

/// The paper's pattern (appState key `brulionBackground`; Excalidraw ignores it).
enum class Background { Plain, Lines, Grid };

/// Distance between the lines of Background::Lines, between the grid's lines (scene units).
inline constexpr double LineSpacing = 32;
inline constexpr double GridSpacing = 24;
/// How far from the scene's origin a stroke Brulion edits may reach (scene units, either axis):
/// a freehand line beyond it (a file from elsewhere) stays foreign, so the drawing is shown, not
/// edited — the paper's lines, the file's size and the view stay bounded.
inline constexpr double CoordinateLimit = 1e6;
/// The paper's colour when the scene names none (Excalidraw's white).
inline constexpr QStringView DefaultPaper = u"#ffffff";
/// The colour of the lines and the grid on the paper.
inline constexpr QStringView RuleColor = u"#c9d3e0";

/// How the drawing is shown (appState key `brulionTheme`: "light" | "dark"; absent: Auto, which
/// follows the app's theme — Excalidraw ignores the key). The colours stay stored as drawn, in
/// light-theme terms; the dark theme maps them when it shows them (palette.h).
enum class PaperTheme { Auto, Light, Dark };

/// A drawing: strokes in drawing order, and what the scene has besides them.
struct Drawing {
    QList<Stroke> strokes;
    /// Elements Brulion does not draw (shapes, arrows, text, pictures, rotated or filled freehand
    /// lines), kept as read. A drawing with any is not editable().
    QJsonArray foreign;
    /// The scene's appState as read (viewBackgroundColor, gridSize, …).
    QJsonObject appState;
    /// The scene's files (the pictures of image elements), as read.
    QJsonObject files;
    /// Other top-level keys of the scene, as read (`source`, …).
    QJsonObject scene;
    Background background = Background::Plain;
    /// The paper's colour, "#rrggbb" (appState `viewBackgroundColor`).
    QString paper = DefaultPaper.toString();
    /// Light or dark paper, or as the app (appState `brulionTheme`).
    PaperTheme theme = PaperTheme::Auto;
};

/// The area a stroke's outline covers (an empty rect for a stroke without points).
QRectF strokeBounds(const Stroke &stroke);
/// The area every stroke covers (an empty rect for a drawing without strokes).
QRectF bounds(const Drawing &drawing);
/// Brulion can edit the drawing: it holds only strokes it draws itself.
bool editable(const Drawing &drawing);
/// The stroke's points as the scene stores them: coordinates to two decimals, pressures to three
/// (0–1), points that are not finite left out. A stroke drawn in the editor is normalised when it
/// ends, so what the editor shows is what the file has.
void normalizeStroke(Stroke &stroke);

/// Why readDrawing() found no drawing.
enum class ReadError {
    None,
    /// No embedded scene: a picture, not a drawing (any other SVG, a PNG, text).
    NoScene,
    /// A scene that cannot be read (broken base64 or compression, not JSON, not a scene).
    Corrupt,
};

/// The drawing in a file's bytes: an SVG with an embedded Excalidraw scene (Brulion's own and
/// excalidraw.com's "Embed scene" exports, payload versions 1 and 2) or a `.excalidraw` JSON file.
std::optional<Drawing> readDrawing(QByteArrayView bytes, ReadError *error = nullptr);

/// The background's name in the scene ("plain", "lines", "grid") and back (unknown: Plain).
QString backgroundName(Background background);
Background backgroundFromName(QStringView name);
/// The theme's name ("auto", "light", "dark") and back (unknown or empty: Auto).
QString paperThemeName(PaperTheme theme);
PaperTheme paperThemeFromName(QStringView name);

} // namespace brulion::drawing
