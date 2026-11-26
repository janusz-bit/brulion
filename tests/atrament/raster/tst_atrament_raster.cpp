// Atrament's CPU renderer (raster/raster.h): exact values where the maths is exact (solid
// interiors, pixels outside, half-covered edges within ±2), properties elsewhere (symmetry,
// falloff, endpoints, clipping), malformed input, and the time of a UI-like frame.
//
// A failing test writes its last frame as a PNG into the temporary directory (the path is
// printed), to look at what went wrong.
#include "raster/raster.h"

#include "base/coverage.h"
#include "scene/atlas.h"
#include "scene/scene.h"

#include "testtiming.h"

#include <QDir>
#include <QImage>
#include <QtTest>

#include <cmath>
#include <cstring>
#include <limits>
#include <memory>
#include <numbers>

namespace brulion::atrament {

char *toString(const Pixel &p)
{
    return QTest::toString(QStringLiteral("Pixel(%1, %2, %3, %4)").arg(p.r).arg(p.g).arg(p.b).arg(p.a));
}

} // namespace brulion::atrament

using namespace brulion::atrament;

namespace {

constexpr float kHuge = 1e30f;
const float kInf = std::numeric_limits<float>::infinity();
const float kNaN = std::numeric_limits<float>::quiet_NaN();

ContentMask everywhere()
{
    return {Rect::fromXYWH(-1e6f, -1e6f, 2e6f, 2e6f)};
}

Pixel px(int r, int g, int b, int a)
{
    return {std::uint8_t(r), std::uint8_t(g), std::uint8_t(b), std::uint8_t(a)};
}

// The pixel a premultiplied colour gives on its own.
Pixel opaquePixel(Color c)
{
    return blendOver(Pixel{}, PremulColor::from(c), 1);
}

// For messages: QTest::toString() hands out memory the caller owns.
QByteArray str(const Pixel &p)
{
    return QStringLiteral("Pixel(%1, %2, %3, %4)").arg(p.r).arg(p.g).arg(p.b).arg(p.a).toLatin1();
}

bool near(Pixel a, Pixel b, int tolerance)
{
    return std::abs(int(a.r) - int(b.r)) <= tolerance && std::abs(int(a.g) - int(b.g)) <= tolerance
           && std::abs(int(a.b) - int(b.b)) <= tolerance && std::abs(int(a.a) - int(b.a)) <= tolerance;
}

Quad quad(Rect bounds, Color fill, Corners radii = {}, Edges widths = {}, Color border = {})
{
    Quad q;
    q.bounds = bounds;
    q.mask = everywhere();
    q.background = Background::solid(fill);
    q.cornerRadii = radii;
    q.borderWidths = widths;
    q.borderColor = border;
    return q;
}

Shadow shadow(Rect bounds, Color color, float blur, Corners radii = {})
{
    Shadow s;
    s.bounds = bounds;
    s.mask = everywhere();
    s.color = color;
    s.blurRadius = blur;
    s.cornerRadii = radii;
    return s;
}

double alphaSum(const RasterTarget &t)
{
    double sum = 0;
    for (const Pixel &p : t.pixels)
        sum += p.a / 255.0;
    return sum;
}

AtlasBitmap alphaBitmap(int w, int h, const std::vector<std::uint8_t> &values)
{
    AtlasBitmap b;
    b.width = w;
    b.height = h;
    b.bytesPerPixel = 1;
    b.pixels = values;
    return b;
}

AtlasBitmap colorBitmap(int w, int h, const std::vector<Pixel> &values)
{
    AtlasBitmap b;
    b.width = w;
    b.height = h;
    b.bytesPerPixel = 4;
    for (const Pixel &p : values) {
        b.pixels.push_back(p.r);
        b.pixels.push_back(p.g);
        b.pixels.push_back(p.b);
        b.pixels.push_back(p.a);
    }
    return b;
}

// A bilinear sample of a 2×2 texture at texel-space (u, v), as the renderer defines it (texel
// centres at i + 0.5, clamped to the edge), in doubles.
Pixel bilinearReference(const Pixel tex[4], double u, double v)
{
    u = std::clamp(u - 0.5, 0.0, 1.0);
    v = std::clamp(v - 0.5, 0.0, 1.0);
    const auto ch = [&](auto get) {
        const double top = get(tex[0]) * (1 - u) + get(tex[1]) * u;
        const double bottom = get(tex[2]) * (1 - u) + get(tex[3]) * u;
        return int(std::lround(top * (1 - v) + bottom * v));
    };
    return px(ch([](Pixel p) { return double(p.r); }), ch([](Pixel p) { return double(p.g); }),
              ch([](Pixel p) { return double(p.b); }), ch([](Pixel p) { return double(p.a); }));
}

} // namespace

class TestAtramentRaster : public QObject {
    Q_OBJECT

private Q_SLOTS:
    void cleanup();

    void clearsToClearColor();
    void blendOverArithmetic_data();
    void blendOverArithmetic();
    void quadInteriorExact_data();
    void quadInteriorExact();
    void quadHalfPixelEdges();
    void roundedQuadSymmetryAndArea();
    void cornerRadiiClamped_data();
    void cornerRadiiClamped();
    void perCornerRadii();
    void bordersPerSide();
    void translucentBorderOverFill();
    void borderOnlyKeepsInside();
    void roundedBorderRing();
    void gradientEndpoints_data();
    void gradientEndpoints();
    void gradientPremultiplied();
    void shadowFalloff();
    void shadowZeroBlurIsSharp();
    void shadowMalformed();
    void monochromeSprite();
    void monochromeSpriteAtPageEdge();
    void colorSpriteBilinear();
    void colorSpriteOpacityAndGrey();
    void pictureCopyAndSource();
    void pictureRoundedCorners();
    void pictureMalformed();
    void pathArea_data();
    void pathArea();
    void pathWinding();
    void pathMalformed();
    void straightUnderline_data();
    void straightUnderline();
    void wavyUnderline();
    void maskClipping_data();
    void maskClipping();
    void damageClipping();
    void primitivesOutsideOrMalformed();
    void hugeQuadStaysExact();
    void occlusionChangesNothing();
    void replayedRowsMatchComputed();
    void deterministic();
    void uiFrameTime();

private:
    RasterTarget render(const Scene &scene, int w, int h, Color clear = transparent(), const Atlas *atlas = nullptr,
                        std::optional<Rect> damage = std::nullopt, RasterStats *stats = nullptr);

    RasterTarget m_last;
};

RasterTarget TestAtramentRaster::render(const Scene &scene, int w, int h, Color clear, const Atlas *atlas,
                                        std::optional<Rect> damage, RasterStats *stats)
{
    static const Atlas empty;
    RasterTarget target;
    target.resize(w, h);
    RasterRenderer renderer;
    const RasterStats s = renderer.render(scene, atlas ? *atlas : empty, target, clear, damage);
    if (stats)
        *stats = s;
    m_last = target;
    return target;
}

void TestAtramentRaster::cleanup()
{
    if (QTest::currentTestFailed() && m_last.width > 0 && m_last.height > 0) {
        QImage image(m_last.width, m_last.height, QImage::Format_RGBA8888_Premultiplied);
        for (int y = 0; y < m_last.height; ++y)
            std::memcpy(image.scanLine(y), &m_last.pixels[std::size_t(y) * std::size_t(m_last.width)],
                        std::size_t(m_last.width) * 4);
        QString name = QString::fromLatin1(QTest::currentTestFunction());
        if (QTest::currentDataTag())
            name += QLatin1Char('-')
                    + QString::fromLatin1(QTest::currentDataTag()).replace(QLatin1Char(' '), QLatin1Char('_'));
        const QString path = QDir::temp().filePath(QStringLiteral("atrament-raster-%1.png").arg(name));
        if (image.save(path))
            qWarning("frame written to %s", qPrintable(path));
    }
    m_last = {};
}

void TestAtramentRaster::clearsToClearColor()
{
    Scene scene;
    RasterStats stats;
    const RasterTarget opaque = render(scene, 7, 5, rgb(0x336699), nullptr, std::nullopt, &stats);
    QCOMPARE(stats.primitives, std::size_t(0));
    QCOMPARE(stats.pixelsTouched, std::size_t(0));
    for (const Pixel &p : opaque.pixels)
        QCOMPARE(p, px(0x33, 0x66, 0x99, 255));
    // A translucent clear colour is stored premultiplied.
    const RasterTarget translucent = render(scene, 3, 3, Color{1, 0.5f, 0, 0.5f});
    for (const Pixel &p : translucent.pixels)
        QCOMPARE(p, px(128, 64, 0, 128));
    // An empty target is not an error.
    const RasterTarget none = render(scene, 0, 0);
    QVERIFY(none.pixels.empty());
}

void TestAtramentRaster::blendOverArithmetic_data()
{
    QTest::addColumn<Pixel>("dst");
    QTest::addColumn<Color>("src");
    QTest::addColumn<float>("cov");
    QTest::addColumn<Pixel>("expected");
    QTest::newRow("opaque replaces") << px(0, 0, 255, 255) << rgb(0xff0000) << 1.0f << px(255, 0, 0, 255);
    // 8-bit arithmetic: the source rounds to 128 first, the rest 255 × 127 / 255.
    QTest::newRow("half red over blue") << px(0, 0, 255, 255) << Color{1, 0, 0, 0.5f} << 1.0f << px(128, 0, 127, 255);
    QTest::newRow("quarter green over half blue")
            << px(0, 0, 128, 128) << Color{0, 1, 0, 0.25f} << 1.0f << px(0, 64, 96, 160);
    QTest::newRow("half coverage of white") << px(0, 0, 0, 0) << white() << 0.5f << px(128, 128, 128, 128);
    QTest::newRow("no coverage") << px(10, 20, 30, 40) << white() << 0.0f << px(10, 20, 30, 40);
    QTest::newRow("coverage above 1") << px(10, 20, 30, 40) << black() << 3.0f << px(0, 0, 0, 255);
    QTest::newRow("NaN coverage") << px(10, 20, 30, 40) << black() << kNaN << px(10, 20, 30, 40);
    QTest::newRow("transparent") << px(10, 20, 30, 40) << transparent() << 1.0f << px(10, 20, 30, 40);
}

void TestAtramentRaster::blendOverArithmetic()
{
    QFETCH(Pixel, dst);
    QFETCH(Color, src);
    QFETCH(float, cov);
    QFETCH(Pixel, expected);
    QCOMPARE(blendOver(dst, PremulColor::from(src), cov), expected);
    // Out-of-range premultiplied input is clamped (colour ≤ alpha ≤ 1), never wraps.
    QCOMPARE(blendOver(px(0, 0, 0, 0), PremulColor{2, -1, kNaN, 1}, 1), px(255, 0, 0, 255));
}

void TestAtramentRaster::quadInteriorExact_data()
{
    QTest::addColumn<Color>("fill");
    QTest::addColumn<Color>("clear");
    QTest::newRow("opaque on transparent") << rgb(0x2a9d8f) << transparent();
    QTest::newRow("translucent on opaque") << Color{0.9f, 0.2f, 0.1f, 0.35f} << rgb(0x264653);
    QTest::newRow("translucent on translucent") << Color{0.1f, 0.2f, 0.9f, 0.6f} << Color{1, 1, 1, 0.5f};
}

void TestAtramentRaster::quadInteriorExact()
{
    QFETCH(Color, fill);
    QFETCH(Color, clear);
    Scene scene;
    scene.insert(quad(Rect::fromXYWH(10, 10, 30, 20), fill, Corners::all(4)));
    RasterStats stats;
    const RasterTarget t = render(scene, 60, 40, clear, nullptr, std::nullopt, &stats);
    const Pixel background = opaquePixel(clear);
    const Pixel inside = blendOver(background, PremulColor::from(fill), 1);
    QCOMPARE(stats.primitives, std::size_t(1));
    for (int y = 0; y < t.height; ++y) {
        for (int x = 0; x < t.width; ++x) {
            const bool in = x >= 10 && x < 40 && y >= 10 && y < 30;
            const bool corner = (x < 14 || x >= 36) && (y < 14 || y >= 26);
            if (!in)
                QCOMPARE(t.at(x, y), background);
            else if (!corner)
                QCOMPARE(t.at(x, y), inside);
        }
    }
}

void TestAtramentRaster::quadHalfPixelEdges()
{
    Scene scene;
    scene.insert(quad(Rect::fromLTRB(10.5f, 4, 30.5f, 12.5f), white()));
    const RasterTarget t = render(scene, 40, 16);
    const Pixel half = px(128, 128, 128, 128);
    for (int y = 4; y < 12; ++y) {
        QVERIFY2(near(t.at(10, y), half, 2), qPrintable(QString::number(y)));
        QVERIFY(near(t.at(30, y), half, 2));
        QCOMPARE(t.at(11, y), px(255, 255, 255, 255));
        QCOMPARE(t.at(9, y), Pixel{});
        QCOMPARE(t.at(31, y), Pixel{});
    }
    for (int x = 11; x < 30; ++x) {
        QVERIFY(near(t.at(x, 12), half, 2));
        QCOMPARE(t.at(x, 13), Pixel{});
        QCOMPARE(t.at(x, 3), Pixel{});
    }
    // The corner pixel is covered no more than the edges next to it.
    QVERIFY(t.at(10, 12).a <= t.at(10, 11).a);
}

void TestAtramentRaster::roundedQuadSymmetryAndArea()
{
    Scene scene;
    const Rect bounds = Rect::fromXYWH(10, 10, 40, 30);
    const float r = 8;
    scene.insert(quad(bounds, black(), Corners::all(r)));
    const RasterTarget t = render(scene, 60, 50);
    // Mirror images across both centre lines (x = 30, y = 25) are identical.
    for (int y = 0; y < 50; ++y) {
        for (int x = 0; x < 60; ++x) {
            if (59 - x >= 0 && 59 - x < 60)
                QCOMPARE(t.at(x, y), t.at(59 - x, y));
            if (49 - y >= 0)
                QCOMPARE(t.at(x, y), t.at(x, 49 - y));
        }
    }
    // The corner pixel lies outside the arc; the straight edge past it is solid.
    QCOMPARE(t.at(10, 10), Pixel{});
    QCOMPARE(t.at(18, 10), px(0, 0, 0, 255));
    QCOMPARE(t.at(10, 18), px(0, 0, 0, 255));
    // Coverage adds up to the area: w·h − (4 − π)·r².
    const double area = 40.0 * 30.0 - (4 - std::numbers::pi) * r * r;
    QVERIFY2(std::abs(alphaSum(t) - area) < 1.0, qPrintable(QStringLiteral("%1 vs %2").arg(alphaSum(t)).arg(area)));
}

void TestAtramentRaster::cornerRadiiClamped_data()
{
    QTest::addColumn<float>("radius");
    QTest::addColumn<float>("equivalent");
    QTest::newRow("huge") << kHuge << 15.0f;
    QTest::newRow("infinite") << kInf << 15.0f;
    QTest::newRow("just above half") << 15.5f << 15.0f;
    QTest::newRow("NaN") << kNaN << 0.0f;
    QTest::newRow("negative") << -5.0f << 0.0f;
}

void TestAtramentRaster::cornerRadiiClamped()
{
    QFETCH(float, radius);
    QFETCH(float, equivalent);
    const Rect bounds = Rect::fromXYWH(5, 5, 50, 30);
    Scene a;
    a.insert(quad(bounds, rgb(0x6a4c93), Corners::all(radius), Edges::all(3), rgb(0xffca3a)));
    Scene b;
    b.insert(quad(bounds, rgb(0x6a4c93), Corners::all(equivalent), Edges::all(3), rgb(0xffca3a)));
    const RasterTarget ta = render(a, 60, 40);
    const RasterTarget tb = render(b, 60, 40);
    QVERIFY(ta.pixels == tb.pixels);
    // A pill: the middle of the left edge is solid.
    if (equivalent == 15)
        QVERIFY(near(ta.at(5, 20), opaquePixel(rgb(0xffca3a)), 3));
}

void TestAtramentRaster::perCornerRadii()
{
    Scene scene;
    scene.insert(quad(Rect::fromXYWH(0, 0, 40, 40), white(), Corners{0, 10, 0, 10}));
    const RasterTarget t = render(scene, 40, 40);
    const Pixel full = px(255, 255, 255, 255);
    QCOMPARE(t.at(0, 0), full);     // square
    QCOMPARE(t.at(39, 0), Pixel{}); // rounded
    QCOMPARE(t.at(39, 39), full);
    QCOMPARE(t.at(0, 39), Pixel{});
    QCOMPARE(t.at(20, 0), full);
    QCOMPARE(t.at(0, 20), full);
}

void TestAtramentRaster::bordersPerSide()
{
    Scene scene;
    const Color border = rgb(0xff0000);
    const Color fill = rgb(0x00ff00);
    // Widths: top 2, right 4, bottom 6, left 8.
    scene.insert(quad(Rect::fromXYWH(10, 10, 40, 30), fill, {}, Edges{2, 4, 6, 8}, border));
    const RasterTarget t = render(scene, 60, 50);
    const Pixel b = opaquePixel(border);
    const Pixel f = opaquePixel(fill);
    for (int x = 10; x < 50; ++x) {
        QCOMPARE(t.at(x, 10), b);
        QCOMPARE(t.at(x, 11), b);
        QCOMPARE(t.at(x, 39), b);
        QCOMPARE(t.at(x, 34), b);
    }
    for (int y = 12; y < 34; ++y) {
        for (int x = 10; x < 18; ++x)
            QCOMPARE(t.at(x, y), b);
        for (int x = 18; x < 46; ++x)
            QCOMPARE(t.at(x, y), f);
        for (int x = 46; x < 50; ++x)
            QCOMPARE(t.at(x, y), b);
    }
    QCOMPARE(t.at(30, 12), f);
    QCOMPARE(t.at(30, 33), f);
    QCOMPARE(t.at(30, 40), Pixel{});
    // Half-pixel widths: the inner edge pixel mixes fill and border half and half.
    Scene half;
    half.insert(quad(Rect::fromXYWH(0, 0, 20, 20), rgb(0x0000ff), {}, Edges::all(1.5f), rgb(0xff0000)));
    const RasterTarget th = render(half, 20, 20);
    QCOMPARE(th.at(0, 10), px(255, 0, 0, 255));
    QVERIFY(near(th.at(1, 10), px(128, 0, 128, 255), 2));
    QCOMPARE(th.at(2, 10), px(0, 0, 255, 255));
}

void TestAtramentRaster::translucentBorderOverFill()
{
    // The fill lies under the border (background-clip: border-box): a translucent border shows it.
    const Color fill = rgb(0x0000ff);
    const Color border = Color{1, 0, 0, 0.5f};
    Scene scene;
    scene.insert(quad(Rect::fromXYWH(0, 0, 20, 20), fill, {}, Edges::all(3), border));
    const RasterTarget t = render(scene, 20, 20, rgb(0x00ff00));
    const Pixel underBorder = blendOver(opaquePixel(fill), PremulColor::from(border), 1);
    QVERIFY(near(t.at(1, 10), underBorder, 1));
    QCOMPARE(t.at(10, 10), opaquePixel(fill));
    // A translucent fill with a translucent border: both blend over what was there.
    Scene both;
    const Color thin = Color{0, 0, 1, 0.5f};
    both.insert(quad(Rect::fromXYWH(0, 0, 20, 20), thin, {}, Edges::all(3), border));
    const RasterTarget tb = render(both, 20, 20, rgb(0x00ff00));
    const Pixel green = opaquePixel(rgb(0x00ff00));
    const Pixel expected = blendOver(blendOver(green, PremulColor::from(thin), 1), PremulColor::from(border), 1);
    QVERIFY2(near(tb.at(1, 10), expected, 1), str(tb.at(1, 10)).constData());
    QCOMPARE(tb.at(10, 10), blendOver(green, PremulColor::from(thin), 1));
}

void TestAtramentRaster::borderOnlyKeepsInside()
{
    Scene scene;
    scene.insert(quad(Rect::fromXYWH(0, 0, 30, 30), rgb(0x123456)));
    scene.insert(quad(Rect::fromXYWH(5, 5, 20, 20), transparent(), Corners::all(6), Edges::all(1), white()));
    const RasterTarget t = render(scene, 30, 30);
    const Pixel under = opaquePixel(rgb(0x123456));
    for (int y = 9; y < 21; ++y)
        for (int x = 9; x < 21; ++x)
            QCOMPARE(t.at(x, y), under);
    QCOMPARE(t.at(15, 5), px(255, 255, 255, 255));
    QCOMPARE(t.at(5, 15), px(255, 255, 255, 255));
    QCOMPARE(t.at(15, 6), under);
}

void TestAtramentRaster::roundedBorderRing()
{
    // Radius 10, border 2: the ring lies between the circles of radius 8 and 10 around (20, 20).
    Scene scene;
    scene.insert(quad(Rect::fromXYWH(10, 10, 40, 40), rgb(0x0000ff), Corners::all(10), Edges::all(2), rgb(0xff0000)));
    const RasterTarget t = render(scene, 60, 60);
    QCOMPARE(t.at(13, 13), px(255, 0, 0, 255)); // centre 9.19 from the arcs' centre
    QCOMPARE(t.at(15, 15), px(0, 0, 255, 255)); // 6.36: inside the inner arc
    QCOMPARE(t.at(10, 10), Pixel{});            // 13.4: outside
    QCOMPARE(t.at(44, 44), px(0, 0, 255, 255));
    QCOMPARE(t.at(46, 46), px(255, 0, 0, 255));
    // Every pixel of the quad is opaque but for the anti-aliased outer arc: no fringe of the fill.
    for (int y = 10; y < 50; ++y)
        for (int x = 10; x < 50; ++x)
            if (t.at(x, y).a < 255)
                QCOMPARE(t.at(x, y).b, std::uint8_t(0));
}

void TestAtramentRaster::gradientEndpoints_data()
{
    QTest::addColumn<float>("angle");
    QTest::addColumn<Rect>("bounds");
    QTest::addColumn<QPoint>("fromPixel");
    QTest::addColumn<QPoint>("toPixel");
    QTest::newRow("to right") << 90.0f << Rect::fromXYWH(0, 0, 100, 10) << QPoint(0, 5) << QPoint(99, 5);
    QTest::newRow("to left") << 270.0f << Rect::fromXYWH(0, 0, 100, 10) << QPoint(99, 5) << QPoint(0, 5);
    QTest::newRow("to bottom") << 180.0f << Rect::fromXYWH(0, 0, 10, 100) << QPoint(5, 0) << QPoint(5, 99);
    QTest::newRow("to top") << 0.0f << Rect::fromXYWH(0, 0, 10, 100) << QPoint(5, 99) << QPoint(5, 0);
    QTest::newRow("to top right") << 45.0f << Rect::fromXYWH(0, 0, 100, 100) << QPoint(0, 99) << QPoint(99, 0);
    QTest::newRow("to bottom left, wide") << 225.0f << Rect::fromXYWH(0, 0, 100, 40) << QPoint(99, 0) << QPoint(0, 39);
    QTest::newRow("full turn") << -270.0f << Rect::fromXYWH(0, 0, 100, 10) << QPoint(0, 5) << QPoint(99, 5);
}

void TestAtramentRaster::gradientEndpoints()
{
    QFETCH(float, angle);
    QFETCH(Rect, bounds);
    QFETCH(QPoint, fromPixel);
    QFETCH(QPoint, toPixel);
    const Color from = rgb(0xff0000);
    const Color to = rgb(0x0000ff);
    Scene scene;
    Quad q = quad(bounds, from);
    q.background = Background::linear(angle, from, to);
    scene.insert(q);
    const RasterTarget t = render(scene, int(bounds.width()), int(bounds.height()));
    QVERIFY2(near(t.at(fromPixel.x(), fromPixel.y()), px(255, 0, 0, 255), 2),
             str(t.at(fromPixel.x(), fromPixel.y())).constData());
    QVERIFY2(near(t.at(toPixel.x(), toPixel.y()), px(0, 0, 255, 255), 2),
             str(t.at(toPixel.x(), toPixel.y())).constData());
    // Along the line between the two the red falls, the blue rises, and every pixel is opaque.
    const int steps = 50;
    int lastRed = 256;
    for (int i = 0; i <= steps; ++i) {
        const int x = fromPixel.x() + (toPixel.x() - fromPixel.x()) * i / steps;
        const int y = fromPixel.y() + (toPixel.y() - fromPixel.y()) * i / steps;
        const Pixel p = t.at(x, y);
        QCOMPARE(p.a, std::uint8_t(255));
        QVERIFY(int(p.r) <= lastRed);
        lastRed = p.r;
        QVERIFY(std::abs(int(p.r) + int(p.b) - 255) <= 1);
    }
}

void TestAtramentRaster::gradientPremultiplied()
{
    // From transparent to white: halfway is half-transparent white, not grey (premultiplied mixing).
    Scene scene;
    Quad q = quad(Rect::fromXYWH(0, 0, 100, 4), white());
    q.background = Background::linear(90, transparent(), white());
    scene.insert(q);
    const RasterTarget t = render(scene, 100, 4);
    const Pixel mid = t.at(50, 2);
    QVERIFY(near(mid, px(129, 129, 129, 129), 2));
    for (int x = 0; x < 100; ++x)
        QCOMPARE(t.at(x, 1).r, t.at(x, 1).a);
}

void TestAtramentRaster::shadowFalloff()
{
    // σ = 10, reaching 30 beyond the bounds.
    const Rect bounds = Rect::fromXYWH(60, 60, 80, 60);
    Scene scene;
    scene.insert(shadow(bounds, black(), 20, Corners::all(8)));
    RasterStats stats;
    const RasterTarget t = render(scene, 200, 180, transparent(), nullptr, std::nullopt, &stats);
    QCOMPARE(stats.primitives, std::size_t(1));
    // Nearly opaque in the middle, nothing beyond 3σ.
    QVERIFY2(t.at(99, 89).a >= 250, str(t.at(99, 89)).constData());
    for (int y = 0; y < 180; ++y) {
        QCOMPARE(t.at(29, y), Pixel{});
        QCOMPARE(t.at(170, y), Pixel{});
    }
    for (int x = 0; x < 200; ++x) {
        QCOMPARE(t.at(x, 29), Pixel{});
        QCOMPARE(t.at(x, 150), Pixel{});
    }
    // Half the strength at the edge of the box, at mid-height.
    QVERIFY2(std::abs(int(t.at(60, 89).a) - 128) <= 6, str(t.at(60, 89)).constData());
    // Falls off monotonically from the middle outwards: along a row, a column and the diagonal.
    for (int x = 100; x < 199; ++x)
        QVERIFY2(int(t.at(x + 1, 90).a) <= int(t.at(x, 90).a) + 1, qPrintable(QString::number(x)));
    for (int y = 90; y < 179; ++y)
        QVERIFY2(int(t.at(100, y + 1).a) <= int(t.at(100, y).a) + 1, qPrintable(QString::number(y)));
    for (int i = 0; i < 70; ++i)
        QVERIFY2(int(t.at(100 + i + 1, 90 + i + 1).a) <= int(t.at(100 + i, 90 + i).a) + 1,
                 qPrintable(QString::number(i)));
    // Symmetric: exactly across the vertical centre line, within a level across the horizontal one.
    for (int y = 0; y < 180; ++y)
        for (int x = 0; x < 100; ++x)
            QCOMPARE(t.at(x, y), t.at(199 - x, y));
    for (int y = 0; y < 90; ++y)
        for (int x = 0; x < 200; ++x)
            QVERIFY(near(t.at(x, y), t.at(x, 179 - y), 1));
    // The rounded corner pulls the shadow in: at the corner it is fainter than at an edge's middle
    // the same distance out.
    QVERIFY(t.at(56, 56).a < t.at(56, 89).a);
}

void TestAtramentRaster::shadowZeroBlurIsSharp()
{
    const Rect bounds = Rect::fromXYWH(4.5f, 4, 30, 20);
    const Color c = Color{0, 0, 0, 0.4f};
    Scene a;
    a.insert(shadow(bounds, c, 0, Corners::all(5)));
    Scene b;
    b.insert(quad(bounds, c, Corners::all(5)));
    const RasterTarget ta = render(a, 40, 30, white());
    const RasterTarget tb = render(b, 40, 30, white());
    QVERIFY(ta.pixels == tb.pixels);
}

void TestAtramentRaster::shadowMalformed()
{
    Scene scene;
    scene.insert(shadow(Rect::fromXYWH(10, 10, 20, 20), black(), kHuge, Corners::all(kHuge)));
    scene.insert(shadow(Rect::fromXYWH(-kHuge, -kHuge, 2 * kHuge, 2 * kHuge), Color{0, 0, 0, 0.1f}, 8));
    scene.insert(shadow(Rect::fromXYWH(10, 10, 20, 20), black(), kInf));
    scene.insert(shadow(Rect::fromXYWH(10, 10, 20, 20), black(), kNaN));
    scene.insert(shadow(Rect::fromXYWH(10, 10, -20, 20), black(), 4));
    scene.insert(shadow(Rect::fromXYWH(10, 10, 20, 20), transparent(), 4));
    const RasterTarget t = render(scene, 40, 40, white());
    // Deterministic and in range (nothing NaN-born): the same again.
    const RasterTarget again = render(scene, 40, 40, white());
    QVERIFY(t.pixels == again.pixels);
    for (const Pixel &p : t.pixels)
        QVERIFY(p.r <= p.a && p.g <= p.a && p.b <= p.a);
}

void TestAtramentRaster::monochromeSprite()
{
    Atlas atlas(64);
    const std::vector<std::uint8_t> values{0, 64, 128, 255, 255, 128, 64, 0, 1, 2, 254, 200};
    const auto tile = atlas.getOrInsert(1, AtlasKind::Alpha, [&] { return std::optional(alphaBitmap(4, 3, values)); });
    QVERIFY(tile);
    const Color red = rgb(0xff0000);
    const auto check = [&](const RasterTarget &t, int ox, int oy, Color c, Pixel under) {
        for (int y = 0; y < 3; ++y) {
            for (int x = 0; x < 4; ++x) {
                const float cov = values[std::size_t(y * 4 + x)] / 255.0f;
                const Pixel expected = blendOver(under, PremulColor::from(c), cov);
                QVERIFY2(near(t.at(ox + x, oy + y), expected, 1),
                         QByteArray(QByteArray::number(x) + ',' + QByteArray::number(y) + ' '
                                    + str(t.at(ox + x, oy + y)))
                                 .constData());
            }
        }
    };
    Scene scene;
    MonochromeSprite s;
    s.bounds = Rect::fromXYWH(5, 7, 4, 3);
    s.mask = everywhere();
    s.color = red;
    s.tile = *tile;
    scene.insert(s);
    RasterStats stats;
    const RasterTarget t = render(scene, 16, 16, transparent(), &atlas, std::nullopt, &stats);
    QCOMPARE(stats.pixelsTouched, std::size_t(12));
    check(t, 5, 7, red, Pixel{});
    QCOMPARE(t.at(8, 7), px(255, 0, 0, 255)); // full coverage: the colour itself
    QCOMPARE(t.at(5, 7), Pixel{});
    QCOMPARE(t.at(4, 7), Pixel{});
    QCOMPARE(t.at(9, 7), Pixel{});
    // A fractional origin is rounded to whole pixels; a translucent colour blends.
    Scene shifted;
    s.bounds = Rect::fromXYWH(5.4f, 6.6f, 4, 3);
    s.color = Color{0, 0, 1, 0.5f};
    shifted.insert(s);
    const RasterTarget ts = render(shifted, 16, 16, white(), &atlas);
    check(ts, 5, 7, Color{0, 0, 1, 0.5f}, px(255, 255, 255, 255));
    // Partly outside the target: cut, no crash.
    Scene corner;
    s.bounds = Rect::fromXYWH(-2, -1, 4, 3);
    corner.insert(s);
    s.bounds = Rect::fromXYWH(14, 15, 4, 3);
    corner.insert(s);
    render(corner, 16, 16, white(), &atlas);
}

void TestAtramentRaster::monochromeSpriteAtPageEdge()
{
    // A 62-wide tile fills a 64-texel page up to its padding column; a malformed tile reaching
    // past the page's edge (or before its start) is cut to the texels that exist.
    Atlas atlas(64);
    const auto tile = atlas.getOrInsert(
            7, AtlasKind::Alpha, [] { return std::optional(alphaBitmap(62, 2, std::vector<std::uint8_t>(124, 255))); });
    QVERIFY(tile);
    QCOMPARE(tile->x + tile->width, 63);
    const AtlasPage &page = atlas.pages()[tile->page];
    Scene scene;
    MonochromeSprite s;
    s.mask = everywhere();
    s.color = black();
    s.tile = *tile;
    s.tile.x = page.width - 4; // texels 60..63: three of the bitmap, then the padding column
    s.tile.width = 10;
    s.bounds = Rect::fromXYWH(0, 0, 10, 2);
    scene.insert(s);
    s.tile.x = -3; // texels −3..6 of which 0..6 exist: drawn from x = 3 on
    s.bounds = Rect::fromXYWH(0, 4, 10, 2);
    scene.insert(s);
    s.tile.page = 99; // no such page
    scene.insert(s);
    MonochromeSprite wrongKind = s;
    wrongKind.tile = *tile;
    wrongKind.tile.page = tile->page;
    const auto colour = atlas.getOrInsert(8, AtlasKind::Color,
                                          [] { return std::optional(colorBitmap(1, 1, {Pixel{1, 2, 3, 255}})); });
    QVERIFY(colour);
    wrongKind.tile = *colour; // a Color page
    wrongKind.bounds = Rect::fromXYWH(0, 8, 1, 1);
    scene.insert(wrongKind);
    const RasterTarget t = render(scene, 12, 10, transparent(), &atlas);
    for (int x = 0; x < 12; ++x) {
        QCOMPARE(t.at(x, 0), x < 3 ? px(0, 0, 0, 255) : Pixel{});
        QCOMPARE(t.at(x, 4), x == 3 ? Pixel{} : (x > 3 && x < 10 ? px(0, 0, 0, 255) : Pixel{}));
    }
    QCOMPARE(t.at(0, 8), Pixel{});
}

void TestAtramentRaster::colorSpriteBilinear()
{
    Atlas atlas(64);
    const Pixel tex[4] = {px(255, 0, 0, 255), px(0, 255, 0, 255), px(0, 0, 255, 255), px(255, 255, 255, 255)};
    const auto tile = atlas.getOrInsert(
            3, AtlasKind::Color, [&] { return std::optional(colorBitmap(2, 2, {tex[0], tex[1], tex[2], tex[3]})); });
    QVERIFY(tile);
    Scene scene;
    ColorSprite s;
    s.bounds = Rect::fromXYWH(10, 10, 20, 20);
    s.mask = everywhere();
    s.tile = *tile;
    scene.insert(s);
    // 1:1 next to it: the texels themselves.
    s.bounds = Rect::fromXYWH(40, 10, 2, 2);
    scene.insert(s);
    const RasterTarget t = render(scene, 50, 40, transparent(), &atlas);
    QCOMPARE(t.at(10, 10), tex[0]);
    QCOMPARE(t.at(29, 10), tex[1]);
    QCOMPARE(t.at(10, 29), tex[2]);
    QCOMPARE(t.at(29, 29), tex[3]);
    QCOMPARE(t.at(40, 10), tex[0]);
    QCOMPARE(t.at(41, 10), tex[1]);
    QCOMPARE(t.at(40, 11), tex[2]);
    QCOMPARE(t.at(41, 11), tex[3]);
    QCOMPARE(t.at(9, 10), Pixel{});
    QCOMPARE(t.at(30, 30), Pixel{});
    for (int y = 10; y < 30; ++y) {
        for (int x = 10; x < 30; ++x) {
            const Pixel expected = bilinearReference(tex, (x - 10 + 0.5) / 10.0, (y - 10 + 0.5) / 10.0);
            QVERIFY2(near(t.at(x, y), expected, 1), qPrintable(QStringLiteral("%1,%2").arg(x).arg(y)));
        }
    }
}

void TestAtramentRaster::colorSpriteOpacityAndGrey()
{
    Atlas atlas(64);
    const auto tile = atlas.getOrInsert(4, AtlasKind::Color,
                                        [] { return std::optional(colorBitmap(1, 1, {px(255, 0, 0, 255)})); });
    QVERIFY(tile);
    Scene scene;
    ColorSprite s;
    s.mask = everywhere();
    s.tile = *tile;
    s.bounds = Rect::fromXYWH(0, 0, 4, 4);
    s.opacity = 0.5f;
    scene.insert(s);
    s.bounds = Rect::fromXYWH(4, 0, 4, 4);
    s.opacity = 1;
    s.grayscale = true;
    scene.insert(s);
    s.bounds = Rect::fromXYWH(8, 0, 4, 4);
    s.opacity = kNaN;
    scene.insert(s);
    // Fractional bounds: the edge pixels are covered in part.
    s.bounds = Rect::fromXYWH(12.5f, 0, 3, 4);
    s.opacity = 1;
    s.grayscale = false;
    scene.insert(s);
    const RasterTarget t = render(scene, 16, 4, transparent(), &atlas);
    QVERIFY(near(t.at(1, 1), px(128, 0, 0, 128), 1));
    QVERIFY(near(t.at(5, 1), px(54, 54, 54, 255), 1)); // Rec. 709 luma of red
    QCOMPARE(t.at(9, 1), Pixel{});
    QVERIFY(near(t.at(12, 1), px(128, 0, 0, 128), 2));
    QCOMPARE(t.at(13, 1), px(255, 0, 0, 255));
    QVERIFY(near(t.at(15, 1), px(128, 0, 0, 128), 2));
}

void TestAtramentRaster::pictureCopyAndSource()
{
    std::vector<Pixel> pixels;
    for (int y = 0; y < 4; ++y)
        for (int x = 0; x < 4; ++x)
            pixels.push_back(px(x * 60, y * 60, 0, 255));
    const auto image = ImageData::make(4, 4, pixels);
    Scene scene;
    Picture p;
    p.mask = everywhere();
    p.image = image;
    p.bounds = Rect::fromXYWH(1, 1, 4, 4);
    scene.insert(p);
    p.bounds = Rect::fromXYWH(10, 1, 2, 2);
    p.source = Rect::fromXYWH(2, 1, 2, 2);
    scene.insert(p);
    const RasterTarget t = render(scene, 14, 6, transparent());
    for (int y = 0; y < 4; ++y)
        for (int x = 0; x < 4; ++x)
            QCOMPARE(t.at(1 + x, 1 + y), pixels[std::size_t(y * 4 + x)]);
    QCOMPARE(t.at(10, 1), pixels[1 * 4 + 2]);
    QCOMPARE(t.at(11, 2), pixels[2 * 4 + 3]);
    QCOMPARE(t.at(0, 0), Pixel{});
    QCOMPARE(t.at(12, 1), Pixel{});
}

void TestAtramentRaster::pictureRoundedCorners()
{
    // A 2×2 white image stretched over 40×40 with radius 20: a disc.
    const auto image = ImageData::make(2, 2, std::vector<Pixel>(4, px(255, 255, 255, 255)));
    Scene scene;
    Picture p;
    p.mask = everywhere();
    p.image = image;
    p.bounds = Rect::fromXYWH(0, 0, 40, 40);
    p.cornerRadii = Corners::all(20);
    p.opacity = 0.75f;
    scene.insert(p);
    const RasterTarget t = render(scene, 40, 40);
    QCOMPARE(t.at(0, 0), Pixel{});
    QCOMPARE(t.at(39, 39), Pixel{});
    QVERIFY(near(t.at(20, 20), px(191, 191, 191, 191), 1));
    QVERIFY(near(t.at(0, 20), px(191, 191, 191, 191), 1));
    for (int y = 0; y < 40; ++y)
        for (int x = 0; x < 40; ++x)
            QCOMPARE(t.at(x, y), t.at(39 - x, 39 - y));
    // The disc's area: π r² × 0.75 of coverage.
    const double expected = std::numbers::pi * 400 * 0.75;
    QVERIFY2(std::abs(alphaSum(t) - expected) < 2, qPrintable(QString::number(alphaSum(t))));
    // Greyscale of a premultiplied translucent colour keeps it premultiplied.
    Scene grey;
    p.image = ImageData::make(1, 1, {px(0, 128, 0, 128)});
    p.cornerRadii = {};
    p.opacity = 1;
    p.grayscale = true;
    p.bounds = Rect::fromXYWH(0, 0, 4, 4);
    grey.insert(p);
    const RasterTarget tg = render(grey, 4, 4);
    QVERIFY(near(tg.at(1, 1), px(92, 92, 92, 128), 1));
}

void TestAtramentRaster::pictureMalformed()
{
    Scene scene;
    Picture p;
    p.mask = everywhere();
    p.bounds = Rect::fromXYWH(0, 0, 8, 8);
    scene.insert(p); // no image
    auto broken = std::make_shared<ImageData>();
    broken->width = 4;
    broken->height = 4; // but no pixels
    p.image = broken;
    scene.insert(p);
    p.image = ImageData::make(2, 2, std::vector<Pixel>(4, px(255, 255, 255, 255)));
    p.source = Rect::fromXYWH(5, 5, 2, 2); // outside the image
    scene.insert(p);
    p.source = Rect::fromXYWH(0, 0, 2, 2);
    p.bounds = Rect::fromXYWH(0, 0, 0, 8); // no width
    scene.insert(p);
    p.bounds = Rect::fromXYWH(0, 0, kHuge, kHuge);
    p.cornerRadii = Corners::all(kHuge);
    p.opacity = 0; // invisible
    scene.insert(p);
    RasterStats stats;
    const RasterTarget t = render(scene, 8, 8, transparent(), nullptr, std::nullopt, &stats);
    QCOMPARE(stats.primitives, std::size_t(0));
    for (const Pixel &q : t.pixels)
        QCOMPARE(q, Pixel{});
}

void TestAtramentRaster::pathArea_data()
{
    QTest::addColumn<QString>("shape");
    QTest::addColumn<double>("area");
    QTest::addColumn<double>("tolerance");
    QTest::newRow("circle") << QStringLiteral("circle") << std::numbers::pi * 20 * 20 << 0.01;
    QTest::newRow("triangle") << QStringLiteral("triangle") << 3200.0 << 0.002;
    QTest::newRow("half-pixel square") << QStringLiteral("square") << 100.0 << 0.002;
    QTest::newRow("quadratic lens") << QStringLiteral("lens") << 2.0 / 3.0 * 40 * 20 << 0.01;
}

void TestAtramentRaster::pathArea()
{
    QFETCH(QString, shape);
    QFETCH(double, area);
    QFETCH(double, tolerance);
    PathBuilder b;
    if (shape == QLatin1String("circle")) {
        b.arcTo({50, 50}, 20, 0, 2 * std::numbers::pi_v<float>);
    } else if (shape == QLatin1String("triangle")) {
        b.moveTo({10, 10});
        b.lineTo({90, 10});
        b.lineTo({10, 90});
    } else if (shape == QLatin1String("square")) {
        b.moveTo({10.5f, 10.5f});
        b.lineTo({20.5f, 10.5f});
        b.lineTo({20.5f, 20.5f});
        b.lineTo({10.5f, 20.5f});
    } else {
        // Between y = 50 and a parabola through (30, 50), (50, 30), (70, 50): ⅔ of its box.
        b.moveTo({30, 50});
        b.quadTo({50, 10}, {70, 50});
    }
    Scene scene;
    scene.insert(std::move(b).build(rgb(0x000000), everywhere()));
    const RasterTarget t = render(scene, 100, 100);
    const double sum = alphaSum(t);
    QVERIFY2(std::abs(sum - area) <= area * tolerance, qPrintable(QStringLiteral("%1 vs %2").arg(sum).arg(area)));
    if (shape == QLatin1String("square")) {
        QCOMPARE(t.at(15, 15), px(0, 0, 0, 255));
        QVERIFY(near(t.at(10, 15), px(0, 0, 0, 128), 2));
        QVERIFY(near(t.at(10, 10), px(0, 0, 0, 64), 2)); // a quarter: coverage is area, not distance
        QCOMPARE(t.at(9, 15), Pixel{});
    }
}

void TestAtramentRaster::pathWinding()
{
    const auto square = [](PathBuilder &b, float l, float t, float r, float bottom, bool clockwise) {
        b.moveTo({l, t});
        if (clockwise) {
            b.lineTo({r, t});
            b.lineTo({r, bottom});
            b.lineTo({l, bottom});
        } else {
            b.lineTo({l, bottom});
            b.lineTo({r, bottom});
            b.lineTo({r, t});
        }
        b.close();
    };
    // Same direction twice: covered once where they overlap.
    PathBuilder same;
    square(same, 0, 0, 20, 20, true);
    square(same, 10, 10, 30, 30, true);
    // A hole of the opposite direction stays empty.
    PathBuilder hole;
    square(hole, 40, 0, 70, 30, true);
    square(hole, 50, 10, 60, 20, false);
    Scene scene;
    scene.insert(std::move(same).build(Color{0, 0, 1, 0.5f}, everywhere()));
    scene.insert(std::move(hole).build(black(), everywhere()));
    const RasterTarget t = render(scene, 72, 32);
    QCOMPARE(t.at(15, 15), t.at(5, 5));
    QCOMPARE(t.at(15, 15), blendOver(Pixel{}, PremulColor::from(Color{0, 0, 1, 0.5f}), 1));
    QCOMPARE(t.at(55, 15), Pixel{});
    QCOMPARE(t.at(45, 15), px(0, 0, 0, 255));
}

void TestAtramentRaster::pathMalformed()
{
    Scene scene;
    PathPrimitive nan;
    nan.mask = everywhere();
    nan.color = black();
    nan.points = {{0, 0}, {kNaN, 5}, {10, 10}};
    nan.bounds = Rect::fromXYWH(0, 0, 10, 10);
    scene.insert(nan);
    PathPrimitive two = nan;
    two.points = {{0, 0}, {10, 10}};
    scene.insert(two);
    PathPrimitive far = nan;
    far.points = {{-kHuge, 0}, {kHuge, 0}, {0, kHuge}};
    far.bounds = Rect::fromXYWH(-kHuge, 0, 2 * kHuge, kHuge);
    far.color = Color{1, 0, 0, 1};
    scene.insert(far);
    PathPrimitive ends = nan;
    ends.points = {{2, 2}, {8, 2}, {8, 8}, {2, 8}};
    ends.contourEnds = {9, 2}; // past the end, then backwards
    ends.bounds = Rect::fromXYWH(2, 2, 6, 6);
    ends.color = rgb(0x00ff00);
    scene.insert(ends);
    const RasterTarget t = render(scene, 12, 12);
    QCOMPARE(t.at(5, 5), px(0, 255, 0, 255));
    for (const Pixel &p : t.pixels)
        QVERIFY(p.r <= p.a && p.g <= p.a && p.b <= p.a);
}

void TestAtramentRaster::straightUnderline_data()
{
    QTest::addColumn<float>("thickness");
    QTest::addColumn<float>("bottom");
    QTest::addColumn<QList<int>>("alphaPerRow"); // rows 8..15
    QTest::newRow("1px") << 1.0f << 14.0f << QList<int>{0, 0, 0, 0, 0, 255, 0, 0};
    QTest::newRow("2px") << 2.0f << 14.0f << QList<int>{0, 0, 0, 0, 255, 255, 0, 0};
    QTest::newRow("1.5px") << 1.5f << 14.0f << QList<int>{0, 0, 0, 0, 128, 255, 0, 0};
    QTest::newRow("half-pixel bottom") << 1.0f << 14.5f << QList<int>{0, 0, 0, 0, 0, 128, 128, 0};
    QTest::newRow("thicker than the bounds") << 10.0f << 14.0f << QList<int>{0, 0, 255, 255, 255, 255, 0, 0};
}

void TestAtramentRaster::straightUnderline()
{
    QFETCH(float, thickness);
    QFETCH(float, bottom);
    QFETCH(QList<int>, alphaPerRow);
    Underline u;
    u.bounds = Rect::fromLTRB(4, 10, 40, bottom);
    u.mask = everywhere();
    u.color = black();
    u.thickness = thickness;
    Scene scene;
    scene.insert(u);
    const RasterTarget t = render(scene, 44, 20);
    for (int i = 0; i < alphaPerRow.size(); ++i) {
        for (int x = 4; x < 40; ++x)
            QVERIFY2(std::abs(int(t.at(x, 8 + i).a) - alphaPerRow[i]) <= 2,
                     qPrintable(QStringLiteral("row %1").arg(8 + i)));
        QCOMPARE(t.at(3, 8 + i), Pixel{});
        QCOMPARE(t.at(40, 8 + i), Pixel{});
    }
}

void TestAtramentRaster::wavyUnderline()
{
    Underline u;
    u.bounds = Rect::fromXYWH(10, 10, 60, 5);
    u.mask = everywhere();
    u.color = rgb(0xe63946);
    u.thickness = 1;
    u.wavy = true;
    Scene scene;
    scene.insert(u);
    const RasterTarget t = render(scene, 80, 24);
    double lowest = 100;
    double highest = -100;
    for (int x = 0; x < 80; ++x) {
        double ink = 0;
        double weighted = 0;
        for (int y = 0; y < 24; ++y) {
            const Pixel p = t.at(x, y);
            if (y < 10 || y >= 15 || x < 10 || x >= 70) {
                QCOMPARE(p, Pixel{}); // all ink within the bounds
                continue;
            }
            ink += p.a;
            weighted += p.a * (y + 0.5);
        }
        if (x < 10 || x >= 70)
            continue;
        QVERIFY2(ink >= 200, qPrintable(QString::number(x))); // no gaps
        const double centre = weighted / ink;
        lowest = std::min(lowest, centre);
        highest = std::max(highest, centre);
        // Period 4 × thickness.
        if (x + 4 < 70)
            for (int y = 10; y < 15; ++y)
                QVERIFY(std::abs(int(t.at(x, y).a) - int(t.at(x + 4, y).a)) <= 2);
    }
    // It waves: the stroke's centre moves by most of the free height (5 − 1 = 4; four samples a
    // period see ±0.7 of the amplitude).
    QVERIFY2(highest - lowest > 2, qPrintable(QStringLiteral("%1..%2").arg(lowest).arg(highest)));
}

void TestAtramentRaster::maskClipping_data()
{
    QTest::addColumn<Rect>("mask");
    QTest::addColumn<QRect>("drawn"); // pixels expected solid; all others untouched
    QTest::newRow("whole") << Rect::fromXYWH(0, 0, 100, 100) << QRect(10, 10, 20, 20);
    QTest::newRow("half") << Rect::fromXYWH(0, 0, 20, 100) << QRect(10, 10, 10, 20);
    QTest::newRow("snapped outwards") << Rect::fromLTRB(12.3f, 14.9f, 17.2f, 15.1f) << QRect(12, 14, 6, 2);
    QTest::newRow("zero width") << Rect::fromXYWH(15, 0, 0, 100) << QRect();
    QTest::newRow("zero height") << Rect::fromXYWH(0, 15, 100, 0) << QRect();
    QTest::newRow("negative") << Rect::fromXYWH(20, 20, -5, -5) << QRect();
    QTest::newRow("apart") << Rect::fromXYWH(50, 50, 10, 10) << QRect();
    QTest::newRow("NaN") << Rect::fromXYWH(kNaN, 0, 100, 100) << QRect();
}

void TestAtramentRaster::maskClipping()
{
    QFETCH(Rect, mask);
    QFETCH(QRect, drawn);
    const Color c = rgb(0xffffff);
    Scene scene;
    Quad q = quad(Rect::fromXYWH(10, 10, 20, 20), c);
    q.mask = {mask};
    scene.insert(q);
    Shadow s = shadow(Rect::fromXYWH(10, 10, 20, 20), Color{0, 0, 0, 0}, 4);
    s.mask = {mask};
    scene.insert(s); // transparent: never drawn
    RasterStats stats;
    const RasterTarget t = render(scene, 40, 40, transparent(), nullptr, std::nullopt, &stats);
    QCOMPARE(stats.primitives, std::size_t(drawn.isEmpty() ? 0 : 1));
    for (int y = 0; y < 40; ++y)
        for (int x = 0; x < 40; ++x)
            QCOMPARE(t.at(x, y), drawn.contains(x, y) ? px(255, 255, 255, 255) : Pixel{});
}

void TestAtramentRaster::damageClipping()
{
    Scene scene;
    scene.insert(shadow(Rect::fromXYWH(10, 10, 40, 30), black(), 12, Corners::all(6)));
    scene.insert(quad(Rect::fromXYWH(10, 10, 40, 30), rgb(0xf1faee), Corners::all(6), Edges::all(1), rgb(0x1d3557)));
    scene.insert(quad(Rect::fromXYWH(55, 5, 10, 10), rgb(0xe63946)));
    const RasterTarget full = render(scene, 70, 50, rgb(0xa8dadc));
    // Over a frame of another colour, only the damaged pixels (snapped outwards) change, and they
    // are the same as in the full frame.
    const Rect damage = Rect::fromLTRB(20.5f, 0.2f, 48.7f, 25);
    RasterTarget part;
    part.resize(70, 50);
    part.fill(px(1, 2, 3, 4));
    RasterRenderer renderer;
    static const Atlas atlas;
    const RasterStats stats = renderer.render(scene, atlas, part, rgb(0xa8dadc), damage);
    m_last = part;
    QCOMPARE(stats.primitives, std::size_t(2)); // the small quad is outside the damage
    for (int y = 0; y < 50; ++y) {
        for (int x = 0; x < 70; ++x) {
            const bool damaged = x >= 20 && x < 49 && y >= 0 && y < 25;
            QCOMPARE(part.at(x, y), damaged ? full.at(x, y) : px(1, 2, 3, 4));
        }
    }
    // A damage of no area, or outside the target, changes nothing.
    RasterTarget untouched = part;
    renderer.render(scene, atlas, untouched, white(), Rect::fromXYWH(10, 10, 0, 5));
    renderer.render(scene, atlas, untouched, white(), Rect::fromXYWH(100, 100, 10, 10));
    QVERIFY(untouched.pixels == part.pixels);
}

void TestAtramentRaster::primitivesOutsideOrMalformed()
{
    Scene scene;
    scene.insert(quad(Rect::fromXYWH(-100, -100, 50, 50), white()));
    scene.insert(quad(Rect::fromXYWH(200, 10, 50, 50), white()));
    scene.insert(quad(Rect::fromXYWH(10, 10, -5, 5), white()));
    scene.insert(quad(Rect::fromXYWH(10, 10, 5, 0), white()));
    scene.insert(quad(Rect::fromXYWH(kNaN, 10, 5, 5), white()));
    scene.insert(quad(Rect::fromXYWH(10, kNaN, 5, 5), white()));
    scene.insert(quad(Rect::fromXYWH(kHuge, kHuge, kHuge, kHuge), white()));
    scene.insert(quad(Rect::fromXYWH(10, 10, 5, 5), transparent()));
    scene.insert(quad(Rect::fromXYWH(10, 10, 5, 5), transparent(), {}, Edges::all(1), transparent()));
    scene.insert(quad(Rect::fromXYWH(10, 10, 5, 5), Color{kNaN, kNaN, kNaN, kNaN}));
    Quad gradient = quad(Rect::fromXYWH(-50, 0, 20, 20), white());
    gradient.background = Background::linear(kNaN, white(), black());
    scene.insert(gradient);
    Underline u;
    u.mask = everywhere();
    u.color = black();
    u.bounds = Rect::fromXYWH(0, 0, 10, 3);
    u.thickness = 0;
    scene.insert(u);
    u.thickness = kNaN;
    u.wavy = true;
    scene.insert(u);
    RasterStats stats;
    const RasterTarget t = render(scene, 64, 64, transparent(), nullptr, std::nullopt, &stats);
    QCOMPARE(stats.primitives, std::size_t(0));
    QCOMPARE(stats.pixelsTouched, std::size_t(0));
    for (const Pixel &p : t.pixels)
        QCOMPARE(p, Pixel{});
    // A NaN gradient angle and inf/NaN borders inside the target: drawn, nothing NaN-born.
    Scene odd;
    gradient.bounds = Rect::fromXYWH(0, 0, 20, 20);
    odd.insert(gradient);
    odd.insert(quad(Rect::fromXYWH(20, 0, 20, 20), white(), Corners::all(kInf), Edges{kInf, kNaN, -1, 2}, black()));
    const RasterTarget to = render(odd, 40, 20);
    QCOMPARE(to.at(10, 10), px(255, 255, 255, 255)); // the start colour
    QCOMPARE(to.at(30, 10), px(0, 0, 0, 255));       // all border
}

void TestAtramentRaster::hugeQuadStaysExact()
{
    // A quad (and a shadow) far larger than the target: every pixel is exact, as if it were small.
    Scene scene;
    scene.insert(shadow(Rect::fromXYWH(-1e7f, -1e7f, 2e7f, 2e7f), Color{0, 0, 0, 1}, 10, Corners::all(1e6f)));
    scene.insert(quad(Rect::fromXYWH(-1e7f, -3e7f, 4e7f, 3e7f + 20), rgb(0x457b9d), Corners::all(5e6f), Edges::all(2),
                      rgb(0xe63946)));
    const RasterTarget t = render(scene, 50, 40);
    for (int x = 0; x < 50; ++x) {
        for (int y = 0; y < 18; ++y)
            QCOMPARE(t.at(x, y), opaquePixel(rgb(0x457b9d)));
        QCOMPARE(t.at(x, 18), opaquePixel(rgb(0xe63946)));
        QCOMPARE(t.at(x, 19), opaquePixel(rgb(0xe63946)));
        QCOMPARE(t.at(x, 20).a, std::uint8_t(255)); // the shadow, deep inside: alpha 1 exactly
    }
}

void TestAtramentRaster::occlusionChangesNothing()
{
    // What an opaque quad paints over later is skipped. The same frame with those quads a hair
    // below opaque (still alpha 255 once in bytes, but hiding nothing) draws everything: the
    // pixels must agree (within a level at the covers' anti-aliased edges).
    Atlas atlas(128);
    const auto glyph = atlas.getOrInsert(9, AtlasKind::Alpha, [] {
        std::vector<std::uint8_t> v(9 * 12);
        for (std::size_t i = 0; i < v.size(); ++i)
            v[i] = std::uint8_t((i * 53) % 256);
        return std::optional(alphaBitmap(9, 12, v));
    });
    QVERIFY(glyph);
    const auto build = [&](float coverAlpha) {
        Scene scene;
        const auto cover = [&](Quad q) {
            q.background.color.a = coverAlpha;
            q.background.color2.a = coverAlpha;
            scene.insert(q);
        };
        scene.insert(quad(Rect::fromXYWH(0, 0, 200, 150), rgb(0x3a86ff)));
        scene.insert(shadow(Rect::fromXYWH(30, 20, 120, 90), black(), 16, Corners::all(10)));
        for (int i = 0; i < 40; ++i) {
            scene.insert(quad(Rect::fromXYWH(float(5 + (i * 37) % 170), float(3 + (i * 23) % 130), 24.5f, 13),
                              Color{1, 0.4f, 0.1f, 0.6f}, Corners::all(3), Edges::all(1), rgb(0xffbe0b)));
            MonochromeSprite s;
            s.bounds = Rect::fromXYWH(float((i * 29) % 190), float((i * 17) % 140), 9, 12);
            s.mask = everywhere();
            s.color = rgb(0xfb5607);
            s.tile = *glyph;
            scene.insert(s);
        }
        PathBuilder b;
        b.arcTo({100, 75}, 50, 0, 6);
        scene.insert(std::move(b).build(Color{0.5f, 1, 0.5f, 0.5f}, everywhere()));
        // The covers: a plain one at fractional bounds, rounded with a border, a gradient of one
        // colour, one cut by its mask, one exactly on tile lines.
        cover(quad(Rect::fromXYWH(20.3f, 17.6f, 71.2f, 45.9f), rgb(0x8338ec)));
        cover(quad(Rect::fromXYWH(90, 60, 80, 70), rgb(0x06d6a0), Corners{14, 3, 20, 0}, Edges{2, 5, 1, 0},
                   Color{0, 0, 0, 0.5f}));
        Quad g = quad(Rect::fromXYWH(5, 90, 60, 50), white());
        g.background = Background::linear(30, rgb(0xef476f), rgb(0xef476f));
        cover(g);
        Quad masked = quad(Rect::fromXYWH(-50, -50, 400, 400), rgb(0x073b4c));
        masked.mask = {Rect::fromXYWH(130.5f, 3.2f, 50, 40)};
        cover(masked);
        cover(quad(Rect::fromXYWH(160, 96, 32, 48), rgb(0x118ab2)));
        // Drawn after the covers, so over them.
        for (int i = 0; i < 10; ++i)
            scene.insert(quad(Rect::fromXYWH(float(10 + i * 18), 70, 12, 12), Color{1, 1, 1, 0.5f}, Corners::all(6)));
        return scene;
    };
    const Scene covered = build(1);
    const Scene drawnAll = build(0.99999994f);
    RasterStats coveredStats;
    RasterStats allStats;
    const RasterTarget a = render(covered, 200, 150, white(), &atlas, std::nullopt, &coveredStats);
    const RasterTarget b = render(drawnAll, 200, 150, white(), &atlas, std::nullopt, &allStats);
    QVERIFY(coveredStats.primitives < allStats.primitives); // it did skip something
    QVERIFY(coveredStats.pixelsTouched < allStats.pixelsTouched);
    for (int y = 0; y < 150; ++y)
        for (int x = 0; x < 200; ++x)
            QVERIFY2(near(a.at(x, y), b.at(x, y), 1), QByteArray(QByteArray::number(x) + ',' + QByteArray::number(y)
                                                                 + ' ' + str(a.at(x, y)) + " vs " + str(b.at(x, y)))
                                                              .constData());
    // With a damage rectangle too.
    const Rect damage = Rect::fromLTRB(37.5f, 41, 151, 133.2f);
    const RasterTarget ad = render(covered, 200, 150, white(), &atlas, damage);
    const RasterTarget bd = render(drawnAll, 200, 150, white(), &atlas, damage);
    for (int y = 41; y < 134; ++y)
        for (int x = 37; x < 151; ++x)
            QVERIFY(near(ad.at(x, y), bd.at(x, y), 1));
}

void TestAtramentRaster::replayedRowsMatchComputed()
{
    // Rows of a quad's straight middle are replayed from the first one, the lower rows of a
    // vertically symmetric quad or shadow from the upper ones. A damage rectangle over the lower
    // half alone leaves nothing to replay from: those rows are computed, and must be the same.
    Scene scene;
    scene.insert(quad(Rect::fromXYWH(4, 4, 50, 36), Color{0.9f, 0.3f, 0.2f, 0.7f}, Corners{9, 4, 4, 9},
                      Edges{3, 1, 3, 2}, Color{0.1f, 0.1f, 0.5f, 0.8f}));
    scene.insert(quad(Rect::fromXYWH(60.5f, 4, 40, 31), rgb(0x2a9d8f), Corners::all(15.5f), Edges::all(1.5f),
                      rgb(0xe9c46a)));
    scene.insert(shadow(Rect::fromXYWH(20, 50, 70, 30), Color{0, 0, 0, 0.6f}, 14, Corners{12, 5, 5, 12}));
    scene.insert(shadow(Rect::fromXYWH(5, 85, 40, 10), Color{0, 0, 0, 0.6f}, 6, Corners{2, 5, 0, 1})); // not symmetric
    scene.insert(shadow(Rect::fromXYWH(100.5f, 50, 40, 31), Color{0.2f, 0, 0.4f, 0.8f}, 9, Corners::all(8)));
    scene.insert(quad(Rect::fromXYWH(110, 4, 30, 40), rgb(0xf4a261), Corners::all(6)));
    const RasterTarget whole = render(scene, 150, 100, white());
    for (const Rect &damage :
         {Rect::fromLTRB(0, 22, 150, 100), Rect::fromLTRB(0, 66, 150, 100), Rect::fromLTRB(0, 0, 150, 30)}) {
        const RasterTarget part = render(scene, 150, 100, white(), nullptr, damage);
        for (int y = int(damage.top()); y < int(damage.bottom()); ++y)
            for (int x = 0; x < 150; ++x)
                QCOMPARE(part.at(x, y), whole.at(x, y));
    }
    // And the mirrored halves of a quad alone are the same, row for row.
    Scene alone;
    alone.insert(scene.primitives().front());
    const RasterTarget single = render(alone, 60, 44, white());
    for (int y = 0; y < 18; ++y)
        for (int x = 0; x < 60; ++x)
            QCOMPARE(single.at(x, 4 + y), single.at(x, 39 - y));
}

void TestAtramentRaster::deterministic()
{
    Atlas atlas(128);
    const auto glyph = atlas.getOrInsert(5, AtlasKind::Alpha, [] {
        std::vector<std::uint8_t> v(7 * 9);
        for (std::size_t i = 0; i < v.size(); ++i)
            v[i] = std::uint8_t((i * 37) % 256);
        return std::optional(alphaBitmap(7, 9, v));
    });
    QVERIFY(glyph);
    Scene scene;
    scene.insert(shadow(Rect::fromXYWH(20.3f, 15.7f, 60, 40), Color{0, 0, 0, 0.3f}, 9, Corners{4, 9, 0, 2}));
    Quad g = quad(Rect::fromXYWH(20.3f, 15.7f, 60, 40), white(), Corners{4, 9, 0, 2}, Edges{1, 2, 3, 0.5f},
                  Color{0.2f, 0.3f, 0.9f, 0.7f});
    g.background = Background::linear(33, Color{1, 0.8f, 0, 0.9f}, Color{0.1f, 0.6f, 0.3f, 1});
    scene.insert(g);
    for (int i = 0; i < 5; ++i) {
        MonochromeSprite s;
        s.bounds = Rect::fromXYWH(25.0f + 8 * i, 30, 7, 9);
        s.mask = {Rect::fromXYWH(22, 20, 40.5f, 30)};
        s.color = Color{0.1f, 0.1f, 0.1f, 0.85f};
        s.tile = *glyph;
        scene.insert(s);
    }
    PathBuilder b;
    b.arcTo({70, 60}, 13.3f, 0.3f, 5.1f);
    scene.insert(std::move(b).build(Color{0.8f, 0.1f, 0.4f, 0.6f}, everywhere()));
    Underline u;
    u.bounds = Rect::fromXYWH(25, 42, 40, 3.5f);
    u.mask = everywhere();
    u.color = rgb(0xd62828);
    u.wavy = true;
    u.thickness = 1.25f;
    scene.insert(u);
    RasterStats s1;
    RasterStats s2;
    const RasterTarget a = render(scene, 100, 80, rgb(0xfafafa), &atlas, std::nullopt, &s1);
    const RasterTarget b2 = render(scene, 100, 80, rgb(0xfafafa), &atlas, std::nullopt, &s2);
    QVERIFY(a.pixels == b2.pixels);
    QCOMPARE(s1.primitives, s2.primitives);
    QCOMPARE(s1.pixelsTouched, s2.pixelsTouched);
    QCOMPARE(s1.primitives, scene.size());
}

void TestAtramentRaster::uiFrameTime()
{
    // A synthetic frame of a busy desktop window: a sidebar and panes, cards and a popup with
    // shadows, ≈2 000 small rounded quads with borders, ≈5 000 glyphs from a filled atlas.
    constexpr int kWidth = 1280;
    constexpr int kHeight = 800;
    std::uint32_t seed = 0x9e3779b9u;
    const auto next = [&seed](int n) {
        seed = seed * 1664525u + 1013904223u;
        return int((seed >> 8) % std::uint32_t(n));
    };
    Atlas atlas(1024);
    std::vector<AtlasTile> glyphs;
    for (int g = 0; g < 160; ++g) {
        // A glyph-like bitmap: an anti-aliased ring or bar drawn by the coverage rasterizer.
        const int w = 5 + next(5);
        const int h = 8 + next(6);
        const auto tile = atlas.getOrInsert(std::uint64_t(1000 + g), AtlasKind::Alpha, [&] {
            CoverageRasterizer r;
            r.reset(w, h);
            const float inset = 0.3f + float(next(10)) / 20;
            r.moveTo({inset, inset});
            r.lineTo({float(w) - inset, inset * 2});
            r.lineTo({float(w) - inset, float(h) - inset});
            r.lineTo({inset * 3, float(h) - inset});
            r.close();
            r.moveTo({float(w) / 2 - 1.2f, float(h) / 3});
            r.lineTo({float(w) / 2 - 1.2f, float(h) * 2 / 3});
            r.lineTo({float(w) / 2 + 1.2f, float(h) * 2 / 3});
            r.lineTo({float(w) / 2 + 1.2f, float(h) / 3});
            r.close();
            AtlasBitmap bitmap;
            bitmap.width = w;
            bitmap.height = h;
            bitmap.pixels.resize(std::size_t(w * h));
            r.accumulate(bitmap.pixels.data(), w);
            return std::optional(bitmap);
        });
        QVERIFY(tile);
        glyphs.push_back(*tile);
    }
    const ContentMask window{Rect::fromXYWH(0, 0, kWidth, kHeight)};
    const auto color = [&](float alpha) {
        return Color{float(next(256)) / 255, float(next(256)) / 255, float(next(256)) / 255, alpha};
    };
    Scene scene;
    const auto add = [&](Quad q) {
        q.mask = window;
        scene.insert(q);
    };
    add(quad(Rect::fromXYWH(0, 0, kWidth, kHeight), rgb(0xf7f5f0)));
    add(quad(Rect::fromXYWH(0, 0, 260, kHeight), rgb(0xebe7df), {}, Edges{0, 1, 0, 0}, rgb(0xd8d2c6)));
    add(quad(Rect::fromXYWH(260, 0, kWidth - 260, 44), rgb(0xfbfaf7), {}, Edges{0, 0, 1, 0}, rgb(0xd8d2c6)));
    for (int i = 0; i < 4; ++i) {
        const Rect card = Rect::fromXYWH(290.0f + float(i) * 240, 70, 220, 140);
        Shadow s = shadow(card.translated({0, 2}), Color{0, 0, 0, 0.12f}, 12, Corners::all(10));
        s.mask = window;
        scene.insert(s);
        add(quad(card, white(), Corners::all(10), Edges::all(1), rgb(0xe2ddd3)));
    }
    std::size_t quads = 0;
    while (quads < 2000) {
        const float w = float(12 + next(70));
        const float h = float(12 + next(18));
        const float x = float(next(kWidth - int(w)));
        const float y = float(next(kHeight - int(h)));
        const bool translucent = next(10) < 3;
        Quad q = quad(Rect::fromXYWH(x, y, w, h), color(translucent ? 0.25f : 1),
                      Corners::all(float(next(int(h / 2) + 1))), next(2) ? Edges::all(1) : Edges{}, color(1));
        if (next(20) == 0)
            q.background = Background::linear(float(next(360)), color(1), color(1));
        add(q);
        ++quads;
    }
    std::size_t sprites = 0;
    for (int line = 0; sprites < 5000; ++line) {
        const float y = float(56 + (line % 40) * 18);
        float x = 280.0f + float((line / 40) * 7);
        while (x < float(kWidth - 20) && sprites < 5000) {
            const AtlasTile &tile = glyphs[std::size_t(next(int(glyphs.size())))];
            MonochromeSprite s;
            s.bounds = Rect::fromXYWH(x, y + float(next(3)), float(tile.width), float(tile.height));
            s.mask = window;
            s.color = next(10) == 0 ? Color{0.2f, 0.3f, 0.8f, 0.8f} : rgb(0x2b2a28);
            s.tile = tile;
            scene.insert(s);
            ++sprites;
            x += float(tile.width + 1 + (next(7) == 0 ? 5 : 0));
        }
    }
    const Rect popup = Rect::fromXYWH(700, 300, 420, 280);
    Shadow ps = shadow(popup.translated({0, 8}), Color{0, 0, 0, 0.25f}, 32, Corners::all(12));
    ps.mask = window;
    scene.insert(ps);
    add(quad(popup, white(), Corners::all(12), Edges::all(1), rgb(0xd8d2c6)));

    RasterTarget target;
    target.resize(kWidth, kHeight);
    RasterRenderer renderer;
    const RasterStats stats = renderer.render(scene, atlas, target, white());
    m_last = target;
    // The fastest of several frames (a stall only lengthens one), and the thread's CPU time over a
    // batch (a stalled runner does not count; Windows counts it in 15.6 ms ticks, hence the batch).
    const double fastestMs =
            double(brulion::test::fastestNs(15, [&] { renderer.render(scene, atlas, target, white()); })) / 1e6;
    constexpr int kBatch = 30;
    brulion::test::CpuTimer cpu;
    cpu.start();
    for (int i = 0; i < kBatch; ++i)
        renderer.render(scene, atlas, target, white());
    const double cpuMs = double(cpu.nsecsElapsed()) / 1e6 / kBatch;
    qDebug("UI frame %dx%d: %zu primitives (%zu drawn: %zu quads, %zu glyphs, 5 shadows), %zu pixels evaluated: "
           "%.3f ms (fastest of 15), %.3f ms of CPU (mean of %d)",
           kWidth, kHeight, scene.size(), stats.primitives, quads, sprites, stats.pixelsTouched, fastestMs, cpuMs,
           kBatch);
    // What lies under the popup and the cards is not drawn at all (occlusion).
    QVERIFY(stats.primitives > scene.size() / 2 && stats.primitives < scene.size());
#if defined(__OPTIMIZE__) || (defined(_MSC_VER) && defined(NDEBUG))
    // docs/ATRAMENT.md M0's gate is < 4 ms a frame at -O2 on a desktop core (≈ 4 ms measured on the
    // development machine, see raster.cpp). The limit here leaves a CI runner's room (a slower
    // core, a sibling thread) and still catches a fast path gone (a Debug build takes ~25 ms).
    QVERIFY2(cpuMs < 12.0 * BRULION_TEST_SLOWDOWN, qPrintable(QStringLiteral("%1 ms").arg(cpuMs)));
#endif
}

QTEST_GUILESS_MAIN(TestAtramentRaster)
#include "tst_atrament_raster.moc"
