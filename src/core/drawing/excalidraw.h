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

/// The drawing in a scene's JSON (UTF-8). Elements of other types, and `freedraw` elements Brulion
/// cannot draw as they are (rotated, filled, broken points), go to Drawing::foreign; deleted
/// elements are dropped (as Excalidraw's own export does). nullopt: no Excalidraw scene, or one
/// beyond MaxSceneBytes or MaxSceneContainers.
std::optional<Drawing> parseScene(QByteArrayView json);
/// The scene's JSON (UTF-8, compact): the strokes (each stroke read from a scene as it was, new
/// ones as Excalidraw writes them), the foreign elements after them, the appState with the paper
/// and the background, the files and the other keys as they were read.
QByteArray sceneJson(const Drawing &drawing);
/// A new stroke as an Excalidraw `freedraw` element (stroke.element is ignored).
QJsonObject strokeElement(const Stroke &stroke);

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
