// The GPU renderer (src/atrament/qt/qrhirenderer.h) against the CPU renderer, its reference: the
// same scenes drawn by both, every pixel within 2 per channel (docs/ATRAMENT.md "Testing"). Each
// QRhi backend that can be created headless here runs every test (Vulkan from the loader, under
// any platform plugin; OpenGL where the platform plugin has it); none: the tests skip.
//
// Also: atlas pages updated across frames (dirty rectangles, new pages, pages emptied and filled
// again, two renderers on one atlas), the path tile cache, batching, a window's scene at several
// scale factors, a busy synthetic frame with its timings, and the Qt host's CPU fallback.

#include "base/coverage.h"
#include "gpuframe.h"
#include "offscreenrhi.h"
#include "qrhirenderer.h"
#include "qtfixtures.h"
#include "qthost.h"
#include "raster/raster.h"
#include "text/fonts.h"
#include "ui/atrament.h"
#include "ui/testplatform.h"

#include <QDir>
#include <QImage>
#include <QQuickWindow>
#include <QRegularExpression>
#include <QSGRendererInterface>
#include <QtTest>
#include <rhi/qrhi.h>

#include <chrono>
#include <cmath>
#include <limits>
#include <map>
#include <numbers>

using namespace brulion::atrament;
using namespace qtfixtures;

Q_DECLARE_METATYPE(brulion::atrament::Rect)

namespace {

const float kNaN = std::numeric_limits<float>::quiet_NaN();

ContentMask everywhere()
{
    return {Rect::fromXYWH(-1e6f, -1e6f, 2e6f, 2e6f)};
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

// A glyph-like A8 bitmap: an anti-aliased quadrilateral with a bar cut out.
AtlasBitmap glyphBitmap(int w, int h, float inset)
{
    CoverageRasterizer r;
    r.reset(w, h);
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
    return bitmap;
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

QImage toImage(const std::vector<Pixel> &pixels, int w, int h)
{
    QImage image(w, h, QImage::Format_RGBA8888_Premultiplied);
    for (int y = 0; y < h; ++y)
        std::memcpy(image.scanLine(y), pixels.data() + std::size_t(y) * std::size_t(w), std::size_t(w) * 4);
    return image;
}

// How close the GPU's pixels must be to the CPU's: every channel within `tolerance`, but for
// `allowed` pixels, which may be off by up to `limit`.
struct Tolerance {
    int tolerance = 2;
    std::size_t allowed = 0;
    int limit = 2;
};

} // namespace

class TestAtramentQt : public QObject {
    Q_OBJECT

private Q_SLOTS:
    void initTestCase();
    void cleanupTestCase();
    void init();

    void quads();
    void borders();
    void gradients();
    void shadows();
    void monochromeSprites();
    void colorSprites();
    void pictures();
    void paths();
    void underlines();
    void masks_data();
    void masks();
    void opaqueBackgroundAndOrder();
    void scaleFactors_data();
    void scaleFactors();
    void atlasUpdatesAcrossFrames();
    void atlasSharedByTwoRenderers();
    void pathTilesCached();
    void batching();
    void opacity();
    void busyFrame();
    void cpuFallback();

private:
    // Both renderers draw `scene`, compared.
    void compare(const Scene &scene, Atlas &atlas, int w, int h, Color clear, Tolerance tolerance = {});
    // One GPU frame on `backend`, compared with the CPU's; `renderer` and `builder` persist.
    void compareFrame(OffscreenRhi &backend, QRhiRenderer &renderer, GpuFrameBuilder &builder, const Scene &scene,
                      Atlas &atlas, const RasterTarget &cpu, Color clear, Tolerance tolerance = {});
    void check(const std::vector<Pixel> &gpu, const RasterTarget &cpu, const QString &backend, Tolerance tolerance);

    std::vector<std::unique_ptr<OffscreenRhi>> m_backends;
    std::map<QString, int> m_maxDifference; // per test and backend, for the summary
};

void TestAtramentQt::initTestCase()
{
    if (!atramentGpuShadersBuilt()) {
        qInfo("built without Qt Shader Tools: no GPU renderer to compare");
        return;
    }
    QString why;
    for (const auto backend : {OffscreenRhi::Backend::Vulkan, OffscreenRhi::Backend::OpenGL}) {
        if (auto rhi = OffscreenRhi::create(backend, &why)) {
            // A context the renderer cannot draw on (an OpenGL compatibility context of the
            // offscreen platform on a desktop, say) is the CPU fallback's case, tested below.
            if (!QRhiRenderer::supports(rhi->rhi())) {
                qInfo("QRhi backend without what the renderer needs: %s", qPrintable(rhi->description()));
                continue;
            }
            qInfo("QRhi backend: %s", qPrintable(rhi->description()));
            m_backends.push_back(std::move(rhi));
        } else {
            qInfo("no %s here: %s", backend == OffscreenRhi::Backend::Vulkan ? "Vulkan" : "OpenGL", qPrintable(why));
        }
    }
}

void TestAtramentQt::cleanupTestCase()
{
    for (const auto &[name, difference] : m_maxDifference)
        qInfo("largest difference from the CPU renderer: %s %d", qPrintable(name), difference);
    m_backends.clear();
}

void TestAtramentQt::init()
{
    const QByteArray name = QTest::currentTestFunction();
    if (m_backends.empty() && name != "cpuFallback")
        QSKIP("no QRhi backend can be created here");
}

void TestAtramentQt::check(const std::vector<Pixel> &gpu, const RasterTarget &cpu, const QString &backend,
                           Tolerance tolerance)
{
    QCOMPARE(gpu.size(), cpu.pixels.size());
    int largest = 0;
    std::size_t over = 0;
    int firstX = -1;
    int firstY = -1;
    for (std::size_t i = 0; i < gpu.size(); ++i) {
        const Pixel a = gpu[i];
        const Pixel b = cpu.pixels[i];
        const int d = std::max(std::max(std::abs(a.r - b.r), std::abs(a.g - b.g)),
                               std::max(std::abs(a.b - b.b), std::abs(a.a - b.a)));
        largest = std::max(largest, d);
        if (d > tolerance.tolerance && over++ == 0) {
            firstX = int(i % std::size_t(cpu.width));
            firstY = int(i / std::size_t(cpu.width));
        }
    }
    QString key = QString::fromLatin1(QTest::currentTestFunction());
    if (QTest::currentDataTag())
        key += QLatin1Char('/') + QString::fromLatin1(QTest::currentDataTag());
    key += QLatin1String(" on ") + backend;
    m_maxDifference[key] = std::max(m_maxDifference[key], largest);
    if (over > tolerance.allowed || largest > std::max(tolerance.tolerance, tolerance.limit)) {
        QString name = key;
        name.replace(QRegularExpression(QStringLiteral("[^A-Za-z0-9.-]+")), QStringLiteral("_"));
        const QString base = QDir::temp().filePath(QStringLiteral("atrament-qt-%1").arg(name));
        toImage(gpu, cpu.width, cpu.height).save(base + QStringLiteral("-gpu.png"));
        toImage(cpu.pixels, cpu.width, cpu.height).save(base + QStringLiteral("-cpu.png"));
        const std::size_t at = std::size_t(firstY) * std::size_t(cpu.width) + std::size_t(firstX);
        const Pixel g = gpu[at];
        const Pixel c = cpu.pixels[at];
        QFAIL(qPrintable(
                QStringLiteral("%1 pixels differ by more than %2 (largest %3), first at %4,%5: GPU %6,%7,%8,%9 "
                                        "CPU %10,%11,%12,%13; images: %14-{gpu,cpu}.png")
                                       .arg(over)
                                       .arg(tolerance.tolerance)
                                       .arg(largest)
                                       .arg(firstX)
                                       .arg(firstY)
                                       .arg(g.r)
                                       .arg(g.g)
                                       .arg(g.b)
                                       .arg(g.a)
                                       .arg(c.r)
                                       .arg(c.g)
                                       .arg(c.b)
                                       .arg(c.a)
                                       .arg(base)));
    }
}

void TestAtramentQt::compareFrame(OffscreenRhi &backend, QRhiRenderer &renderer, GpuFrameBuilder &builder,
                                  const Scene &scene, Atlas &atlas, const RasterTarget &cpu, Color clear,
                                  Tolerance tolerance)
{
    renderer.setFrame(std::make_shared<const GpuFrame>(builder.build(scene, atlas, cpu.width, cpu.height)));
    renderer.syncAtlas(QRhiRenderer::AtlasSlot::Atlas, atlas);
    renderer.syncAtlas(QRhiRenderer::AtlasSlot::Paths, builder.pathAtlas());
    const std::optional<std::vector<Pixel>> gpu = backend.render(renderer, clear);
    QVERIFY2(gpu, qPrintable(QStringLiteral("the GPU renderer cannot draw on ") + backend.description()));
    check(*gpu, cpu, backend.description(), tolerance);
}

void TestAtramentQt::compare(const Scene &scene, Atlas &atlas, int w, int h, Color clear, Tolerance tolerance)
{
    RasterTarget cpu;
    cpu.resize(w, h);
    RasterRenderer().render(scene, atlas, cpu, clear);
    for (const auto &backend : m_backends) {
        GpuFrameBuilder builder;
        QRhiRenderer renderer;
        compareFrame(*backend, renderer, builder, scene, atlas, cpu, clear, tolerance);
        if (QTest::currentTestFailed())
            return;
    }
}

void TestAtramentQt::quads()
{
    Atlas atlas;
    for (const Color clear : {transparent(), rgb(0x202124)}) {
        Scene scene;
        scene.insert(quad(Rect::fromXYWH(10.3f, 12.7f, 50.5f, 30.25f), rgb(0x3366cc)));
        scene.insert(quad(Rect::fromXYWH(30, 20, 60, 40), Color{1, 0.5f, 0, 0.4f}));
        scene.insert(quad(Rect::fromXYWH(100, 10, 60, 40), rgb(0x22aa55), Corners::all(8)));
        scene.insert(quad(Rect::fromXYWH(170.5f, 10.25f, 61, 41), rgb(0xaa2255), Corners{0, 4, 12, 20}));
        scene.insert(quad(Rect::fromXYWH(10, 70, 80, 24), rgb(0xeeeeee), Corners::all(1e6f))); // a pill
        scene.insert(quad(Rect::fromXYWH(100.4f, 70.6f, 0.6f, 0.6f), rgb(0xffffff)));
        scene.insert(quad(Rect::fromXYWH(110, 70, 30, 30), Color{0.2f, 0.9f, 0.8f, 0.7f}, Corners::all(15)));
        scene.insert(quad(Rect::fromXYWH(150.5f, 60.5f, 40, 40), Color{2, -1, 0.5f, 1.5f})); // components clamped
        // Huge, far beyond the target: its edges still exact where they cross it.
        scene.insert(
                quad(Rect::fromLTRB(-1e7f, 105.5f, 1e7f, 115.25f), Color{0.9f, 0.9f, 0.2f, 0.8f}, Corners::all(4)));
        // Malformed: nothing.
        scene.insert(quad(Rect::fromXYWH(kNaN, 10, 20, 20), rgb(0xff0000)));
        scene.insert(quad(Rect::fromXYWH(10, 10, -5, 20), rgb(0xff0000)));
        scene.insert(quad(Rect::fromXYWH(200, 70, 30, 30), rgb(0x8040ff), Corners{kNaN, -4, 8, 1e30f}));
        compare(scene, atlas, 240, 120, clear);
        if (QTest::currentTestFailed())
            return;
    }
}

void TestAtramentQt::borders()
{
    Atlas atlas;
    Scene scene;
    scene.insert(
            quad(Rect::fromXYWH(10, 10, 60, 40), rgb(0xffffff), Corners::all(10), Edges{1, 2, 3, 4}, rgb(0x334455)));
    scene.insert(quad(Rect::fromXYWH(80.5f, 10.5f, 60, 40), rgb(0x99ccff), Corners{2, 14, 6, 0}, Edges::all(2.5f),
                      Color{0.8f, 0.1f, 0.1f, 0.5f})); // translucent over the fill
    scene.insert(
            quad(Rect::fromXYWH(150, 10, 60, 40), transparent(), Corners::all(12), Edges::all(1.5f), rgb(0x22aa55)));
    scene.insert(quad(Rect::fromXYWH(220, 10, 40, 40), Color{0, 0, 0, 0.3f}, Corners::all(20), Edges::all(3),
                      rgb(0xe0e0e0)));
    scene.insert(quad(Rect::fromXYWH(10, 60, 20, 16), rgb(0x404040), Corners::all(4), Edges::all(12),
                      rgb(0xffcc00))); // all border
    scene.insert(
            quad(Rect::fromXYWH(40.25f, 60.75f, 50, 30), rgb(0x5050a0), {}, Edges{0.5f, 0, 0.5f, 0}, rgb(0xffffff)));
    scene.insert(quad(Rect::fromXYWH(100, 60, 50, 30), rgb(0x5050a0), Corners::all(6), Edges::all(2),
                      transparent())); // none
    scene.insert(
            quad(Rect::fromXYWH(160, 60, 50, 30), rgb(0x5050a0), Corners::all(6), Edges{0, 0, 2, 0}, rgb(0xff8080)));
    scene.insert(
            quad(Rect::fromXYWH(220, 60, 40, 30), rgba(0xffffff40), Corners::all(30), Edges::all(1), rgba(0x00000080)));
    compare(scene, atlas, 270, 100, rgb(0x303030));
}

void TestAtramentQt::gradients()
{
    Atlas atlas;
    Scene scene;
    const float angles[] = {0, 45, 90, 135, 180, 225, 270, 315, 33.3f, -90, 450, 1e-3f};
    for (int i = 0; i < int(std::size(angles)); ++i) {
        Quad q = quad(Rect::fromXYWH(8 + float(i % 6) * 44 + 0.3f, 8 + float(i / 6) * 50, 40, 44), {},
                      Corners::all(float(i % 4) * 5), i % 3 == 0 ? Edges::all(1.5f) : Edges{}, rgba(0x10101080));
        q.background = Background::linear(angles[i], rgba(0xff4060ff), i % 2 ? rgba(0x2040ff40) : rgb(0x40ff80));
        scene.insert(q);
    }
    Quad clear = quad(Rect::fromXYWH(8, 110, 120, 20), {});
    clear.background = Background::linear(90, transparent(), rgb(0x000000));
    scene.insert(clear);
    compare(scene, atlas, 280, 140, transparent());
}

void TestAtramentQt::shadows()
{
    Atlas atlas;
    Scene scene;
    scene.insert(quad(Rect::fromXYWH(0, 0, 320, 160), rgb(0xf7f5f0)));
    scene.insert(shadow(Rect::fromXYWH(20, 20, 60, 40), Color{0, 0, 0, 0.3f}, 8, Corners::all(10)));
    scene.insert(shadow(Rect::fromXYWH(110.5f, 22.25f, 60, 40), Color{0.1f, 0, 0.4f, 0.5f}, 2));
    scene.insert(shadow(Rect::fromXYWH(200, 20, 80, 50), Color{0, 0, 0, 0.6f}, 24, Corners{0, 20, 6, 12}));
    scene.insert(shadow(Rect::fromXYWH(20, 100, 50, 30), Color{0, 0, 0, 0.5f}, 0, Corners::all(6))); // sharp
    scene.insert(shadow(Rect::fromXYWH(90, 100, 50, 30), Color{0, 0, 0, 0.5f}, 0.005f));             // sharp too
    scene.insert(shadow(Rect::fromXYWH(160, 110, 20, 6), Color{0.8f, 0.2f, 0.2f, 0.9f}, 5, Corners::all(3)));
    scene.insert(shadow(Rect::fromXYWH(220, 100, 60, 40), Color{0, 0, 0, 0.25f}, 1e-3f + 12, Corners::all(1e6f)));
    scene.insert(quad(Rect::fromXYWH(200, 20, 80, 50), white(), Corners{0, 20, 6, 12}));
    compare(scene, atlas, 320, 160, transparent());
    if (QTest::currentTestFailed())
        return;
    // A blur that is not a number: sharp. (Not over an opaque quad: the CPU renderer's occlusion
    // skips such a shadow there, as its reach is not a number either.)
    Scene malformed;
    malformed.insert(shadow(Rect::fromXYWH(10, 10, 40, 20), Color{0, 0, 0, 0.5f}, kNaN, Corners::all(4)));
    malformed.insert(shadow(Rect::fromXYWH(60, 10, -40, 20), black(), 4));
    malformed.insert(shadow(Rect::fromXYWH(60, 10, 20, 20), transparent(), 4));
    compare(malformed, atlas, 100, 40, transparent());
}

void TestAtramentQt::monochromeSprites()
{
    Atlas atlas(128);
    std::vector<AtlasTile> tiles;
    for (int g = 0; g < 24; ++g) {
        const auto tile = atlas.getOrInsert(std::uint64_t(100 + g), AtlasKind::Alpha, [&] {
            return std::optional(glyphBitmap(5 + g % 7, 8 + g % 5, 0.3f + float(g % 5) / 10));
        });
        QVERIFY(tile);
        tiles.push_back(*tile);
    }
    Scene scene;
    scene.insert(quad(Rect::fromXYWH(0, 0, 200, 60), rgb(0xfafafa)));
    for (int i = 0; i < 60; ++i) {
        MonochromeSprite s;
        s.tile = tiles[std::size_t(i) % tiles.size()];
        // Fractional origins round to whole pixels (half-way up).
        s.bounds = Rect::fromXYWH(4 + float(i % 20) * 9.5f + (i % 3 == 0 ? 0.5f : 0.25f), 4 + float(i / 20) * 18 - 0.5f,
                                  float(s.tile.width), float(s.tile.height));
        s.mask = everywhere();
        s.color = i % 4 == 0 ? Color{0.1f, 0.2f, 0.8f, 0.6f} : rgb(0x202020);
        scene.insert(s);
    }
    // A tile reaching past its page's edge, one before its start, one of no page, one on a colour
    // page: cut to what exists, or nothing.
    MonochromeSprite edge;
    edge.mask = everywhere();
    edge.color = black();
    edge.tile = tiles[0];
    edge.tile.x = atlas.pages()[edge.tile.page].width - 3;
    edge.tile.width = 8;
    edge.bounds = Rect::fromXYWH(150, 50, 8, float(edge.tile.height));
    scene.insert(edge);
    edge.tile.x = -2;
    edge.bounds = Rect::fromXYWH(170, 50, 8, float(edge.tile.height));
    scene.insert(edge);
    edge.tile.page = 99;
    scene.insert(edge);
    const auto colour =
            atlas.getOrInsert(7, AtlasKind::Color, [] { return std::optional(colorBitmap(1, 1, {px(1, 2, 3, 255)})); });
    QVERIFY(colour);
    edge.tile = *colour;
    scene.insert(edge);
    compare(scene, atlas, 200, 64, transparent());
}

void TestAtramentQt::colorSprites()
{
    Atlas atlas(64);
    const auto tile =
            atlas.getOrInsert(3, AtlasKind::Color, [] { return std::optional(colorBitmap(6, 5, pattern(6, 5))); });
    QVERIFY(tile);
    Scene scene;
    scene.insert(quad(Rect::fromXYWH(0, 0, 160, 60), rgb(0x404850)));
    ColorSprite s;
    s.mask = everywhere();
    s.tile = *tile;
    s.bounds = Rect::fromXYWH(4.5f, 4.25f, 37, 23);
    scene.insert(s);
    s.bounds = Rect::fromXYWH(50, 4, 6, 5); // one to one
    scene.insert(s);
    s.bounds = Rect::fromXYWH(60.3f, 4.6f, 40, 30);
    s.opacity = 0.5f;
    scene.insert(s);
    s.bounds = Rect::fromXYWH(105, 4, 50, 50);
    s.opacity = 1;
    s.grayscale = true;
    scene.insert(s);
    s.bounds = Rect::fromXYWH(4, 34, 3, 2); // scaled down
    s.grayscale = false;
    scene.insert(s);
    s.opacity = kNaN; // malformed: nothing
    s.bounds = Rect::fromXYWH(20, 34, 20, 20);
    scene.insert(s);
    compare(scene, atlas, 160, 60, transparent());
}

void TestAtramentQt::pictures()
{
    Atlas atlas;
    const auto image = ImageData::make(16, 12, pattern(16, 12));
    Scene scene;
    scene.insert(quad(Rect::fromXYWH(0, 0, 240, 100), rgb(0xe8e4dc)));
    Picture p;
    p.mask = everywhere();
    p.image = image;
    p.bounds = Rect::fromXYWH(4.5f, 4.5f, 53, 40);
    scene.insert(p);
    p.bounds = Rect::fromXYWH(62, 4, 60, 44);
    p.cornerRadii = Corners::all(10);
    scene.insert(p);
    p.bounds = Rect::fromXYWH(126.25f, 4.75f, 48, 36);
    p.cornerRadii = Corners{0, 18, 4, 9};
    p.source = Rect::fromXYWH(3.5f, 2, 8, 6);
    p.opacity = 0.7f;
    scene.insert(p);
    p.bounds = Rect::fromXYWH(180, 4, 50, 40);
    p.source = {};
    p.cornerRadii = {};
    p.opacity = 1;
    p.grayscale = true;
    scene.insert(p);
    p.bounds = Rect::fromXYWH(4, 60, 8, 6); // scaled down
    p.grayscale = false;
    scene.insert(p);
    // A second picture, and the first again: three batches, two textures.
    Picture q = p;
    q.image = ImageData::make(3, 3, pattern(3, 3));
    q.bounds = Rect::fromXYWH(20, 56, 30, 30);
    scene.insert(q);
    p.bounds = Rect::fromXYWH(60, 56, 30, 30);
    scene.insert(p);
    Picture broken = p;
    broken.image = nullptr;
    scene.insert(broken);
    auto wrong = std::make_shared<ImageData>();
    wrong->width = 4;
    wrong->height = 4; // no pixels
    broken.image = wrong;
    scene.insert(broken);
    compare(scene, atlas, 240, 100, transparent());
}

void TestAtramentQt::paths()
{
    Atlas atlas;
    Scene scene;
    scene.insert(quad(Rect::fromXYWH(0, 0, 260, 120), rgb(0xffffff)));
    const auto star = [](Point c, float r) {
        PathBuilder b;
        for (int i = 0; i < 10; ++i) {
            const float a = float(i) * std::numbers::pi_v<float> / 5 - std::numbers::pi_v<float> / 2;
            const float rr = i % 2 ? r * 0.45f : r;
            const Point p{c.x + rr * std::cos(a), c.y + rr * std::sin(a)};
            if (i == 0)
                b.moveTo(p);
            else
                b.lineTo(p);
        }
        b.close();
        return b;
    };
    scene.insert(star({40.3f, 40.6f}, 32).build(rgb(0xd04020), everywhere()));
    PathBuilder ring;
    ring.arcTo({120, 40}, 30, 0, 2 * std::numbers::pi_v<float>);
    ring.close();
    ring.arcTo({120, 40}, 18, 2 * std::numbers::pi_v<float>, 0); // a hole, the other way round
    ring.close();
    scene.insert(std::move(ring).build(Color{0.1f, 0.3f, 0.9f, 0.7f}, everywhere()));
    PathBuilder curve;
    curve.moveTo({170, 10});
    curve.cubicTo({260, 0}, {250, 90}, {180, 70});
    curve.quadTo({150, 40}, {170, 10});
    curve.close();
    scene.insert(std::move(curve).build(rgba(0x30a05090), everywhere()));
    // Beyond the target's edges.
    scene.insert(star({250, 110}, 40).build(rgba(0x80008080), everywhere()));
    // Malformed: too few points, a NaN, nothing at all.
    PathBuilder two;
    two.moveTo({10, 90});
    two.lineTo({50, 110});
    scene.insert(std::move(two).build(black(), everywhere()));
    PathBuilder nan;
    nan.moveTo({10, 90});
    nan.lineTo({kNaN, 110});
    nan.lineTo({30, 100});
    scene.insert(std::move(nan).build(black(), everywhere()));
    compare(scene, atlas, 260, 120, transparent());
}

void TestAtramentQt::underlines()
{
    Atlas atlas;
    Scene scene;
    scene.insert(quad(Rect::fromXYWH(0, 0, 220, 80), rgb(0xffffff)));
    const float thickness[] = {1, 1.5f, 2, 3};
    for (int i = 0; i < 4; ++i) {
        for (const bool wavy : {false, true}) {
            Underline u;
            u.mask = everywhere();
            u.color = wavy ? Color{0.85f, 0.2f, 0.2f, 0.9f} : rgb(0x203060);
            u.thickness = thickness[i];
            u.wavy = wavy;
            u.bounds = Rect::fromXYWH(6.3f + float(i) * 52, (wavy ? 40 : 10) + 0.5f * float(i), 47.4f,
                                      wavy ? 2 + thickness[i] * 2 : 4);
            scene.insert(u);
        }
    }
    Underline flat;
    flat.mask = everywhere();
    flat.color = black();
    flat.thickness = 0;
    flat.bounds = Rect::fromXYWH(10, 70, 40, 3); // no thickness: nothing
    scene.insert(flat);
    compare(scene, atlas, 220, 80, transparent());
}

void TestAtramentQt::masks_data()
{
    QTest::addColumn<Rect>("mask");
    QTest::newRow("fractional") << Rect::fromXYWH(20.4f, 15.6f, 101.3f, 50.2f);
    QTest::newRow("whole pixels") << Rect::fromXYWH(30, 10, 60, 40);
    QTest::newRow("thin") << Rect::fromXYWH(0, 33.5f, 200, 0.5f);
    QTest::newRow("outside") << Rect::fromXYWH(300, 300, 10, 10);
    QTest::newRow("empty") << Rect::fromXYWH(50, 50, 0, 10);
}

void TestAtramentQt::masks()
{
    QFETCH(Rect, mask);
    Atlas atlas(64);
    const auto glyph = atlas.getOrInsert(1, AtlasKind::Alpha, [] { return std::optional(glyphBitmap(9, 12, 0.4f)); });
    const auto colour =
            atlas.getOrInsert(2, AtlasKind::Color, [] { return std::optional(colorBitmap(4, 4, pattern(4, 4))); });
    QVERIFY(glyph && colour);
    const ContentMask m{mask};
    Scene scene;
    scene.insert(quad(Rect::fromXYWH(0, 0, 200, 90), rgb(0x303030)));
    Quad q = quad(Rect::fromXYWH(10, 10, 80, 60), rgb(0x60a0ff), Corners::all(12), Edges::all(2), rgb(0xffffff));
    q.mask = m;
    scene.insert(q);
    Shadow s = shadow(Rect::fromXYWH(100, 20, 40, 30), Color{1, 1, 1, 0.5f}, 10, Corners::all(5));
    s.mask = m;
    scene.insert(s);
    for (int i = 0; i < 12; ++i) {
        MonochromeSprite g;
        g.tile = *glyph;
        g.bounds = Rect::fromXYWH(15 + float(i) * 10, 30, 9, 12);
        g.mask = m;
        g.color = rgb(0xffe080);
        scene.insert(g);
    }
    ColorSprite c;
    c.tile = *colour;
    c.bounds = Rect::fromXYWH(100, 50, 30, 30);
    c.mask = m;
    scene.insert(c);
    Picture p;
    p.image = ImageData::make(5, 5, pattern(5, 5));
    p.bounds = Rect::fromXYWH(25, 45, 40, 40);
    p.cornerRadii = Corners::all(8);
    p.mask = m;
    scene.insert(p);
    PathBuilder b;
    b.arcTo({160, 45}, 25, 0, 2 * std::numbers::pi_v<float>);
    b.close();
    scene.insert(std::move(b).build(rgba(0x40ff4080), m));
    Underline u;
    u.bounds = Rect::fromXYWH(15, 70, 150, 5);
    u.thickness = 1.5f;
    u.wavy = true;
    u.color = rgb(0xff6060);
    u.mask = m;
    scene.insert(u);
    compare(scene, atlas, 200, 90, transparent());
}

void TestAtramentQt::opaqueBackgroundAndOrder()
{
    // Painter's order across kinds and batches: a quad over glyphs over a quad, a shadow over a
    // picture; whatever the CPU renderer skips under opaque quads (its occlusion) must not show.
    Atlas atlas(64);
    const auto glyph = atlas.getOrInsert(1, AtlasKind::Alpha, [] { return std::optional(glyphBitmap(8, 10, 0.5f)); });
    QVERIFY(glyph);
    Scene scene;
    scene.insert(quad(Rect::fromXYWH(0, 0, 120, 80), rgb(0xff0000)));
    for (int i = 0; i < 10; ++i) {
        MonochromeSprite g;
        g.tile = *glyph;
        g.bounds = Rect::fromXYWH(4 + float(i) * 11, 8, 8, 10);
        g.mask = everywhere();
        g.color = rgb(0x00ff00);
        scene.insert(g);
    }
    scene.insert(quad(Rect::fromXYWH(30, 0, 40, 80), rgb(0x0000ff)));
    Picture p;
    p.image = ImageData::make(4, 4, pattern(4, 4));
    p.bounds = Rect::fromXYWH(10, 30, 40, 40);
    p.mask = everywhere();
    scene.insert(p);
    scene.insert(shadow(Rect::fromXYWH(20, 40, 30, 20), Color{0, 0, 0, 0.7f}, 6));
    scene.insert(quad(Rect::fromXYWH(60, 40, 50, 30), rgba(0xffffffc0), Corners::all(8)));
    compare(scene, atlas, 120, 80, transparent());
}

void TestAtramentQt::scaleFactors_data()
{
    QTest::addColumn<float>("scale");
    QTest::newRow("1") << 1.0f;
    QTest::newRow("1.25") << 1.25f;
    QTest::newRow("1.5") << 1.5f;
    QTest::newRow("2") << 2.0f;
}

void TestAtramentQt::scaleFactors()
{
    QFETCH(float, scale);
    auto platform = std::make_shared<TestPlatform>();
    App app(platform, loadFonts());
    TestWindow &tw = platform->newWindow({280, 210}, scale);
    Entity<Card> root = app.newEntity<Card>();
    app.openWindow(tw, root);
    tw.settle();
    QVERIFY(tw.lastScene());
    QVERIFY(tw.lastScene()->size() > 50);
    // TestWindow's CPU frame of the same scene and atlas, cleared to white.
    for (const auto &backend : m_backends) {
        GpuFrameBuilder builder;
        QRhiRenderer renderer;
        compareFrame(*backend, renderer, builder, *tw.lastScene(), app.atlas(), tw.image(), white());
        if (QTest::currentTestFailed())
            return;
    }
}

void TestAtramentQt::atlasUpdatesAcrossFrames()
{
    for (const auto &backend : m_backends) {
        Atlas atlas(64); // small pages: new ones come quickly
        GpuFrameBuilder builder;
        QRhiRenderer renderer;
        std::vector<AtlasTile> tiles;
        std::uint64_t nextKey = 1;
        const auto addTiles = [&](int n, float inset) {
            for (int i = 0; i < n; ++i) {
                const int w = 5 + int(nextKey % 6);
                const int h = 7 + int(nextKey % 4);
                const auto tile = atlas.getOrInsert(nextKey++, AtlasKind::Alpha,
                                                    [&] { return std::optional(glyphBitmap(w, h, inset)); });
                QVERIFY(tile);
                tiles.push_back(*tile);
            }
        };
        const auto frame = [&](std::size_t from) {
            atlas.beginFrame();
            Scene scene;
            scene.insert(quad(Rect::fromXYWH(0, 0, 200, 100), rgb(0xffffff)));
            for (std::size_t i = from; i < tiles.size(); ++i) {
                // Look the tile up as a frame does: it stays alive.
                const std::size_t n = i - from;
                MonochromeSprite s;
                s.tile = tiles[i];
                s.bounds = Rect::fromXYWH(2 + float(n % 18) * 11, 2 + float(n / 18) * 13, float(s.tile.width),
                                          float(s.tile.height));
                s.mask = everywhere();
                s.color = rgb(0x102030);
                scene.insert(s);
            }
            RasterTarget cpu;
            cpu.resize(200, 100);
            RasterRenderer().render(scene, atlas, cpu, transparent());
            compareFrame(*backend, renderer, builder, scene, atlas, cpu, transparent());
        };
        addTiles(10, 0.3f);
        frame(0);
        QVERIFY(!QTest::currentTestFailed());
        QCOMPARE(renderer.stats().uploadedBytes, std::size_t(64 * 64)); // the whole first page
        QVERIFY(atlas.pages()[0].dirty.isEmpty());
        // A few more glyphs: only the changed part goes up.
        addTiles(3, 0.6f);
        frame(0);
        QVERIFY(!QTest::currentTestFailed());
        QVERIFY2(renderer.stats().uploadedBytes > 0 && renderer.stats().uploadedBytes < std::size_t(64 * 64 / 2),
                 qPrintable(QString::number(renderer.stats().uploadedBytes)));
        // Nothing new: nothing uploaded.
        frame(0);
        QVERIFY(!QTest::currentTestFailed());
        QCOMPARE(renderer.stats().uploadedBytes, std::size_t(0));
        // Enough for more pages, added between frames.
        const std::size_t pagesBefore = atlas.pages().size();
        addTiles(60, 0.2f);
        QVERIFY(atlas.pages().size() > pagesBefore);
        frame(0);
        QVERIFY(!QTest::currentTestFailed());
        // The first tiles fall out of use, their pages empty and are packed again with other
        // glyphs at the same places: a stale texture would show the old ones.
        const std::size_t keep = tiles.size();
        for (int i = 0; i < 4; ++i) {
            atlas.beginFrame();
            for (std::size_t t = 13; t < keep; ++t)
                atlas.find(t + 1); // the later tiles stay in use
        }
        atlas.collectGarbage(2);
        tiles.erase(tiles.begin(), tiles.begin() + 13);
        addTiles(13, 1.1f);
        frame(0);
        QVERIFY(!QTest::currentTestFailed());
    }
}

void TestAtramentQt::atlasSharedByTwoRenderers()
{
    // Two windows' renderers on one atlas: each marks pages clean after its copy; the other then
    // takes the whole page instead of a dirty rectangle that misses what was cleaned.
    for (const auto &backend : m_backends) {
        Atlas atlas(64);
        GpuFrameBuilder builderA;
        GpuFrameBuilder builderB;
        QRhiRenderer a;
        QRhiRenderer b;
        std::vector<AtlasTile> tiles;
        std::uint64_t key = 1;
        const auto add = [&](int n) {
            for (int i = 0; i < n; ++i) {
                const auto tile = atlas.getOrInsert(key, AtlasKind::Alpha, [&] {
                    return std::optional(glyphBitmap(6 + int(key % 4), 9, 0.25f + float(key % 3) / 4));
                });
                ++key;
                QVERIFY(tile);
                tiles.push_back(*tile);
            }
        };
        const auto draw = [&](QRhiRenderer &renderer, GpuFrameBuilder &builder) {
            Scene scene;
            for (std::size_t i = 0; i < tiles.size(); ++i) {
                MonochromeSprite s;
                s.tile = tiles[i];
                s.bounds = Rect::fromXYWH(2 + float(i % 12) * 10, 2 + float(i / 12) * 12, float(s.tile.width),
                                          float(s.tile.height));
                s.mask = everywhere();
                s.color = black();
                scene.insert(s);
            }
            RasterTarget cpu;
            cpu.resize(124, 40);
            RasterRenderer().render(scene, atlas, cpu, white());
            compareFrame(*backend, renderer, builder, scene, atlas, cpu, white());
        };
        add(6);
        draw(a, builderA);
        add(4);
        draw(a, builderA); // a cleans the page
        QVERIFY(!QTest::currentTestFailed());
        draw(b, builderB); // b's first copy: whole
        add(3);
        draw(b, builderB); // b cleans: only b's copy is up to date from the dirty rectangle
        QVERIFY(!QTest::currentTestFailed());
        draw(a, builderA); // a must take the whole page
        QVERIFY(!QTest::currentTestFailed());
        QCOMPARE(a.stats().uploadedBytes, std::size_t(64 * 64));
    }
}

void TestAtramentQt::pathTilesCached()
{
    for (const auto &backend : m_backends) {
        Atlas atlas;
        GpuFrameBuilder builder;
        QRhiRenderer renderer;
        const auto blob = [](float x, float y) {
            PathBuilder b;
            b.moveTo({x, y});
            b.cubicTo({x + 30, y - 10}, {x + 40, y + 30}, {x + 10, y + 25});
            b.close();
            return std::move(b).build(rgba(0x3050c0c0), everywhere());
        };
        const auto frame = [&](std::initializer_list<Point> at) {
            Scene scene;
            for (const Point p : at)
                scene.insert(blob(p.x, p.y));
            RasterTarget cpu;
            cpu.resize(160, 60);
            RasterRenderer().render(scene, atlas, cpu, white());
            compareFrame(*backend, renderer, builder, scene, atlas, cpu, white());
        };
        // Whole pixels apart: one tile; a quarter of a pixel off: another.
        frame({{5.25f, 15}, {55.25f, 15}, {105.5f, 15}});
        QVERIFY(!QTest::currentTestFailed());
        QCOMPARE(builder.pathAtlas().tileCount(), std::size_t(2));
        // The same paths again: no upload.
        frame({{5.25f, 15}, {55.25f, 15}, {105.5f, 15}});
        QVERIFY(!QTest::currentTestFailed());
        QCOMPARE(renderer.stats().uploadedBytes, std::size_t(0));
    }
}

void TestAtramentQt::batching()
{
    Atlas atlas(64);
    const auto glyph = atlas.getOrInsert(1, AtlasKind::Alpha, [] { return std::optional(glyphBitmap(6, 8, 0.4f)); });
    QVERIFY(glyph);
    Scene scene;
    const auto addQuads = [&](int n) {
        for (int i = 0; i < n; ++i)
            scene.insert(quad(Rect::fromXYWH(float(i % 20) * 6, float(i / 20) * 6, 5, 5), rgb(0x808080)));
    };
    const auto addGlyphs = [&](int n) {
        for (int i = 0; i < n; ++i) {
            MonochromeSprite s;
            s.tile = *glyph;
            s.bounds = Rect::fromXYWH(float(i % 20) * 6, float(i / 20) * 9, 6, 8);
            s.mask = everywhere();
            s.color = black();
            scene.insert(s);
        }
    };
    addQuads(100);
    addGlyphs(100);
    addQuads(100);
    scene.insert(shadow(Rect::fromXYWH(10, 10, 30, 20), Color{0, 0, 0, 0.4f}, 6));
    scene.insert(quad(Rect::fromXYWH(-50, -50, 10, 10), rgb(0xff0000))); // outside: no instance
    GpuFrameBuilder builder;
    const GpuFrame frame = builder.build(scene, atlas, 120, 60);
    QCOMPARE(frame.batches.size(), std::size_t(4));
    QCOMPARE(frame.batches[0].kind, GpuKind::Quad);
    QCOMPARE(frame.batches[0].count, 100u);
    QCOMPARE(frame.batches[1].kind, GpuKind::Sprite);
    QCOMPARE(frame.batches[2].kind, GpuKind::Quad);
    QCOMPARE(frame.batches[3].kind, GpuKind::Shadow);
    QCOMPARE(frame.primitives, std::size_t(301));
    for (const auto &backend : m_backends) {
        QRhiRenderer renderer;
        renderer.setFrame(std::make_shared<const GpuFrame>(frame));
        renderer.syncAtlas(QRhiRenderer::AtlasSlot::Atlas, atlas);
        QVERIFY(backend->render(renderer, white()));
        QCOMPARE(renderer.stats().batches, std::size_t(4));
        QCOMPARE(renderer.stats().instances, std::size_t(301));
    }
    compare(scene, atlas, 120, 60, white());
}

void TestAtramentQt::opacity()
{
    // Qt Quick's inherited opacity multiplies every premultiplied colour: half of the frame drawn
    // over transparency.
    Atlas atlas;
    Scene scene;
    scene.insert(quad(Rect::fromXYWH(4, 4, 40, 30), rgb(0xff8040), Corners::all(6)));
    scene.insert(shadow(Rect::fromXYWH(50, 4, 30, 30), Color{0, 0, 1, 1}, 4));
    for (const auto &backend : m_backends) {
        GpuFrameBuilder builder;
        QRhiRenderer renderer;
        renderer.setFrame(std::make_shared<const GpuFrame>(builder.build(scene, atlas, 90, 40)));
        const auto full = backend->render(renderer, transparent());
        QVERIFY(full);
        // OffscreenRhi draws at opacity 1; the same frame at ½ through prepare() directly.
        QRhi *rhi = backend->rhi();
        std::unique_ptr<QRhiTexture> texture(rhi->newTexture(
                QRhiTexture::RGBA8, QSize(90, 40), 1, QRhiTexture::RenderTarget | QRhiTexture::UsedAsTransferSource));
        QVERIFY(texture->create());
        std::unique_ptr<QRhiTextureRenderTarget> rt(rhi->newTextureRenderTarget({texture.get()}));
        std::unique_ptr<QRhiRenderPassDescriptor> rp(rt->newCompatibleRenderPassDescriptor());
        rt->setRenderPassDescriptor(rp.get());
        QVERIFY(rt->create());
        QRhiCommandBuffer *cb = nullptr;
        QCOMPARE(rhi->beginOffscreenFrame(&cb), QRhi::FrameOpSuccess);
        QMatrix4x4 transform = rhi->clipSpaceCorrMatrix();
        transform.ortho(0, 90, 40, 0, -1, 1);
        QVERIFY(renderer.prepare(rhi, cb, rp.get(), 1, transform, 0.5f));
        cb->beginPass(rt.get(), QColor(0, 0, 0, 0), {1.0f, 0});
        renderer.render(cb, QSize(90, 40));
        QRhiReadbackResult result;
        QRhiResourceUpdateBatch *readback = rhi->nextResourceUpdateBatch();
        readback->readBackTexture({texture.get()}, &result);
        cb->endPass(readback);
        rhi->endOffscreenFrame();
        QCOMPARE(result.data.size(), qsizetype(90 * 40 * 4));
        const bool flip = rhi->isYUpInFramebuffer();
        int largest = 0;
        for (int y = 0; y < 40; ++y) {
            const auto *row =
                    reinterpret_cast<const Pixel *>(result.data.constData()) + std::size_t(flip ? 39 - y : y) * 90;
            for (int x = 0; x < 90; ++x) {
                const Pixel f = (*full)[std::size_t(y) * 90 + std::size_t(x)];
                const Pixel h = row[x];
                for (const auto [a, b] :
                     {std::pair{f.r, h.r}, std::pair{f.g, h.g}, std::pair{f.b, h.b}, std::pair{f.a, h.a}})
                    largest = std::max(largest, std::abs(int(a) - 2 * int(b)));
            }
        }
        if (largest > 2) {
            QImage half(90, 40, QImage::Format_RGBA8888_Premultiplied);
            for (int y = 0; y < 40; ++y)
                std::memcpy(half.scanLine(y), result.data.constData() + std::size_t(flip ? 39 - y : y) * 90 * 4,
                            90 * 4);
            half.save(QDir::temp().filePath(QStringLiteral("atrament-qt-opacity-half.png")));
            toImage(*full, 90, 40).save(QDir::temp().filePath(QStringLiteral("atrament-qt-opacity-full.png")));
        }
        QVERIFY2(largest <= 2, qPrintable(backend->description() + QStringLiteral(": ") + QString::number(largest)));
        renderer.releaseResources(); // before the render pass it was made for goes
    }
}

void TestAtramentQt::busyFrame()
{
    // A busy desktop window (tst_atrament_raster's uiFrameTime): a sidebar and panes, cards with
    // shadows, ≈2 000 small rounded quads with borders and gradients, ≈5 000 glyphs; then the
    // time of each renderer for it.
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
        const int w = 5 + next(5);
        const int h = 8 + next(6);
        const float inset = 0.3f + float(next(10)) / 20;
        const auto tile = atlas.getOrInsert(std::uint64_t(1000 + g), AtlasKind::Alpha,
                                            [&] { return std::optional(glyphBitmap(w, h, inset)); });
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
    for (int quads = 0; quads < 2000; ++quads) {
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
    }
    std::size_t sprites = 0;
    for (int line = 0; sprites < 5000; ++line) {
        const float y = float(56 + (line % 40) * 18);
        float x = 280.0f + float((line / 40) * 7);
        while (x < float(kWidth - 20) && sprites < 5000) {
            const AtlasTile &tile = glyphs[std::size_t(next(int(glyphs.size())))];
            MonochromeSprite s;
            s.bounds = Rect::fromXYWH(x, y, float(tile.width), float(tile.height));
            s.mask = window;
            s.color = rgb(0x2a2a2e);
            s.tile = tile;
            scene.insert(s);
            x += float(tile.width) + 1;
            ++sprites;
        }
    }
    RasterTarget cpu;
    cpu.resize(kWidth, kHeight);
    RasterRenderer raster;
    raster.render(scene, atlas, cpu, transparent());
    constexpr int kRuns = 5;
    const auto t0 = std::chrono::steady_clock::now();
    for (int i = 0; i < kRuns; ++i)
        raster.render(scene, atlas, cpu, transparent());
    const double cpuMs =
            std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count() / kRuns;
    qInfo("busy frame (%zu primitives), CPU renderer: %.2f ms", scene.size(), cpuMs);
    for (const auto &backend : m_backends) {
        GpuFrameBuilder builder;
        QRhiRenderer renderer;
        // Where several translucent quads overlap, both renderers round every layer to bytes, the
        // CPU its source before blending, the GPU (at its own precision) the blended result: up to
        // 3, a few pixels (of a million) up to 6.
        compareFrame(*backend, renderer, builder, scene, atlas, cpu, transparent(), {3, 50, 6});
        if (QTest::currentTestFailed())
            return;
        double buildMs = 0;
        double drawMs = 0;
        for (int i = 0; i < kRuns; ++i) {
            const auto a = std::chrono::steady_clock::now();
            renderer.setFrame(std::make_shared<const GpuFrame>(builder.build(scene, atlas, kWidth, kHeight)));
            renderer.syncAtlas(QRhiRenderer::AtlasSlot::Atlas, atlas);
            const auto b = std::chrono::steady_clock::now();
            QVERIFY(backend->render(renderer, transparent(), false));
            const auto c = std::chrono::steady_clock::now();
            buildMs += std::chrono::duration<double, std::milli>(b - a).count();
            drawMs += std::chrono::duration<double, std::milli>(c - b).count();
        }
        qInfo("busy frame on %s: instances %.2f ms, record + submit + wait %.2f ms; %zu draw calls",
              qPrintable(backend->description()), buildMs / kRuns, drawMs / kRuns, renderer.stats().batches);
    }
}

void TestAtramentQt::cpuFallback()
{
    QVERIFY(atramentGpuRendererWanted() || qEnvironmentVariable("BRULION_ATRAMENT_RENDERER") == QLatin1String("cpu"));
    qputenv("BRULION_ATRAMENT_RENDERER", "CPU");
    QVERIFY(!atramentGpuRendererWanted());
    qunsetenv("BRULION_ATRAMENT_RENDERER");
    QVERIFY(atramentGpuRendererWanted());
    // Under the offscreen platform plugin Qt Quick has no QRhi (its software backend): the item
    // draws with the CPU renderer into a texture.
    if (QGuiApplication::platformName() != QLatin1String("offscreen"))
        QSKIP("Qt Quick's software backend is chosen by the offscreen platform plugin only");
    auto platform = std::make_shared<QtPlatform>();
    App app(platform, loadFonts());
    Entity<Card> root = app.newEntity<Card>();
    QQuickWindow window;
    window.resize(200, 120);
    auto *item = new AtramentItem(window.contentItem());
    item->setSize(QSizeF(200, 120));
    item->attach(app, AnyView{root.id(), &renderView<Card>});
    window.show();
    QTRY_VERIFY(!item->frameImage().isNull());
    QCOMPARE(window.rendererInterface()->graphicsApi(), QSGRendererInterface::Software);
    QVERIFY(!item->usesGpuRenderer());
    QCOMPARE(item->frameImage().size(), QSize(200, 120));
    delete item; // before the App
}

QTEST_MAIN(TestAtramentQt)
#include "tst_atrament_qt.moc"
