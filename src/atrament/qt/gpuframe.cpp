#include "gpuframe.h"

#include "base/hash.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <numbers>
#include <optional>
#include <variant>

namespace brulion::atrament {

namespace {

// ---- The CPU renderer's arithmetic (raster/raster.cpp), so both draw the same pixels ----------

struct IRect {
    int x0 = 0;
    int y0 = 0;
    int x1 = 0;
    int y1 = 0;
    bool isEmpty() const { return x0 >= x1 || y0 >= y1; }
};

inline int floorIn(float v, int lo, int hi)
{
    if (!(v > float(lo)))
        return lo;
    if (!(v < float(hi)))
        return hi;
    const int i = int(v);
    return float(i) > v ? i - 1 : i;
}

inline int ceilIn(float v, int lo, int hi)
{
    if (!(v > float(lo)))
        return lo;
    if (!(v < float(hi)))
        return hi;
    const int i = int(v);
    return float(i) < v ? i + 1 : i;
}

// The whole pixels a rectangle touches, cut to `clip`.
inline IRect pixelsCovering(const Rect &r, const IRect &clip)
{
    if (r.isEmpty())
        return {};
    IRect p{floorIn(r.left(), clip.x0, clip.x1), floorIn(r.top(), clip.y0, clip.y1),
            ceilIn(r.right(), clip.x0, clip.x1), ceilIn(r.bottom(), clip.y0, clip.y1)};
    return p.isEmpty() ? IRect{} : p;
}

// The whole pixel a sprite's origin falls on (half-way rounds up).
inline std::optional<int> pixelOrigin(float v)
{
    if (!(std::abs(v) < 1e9f))
        return std::nullopt;
    return floorIn(v + 0.5f, -1000000000, 1000000000);
}

inline float clamp01(float v)
{
    return v > 0 ? (v < 1 ? v : 1) : 0;
}

// Premultiplied in [0, 1], components clamped first (the CPU renderer's paintOf, unscaled).
struct Premul {
    float r = 0;
    float g = 0;
    float b = 0;
    float a = 0;
};

Premul premul(Color c)
{
    const float a = clamp01(c.a);
    return {clamp01(c.r) * a, clamp01(c.g) * a, clamp01(c.b) * a, a};
}

Corners clampRadii(const Corners &c, Size size)
{
    const float limit = std::max(0.0f, std::min(size.width, size.height) / 2);
    const auto f = [limit](float r) { return r > 0 ? std::min(r, limit) : 0.0f; };
    return {f(c.topLeft), f(c.topRight), f(c.bottomRight), f(c.bottomLeft)};
}

Edges clampWidths(const Edges &e)
{
    const auto f = [](float w) { return w > 0 ? w : 0.0f; };
    return {f(e.top), f(e.right), f(e.bottom), f(e.left)};
}

Rect limitTo(const Rect &r, const IRect &target, float margin)
{
    return Rect::fromLTRB(std::max(r.left(), float(target.x0) - margin), std::max(r.top(), float(target.y0) - margin),
                          std::min(r.right(), float(target.x1) + margin),
                          std::min(r.bottom(), float(target.y1) + margin));
}

// Centre and half sizes of a rounded rectangle, as the CPU renderer's RoundedRect::of (zero for an
// empty one).
void setBox(float out[4], const Rect &r)
{
    if (r.isEmpty()) {
        out[0] = out[1] = out[2] = out[3] = 0;
        return;
    }
    const float hw = r.width() / 2;
    const float hh = r.height() / 2;
    out[0] = r.left() + hw;
    out[1] = r.top() + hh;
    out[2] = hw;
    out[3] = hh;
}

void set4(float out[4], float a, float b, float c, float d)
{
    out[0] = a;
    out[1] = b;
    out[2] = c;
    out[3] = d;
}

void setColor(float out[4], const Premul &p)
{
    set4(out, p.r, p.g, p.b, p.a);
}

void setRadii(float out[4], const Corners &c)
{
    set4(out, c.topLeft, c.topRight, c.bottomRight, c.bottomLeft);
}

void setRect(float out[4], const IRect &r)
{
    set4(out, float(r.x0), float(r.y0), float(r.x1), float(r.y1));
}

// CSS's linear-gradient(angle) over `box`: gx, gy, g0 of t = gx·x + gy·y + g0.
void setGradient(float out[4], const Rect &box, float angleDegrees)
{
    float a = std::fmod(angleDegrees, 360.0f);
    if (a < 0)
        a += 360.0f;
    float dx;
    float dy;
    if (a == 0) {
        dx = 0;
        dy = -1;
    } else if (a == 90) {
        dx = 1;
        dy = 0;
    } else if (a == 180) {
        dx = 0;
        dy = 1;
    } else if (a == 270) {
        dx = -1;
        dy = 0;
    } else {
        const double rad = double(a) * std::numbers::pi / 180.0;
        dx = float(std::sin(rad));
        dy = float(-std::cos(rad));
    }
    const float length = std::abs(box.width() * dx) + std::abs(box.height() * dy);
    const float gx = dx / length;
    const float gy = dy / length;
    const Point c = box.center();
    set4(out, gx, gy, 0.5f - (c.x * gx + c.y * gy), 1);
}

// Below this blur a shadow is a sharp rounded rectangle.
constexpr float kMinBlur = 0.01f;

// ---- Building ------------------------------------------------------------------------------------

class Writer {
public:
    Writer(GpuFrame &frame, const Atlas &atlas, Atlas &paths, CoverageRasterizer &rasterizer)
        : m_frame(frame)
        , m_atlas(atlas)
        , m_paths(paths)
        , m_rasterizer(rasterizer)
        , m_full{0, 0, frame.width, frame.height}
    { }

    void add(const Primitive &p, const IRect &visible)
    {
        std::visit([&](const auto &prim) { draw(prim, visible); }, p);
    }

private:
    template<typename T>
    void emit(GpuKind kind, GpuTexture texture, const T &instance)
    {
        static_assert(sizeof(T) % 16 == 0);
        if (m_frame.batches.empty() || m_frame.batches.back().kind != kind
            || !(m_frame.batches.back().texture == texture))
            m_frame.batches.push_back(GpuBatch{kind, texture, m_frame.instances.size(), 0});
        const std::size_t at = m_frame.instances.size();
        m_frame.instances.resize(at + sizeof(T));
        std::memcpy(m_frame.instances.data() + at, &instance, sizeof(T));
        ++m_frame.batches.back().count;
        ++m_frame.primitives;
    }

    void draw(const Quad &q, const IRect &clip)
    {
        if (q.bounds.isEmpty())
            return;
        const IRect region = pixelsCovering(q.bounds, clip);
        if (region.isEmpty())
            return;
        GpuQuadInstance g{};
        const Premul fill = premul(q.background.color);
        Premul fill2 = fill;
        set4(g.gradient, 0, 0, 0, 0);
        if (q.background.kind == Background::Kind::LinearGradient) {
            fill2 = premul(q.background.color2);
            setGradient(g.gradient, q.bounds, q.background.angle);
        }
        const Edges widths = clampWidths(q.borderWidths);
        Premul border = premul(q.borderColor);
        const bool hasBorder = border.a > 0 && !widths.isZero();
        if (!hasBorder)
            border = {};
        if (!hasBorder && !(fill.a > 0) && !(fill2.a > 0))
            return;
        const Corners radii = clampRadii(q.cornerRadii, q.bounds.size);
        const float maxWidth = std::max(std::max(widths.top, widths.right), std::max(widths.bottom, widths.left));
        const Rect geometry = limitTo(q.bounds, m_full, radii.max() + maxWidth + 2);
        const Rect innerRect = geometry.inset(widths);
        const Corners innerRadii = clampRadii(Corners{radii.topLeft - std::max(widths.top, widths.left),
                                                      radii.topRight - std::max(widths.top, widths.right),
                                                      radii.bottomRight - std::max(widths.bottom, widths.right),
                                                      radii.bottomLeft - std::max(widths.bottom, widths.left)},
                                              innerRect.size);
        setRect(g.rect, region);
        setBox(g.outer, geometry);
        setRadii(g.outerRadii, radii);
        if (hasBorder) {
            setBox(g.inner, innerRect);
            setRadii(g.innerRadii, innerRadii);
        }
        setColor(g.fill, fill);
        setColor(g.fill2, fill2);
        setColor(g.border, border);
        emit(GpuKind::Quad, {}, g);
    }

    void draw(const Shadow &s, const IRect &clip)
    {
        if (s.bounds.isEmpty())
            return;
        const Premul color = premul(s.color);
        if (!(color.a > 0))
            return;
        const Corners radii = clampRadii(s.cornerRadii, s.bounds.size);
        if (!(s.blurRadius >= kMinBlur)) {
            Quad sharp;
            sharp.bounds = s.bounds;
            sharp.background = Background::solid(s.color);
            sharp.cornerRadii = radii;
            draw(sharp, clip);
            return;
        }
        const float sigma = s.blurRadius / 2;
        const float reach = 3 * sigma;
        const IRect region = pixelsCovering(s.bounds.dilated(reach), clip);
        if (region.isEmpty())
            return;
        GpuShadowInstance g{};
        setRect(g.rect, region);
        setBox(g.box, limitTo(s.bounds, m_full, radii.max() + 6 * sigma + 2));
        setRadii(g.radii, radii);
        setColor(g.color, color);
        set4(g.blur, sigma, std::numbers::sqrt2_v<float> / 2 / sigma,
             1 / (std::sqrt(2 * std::numbers::pi_v<float>) * sigma), reach);
        emit(GpuKind::Shadow, {}, g);
    }

    // Coverage texels at `shift` (texel = pixel − shift), tinted.
    void sprite(GpuTexture texture, const IRect &region, std::int64_t sx, std::int64_t sy, const Premul &color)
    {
        GpuSpriteInstance g{};
        setRect(g.rect, region);
        set4(g.shift, float(sx), float(sy), 0, 0);
        setColor(g.color, color);
        emit(GpuKind::Sprite, texture, g);
    }

    void draw(const MonochromeSprite &s, const IRect &clip)
    {
        const AtlasTile &tile = s.tile;
        if (tile.isEmpty() || tile.page >= m_atlas.pages().size() || s.bounds.isEmpty())
            return;
        const AtlasPage &page = m_atlas.pages()[tile.page];
        if (page.kind != AtlasKind::Alpha)
            return;
        const Premul color = premul(s.color);
        if (!(color.a > 0))
            return;
        const std::optional<int> originX = pixelOrigin(s.bounds.left());
        const std::optional<int> originY = pixelOrigin(s.bounds.top());
        if (!originX || !originY)
            return;
        // The tile's texels that exist in its page, and where they land.
        const std::int64_t tx0 = std::max<std::int64_t>(tile.x, 0);
        const std::int64_t ty0 = std::max<std::int64_t>(tile.y, 0);
        const std::int64_t tx1 = std::min<std::int64_t>(std::int64_t(tile.x) + tile.width, page.width);
        const std::int64_t ty1 = std::min<std::int64_t>(std::int64_t(tile.y) + tile.height, page.height);
        const std::int64_t sx = std::int64_t(*originX) - tile.x;
        const std::int64_t sy = std::int64_t(*originY) - tile.y;
        const auto within = [](std::int64_t v, int lo, int hi) { return int(std::clamp<std::int64_t>(v, lo, hi)); };
        const IRect region{within(tx0 + sx, clip.x0, clip.x1), within(ty0 + sy, clip.y0, clip.y1),
                           within(tx1 + sx, clip.x0, clip.x1), within(ty1 + sy, clip.y0, clip.y1)};
        if (region.isEmpty())
            return;
        sprite(GpuTexture{GpuTexture::Source::Atlas, tile.page, 0}, region, sx, sy, color);
    }

    // `source` (texel space, within `limits`) scaled into `bounds`, cut to the rounded corners.
    void image(GpuTexture texture, const Rect &bounds, const Corners &cornerRadii, const IRect &limits,
               const Rect &source, float opacity, bool grayscale, const IRect &clip)
    {
        if (bounds.isEmpty() || source.isEmpty() || limits.isEmpty())
            return;
        const float alpha = clamp01(opacity);
        if (!(alpha > 0))
            return;
        const IRect region = pixelsCovering(bounds, clip);
        if (region.isEmpty())
            return;
        const Corners radii = clampRadii(cornerRadii, bounds.size);
        GpuImageInstance g{};
        setRect(g.rect, region);
        setBox(g.shape, limitTo(bounds, m_full, radii.max() + 2));
        setRadii(g.radii, radii);
        set4(g.map, bounds.left(), bounds.top(), source.width() / bounds.width(), source.height() / bounds.height());
        set4(g.source, source.left(), source.top(), alpha, grayscale ? 1.0f : 0.0f);
        setRect(g.limits, limits);
        emit(GpuKind::Image, texture, g);
    }

    void draw(const ColorSprite &s, const IRect &clip)
    {
        const AtlasTile &tile = s.tile;
        if (tile.isEmpty() || tile.page >= m_atlas.pages().size())
            return;
        const AtlasPage &page = m_atlas.pages()[tile.page];
        if (page.kind != AtlasKind::Color)
            return;
        IRect limits;
        limits.x0 = std::clamp(tile.x, 0, page.width);
        limits.y0 = std::clamp(tile.y, 0, page.height);
        limits.x1 = int(std::clamp<std::int64_t>(std::int64_t(tile.x) + tile.width, limits.x0, page.width));
        limits.y1 = int(std::clamp<std::int64_t>(std::int64_t(tile.y) + tile.height, limits.y0, page.height));
        const Rect source = Rect::fromXYWH(float(tile.x), float(tile.y), float(tile.width), float(tile.height));
        image(GpuTexture{GpuTexture::Source::Atlas, tile.page, 0}, s.bounds, Corners{}, limits, source, s.opacity,
              s.grayscale, clip);
    }

    void draw(const Picture &p, const IRect &clip)
    {
        const ImageData *data = p.image.get();
        if (!data || data->width <= 0 || data->height <= 0
            || data->pixels.size() < std::size_t(data->width) * std::size_t(data->height))
            return;
        const Rect whole = Rect::fromXYWH(0, 0, float(data->width), float(data->height));
        const Rect source = p.source.isEmpty() ? whole : p.source;
        const IRect limits{floorIn(source.left(), 0, data->width), floorIn(source.top(), 0, data->height),
                           ceilIn(source.right(), 0, data->width), ceilIn(source.bottom(), 0, data->height)};
        const std::size_t batches = m_frame.batches.size();
        image(GpuTexture{GpuTexture::Source::Picture, 0, data->id}, p.bounds, p.cornerRadii, limits, source, p.opacity,
              p.grayscale, clip);
        if (m_frame.batches.size() != batches)
            m_frame.pictures.push_back(p.image);
    }

    void draw(const PathPrimitive &p, const IRect &clip)
    {
        const std::size_t n = p.points.size();
        if (n < 3)
            return;
        const Premul color = premul(p.color);
        if (!(color.a > 0))
            return;
        const IRect region = pixelsCovering(p.bounds, clip);
        if (region.isEmpty())
            return;
        for (const Point &pt : p.points) {
            if (!std::isfinite(pt.x) || !std::isfinite(pt.y))
                return;
        }
        // The coverage of the whole path within the target, whatever its mask: a tile shared by
        // every frame that draws the same path at the same subpixel offset.
        const IRect tileRegion = pixelsCovering(p.bounds, m_full);
        const int w = tileRegion.x1 - tileRegion.x0;
        const int h = tileRegion.y1 - tileRegion.y0;
        constexpr float kFar = 1e7f;
        const float ox = float(tileRegion.x0);
        const float oy = float(tileRegion.y0);
        const auto local = [&](const Point &pt) {
            return Point{std::clamp(pt.x - ox, -kFar, kFar), std::clamp(pt.y - oy, -kFar, kFar)};
        };
        // The contours as the CPU renderer reads them (ends clamped, the rest one more contour).
        std::uint64_t key = hashCombine(hashValue(w), std::uint64_t(h));
        std::size_t start = 0;
        const auto hashContour = [&](std::size_t end) {
            key = hashCombine(key, end - start);
            for (std::size_t i = start; i < end; ++i) {
                const Point l = local(p.points[i]);
                key = hashFloat(l.y, hashFloat(l.x, key));
            }
            start = end;
        };
        for (const std::uint32_t e : p.contourEnds)
            hashContour(std::clamp<std::size_t>(e, start, n));
        if (start < n)
            hashContour(n);
        const std::optional<AtlasTile> tile =
                m_paths.getOrInsert(key, AtlasKind::Alpha, [&]() -> std::optional<AtlasBitmap> {
                    m_rasterizer.reset(w, h);
                    std::size_t begin = 0;
                    const auto contour = [&](std::size_t end) {
                        if (end >= begin + 2) {
                            m_rasterizer.moveTo(local(p.points[begin]));
                            for (std::size_t i = begin + 1; i < end; ++i)
                                m_rasterizer.lineTo(local(p.points[i]));
                            m_rasterizer.close();
                        }
                        begin = end;
                    };
                    for (const std::uint32_t e : p.contourEnds)
                        contour(std::clamp<std::size_t>(e, begin, n));
                    if (begin < n)
                        contour(n);
                    AtlasBitmap bitmap;
                    bitmap.width = w;
                    bitmap.height = h;
                    bitmap.bytesPerPixel = 1;
                    bitmap.pixels.resize(std::size_t(w) * std::size_t(h));
                    m_rasterizer.accumulate(bitmap.pixels.data(), w);
                    return bitmap;
                });
        if (!tile || tile->isEmpty())
            return;
        sprite(GpuTexture{GpuTexture::Source::PathAtlas, tile->page, 0}, region, std::int64_t(tileRegion.x0) - tile->x,
               std::int64_t(tileRegion.y0) - tile->y, color);
    }

    void draw(const Underline &u, const IRect &clip)
    {
        if (u.bounds.isEmpty())
            return;
        const float height = u.bounds.height();
        const float thickness = u.thickness > 0 ? std::min(u.thickness, height) : 0.0f;
        if (!(thickness > 0))
            return;
        if (!u.wavy) {
            Quad line;
            line.bounds =
                    Rect::fromLTRB(u.bounds.left(), u.bounds.bottom() - thickness, u.bounds.right(), u.bounds.bottom());
            line.background = Background::solid(u.color);
            draw(line, clip);
            return;
        }
        const Premul color = premul(u.color);
        if (!(color.a > 0))
            return;
        const IRect region = pixelsCovering(u.bounds, clip);
        if (region.isEmpty())
            return;
        const float period = std::max(4 * thickness, 2.0f);
        GpuUnderlineInstance g{};
        setRect(g.rect, region);
        set4(g.wave, u.bounds.left(), u.bounds.right(), u.bounds.top() + height / 2, (height - thickness) / 2);
        set4(g.wave2, 2 * std::numbers::pi_v<float> / period, thickness / 2 + 0.5f, 0, 0);
        setColor(g.color, color);
        emit(GpuKind::Underline, {}, g);
    }

    GpuFrame &m_frame;
    const Atlas &m_atlas;
    Atlas &m_paths;
    CoverageRasterizer &m_rasterizer;
    IRect m_full;
};

} // namespace

GpuFrameBuilder::GpuFrameBuilder() : m_paths(1024) { }

GpuFrame GpuFrameBuilder::build(const Scene &scene, const Atlas &atlas, int width, int height)
{
    GpuFrame frame;
    frame.width = std::max(0, width);
    frame.height = std::max(0, height);
    if (frame.width == 0 || frame.height == 0)
        return frame;
    m_paths.beginFrame();
    frame.instances.reserve(scene.size() * sizeof(GpuSpriteInstance));
    Writer writer(frame, atlas, m_paths, m_rasterizer);
    const IRect target{0, 0, frame.width, frame.height};
    for (const Primitive &p : scene.primitives()) {
        const ContentMask &mask = std::visit([](const auto &prim) -> const ContentMask & { return prim.mask; }, p);
        const IRect visible = pixelsCovering(mask.bounds, target);
        if (!visible.isEmpty())
            writer.add(p, visible);
    }
    // Paths not drawn for a while leave the path atlas (pages left empty are packed again).
    if (++m_frames % 64 == 0)
        m_paths.collectGarbage(128);
    return frame;
}

} // namespace brulion::atrament
