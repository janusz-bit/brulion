// The SVG file of a drawing: a picture every program shows, with the Excalidraw scene embedded
// as excalidraw.com's "Embed scene" export does (excalidraw.h), so Brulion and excalidraw.com
// open it as a drawing again.
//
//   <svg version="1.1" xmlns="http://www.w3.org/2000/svg" viewBox="0 0 W H" width="W" height="H">
//   <!-- svg-source:excalidraw -->
//   <metadata><!-- payload-type:application/vnd.excalidraw+json --><!-- payload-version:2 -->
//   <!-- payload-start -->BASE64<!-- payload-end --></metadata>
//   [<style>@media (prefers-color-scheme: dark){.p{fill:#121212}.r{…}.k0{fill:#caced8}…}</style>]
//   <rect …paper…/> <path …lines or grid…/> <path d="…" fill="#1f2430"/> …
//   </svg>
//
// The picture covers the strokes plus a margin (an empty drawing: EmptyWidth × EmptyHeight,
// pictureRect()); integer width and height on the root (the editor sizes an embed from the root
// alone, picturesize.h). Only rect and path elements with plain attributes (no fonts or
// foreignObject): the attributes are the picture, and Qt's SVG renderer (SVG Tiny 1.2) draws
// exactly what browsers draw from them.
//
// The picture follows the drawing's theme (Drawing::theme, palette.h): Light — the colours as
// drawn; Dark — the dark theme's colours in the attributes (every program shows it dark); Auto —
// the light attributes plus a style sheet holding nothing but one `@media (prefers-color-scheme:
// dark)` block with the dark colours, so browsers (Obsidian, GitHub, a file manager's preview)
// show it dark when their viewer prefers dark. Qt's SVG renderer ignores that block (it applies
// media rules only for a medium it is given, and drops a style sheet with a media feature query
// whole): Brulion draws its previews itself (the app's DrawingImageProvider). Only colours and
// numbers of our own formatting go into the CSS, never text from a scene; the classes on the
// elements: `p` the paper, `r` the rules, `k<n>` a pen stroke (one per colour, first use first),
// `h<n>` a highlighter stroke (one per opacity in the dark).
#pragma once

#include "drawing/drawing.h"

#include <QByteArray>

namespace brulion::drawing {

/// Room around the strokes in the picture (scene units).
inline constexpr double SvgMargin = 16;
/// The picture of a drawing without strokes.
inline constexpr int EmptyWidth = 320;
inline constexpr int EmptyHeight = 180;

/// The file of a drawing (UTF-8, ASCII in practice). Foreign elements are kept in the scene but
/// not drawn: write only editable() drawings.
QByteArray toSvg(const Drawing &drawing);
/// The scene area the picture shows (its root's width and height): the strokes' bounds plus
/// SvgMargin, out to whole units — 0, 0, EmptyWidth × EmptyHeight for a drawing without strokes.
QRectF pictureRect(const Drawing &drawing);

/// A colour as written into an SVG attribute: "#rgb", "#rrggbb" or "#rrggbbaa" (from a scene of
/// anywhere: anything else would be markup) or `fallback`.
QString safeColor(const QString &color, const QString &fallback);

} // namespace brulion::drawing
