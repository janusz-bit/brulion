// Drawings in notes (module "drawing"; deps: none but QtCore — docs/SYNTAX.md §2 "Drawings",
// docs/UI.md "Drawing editor").
//
// A drawing is an ordinary SVG attachment (`![[Rysunek 2026-10-27 14.32.svg]]`) that carries its
// editable data inside: an Excalidraw scene, embedded the way Excalidraw itself writes "Export →
// SVG → Embed scene" (excalidraw.h). Every program shows the file as a picture (Obsidian, Logseq,
// browsers, file managers); excalidraw.com opens it as an editable scene; Brulion's drawing editor
// edits its handwriting, shapes, lines, arrows, text and pictures. The same editor edits
// `.excalidraw` scene files and the Obsidian Excalidraw plugin's `*.excalidraw.md` files, each
// written back in its own format (drawingfile.h).
//
// API overview (namespace brulion::drawing)
//
//   StrokePoint, Stroke (= Element), Drawing   the model: the Excalidraw elements Brulion draws —
//                                              freehand lines, rectangles, ellipses, diamonds,
//                                              lines, arrows, text, pictures — in scene coordinates
//   Background, PaperTheme                     the paper's pattern; light, dark or as the app
//   bounds(drawing), strokeBounds(element)     what the elements cover (geometry.h elementBounds)
//   editable(drawing)                          only elements Brulion draws itself (no frames,
//                                              embedded pages, unknown or far-away elements)
//   normalizeStroke(stroke)                    its points rounded as the scene stores them
//   readDrawing(bytes, &error)                 an SVG with an embedded scene, a `.excalidraw`
//                                              file or an Obsidian Excalidraw plugin file →
//                                              Drawing (nullopt: not a drawing / broken)
//   readDrawingFile(), writeDrawingFile(),     a drawing file read and written back in its own
//   looksLikeDrawingFile(), previewDrawing()   format: SVG, `.excalidraw`, `*.excalidraw.md`
//                                              (drawingfile.h)
//   readExcalidrawMarkdown(),                  the Obsidian Excalidraw plugin's files
//   writeExcalidrawMarkdown(), stringifyJson() (excalidrawmd.h); LZ-String (lzstring.h)
//   toSvg(drawing), pictureRect(drawing)       the file Brulion writes (svgwrite.h)
//   svgWithScene(picture, drawing)             another program's picture of the scene with the
//                                              scene embedded (an imported Trilium canvas)
//   elementShape(), hitTest(), lassoSelects()  the look and the hit tests of elements (geometry.h)
//   moveElements(), duplicateElements(), …     the edits of elements; undo diffs (edit.h)
//   merge(), mergeBase(), mergeKey()           two versions of a drawing merged (merge.h)
//   darkColor(), displayInk(), displayPaper()  the colours of the dark theme (palette.h)
//   strokeOutline(stroke), outlineToSvgPath()  the outline of a freehand line (stroke.h)
//   parseScene(), sceneJson(), svgPayload(),   the Excalidraw format (excalidraw.h)
//   encodePayload(), looksLikeExcalidrawSvg()
//
// Pure functions of their input: no clock, no random numbers (the app passes ids, seeds and the
// time of new elements), no files.
#pragma once

#include <QJsonArray>
#include <QJsonObject>
#include <QList>
#include <QRectF>
#include <QString>
#include <QStringList>

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

/// The kinds of Excalidraw elements Brulion draws and edits (`type`: "freedraw", "rectangle",
/// "ellipse", "diamond", "line", "arrow", "text", "image"). Others (frames, embedded pages,
/// unknown future kinds) stay in Drawing::foreign.
enum class ElementType { Freedraw, Rectangle, Ellipse, Diamond, Line, Arrow, Text, Image };
/// `fillStyle`: "solid", "hachure", "cross-hatch", "zigzag" (Brulion draws zigzag as hachure).
enum class FillStyle { Solid, Hachure, CrossHatch, Zigzag };
/// `strokeStyle`: "solid", "dashed", "dotted".
enum class StrokeStyle { Solid, Dashed, Dotted };
/// `startArrowhead` / `endArrowhead`: null, "arrow", "triangle", "triangle_outline", "bar", "dot"
/// (an old name of "circle"), "circle", "circle_outline", "diamond", "diamond_outline"; Other: any
/// other value, kept as read and drawn as an arrow.
enum class Arrowhead {
    None,
    Arrow,
    Triangle,
    TriangleOutline,
    Bar,
    Dot,
    Circle,
    CircleOutline,
    Diamond,
    DiamondOutline,
    Other
};
/// Text's `textAlign` ("left", "center", "right") and `verticalAlign` ("top", "middle", "bottom").
enum class TextAlign { Left, Center, Right };
enum class VerticalAlign { Top, Middle, Bottom };

/// One element of the scene Brulion draws — an Excalidraw element. (`Stroke` is the name of the
/// freehand lines Brulion began with; `Element` says the same.)
struct Stroke {
    ElementType type = ElementType::Freedraw;
    /// The element's id (unique in the scene).
    QString id;
    /// Freedraw, Line, Arrow: the points in scene coordinates (the element's `x`/`y` + its
    /// relative `points`); Freedraw with the pen's pressures.
    QList<StrokePoint> points;
    /// Rectangle, Ellipse, Diamond, Text, Image: `x`, `y`, `width`, `height` (never negative);
    /// Freedraw, Line, Arrow: their own `x`, `y`, `width`, `height` as read (unused).
    QRectF box;
    /// Radians, clockwise about the element's centre (the box's; for points, their bounding
    /// box's).
    double angle = 0;
    /// `strokeColor`, "#rrggbb" (the text's colour for Text).
    QString color = QStringLiteral("#1f2430");
    /// `backgroundColor`: "transparent" or a colour.
    QString fill = QStringLiteral("transparent");
    FillStyle fillStyle = FillStyle::Solid;
    /// Excalidraw's `strokeWidth`; a freehand line's outline is about 1.4 × 4.25 × width wide at
    /// middle pressure.
    double width = 1;
    StrokeStyle strokeStyle = StrokeStyle::Solid;
    /// `roughness` (0 clean … 2 sketchy) as read; Brulion draws every element clean.
    int roughness = 0;
    /// 0–100, as Excalidraw's `opacity`.
    int opacity = 100;
    /// `roundness`: 0 sharp corners (null), else its `type` (1 legacy, 2 proportional, 3 adaptive).
    int roundness = 0;
    /// Locked elements are never selected, moved or erased in Brulion (a backdrop).
    bool locked = false;
    /// `groupIds`, innermost first (Excalidraw's order).
    QStringList groupIds;
    /// `boundElements[].id`: a shape's label, the arrows bound to it.
    QStringList boundIds;
    /// Freedraw: no real pressure (a mouse, a finger) — the outline thins with the speed instead.
    bool simulatePressure = true;
    /// Freedraw: a pen or a highlighter.
    Tool tool = Tool::Pen;
    /// Line, Arrow: the heads at either end.
    Arrowhead startArrowhead = Arrowhead::None;
    Arrowhead endArrowhead = Arrowhead::None;
    /// Arrow: an elbow arrow (right angles; Brulion moves it rigidly). Line: closed (a polygon).
    bool elbowed = false;
    bool polygon = false;
    /// Line, Arrow: `startBinding.elementId` / `endBinding.elementId` ("" none).
    QString startBound;
    QString endBound;
    /// Text: its lines (split at "\n") as shown — wrapped in a container — and as typed.
    QString text;
    QString originalText;
    double fontSize = 20;
    /// Excalidraw's font family (1 Virgil, 2 Helvetica, 3 Cascadia, 5 Excalifont, …).
    int fontFamily = 2;
    TextAlign textAlign = TextAlign::Left;
    VerticalAlign verticalAlign = VerticalAlign::Top;
    /// The line height as a multiple of fontSize.
    double lineHeight = 1.25;
    /// The shape (or arrow) whose label this text is ("" a free text).
    QString containerId;
    /// The box follows the text (else the text wraps at the box's width).
    bool autoResize = true;
    /// Image: the key of its picture in Drawing::files ("" none: drawn as a placeholder).
    QString fileId;
    int seed = 1;
    /// Raised by every edit (with versionNonce and updated): an element whose version is the one
    /// of its read `element` is written back exactly as it was read.
    int version = 1;
    int versionNonce = 0;
    /// Milliseconds since the epoch of the last change (`updated`).
    qint64 updated = 0;
    /// The element as it was read: written back unchanged while `version` is the one read (every
    /// field Brulion does not model kept as it was), the base of what is written after an edit.
    /// Empty for an element made in Brulion.
    QJsonObject element;
};
using Element = Stroke;

/// Rectangle, Ellipse or Diamond: a shape that may hold a label.
bool isShape(ElementType type);
/// Line or Arrow.
bool isLinear(ElementType type);
/// Freedraw, Line or Arrow: the element is its points (else its box).
bool hasPoints(ElementType type);
/// The element type's name in the scene ("freedraw", …) and back (nullopt: none Brulion draws).
QString elementTypeName(ElementType type);
std::optional<ElementType> elementTypeFromName(QStringView name);

/// The paper's pattern (appState key `brulionBackground`; Excalidraw ignores it).
enum class Background { Plain, Lines, Grid };

/// Distance between the lines of Background::Lines, between the grid's lines (scene units).
inline constexpr double LineSpacing = 32;
inline constexpr double GridSpacing = 24;
/// How far from the scene's origin an element Brulion edits may reach (scene units, either axis):
/// an element beyond it (a file from elsewhere) stays foreign, so the drawing is shown, not
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

/// A drawing: its elements in drawing order (the first at the bottom), and what the scene has
/// besides them.
struct Drawing {
    /// The elements Brulion draws (freehand lines, shapes, lines, arrows, text, pictures).
    QList<Stroke> strokes;
    /// Elements Brulion does not draw (frames, embedded web pages, unknown kinds, elements beyond
    /// CoordinateLimit or broken), kept as read. A drawing with any is not editable().
    QJsonArray foreign;
    /// The scene's appState as read (viewBackgroundColor, gridSize, …).
    QJsonObject appState;
    /// The scene's files (the pictures of image elements: `{ id, mimeType, dataURL, … }` by id).
    QJsonObject files;
    /// Other top-level keys of the scene, as read (`source`, …).
    QJsonObject scene;
    Background background = Background::Plain;
    /// The paper's colour, "#rrggbb" (appState `viewBackgroundColor`).
    QString paper = DefaultPaper.toString();
    /// Light or dark paper, or as the app (appState `brulionTheme`).
    PaperTheme theme = PaperTheme::Auto;
};

/// The area an element covers (geometry.h elementBounds: a freehand line's outline, a shape with
/// its stroke and arrowheads, rotated; an empty rect for a freehand line without points).
QRectF strokeBounds(const Stroke &stroke);
/// The area every element covers (an empty rect for a drawing without elements).
QRectF bounds(const Drawing &drawing);
/// Brulion can edit the drawing: it holds only elements it draws itself.
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
/// excalidraw.com's "Embed scene" exports, payload versions 1 and 2), a `.excalidraw` JSON file or
/// an Obsidian Excalidraw plugin file (`*.excalidraw.md`) — drawingfile.h readDrawingFile(), which
/// also tells the format to write it back in.
std::optional<Drawing> readDrawing(QByteArrayView bytes, ReadError *error = nullptr);

/// The background's name in the scene ("plain", "lines", "grid") and back (unknown: Plain).
QString backgroundName(Background background);
Background backgroundFromName(QStringView name);
/// The theme's name ("auto", "light", "dark") and back (unknown or empty: Auto).
QString paperThemeName(PaperTheme theme);
PaperTheme paperThemeFromName(QStringView name);

} // namespace brulion::drawing
