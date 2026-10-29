// The Excalidraw format of drawings (https://excalidraw.com, MIT; the format as its
// packages/excalidraw/data/encode.ts and scene/export.ts write it):
//
//  * the scene: JSON `{ "type": "excalidraw", "version": 2, "source", "elements": [...],
//    "appState": {...}, "files": {...} }` — what a `.excalidraw` file holds. A freehand stroke is a
//    `freedraw` element (`x`, `y`, `points` relative to them, `pressures`, `simulatePressure`,
//    `strokeColor`, `strokeWidth`, `opacity`, …);
//  * the scene embedded in an SVG ("Export → SVG → Embed scene"):
//        <!-- payload-type:application/vnd.excalidraw+json --><!-- payload-version:2 -->
//        <!-- payload-start -->BASE64<!-- payload-end -->
//    BASE64 is base64 of the JSON `{"version":"1","encoding":"bstring","compressed":true,
//    "encoded":"…"}` written as Latin-1 bytes, where `encoded` holds the zlib-compressed (pako
//    deflate) scene as one character per byte; `compressed: false` holds the UTF-8 scene the same
//    way. Payload version 1 (old exports): base64 of the UTF-8 JSON, either that wrapper or the
//    scene itself.
#pragma once

#include "drawing/drawing.h"

#include <QByteArray>
#include <QByteArrayView>
#include <QHash>

#include <optional>

namespace brulion::drawing {

/// Largest scene read (decompressed), and largest payload looked at — a drawing is far smaller
/// (pictures pasted into an Excalidraw scene are its `files`, a few MB each); more is a broken or
/// hostile file. A small file may inflate to this much (zlib reaches 1000:1).
inline constexpr qsizetype MaxSceneBytes = 32 * 1024 * 1024;
/// Most JSON arrays and objects a scene may have — every point of a stroke is an array, every
/// element an object: half a million is some thirty pages of dense handwriting. The parsed JSON
/// takes some 200 bytes per container, so a scene of nothing but `[0,0]` within MaxSceneBytes
/// would take gigabytes; such a scene is refused before it is parsed.
inline constexpr qsizetype MaxSceneContainers = 500'000;

/// The scene's `source` Brulion writes.
inline constexpr QStringView SceneSource = u"https://github.com/janusz-bit/brulion";

/// The drawing in a scene's JSON (UTF-8). Elements of other kinds (frames, embedded pages,
/// unknown ones) and elements Brulion cannot draw as they are (broken numbers or points, beyond
/// CoordinateLimit) go to Drawing::foreign; deleted elements are dropped (as Excalidraw's own
/// export does). nullopt: no Excalidraw scene, or one beyond MaxSceneBytes or MaxSceneContainers.
std::optional<Drawing> parseScene(QByteArrayView json);
/// The first half of parseScene(): the scene's root object (`type` "excalidraw", `elements` an
/// array when present), within MaxSceneBytes and MaxSceneContainers; nullopt otherwise.
std::optional<QJsonObject> sceneRoot(QByteArrayView json);
/// The second half: the drawing in a scene's root object (deleted elements dropped).
std::optional<Drawing> sceneFromObject(const QJsonObject &root);
/// The scene sceneJson() writes, as an object (the Excalidraw plugin's files and `.excalidraw`
/// files write it with their own layout: excalidrawmd.h stringifyJson()).
QJsonObject sceneObject(const Drawing &drawing);
/// The scene's JSON (UTF-8, compact): the elements in their order (each one read from a scene and
/// not changed since as it was read, the others as Excalidraw writes them: elementJson()), the
/// foreign elements after them, the appState with the paper, the background and the theme
/// (`brulionTheme`, left out for Auto), the files the pictures use and the other keys as they
/// were read. Excalidraw's fractional `index` values are kept while they still rise in the
/// written order, else left out of every element (Excalidraw makes new ones when it opens the
/// scene).
QByteArray sceneJson(const Drawing &drawing);
/// A new stroke as an Excalidraw `freedraw` element (stroke.element is ignored).
QJsonObject strokeElement(const Stroke &stroke);
/// An Excalidraw element (as excalidraw.com reads it) Brulion draws, or nullopt: the element then
/// stays foreign. Its numbers are checked and clamped (sizes not negative, opacity 0–100,
/// strokeWidth 0.05–200, fontSize 1–1000, lineHeight 0.5–5); `element` keeps it as read.
std::optional<Stroke> elementFromJson(const QJsonObject &e);
/// The element as the scene stores it: `e.element` itself while `e.version` (and its id) are the
/// ones read; else `e.element` (or, for an element made in Brulion, Excalidraw's template of its
/// kind: roughness 0, no bindings, no index) with every field Brulion models written over.
/// `types` (id → kind) names the kinds of the elements in boundElements (bound ones it lacks are
/// left out).
QJsonObject elementJson(const Stroke &e, const QHash<QString, ElementType> *types = nullptr);
/// The scene names of the fill, stroke, arrowhead and alignment styles, and back.
QString fillStyleName(FillStyle style);
QString strokeStyleName(StrokeStyle style);
/// (None: "".)
QString arrowheadName(Arrowhead head);
QString textAlignName(TextAlign align);
QString verticalAlignName(VerticalAlign align);

/// The SVG payload of a scene: the base64 between the payload markers (version 2, compressed).
QByteArray encodePayload(QByteArrayView sceneJson);
/// The scene JSON in a payload's base64 of `version` (1 or 2); nullopt when it cannot be read.
std::optional<QByteArray> decodePayload(QByteArrayView base64, int version);
/// The scene JSON embedded in an SVG (the markers above, whitespace around the base64 allowed);
/// nullopt when there is none or it cannot be read (`corrupt` tells which).
std::optional<QByteArray> svgPayload(QByteArrayView svg, bool *corrupt = nullptr);
/// The start of a file (`head`, e.g. its first 4 KB) is an SVG with an embedded Excalidraw scene:
/// the payload type or Excalidraw's `svg-source` comment is there. A cheap check for lists.
bool looksLikeExcalidrawSvg(QByteArrayView head);
/// How much of a file looksLikeExcalidrawSvg() needs to see.
inline constexpr qsizetype HeadBytes = 4096;

/// zlib (RFC 1950) compression and decompression as pako's deflate/inflate; inflate() gives
/// nullopt for a broken stream or more than `limit` bytes.
QByteArray deflate(QByteArrayView data);
std::optional<QByteArray> inflate(QByteArrayView data, qsizetype limit = MaxSceneBytes);

} // namespace brulion::drawing
