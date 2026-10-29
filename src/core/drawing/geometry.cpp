#include "drawing/geometry.h"

#include "drawing/stroke.h"

#include <QtMath>

#include <algorithm>
#include <cmath>
#include <numbers>

using namespace Qt::StringLiterals;

namespace brulion::drawing {

namespace {

constexpr double Kappa = 0.5522847498; // a quarter circle as a cubic Bézier
constexpr double HatchAngle = -41 * std::numbers::pi / 180;

QRectF pointsRectOf(const QList<QPointF> &points)
{
    if (points.isEmpty())
        return {};
    double minX = points.first().x(), maxX = minX, minY = points.first().y(), maxY = minY;
    for (const QPointF &p : points) {
        minX = std::min(minX, p.x());
        maxX = std::max(maxX, p.x());
        minY = std::min(minY, p.y());
        maxY = std::max(maxY, p.y());
    }
    return QRectF(QPointF(minX, minY), QPointF(maxX, maxY));
}

bool transparent(const QString &color)
{
    const QString c = color.trimmed();
    return c.isEmpty() || c == u"transparent" || c == u"none";
}

double distance(QPointF a, QPointF b)
{
    return std::hypot(a.x() - b.x(), a.y() - b.y());
}

double pointSegment(QPointF p, QPointF s, QPointF e)
{
    const QPointF se = e - s;
    const double len2 = QPointF::dotProduct(se, se);
    double t = len2 > 0 ? QPointF::dotProduct(p - s, se) / len2 : 0;
    t = std::clamp(t, 0.0, 1.0);
    return distance(p, s + se * t);
}

/// Excalidraw's getCornerRadius for a side `x`.
double radiusFor(double x, int roundness)
{
    if (roundness == 1 || roundness == 2)
        return x * 0.25;
    if (roundness == 3)
        return x <= 128 ? x * 0.25 : 32;
    return 0;
}

Path polygonPath(const QList<QPointF> &points, bool close)
{
    Path p;
    for (qsizetype i = 0; i < points.size(); ++i)
        p.append({i == 0 ? PathOp::Move : PathOp::Line, points.at(i), {}, {}});
    if (close && !points.isEmpty())
        p.append({PathOp::Close, {}, {}, {}});
    return p;
}

/// A closed polygon with every corner rounded by a quadratic curve through it, `radius(i)` along
/// both edges (at most half of each).
Path roundedPolygon(const QList<QPointF> &corners, const QList<double> &radii)
{
    const qsizetype n = corners.size();
    Path p;
    if (n < 3)
        return polygonPath(corners, true);
    const auto towards = [](QPointF from, QPointF to, double r) {
        const double len = distance(from, to);
        return len > 0 ? from + (to - from) * (std::min(r, len / 2) / len) : from;
    };
    for (qsizetype i = 0; i < n; ++i) {
        const QPointF prev = corners.at((i + n - 1) % n);
        const QPointF c = corners.at(i);
        const QPointF next = corners.at((i + 1) % n);
        const QPointF in = towards(c, prev, radii.at(i));
        const QPointF out = towards(c, next, radii.at(i));
        p.append({i == 0 ? PathOp::Move : PathOp::Line, in, {}, {}});
        p.append({PathOp::Quad, c, out, {}});
    }
    p.append({PathOp::Close, {}, {}, {}});
    return p;
}

Path rectanglePath(const QRectF &r, double radius)
{
    if (radius <= 0) {
        return polygonPath({r.topLeft(), r.topRight(), r.bottomRight(), r.bottomLeft()}, true);
    }
    // Excalidraw's: M r 0 L w−r 0 Q w 0, w r … (the corners as quadratic curves).
    const double x = r.left(), y = r.top(), w = r.width(), h = r.height();
    const double k = std::min({radius, w / 2, h / 2});
    Path p;
    p.append({PathOp::Move, {x + k, y}, {}, {}});
    p.append({PathOp::Line, {x + w - k, y}, {}, {}});
    p.append({PathOp::Quad, {x + w, y}, {x + w, y + k}, {}});
    p.append({PathOp::Line, {x + w, y + h - k}, {}, {}});
    p.append({PathOp::Quad, {x + w, y + h}, {x + w - k, y + h}, {}});
    p.append({PathOp::Line, {x + k, y + h}, {}, {}});
    p.append({PathOp::Quad, {x, y + h}, {x, y + h - k}, {}});
    p.append({PathOp::Line, {x, y + k}, {}, {}});
    p.append({PathOp::Quad, {x, y}, {x + k, y}, {}});
    p.append({PathOp::Close, {}, {}, {}});
    return p;
}

Path ellipsePath(const QRectF &r)
{
    const double cx = r.center().x(), cy = r.center().y();
    const double rx = r.width() / 2, ry = r.height() / 2;
    const double kx = rx * Kappa, ky = ry * Kappa;
    Path p;
    p.append({PathOp::Move, {cx + rx, cy}, {}, {}});
    p.append({PathOp::Cubic, {cx + rx, cy + ky}, {cx + kx, cy + ry}, {cx, cy + ry}});
    p.append({PathOp::Cubic, {cx - kx, cy + ry}, {cx - rx, cy + ky}, {cx - rx, cy}});
    p.append({PathOp::Cubic, {cx - rx, cy - ky}, {cx - kx, cy - ry}, {cx, cy - ry}});
    p.append({PathOp::Cubic, {cx + kx, cy - ry}, {cx + rx, cy - ky}, {cx + rx, cy}});
    p.append({PathOp::Close, {}, {}, {}});
    return p;
}

QList<QPointF> diamondCorners(const QRectF &r)
{
    return {QPointF(r.center().x(), r.top()), QPointF(r.right(), r.center().y()), QPointF(r.center().x(), r.bottom()),
            QPointF(r.left(), r.center().y())};
}

Path diamondPath(const Stroke &e)
{
    const QList<QPointF> corners = diamondCorners(e.box);
    if (e.roundness == 0)
        return polygonPath(corners, true);
    // (Excalidraw: proportional to the half diagonals.)
    const double v = radiusFor(e.box.width() / 2, e.roundness == 3 ? 2 : e.roundness);
    const double h = radiusFor(e.box.height() / 2, e.roundness == 3 ? 2 : e.roundness);
    const double r = std::min(v, h);
    return roundedPolygon(corners, {r, r, r, r});
}

QList<QPointF> pointsOf(const Stroke &e)
{
    QList<QPointF> out;
    out.reserve(e.points.size());
    for (const StrokePoint &p : e.points)
        out.append(QPointF(p.x, p.y));
    return out;
}

/// A line's or an arrow's path: straight segments, or a Catmull-Rom curve with roundness and three
/// points or more (not for elbow arrows); a closed polygon when `polygon`.
Path linearPath(const Stroke &e)
{
    const QList<QPointF> pts = pointsOf(e);
    const bool closed = e.polygon && e.type == ElementType::Line && pts.size() >= 3;
    if (pts.size() < 3 || e.roundness == 0 || e.elbowed)
        return polygonPath(pts, closed);
    Path p;
    p.append({PathOp::Move, pts.first(), {}, {}});
    const qsizetype n = pts.size();
    const auto at = [&](qsizetype i) {
        if (closed)
            return pts.at((i + n) % n);
        return pts.at(std::clamp<qsizetype>(i, 0, n - 1));
    };
    const qsizetype segments = closed ? n : n - 1;
    for (qsizetype i = 0; i < segments; ++i) {
        const QPointF p0 = at(i - 1), p1 = at(i), p2 = at(i + 1), p3 = at(i + 2);
        p.append({PathOp::Cubic, p1 + (p2 - p0) / 6, p2 - (p3 - p1) / 6, p2});
    }
    if (closed)
        p.append({PathOp::Close, {}, {}, {}});
    return p;
}

Path outlinePathOf(const QList<QPointF> &outline)
{
    // perfect-freehand's getSvgPathFromStroke: quadratic curves through the midpoints, closed.
    Path p;
    if (outline.isEmpty())
        return p;
    p.reserve(outline.size() + 3);
    p.append({PathOp::Move, outline.first(), {}, {}});
    const qsizetype max = outline.size() - 1;
    for (qsizetype i = 0; i <= max; ++i) {
        const QPointF a = outline.at(i);
        const QPointF next = i == max ? outline.first() : outline.at(i + 1);
        p.append({PathOp::Quad, a, (a + next) / 2, {}});
    }
    p.append({PathOp::Line, outline.first(), {}, {}});
    p.append({PathOp::Close, {}, {}, {}});
    return p;
}

void transformPath(Path &p, QPointF center, double angle)
{
    for (PathOp &op : p) {
        op.a = rotatePoint(op.a, center, angle);
        op.b = rotatePoint(op.b, center, angle);
        op.c = rotatePoint(op.c, center, angle);
    }
}

/// Extents grown point by point (QRectF::united ignores empty rects: a point is one).
struct Extent {
    bool any = false;
    double minX = 0, minY = 0, maxX = 0, maxY = 0;

    void add(QPointF p, double pad)
    {
        if (!std::isfinite(p.x()) || !std::isfinite(p.y()))
            return;
        if (!any) {
            minX = maxX = p.x();
            minY = maxY = p.y();
            any = true;
        }
        minX = std::min(minX, p.x() - pad);
        maxX = std::max(maxX, p.x() + pad);
        minY = std::min(minY, p.y() - pad);
        maxY = std::max(maxY, p.y() + pad);
    }
    QRectF rect() const { return any ? QRectF(QPointF(minX, minY), QPointF(maxX, maxY)) : QRectF(); }
};

void addPathBounds(Extent &bounds, const Path &p, double pad)
{
    for (const PathOp &op : p) {
        int count = 0;
        switch (op.kind) {
        case PathOp::Move:
        case PathOp::Line: count = 1; break;
        case PathOp::Quad: count = 2; break;
        case PathOp::Cubic: count = 3; break;
        case PathOp::Close: count = 0; break;
        }
        const QPointF pts[3] = {op.a, op.b, op.c};
        for (int i = 0; i < count; ++i)
            bounds.add(pts[i], pad);
    }
}

/// The segments of the line through `origin` in direction `dir` inside the closed polygons
/// `polygons` (even-odd).
QList<QLineF> clipLine(QPointF origin, QPointF dir, const QList<QList<QPointF>> &polygons)
{
    QList<double> ts;
    for (const QList<QPointF> &poly : polygons) {
        const qsizetype n = poly.size();
        for (qsizetype i = 0; i + 1 < n; ++i) {
            const QPointF a = poly.at(i), b = poly.at(i + 1);
            const QPointF ab = b - a;
            const double denom = dir.x() * ab.y() - dir.y() * ab.x();
            if (std::abs(denom) < 1e-12)
                continue;
            const QPointF ao = a - origin;
            const double t = (ao.x() * ab.y() - ao.y() * ab.x()) / denom;
            const double u = (ao.x() * dir.y() - ao.y() * dir.x()) / denom;
            // Half-open: a vertex the line passes through counts once.
            if (u >= 0 && u < 1)
                ts.append(t);
        }
    }
    std::sort(ts.begin(), ts.end());
    QList<QLineF> out;
    for (qsizetype i = 0; i + 1 < ts.size(); i += 2) {
        if (ts.at(i + 1) - ts.at(i) > 1e-9)
            out.append(QLineF(origin + dir * ts.at(i), origin + dir * ts.at(i + 1)));
    }
    return out;
}

QList<QLineF> clipLineEllipse(QPointF origin, QPointF dir, const QRectF &box)
{
    const double rx = box.width() / 2, ry = box.height() / 2;
    if (rx <= 0 || ry <= 0)
        return {};
    const QPointF c = box.center();
    const double ox = (origin.x() - c.x()) / rx, oy = (origin.y() - c.y()) / ry;
    const double dx = dir.x() / rx, dy = dir.y() / ry;
    const double a = dx * dx + dy * dy, b = 2 * (ox * dx + oy * dy), cc = ox * ox + oy * oy - 1;
    const double disc = b * b - 4 * a * cc;
    if (a <= 0 || disc <= 0)
        return {};
    const double s = std::sqrt(disc);
    const double t1 = (-b - s) / (2 * a), t2 = (-b + s) / (2 * a);
    return {QLineF(origin + dir * t1, origin + dir * t2)};
}

/// Hatch lines over `region` (its bounding box `box`) at `angle`, `gap` apart.
template<typename Clip>
void hatchLines(QList<QLineF> &out, const QRectF &box, double angle, double gap, const Clip &clip)
{
    const QPointF dir(std::cos(angle), std::sin(angle));
    const QPointF normal(-dir.y(), dir.x());
    double lo = 1e300, hi = -1e300;
    for (const QPointF &c : {box.topLeft(), box.topRight(), box.bottomLeft(), box.bottomRight()}) {
        const double v = QPointF::dotProduct(c, normal);
        lo = std::min(lo, v);
        hi = std::max(hi, v);
    }
    if (!(hi > lo) || gap <= 0 || (hi - lo) / gap > 20000)
        return;
    for (double offset = lo + gap / 2; offset < hi; offset += gap)
        out.append(clip(normal * offset, dir));
}

ElementShape localShape(const Stroke &e)
{
    ElementShape s;
    const bool filled = !transparent(e.fill);
    const bool solid = e.fillStyle == FillStyle::Solid;
    const double gap = std::max(4.0, 4 * e.width);
    s.hatchWidth = e.width / 2;
    const auto hatchPolygon = [&](const QList<QPointF> &corners) {
        if (!filled || solid)
            return;
        QList<QPointF> closed = corners;
        closed.append(corners.first());
        const auto clip = [&closed](QPointF o, QPointF d) { return clipLine(o, d, {closed}); };
        const QRectF box = pointsRectOf(corners);
        hatchLines(s.hatch, box, HatchAngle, gap, clip);
        if (e.fillStyle == FillStyle::CrossHatch)
            hatchLines(s.hatch, box, HatchAngle + std::numbers::pi / 2, gap, clip);
    };
    switch (e.type) {
    case ElementType::Freedraw: {
        s.ink = outlinePathOf(strokeOutline(e));
        if (filled && e.points.size() >= 3)
            s.area = polygonPath(pointsOf(e), true);
        break;
    }
    case ElementType::Rectangle:
        s.outline = rectanglePath(e.box, cornerRadius(e));
        if (filled && solid)
            s.area = s.outline;
        hatchPolygon({e.box.topLeft(), e.box.topRight(), e.box.bottomRight(), e.box.bottomLeft()});
        break;
    case ElementType::Ellipse:
        s.outline = ellipsePath(e.box);
        if (filled && solid)
            s.area = s.outline;
        if (filled && !solid) {
            const QRectF box = e.box;
            const auto clip = [&box](QPointF o, QPointF d) { return clipLineEllipse(o, d, box); };
            hatchLines(s.hatch, box, HatchAngle, gap, clip);
            if (e.fillStyle == FillStyle::CrossHatch)
                hatchLines(s.hatch, box, HatchAngle + std::numbers::pi / 2, gap, clip);
        }
        break;
    case ElementType::Diamond:
        s.outline = diamondPath(e);
        if (filled && solid)
            s.area = s.outline;
        hatchPolygon(diamondCorners(e.box));
        break;
    case ElementType::Line:
    case ElementType::Arrow: {
        s.outline = linearPath(e);
        const bool closed = e.polygon && e.type == ElementType::Line && e.points.size() >= 3;
        if (closed && filled && solid)
            s.area = s.outline;
        if (closed)
            hatchPolygon(pointsOf(e));
        if (e.points.size() >= 2) {
            const QList<QPointF> pts = pointsOf(e);
            const auto head = [&](Arrowhead kind, QPointF tip, QPointF from) {
                if (kind == Arrowhead::None)
                    return;
                bool full = false;
                Path h = arrowhead(kind, tip, from, e.width, &full);
                if (h.isEmpty())
                    return;
                s.heads.append(std::move(h));
                s.headFilled.append(full);
            };
            head(e.startArrowhead, pts.first(), pts.at(1));
            head(e.endArrowhead, pts.last(), pts.at(pts.size() - 2));
        }
        break;
    }
    case ElementType::Text:
    case ElementType::Image: break;
    }
    return s;
}

QRectF pointsRectOfStroke(const Stroke &e)
{
    return pointsRect(e.points);
}

} // namespace

QRectF pointsRect(const QList<StrokePoint> &points)
{
    QList<QPointF> pts;
    pts.reserve(points.size());
    for (const StrokePoint &p : points) {
        if (std::isfinite(p.x) && std::isfinite(p.y))
            pts.append(QPointF(p.x, p.y));
    }
    return pointsRectOf(pts);
}

QPointF rotatePoint(QPointF p, QPointF center, double angle)
{
    if (angle == 0)
        return p;
    const double c = std::cos(angle), s = std::sin(angle);
    const QPointF d = p - center;
    return center + QPointF(d.x() * c - d.y() * s, d.x() * s + d.y() * c);
}

QPointF elementCenter(const Stroke &e)
{
    if (hasPoints(e.type))
        return pointsRectOfStroke(e).center();
    return e.box.center();
}

QList<double> dashPattern(const Stroke &e)
{
    switch (e.strokeStyle) {
    case StrokeStyle::Dashed: return {8, 8 + e.width};
    case StrokeStyle::Dotted: return {1.5, 6 + e.width};
    case StrokeStyle::Solid: break;
    }
    return {};
}

double cornerRadius(const Stroke &e)
{
    return radiusFor(std::min(std::abs(e.box.width()), std::abs(e.box.height())), e.roundness);
}

Path arrowhead(Arrowhead head, QPointF tip, QPointF from, double width, bool *filled)
{
    if (filled)
        *filled = false;
    const double length = distance(tip, from);
    if (head == Arrowhead::None || !(length > 0) || !std::isfinite(length))
        return {};
    const QPointF dir = (tip - from) / length;
    const bool diamond = head == Arrowhead::Diamond || head == Arrowhead::DiamondOutline;
    const bool arrow = head == Arrowhead::Arrow || head == Arrowhead::Other;
    const double size = arrow ? 25 : diamond ? 12 : 15;
    const double minSize = std::min(size, length * (diamond ? 0.25 : 0.5));
    const QPointF base = tip - dir * minSize;
    if (head == Arrowhead::Dot || head == Arrowhead::Circle || head == Arrowhead::CircleOutline) {
        const double r = std::max(0.5, (minSize + width - 2) / 2);
        if (filled)
            *filled = head != Arrowhead::CircleOutline;
        return ellipsePath(QRectF(tip - QPointF(r, r), QSizeF(2 * r, 2 * r)));
    }
    const double degrees = arrow ? (minSize < size ? 30 : 20) : head == Arrowhead::Bar ? 90 : 25;
    const double angle = degrees * std::numbers::pi / 180;
    const QPointF a = rotatePoint(base, tip, -angle);
    const QPointF b = rotatePoint(base, tip, angle);
    switch (head) {
    case Arrowhead::Bar: return polygonPath({a, b}, false);
    case Arrowhead::Triangle:
    case Arrowhead::TriangleOutline:
        if (filled)
            *filled = head == Arrowhead::Triangle;
        return polygonPath({tip, a, b}, true);
    case Arrowhead::Diamond:
    case Arrowhead::DiamondOutline: {
        if (filled)
            *filled = head == Arrowhead::Diamond;
        const QPointF opposite = tip - dir * minSize * 2;
        return polygonPath({tip, a, opposite, b}, true);
    }
    default: break;
    }
    return polygonPath({a, tip, b}, false);
}

ElementShape elementShape(const Stroke &e, bool rotated)
{
    ElementShape s = localShape(e);
    const QPointF center = elementCenter(e);
    if (rotated && e.angle != 0 && std::isfinite(e.angle)) {
        transformPath(s.area, center, e.angle);
        transformPath(s.ink, center, e.angle);
        transformPath(s.outline, center, e.angle);
        for (Path &h : s.heads)
            transformPath(h, center, e.angle);
        for (QLineF &l : s.hatch)
            l = QLineF(rotatePoint(l.p1(), center, e.angle), rotatePoint(l.p2(), center, e.angle));
    }
    Extent extent;
    if (e.type == ElementType::Freedraw) {
        addPathBounds(extent, s.ink, 0);
        addPathBounds(extent, s.area, 0);
        s.bounds = extent.rect();
        return s;
    }
    if (e.type == ElementType::Text || e.type == ElementType::Image) {
        Stroke unrotated = e;
        if (!rotated)
            unrotated.angle = 0;
        s.bounds = elementBounds(unrotated);
        return s;
    }
    const double pad = std::max(0.0, e.width) / 2;
    addPathBounds(extent, s.outline, pad);
    for (const Path &h : s.heads)
        addPathBounds(extent, h, pad);
    s.bounds = extent.rect();
    return s;
}

QRectF elementBounds(const Stroke &e)
{
    if (e.type == ElementType::Text || e.type == ElementType::Image) {
        const QPointF c = e.box.center();
        Extent extent;
        for (const QPointF &p : {e.box.topLeft(), e.box.topRight(), e.box.bottomRight(), e.box.bottomLeft()})
            extent.add(rotatePoint(p, c, e.angle), 0);
        QRectF r = extent.rect();
        if (r.width() <= 0 || r.height() <= 0) // (a zero-sized box still is somewhere)
            r = QRectF(r.topLeft(), QSizeF(std::max(r.width(), 0.01), std::max(r.height(), 0.01)));
        return r;
    }
    return elementShape(e).bounds;
}

QList<TextLine> textLines(const Stroke &e)
{
    QList<TextLine> out;
    QString text = e.text;
    text.remove(u'\r');
    const QStringList lines = text.split(u'\n');
    const double step = e.fontSize * e.lineHeight;
    const double x = e.textAlign == TextAlign::Center  ? e.box.center().x()
                     : e.textAlign == TextAlign::Right ? e.box.right()
                                                       : e.box.left();
    for (qsizetype i = 0; i < lines.size(); ++i)
        out.append({lines.at(i), QPointF(x, e.box.top() + i * step + 0.5 * step + 0.35 * e.fontSize)});
    return out;
}

QString pathToSvg(const Path &path, double dx, double dy)
{
    const auto pt = [dx, dy](QPointF p) { return formatNumber(p.x() + dx) + u',' + formatNumber(p.y() + dy); };
    QString d;
    d.reserve(path.size() * 20);
    for (const PathOp &op : path) {
        if (!d.isEmpty())
            d += u' ';
        switch (op.kind) {
        case PathOp::Move: d += u'M' + pt(op.a); break;
        case PathOp::Line: d += u'L' + pt(op.a); break;
        case PathOp::Quad: d += u'Q' + pt(op.a) + u' ' + pt(op.b); break;
        case PathOp::Cubic: d += u'C' + pt(op.a) + u' ' + pt(op.b) + u' ' + pt(op.c); break;
        case PathOp::Close: d += u'Z'; break;
        }
    }
    return d;
}

QList<QList<QPointF>> flatten(const Path &path, double tolerance)
{
    QList<QList<QPointF>> out;
    QList<QPointF> current;
    QPointF start, last;
    const auto steps = [tolerance](double length) {
        return std::clamp(int(std::ceil(std::sqrt(std::max(0.0, length) / std::max(0.01, tolerance)))), 1, 64);
    };
    const auto flush = [&] {
        if (current.size() >= 1)
            out.append(current);
        current.clear();
    };
    for (const PathOp &op : path) {
        switch (op.kind) {
        case PathOp::Move:
            flush();
            start = last = op.a;
            current.append(op.a);
            break;
        case PathOp::Line:
            current.append(op.a);
            last = op.a;
            break;
        case PathOp::Quad: {
            const int n = steps(distance(last, op.a) + distance(op.a, op.b));
            for (int i = 1; i <= n; ++i) {
                const double t = double(i) / n, u = 1 - t;
                current.append(last * (u * u) + op.a * (2 * u * t) + op.b * (t * t));
            }
            last = op.b;
            break;
        }
        case PathOp::Cubic: {
            const int n = steps(distance(last, op.a) + distance(op.a, op.b) + distance(op.b, op.c));
            for (int i = 1; i <= n; ++i) {
                const double t = double(i) / n, u = 1 - t;
                current.append(last * (u * u * u) + op.a * (3 * u * u * t) + op.b * (3 * u * t * t)
                               + op.c * (t * t * t));
            }
            last = op.c;
            break;
        }
        case PathOp::Close:
            if (!current.isEmpty() && current.last() != start)
                current.append(start);
            last = start;
            flush();
            break;
        }
    }
    flush();
    return out;
}

double segmentDistance(QPointF a, QPointF b, QPointF c, QPointF d)
{
    const auto cross = [](QPointF o, QPointF p, QPointF q) {
        return (p.x() - o.x()) * (q.y() - o.y()) - (p.y() - o.y()) * (q.x() - o.x());
    };
    const double d1 = cross(c, d, a), d2 = cross(c, d, b), d3 = cross(a, b, c), d4 = cross(a, b, d);
    if (((d1 > 0 && d2 < 0) || (d1 < 0 && d2 > 0)) && ((d3 > 0 && d4 < 0) || (d3 < 0 && d4 > 0)))
        return 0;
    return std::min({pointSegment(a, c, d), pointSegment(b, c, d), pointSegment(c, a, b), pointSegment(d, a, b)});
}

bool pointInPolygon(QPointF p, const QList<QPointF> &polygon)
{
    const qsizetype n = polygon.size();
    if (n < 3)
        return false;
    bool inside = false;
    for (qsizetype i = 0, j = n - 1; i < n; j = i++) {
        const QPointF a = polygon.at(i), b = polygon.at(j);
        if ((a.y() > p.y()) != (b.y() > p.y())) {
            const double x = (b.x() - a.x()) * (p.y() - a.y()) / (b.y() - a.y()) + a.x();
            if (p.x() < x)
                inside = !inside;
        }
    }
    return inside;
}

namespace {

/// The element's polylines (unrotated): its outline and its heads; and whether it has an inside
/// that counts (filled).
struct HitShape {
    QList<QList<QPointF>> lines;
    QList<QList<QPointF>> areas; ///< closed polygons whose inside hits
};

HitShape hitShape(const Stroke &e)
{
    HitShape h;
    const ElementShape s = localShape(e);
    h.lines = flatten(s.outline, 1);
    for (const Path &head : s.heads)
        h.lines.append(flatten(head, 1));
    const bool filled = !transparent(e.fill);
    if (filled) {
        const bool closedLine = e.polygon && e.type == ElementType::Line && e.points.size() >= 3;
        if (isShape(e.type) || closedLine)
            h.areas = flatten(s.outline, 1);
    }
    return h;
}

QList<QPointF> boxCorners(const QRectF &r)
{
    return {r.topLeft(), r.topRight(), r.bottomRight(), r.bottomLeft()};
}

} // namespace

bool hitTest(const Stroke &e, QPointF scene, double tolerance)
{
    if (e.locked || !std::isfinite(scene.x()) || !std::isfinite(scene.y()))
        return false;
    const QPointF p = rotatePoint(scene, elementCenter(e), -e.angle);
    switch (e.type) {
    case ElementType::Freedraw: {
        const double reach = tolerance + e.width * 4.25 / 2;
        const qsizetype n = e.points.size();
        for (qsizetype k = 0; k < n; ++k) {
            const QPointF a(e.points.at(k).x, e.points.at(k).y);
            const QPointF b = k + 1 < n ? QPointF(e.points.at(k + 1).x, e.points.at(k + 1).y) : a;
            if (pointSegment(p, a, b) <= reach)
                return true;
        }
        return !transparent(e.fill) && pointInPolygon(p, pointsOf(e));
    }
    case ElementType::Text:
    case ElementType::Image: return e.box.adjusted(-tolerance, -tolerance, tolerance, tolerance).contains(p);
    default: break;
    }
    const HitShape h = hitShape(e);
    const double reach = tolerance + e.width / 2;
    for (const QList<QPointF> &line : h.lines) {
        for (qsizetype k = 0; k < line.size(); ++k) {
            const QPointF b = k + 1 < line.size() ? line.at(k + 1) : line.at(k);
            if (pointSegment(p, line.at(k), b) <= reach)
                return true;
        }
    }
    for (const QList<QPointF> &area : h.areas) {
        if (pointInPolygon(p, area))
            return true;
    }
    return false;
}

bool touchesSegment(const Stroke &e, QPointF a, QPointF b, double reach)
{
    if (e.locked)
        return false;
    const QPointF center = elementCenter(e);
    const QPointF la = rotatePoint(a, center, -e.angle);
    const QPointF lb = rotatePoint(b, center, -e.angle);
    switch (e.type) {
    case ElementType::Freedraw: {
        const double r = reach + e.width * 4.25 * 0.5;
        const qsizetype n = e.points.size();
        for (qsizetype k = 0; k < n; ++k) {
            const QPointF p(e.points.at(k).x, e.points.at(k).y);
            const QPointF q = k + 1 < n ? QPointF(e.points.at(k + 1).x, e.points.at(k + 1).y) : p;
            if (segmentDistance(la, lb, p, q) <= r)
                return true;
        }
        return false;
    }
    case ElementType::Text:
    case ElementType::Image: {
        const QList<QPointF> box = boxCorners(e.box);
        if (pointInPolygon(la, box) || pointInPolygon(lb, box))
            return true;
        for (qsizetype k = 0; k < 4; ++k) {
            if (segmentDistance(la, lb, box.at(k), box.at((k + 1) % 4)) <= reach)
                return true;
        }
        return false;
    }
    default: break;
    }
    const HitShape h = hitShape(e);
    const double r = reach + e.width / 2;
    for (const QList<QPointF> &line : h.lines) {
        for (qsizetype k = 0; k < line.size(); ++k) {
            const QPointF q = k + 1 < line.size() ? line.at(k + 1) : line.at(k);
            if (segmentDistance(la, lb, line.at(k), q) <= r)
                return true;
        }
    }
    for (const QList<QPointF> &area : h.areas) {
        if (pointInPolygon(la, area) || pointInPolygon(lb, area))
            return true;
    }
    return false;
}

QList<QPointF> lassoSamples(const Stroke &e)
{
    constexpr qsizetype MaxSamples = 64;
    QList<QPointF> samples;
    const QPointF center = elementCenter(e);
    const auto evenly = [](const QList<QPointF> &all) {
        if (all.size() <= MaxSamples)
            return all;
        QList<QPointF> out;
        out.reserve(MaxSamples);
        for (qsizetype i = 0; i < MaxSamples; ++i)
            out.append(all.at(i * (all.size() - 1) / (MaxSamples - 1)));
        return out;
    };
    switch (e.type) {
    case ElementType::Freedraw: samples = evenly(pointsOf(e)); break;
    case ElementType::Line:
    case ElementType::Arrow: {
        const QList<QPointF> pts = pointsOf(e);
        QList<QPointF> dense;
        for (qsizetype i = 0; i < pts.size(); ++i) {
            dense.append(pts.at(i));
            if (i + 1 < pts.size()) {
                const double len = distance(pts.at(i), pts.at(i + 1));
                const int n = int(std::min(1000.0, std::floor(len / 8)));
                for (int k = 1; k < n; ++k)
                    dense.append(pts.at(i) + (pts.at(i + 1) - pts.at(i)) * (double(k) / n));
            }
        }
        samples = evenly(dense);
        break;
    }
    default: {
        const QRectF r = e.box;
        samples = {r.topLeft(),
                   r.topRight(),
                   r.bottomRight(),
                   r.bottomLeft(),
                   QPointF(r.center().x(), r.top()),
                   QPointF(r.right(), r.center().y()),
                   QPointF(r.center().x(), r.bottom()),
                   QPointF(r.left(), r.center().y()),
                   r.center()};
        break;
    }
    }
    if (e.angle != 0) {
        for (QPointF &p : samples)
            p = rotatePoint(p, center, e.angle);
    }
    return samples;
}

bool lassoSelects(const Stroke &e, const QList<QPointF> &lasso)
{
    if (e.locked || lasso.size() < 3)
        return false;
    const QList<QPointF> samples = lassoSamples(e);
    if (samples.isEmpty())
        return false;
    qsizetype inside = 0;
    for (const QPointF &p : samples) {
        if (pointInPolygon(p, lasso))
            ++inside;
    }
    return inside * 2 >= samples.size();
}

} // namespace brulion::drawing
