#pragma once

// The CPU renderer (docs/ATRAMENT.md D6): draws a Scene into premultiplied RGBA8 pixels. It is
// the reference every GPU renderer is compared with, the renderer of tests and of the first Qt
// host, and the fallback where no GPU works — so it must be exact first and fast second.
//
// Every primitive kind of scene/scene.h, with the shading GPUI's shaders use:
//   Quad              rounded rectangle (per-corner radii, clamped to half the shorter side),
//                     signed-distance anti-aliasing over one pixel; borders of per-side widths
//                     (the inner edge a rounded rectangle inset by the widths, its radii the outer
//                     ones less the adjacent widths); a solid or a linear-gradient fill
//   Shadow            Evan Wallace's blurred rounded rectangle (erf along x, integrated along y),
//                     σ = blurRadius / 2, painted out to 3σ around its bounds
//   MonochromeSprite  the Alpha atlas tile's coverage × the colour, at the sprite's whole-pixel
//                     origin (no scaling: glyphs are rasterized at device size)
//   ColorSprite       the Color atlas tile scaled into the bounds (bilinear), × opacity
//   Picture           the image (or its source rect) scaled into the bounds, bilinear, clipped to
//                     the rounded corners, × opacity, optionally grey
//   PathPrimitive     non-zero fill of the contours (base/coverage.h), anti-aliased
//   Underline         straight (a rectangle `thickness` high at the bottom of the bounds) or wavy
//                     (a sine of period ~4×thickness, amplitude filling the bounds' height)
//
// Each is cut to its content mask (whole pixels: the mask is snapped outwards), the optional
// damage rectangle and the target; colours blend with source-over on premultiplied components in
// sRGB space. Deterministic: the same scene gives the same bytes on every run of a build.
//
// Blending is 8-bit integer arithmetic on the premultiplied bytes (each channel × m / 255, rounded
// exactly): a source is rounded to bytes once, so a pixel is within a level of the exact value.
// Coverage comes from the float formulas above. What a later opaque quad paints over (16×16
// tiles) is not drawn, nor cleared: that changes no byte of the result.

#include "base/color.h"
#include "base/geometry.h"
#include "scene/atlas.h"
#include "scene/scene.h"

#include <cstdint>
#include <optional>
#include <vector>

namespace brulion::atrament {

struct RasterTarget {
    int width = 0;
    int height = 0;
    std::vector<Pixel> pixels; // width × height, row by row (no padding)

    void resize(int w, int h);
    Pixel &at(int x, int y) { return pixels[std::size_t(y) * std::size_t(width) + std::size_t(x)]; }
    Pixel at(int x, int y) const { return pixels[std::size_t(y) * std::size_t(width) + std::size_t(x)]; }
    void fill(Pixel p);
};

struct RasterStats {
    std::size_t primitives = 0;    // drawn (not entirely outside, nor entirely under a later opaque quad)
    std::size_t pixelsTouched = 0; // pixels a primitive was evaluated at
};

class RasterRenderer {
public:
    // Clears `damage` (default: the whole target) to `clear`, then draws every primitive that
    // meets it, in order.
    RasterStats render(const Scene &scene, const Atlas &atlas, RasterTarget &target, Color clear,
            std::optional<Rect> damage = std::nullopt);
};

// Source-over of a premultiplied colour with coverage onto a pixel (exposed for tests).
Pixel blendOver(Pixel dst, PremulColor src, float coverage);

} // namespace brulion::atrament
