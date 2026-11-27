#pragma once

// Vector icons (docs/ATRAMENT.md "Vector paths and icons"): a monochrome icon read from a subset
// of SVG — what Brulion's icons (Lucide's, assets/icons: BIcon's) are made of — as vector paths
// (scene/vectorpath.h) filled and stroked in one colour, the caller's.
//
// The subset: <svg> with its viewBox (else width × height, else 24 × 24), <g>, and the shapes
// <path d>, <rect x y width height rx ry>, <circle cx cy r>, <ellipse cx cy rx ry>, <line x1 y1 x2
// y2>, <polyline points>, <polygon points>; the presentation attributes fill, stroke,
// stroke-width, stroke-linecap, stroke-linejoin, stroke-miterlimit, inherited from the elements
// around (any paint but `none` paints: the icon has one colour). Path data: every command of SVG
// 1.1 (M L H V C S Q T A Z, absolute and relative, implicit repeats, flags without separators).
// Anything else — <defs>, <clipPath>, <mask>, gradients, transforms, text — is skipped; malformed
// input gives the shapes read before the mistake (or nothing), never a crash. Input is bounded
// (kMaxSvgBytes, kMaxPathCommands).
//
// Drawing (Window::paintIcon): the viewBox fitted into a square, centred; the outlines for one
// size in device px are made once and kept (`outline()`, the last few sizes of an icon: a frame
// shows an icon at one or two), so a frame copies points instead of flattening and stroking again.

#include "base/geometry.h"
#include "scene/scene.h"
#include "scene/vectorpath.h"

#include <cstddef>
#include <memory>
#include <string_view>
#include <utility>
#include <vector>

namespace brulion::atrament {

class VectorIcon {
public:
    static constexpr std::size_t kMaxSvgBytes = 256 * 1024;
    static constexpr std::size_t kMaxPathCommands = 20000;

    struct Shape {
        VectorPath path;
        bool filled = false;
        bool stroked = false;
        StrokeStyle stroke; // in the viewBox's units
    };

    // The icon of an SVG document's text (UTF-8); nullptr when it holds no <svg> or nothing to draw.
    static std::shared_ptr<const VectorIcon> parseSvg(std::string_view svg);

    Rect viewBox() const { return m_viewBox; }
    const std::vector<Shape> &shapes() const { return m_shapes; }

    // The icon fitted into a square of `side` device px with its top-left at the origin: what is
    // filled and what is stroked, as path primitives in device px (colour and mask unset; either
    // may have no points). Main thread only (the cache is the icon's).
    struct Outline {
        PathPrimitive fill;
        PathPrimitive stroke;
    };
    std::shared_ptr<const Outline> outline(float side) const;

private:
    Rect m_viewBox;
    std::vector<Shape> m_shapes;
    mutable std::vector<std::pair<float, std::shared_ptr<const Outline>>> m_outlines; // most recent last
};

// Path data (`d`) into `path`, in its units; false when it stopped at a mistake (what came before
// it stays, as SVG renders a path up to its first error).
bool parseSvgPathData(std::string_view d, VectorPath &path);

} // namespace brulion::atrament
