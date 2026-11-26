#pragma once

// A scene as the GPU draws it (docs/ATRAMENT.md D5, D12): instances of five shader kinds in
// painter's order, one batch per run of instances of the same kind and texture — the shape of
// GPUI's renderers (zed's crates/gpui: one instanced draw per run of same-kind primitives).
//
// Built on the GUI thread from a Scene, then handed to a renderer (qrhirenderer.h) that may draw
// it on another thread: everything it needs is copied or shared, nothing points into the Scene.
// Qt-free like the rest of Atrament outside qt/ — the instance layouts are what a WebGPU renderer
// of the same scene would upload too.
//
// The CPU renderer (raster/raster.h) is the reference: every value a shader needs is computed here
// with the CPU renderer's arithmetic (radii clamped, the inner edge of a border, gradient lines,
// σ and the erf factors of a shadow, the wave of an underline), so a fragment shader only evaluates
// the per-pixel formula. Content masks cut each instance's rectangle in whole pixels, as the CPU
// renderer cuts each primitive: no fragment runs outside its mask.
//
// Kinds:
//   Quad       quads, straight underlines, shadows without blur
//   Shadow     blurred rounded rectangles
//   Sprite     A8 coverage × a colour: monochrome glyphs, and paths (their coverage rasterized on
//              the CPU into tiles of the builder's own path atlas, cached by content)
//   Image      premultiplied RGBA, bilinear, cut to rounded corners: colour sprites and pictures
//   Underline  wavy underlines

#include "base/coverage.h"
#include "base/geometry.h"
#include "scene/atlas.h"
#include "scene/scene.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

namespace brulion::atrament {

enum class GpuKind : std::uint8_t { Quad, Shadow, Sprite, Image, Underline };

// Every instance starts with the pixels it draws, [x0, x1) × [y0, y1) in device pixels: its
// primitive's reach cut to its content mask (and the target).
struct GpuQuadInstance {
    float rect[4];
    float outer[4];      // the rounded rectangle: centre x, y, half width, half height
    float outerRadii[4]; // top-left, top-right, bottom-right, bottom-left
    float inner[4];      // the border's inner edge (half width or height 0: none, all border)
    float innerRadii[4];
    float fill[4];     // premultiplied, 0–1
    float fill2[4];    // a gradient's end
    float border[4];   // premultiplied; alpha 0: no border
    float gradient[4]; // t = gx·x + gy·y + g0; [3] = 1 for a gradient
};

struct GpuShadowInstance {
    float rect[4];
    float box[4]; // centre x, y, half width, half height
    float radii[4];
    float color[4]; // premultiplied
    float blur[4];  // σ, √½ / σ, 1 / (√(2π) σ), 3σ
};

struct GpuSpriteInstance {
    float rect[4];
    float shift[4]; // texel = pixel − shift (whole numbers); [2], [3] unused
    float color[4]; // premultiplied
};

struct GpuImageInstance {
    float rect[4];
    float shape[4]; // the rounded rectangle it is cut to: centre x, y, half width, half height
    float radii[4];
    float map[4];    // bounds' left, top; texels per pixel along x, y
    float source[4]; // the source rectangle's left, top; opacity; 1 = grey
    float limits[4]; // the texels that may be read: [x0, x1) × [y0, y1)
};

struct GpuUnderlineInstance {
    float rect[4];
    float wave[4];  // left, right, middle line, amplitude
    float wave2[4]; // ω, half the stroke + half a pixel; [2], [3] unused
    float color[4]; // premultiplied
};

// Which texture a batch samples.
struct GpuTexture {
    enum class Source : std::uint8_t { None, Atlas, PathAtlas, Picture };
    Source source = Source::None;
    std::uint32_t page = 0;  // Atlas, PathAtlas
    std::uint64_t image = 0; // Picture: ImageData::id
    bool operator==(const GpuTexture &) const = default;
};

struct GpuBatch {
    GpuKind kind = GpuKind::Quad;
    GpuTexture texture;
    std::size_t offset = 0; // bytes into GpuFrame::instances
    std::uint32_t count = 0;
};

struct GpuFrame {
    int width = 0; // device pixels
    int height = 0;
    std::vector<std::byte> instances; // every batch's instances, batch after batch
    std::vector<GpuBatch> batches;
    // The pictures the batches show, kept alive while the frame is drawn.
    std::vector<std::shared_ptr<const ImageData>> pictures;
    std::size_t primitives = 0; // drawn: not cut away by masks, not transparent
};

// Builds frames; keeps the path atlas (coverage tiles of paths) between them.
class GpuFrameBuilder {
public:
    GpuFrameBuilder();

    // The scene for a target of `width` × `height` device pixels; tiles refer to `atlas`'s pages
    // (GpuTexture::Source::Atlas) and to pathAtlas()'s.
    GpuFrame build(const Scene &scene, const Atlas &atlas, int width, int height);

    Atlas &pathAtlas() { return m_paths; }
    const Atlas &pathAtlas() const { return m_paths; }

private:
    Atlas m_paths;
    CoverageRasterizer m_rasterizer;
    std::uint64_t m_frames = 0;
};

} // namespace brulion::atrament
