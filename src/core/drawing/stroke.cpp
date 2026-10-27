// The outline of a freehand stroke: getStrokePoints() and getStrokeOutlinePoints() of
// perfect-freehand 1.2 (https://github.com/steveruiz/perfect-freehand), ported to C++ for Brulion,
// with the options Excalidraw uses (no tapers, rounded caps), and its getSvgPathFromStroke().
//
// perfect-freehand is under the MIT License:
//
//   MIT License
//
//   Copyright (c) 2021 Stephen Ruiz Ltd
//
//   Permission is hereby granted, free of charge, to any person obtaining a copy
//   of this software and associated documentation files (the "Software"), to deal
//   in the Software without restriction, including without limitation the rights
//   to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
//   copies of the Software, and to permit persons to whom the Software is
//   furnished to do so, subject to the following conditions:
//
//   The above copyright notice and this permission notice shall be included in all
//   copies or substantial portions of the Software.
//
//   THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
//   IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
//   FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
//   AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
//   LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
//   OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
//   SOFTWARE.
#include "drawing/stroke.h"

#include <QtMath>

#include <algorithm>
#include <cmath>

using namespace Qt::StringLiterals;

namespace brulion::drawing {

namespace {

// perfect-freehand's vector helpers (vec.ts) on QPointF.
using Vec = QPointF;

Vec add(Vec a, Vec b) { return a + b; }
Vec sub(Vec a, Vec b) { return a - b; }
Vec mul(Vec a, double n) { return a * n; }
Vec neg(Vec a) { return -a; }
/// Perpendicular rotation.
Vec per(Vec a) { return {a.y(), -a.x()}; }
double dpr(Vec a, Vec b) { return a.x() * b.x() + a.y() * b.y(); }
double len(Vec a) { return std::hypot(a.x(), a.y()); }
double dist(Vec a, Vec b) { return len(a - b); }
double dist2(Vec a, Vec b)
{
    const Vec d = a - b;
    return d.x() * d.x() + d.y() * d.y();
}
Vec uni(Vec a)
{
    const double l = len(a);
    return l > 0 ? a / l : Vec(0, 0);
}
Vec lrp(Vec a, Vec b, double t) { return a + (b - a) * t; }
/// Project a point in direction b by c.
Vec prj(Vec a, Vec b, double c) { return a + b * c; }
Vec rotAround(Vec a, Vec c, double r)
{
    const double s = std::sin(r);
    const double co = std::cos(r);
    const double px = a.x() - c.x();
    const double py = a.y() - c.y();
    return {px * co - py * s + c.x(), px * s + py * co + c.y()};
}
bool isEqual(Vec a, Vec b) { return a.x() == b.x() && a.y() == b.y(); }

constexpr double RateOfPressureChange = 0.275;
constexpr double FixedPi = M_PI + 0.0001;

double easing(double t) { return std::sin(t * M_PI / 2); } // Excalidraw's freedraw easing

double strokeRadius(double size, double thinning, double pressure)
{
    return size * easing(0.5 - thinning * (0.5 - pressure));
}

struct Input {
    Vec point;
    double pressure; ///< < 0: none given
};

struct StrokePt {
    Vec point;
    double pressure = 0.5;
    Vec vector;
    double distance = 0;
    double runningLength = 0;
};

/// getStrokePoints(): the input smoothed (streamline), with direction vectors and lengths.
QList<StrokePt> strokePoints(QList<Input> pts, const OutlineOptions &o)
{
    if (pts.isEmpty())
        return {};
    const double t = 0.15 + (1 - o.streamline) * 0.85;
    if (pts.size() == 2) {
        const Input last = pts.at(1);
        pts.removeLast();
        for (int i = 1; i < 5; ++i)
            pts.append({lrp(pts.at(0).point, last.point, i / 4.0), last.pressure});
    }
    if (pts.size() == 1)
        pts.append({add(pts.at(0).point, Vec(1, 1)), pts.at(0).pressure});

    QList<StrokePt> out;
    out.append({pts.at(0).point, pts.at(0).pressure >= 0 ? pts.at(0).pressure : 0.25, Vec(1, 1), 0, 0});
    bool reachedMinimumLength = false;
    double runningLength = 0;
    StrokePt prev = out.at(0);
    const int max = int(pts.size()) - 1;
    for (int i = 1; i < pts.size(); ++i) {
        const Vec point = o.last && i == max ? pts.at(i).point : lrp(prev.point, pts.at(i).point, t);
        if (isEqual(prev.point, point))
            continue;
        const double distance = dist(point, prev.point);
        runningLength += distance;
        if (i < max && !reachedMinimumLength) {
            if (runningLength < o.size)
                continue;
            reachedMinimumLength = true;
        }
        prev = {point, pts.at(i).pressure >= 0 ? pts.at(i).pressure : 0.5, uni(sub(prev.point, point)), distance,
                runningLength};
        out.append(prev);
    }
    out[0].vector = out.size() > 1 ? out.at(1).vector : Vec(0, 0);
    return out;
}

/// getStrokeOutlinePoints() without tapers (Excalidraw uses none), caps rounded.
QList<QPointF> outlinePoints(const QList<StrokePt> &points, const OutlineOptions &o)
{
    if (points.isEmpty() || o.size <= 0)
        return {};
    const double size = o.size;
    const double totalLength = points.last().runningLength;
    const double minDistance = std::pow(size * o.smoothing, 2);
    QList<QPointF> left;
    QList<QPointF> right;

    double prevPressure = points.at(0).pressure;
    for (int i = 0; i < std::min<qsizetype>(10, points.size()); ++i) {
        double pressure = points.at(i).pressure;
        if (o.simulatePressure) {
            const double sp = std::min(1.0, points.at(i).distance / size);
            const double rp = std::min(1.0, 1 - sp);
            pressure = std::min(1.0, prevPressure + (rp - prevPressure) * (sp * RateOfPressureChange));
        }
        prevPressure = (prevPressure + pressure) / 2;
    }

    double radius = strokeRadius(size, o.thinning, points.last().pressure);
    double firstRadius = -1;
    Vec prevVector = points.at(0).vector;
    Vec pl = points.at(0).point;
    Vec pr = pl;
    Vec tl = pl;
    Vec tr = pr;
    bool prevPointSharpCorner = false;

    for (int i = 0; i < points.size(); ++i) {
        double pressure = points.at(i).pressure;
        const Vec point = points.at(i).point;
        const Vec vector = points.at(i).vector;
        const double distance = points.at(i).distance;
        const double runningLength = points.at(i).runningLength;
        // Removes noise from the end of the line.
        if (i < points.size() - 1 && totalLength - runningLength < 3)
            continue;
        if (o.thinning != 0) {
            if (o.simulatePressure) {
                const double sp = std::min(1.0, distance / size);
                const double rp = std::min(1.0, 1 - sp);
                pressure = std::min(1.0, prevPressure + (rp - prevPressure) * (sp * RateOfPressureChange));
            }
            radius = strokeRadius(size, o.thinning, pressure);
        } else {
            radius = size / 2;
        }
        if (firstRadius < 0)
            firstRadius = radius;
        radius = std::max(0.01, radius);

        const Vec nextVector = (i < points.size() - 1 ? points.at(i + 1) : points.at(i)).vector;
        const double nextDpr = i < points.size() - 1 ? dpr(vector, nextVector) : 1.0;
        const double prevDpr = dpr(vector, prevVector);
        const bool pointSharpCorner = prevDpr < 0 && !prevPointSharpCorner;
        const bool nextPointSharpCorner = nextDpr < 0;

        if (pointSharpCorner || nextPointSharpCorner) {
            const Vec offset = mul(per(prevVector), radius);
            constexpr double step = 1.0 / 13;
            for (double t = 0; t <= 1; t += step) {
                tl = rotAround(sub(point, offset), point, FixedPi * t);
                left.append(tl);
                tr = rotAround(add(point, offset), point, FixedPi * -t);
                right.append(tr);
            }
            pl = tl;
            pr = tr;
            if (nextPointSharpCorner)
                prevPointSharpCorner = true;
            continue;
        }
        prevPointSharpCorner = false;

        if (i == points.size() - 1) {
            const Vec offset = mul(per(vector), radius);
            left.append(sub(point, offset));
            right.append(add(point, offset));
            continue;
        }

        const Vec offset = mul(per(lrp(nextVector, vector, nextDpr)), radius);
        tl = sub(point, offset);
        if (i <= 1 || dist2(pl, tl) > minDistance) {
            left.append(tl);
            pl = tl;
        }
        tr = add(point, offset);
        if (i <= 1 || dist2(pr, tr) > minDistance) {
            right.append(tr);
            pr = tr;
        }
        prevPressure = pressure;
        prevVector = vector;
    }

    const Vec firstPoint = points.at(0).point;
    const Vec lastPoint = points.size() > 1 ? points.last().point : add(points.at(0).point, Vec(1, 1));
    if (points.size() == 1) {
        // A dot.
        const Vec start = prj(firstPoint, uni(per(sub(firstPoint, lastPoint))), -(firstRadius >= 0 ? firstRadius : radius));
        QList<QPointF> dot;
        constexpr double step = 1.0 / 13;
        for (double t = step; t <= 1; t += step)
            dot.append(rotAround(start, firstPoint, FixedPi * 2 * t));
        return dot;
    }
    if (left.isEmpty() || right.isEmpty())
        return {};
    QList<QPointF> startCap;
    {
        constexpr double step = 1.0 / 13;
        for (double t = step; t <= 1; t += step)
            startCap.append(rotAround(right.at(0), firstPoint, FixedPi * t));
    }
    QList<QPointF> endCap;
    {
        const Vec direction = per(neg(points.last().vector));
        const Vec start = prj(lastPoint, direction, radius);
        constexpr double step = 1.0 / 29;
        for (double t = step; t < 1; t += step)
            endCap.append(rotAround(start, lastPoint, FixedPi * 3 * t));
    }
    QList<QPointF> out;
    out.reserve(left.size() + endCap.size() + right.size() + startCap.size());
    out.append(left);
    out.append(endCap);
    for (qsizetype i = right.size() - 1; i >= 0; --i)
        out.append(right.at(i));
    out.append(startCap);
    return out;
}

} // namespace

OutlineOptions outlineOptions(const Stroke &stroke, bool finished)
{
    OutlineOptions o;
    o.size = stroke.width * 4.25;
    o.simulatePressure = stroke.simulatePressure;
    o.last = finished;
    return o;
}

QList<QPointF> strokeOutline(const QList<StrokePoint> &points, const OutlineOptions &options)
{
    QList<Input> input;
    input.reserve(points.size());
    for (const StrokePoint &p : points) {
        if (!std::isfinite(p.x) || !std::isfinite(p.y))
            continue;
        // Excalidraw passes no pressure when it simulates it (perfect-freehand then uses 0.5).
        const double pressure = options.simulatePressure || !std::isfinite(p.pressure)
                                        ? -1
                                        : std::clamp(p.pressure, 0.0, 1.0);
        input.append({Vec(p.x, p.y), pressure});
    }
    if (input.isEmpty() || !std::isfinite(options.size))
        return {};
    return outlinePoints(strokePoints(input, options), options);
}

QString formatNumber(double value, int decimals)
{
    if (!std::isfinite(value))
        return u"0"_s;
    QString s = QString::number(value, 'f', decimals);
    if (s.contains(u'.')) {
        while (s.endsWith(u'0'))
            s.chop(1);
        if (s.endsWith(u'.'))
            s.chop(1);
    }
    if (s == u"-0")
        s = u"0"_s;
    return s;
}

QString outlineToSvgPath(const QList<QPointF> &outline, double dx, double dy)
{
    if (outline.isEmpty())
        return {};
    const auto pt = [dx, dy](QPointF p) { return formatNumber(p.x() + dx) + u',' + formatNumber(p.y() + dy); };
    const auto mid = [](QPointF a, QPointF b) { return QPointF((a.x() + b.x()) / 2, (a.y() + b.y()) / 2); };
    QString d;
    d.reserve(outline.size() * 28 + 16);
    d += u"M"_s + pt(outline.at(0)) + u" Q"_s;
    const qsizetype max = outline.size() - 1;
    for (qsizetype i = 0; i <= max; ++i) {
        const QPointF p = outline.at(i);
        const QPointF next = i == max ? outline.at(0) : outline.at(i + 1);
        d += u' ' + pt(p) + u' ' + pt(mid(p, next));
    }
    d += u" L"_s + pt(outline.at(0)) + u" Z"_s;
    return d;
}

} // namespace brulion::drawing
