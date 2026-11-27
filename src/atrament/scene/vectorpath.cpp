#include "scene/vectorpath.h"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace brulion::atrament {

namespace {

constexpr float kPi = std::numbers::pi_v<float>;
// A quarter of a circle as a cubic: the control points' distance in radii.
constexpr float kKappa = 0.5522847498f;
// The most pieces one curve or one dash pattern is cut into (malformed input stays bounded).
constexpr int kMaxCurveSteps = 256;
constexpr std::size_t kMaxDashPieces = 20000;

float length(Point p)
{
    return std::sqrt(p.x * p.x + p.y * p.y);
}

bool finite(Point p)
{
    return std::isfinite(p.x) && std::isfinite(p.y);
}

int curveSteps(float secondDifference, float factor, float tolerance)
{
    // Wang's formula: n = sqrt(d (d − 1) / 8 · M / tolerance) for a curve of degree d.
    if (!(secondDifference > 0) || !(tolerance > 0))
        return 1;
    const float n = std::ceil(std::sqrt(factor * secondDifference / tolerance));
    return std::isfinite(n) ? std::clamp(int(n), 1, kMaxCurveSteps) : kMaxCurveSteps;
}

} // namespace

// --- VectorPath ---------------------------------------------------------------------------------

void VectorPath::moveTo(Point p)
{
    m_ops.push_back({Op::Kind::Move, p, {}, {}});
    m_current = p;
    m_start = p;
    m_open = true;
}

void VectorPath::ensureStarted()
{
    if (!m_open)
        moveTo(m_current); // after close(): a new contour from the closed one's start
}

void VectorPath::lineTo(Point p)
{
    ensureStarted();
    m_ops.push_back({Op::Kind::Line, p, {}, {}});
    m_current = p;
}

void VectorPath::quadTo(Point control, Point p)
{
    ensureStarted();
    m_ops.push_back({Op::Kind::Quad, p, control, {}});
    m_current = p;
}

void VectorPath::cubicTo(Point control1, Point control2, Point p)
{
    ensureStarted();
    m_ops.push_back({Op::Kind::Cubic, p, control1, control2});
    m_current = p;
}

void VectorPath::close()
{
    if (!m_open)
        return;
    m_ops.push_back({Op::Kind::Close, m_start, {}, {}});
    m_current = m_start;
    m_open = false;
}

void VectorPath::arcTo(float rx, float ry, float rotationDegrees, bool largeArc, bool sweep, Point p)
{
    ensureStarted();
    const Point p0 = m_current;
    if (!finite(p) || !std::isfinite(rx) || !std::isfinite(ry) || !std::isfinite(rotationDegrees))
        return;
    if (p0 == p)
        return; // SVG: no arc between equal points
    rx = std::abs(rx);
    ry = std::abs(ry);
    if (rx == 0 || ry == 0) {
        lineTo(p);
        return;
    }
    // SVG 1.1 F.6.5: the endpoints to the centre.
    const float phi = rotationDegrees * kPi / 180.0f;
    const float cosPhi = std::cos(phi);
    const float sinPhi = std::sin(phi);
    const float dx = (p0.x - p.x) / 2;
    const float dy = (p0.y - p.y) / 2;
    const float x1 = cosPhi * dx + sinPhi * dy;
    const float y1 = -sinPhi * dx + cosPhi * dy;
    // Radii too small to join the points grow (F.6.6).
    const float lambda = (x1 * x1) / (rx * rx) + (y1 * y1) / (ry * ry);
    if (lambda > 1) {
        const float s = std::sqrt(lambda);
        rx *= s;
        ry *= s;
    }
    const float num = rx * rx * ry * ry - rx * rx * y1 * y1 - ry * ry * x1 * x1;
    const float den = rx * rx * y1 * y1 + ry * ry * x1 * x1;
    float coef = den > 0 ? std::sqrt(std::max(0.0f, num / den)) : 0;
    if (largeArc == sweep)
        coef = -coef;
    const float cx1 = coef * rx * y1 / ry;
    const float cy1 = -coef * ry * x1 / rx;
    const Point c{cosPhi * cx1 - sinPhi * cy1 + (p0.x + p.x) / 2, sinPhi * cx1 + cosPhi * cy1 + (p0.y + p.y) / 2};
    auto angle = [](float ux, float uy, float vx, float vy) {
        return std::atan2(ux * vy - uy * vx, ux * vx + uy * vy);
    };
    const float ux = (x1 - cx1) / rx;
    const float uy = (y1 - cy1) / ry;
    const float vx = (-x1 - cx1) / rx;
    const float vy = (-y1 - cy1) / ry;
    const float theta = angle(1, 0, ux, uy);
    float delta = angle(ux, uy, vx, vy);
    if (!sweep && delta > 0)
        delta -= 2 * kPi;
    else if (sweep && delta < 0)
        delta += 2 * kPi;
    if (!std::isfinite(theta) || !std::isfinite(delta)) {
        lineTo(p);
        return;
    }
    // Pieces of at most a quarter turn, each a cubic.
    const int pieces = std::clamp(int(std::ceil(std::abs(delta) / (kPi / 2) - 1e-4f)), 1, 4);
    const float step = delta / float(pieces);
    const float k = 4.0f / 3.0f * std::tan(step / 4);
    auto onEllipse = [&](float a) {
        const float ex = rx * std::cos(a);
        const float ey = ry * std::sin(a);
        return Point{c.x + cosPhi * ex - sinPhi * ey, c.y + sinPhi * ex + cosPhi * ey};
    };
    auto derivative = [&](float a) {
        const float ex = -rx * std::sin(a);
        const float ey = ry * std::cos(a);
        return Point{cosPhi * ex - sinPhi * ey, sinPhi * ex + cosPhi * ey};
    };
    float a = theta;
    for (int i = 0; i < pieces; ++i) {
        const float b = a + step;
        const Point from = onEllipse(a);
        const Point to = i + 1 == pieces ? p : onEllipse(b);
        cubicTo(from + derivative(a) * k, onEllipse(b) - derivative(b) * k, to);
        a = b;
    }
}

void VectorPath::addRect(Rect r, float rx, float ry)
{
    if (!(r.width() > 0) || !(r.height() > 0))
        return;
    rx = std::clamp(std::isfinite(rx) ? rx : 0.0f, 0.0f, r.width() / 2);
    ry = std::clamp(std::isfinite(ry) ? ry : 0.0f, 0.0f, r.height() / 2);
    const float l = r.left(), t = r.top(), ri = r.right(), b = r.bottom();
    if (rx == 0 || ry == 0) {
        moveTo({l, t});
        lineTo({ri, t});
        lineTo({ri, b});
        lineTo({l, b});
        close();
        return;
    }
    const float kx = rx * kKappa;
    const float ky = ry * kKappa;
    moveTo({l + rx, t});
    lineTo({ri - rx, t});
    cubicTo({ri - rx + kx, t}, {ri, t + ry - ky}, {ri, t + ry});
    lineTo({ri, b - ry});
    cubicTo({ri, b - ry + ky}, {ri - rx + kx, b}, {ri - rx, b});
    lineTo({l + rx, b});
    cubicTo({l + rx - kx, b}, {l, b - ry + ky}, {l, b - ry});
    lineTo({l, t + ry});
    cubicTo({l, t + ry - ky}, {l + rx - kx, t}, {l + rx, t});
    close();
}

void VectorPath::addEllipse(Point center, float rx, float ry)
{
    rx = std::abs(rx);
    ry = std::abs(ry);
    if (!(rx > 0) || !(ry > 0) || !finite(center))
        return;
    const float kx = rx * kKappa;
    const float ky = ry * kKappa;
    const float x = center.x, y = center.y;
    moveTo({x + rx, y});
    cubicTo({x + rx, y + ky}, {x + kx, y + ry}, {x, y + ry});
    cubicTo({x - kx, y + ry}, {x - rx, y + ky}, {x - rx, y});
    cubicTo({x - rx, y - ky}, {x - kx, y - ry}, {x, y - ry});
    cubicTo({x + kx, y - ry}, {x + rx, y - ky}, {x + rx, y});
    close();
}

Rect VectorPath::bounds() const
{
    if (m_ops.empty())
        return {};
    float l = m_ops[0].p.x, t = m_ops[0].p.y, r = l, b = t;
    auto add = [&](Point p) {
        l = std::min(l, p.x);
        t = std::min(t, p.y);
        r = std::max(r, p.x);
        b = std::max(b, p.y);
    };
    for (const Op &op : m_ops) {
        add(op.p);
        if (op.kind == Op::Kind::Quad || op.kind == Op::Kind::Cubic)
            add(op.c1);
        if (op.kind == Op::Kind::Cubic)
            add(op.c2);
    }
    return Rect::fromLTRB(l, t, r, b);
}

std::vector<FlatContour> VectorPath::flatten(const PathTransform &t, float tolerance) const
{
    std::vector<FlatContour> out;
    FlatContour current;
    Point last;
    auto finish = [&] {
        if (!current.points.empty())
            out.push_back(std::move(current));
        current = {};
    };
    auto add = [&](Point p) {
        if (finite(p))
            current.points.push_back(p);
        last = p;
    };
    for (const Op &op : m_ops) {
        switch (op.kind) {
        case Op::Kind::Move:
            finish();
            add(t.apply(op.p));
            break;
        case Op::Kind::Line: add(t.apply(op.p)); break;
        case Op::Kind::Quad: {
            const Point p0 = last;
            const Point c = t.apply(op.c1);
            const Point p = t.apply(op.p);
            const int n = curveSteps(length(p0 - c * 2 + p), 0.25f, tolerance);
            for (int i = 1; i <= n; ++i) {
                const float s = float(i) / float(n);
                const float m = 1 - s;
                add(i == n ? p : p0 * (m * m) + c * (2 * m * s) + p * (s * s));
            }
            break;
        }
        case Op::Kind::Cubic: {
            const Point p0 = last;
            const Point c1 = t.apply(op.c1);
            const Point c2 = t.apply(op.c2);
            const Point p = t.apply(op.p);
            const float m2 = std::max(length(p0 - c1 * 2 + c2), length(c1 - c2 * 2 + p));
            const int n = curveSteps(m2, 0.75f, tolerance);
            for (int i = 1; i <= n; ++i) {
                const float s = float(i) / float(n);
                const float m = 1 - s;
                add(i == n ? p
                           : p0 * (m * m * m) + c1 * (3 * m * m * s) + c2 * (3 * m * s * s) + p * (s * s * s));
            }
            break;
        }
        case Op::Kind::Close:
            current.closed = true;
            last = t.apply(op.p);
            finish();
            break;
        }
    }
    finish();
    return out;
}

// --- fill and stroke ----------------------------------------------------------------------------

void fillContours(std::span<const FlatContour> contours, PathBuilder &out)
{
    for (const FlatContour &c : contours) {
        if (c.points.size() < 3)
            continue;
        out.moveTo(c.points.front());
        for (std::size_t i = 1; i < c.points.size(); ++i)
            out.lineTo(c.points[i]);
        out.close();
    }
}

std::vector<FlatContour> dashContours(std::span<const FlatContour> contours, std::span<const float> dashes,
                                      float offset)
{
    std::vector<float> pattern;
    float period = 0;
    for (const float d : dashes) {
        if (!std::isfinite(d) || d < 0)
            return {contours.begin(), contours.end()}; // SVG: an invalid pattern is solid
        pattern.push_back(d);
        period += d;
    }
    if (pattern.size() % 2 == 1) // an odd list repeats once (SVG)
        pattern.insert(pattern.end(), pattern.begin(), pattern.end());
    period *= pattern.size() > dashes.size() ? 2 : 1;
    if (pattern.empty() || !(period > 0))
        return {contours.begin(), contours.end()};
    // A pattern far too fine for the line's length is solid (bounded work for any input).
    float total = 0;
    for (const FlatContour &c : contours)
        for (std::size_t i = 1; i < c.points.size() + (c.closed ? 1 : 0); ++i)
            total += length(c.points[i % c.points.size()] - c.points[i - 1]);
    if (total / period * float(pattern.size()) > float(kMaxDashPieces))
        return {contours.begin(), contours.end()};

    std::vector<FlatContour> out;
    for (const FlatContour &c : contours) {
        if (c.points.empty())
            continue;
        std::vector<Point> pts = c.points;
        if (c.closed && pts.size() > 1)
            pts.push_back(pts.front());
        // Where the pattern is at the line's start.
        float phase = std::fmod(std::isfinite(offset) ? offset : 0.0f, period);
        if (phase < 0)
            phase += period;
        std::size_t index = 0;
        for (std::size_t guard = 0; guard < 4 * pattern.size(); ++guard) {
            const float len = pattern[index];
            if (phase < len || (len == 0 && phase == 0))
                break;
            phase -= len;
            index = (index + 1) % pattern.size();
        }
        float remaining = pattern[index] - phase;
        bool on = index % 2 == 0;
        FlatContour piece;
        if (on)
            piece.points.push_back(pts.front());
        for (std::size_t i = 1; i < pts.size(); ++i) {
            Point a = pts[i - 1];
            const Point b = pts[i];
            float segment = length(b - a);
            while (segment > remaining) {
                const Point at = a + (b - a) * (remaining / segment);
                if (on) {
                    piece.points.push_back(at);
                    out.push_back(std::move(piece));
                    piece = {};
                } else {
                    piece.points.push_back(at);
                }
                segment -= remaining;
                a = at;
                index = (index + 1) % pattern.size();
                remaining = pattern[index];
                on = !on;
                if (out.size() > kMaxDashPieces)
                    return out;
            }
            remaining -= segment;
            if (on)
                piece.points.push_back(b);
        }
        if (on && !piece.points.empty())
            out.push_back(std::move(piece));
    }
    return out;
}

namespace {

// Polygons of one orientation (positive area, y down: clockwise on screen) into the builder.
class OutlineWriter {
public:
    explicit OutlineWriter(PathBuilder &out) : m_out(out) { }

    void polygon(std::span<const Point> pts)
    {
        if (pts.size() < 3)
            return;
        float area = 0;
        for (std::size_t i = 0; i < pts.size(); ++i) {
            const Point a = pts[i];
            const Point b = pts[(i + 1) % pts.size()];
            area += a.x * b.y - b.x * a.y;
        }
        if (!(std::abs(area) > 1e-9f))
            return; // nothing to fill
        if (area > 0) {
            m_out.moveTo(pts[0]);
            for (std::size_t i = 1; i < pts.size(); ++i)
                m_out.lineTo(pts[i]);
        } else {
            m_out.moveTo(pts[pts.size() - 1]);
            for (std::size_t i = pts.size() - 1; i-- > 0;)
                m_out.lineTo(pts[i]);
        }
        m_out.close();
    }
    void disc(Point c, float r, float tolerance)
    {
        const float x = std::max(-1.0f, 1.0f - tolerance / std::max(r, 1e-6f));
        const float step = 2 * std::acos(x);
        const int n = std::clamp(int(std::ceil(2 * kPi / std::max(step, 1e-3f))), 8, 128);
        // The polygon as large as the circle (an inscribed one is smaller: dots and round caps would
        // lose ink), half inside it and half outside.
        const float k = std::sqrt(2 * kPi / (float(n) * std::sin(2 * kPi / float(n))));
        m_scratch.clear();
        for (int i = 0; i < n; ++i) {
            const float a = 2 * kPi * float(i) / float(n);
            m_scratch.push_back({c.x + r * k * std::cos(a), c.y + r * k * std::sin(a)});
        }
        polygon(m_scratch);
    }

private:
    PathBuilder &m_out;
    std::vector<Point> m_scratch;
};

Point normalized(Point d)
{
    const float l = length(d);
    return l > 0 ? d / l : Point{1, 0};
}

void strokeContour(const FlatContour &contour, const StrokeStyle &style, float h, float tolerance,
                   OutlineWriter &w)
{
    // Points without repeats (a zero-length segment has no direction).
    std::vector<Point> pts;
    pts.reserve(contour.points.size());
    for (const Point &p : contour.points) {
        if (!finite(p))
            continue;
        if (pts.empty() || length(p - pts.back()) > 1e-4f)
            pts.push_back(p);
    }
    bool closed = contour.closed;
    if (closed && pts.size() > 1 && length(pts.front() - pts.back()) <= 1e-4f)
        pts.pop_back();
    if (pts.empty())
        return;
    if (pts.size() == 1) {
        // A dot: what the caps make of a line of no length (SVG).
        if (style.cap == LineCap::Round)
            w.disc(pts[0], h, tolerance);
        else if (style.cap == LineCap::Square) {
            const Point p = pts[0];
            const Point sq[] = {{p.x - h, p.y - h}, {p.x + h, p.y - h}, {p.x + h, p.y + h}, {p.x - h, p.y + h}};
            w.polygon(sq);
        }
        return;
    }
    const std::size_t n = pts.size();
    const std::size_t segments = closed ? n : n - 1;
    std::vector<Point> dirs(segments);
    for (std::size_t i = 0; i < segments; ++i)
        dirs[i] = normalized(pts[(i + 1) % n] - pts[i]);
    // Segments.
    for (std::size_t i = 0; i < segments; ++i) {
        const Point a = pts[i];
        const Point b = pts[(i + 1) % n];
        const Point nrm = Point{-dirs[i].y, dirs[i].x} * h;
        const Point quad[] = {a + nrm, b + nrm, b - nrm, a - nrm};
        w.polygon(quad);
    }
    // Joins: every corner between two segments.
    const float flat = h > tolerance ? 2 * std::acos(std::clamp(1 - tolerance / h, -1.0f, 1.0f)) : kPi;
    auto join = [&](Point v, Point din, Point dout) {
        const float cross = din.x * dout.y - din.y * dout.x;
        const float dot = din.x * dout.x + din.y * dout.y;
        const float turn = std::atan2(std::abs(cross), dot); // 0 … π
        if (turn < 1e-4f)
            return; // straight on
        const float s = cross > 0 ? -1.0f : 1.0f; // the outer side
        const Point p1 = v + Point{-din.y, din.x} * (s * h);
        const Point p2 = v + Point{-dout.y, dout.x} * (s * h);
        if (style.join == LineJoin::Round) {
            if (turn < flat) {
                // A wedge closer to the arc than the tolerance.
                const Point wedge[] = {v, p1, p2};
                w.polygon(wedge);
            } else {
                w.disc(v, h, tolerance);
            }
            return;
        }
        const float half = std::cos(turn / 2);
        const float limit = std::max(1.0f, std::isfinite(style.miterLimit) ? style.miterLimit : 4.0f);
        if (style.join == LineJoin::Miter && half > 1e-4f && 1 / half <= limit) {
            const Point bisector = normalized(p1 + p2 - v * 2);
            const Point miter = v + bisector * (h / half);
            const Point shape[] = {v, p1, miter, p2};
            w.polygon(shape);
            return;
        }
        const Point bevel[] = {v, p1, p2};
        w.polygon(bevel);
    };
    if (closed) {
        for (std::size_t i = 0; i < n; ++i)
            join(pts[i], dirs[(i + segments - 1) % segments], dirs[i]);
    } else {
        for (std::size_t i = 1; i + 1 < n; ++i)
            join(pts[i], dirs[i - 1], dirs[i]);
        // Caps at both ends.
        auto cap = [&](Point p, Point outward) {
            if (style.cap == LineCap::Round) {
                w.disc(p, h, tolerance);
            } else if (style.cap == LineCap::Square) {
                const Point nrm = Point{-outward.y, outward.x} * h;
                const Point e = outward * h;
                const Point sq[] = {p + nrm, p + nrm + e, p - nrm + e, p - nrm};
                w.polygon(sq);
            }
        };
        cap(pts.front(), -dirs.front());
        cap(pts.back(), dirs.back());
    }
}

} // namespace

void strokeContours(std::span<const FlatContour> contours, const StrokeStyle &style, PathBuilder &out,
                    float tolerance)
{
    const float h = style.width / 2;
    if (!(h > 0) || !std::isfinite(h))
        return;
    OutlineWriter w(out);
    tolerance = tolerance > 0 ? tolerance : 0.2f;
    if (!style.dashes.empty()) {
        const std::vector<FlatContour> pieces = dashContours(contours, style.dashes, style.dashOffset);
        for (const FlatContour &c : pieces)
            strokeContour(c, style, h, tolerance, w);
        return;
    }
    for (const FlatContour &c : contours)
        strokeContour(c, style, h, tolerance, w);
}

} // namespace brulion::atrament
