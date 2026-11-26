// Atrament's scene: the atlas (packing, keys, eviction) and paths.
#include "scene/atlas.h"
#include "scene/scene.h"

#include <QtTest>

#include <set>

using namespace brulion::atrament;

namespace {

AtlasBitmap alphaBitmap(int w, int h, std::uint8_t value)
{
    AtlasBitmap b;
    b.width = w;
    b.height = h;
    b.bytesPerPixel = 1;
    b.pixels.assign(std::size_t(w * h), value);
    return b;
}

bool overlap(const AtlasTile &a, const AtlasTile &b)
{
    if (a.page != b.page)
        return false;
    return a.x < b.x + b.width && b.x < a.x + a.width && a.y < b.y + b.height && b.y < a.y + a.height;
}

} // namespace

class TestAtramentScene : public QObject {
    Q_OBJECT

private Q_SLOTS:
    void atlasInsertsOnce();
    void atlasTilesNeverOverlap();
    void atlasBlitsPixels();
    void atlasEmptyBitmap();
    void atlasNewPageWhenFull();
    void atlasHugeTileOwnPage();
    void atlasGarbageCollection();
    void atlasRejectsWrongFormat();
    void pathBounds();
    void sceneBoundsUnderMask();
};

void TestAtramentScene::atlasInsertsOnce()
{
    Atlas atlas(256);
    int made = 0;
    auto make = [&]() -> std::optional<AtlasBitmap> {
        ++made;
        return alphaBitmap(10, 12, 200);
    };
    const auto a = atlas.getOrInsert(42, AtlasKind::Alpha, make);
    const auto b = atlas.getOrInsert(42, AtlasKind::Alpha, make);
    QVERIFY(a && b);
    QCOMPARE(made, 1);
    QCOMPARE(a->x, b->x);
    QCOMPARE(a->width, 10);
    QCOMPARE(a->height, 12);
    QCOMPARE(atlas.tileCount(), std::size_t(1));
    QVERIFY(atlas.find(42));
    QVERIFY(!atlas.find(43));
}

void TestAtramentScene::atlasTilesNeverOverlap()
{
    Atlas atlas(256);
    std::vector<AtlasTile> tiles;
    for (int i = 0; i < 300; ++i) {
        const int w = 3 + (i * 7) % 29;
        const int h = 4 + (i * 13) % 23;
        const auto t = atlas.getOrInsert(std::uint64_t(i), AtlasKind::Alpha,
                [&]() -> std::optional<AtlasBitmap> { return alphaBitmap(w, h, 1); });
        QVERIFY(t);
        QVERIFY(t->x >= 1 && t->y >= 1);
        QVERIFY(t->x + t->width <= atlas.pages()[t->page].width);
        QVERIFY(t->y + t->height <= atlas.pages()[t->page].height);
        for (const AtlasTile &o : tiles)
            QVERIFY(!overlap(*t, o));
        tiles.push_back(*t);
    }
}

void TestAtramentScene::atlasBlitsPixels()
{
    Atlas atlas(128);
    AtlasBitmap b = alphaBitmap(3, 2, 0);
    for (int i = 0; i < 6; ++i)
        b.pixels[std::size_t(i)] = std::uint8_t(10 * (i + 1));
    const auto t = atlas.getOrInsert(1, AtlasKind::Alpha, [&]() -> std::optional<AtlasBitmap> { return b; });
    QVERIFY(t);
    const AtlasPage &page = atlas.pages()[t->page];
    QCOMPARE(int(page.alpha(t->x, t->y)), 10);
    QCOMPARE(int(page.alpha(t->x + 2, t->y)), 30);
    QCOMPARE(int(page.alpha(t->x, t->y + 1)), 40);
    QCOMPARE(int(page.alpha(t->x + 2, t->y + 1)), 60);
    // The padding around a tile stays empty.
    QCOMPARE(int(page.alpha(t->x + 3, t->y)), 0);
    QVERIFY(!page.dirty.isEmpty());
    QVERIFY(page.revision > 0);
    atlas.markClean(t->page);
    QVERIFY(atlas.pages()[t->page].dirty.isEmpty());

    AtlasBitmap c;
    c.width = 1;
    c.height = 1;
    c.bytesPerPixel = 4;
    c.pixels = { 1, 2, 3, 4 };
    const auto ct = atlas.getOrInsert(2, AtlasKind::Color, [&]() -> std::optional<AtlasBitmap> { return c; });
    QVERIFY(ct);
    QVERIFY(ct->page != t->page);
    QCOMPARE(atlas.pages()[ct->page].color(ct->x, ct->y), (Pixel { 1, 2, 3, 4 }));
}

void TestAtramentScene::atlasEmptyBitmap()
{
    Atlas atlas(64);
    int made = 0;
    const auto t = atlas.getOrInsert(7, AtlasKind::Alpha, [&]() -> std::optional<AtlasBitmap> {
        ++made;
        return AtlasBitmap {};
    });
    QVERIFY(t);
    QVERIFY(t->isEmpty());
    atlas.getOrInsert(7, AtlasKind::Alpha, [&]() -> std::optional<AtlasBitmap> {
        ++made;
        return AtlasBitmap {};
    });
    QCOMPARE(made, 1);
    QVERIFY(atlas.pages().empty());
}

void TestAtramentScene::atlasNewPageWhenFull()
{
    Atlas atlas(64);
    std::set<std::uint32_t> pages;
    for (int i = 0; i < 40; ++i) {
        const auto t = atlas.getOrInsert(std::uint64_t(i), AtlasKind::Alpha,
                [&]() -> std::optional<AtlasBitmap> { return alphaBitmap(20, 20, 9); });
        QVERIFY(t);
        pages.insert(t->page);
    }
    // 4 tiles of 22×22 fit a 64×64 page.
    QCOMPARE(pages.size(), std::size_t(10));
}

void TestAtramentScene::atlasHugeTileOwnPage()
{
    Atlas atlas(64);
    const auto t = atlas.getOrInsert(1, AtlasKind::Alpha,
            [&]() -> std::optional<AtlasBitmap> { return alphaBitmap(100, 30, 5); });
    QVERIFY(t);
    QCOMPARE(atlas.pages()[t->page].width, 102);
    QCOMPARE(int(atlas.pages()[t->page].alpha(t->x + 99, t->y + 29)), 5);
}

void TestAtramentScene::atlasGarbageCollection()
{
    Atlas atlas(64);
    atlas.beginFrame();
    atlas.getOrInsert(1, AtlasKind::Alpha, [&]() -> std::optional<AtlasBitmap> { return alphaBitmap(20, 20, 1); });
    atlas.getOrInsert(2, AtlasKind::Alpha, [&]() -> std::optional<AtlasBitmap> { return alphaBitmap(20, 20, 1); });
    for (int f = 0; f < 5; ++f) {
        atlas.beginFrame();
        QVERIFY(atlas.find(1)); // 1 stays in use, 2 does not
        atlas.collectGarbage(3);
    }
    QVERIFY(atlas.find(1));
    QVERIFY(!atlas.find(2));
    // Once nothing uses the page, it is cleared and packed from the top again.
    for (int f = 0; f < 5; ++f) {
        atlas.beginFrame();
        atlas.collectGarbage(3);
    }
    QCOMPARE(atlas.tileCount(), std::size_t(0));
    const auto t = atlas.getOrInsert(3, AtlasKind::Alpha,
            [&]() -> std::optional<AtlasBitmap> { return alphaBitmap(20, 20, 1); });
    QVERIFY(t);
    QCOMPARE(t->y, 1);
    QCOMPARE(t->x, 1);
}

void TestAtramentScene::atlasRejectsWrongFormat()
{
    Atlas atlas(64);
    const auto t = atlas.getOrInsert(1, AtlasKind::Color,
            [&]() -> std::optional<AtlasBitmap> { return alphaBitmap(4, 4, 1); }); // 1 byte per pixel
    QVERIFY(!t);
    QVERIFY(!atlas.find(1));
}

void TestAtramentScene::pathBounds()
{
    PathBuilder b;
    b.moveTo({ 10, 10 });
    b.lineTo({ 30, 10 });
    b.quadTo({ 40, 20 }, { 30, 30 });
    b.close();
    b.arcTo({ 0, 0 }, 5, 0, float(M_PI));
    const PathPrimitive p = std::move(b).build(black(), ContentMask { Rect::fromXYWH(0, 0, 100, 100) });
    QCOMPARE(p.contourEnds.size(), std::size_t(2));
    QVERIFY(p.bounds.left() <= -5 + 0.01f);
    QVERIFY(p.bounds.right() > 34 && p.bounds.right() <= 35.01f);
    QCOMPARE(p.bounds.bottom(), 30.0f);
}

void TestAtramentScene::sceneBoundsUnderMask()
{
    Scene scene;
    Quad q;
    q.bounds = Rect::fromXYWH(0, 0, 100, 100);
    q.mask = ContentMask { Rect::fromXYWH(10, 10, 20, 20) };
    q.background = Background::solid(white());
    scene.insert(q);
    Shadow s;
    s.bounds = Rect::fromXYWH(200, 200, 10, 10);
    s.mask = ContentMask { Rect::fromXYWH(0, 0, 1000, 1000) };
    s.blurRadius = 4;
    scene.insert(s);
    const Rect b = scene.bounds();
    QCOMPARE(b.left(), 10.0f);
    QVERIFY(b.right() > 210 + 6);
}

QTEST_GUILESS_MAIN(TestAtramentScene)
#include "tst_atrament_scene.moc"
