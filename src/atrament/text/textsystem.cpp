#include "text/textsystem.h"

#include "base/coverage.h"
#include "base/hash.h"

#include <hb.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <unordered_map>

namespace brulion::atrament {

namespace {

// Glyph sizes are quantized to 1/16 px (a cache key, and the size actually drawn).
constexpr float kSizeSteps = 16;
// Larger glyphs are not rasterized (a 4096 px glyph would be a 16 MB bitmap).
constexpr float kMaxGlyphPixels = 2048;
constexpr std::size_t kMaxPlacements = 1 << 16;
constexpr std::uint64_t kGlyphKeyTag = 0x61747261676c7970ull; // "atraglyp"

// An outline recorded from HarfBuzz in font units (y up), with the bounds of its points.
struct Outline {
    enum class Op : std::uint8_t { Move, Line, Quad, Cubic, Close };
    struct Command {
        Op op;
        Point p[3];
    };
    std::vector<Command> commands;
    float minX = 0, minY = 0, maxX = 0, maxY = 0;
    bool empty = true;

    void add(Op op, std::initializer_list<Point> points)
    {
        Command c{op, {}};
        int i = 0;
        for (const Point &p : points) {
            c.p[i++] = p;
            if (empty) {
                minX = maxX = p.x;
                minY = maxY = p.y;
                empty = false;
            } else {
                minX = std::min(minX, p.x);
                maxX = std::max(maxX, p.x);
                minY = std::min(minY, p.y);
                maxY = std::max(maxY, p.y);
            }
        }
        commands.push_back(c);
    }
};

Outline &outlineOf(void *data)
{
    return *static_cast<Outline *>(data);
}

hb_draw_funcs_t *drawFuncs()
{
    static hb_draw_funcs_t *const funcs = [] {
        hb_draw_funcs_t *f = hb_draw_funcs_create();
        hb_draw_funcs_set_move_to_func(
                f,
                [](hb_draw_funcs_t *, void *data, hb_draw_state_t *, float x, float y, void *) {
                    outlineOf(data).add(Outline::Op::Move, {{x, y}});
                },
                nullptr, nullptr);
        hb_draw_funcs_set_line_to_func(
                f,
                [](hb_draw_funcs_t *, void *data, hb_draw_state_t *, float x, float y, void *) {
                    outlineOf(data).add(Outline::Op::Line, {{x, y}});
                },
                nullptr, nullptr);
        hb_draw_funcs_set_quadratic_to_func(
                f,
                [](hb_draw_funcs_t *, void *data, hb_draw_state_t *, float cx, float cy, float x, float y, void *) {
                    outlineOf(data).add(Outline::Op::Quad, {{cx, cy}, {x, y}});
                },
                nullptr, nullptr);
        hb_draw_funcs_set_cubic_to_func(
                f,
                [](hb_draw_funcs_t *, void *data, hb_draw_state_t *, float c1x, float c1y, float c2x, float c2y,
                   float x, float y,
                   void *) { outlineOf(data).add(Outline::Op::Cubic, {{c1x, c1y}, {c2x, c2y}, {x, y}}); },
                nullptr, nullptr);
        hb_draw_funcs_set_close_path_func(
                f,
                [](hb_draw_funcs_t *, void *data, hb_draw_state_t *, void *) {
                    Outline &o = outlineOf(data);
                    o.commands.push_back({Outline::Op::Close, {}});
                },
                nullptr, nullptr);
        hb_draw_funcs_make_immutable(f);
        return f;
    }();
    return funcs;
}

float quantizedSize(float devicePixelSize)
{
    return std::round(devicePixelSize * kSizeSteps) / kSizeSteps;
}

bool sameRuns(std::span<const FontRun> a, const std::vector<FontRun> &b)
{
    if (a.size() != b.size())
        return false;
    for (std::size_t i = 0; i < a.size(); ++i) {
        const FontRun &x = a[i];
        const FontRun &y = b[i];
        if (x.length != y.length || x.face != y.face || x.styleIndex != y.styleIndex || x.features != y.features
            || x.language != y.language || std::memcmp(&x.fontSize, &y.fontSize, sizeof(float)) != 0)
            return false;
    }
    return true;
}

std::uint64_t hashRuns(std::u16string_view text, std::span<const FontRun> runs)
{
    std::uint64_t h = hashString(text);
    h = hashCombine(h, runs.size());
    for (const FontRun &run : runs) {
        h = hashCombine(h, (std::uint64_t(std::uint32_t(run.length)) << 32) | run.face);
        h = hashFloat(run.fontSize, h);
        h = hashCombine(h, run.styleIndex);
        h = hashString(run.features, h);
        h = hashCombine(h, run.features.size());
        h = hashString(run.language, h);
    }
    return h;
}

std::uint64_t hashStyle(const ParagraphStyle &style, std::uint64_t h)
{
    h = hashCombine(h, style.wrapWidth.has_value());
    if (style.wrapWidth)
        h = hashFloat(*style.wrapWidth, h);
    return hashFloat(style.lineHeight, h);
}

bool sameStyle(const ParagraphStyle &a, const ParagraphStyle &b)
{
    auto same = [](float x, float y) { return x == y || (std::isnan(x) && std::isnan(y)); };
    return a.wrapWidth.has_value() == b.wrapWidth.has_value() && (!a.wrapWidth || same(*a.wrapWidth, *b.wrapWidth))
           && same(a.lineHeight, b.lineHeight);
}

} // namespace

GlyphBitmap rasterizeGlyph(const FontCollection &fonts, FaceId face, std::uint32_t glyph, float devicePixelSize,
                           float subpixelOffsetX)
{
    GlyphBitmap out;
    hb_font_t *font = fonts.hbFont(face);
    if (!font || !std::isfinite(devicePixelSize) || devicePixelSize <= 0 || devicePixelSize > kMaxGlyphPixels
        || !std::isfinite(subpixelOffsetX))
        return out;
    Outline outline;
    hb_font_draw_glyph(font, glyph, drawFuncs(), &outline);
    if (outline.empty)
        return out;
    const float scale = devicePixelSize / float(std::max(1, fonts.unitsPerEm(face)));
    // Device pixels, y down, relative to the pen (rounded down in x by the caller, which passes
    // the fraction as the subpixel offset).
    const int left = int(std::floor(outline.minX * scale + subpixelOffsetX));
    const int right = int(std::ceil(outline.maxX * scale + subpixelOffsetX));
    const int top = int(std::floor(-outline.maxY * scale));
    const int bottom = int(std::ceil(-outline.minY * scale));
    const int width = right - left;
    const int height = bottom - top;
    if (width <= 0 || height <= 0 || width > 4 * int(kMaxGlyphPixels) || height > 4 * int(kMaxGlyphPixels))
        return out;
    auto map = [&](Point p) { return Point{p.x * scale + subpixelOffsetX - float(left), -p.y * scale - float(top)}; };
    CoverageRasterizer raster;
    raster.reset(width, height);
    for (const Outline::Command &c : outline.commands) {
        switch (c.op) {
        case Outline::Op::Move: raster.moveTo(map(c.p[0])); break;
        case Outline::Op::Line: raster.lineTo(map(c.p[0])); break;
        case Outline::Op::Quad: raster.quadTo(map(c.p[0]), map(c.p[1])); break;
        case Outline::Op::Cubic: raster.cubicTo(map(c.p[0]), map(c.p[1]), map(c.p[2])); break;
        case Outline::Op::Close: raster.close(); break;
        }
    }
    raster.close();
    out.left = left;
    out.top = top;
    out.width = width;
    out.height = height;
    out.coverage.resize(std::size_t(width) * std::size_t(height));
    raster.accumulate(out.coverage.data(), width);
    return out;
}

struct TextSystem::Impl {
    struct Entry {
        std::shared_ptr<const TextLayout> layout;
        std::vector<FontRun> runs;
        ParagraphStyle style;
    };
    using Map = std::unordered_map<std::uint64_t, std::shared_ptr<Entry>>;
    // GPUI's two generations: what this frame used, and what the previous one did.
    Map current;
    Map previous;
    // The same by text and runs only: a layout to rewrap at another width without shaping.
    Map shapedCurrent;
    Map shapedPrevious;

    struct Placement {
        int left = 0;
        int top = 0;
    };
    // The atlas keeps tiles, not where they go relative to the pen.
    std::unordered_map<std::uint64_t, Placement> placements;
};

TextSystem::TextSystem(std::shared_ptr<FontCollection> fonts)
    : m_fonts(fonts ? std::move(fonts) : std::make_shared<FontCollection>()), d(std::make_unique<Impl>())
{ }

TextSystem::~TextSystem() = default;

std::shared_ptr<const TextLayout> TextSystem::layout(std::u16string_view text, std::span<const FontRun> runs,
                                                     const ParagraphStyle &style)
{
    const std::uint64_t shapedKey = hashRuns(text, runs);
    const std::uint64_t key = hashStyle(style, shapedKey);
    auto matches = [&](const Impl::Entry &e) {
        return e.layout->text() == text && sameRuns(runs, e.runs) && sameStyle(e.style, style);
    };
    if (auto it = d->current.find(key); it != d->current.end() && matches(*it->second))
        return it->second->layout;
    if (auto it = d->previous.find(key); it != d->previous.end() && matches(*it->second)) {
        std::shared_ptr<Impl::Entry> entry = std::move(it->second);
        d->previous.erase(it);
        d->current[key] = entry;
        d->shapedCurrent[shapedKey] = entry;
        return entry->layout;
    }
    std::shared_ptr<const TextLayout> shaped;
    for (Impl::Map *map : {&d->shapedCurrent, &d->shapedPrevious}) {
        auto it = map->find(shapedKey);
        if (it != map->end() && it->second->layout->text() == text && sameRuns(runs, it->second->runs)) {
            shaped = it->second->layout;
            break;
        }
    }
    auto entry = std::make_shared<Impl::Entry>();
    entry->layout = std::make_shared<const TextLayout>(shaped ? rewrapParagraph(*m_fonts, *shaped, runs, style)
                                                              : layoutParagraph(*m_fonts, text, runs, style));
    entry->runs.assign(runs.begin(), runs.end());
    entry->style = style;
    d->current[key] = entry;
    d->shapedCurrent[shapedKey] = entry;
    return entry->layout;
}

std::optional<GlyphImage> TextSystem::glyph(Atlas &atlas, FaceId face, std::uint32_t glyph, float devicePixelSize,
                                            int subpixelX)
{
    if (face >= m_fonts->faceCount())
        return std::nullopt;
    if (!std::isfinite(devicePixelSize) || devicePixelSize <= 0)
        return GlyphImage{}; // nothing to draw
    const float size = quantizedSize(std::min(devicePixelSize, kMaxGlyphPixels));
    subpixelX = std::clamp(subpixelX, 0, 3);
    std::uint64_t key = hashCombine(kGlyphKeyTag, face);
    key = hashCombine(key, glyph);
    key = hashCombine(key, std::uint64_t(size * kSizeSteps));
    key = hashCombine(key, std::uint64_t(subpixelX));
    const float offset = float(subpixelX) / 4;
    std::optional<Impl::Placement> made;
    const std::optional<AtlasTile> tile = atlas.getOrInsert(key, AtlasKind::Alpha, [&]() -> std::optional<AtlasBitmap> {
        GlyphBitmap bitmap = rasterizeGlyph(*m_fonts, face, glyph, size, offset);
        made = Impl::Placement{bitmap.left, bitmap.top};
        AtlasBitmap out;
        out.width = bitmap.width;
        out.height = bitmap.height;
        out.bytesPerPixel = 1;
        out.pixels = std::move(bitmap.coverage);
        return out;
    });
    if (!tile)
        return std::nullopt;
    Impl::Placement placement;
    if (made) {
        placement = *made;
        d->placements[key] = placement;
    } else if (auto it = d->placements.find(key); it != d->placements.end()) {
        placement = it->second;
    } else {
        // The tile came from another text system sharing the atlas: measure it again.
        const GlyphBitmap bitmap = rasterizeGlyph(*m_fonts, face, glyph, size, offset);
        placement = {bitmap.left, bitmap.top};
        d->placements[key] = placement;
    }
    GlyphImage image;
    image.tile = *tile;
    image.left = placement.left;
    image.top = placement.top;
    image.color = tile->kind == AtlasKind::Color;
    return image;
}

void TextSystem::endFrame()
{
    // Placements are measured again when needed: a bound on what glyphs long gone leave behind.
    if (d->placements.size() > kMaxPlacements)
        d->placements.clear();
    d->previous = std::move(d->current);
    d->current.clear();
    d->shapedPrevious = std::move(d->shapedCurrent);
    d->shapedCurrent.clear();
}

std::size_t TextSystem::cachedLayouts() const
{
    return d->current.size() + d->previous.size();
}

} // namespace brulion::atrament
