// Vector paths and icons (src/atrament/scene/vectorpath.h, vectoricon.h; docs/ATRAMENT.md "Vector
// paths and icons"): curves flattened within the tolerance, SVG's arcs, rounded rectangles; strokes
// as polygons of one winding — their coverage the area of a line of a width with its caps, the
// joins' corners, a closed outline's hole, dashes and dots; SVG path data with every command and
// compact numbers; icons of the SVG subset (Lucide's: every icon of assets/icons reads), what is
// skipped, malformed and oversized input, the outline cached per size.
#include "base/coverage.h"
#include "scene/vectoricon.h"
#include "scene/vectorpath.h"

#include <QDir>
#include <QFile>
#include <QtTest>

#include <cmath>
#include <numbers>

using namespace brulion::atrament;

namespace {

float distance(Point a, Point b)
{
    return std::hypot(a.x - b.x, a.y - b.y);
}

// The coverage of a path primitive in a w × h target.
struct Coverage {
    int width = 0;
    int height = 0;
    std::vector<float> cells;
    float at(int x, int y) const
    {
        return x >= 0 && y >= 0 && x < width && y < height ? cells[std::size_t(y * width + x)] : 0.0f;
    }
    float total() const
    {
        float sum = 0;
        for (const float c : cells)
            sum += c;
        return sum;
    }
};

Coverage rasterize(const PathPrimitive &p, int w, int h)
{
    CoverageRasterizer r;
    r.reset(w, h);
    std::size_t start = 0;
    for (const std::uint32_t end : p.contourEnds) {
        if (end > start + 1) {
            r.moveTo(p.points[start]);
            for (std::size_t i = start + 1; i < end; ++i)
                r.lineTo(p.points[i]);
            r.close();
        }
        start = end;
    }
    Coverage c{w, h, {}};
    r.accumulate(c.cells);
    return c;
}

PathPrimitive stroked(const VectorPath &path, const StrokeStyle &style)
{
    PathBuilder b;
    strokeContours(path.flatten({}), style, b);
    return std::move(b).build(black(), {});
}

// The signed area of each contour (positive: the stroke's one orientation).
std::vector<float> contourAreas(const PathPrimitive &p)
{
    std::vector<float> areas;
    std::size_t start = 0;
    for (const std::uint32_t end : p.contourEnds) {
        float area = 0;
        for (std::size_t i = start; i < end; ++i) {
            const Point a = p.points[i];
            const Point b = p.points[i + 1 < end ? i + 1 : start];
            area += a.x * b.y - b.x * a.y;
        }
        areas.push_back(area / 2);
        start = end;
    }
    return areas;
}

} // namespace

class TestAtramentVector : public QObject {
    Q_OBJECT
private Q_SLOTS:
    void curvesFlattenedWithinTolerance();
    void transformApplied();
    void arcsFollowSvg();
    void roundedRectsAndEllipses();
    void lineStrokeAreaWithCaps_data();
    void lineStrokeAreaWithCaps();
    void strokeOfOneWinding();
    void joins();
    void closedOutlineHasAHole();
    void dashesAndDots();
    void malformedStrokes();
    void pathDataCommands_data();
    void pathDataCommands();
    void pathDataStopsAtAMistake();
    void iconOfTheSubset();
    void iconSkipsWhatIsNotDrawn();
    void malformedIcons();
    void outlineCachedPerSize();
    void everyAppIconReads();
};

void TestAtramentVector::curvesFlattenedWithinTolerance()
{
    VectorPath p;
    p.moveTo({0, 0});
    p.cubicTo({0, 100}, {100, 100}, {100, 0});
    p.quadTo({150, -50}, {200, 0});
    const std::vector<FlatContour> flat = p.flatten({}, 0.1f);
    QCOMPARE(flat.size(), std::size_t(1));
    QVERIFY(!flat[0].closed);
    const std::vector<Point> &pts = flat[0].points;
    QCOMPARE(pts.front(), Point(0, 0));
    QCOMPARE(pts.back(), Point(200, 0));
    QVERIFY(pts.size() > 20);
    // Every chord's middle is near the cubic: its distance to the curve (sampled finely) within the
    // tolerance.
    std::vector<Point> curve;
    for (int k = 0; k <= 20000; ++k) {
        const float t = float(k) / 20000;
        curve.push_back({3 * (1 - t) * t * t * 100 + t * t * t * 100, 3 * (1 - t) * (1 - t) * t * 100 + 3 * (1 - t) * t * t * 100});
    }
    for (std::size_t i = 1; i < pts.size() && pts[i].x <= 100 && pts[i - 1].x < 100; ++i) {
        const Point mid = (pts[i - 1] + pts[i]) / 2;
        float nearest = 1e9f;
        for (const Point &c : curve)
            nearest = std::min(nearest, distance(mid, c));
        QVERIFY2(nearest < 0.12f, qPrintable(QString::number(nearest)));
    }
    // A closed contour, and a line after close() starting a new one at the old start.
    VectorPath q;
    q.moveTo({1, 1});
    q.lineTo({5, 1});
    q.lineTo({5, 5});
    q.close();
    q.lineTo({9, 9});
    const std::vector<FlatContour> two = q.flatten({});
    QCOMPARE(two.size(), std::size_t(2));
    QVERIFY(two[0].closed);
    QCOMPARE(two[1].points.front(), Point(1, 1));
    QVERIFY(!two[1].closed);
}

void TestAtramentVector::transformApplied()
{
    VectorPath p;
    p.moveTo({1, 2});
    p.lineTo({3, 4});
    const std::vector<FlatContour> flat = p.flatten({2, {10, 20}});
    QCOMPARE(flat[0].points[0], Point(12, 24));
    QCOMPARE(flat[0].points[1], Point(16, 28));
    QCOMPARE(p.bounds(), Rect::fromLTRB(1, 2, 3, 4));
}

void TestAtramentVector::arcsFollowSvg()
{
    // A half circle above the chord (sweep: clockwise on screen, y down).
    VectorPath p;
    p.moveTo({0, 0});
    p.arcTo(5, 5, 0, false, true, {10, 0});
    std::vector<FlatContour> flat = p.flatten({}, 0.05f);
    float minY = 0;
    for (const Point &pt : flat[0].points) {
        QVERIFY2(std::abs(distance(pt, {5, 0}) - 5) < 0.1f, qPrintable(QString::number(distance(pt, {5, 0}))));
        minY = std::min(minY, pt.y);
    }
    QVERIFY(std::abs(minY + 5) < 0.05f);
    QCOMPARE(flat[0].points.back(), Point(10, 0));
    // The other sweep goes below.
    VectorPath below;
    below.moveTo({0, 0});
    below.arcTo(5, 5, 0, false, false, {10, 0});
    float maxY = 0;
    const std::vector<FlatContour> belowFlat = below.flatten({});
    for (const Point &pt : belowFlat[0].points)
        maxY = std::max(maxY, pt.y);
    QVERIFY(std::abs(maxY - 5) < 0.1f);
    // Radii too small grow to join the points (a half circle of radius 5 again).
    VectorPath small;
    small.moveTo({0, 0});
    small.arcTo(1, 1, 0, false, true, {10, 0});
    const std::vector<FlatContour> smallFlat = small.flatten({});
    for (const Point &pt : smallFlat[0].points)
        QVERIFY(std::abs(distance(pt, {5, 0}) - 5) < 0.15f);
    // The large arc of a circle of radius 10 through two points 10 apart: more than half a turn.
    VectorPath large;
    large.moveTo({0, 0});
    large.arcTo(10, 10, 0, true, true, {10, 0});
    const Rect b = large.bounds();
    QVERIFY(b.height() > 15);
    // Zero radii: a line.
    VectorPath line;
    line.moveTo({0, 0});
    line.arcTo(0, 4, 0, false, true, {4, 4});
    QCOMPARE(line.flatten({})[0].points.size(), std::size_t(2));
}

void TestAtramentVector::roundedRectsAndEllipses()
{
    VectorPath r;
    r.addRect(Rect::fromXYWH(2, 3, 20, 10), 4, 4);
    const std::vector<FlatContour> flat = r.flatten({}, 0.05f);
    QCOMPARE(flat.size(), std::size_t(1));
    QVERIFY(flat[0].closed);
    for (const Point &pt : flat[0].points) {
        QVERIFY(pt.x >= 2 - 1e-3f && pt.x <= 22 + 1e-3f && pt.y >= 3 - 1e-3f && pt.y <= 13 + 1e-3f);
        // No point in the corner the radius cuts off.
        QVERIFY(!(pt.x < 2.5f && pt.y < 3.5f));
    }
    // Radii clamped to half the sides: a 10 × 10 square with radius 50 is a circle.
    VectorPath c;
    c.addRect(Rect::fromXYWH(0, 0, 10, 10), 50, 50);
    const std::vector<FlatContour> circle = c.flatten({}, 0.02f);
    for (const Point &pt : circle[0].points)
        QVERIFY(std::abs(distance(pt, {5, 5}) - 5) < 0.05f);
    // An ellipse's area, filled.
    VectorPath e;
    e.addEllipse({20, 20}, 10, 5);
    PathBuilder b;
    fillContours(e.flatten({}, 0.05f), b);
    const Coverage cov = rasterize(std::move(b).build(black(), {}), 40, 40);
    QVERIFY(std::abs(cov.total() - std::numbers::pi_v<float> * 50) < 1.5f);
}

void TestAtramentVector::lineStrokeAreaWithCaps_data()
{
    QTest::addColumn<int>("cap");
    QTest::addColumn<float>("area");
    const float pi = std::numbers::pi_v<float>;
    QTest::newRow("butt") << int(LineCap::Butt) << 40.0f * 4;
    QTest::newRow("round") << int(LineCap::Round) << 40.0f * 4 + pi * 4;
    QTest::newRow("square") << int(LineCap::Square) << 44.0f * 4;
}

void TestAtramentVector::lineStrokeAreaWithCaps()
{
    QFETCH(int, cap);
    QFETCH(float, area);
    VectorPath p;
    p.moveTo({10, 10});
    p.lineTo({50, 10});
    StrokeStyle style;
    style.width = 4;
    style.cap = LineCap(cap);
    const Coverage c = rasterize(stroked(p, style), 64, 24);
    QVERIFY2(std::abs(c.total() - area) < 1.0f, qPrintable(QString::number(c.total())));
    // Inside the line: covered; above and below it: not.
    QVERIFY(c.at(30, 9) > 0.99f && c.at(30, 10) > 0.99f);
    QVERIFY(c.at(30, 6) < 0.01f && c.at(30, 13) < 0.01f);
}

void TestAtramentVector::strokeOfOneWinding()
{
    VectorPath p;
    p.moveTo({10, 10});
    p.cubicTo({40, 0}, {0, 40}, {30, 30});
    p.lineTo({5, 30});
    for (const LineJoin join : {LineJoin::Miter, LineJoin::Round, LineJoin::Bevel}) {
        StrokeStyle style;
        style.width = 3;
        style.join = join;
        style.cap = LineCap::Round;
        const PathPrimitive s = stroked(p, style);
        QVERIFY(!s.contourEnds.empty());
        for (const float a : contourAreas(s))
            QVERIFY2(a >= 0, qPrintable(QString::number(a)));
    }
}

void TestAtramentVector::joins()
{
    // An L: right along y = 40, then up along x = 40; the corner's outer point is (43, 43).
    VectorPath p;
    p.moveTo({10, 40});
    p.lineTo({40, 40});
    p.lineTo({40, 10});
    StrokeStyle style;
    style.width = 6;
    style.join = LineJoin::Miter;
    const Coverage miter = rasterize(stroked(p, style), 60, 60);
    style.join = LineJoin::Bevel;
    const Coverage bevel = rasterize(stroked(p, style), 60, 60);
    style.join = LineJoin::Round;
    const Coverage round = rasterize(stroked(p, style), 60, 60);
    QVERIFY(miter.at(42, 42) > 0.99f); // the miter's square corner
    QVERIFY(bevel.at(42, 42) < 0.01f); // cut off
    QVERIFY(miter.at(42, 41) > 0.99f && bevel.at(42, 41) < 0.01f);
    QVERIFY(round.at(42, 41) > 0.2f && round.at(42, 41) < 0.95f); // the round join's edge
    // Miter > round > bevel by area; the inside of the corner is covered the same.
    QVERIFY(miter.total() > round.total() && round.total() > bevel.total());
    QVERIFY(miter.at(38, 38) > 0.99f && bevel.at(38, 38) > 0.99f);
    // A sharp V past the miter limit becomes a bevel.
    VectorPath v;
    v.moveTo({10, 50});
    v.lineTo({30, 10});
    v.lineTo({50, 50});
    style.join = LineJoin::Miter;
    style.miterLimit = 1.5f;
    const Coverage limited = rasterize(stroked(v, style), 60, 60);
    style.join = LineJoin::Bevel;
    const Coverage beveled = rasterize(stroked(v, style), 60, 60);
    QVERIFY(std::abs(limited.total() - beveled.total()) < 0.5f);
}

void TestAtramentVector::closedOutlineHasAHole()
{
    VectorPath p;
    p.addRect(Rect::fromXYWH(10, 10, 30, 30));
    StrokeStyle style;
    style.width = 2;
    const Coverage c = rasterize(stroked(p, style), 50, 50);
    QVERIFY(c.at(25, 25) < 0.01f); // inside: not filled
    QVERIFY(c.at(25, 10) > 0.99f || c.at(25, 9) > 0.99f);
    QVERIFY(c.at(9, 9) > 0.99f);   // a closed outline joins at its start (a miter's corner)
    QVERIFY(std::abs(c.total() - (32.0f * 32 - 28.0f * 28)) < 1.5f);
}

void TestAtramentVector::dashesAndDots()
{
    VectorPath p;
    p.moveTo({0, 10});
    p.lineTo({100, 10});
    StrokeStyle style;
    style.width = 2;
    style.dashes = {10, 10};
    Coverage c = rasterize(stroked(p, style), 110, 20);
    QVERIFY(c.at(5, 9) > 0.99f);
    QVERIFY(c.at(15, 9) < 0.01f);
    QVERIFY(c.at(25, 9) > 0.99f);
    QVERIFY(std::abs(c.total() - 100.0f) < 1.0f); // half of 100 × 2
    // An offset moves the pattern.
    style.dashOffset = 10;
    c = rasterize(stroked(p, style), 110, 20);
    QVERIFY(c.at(5, 9) < 0.01f && c.at(15, 9) > 0.99f);
    // An odd list repeats: 6 on, 4 off, 6 off… → {6, 4, 6, 4}… here {5} is 5 on, 5 off.
    style.dashOffset = 0;
    style.dashes = {5};
    c = rasterize(stroked(p, style), 110, 20);
    QVERIFY(c.at(2, 9) > 0.99f && c.at(7, 9) < 0.01f && c.at(12, 9) > 0.99f);
    // Dots: dashes of no length with round caps, every 10 px from x 5 (x 5 … 95).
    VectorPath dotted;
    dotted.moveTo({5, 10});
    dotted.lineTo({100, 10});
    style.dashes = {0, 10};
    style.cap = LineCap::Round;
    c = rasterize(stroked(dotted, style), 110, 20);
    const float dot = std::numbers::pi_v<float>; // r = 1
    QVERIFY2(std::abs(c.total() - 10 * dot) < 1.0f, qPrintable(QString::number(c.total())));
    QVERIFY(c.at(10, 9) < 0.01f);
    QVERIFY(c.at(14, 9) > 0.3f);
    // Dashes across a corner and a closed contour's closing segment.
    VectorPath square;
    square.addRect(Rect::fromXYWH(10, 10, 20, 20));
    style.dashes = {4, 4};
    style.cap = LineCap::Butt;
    const std::vector<FlatContour> pieces = dashContours(square.flatten({}), style.dashes, 0);
    QCOMPARE(pieces.size(), std::size_t(10)); // 80 px of outline, a dash every 8
    for (const FlatContour &piece : pieces)
        QVERIFY(!piece.closed);
}

void TestAtramentVector::malformedStrokes()
{
    VectorPath p;
    p.moveTo({0, 0});
    p.lineTo({std::nanf(""), 3});
    p.lineTo({10, 0});
    StrokeStyle style;
    style.width = 2;
    const PathPrimitive s = stroked(p, style);
    for (const Point &pt : s.points)
        QVERIFY(std::isfinite(pt.x) && std::isfinite(pt.y));
    // No width: nothing; a negative dash: solid; a pattern far too fine: solid.
    style.width = 0;
    QVERIFY(stroked(p, style).points.empty());
    style.width = 2;
    style.dashes = {3, -1};
    QCOMPARE(dashContours(p.flatten({}), style.dashes, 0).size(), std::size_t(1));
    VectorPath longLine;
    longLine.moveTo({0, 0});
    longLine.lineTo({1e6f, 0});
    const std::vector<float> fine{1e-3f, 1e-3f};
    QCOMPARE(dashContours(longLine.flatten({}), fine, 0).size(), std::size_t(1));
    // A point alone: a dot with round caps, nothing with butt caps.
    VectorPath dot;
    dot.moveTo({5, 5});
    dot.lineTo({5, 5});
    style.dashes.clear();
    style.cap = LineCap::Round;
    QVERIFY(!stroked(dot, style).points.empty());
    style.cap = LineCap::Butt;
    QVERIFY(stroked(dot, style).points.empty());
}

void TestAtramentVector::pathDataCommands_data()
{
    QTest::addColumn<QString>("d");
    QTest::addColumn<QRectF>("bounds");
    QTest::addColumn<int>("contours");
    QTest::newRow("absolute lines") << "M10 10 L20 10 L20 20 Z" << QRectF(10, 10, 10, 10) << 1;
    QTest::newRow("implicit lines after M") << "M10 10 20 10 20 20z" << QRectF(10, 10, 10, 10) << 1;
    QTest::newRow("relative, H and V") << "m10 10h10v10H10z" << QRectF(10, 10, 10, 10) << 1;
    QTest::newRow("compact numbers") << "M.5.5L10-2e1l-1.5.25" << QRectF(0.5, -20, 9.5, 20.5) << 1;
    QTest::newRow("two subpaths") << "M0 0h4M10 10h4" << QRectF(0, 0, 14, 10) << 2;
    QTest::newRow("smooth cubics") << "M0 0C0 10 10 10 10 0S20-10 20 0" << QRectF(0, -10, 20, 20) << 1;
    QTest::newRow("smooth quads") << "M0 0Q5 10 10 0T20 0" << QRectF(0, -10, 20, 20) << 1;
    QTest::newRow("compact arc flags") << "M4 2a2 2 0 0 0-2 2" << QRectF(2, 2, 2, 2) << 1;
    QTest::newRow("glued flags") << "M0 0a5 5 0 1110 0" << QRectF(0, -5, 10, 10) << 1;
    QTest::newRow("Lucide's bell") << "M10.268 21a2 2 0 0 0 3.464 0" << QRectF(10.268, 21, 3.464, 1) << 1;
}

void TestAtramentVector::pathDataCommands()
{
    QFETCH(QString, d);
    QFETCH(QRectF, bounds);
    QFETCH(int, contours);
    const QByteArray utf8 = d.toUtf8();
    VectorPath p;
    QVERIFY(parseSvgPathData(std::string_view(utf8.constData(), std::size_t(utf8.size())), p));
    const std::vector<FlatContour> flat = p.flatten({}, 0.01f);
    QCOMPARE(int(flat.size()), contours);
    float l = 1e9f, t = 1e9f, r = -1e9f, b = -1e9f;
    for (const FlatContour &c : flat)
        for (const Point &pt : c.points) {
            l = std::min(l, pt.x);
            t = std::min(t, pt.y);
            r = std::max(r, pt.x);
            b = std::max(b, pt.y);
        }
    // Curves stay inside their control points' hull: compare the flattened extent loosely.
    QVERIFY2(std::abs(l - bounds.left()) < 0.6f && std::abs(r - bounds.right()) < 0.6f,
             qPrintable(QStringLiteral("%1 %2").arg(l).arg(r)));
    QVERIFY2(t >= float(bounds.top()) - 0.6f && b <= float(bounds.bottom()) + 0.6f,
             qPrintable(QStringLiteral("%1 %2").arg(t).arg(b)));
}

void TestAtramentVector::pathDataStopsAtAMistake()
{
    VectorPath p;
    QVERIFY(!parseSvgPathData("M0 0 L10 0 L10 x10 L0 10", p));
    const std::vector<FlatContour> flat = p.flatten({});
    QCOMPARE(flat.size(), std::size_t(1));
    QCOMPARE(flat[0].points.size(), std::size_t(2)); // what came before the mistake
    VectorPath q;
    QVERIFY(!parseSvgPathData("10 10 L20 20", q)); // numbers before any command
    QVERIFY(q.isEmpty());
    VectorPath r;
    QVERIFY(!parseSvgPathData("M0 0 Z 5 5", r)); // numbers after Z
    QVERIFY(!parseSvgPathData("M0 0 A5 5 0 2 0 10 10", r)); // a flag that is no 0 or 1
    QVERIFY(parseSvgPathData("", r));
}

void TestAtramentVector::iconOfTheSubset()
{
    // Lucide's "tag": a stroked outline and a filled dot (fill="#000" on the circle).
    const std::shared_ptr<const VectorIcon> tag = VectorIcon::parseSvg(
            R"(<svg xmlns="http://www.w3.org/2000/svg" width="24" height="24" viewBox="0 0 24 24" fill="none" )"
            R"(stroke="#000" stroke-width="2" stroke-linecap="round" stroke-linejoin="round">)"
            R"(<path d="M12.586 2.586A2 2 0 0 0 11.172 2H4a2 2 0 0 0-2 2v7.172a2 2 0 0 0 .586 1.414l8.704 8.704)"
            R"(a2.426 2.426 0 0 0 3.42 0l6.58-6.58a2.426 2.426 0 0 0 0-3.42z"/>)"
            R"(<circle cx="7.5" cy="7.5" r=".5" fill="#000"/></svg>)");
    QVERIFY(tag);
    QCOMPARE(tag->viewBox(), Rect::fromXYWH(0, 0, 24, 24));
    QCOMPARE(tag->shapes().size(), std::size_t(2));
    const VectorIcon::Shape &outline = tag->shapes()[0];
    QVERIFY(outline.stroked && !outline.filled);
    QCOMPARE(outline.stroke.width, 2.0f);
    QVERIFY(outline.stroke.cap == LineCap::Round && outline.stroke.join == LineJoin::Round);
    QVERIFY(tag->shapes()[1].filled && tag->shapes()[1].stroked);
    // Rect, line, polyline, polygon, ellipse and a group's attributes inherited.
    const std::shared_ptr<const VectorIcon> shapes = VectorIcon::parseSvg(
            "<?xml version=\"1.0\"?><!-- ikona: zażółć --><svg viewBox=\"0 0 48 24\">"
            "<g fill=\"none\" stroke=\"currentColor\" stroke-width=\"1.5\" stroke-linecap=\"square\">"
            "<rect x=\"3\" y=\"3\" width=\"18\" height=\"18\" rx=\"2\"/><line x1=\"1\" y1=\"1\" x2=\"5\" y2=\"5\"/>"
            "<polyline points=\"1,2 3,4 5,6\"/><polygon points=\"10 10 12 10 12 12\" fill=\"red\"/>"
            "<ellipse cx=\"30\" cy=\"12\" rx=\"6\" ry=\"3\"/></g></svg>");
    QVERIFY(shapes);
    QCOMPARE(shapes->viewBox(), Rect::fromXYWH(0, 0, 48, 24));
    QCOMPARE(shapes->shapes().size(), std::size_t(5));
    for (const VectorIcon::Shape &s : shapes->shapes()) {
        QVERIFY(s.stroked);
        QCOMPARE(s.stroke.width, 1.5f);
        QVERIFY(s.stroke.cap == LineCap::Square);
    }
    QVERIFY(shapes->shapes()[3].filled); // the polygon's own fill
    QVERIFY(!shapes->shapes()[1].filled); // a line has no inside
    // Without fill="none" a shape is filled (SVG's default black).
    const std::shared_ptr<const VectorIcon> filled =
            VectorIcon::parseSvg("<svg width='16' height='16'><path d='M0 0h16v16H0z'/></svg>");
    QVERIFY(filled && filled->shapes()[0].filled && !filled->shapes()[0].stroked);
    QCOMPARE(filled->viewBox(), Rect::fromXYWH(0, 0, 16, 16));
}

void TestAtramentVector::iconSkipsWhatIsNotDrawn()
{
    const std::shared_ptr<const VectorIcon> icon = VectorIcon::parseSvg(
            "<svg viewBox='0 0 24 24'><defs><clipPath id='c'><path d='M0 0h24v24H0z'/></clipPath></defs>"
            "<title>Dzwonek &amp; gęś</title><text x='1' y='1'>ąę</text>"
            "<path d='M2 2h20'><title>linia</title></path><style>path{}</style></svg>");
    QVERIFY(icon);
    QCOMPARE(icon->shapes().size(), std::size_t(1));
}

void TestAtramentVector::malformedIcons()
{
    QVERIFY(!VectorIcon::parseSvg(""));
    QVERIFY(!VectorIcon::parseSvg("nie jest to SVG: źdźbło"));
    QVERIFY(!VectorIcon::parseSvg("<svg viewBox='0 0 24 24'></svg>")); // nothing to draw
    QVERIFY(!VectorIcon::parseSvg("<path d='M0 0h5v5z'/>"));         // no <svg>
    // A shape after a broken tag is never reached; the one before is kept.
    const std::shared_ptr<const VectorIcon> half =
            VectorIcon::parseSvg("<svg><path d='M0 0h5v5z'/><path d='M1 1 <circle r=\"3\"/></svg>");
    QVERIFY(half && half->shapes().size() == 1);
    // Unclosed quotes, a stray '<', garbage numbers.
    QVERIFY(!VectorIcon::parseSvg("<svg viewBox='0 0 24 24'><path d=\"M0 0h5/></svg>"));
    QVERIFY(VectorIcon::parseSvg("<svg viewBox='x y z w'><circle cx='a' cy='5' r='3'/></svg>"));
    // Too large.
    std::string big = "<svg viewBox='0 0 24 24'>";
    while (big.size() <= VectorIcon::kMaxSvgBytes)
        big += "<path d='M0 0h1'/>";
    big += "</svg>";
    QVERIFY(!VectorIcon::parseSvg(big));
}

void TestAtramentVector::outlineCachedPerSize()
{
    const std::shared_ptr<const VectorIcon> icon = VectorIcon::parseSvg(
            "<svg viewBox='0 0 24 24' fill='none' stroke='#000' stroke-width='2'><path d='M4 12h16'/>"
            "<circle cx='12' cy='12' r='1' fill='#000' stroke='none'/></svg>");
    QVERIFY(icon);
    const auto at24 = icon->outline(24);
    QCOMPARE(icon->outline(24).get(), at24.get());
    const auto at48 = icon->outline(48);
    QVERIFY(at48.get() != at24.get());
    QCOMPARE(icon->outline(24).get(), at24.get()); // both kept
    // The stroke of a 2 px line from x 4 to 20 across the middle at 24 px: 16 × 2 px.
    const Coverage c = rasterize(at24->stroke, 24, 24);
    QVERIFY2(std::abs(c.total() - 16 * 2) < 0.5f, qPrintable(QString::number(c.total())));
    QVERIFY(c.at(10, 11) > 0.99f && c.at(10, 14) < 0.01f);
    // Twice the size: twice the stroke's width and length.
    const Coverage c48 = rasterize(at48->stroke, 48, 48);
    QVERIFY(std::abs(c48.total() - 4 * c.total()) < 8);
    QVERIFY(!at24->fill.points.empty());
    // A wider viewBox is fitted into the square and centred.
    const auto wide = VectorIcon::parseSvg("<svg viewBox='0 0 48 24'><path d='M0 0h48v24H0z'/></svg>");
    const Coverage w = rasterize(wide->outline(24)->fill, 24, 24);
    QVERIFY(std::abs(w.total() - 24 * 12) < 1);
    QVERIFY(w.at(12, 2) < 0.01f && w.at(12, 12) > 0.99f);
    QVERIFY(icon->outline(0)->fill.points.empty());
}

void TestAtramentVector::everyAppIconReads()
{
    const QDir dir(QStringLiteral(BRULION_SOURCE_DIR "/assets/icons"));
    const QStringList files = dir.entryList({QStringLiteral("*.svg")}, QDir::Files);
    QVERIFY(files.size() > 100);
    for (const QString &name : files) {
        QFile f(dir.filePath(name));
        QVERIFY(f.open(QIODevice::ReadOnly));
        const QByteArray svg = f.readAll();
        const std::shared_ptr<const VectorIcon> icon =
                VectorIcon::parseSvg(std::string_view(svg.constData(), std::size_t(svg.size())));
        QVERIFY2(icon, qPrintable(name));
        // Something inside the square at 20 px, nothing far outside it.
        const auto outline = icon->outline(20);
        QVERIFY2(!outline->stroke.points.empty() || !outline->fill.points.empty(), qPrintable(name));
        const Rect b = outline->stroke.points.empty() ? outline->fill.bounds : outline->stroke.bounds;
        QVERIFY2(b.left() > -2 && b.top() > -2 && b.right() < 22 && b.bottom() < 22, qPrintable(name));
    }
}

QTEST_GUILESS_MAIN(TestAtramentVector)
#include "tst_atrament_vector.moc"
