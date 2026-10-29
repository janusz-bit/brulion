// The SVG file of a drawing: a picture every program shows, with the Excalidraw scene embedded
// as excalidraw.com's "Embed scene" export does (excalidraw.h), so Brulion and excalidraw.com
// open it as a drawing again. Brulion draws the picture itself (toSvg); svgWithScene() embeds a
// scene in a picture another program drew of it (an import keeps Trilium's own picture).
//
//   <svg version="1.1" xmlns="http://www.w3.org/2000/svg" viewBox="0 0 W H" width="W" height="H">
//   <!-- svg-source:excalidraw -->
//   <metadata><!-- payload-type:application/vnd.excalidraw+json --><!-- payload-version:2 -->
//   <!-- payload-start -->BASE64<!-- payload-end --></metadata>
//   [<style>@media (prefers-color-scheme: dark){.p{fill:#121212}.r{…}.k0{fill:#caced8}…}</style>]
//   <rect …paper…/> <path …lines or grid…/> <path d="…" fill="#1f2430"/> …
//   </svg>
//
// The picture covers the elements plus a margin (an empty drawing: EmptyWidth × EmptyHeight,
// pictureRect()); integer width and height on the root (the editor sizes an embed from the root
// alone, picturesize.h). Every element in drawing order from geometry.h's elementShape(): paths
// (fills, hatch lines, outlines with their dashes, arrowheads), `<text>` per line of a text element
// (font-family by Excalidraw's family, fontFamilyCss(); the text XML-escaped,
// xml:space="preserve"), `<image>` for pictures (only base64 data URLs of PNG, JPEG, GIF, WebP or
// SVG from the scene's files; anything else a grey placeholder), `<g transform="rotate(…)">` for
// rotated elements and `<g opacity>` for see-through ones. Plain attributes (no foreignObject):
// the attributes are the picture, and Qt's SVG renderer (SVG Tiny 1.2) draws what browsers draw
// from them (text in the fonts it has).
//
// The picture follows the drawing's theme (Drawing::theme, palette.h): Light — the colours as
// drawn; Dark — the dark theme's colours in the attributes (every program shows it dark); Auto —
// the light attributes plus a style sheet holding nothing but one `@media (prefers-color-scheme:
// dark)` block with the dark colours, so browsers (Obsidian, GitHub, a file manager's preview)
// show it dark when their viewer prefers dark. Qt's SVG renderer ignores that block (it applies
// media rules only for a medium it is given, and drops a style sheet with a media feature query
// whole): Brulion draws its previews itself (the app's DrawingImageProvider). Only colours and
// numbers of our own formatting go into the CSS, never text from a scene; the classes on the
// elements: `p` the paper, `r` the rules, `k<n>` what is filled with ink — a pen stroke, text, a
// filled arrowhead — (one per colour, first use first), `s<n>` what is stroked (outlines, hatch
// lines), `f<n>` a shape's fill, `h<n>` a highlighter stroke (one per opacity in the dark).
#pragma once

#include "drawing/drawing.h"

#include <QByteArray>
#include <QByteArrayView>
#include <QJsonObject>

#include <optional>

namespace brulion::drawing {

/// Room around the strokes in the picture (scene units).
inline constexpr double SvgMargin = 16;
/// The picture of a drawing without strokes.
inline constexpr int EmptyWidth = 320;
inline constexpr int EmptyHeight = 180;

/// The file of a drawing (UTF-8). Foreign elements are kept in the scene but not drawn: write only
/// editable() drawings.
QByteArray toSvg(const Drawing &drawing);
/// The scene area the picture shows (its root's width and height): the elements' bounds plus
/// SvgMargin, out to whole units — 0, 0, EmptyWidth × EmptyHeight for a drawing without elements.
QRectF pictureRect(const Drawing &drawing);

/// `picture` — an SVG another program drew of the scene (Trilium's canvas export, excalidraw.com's
/// export without "Embed scene") — with `drawing`'s scene embedded as excalidraw.com's "Embed
/// scene" does, right after the root's start tag:
///
///   \n<!-- svg-source:excalidraw -->\n<metadata><!-- payload-type:application/vnd.excalidraw+json
///   --><!-- payload-version:2 --><!-- payload-start -->BASE64<!-- payload-end --></metadata>\n
///
/// (the `svg-source` comment only when the picture has none). Every other byte stays as it was (a
/// BOM, the prolog, the picture's own style sheets and fonts) but a scene the picture had already,
/// as Excalidraw writes one (those comments and the base64 between them, nothing else), removed:
/// one scene per file. Brulion and excalidraw.com then open the file as a drawing, every program
/// shows the picture; Brulion writes its own picture (toSvg) when it saves an edit. nullopt — use
/// toSvg() — when the picture has no well-formed prolog and root `<svg …>` start tag
/// (QXmlStreamReader; only the head is read), the root is empty (`<svg/>`, `width` or `height` 0:
/// a picture of an empty scene), its start tag ends beyond HeadBytes − 512
/// (looksLikeExcalidrawSvg() must see the payload in the file's head), the prolog names a payload
/// or the picture has the Excalidraw payload's type in any other place or shape.
std::optional<QByteArray> svgWithScene(QByteArrayView picture, const Drawing &drawing);
/// The same with the scene's JSON as given (UTF-8; e.g. a scene Brulion only shows, which the
/// Drawing model would write with its foreign elements moved above the others).
std::optional<QByteArray> svgWithScene(QByteArrayView picture, QByteArrayView sceneJson);

/// A colour as written into an SVG attribute: "#rgb", "#rrggbb" or "#rrggbbaa" (from a scene of
/// anywhere: anything else would be markup) or `fallback`.
QString safeColor(const QString &color, const QString &fallback);
/// The data URL of the picture `fileId` in a scene's files when it is one Brulion draws: a base64
/// PNG, JPEG, GIF, WebP or SVG ("" anything else — a picture's placeholder is drawn instead).
QString safeDataUrl(const QJsonObject &files, const QString &fileId);
/// The CSS font-family of Excalidraw's font family (1 Virgil, 2 Helvetica, 3 Cascadia,
/// 5 Excalifont, 6 Nunito, 7 Lilita One, 8 Comic Shanns, 9 Liberation Sans; others sans-serif) —
/// constants, never text from a scene.
QString fontFamilyCss(int family);
/// Excalidraw's monospace families (3 Cascadia, 8 Comic Shanns): Brulion draws them in its
/// monospace font, all others in its UI font.
bool isMonospaceFamily(int family);
/// The placeholder of a picture without its file: a light grey box with a darker edge.
inline constexpr QStringView ImagePlaceholderFill = u"#e9ecef";
inline constexpr QStringView ImagePlaceholderEdge = u"#adb5bd";

} // namespace brulion::drawing
