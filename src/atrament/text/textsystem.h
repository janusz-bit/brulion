#pragma once

// The text system (docs/ATRAMENT.md "Text"): fonts, a cache of paragraph layouts and the glyph
// rasterizer that fills the atlas. One per App; the main thread only.
//
// Glyphs are rasterized from HarfBuzz's outlines (hb_font_draw_glyph) with the coverage
// rasterizer (base/coverage.h) at the device size, unhinted, with the pen's x position's
// fraction quantized to quarters of a pixel (four variants of a glyph at most), as Zed does.
// Each tile's bitmap is placed relative to the pen position rounded down in x and to the nearest
// pixel in y.
//
// Layouts are cached by (text, runs, paragraph style); the cache is generational: what a frame
// used survives `endFrame()`, the rest of the previous generation is dropped.

#include "scene/atlas.h"
#include "text/fonts.h"
#include "text/textlayout.h"

#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string_view>

namespace brulion::atrament {

struct GlyphImage {
    AtlasTile tile;
    // The bitmap's top-left relative to the pen (whole device pixels; top is negative above the
    // baseline).
    int left = 0;
    int top = 0;
    bool color = false; // a Color tile (colour emoji); else Alpha
};

class TextSystem {
public:
    explicit TextSystem(std::shared_ptr<FontCollection> fonts);
    ~TextSystem();

    FontCollection &fonts() { return *m_fonts; }
    const FontCollection &fonts() const { return *m_fonts; }

    std::shared_ptr<const TextLayout> layout(std::u16string_view text, std::span<const FontRun> runs,
            const ParagraphStyle &style);

    // The tile of a glyph at `devicePixelSize` (the font size × the scale factor) and subpixel
    // variant `subpixelX` (0–3: the pen's x fraction in quarters). Empty glyphs (a space) give an
    // empty tile.
    std::optional<GlyphImage> glyph(Atlas &atlas, FaceId face, std::uint32_t glyph, float devicePixelSize,
            int subpixelX);

    // Ends a frame of the layout cache.
    void endFrame();
    std::size_t cachedLayouts() const;

private:
    struct Impl;
    std::shared_ptr<FontCollection> m_fonts;
    std::unique_ptr<Impl> d;
};

// The coverage bitmap of one glyph (exposed for tests): A8, `left`/`top` as in GlyphImage.
struct GlyphBitmap {
    int left = 0;
    int top = 0;
    int width = 0;
    int height = 0;
    std::vector<std::uint8_t> coverage;
};
GlyphBitmap rasterizeGlyph(const FontCollection &fonts, FaceId face, std::uint32_t glyph, float devicePixelSize,
        float subpixelOffsetX);

} // namespace brulion::atrament
