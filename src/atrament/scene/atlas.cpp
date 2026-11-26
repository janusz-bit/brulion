#include "scene/atlas.h"

#include <algorithm>
#include <cstring>

namespace brulion::atrament {

namespace {
// One texel of empty space around every tile, so bilinear sampling never bleeds a neighbour in.
constexpr int kPadding = 1;
} // namespace

Atlas::Atlas(int pageSize)
    : m_pageSize(std::clamp(pageSize, 64, 8192))
{
}

std::optional<AtlasTile> Atlas::find(std::uint64_t key) const
{
    auto it = m_tiles.find(key);
    if (it == m_tiles.end())
        return std::nullopt;
    it->second.lastUsed = m_frame;
    return it->second.tile;
}

std::optional<AtlasTile> Atlas::getOrInsert(std::uint64_t key, AtlasKind kind,
        const std::function<std::optional<AtlasBitmap>()> &make)
{
    if (auto found = find(key))
        return found;
    std::optional<AtlasBitmap> bitmap = make();
    if (!bitmap)
        return std::nullopt;
    AtlasTile tile;
    tile.kind = kind;
    if (bitmap->width > 0 && bitmap->height > 0) {
        const int bpp = kind == AtlasKind::Alpha ? 1 : 4;
        if (bitmap->bytesPerPixel != bpp
                || bitmap->pixels.size() < std::size_t(bitmap->width) * std::size_t(bitmap->height) * std::size_t(bpp))
            return std::nullopt;
        std::optional<AtlasTile> placed = allocate(kind, bitmap->width, bitmap->height);
        if (!placed)
            return std::nullopt;
        tile = *placed;
        blit(tile, *bitmap);
        ++m_states[tile.page].liveTiles;
    }
    m_tiles[key] = Entry { tile, m_frame };
    return tile;
}

std::optional<AtlasTile> Atlas::allocate(AtlasKind kind, int width, int height)
{
    if (width + 2 * kPadding > m_pageSize || height + 2 * kPadding > m_pageSize) {
        // Larger than a page: a page of its own, sized for it.
        AtlasPage page;
        page.kind = kind;
        page.width = width + 2 * kPadding;
        page.height = height + 2 * kPadding;
        page.pixels.assign(std::size_t(page.width) * std::size_t(page.height) * std::size_t(page.bytesPerPixel()), 0);
        m_pages.push_back(std::move(page));
        PageState state;
        state.nextShelfY = m_pages.back().height; // full
        m_states.push_back(state);
        AtlasTile tile;
        tile.page = std::uint32_t(m_pages.size() - 1);
        tile.kind = kind;
        tile.x = kPadding;
        tile.y = kPadding;
        tile.width = width;
        tile.height = height;
        return tile;
    }
    for (std::uint32_t i = 0; i < m_pages.size(); ++i) {
        if (m_pages[i].kind != kind)
            continue;
        if (auto tile = allocateIn(i, width, height))
            return tile;
    }
    AtlasPage page;
    page.kind = kind;
    page.width = m_pageSize;
    page.height = m_pageSize;
    page.pixels.assign(std::size_t(m_pageSize) * std::size_t(m_pageSize) * std::size_t(page.bytesPerPixel()), 0);
    m_pages.push_back(std::move(page));
    m_states.emplace_back();
    return allocateIn(std::uint32_t(m_pages.size() - 1), width, height);
}

std::optional<AtlasTile> Atlas::allocateIn(std::uint32_t pageIndex, int width, int height)
{
    AtlasPage &page = m_pages[pageIndex];
    PageState &state = m_states[pageIndex];
    const int w = width + 2 * kPadding;
    const int h = height + 2 * kPadding;
    // The shelf that wastes the least height.
    Shelf *best = nullptr;
    for (Shelf &shelf : state.shelves) {
        if (shelf.height >= h && shelf.x + w <= page.width && shelf.height <= h * 2
                && (!best || shelf.height < best->height))
            best = &shelf;
    }
    if (!best) {
        if (state.nextShelfY + h > page.height)
            return std::nullopt;
        state.shelves.push_back(Shelf { state.nextShelfY, h, 0 });
        state.nextShelfY += h;
        best = &state.shelves.back();
    }
    AtlasTile tile;
    tile.page = pageIndex;
    tile.kind = page.kind;
    tile.x = best->x + kPadding;
    tile.y = best->y + kPadding;
    tile.width = width;
    tile.height = height;
    best->x += w;
    return tile;
}

void Atlas::blit(const AtlasTile &tile, const AtlasBitmap &bitmap)
{
    AtlasPage &page = m_pages[tile.page];
    const int bpp = page.bytesPerPixel();
    for (int y = 0; y < tile.height; ++y) {
        std::uint8_t *dst = page.pixels.data()
                + (std::size_t(tile.y + y) * std::size_t(page.width) + std::size_t(tile.x)) * std::size_t(bpp);
        const std::uint8_t *src = bitmap.pixels.data() + std::size_t(y) * std::size_t(tile.width) * std::size_t(bpp);
        std::memcpy(dst, src, std::size_t(tile.width) * std::size_t(bpp));
    }
    ++page.revision;
    const Rect r = Rect::fromXYWH(float(tile.x - kPadding), float(tile.y - kPadding), float(tile.width + 2 * kPadding),
            float(tile.height + 2 * kPadding));
    page.dirty = page.dirty.united(r);
}

void Atlas::markClean(std::uint32_t page)
{
    if (page < m_pages.size()) {
        m_pages[page].dirty = Rect {};
        m_pages[page].cleanRevision = m_pages[page].revision;
    }
}

void Atlas::collectGarbage(std::uint32_t maxIdleFrames)
{
    for (auto it = m_tiles.begin(); it != m_tiles.end();) {
        if (m_frame - it->second.lastUsed > maxIdleFrames) {
            const AtlasTile &tile = it->second.tile;
            if (!tile.isEmpty() && tile.page < m_states.size())
                --m_states[tile.page].liveTiles;
            it = m_tiles.erase(it);
        } else {
            ++it;
        }
    }
    // A page left without tiles is cleared and packed again from the top.
    for (std::uint32_t i = 0; i < m_pages.size(); ++i) {
        PageState &state = m_states[i];
        if (state.liveTiles == 0 && (state.nextShelfY > 0 || !state.shelves.empty())) {
            AtlasPage &page = m_pages[i];
            const bool ownPage = page.width != m_pageSize || page.height != m_pageSize;
            std::fill(page.pixels.begin(), page.pixels.end(), 0);
            state.shelves.clear();
            state.nextShelfY = ownPage ? page.height : 0;
            if (ownPage) {
                // A page made for one large tile is not reused: free its memory.
                page.pixels.clear();
                page.pixels.shrink_to_fit();
                page.width = page.height = 0;
            }
            ++page.revision;
            page.dirty = Rect::fromXYWH(0, 0, float(page.width), float(page.height));
        }
    }
}

std::size_t Atlas::bytes() const
{
    std::size_t total = 0;
    for (const AtlasPage &page : m_pages)
        total += page.pixels.size();
    return total;
}

} // namespace brulion::atrament
