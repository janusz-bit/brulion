#pragma once

// The atlas (docs/ATRAMENT.md "Text"): small bitmaps — glyphs, colour glyphs, icons — packed into
// pages that every renderer reads. Kept in CPU memory; a GPU renderer mirrors each page as a
// texture and uploads the regions marked dirty since its last upload.
//
// Two kinds of page: Alpha (A8 coverage, for monochrome glyphs tinted when drawn) and Color
// (premultiplied RGBA8). Tiles are found by a 64-bit key the caller makes (the text system hashes
// face, glyph, size and subpixel offset). Pages are packed in shelves; when the atlas is full a
// new page is added. Eviction: `collectGarbage()` drops tiles not used for a number of frames and
// recycles pages left empty.

#include "base/color.h"
#include "base/geometry.h"

#include <cstdint>
#include <functional>
#include <optional>
#include <unordered_map>
#include <vector>

namespace brulion::atrament {

enum class AtlasKind : std::uint8_t { Alpha, Color };

struct AtlasTile {
    std::uint32_t page = 0; // index into Atlas::pages()
    AtlasKind kind = AtlasKind::Alpha;
    // Position and size of the bitmap in its page, in texels.
    int x = 0;
    int y = 0;
    int width = 0;
    int height = 0;
    bool isEmpty() const { return width <= 0 || height <= 0; }
};

// A bitmap to put into the atlas: A8 (`bytesPerPixel` 1) or premultiplied RGBA8 (4).
struct AtlasBitmap {
    int width = 0;
    int height = 0;
    int bytesPerPixel = 1;
    std::vector<std::uint8_t> pixels; // width × height × bytesPerPixel, tightly packed
};

struct AtlasPage {
    AtlasKind kind = AtlasKind::Alpha;
    int width = 0;
    int height = 0;
    std::vector<std::uint8_t> pixels; // width × height × (1 or 4)
    // Grows with every change: a GPU mirror compares it with what it uploaded.
    std::uint64_t revision = 0;
    // The part changed since markClean() (empty when clean).
    Rect dirty;
    // The revision markClean() last saw: `dirty` covers every change after it. A mirror that
    // uploaded an older revision (another window's renderer cleaned the page since) uploads the
    // whole page instead.
    std::uint64_t cleanRevision = 0;

    int bytesPerPixel() const { return kind == AtlasKind::Alpha ? 1 : 4; }
    const std::uint8_t *row(int y) const { return pixels.data() + std::size_t(y) * std::size_t(width * bytesPerPixel()); }
    std::uint8_t alpha(int x, int y) const { return pixels[std::size_t(y) * std::size_t(width) + std::size_t(x)]; }
    Pixel color(int x, int y) const
    {
        const std::uint8_t *p = pixels.data() + (std::size_t(y) * std::size_t(width) + std::size_t(x)) * 4;
        return { p[0], p[1], p[2], p[3] };
    }
};

class Atlas {
public:
    explicit Atlas(int pageSize = 1024);

    // The tile for a key, made with `make` the first time (an empty bitmap gives an empty tile —
    // e.g. a space — which is remembered too). `make` returns nullopt when it cannot produce the
    // bitmap; nothing is remembered then.
    std::optional<AtlasTile> getOrInsert(std::uint64_t key, AtlasKind kind,
            const std::function<std::optional<AtlasBitmap>()> &make);
    std::optional<AtlasTile> find(std::uint64_t key) const;

    const std::vector<AtlasPage> &pages() const { return m_pages; }
    void markClean(std::uint32_t page);

    // Starts a new frame: tiles looked up from now on count as used in it.
    void beginFrame() { ++m_frame; }
    // Drops tiles unused for `maxIdleFrames` frames; their pages are repacked when they empty.
    void collectGarbage(std::uint32_t maxIdleFrames);

    std::size_t tileCount() const { return m_tiles.size(); }
    std::size_t bytes() const;

private:
    struct Shelf {
        int y = 0;
        int height = 0;
        int x = 0; // next free x
    };
    struct PageState {
        std::vector<Shelf> shelves;
        int nextShelfY = 0;
        int liveTiles = 0;
    };
    struct Entry {
        AtlasTile tile;
        std::uint64_t lastUsed = 0;
    };
    std::optional<AtlasTile> allocate(AtlasKind kind, int width, int height);
    std::optional<AtlasTile> allocateIn(std::uint32_t page, int width, int height);
    void blit(const AtlasTile &tile, const AtlasBitmap &bitmap);

    int m_pageSize;
    std::vector<AtlasPage> m_pages;
    std::vector<PageState> m_states;
    mutable std::unordered_map<std::uint64_t, Entry> m_tiles;
    std::uint64_t m_frame = 0;
};

} // namespace brulion::atrament
