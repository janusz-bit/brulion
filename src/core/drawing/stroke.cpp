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

#include <algorithm>
#include <cmath>
#include <iterator>
#include <numbers>

using namespace Qt::StringLiterals;

namespace brulion::drawing {

namespace {

// perfect-freehand's vector helpers (vec.ts) on QPointF (its add, sub, mul and neg are QPointF's
// operators).
using Vec = QPointF;

/// Perpendicular rotation.
Vec per(Vec a)
{
    return {a.y(), -a.x()};
}
double dpr(Vec a, Vec b)
{
    return a.x() * b.x() + a.y() * b.y();
}
double len(Vec a)
{
    return std::hypot(a.x(), a.y());
}
double dist(Vec a, Vec b)
{
    return len(a - b);
}
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
Vec lrp(Vec a, Vec b, double t)
{
    return a + (b - a) * t;
}
/// Project a point in direction b by c.
Vec prj(Vec a, Vec b, double c)
{
    return a + b * c;
}
Vec rotAround(Vec a, Vec c, double r)
{
    const double s = std::sin(r);
    const double co = std::cos(r);
    const double px = a.x() - c.x();
    const double py = a.y() - c.y();
    return {px * co - py * s + c.x(), px * s + py * co + c.y()};
}
bool isEqual(Vec a, Vec b)
{
    return a.x() == b.x() && a.y() == b.y();
}

constexpr double RateOfPressureChange = 0.275;
constexpr double FixedPi = std::numbers::pi + 0.0001;
/// The steps of t around a round corner or cap (summed as JavaScript sums them: the sums decide
/// the last point) — the end cap's are finer.
constexpr double CapStep = 1.0 / 13;
constexpr double EndCapStep = 1.0 / 29;

/// Excalidraw's freedraw easing.
double easing(double t)
{
    return std::sin(t * std::numbers::pi / 2);
}

double strokeRadius(double size, double thinning, double pressure)
{
    return size * easing(0.5 - thinning * (0.5 - pressure));
}

/// The pressure simulated from the speed: `distance` moved since the point before.
double simulatedPressure(double prevPressure, double distance, double size)
{
    const double sp = std::min(1.0, distance / size);
    const double rp = std::min(1.0, 1 - sp);
    return std::min(1.0, prevPressure + (rp - prevPressure) * (sp * RateOfPressureChange));
}

struct Input {
    Vec point;
    double pressure = -1; ///< < 0: none given
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
        pts.append({pts.at(0).point + Vec(1, 1), pts.at(0).pressure});

    QList<StrokePt> out;
    out.reserve(pts.size());
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
        prev = {point, pts.at(i).pressure >= 0 ? pts.at(i).pressure : 0.5, uni(prev.point - point), distance,
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
    for (qsizetype i = 0; i < std::min<qsizetype>(10, points.size()); ++i) {
        const StrokePt &p = points.at(i);
        const double pressure = o.simulatePressure ? simulatedPressure(prevPressure, p.distance, size) : p.pressure;
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

    const qsizetype last = points.size() - 1;
    for (qsizetype i = 0; i <= last; ++i) {
        const auto &[point, pointPressure, vector, distance, runningLength] = points.at(i);
        // Removes noise from the end of the line.
        if (i < last && totalLength - runningLength < 3)
            continue;
        double pressure = pointPressure;
        if (o.thinning != 0) {
            if (o.simulatePressure)
                pressure = simulatedPressure(prevPressure, distance, size);
            radius = strokeRadius(size, o.thinning, pressure);
        } else {
            radius = size / 2;
        }
        if (firstRadius < 0)
            firstRadius = radius;
        radius = std::max(0.01, radius);

        const Vec nextVector = points.at(i < last ? i + 1 : i).vector;
        const double nextDpr = i < last ? dpr(vector, nextVector) : 1.0;
        const double prevDpr = dpr(vector, prevVector);
        const bool pointSharpCorner = prevDpr < 0 && !prevPointSharpCorner;
        const bool nextPointSharpCorner = nextDpr < 0;

        if (pointSharpCorner || nextPointSharpCorner) {
            const Vec offset = per(prevVector) * radius;
            for (double t = 0; t <= 1; t += CapStep) {
                tl = rotAround(point - offset, point, FixedPi * t);
                left.append(tl);
                tr = rotAround(point + offset, point, FixedPi * -t);
                right.append(tr);
            }
            pl = tl;
            pr = tr;
            if (nextPointSharpCorner)
                prevPointSharpCorner = true;
            continue;
        }
        prevPointSharpCorner = false;

        if (i == last) {
            const Vec offset = per(vector) * radius;
            left.append(point - offset);
            right.append(point + offset);
            continue;
        }

        const Vec offset = per(lrp(nextVector, vector, nextDpr)) * radius;
        tl = point - offset;
        if (i <= 1 || dist2(pl, tl) > minDistance) {
            left.append(tl);
            pl = tl;
        }
        tr = point + offset;
        if (i <= 1 || dist2(pr, tr) > minDistance) {
            right.append(tr);
            pr = tr;
        }
        prevPressure = pressure;
        prevVector = vector;
    }

    const Vec firstPoint = points.at(0).point;
    const Vec lastPoint = points.size() > 1 ? points.last().point : firstPoint + Vec(1, 1);
    if (points.size() == 1) {
        // A dot.
        const Vec start = prj(firstPoint, uni(per(firstPoint - lastPoint)), -(firstRadius >= 0 ? firstRadius : radius));
        QList<QPointF> dot;
        for (double t = CapStep; t <= 1; t += CapStep)
            dot.append(rotAround(start, firstPoint, FixedPi * 2 * t));
        return dot;
    }
    if (left.isEmpty() || right.isEmpty())
        return {};
    // The left side, the end cap, the right side backwards, the start cap.
    QList<QPointF> out;
    out.reserve(left.size() + right.size() + 42);
    out.append(left);
    const Vec endCapStart = prj(lastPoint, per(-points.last().vector), radius);
    for (double t = EndCapStep; t < 1; t += EndCapStep)
        out.append(rotAround(endCapStart, lastPoint, FixedPi * 3 * t));
    std::ranges::reverse_copy(std::as_const(right), std::back_inserter(out));
    for (double t = CapStep; t <= 1; t += CapStep)
        out.append(rotAround(right.at(0), firstPoint, FixedPi * t));
    return out;
}

} // namespace

OutlineOptions outlineOptions(const Stroke &stroke, bool finished)
{
    return {.size = stroke.width * 4.25, .simulatePressure = stroke.simulatePressure, .last = finished};
}

QList<QPointF> strokeOutline(const QList<StrokePoint> &points, const OutlineOptions &options)
{
    if (!std::isfinite(options.size))
        return {};
    QList<Input> input;
    input.reserve(points.size());
    for (const StrokePoint &p : points) {
        if (!std::isfinite(p.x) || !std::isfinite(p.y))
            continue;
        // Excalidraw passes no pressure when it simulates it (perfect-freehand then uses 0.5).
        const double pressure =
                options.simulatePressure || !std::isfinite(p.pressure) ? -1 : std::clamp(p.pressure, 0.0, 1.0);
        input.append({Vec(p.x, p.y), pressure});
    }
    return outlinePoints(strokePoints(std::move(input), options), options);
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
