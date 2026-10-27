// The SVG file of a drawing: a picture every program shows, with the Excalidraw scene embedded
// as excalidraw.com's "Embed scene" export does (excalidraw.h), so Brulion and excalidraw.com
// open it as a drawing again.
//
//   <svg version="1.1" xmlns="http://www.w3.org/2000/svg" viewBox="0 0 W H" width="W" height="H">
//   <!-- svg-source:excalidraw -->
//   <metadata><!-- payload-type:application/vnd.excalidraw+json --><!-- payload-version:2 -->
//   <!-- payload-start -->BASE64<!-- payload-end --></metadata>
//   <rect …paper…/> <path …lines or grid…/> <path d="…" fill="#1f2430"/> …
//   </svg>
//
// The picture covers the strokes plus a margin (an empty drawing: EmptyWidth × EmptyHeight);
// integer width and height on the root (the editor sizes an embed from the root alone,
// picturesize.h). Only rect and path elements with plain attributes (no CSS, fonts or
// foreignObject): Qt's SVG renderer (SVG Tiny 1.2) draws exactly what browsers draw.
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

/// A colour as written into an SVG attribute: "#rgb", "#rrggbb" or "#rrggbbaa" (from a scene of
/// anywhere: anything else would be markup) or `fallback`.
QString safeColor(const QString &color, const QString &fallback);

} // namespace brulion::drawing
