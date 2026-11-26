// The CPU renderer (raster/raster.h). Exact first: a pixel's coverage is the formula a GPU shader
// evaluates at the pixel's centre (GPUI's: a signed distance over one pixel, Evan Wallace's
// blurred rectangle), so the GPU renderers can match it. Fast second, without changing a byte:
//   * a row of a rounded shape is split, from its geometry, into the pixels near its edges
//     (evaluated one by one) and the run between them at one coverage (one span);
//   * a quad's rows without corners are all alike: the first is recorded as spans of finished
//     paint and replayed; a vertically symmetric quad or shadow replays its lower rows from its
//     upper ones;
//   * blending is 8-bit integer arithmetic, two pixels a step in 16-bit vector lanes where the
//     compiler has vector types;
//   * whatever a later opaque quad paints over is skipped (Occlusion).
// The synthetic UI frame of tst_atrament_raster (1280×800, 2 000 quads, 5 000 glyphs, 5 shadows)
// took ≈ 18 M cycles at -O3 on the x86-64 development machine (≈ 4 ms at 4.6 GHz).

#include "raster/raster.h"

#include "base/coverage.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <numbers>
#include <optional>
#include <vector>

namespace brulion::atrament {

void RasterTarget::resize(int w, int h)
{
    width = std::max(0, w);
    height = std::max(0, h);
    pixels.assign(std::size_t(width) * std::size_t(height), Pixel{});
}

void RasterTarget::fill(Pixel p)
{
    std::fill(pixels.begin(), pixels.end(), p);
}

namespace {

// ---- Pixels and paint ---------------------------------------------------------------------------

struct IRect {
    int x0 = 0;
    int y0 = 0;
    int x1 = 0;
    int y1 = 0;

    bool isEmpty() const { return x0 >= x1 || y0 >= y1; }
    std::size_t area() const { return isEmpty() ? 0 : std::size_t(x1 - x0) * std::size_t(y1 - y0); }
    IRect intersected(const IRect &o) const
    {
        return {std::max(x0, o.x0), std::max(y0, o.y0), std::min(x1, o.x1), std::min(y1, o.y1)};
    }
};

// floor / ceil of `v` kept within [lo, hi]; NaN gives lo. Without libm's calls: the per-row
// bounds use them several times a row.
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

// The whole pixels a rectangle touches (edges rounded outwards), cut to `clip`; nothing for an
// empty or NaN rectangle.
inline IRect pixelsCovering(const Rect &r, const IRect &clip)
{
    if (r.isEmpty())
        return {};
    IRect p{floorIn(r.left(), clip.x0, clip.x1), floorIn(r.top(), clip.y0, clip.y1),
            ceilIn(r.right(), clip.x0, clip.x1), ceilIn(r.bottom(), clip.y0, clip.y1)};
    return p.isEmpty() ? IRect{} : p;
}

// The whole pixel a sprite's origin falls on (half-way rounds up), or nullopt for NaN and far away
// values. Without libm's call, once a glyph.
inline std::optional<int> pixelOrigin(float v)
{
    if (!(std::abs(v) < 1e9f))
        return std::nullopt;
    return floorIn(v + 0.5f, -1000000000, 1000000000);
}

// [0, 1]; NaN gives 0.
inline float clamp01(float v)
{
    return v > 0 ? (v < 1 ? v : 1) : 0;
}

// Coverage of a pixel whose centre lies `d` from an edge (negative inside): anti-aliasing over
// one pixel.
inline float coverage(float d)
{
    return clamp01(0.5f - d);
}

// A premultiplied colour scaled to 0–255, the unit of the target's bytes.
struct Paint {
    float r = 0;
    float g = 0;
    float b = 0;
    float a = 0;

    Paint operator*(float s) const { return {r * s, g * s, b * s, a * s}; }
    Paint operator+(const Paint &o) const { return {r + o.r, g + o.g, b + o.b, a + o.a}; }
    Paint operator-(const Paint &o) const { return {r - o.r, g - o.g, b - o.b, a - o.a}; }
};

// Premultiplied in [0, 1] first, then scaled: the same bytes as blendOver(…, PremulColor::from(c), …).
Paint paintOf(Color c)
{
    const float a = clamp01(c.a);
    return {clamp01(c.r) * a * 255.0f, clamp01(c.g) * a * 255.0f, clamp01(c.b) * a * 255.0f, a * 255.0f};
}

Paint paintOf(PremulColor c)
{
    const float a = clamp01(c.a);
    const auto channel = [a](float v) { return (v > 0 ? std::min(v, a) : 0.0f) * 255.0f; };
    return {channel(c.r), channel(c.g), channel(c.b), a * 255.0f};
}

inline Paint lerp(const Paint &x, const Paint &y, float t)
{
    return {x.r + (y.r - x.r) * t, x.g + (y.g - x.g) * t, x.b + (y.b - x.b) * t, x.a + (y.a - x.a) * t};
}

// Rounded to the nearest byte; NaN gives 0 (a float → int conversion of NaN is undefined).
inline std::uint32_t to8(float v)
{
    return std::uint32_t(int((v > 0 ? std::min(v, 255.0f) : 0.0f) + 0.5f));
}

// Blending is 8-bit integer arithmetic on premultiplied RGBA packed into 32 bits (r the low byte):
// each byte × m / 255, rounded exactly, two multiplications for all four channels. A source is
// rounded to bytes once, so a result is within a level of the exact one, and the same on every
// machine.
inline std::uint32_t pack(Pixel p)
{
    return std::uint32_t(p.r) | std::uint32_t(p.g) << 8 | std::uint32_t(p.b) << 16 | std::uint32_t(p.a) << 24;
}

inline Pixel unpack(std::uint32_t v)
{
    return {std::uint8_t(v), std::uint8_t(v >> 8), std::uint8_t(v >> 16), std::uint8_t(v >> 24)};
}

// Every byte of x × m / 255, rounded (m ≤ 255): (t + t / 256 + 128) / 256 is exact for 8-bit
// products, and the 16-bit fields never carry into each other.
inline std::uint32_t scale(std::uint32_t x, std::uint32_t m)
{
    std::uint32_t rb = (x & 0x00ff00ffu) * m + 0x00800080u;
    rb = ((rb + ((rb >> 8) & 0x00ff00ffu)) >> 8) & 0x00ff00ffu;
    std::uint32_t ag = ((x >> 8) & 0x00ff00ffu) * m + 0x00800080u;
    ag = (ag + ((ag >> 8) & 0x00ff00ffu)) & 0xff00ff00u;
    return rb | ag;
}

// Source-over, s + d × (1 − s.a), of a source whose channels are ≤ its alpha (no byte overflows).
inline std::uint32_t overPacked(std::uint32_t d, std::uint32_t s)
{
    return s + scale(d, 255 - (s >> 24));
}

// A paint as bytes, its channels kept ≤ its alpha.
inline std::uint32_t packed(const Paint &p)
{
    const std::uint32_t a = to8(p.a);
    return std::min(to8(p.r), a) | std::min(to8(p.g), a) << 8 | std::min(to8(p.b), a) << 16 | a << 24;
}

// Coverage in [0, 1] as the multiplier of scale().
inline std::uint32_t coverage8(float c)
{
    return to8(c * 255.0f);
}

inline Pixel solid(const Paint &s)
{
    return unpack(packed(s));
}

// For an opaque `s` it is solid(s) exactly, for a zero `s` d.
inline Pixel over(Pixel d, const Paint &s)
{
    return unpack(overPacked(pack(d), packed(s)));
}

// Two pixels at a time as eight 16-bit lanes, where the compiler has vector types (GCC, Clang): the
// same arithmetic as scale() and overPacked(), so the same bytes as the scalar code elsewhere.
#if defined(__clang__) || (defined(__GNUC__) && __GNUC__ >= 12)
#define ATRAMENT_RASTER_VECTORS 1
typedef std::uint16_t U16x8 __attribute__((vector_size(16)));
typedef std::uint8_t U8x8 __attribute__((vector_size(8)));

inline U16x8 load2(const Pixel *p)
{
    U8x8 bytes;
    std::memcpy(&bytes, p, sizeof bytes);
    return __builtin_convertvector(bytes, U16x8);
}

inline void store2(Pixel *p, U16x8 v)
{
    const U8x8 bytes = __builtin_convertvector(v, U8x8);
    std::memcpy(p, &bytes, sizeof bytes);
}

inline U16x8 scale2(U16x8 x, U16x8 m)
{
    const U16x8 t = x * m + 128;
    return (t + (t >> 8)) >> 8;
}

inline U16x8 twice(std::uint32_t s)
{
    const Pixel p = unpack(s);
    return U16x8{p.r, p.g, p.b, p.a, p.r, p.g, p.b, p.a};
}
#endif

// One packed source over a run of pixels: opaque runs are plain stores.
void blendPacked(Pixel *row, int x0, int x1, std::uint32_t s)
{
    if (x0 >= x1)
        return;
    const std::uint32_t alpha = s >> 24;
    if (alpha == 0) // and so every channel
        return;
    if (alpha == 255) {
        const int n = x1 - x0;
        const Pixel p = unpack(s);
        if (n >= 64) {
            std::fill(row + x0, row + x1, p);
        } else if (n >= 4) {
            // Eight pixels a step (std::fill stores a Pixel at a time), the last four overlapping.
            const Pixel eight[8] = {p, p, p, p, p, p, p, p};
            int x = x0;
            for (; x + 8 <= x1; x += 8)
                std::memcpy(row + x, eight, sizeof eight);
            if (x + 4 <= x1)
                std::memcpy(row + x, eight, sizeof eight / 2);
            std::memcpy(row + x1 - 4, eight, sizeof eight / 2);
        } else {
            for (int x = x0; x < x1; ++x)
                row[x] = p;
        }
        return;
    }
    const std::uint32_t m = 255 - alpha;
    int x = x0;
#ifdef ATRAMENT_RASTER_VECTORS
    const U16x8 source = twice(s);
    const U16x8 keep = U16x8{} + std::uint16_t(m);
    for (; x + 2 <= x1; x += 2)
        store2(row + x, source + scale2(load2(row + x), keep));
#endif
    for (; x < x1; ++x)
        row[x] = unpack(s + scale(pack(row[x]), m));
}

// A packed paint scaled by 8-bit coverage per pixel (glyphs; coverage 0 leaves a pixel as it is).
void blendCoverage8(Pixel *dst, int n, std::uint32_t paint, const std::uint8_t *coverage)
{
    int i = 0;
#ifdef ATRAMENT_RASTER_VECTORS
    const U16x8 source = twice(paint);
    for (; i + 2 <= n; i += 2) {
        const std::uint16_t c0 = coverage[i];
        const std::uint16_t c1 = coverage[i + 1];
        const U16x8 s = scale2(source, U16x8{c0, c0, c0, c0, c1, c1, c1, c1});
        const U16x8 alpha = __builtin_shufflevector(s, s, 3, 3, 3, 3, 7, 7, 7, 7);
        store2(dst + i, s + scale2(load2(dst + i), 255 - alpha));
    }
#endif
    for (; i < n; ++i)
        dst[i] = unpack(overPacked(pack(dst[i]), scale(paint, coverage[i])));
}

// A paint scaled per pixel by `alpha` in [0, 1]: shadows, paths, waves.
void blendAlpha(Pixel *dst, int n, const Paint &paint, const float *alpha)
{
    const std::uint32_t p = packed(paint);
    constexpr int kChunk = 64;
    std::uint8_t coverage[kChunk];
    for (int i = 0; i < n; i += kChunk) {
        const int m = std::min(kChunk, n - i);
        for (int j = 0; j < m; ++j)
            coverage[j] = std::uint8_t(coverage8(alpha[i + j]));
        blendCoverage8(dst + i, m, p, coverage);
    }
}

// ---- Rounded rectangles -------------------------------------------------------------------------

// Radii no larger than half the shorter side; NaN and negative radii are 0.
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

// A rectangle far larger than the target loses float precision at its far edges: such sides move
// to `margin` outside the target, which changes no pixel of it when `margin` exceeds every radius
// and width that shapes those sides.
Rect limitTo(const Rect &r, const IRect &target, float margin)
{
    return Rect::fromLTRB(std::max(r.left(), float(target.x0) - margin), std::max(r.top(), float(target.y0) - margin),
                          std::min(r.right(), float(target.x1) + margin),
                          std::min(r.bottom(), float(target.y1) + margin));
}

// A rounded rectangle on one pixel row: everything of the signed distance that does not depend on x.
struct ShapeRow {
    float cx = 0;
    float hw = 0;
    float dy = 0; // vertical distance to the straight edge (negative inside)
    float rl = 0; // the corner radius of this row's left and right half
    float rr = 0;
    // Coverage of the row's middle, where only `dy` counts: no pixel of the row gets more.
    float plateau = 0;

    // GPUI's quad_sdf: the corner's circle where both distances point outside it, the straight
    // edges elsewhere (and a square corner for radius 0).
    float sdf(float px) const
    {
        const float dx = std::abs(px - cx) - hw;
        const float r = px < cx ? rl : rr;
        const float vx = dx + r;
        const float vy = dy + r;
        if (r > 0 && vx > 0 && vy > 0)
            return std::sqrt(vx * vx + vy * vy) - r;
        return std::max(dx, dy);
    }

    // Fully covered in the middle and no corner reaching this row: every pixel's coverage depends
    // on its x alone, the same in every such row.
    bool straight() const { return plateau >= 1 && !(rl > 0 && dy + rl > 0) && !(rr > 0 && dy + rr > 0); }

    // How far from cx, on the side with radius r, a pixel's centre may lie and still be covered
    // (`reach`), and lie and be exactly at the plateau (`flat`): the circle's chord at this row
    // grown or shrunk by half a pixel.
    void extents(float r, float &reach, float &flat) const
    {
        const float vy = dy + r;
        if (r > 0 && vy > 0) {
            const float outer = r + 0.5f;
            const float inner = r - 0.5f;
            reach = hw - r + std::sqrt(std::max(0.0f, outer * outer - vy * vy));
            flat = plateau >= 1 ? hw - r + std::sqrt(std::max(0.0f, inner * inner - vy * vy)) : hw - r;
        } else {
            reach = hw + 0.5f;
            flat = plateau >= 1 ? hw - 0.5f : hw + dy;
        }
    }
};

// The pixels of one row of a rounded rectangle, within [x0, x1): [lo, hi) may be covered, [p0, p1)
// are at the row's plateau; the rest of [lo, hi) is near an edge and evaluated one by one. Found
// from the geometry, not by walking pixels, then each bound checked against sdf() itself (float
// rounding may put the geometry's answer a pixel off): along a row the distance falls to the
// middle and rises again (the shape is convex, every step of sdf() monotonic in float too), so
// when the first and the last pixel of [p0, p1) are at the plateau, all between them are.
struct RowSpans {
    int lo = 0;
    int p0 = 0;
    int p1 = 0;
    int hi = 0;
};

RowSpans spansOf(const ShapeRow &s, int x0, int x1)
{
    if (!(s.plateau > 0))
        return {x0, x0, x0, x0};
    float reachL;
    float flatL;
    float reachR;
    float flatR;
    s.extents(s.rl, reachL, flatL);
    if (s.rr == s.rl) {
        reachR = reachL;
        flatR = flatL;
    } else {
        s.extents(s.rr, reachR, flatR);
    }
    const auto at = [&s](int x) { return coverage(s.sdf(float(x) + 0.5f)); };
    // Pixel x has its centre at x + 0.5. A bound is checked only when a pixel's centre lies within
    // float's error of the geometry's answer.
    const float eps = 1e-3f + 1e-6f * (std::abs(s.cx) + s.hw);
    const float left = s.cx - reachL;
    const float right = s.cx + reachR;
    const float flat0 = s.cx - flatL;
    const float flat1 = s.cx + flatR;
    RowSpans r;
    r.lo = std::min(floorIn(left - 0.5f, x0 - 1, x1) + 1, x1);
    if (left - (float(r.lo) - 0.5f) <= eps)
        while (r.lo > x0 && at(r.lo - 1) > 0)
            --r.lo;
    r.hi = ceilIn(right - 0.5f, r.lo, x1);
    if (float(r.hi) + 0.5f - right <= eps)
        while (r.hi < x1 && at(r.hi) > 0)
            ++r.hi;
    r.p0 = ceilIn(flat0 - 0.5f, r.lo, r.hi);
    if (float(r.p0) + 0.5f - flat0 <= eps)
        while (r.p0 < r.hi && at(r.p0) < s.plateau)
            ++r.p0;
    r.p1 = std::max(floorIn(flat1 - 0.5f, r.p0 - 1, r.hi - 1) + 1, r.p0);
    if (flat1 - (float(r.p1) - 0.5f) <= eps)
        while (r.p1 > r.p0 && at(r.p1 - 1) < s.plateau)
            --r.p1;
    return r;
}

// `edge(x, coverage)` for each pixel of a row near an edge, `span(a, b, plateau)` for the rest.
template<typename Edge, typename Span>
inline void walkRow(const ShapeRow &s, int x0, int x1, Edge &&edge, Span &&span)
{
    const RowSpans r = spansOf(s, x0, x1);
    for (int x = r.lo; x < r.p0; ++x)
        edge(x, coverage(s.sdf(float(x) + 0.5f)));
    if (r.p0 < r.p1)
        span(r.p0, r.p1, s.plateau);
    for (int x = r.p1; x < r.hi; ++x)
        edge(x, coverage(s.sdf(float(x) + 0.5f)));
}

struct RoundedRect {
    float cx = 0;
    float cy = 0;
    float hw = 0;
    float hh = 0;
    Corners radii;
    bool empty = true;

    static RoundedRect of(const Rect &r, const Corners &radii)
    {
        RoundedRect s;
        if (r.isEmpty())
            return s;
        s.hw = r.width() / 2;
        s.hh = r.height() / 2;
        s.cx = r.left() + s.hw;
        s.cy = r.top() + s.hh;
        s.radii = radii;
        s.empty = false;
        return s;
    }

    ShapeRow row(float py) const
    {
        ShapeRow row;
        if (empty)
            return row;
        const bool upper = py < cy;
        row.cx = cx;
        row.hw = hw;
        row.dy = std::abs(py - cy) - hh;
        row.rl = upper ? radii.topLeft : radii.bottomLeft;
        row.rr = upper ? radii.topRight : radii.bottomRight;
        row.plateau = coverage(row.dy);
        return row;
    }
};

// ---- Frame state --------------------------------------------------------------------------------

struct Tap {
    int i0 = 0; // the two texels (indices) a sample mixes, and the weight of the second
    int i1 = 0;
    float f = 0;
};

// One paint over the pixels [x0, x1) of a row: a recorded row of a quad, replayed for every row
// that is the same.
struct SpanOp {
    int x0 = 0;
    int x1 = 0;
    std::uint32_t source = 0; // packed()
};

inline void apply(Pixel *row, int x0, int x1, std::uint32_t source)
{
    if (x1 - x0 == 1)
        row[x0] = unpack(source >> 24 == 255 ? source : overPacked(pack(row[x0]), source));
    else
        blendPacked(row, x0, x1, source);
}

void replay(Pixel *row, const SpanOp *begin, const SpanOp *end)
{
    for (const SpanOp *op = begin; op != end; ++op)
        apply(row, op->x0, op->x1, op->source);
}

// Reused by every primitive of a frame: nothing is allocated per pixel or per primitive once the
// buffers have grown.
struct Scratch {
    std::vector<float> columns;
    std::vector<float> columns2;
    std::vector<float> columns3;
    std::vector<float> alpha;
    std::vector<std::uint8_t> bytes;
    std::vector<int> slots;
    std::vector<Tap> taps;
    std::vector<SpanOp> ops;
    std::vector<std::uint32_t> rowOps; // per row of a quad: [begin, end) of its recorded ops
    CoverageRasterizer rasterizer;
    std::vector<float> coverage;
};

struct Context {
    RasterTarget &target;
    const Atlas &atlas;
    IRect full; // the whole target: what limitTo() measures against, the same with any damage
    Scratch &scratch;

    Pixel *row(int y) { return target.pixels.data() + std::size_t(y) * std::size_t(target.width); }
};

// ---- Quads --------------------------------------------------------------------------------------

// The colour of a quad's pixel. A pixel is covered `co` by the quad and `ci` (≤ co) of it by the
// inside of the border; the fill lies under the whole quad (CSS's background-clip: border-box), the
// border over it in the band between. Mixed by area in premultiplied space, so an anti-aliased
// outer edge shows no fringe of the fill.
struct QuadPaint {
    Paint fill;
    Paint fill2;
    bool gradient = false;
    // t = gx × px + gy × py + g0 along the gradient line.
    float gx = 0;
    float gy = 0;
    float g0 = 0;
    Paint border;
    float keep = 1; // the fill's share under the border: 1 − the border's alpha

    float rowT(float py) const { return gy * py + g0; }
    Paint fillAt(float px, float rowT) const { return gradient ? lerp(fill, fill2, clamp01(gx * px + rowT)) : fill; }
    Paint shade(const Paint &f, float co, float ci) const
    {
        const float wb = co - ci;
        const float wf = ci + keep * wb;
        return {f.r * wf + border.r * wb, f.g * wf + border.g * wb, f.b * wf + border.b * wb, f.a * wf + border.a * wb};
    }
};

// CSS's linear-gradient(angle) over `box`: 0° to the top, 90° to the right; the gradient line runs
// through the centre, as long as the box's projection onto it, so the start and end colours fall
// on the corners (or edges) it points from and to.
void setGradient(QuadPaint &p, const Rect &box, float angleDegrees)
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
    p.gx = dx / length;
    p.gy = dy / length;
    const Point c = box.center();
    p.g0 = 0.5f - (c.x * p.gx + c.y * p.gy);
}

// The last row of the straight band that starts at row `first` (≤ `limit`): where py + 0.5 still
// keeps half a pixel and every lower corner radius away from the bottom edge, of the quad and of
// its inside.
int bandLastRow(const RoundedRect &outer, const RoundedRect *inner, int first, int limit)
{
    const auto bottom = [](const RoundedRect &r) {
        return r.cy + r.hh - std::max(0.5f, std::max(r.radii.bottomLeft, r.radii.bottomRight));
    };
    float lowest = bottom(outer);
    if (inner)
        lowest = std::min(lowest, bottom(*inner));
    // Rows y with y + 0.5 ≤ lowest.
    return floorIn(lowest - 0.5f, first, limit);
}

// A run of a gradient fill, each pixel `wf` of the fill and `wb` of the border (QuadPaint::shade).
void gradientSpan(Pixel *row, int a, int b, const QuadPaint &p, float rowT, float wf, float wb)
{
    const Paint from = p.fill;
    const Paint to = p.fill2;
    const Paint border = p.border * wb;
    const float gx = p.gx;
    for (int x = a; x < b; ++x) {
        const Paint f = lerp(from, to, clamp01(gx * (float(x) + 0.5f) + rowT));
        row[x] = over(row[x], f * wf + border);
    }
}

std::size_t drawQuad(Context &cx, const Quad &q, const IRect &clip)
{
    if (q.bounds.isEmpty())
        return 0;
    const IRect region = pixelsCovering(q.bounds, clip);
    if (region.isEmpty())
        return 0;
    QuadPaint paint;
    paint.fill = paintOf(q.background.color);
    paint.fill2 = paint.fill;
    if (q.background.kind == Background::Kind::LinearGradient) {
        paint.fill2 = paintOf(q.background.color2);
        paint.gradient = true;
        setGradient(paint, q.bounds, q.background.angle);
    }
    const Edges widths = clampWidths(q.borderWidths);
    paint.border = paintOf(q.borderColor);
    const bool border = paint.border.a > 0 && !widths.isZero();
    if (!border)
        paint.border = {};
    paint.keep = (255.0f - paint.border.a) * (1.0f / 255.0f);
    if (!border && !(paint.fill.a > 0) && !(paint.fill2.a > 0))
        return 0;

    const Corners radii = clampRadii(q.cornerRadii, q.bounds.size);
    const float maxWidth = std::max(std::max(widths.top, widths.right), std::max(widths.bottom, widths.left));
    const Rect geometry = limitTo(q.bounds, cx.full, radii.max() + maxWidth + 2);
    const RoundedRect outer = RoundedRect::of(geometry, radii);
    // The inner edge: the bounds inset by the widths, its radii the outer ones less the larger
    // adjacent width (and no more than half the inner rectangle's shorter side).
    const Rect innerRect = geometry.inset(widths);
    const Corners innerRadii = clampRadii(Corners{radii.topLeft - std::max(widths.top, widths.left),
                                                  radii.topRight - std::max(widths.top, widths.right),
                                                  radii.bottomRight - std::max(widths.bottom, widths.right),
                                                  radii.bottomLeft - std::max(widths.bottom, widths.left)},
                                          innerRect.size);
    const RoundedRect inner = border ? RoundedRect::of(innerRect, innerRadii) : RoundedRect{};

    // Rows of the straight middle (no corner reaches them, outside or inside the border) are all
    // the same: the first one is recorded as spans of finished paint and replayed for the others.
    // A quad symmetric about its horizontal middle line, on the pixel grid, has its bottom rows
    // the same as its top ones: those are recorded too and replayed mirrored.
    std::vector<SpanOp> &ops = cx.scratch.ops;
    std::vector<std::uint32_t> &rowOps = cx.scratch.rowOps;
    constexpr std::uint32_t kNotRecorded = ~0u;
    ops.clear();
    std::uint32_t bandBegin = kNotRecorded;
    std::uint32_t bandEnd = 0;
    const float twiceMiddle = 2 * outer.cy;
    const bool mirrored = !paint.gradient && radii.topLeft == radii.bottomLeft && radii.topRight == radii.bottomRight
                          && (!border
                              || (inner.cy == outer.cy && innerRadii.topLeft == innerRadii.bottomLeft
                                  && innerRadii.topRight == innerRadii.bottomRight))
                          && std::abs(twiceMiddle) < 1e7f && twiceMiddle == float(int(twiceMiddle));
    const int mirror = mirrored ? int(twiceMiddle) - 1 : 0; // rows y and mirror − y are the same
    if (mirrored)
        rowOps.assign(2 * std::size_t(region.y1 - region.y0), kNotRecorded);
    for (int y = region.y0; y < region.y1; ++y) {
        const float py = float(y) + 0.5f;
        const ShapeRow o = outer.row(py);
        if (!(o.plateau > 0))
            continue;
        const ShapeRow in = inner.row(py);
        Pixel *row = cx.row(y);
        const bool same = !paint.gradient && o.straight() && (!border || in.straight());
        if (same && bandBegin != kNotRecorded) {
            replay(row, ops.data() + bandBegin, ops.data() + bandEnd);
            continue;
        }
        const int twin = mirror - y;
        if (mirrored && twin < y && twin >= region.y0) {
            const std::size_t t = 2 * std::size_t(twin - region.y0);
            if (rowOps[t] != kNotRecorded) {
                replay(row, ops.data() + rowOps[t], ops.data() + rowOps[t + 1]);
                continue;
            }
        }
        // Rows are recorded where they will be replayed: the band's first one, and the upper
        // half's rows of a mirrored quad whose twin is drawn too.
        const bool recording = same || (mirrored && twin > y && twin < region.y1);
        const auto begin = std::uint32_t(ops.size());
        const float rowT = paint.rowT(py);

        const auto emit = [&](int a, int b, const Paint &p) {
            if (recording)
                ops.push_back(SpanOp{a, b, packed(p)});
            else
                apply(row, a, b, packed(p));
        };
        const auto pixel = [&](int x, float co, float ci) {
            emit(x, x + 1, paint.shade(paint.fillAt(float(x) + 0.5f, rowT), co, ci));
        };
        const auto run = [&](int a, int b, float co, float ci) {
            if (a >= b)
                return;
            if (!paint.gradient) {
                emit(a, b, paint.shade(paint.fill, co, ci));
                return;
            }
            const float wb = co - ci;
            gradientSpan(row, a, b, paint, rowT, ci + paint.keep * wb, wb);
        };
        const auto edge = [&](int x, float co) {
            if (!(co > 0))
                return;
            float ci = co;
            if (border)
                ci = in.plateau > 0 ? std::min(co, coverage(in.sdf(float(x) + 0.5f))) : 0.0f;
            pixel(x, co, ci);
        };
        const auto innerEdge = [&](int x, float co) { pixel(x, co, std::min(co, coverage(in.sdf(float(x) + 0.5f)))); };
        const auto middle = [&](int a, int b, float co) {
            if (!border) {
                run(a, b, co, co);
                return;
            }
            if (!(in.plateau > 0)) { // a row of the top or bottom border
                run(a, b, co, 0);
                return;
            }
            // Beyond the inner edge's reach: border only.
            const RowSpans r = spansOf(in, a, b);
            run(a, r.lo, co, 0);
            for (int x = r.lo; x < r.p0; ++x)
                innerEdge(x, co);
            run(r.p0, r.p1, co, std::min(co, in.plateau));
            for (int x = r.p1; x < r.hi; ++x)
                innerEdge(x, co);
            run(r.hi, b, co, 0);
        };
        walkRow(o, region.x0, region.x1, edge, middle);
        if (recording) {
            const auto end = std::uint32_t(ops.size());
            if (same) {
                bandBegin = begin;
                bandEnd = end;
                replay(row, ops.data() + begin, ops.data() + end);
                // The band runs down to where a lower corner (or the lower edge's anti-aliasing)
                // reaches a row, outside or inside the border; its last row checked like any.
                int last = bandLastRow(outer, border ? &inner : nullptr, y, region.y1 - 1);
                const auto sameAt = [&](int yy) {
                    const float pyy = float(yy) + 0.5f;
                    return outer.row(pyy).straight() && (!border || inner.row(pyy).straight());
                };
                while (last > y && !sameAt(last))
                    --last;
                for (int yy = y + 1; yy <= last; ++yy)
                    replay(cx.row(yy), ops.data() + begin, ops.data() + end);
                y = last;
                continue;
            } else {
                rowOps[2 * std::size_t(y - region.y0)] = begin;
                rowOps[2 * std::size_t(y - region.y0) + 1] = end;
            }
            replay(row, ops.data() + begin, ops.data() + end);
        }
    }
    return region.area();
}

std::size_t draw(Context &cx, const Quad &q, const IRect &clip)
{
    return drawQuad(cx, q, clip);
}

// ---- Shadows ------------------------------------------------------------------------------------

// Below this blur a shadow is drawn as a sharp rounded rectangle (σ → 0 would divide by zero).
constexpr float kMinBlur = 0.01f;

// The error function, Abramowitz & Stegun 7.1.27 (as GPUI's shader): within 5e-4, monotonic, and
// exactly ±1 in float from |v| ≈ 5.6 on.
inline float erfApprox(float v)
{
    const float a = std::abs(v);
    const float r1 = 1 + (0.278393f + (0.230389f + (0.000972f + 0.078108f * a) * a) * a) * a;
    const float r2 = r1 * r1;
    return std::copysign(1 - 1 / (r2 * r2), v);
}

// A box of half-width `c` blurred along x, at `x` from its centre (k = √½ / σ).
inline float blurAlongX(float x, float c, float k)
{
    return 0.5f * (erfApprox((x + c) * k) - erfApprox((x - c) * k));
}

constexpr int kShadowSamples = 4;

std::size_t draw(Context &cx, const Shadow &s, const IRect &clip)
{
    if (s.bounds.isEmpty())
        return 0;
    const Paint color = paintOf(s.color);
    if (!(color.a > 0))
        return 0;
    const Corners radii = clampRadii(s.cornerRadii, s.bounds.size);
    if (!(s.blurRadius >= kMinBlur)) {
        Quad sharp;
        sharp.bounds = s.bounds;
        sharp.background = Background::solid(s.color);
        sharp.cornerRadii = radii;
        return drawQuad(cx, sharp, clip);
    }
    const float sigma = s.blurRadius / 2;
    const float reach = 3 * sigma;
    const IRect region = pixelsCovering(s.bounds.dilated(reach), clip);
    if (region.isEmpty())
        return 0;
    const Rect geometry = limitTo(s.bounds, cx.full, radii.max() + 6 * sigma + 2);
    const float hw = geometry.width() / 2;
    const float hh = geometry.height() / 2;
    const float cxs = geometry.left() + hw;
    const float cys = geometry.top() + hh;
    const float k = std::numbers::sqrt2_v<float> / 2 / sigma;
    const float norm = 1 / (std::sqrt(2 * std::numbers::pi_v<float>) * sigma);
    // From |v| ≈ 5.6 on erfApprox(v) is ±1 exactly: a box at least this much wider than a pixel's
    // distance from the centre has its far edge's term at ∓1, and one erf per sample is enough.
    constexpr float kSaturated = 6.5f;

    // The blur along x of the box's straight part, per column; the pixels' x from the centre.
    const int nx = region.x1 - region.x0;
    std::vector<float> &straight = cx.scratch.columns;
    std::vector<float> &offsets = cx.scratch.columns2;
    std::vector<float> &alpha = cx.scratch.alpha;
    straight.resize(std::size_t(nx));
    offsets.resize(std::size_t(nx));
    alpha.resize(std::size_t(nx));
    for (int i = 0; i < nx; ++i) {
        offsets[std::size_t(i)] = float(region.x0 + i) + 0.5f - cxs;
        straight[std::size_t(i)] = blurAlongX(offsets[std::size_t(i)], hw, k);
    }
    // Columns left of `split` take the left corners.
    const int split = ceilIn(cxs - 0.5f, region.x0, region.x1) - region.x0;

    // With the same radii above and below and the middle on the pixel grid, a row below the
    // middle is computed as its twin above (py negated: the very same arithmetic), and so taken
    // from the twin's coverage when that was drawn in this call.
    const float twiceMiddle = 2 * cys;
    const bool mirrored = radii.topLeft == radii.bottomLeft && radii.topRight == radii.bottomRight
                          && std::abs(twiceMiddle) < 1e7f && twiceMiddle == float(int(twiceMiddle));
    const int mirror = mirrored ? int(twiceMiddle) - 1 : 0; // rows y and mirror − y are twins
    const int nRows = region.y1 - region.y0;
    std::vector<std::uint8_t> &coverageRows = cx.scratch.bytes;
    std::vector<int> &slots = cx.scratch.slots;
    if (mirrored) {
        slots.assign(std::size_t(nRows), -1);
        coverageRows.resize(std::size_t(nx) * std::size_t((nRows + 1) / 2 + 1));
    } else {
        coverageRows.resize(std::size_t(nx));
    }
    const std::uint32_t source = packed(color);
    const float *alphas = alpha.data();
    int used = 0;

    for (int y = region.y0; y < region.y1; ++y) {
        float py = float(y) + 0.5f - cys;
        const int twin = mirror - y;
        if (mirrored && py > 0) {
            py = -py;
            if (twin >= region.y0 && twin < y && slots[std::size_t(twin - region.y0)] >= 0) {
                const std::uint8_t *cached =
                        coverageRows.data() + std::size_t(slots[std::size_t(twin - region.y0)]) * std::size_t(nx);
                blendCoverage8(cx.row(y) + region.x0, nx, source, cached);
                continue;
            }
        }
        // GPUI's fs_shadow: the blur along y integrated with a few samples over the part of
        // [−3σ, 3σ] where the box is.
        const float low = py - hh;
        const float high = py + hh;
        const float start = std::clamp(-reach, low, high);
        const float end = std::clamp(reach, low, high);
        const float step = (end - start) / kShadowSamples;
        if (!(step > 0))
            continue;
        float weight[kShadowSamples];
        float offset[kShadowSamples];
        for (int i = 0; i < kShadowSamples; ++i) {
            const float yi = start + step * (float(i) + 0.5f);
            const float u = yi / sigma;
            weight[i] = norm * std::exp(-0.5f * u * u) * step;
            offset[i] = py - yi;
        }
        const bool upper = py < 0;

        // alpha = Σ weight × the blur along x of the box's width at the sample's row: narrower
        // where the sample's row crosses a corner.
        const auto half = [&](int a, int b, float corner, bool left) {
            if (a >= b)
                return;
            float straightWeight = 0;
            float curved[kShadowSamples];
            float curvedWeight[kShadowSamples];
            int curves = 0;
            for (int i = 0; i < kShadowSamples; ++i) {
                const float delta = std::min(hh - corner - std::abs(offset[i]), 0.0f);
                if (delta < 0) {
                    curved[curves] = hw - corner + std::sqrt(std::max(0.0f, corner * corner - delta * delta));
                    curvedWeight[curves++] = weight[i];
                } else {
                    straightWeight += weight[i];
                }
            }
            float *al = alpha.data() + a;
            const float *st = straight.data() + a;
            const float *px = offsets.data() + a;
            const int n = b - a;
            for (int j = 0; j < n; ++j)
                al[j] = straightWeight * st[j];
            for (int c = 0; c < curves; ++c) {
                const float w = curvedWeight[c] * 0.5f;
                const float width = curved[c];
                if (width * k >= kSaturated) {
                    if (left) {
                        for (int j = 0; j < n; ++j)
                            al[j] += w * (erfApprox((px[j] + width) * k) + 1);
                    } else {
                        for (int j = 0; j < n; ++j)
                            al[j] += w * (1 - erfApprox((px[j] - width) * k));
                    }
                } else {
                    for (int j = 0; j < n; ++j)
                        al[j] += w * (erfApprox((px[j] + width) * k) - erfApprox((px[j] - width) * k));
                }
            }
        };
        half(0, split, upper ? radii.topLeft : radii.bottomLeft, true);
        half(split, nx, upper ? radii.topRight : radii.bottomRight, false);
        int slot = 0;
        if (mirrored && twin > y && twin < region.y1) {
            slot = ++used;
            slots[std::size_t(y - region.y0)] = slot;
        }
        std::uint8_t *coverage = coverageRows.data() + std::size_t(slot) * std::size_t(nx);
        for (int i = 0; i < nx; ++i)
            coverage[i] = std::uint8_t(coverage8(alphas[i]));
        blendCoverage8(cx.row(y) + region.x0, nx, source, coverage);
    }
    return region.area();
}

// ---- Monochrome sprites (glyphs) ----------------------------------------------------------------

std::size_t draw(Context &cx, const MonochromeSprite &s, const IRect &clip)
{
    const AtlasTile &tile = s.tile;
    if (tile.isEmpty() || tile.page >= cx.atlas.pages().size() || s.bounds.isEmpty())
        return 0;
    const AtlasPage &page = cx.atlas.pages()[tile.page];
    if (page.kind != AtlasKind::Alpha)
        return 0;
    const Paint paint = paintOf(s.color);
    if (!(paint.a > 0))
        return 0;
    const std::optional<int> originX = pixelOrigin(s.bounds.left());
    const std::optional<int> originY = pixelOrigin(s.bounds.top());
    if (!originX || !originY)
        return 0;
    // The tile's texels that exist in its page (a malformed tile may reach past the page's edge),
    // and where they land: texel + shift.
    const std::int64_t tx0 = std::max<std::int64_t>(tile.x, 0);
    const std::int64_t ty0 = std::max<std::int64_t>(tile.y, 0);
    const std::int64_t tx1 = std::min<std::int64_t>(std::int64_t(tile.x) + tile.width, page.width);
    const std::int64_t ty1 = std::min<std::int64_t>(std::int64_t(tile.y) + tile.height, page.height);
    const std::int64_t sx = std::int64_t(*originX) - tile.x;
    const std::int64_t sy = std::int64_t(*originY) - tile.y;
    const auto within = [](std::int64_t v, int lo, int hi) { return int(std::clamp<std::int64_t>(v, lo, hi)); };
    IRect region{within(tx0 + sx, clip.x0, clip.x1), within(ty0 + sy, clip.y0, clip.y1),
                 within(tx1 + sx, clip.x0, clip.x1), within(ty1 + sy, clip.y0, clip.y1)};
    if (region.isEmpty())
        return 0;

    // Every texel blended, none skipped: no branch to mispredict, and coverage 0 leaves a pixel
    // exactly as it was.
    const std::uint32_t source = packed(paint);
    const int n = region.x1 - region.x0;
    for (int y = region.y0; y < region.y1; ++y)
        blendCoverage8(cx.row(y) + region.x0, n, source, page.row(int(y - sy)) + (region.x0 - sx));
    return region.area();
}

// ---- Colour sprites and pictures ----------------------------------------------------------------

// Premultiplied RGBA8 texels and the part of them that may be read: [x0, x1) × [y0, y1).
struct Texture {
    const std::uint8_t *data = nullptr;
    std::ptrdiff_t stride = 0; // bytes per row
    int x0 = 0;
    int y0 = 0;
    int x1 = 0;
    int y1 = 0;
};

// The two texels around `u` (texel i covers [i, i + 1)), clamped to the texture's edge.
inline Tap tapAt(float u, int lo, int hi)
{
    u -= 0.5f;
    const float top = float(hi - 1);
    u = u > float(lo) ? (u < top ? u : top) : float(lo);
    const int i = int(u); // u ≥ lo ≥ 0: truncation is floor
    return {i, std::min(i + 1, hi - 1), u - float(i)};
}

inline Paint texel(const std::uint8_t *row, int i)
{
    const std::uint8_t *p = row + std::ptrdiff_t(i) * 4;
    return {float(p[0]), float(p[1]), float(p[2]), float(p[3])};
}

inline Paint bilinear(const std::uint8_t *r0, const std::uint8_t *r1, const Tap &t, float fv)
{
    const Paint top = lerp(texel(r0, t.i0), texel(r0, t.i1), t.f);
    const Paint bottom = lerp(texel(r1, t.i0), texel(r1, t.i1), t.f);
    return lerp(top, bottom, fv);
}

// Rec. 709 luma of premultiplied components (linear, so the premultiplication carries over).
inline Paint grey(const Paint &p)
{
    const float l = 0.2126f * p.r + 0.7152f * p.g + 0.0722f * p.b;
    return {l, l, l, p.a};
}

// `source` (texel space) scaled into `bounds`, bilinear, cut to the rounded corners.
std::size_t drawImage(Context &cx, const Rect &bounds, const Corners &cornerRadii, const Texture &tex,
                      const Rect &source, float opacity, bool grayscale, const IRect &clip)
{
    if (bounds.isEmpty() || source.isEmpty() || tex.x0 >= tex.x1 || tex.y0 >= tex.y1)
        return 0;
    const float alpha = clamp01(opacity);
    if (!(alpha > 0))
        return 0;
    const IRect region = pixelsCovering(bounds, clip);
    if (region.isEmpty())
        return 0;
    const Corners radii = clampRadii(cornerRadii, bounds.size);
    const RoundedRect shape = RoundedRect::of(limitTo(bounds, cx.full, radii.max() + 2), radii);
    const float du = source.width() / bounds.width();
    const float dv = source.height() / bounds.height();

    const int nx = region.x1 - region.x0;
    std::vector<Tap> &taps = cx.scratch.taps;
    taps.resize(std::size_t(nx));
    for (int i = 0; i < nx; ++i)
        taps[std::size_t(i)] =
                tapAt(source.left() + (float(region.x0 + i) + 0.5f - bounds.left()) * du, tex.x0, tex.x1);
    const Tap *columnTaps = taps.data();

    for (int y = region.y0; y < region.y1; ++y) {
        const float py = float(y) + 0.5f;
        const ShapeRow s = shape.row(py);
        if (!(s.plateau > 0))
            continue;
        const Tap v = tapAt(source.top() + (py - bounds.top()) * dv, tex.y0, tex.y1);
        const std::uint8_t *r0 = tex.data + std::ptrdiff_t(v.i0) * tex.stride;
        const std::uint8_t *r1 = tex.data + std::ptrdiff_t(v.i1) * tex.stride;
        Pixel *row = cx.row(y);
        const auto put = [&](int x, float c) {
            if (!(c > 0))
                return;
            Paint p = bilinear(r0, r1, columnTaps[x - region.x0], v.f) * (c * alpha);
            if (grayscale)
                p = grey(p);
            row[x] = over(row[x], p);
        };
        walkRow(s, region.x0, region.x1, put, [&](int a, int b, float c) {
            for (int x = a; x < b; ++x)
                put(x, c);
        });
    }
    return region.area();
}

std::size_t draw(Context &cx, const ColorSprite &s, const IRect &clip)
{
    const AtlasTile &tile = s.tile;
    if (tile.isEmpty() || tile.page >= cx.atlas.pages().size())
        return 0;
    const AtlasPage &page = cx.atlas.pages()[tile.page];
    if (page.kind != AtlasKind::Color)
        return 0;
    Texture tex;
    tex.data = page.pixels.data();
    tex.stride = std::ptrdiff_t(page.width) * 4;
    tex.x0 = std::clamp(tile.x, 0, page.width);
    tex.y0 = std::clamp(tile.y, 0, page.height);
    tex.x1 = int(std::clamp<std::int64_t>(std::int64_t(tile.x) + tile.width, tex.x0, page.width));
    tex.y1 = int(std::clamp<std::int64_t>(std::int64_t(tile.y) + tile.height, tex.y0, page.height));
    const Rect source = Rect::fromXYWH(float(tile.x), float(tile.y), float(tile.width), float(tile.height));
    return drawImage(cx, s.bounds, Corners{}, tex, source, s.opacity, s.grayscale, clip);
}

std::size_t draw(Context &cx, const Picture &p, const IRect &clip)
{
    const ImageData *image = p.image.get();
    if (!image || image->width <= 0 || image->height <= 0
        || image->pixels.size() < std::size_t(image->width) * std::size_t(image->height))
        return 0;
    const Rect whole = Rect::fromXYWH(0, 0, float(image->width), float(image->height));
    const Rect source = p.source.isEmpty() ? whole : p.source;
    Texture tex;
    tex.data = reinterpret_cast<const std::uint8_t *>(image->pixels.data());
    tex.stride = std::ptrdiff_t(image->width) * 4;
    tex.x0 = floorIn(source.left(), 0, image->width);
    tex.y0 = floorIn(source.top(), 0, image->height);
    tex.x1 = ceilIn(source.right(), 0, image->width);
    tex.y1 = ceilIn(source.bottom(), 0, image->height);
    return drawImage(cx, p.bounds, p.cornerRadii, tex, source, p.opacity, p.grayscale, clip);
}

// ---- Paths --------------------------------------------------------------------------------------

std::size_t draw(Context &cx, const PathPrimitive &p, const IRect &clip)
{
    const std::size_t n = p.points.size();
    if (n < 3)
        return 0;
    const Paint paint = paintOf(p.color);
    if (!(paint.a > 0))
        return 0;
    const IRect region = pixelsCovering(p.bounds, clip);
    if (region.isEmpty())
        return 0;
    for (const Point &pt : p.points) {
        if (!std::isfinite(pt.x) || !std::isfinite(pt.y))
            return 0;
    }
    const int w = region.x1 - region.x0;
    const int h = region.y1 - region.y0;
    CoverageRasterizer &raster = cx.scratch.rasterizer;
    raster.reset(w, h);
    // Region-local coordinates, kept where the rasterizer's slopes stay finite.
    constexpr float kFar = 1e7f;
    const float ox = float(region.x0);
    const float oy = float(region.y0);
    const auto local = [&](const Point &pt) {
        return Point{std::clamp(pt.x - ox, -kFar, kFar), std::clamp(pt.y - oy, -kFar, kFar)};
    };
    std::size_t start = 0;
    const auto contour = [&](std::size_t end) {
        if (end >= start + 2) {
            raster.moveTo(local(p.points[start]));
            for (std::size_t i = start + 1; i < end; ++i)
                raster.lineTo(local(p.points[i]));
            raster.close();
        }
        start = end;
    };
    for (const std::uint32_t e : p.contourEnds)
        contour(std::clamp<std::size_t>(e, start, n));
    if (start < n)
        contour(n);
    std::vector<float> &cov = cx.scratch.coverage;
    raster.accumulate(cov);

    for (int y = region.y0; y < region.y1; ++y)
        blendAlpha(cx.row(y) + region.x0, w, paint, cov.data() + std::size_t(y - region.y0) * std::size_t(w));
    return region.area();
}

// ---- Underlines ---------------------------------------------------------------------------------

std::size_t draw(Context &cx, const Underline &u, const IRect &clip)
{
    if (u.bounds.isEmpty())
        return 0;
    const float height = u.bounds.height();
    const float thickness = u.thickness > 0 ? std::min(u.thickness, height) : 0.0f;
    if (!(thickness > 0))
        return 0;
    if (!u.wavy) {
        Quad line;
        line.bounds =
                Rect::fromLTRB(u.bounds.left(), u.bounds.bottom() - thickness, u.bounds.right(), u.bounds.bottom());
        line.background = Background::solid(u.color);
        return drawQuad(cx, line, clip);
    }
    const Paint paint = paintOf(u.color);
    if (!(paint.a > 0))
        return 0;
    const IRect region = pixelsCovering(u.bounds, clip);
    if (region.isEmpty())
        return 0;
    // A sine around the bounds' middle line whose stroke just fills their height, period 4 ×
    // thickness; the distance to it is |Δy| / √(1 + slope²), anti-aliased over a pixel.
    const float amplitude = (height - thickness) / 2;
    const float middle = u.bounds.top() + height / 2;
    const float period = std::max(4 * thickness, 2.0f);
    const float omega = 2 * std::numbers::pi_v<float> / period;
    const float halfStroke = thickness / 2 + 0.5f;
    const int nx = region.x1 - region.x0;
    std::vector<float> &centre = cx.scratch.columns;
    std::vector<float> &scale = cx.scratch.columns2;
    std::vector<float> &horizontal = cx.scratch.columns3;
    centre.resize(std::size_t(nx));
    scale.resize(std::size_t(nx));
    horizontal.resize(std::size_t(nx));
    for (int i = 0; i < nx; ++i) {
        const float x = float(region.x0 + i);
        const float phase = omega * (x + 0.5f - u.bounds.left());
        const float slope = amplitude * omega * std::cos(phase);
        centre[std::size_t(i)] = middle + amplitude * std::sin(phase);
        scale[std::size_t(i)] = 1 / std::sqrt(1 + slope * slope);
        // The part of the pixel inside the bounds' x span: crisp ends at fractional positions.
        horizontal[std::size_t(i)] = clamp01(std::min(x + 1, u.bounds.right()) - std::max(x, u.bounds.left()));
    }
    std::vector<float> &alpha = cx.scratch.alpha;
    alpha.resize(std::size_t(nx));
    // Plain pointers in the loops: a hardened build checks every vector index.
    const float *centres = centre.data();
    const float *scales = scale.data();
    const float *horizontals = horizontal.data();
    float *alphas = alpha.data();
    for (int y = region.y0; y < region.y1; ++y) {
        const float py = float(y) + 0.5f;
        for (int i = 0; i < nx; ++i)
            alphas[i] = clamp01(halfStroke - std::abs(py - centres[i]) * scales[i]) * horizontals[i];
        blendAlpha(cx.row(y) + region.x0, nx, paint, alphas);
    }
    return region.area();
}

// ---- Occlusion ----------------------------------------------------------------------------------

// The pixels a primitive's drawing may touch (sprites are drawn at their rounded origin, the
// tile's size).
Rect drawnBounds(const Primitive &p)
{
    if (const auto *sprite = std::get_if<MonochromeSprite>(&p)) {
        const std::optional<int> x = pixelOrigin(sprite->bounds.left());
        const std::optional<int> y = pixelOrigin(sprite->bounds.top());
        if (!x || !y)
            return {};
        return Rect::fromXYWH(float(*x), float(*y), float(sprite->tile.width), float(sprite->tile.height));
    }
    return primitiveBounds(p);
}

// Tiles of the target that a quad paints fully opaque: whatever is drawn there before that quad
// is never seen, so it is not drawn (nor cleared). No byte changes: an opaque pixel's value does
// not depend on what was under it. It is what makes a panel over a background, a card over its
// shadow and a popup over a page cheap.
class Occlusion {
public:
    static constexpr int kTile = 16;
    enum class Cover : std::uint8_t { None, Partly, Fully };

    int cols = 0;
    int rows = 0;

    Occlusion(const std::vector<Primitive> &primitives, const IRect &clip) : m_clip(clip)
    {
        cols = (clip.x1 - clip.x0 + kTile - 1) / kTile;
        rows = (clip.y1 - clip.y0 + kTile - 1) / kTile;
        m_latest.assign(std::size_t(cols) * std::size_t(rows), 0);
        for (std::size_t i = 0; i < primitives.size(); ++i) {
            if (const auto *q = std::get_if<Quad>(&primitives[i]))
                mark(*q, std::uint32_t(i) + 1);
        }
    }

    // No tile painted over at all: nothing to check.
    bool isEmpty() const { return !m_any; }

    // 1 + the index of the last primitive painting tile (tx, ty) opaque, 0 for none.
    std::uint32_t latest(int tx, int ty) const
    {
        return m_latest[std::size_t(ty) * std::size_t(cols) + std::size_t(tx)];
    }

    // The pixels of tile row ty.
    IRect band(int ty) const
    {
        return {m_clip.x0, m_clip.y0 + ty * kTile, m_clip.x1, std::min(m_clip.y1, m_clip.y0 + (ty + 1) * kTile)};
    }

    // How much of `r` primitive `self` (1 + index) draws under a later opaque quad.
    Cover cover(const IRect &r, std::uint32_t self) const
    {
        const int tx0 = (r.x0 - m_clip.x0) / kTile;
        const int tx1 = (r.x1 - 1 - m_clip.x0) / kTile + 1;
        const int ty0 = (r.y0 - m_clip.y0) / kTile;
        const int ty1 = (r.y1 - 1 - m_clip.y0) / kTile + 1;
        bool hidden = false;
        bool shown = false;
        for (int ty = ty0; ty < ty1; ++ty) {
            const std::uint32_t *row = m_latest.data() + std::size_t(ty) * std::size_t(cols);
            for (int tx = tx0; tx < tx1; ++tx) {
                if (row[tx] > self)
                    hidden = true;
                else
                    shown = true;
            }
            if (hidden && shown)
                return Cover::Partly;
        }
        return hidden ? Cover::Fully : Cover::None;
    }

    template<typename Draw>
    void forEachVisibleBand(const IRect &r, std::uint32_t self, Draw &&draw) const
    {
        const int tx0 = (r.x0 - m_clip.x0) / kTile;
        const int tx1 = (r.x1 - 1 - m_clip.x0) / kTile + 1;
        const int ty0 = (r.y0 - m_clip.y0) / kTile;
        const int ty1 = (r.y1 - 1 - m_clip.y0) / kTile + 1;
        for (int ty = ty0; ty < ty1; ++ty) {
            int a = tx0;
            while (a < tx1 && latest(a, ty) > self)
                ++a;
            if (a == tx1)
                continue;
            int b = tx1 - 1;
            while (latest(b, ty) > self)
                --b;
            const IRect b0 = band(ty);
            const IRect part = IRect{m_clip.x0 + a * kTile, b0.y0, m_clip.x0 + (b + 1) * kTile, b0.y1}.intersected(r);
            if (!part.isEmpty())
                draw(part);
        }
    }

private:
    // A quad with an opaque fill paints every pixel it covers fully as opaque (a border of any
    // alpha lies over the fill): the cross of its straight parts. Float's rounding at an edge
    // cannot decide: a coverage within 10⁻⁶ of 1 gives alpha 255 and the same bytes.
    void mark(const Quad &q, std::uint32_t self)
    {
        if (q.bounds.isEmpty())
            return;
        const bool opaque = q.background.color.a >= 1
                            && (q.background.kind == Background::Kind::Solid || q.background.color2.a >= 1);
        if (!opaque)
            return;
        const IRect visible = pixelsCovering(q.mask.bounds, m_clip);
        if (visible.isEmpty())
            return;
        const float r = std::max(0.5f, clampRadii(q.cornerRadii, q.bounds.size).max());
        const float l = q.bounds.left();
        const float t = q.bounds.top();
        const float rt = q.bounds.right();
        const float b = q.bounds.bottom();
        const auto inner = [&](float x0, float y0, float x1, float y1) {
            // Whole pixels whose centres lie at least half a pixel inside.
            return IRect{ceilIn(x0, visible.x0, visible.x1), ceilIn(y0, visible.y0, visible.y1),
                         floorIn(x1, visible.x0, visible.x1), floorIn(y1, visible.y0, visible.y1)};
        };
        fill(inner(l, t + r - 0.5f, rt, b - r + 0.5f), self);
        fill(inner(l + r - 0.5f, t, rt - r + 0.5f, b), self);
    }

    // Tiles fully inside `r`.
    void fill(const IRect &r, std::uint32_t self)
    {
        if (r.isEmpty())
            return;
        const int ox = r.x0 - m_clip.x0;
        const int oy = r.y0 - m_clip.y0;
        const int tx0 = (ox + kTile - 1) / kTile;
        const int ty0 = (oy + kTile - 1) / kTile;
        // A tile at the clip's right or bottom edge is smaller: it counts when r reaches that edge.
        const int ex = r.x1 - m_clip.x0;
        const int ey = r.y1 - m_clip.y0;
        const int tx1 = r.x1 == m_clip.x1 ? cols : ex / kTile;
        const int ty1 = r.y1 == m_clip.y1 ? rows : ey / kTile;
        for (int ty = ty0; ty < ty1; ++ty) {
            for (int tx = tx0; tx < tx1; ++tx) {
                m_latest[std::size_t(ty) * std::size_t(cols) + std::size_t(tx)] = self;
                m_any = true;
            }
        }
    }

    IRect m_clip;
    std::vector<std::uint32_t> m_latest;
    bool m_any = false;
};

} // namespace

Pixel blendOver(Pixel dst, PremulColor src, float coverage)
{
    return unpack(overPacked(pack(dst), scale(packed(paintOf(src)), coverage8(clamp01(coverage)))));
}

RasterStats RasterRenderer::render(const Scene &scene, const Atlas &atlas, RasterTarget &target, Color clear,
                                   std::optional<Rect> damage)
{
    RasterStats stats;
    if (target.width <= 0 || target.height <= 0
        || target.pixels.size() < std::size_t(target.width) * std::size_t(target.height))
        return stats;
    const IRect full{0, 0, target.width, target.height};
    const IRect clip = damage ? pixelsCovering(*damage, full) : full;
    if (clip.isEmpty())
        return stats;

    Scratch scratch;
    Context cx{target, atlas, full, scratch};
    const std::vector<Primitive> &primitives = scene.primitives();
    const Occlusion occlusion(primitives, clip);

    // The clear, but for tiles an opaque quad paints over anyway.
    const Pixel background = solid(paintOf(clear));
    for (int ty = 0; ty < occlusion.rows; ++ty) {
        const IRect band = occlusion.band(ty);
        for (int tx = 0; tx < occlusion.cols;) {
            if (occlusion.latest(tx, ty) != 0) {
                ++tx;
                continue;
            }
            int end = tx + 1;
            while (end < occlusion.cols && occlusion.latest(end, ty) == 0)
                ++end;
            const int x0 = band.x0 + tx * Occlusion::kTile;
            const int x1 = std::min(band.x1, band.x0 + end * Occlusion::kTile);
            for (int y = band.y0; y < band.y1; ++y) {
                Pixel *row = cx.row(y);
                std::fill(row + x0, row + x1, background);
            }
            tx = end;
        }
    }

    for (std::size_t i = 0; i < primitives.size(); ++i) {
        const Primitive &primitive = primitives[i];
        const ContentMask &mask = std::visit([](const auto &p) -> const ContentMask & { return p.mask; }, primitive);
        const IRect visible = pixelsCovering(mask.bounds, clip);
        if (visible.isEmpty())
            continue;
        const auto drawIn = [&](const IRect &c) {
            return std::visit([&](const auto &p) { return draw(cx, p, c); }, primitive);
        };
        std::size_t touched = 0;
        Occlusion::Cover cover = Occlusion::Cover::None;
        IRect reach;
        if (!occlusion.isEmpty()) {
            reach = pixelsCovering(drawnBounds(primitive), visible);
            if (reach.isEmpty())
                continue;
            cover = occlusion.cover(reach, std::uint32_t(i) + 1);
        }
        if (cover == Occlusion::Cover::None) {
            touched = drawIn(visible);
        } else if (cover == Occlusion::Cover::Partly) {
            // Band by band of tiles, from the first to the last tile not painted over later.
            occlusion.forEachVisibleBand(reach, std::uint32_t(i) + 1,
                                         [&](const IRect &part) { touched += drawIn(part); });
        }
        if (touched > 0) {
            ++stats.primitives;
            stats.pixelsTouched += touched;
        }
    }
    return stats;
}

} // namespace brulion::atrament
