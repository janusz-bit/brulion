// Atrament's base: geometry, colours, the frame arena, frame callables, the coverage rasterizer.
#include "base/arena.h"
#include "base/color.h"
#include "base/coverage.h"
#include "base/function.h"
#include "base/geometry.h"
#include "base/hash.h"

#include <QtTest>

#include <cmath>
#include <numeric>

using namespace brulion::atrament;

class TestAtramentBase : public QObject {
    Q_OBJECT

private Q_SLOTS:
    void rectIntersection_data();
    void rectIntersection();
    void rectUnionAndInset();
    void cornersClamp();
    void colorHex();
    void colorHsl();
    void colorOklch();
    void arenaAlignmentAndReuse();
    void arenaDestructorsNewestFirst();
    void arenaLargeAllocation();
    void arenaVector();
    void fnCallsInArena();
    void coverageFullPixels();
    void coverageHalfPixel();
    void coverageCircleArea();
    void coverageWindingOfHole();
    void coverageClipsOutside();
    void hashStable();
};

void TestAtramentBase::rectIntersection_data()
{
    QTest::addColumn<Rect>("a");
    QTest::addColumn<Rect>("b");
    QTest::addColumn<Rect>("expected");
    QTest::addColumn<bool>("intersects");
    QTest::newRow("overlap") << Rect::fromXYWH(0, 0, 10, 10) << Rect::fromXYWH(5, 5, 10, 10)
                             << Rect::fromXYWH(5, 5, 5, 5) << true;
    QTest::newRow("inside") << Rect::fromXYWH(0, 0, 10, 10) << Rect::fromXYWH(2, 3, 4, 5)
                            << Rect::fromXYWH(2, 3, 4, 5) << true;
    QTest::newRow("touching") << Rect::fromXYWH(0, 0, 10, 10) << Rect::fromXYWH(10, 0, 10, 10)
                              << Rect::fromXYWH(10, 0, 0, 10) << false;
    QTest::newRow("apart") << Rect::fromXYWH(0, 0, 10, 10) << Rect::fromXYWH(20, 20, 5, 5)
                           << Rect::fromXYWH(20, 20, 0, 0) << false;
}

void TestAtramentBase::rectIntersection()
{
    QFETCH(Rect, a);
    QFETCH(Rect, b);
    QFETCH(Rect, expected);
    QFETCH(bool, intersects);
    QCOMPARE(a.intersects(b), intersects);
    QCOMPARE(a.intersected(b), expected);
    QCOMPARE(a.intersected(b).isEmpty(), !intersects);
}

void TestAtramentBase::rectUnionAndInset()
{
    const Rect a = Rect::fromXYWH(0, 0, 10, 10);
    const Rect b = Rect::fromXYWH(20, 5, 5, 20);
    QCOMPARE(a.united(b), Rect::fromLTRB(0, 0, 25, 25));
    QCOMPARE(Rect {}.united(b), b);
    QCOMPARE(a.inset(Edges { 1, 2, 3, 4 }), Rect::fromLTRB(4, 1, 8, 7));
    // Insets larger than the rectangle leave nothing, never a negative size.
    QCOMPARE(a.inset(Edges::all(8)).size, (Size { 0, 0 }));
    QCOMPARE(Rect::fromLTRB(0.4f, 0.6f, 10.2f, 9.5f).roundedOut(), Rect::fromLTRB(0, 0, 11, 10));
    QCOMPARE(Rect::fromLTRB(0.4f, 0.6f, 10.2f, 9.5f).snapped(), Rect::fromLTRB(0, 1, 10, 10));
    QVERIFY(a.contains({ 0, 0 }));
    QVERIFY(!a.contains({ 10, 5 })); // half-open
}

void TestAtramentBase::cornersClamp()
{
    const Corners c = Corners { 50, 2, -3, 8 }.clampedTo({ 20, 10 });
    QCOMPARE(c, (Corners { 5, 2, 0, 5 }));
}

void TestAtramentBase::colorHex()
{
    const Color c = rgb(0x336699);
    QCOMPARE(c.r, 0x33 / 255.0f);
    QCOMPARE(c.g, 0x66 / 255.0f);
    QCOMPARE(c.b, 0x99 / 255.0f);
    QCOMPARE(c.a, 1.0f);
    QCOMPARE(rgba(0xff000080).a, 128 / 255.0f);
    const PremulColor p = PremulColor::from(rgba(0xffffff80));
    QCOMPARE(p.r, p.a);
}

void TestAtramentBase::colorHsl()
{
    const Color red = Color::hsla(0, 1, 0.5f);
    QCOMPARE(red.r, 1.0f);
    QCOMPARE(red.g, 0.0f);
    QCOMPARE(red.b, 0.0f);
    const Color gray = Color::hsla(123, 0, 0.25f);
    QCOMPARE(gray.r, 0.25f);
    QCOMPARE(gray.g, 0.25f);
    const Color wrapped = Color::hsla(-240, 1, 0.5f); // = 120: green
    QVERIFY(std::abs(wrapped.g - 1) < 1e-5f && wrapped.r < 1e-5f);
}

void TestAtramentBase::colorOklch()
{
    // White and black are exact; a mid grey has equal components.
    const Color w = Color::oklch(1, 0, 0);
    QVERIFY(std::abs(w.r - 1) < 1e-3f && std::abs(w.g - 1) < 1e-3f && std::abs(w.b - 1) < 1e-3f);
    const Color k = Color::oklch(0, 0, 0);
    QVERIFY(k.r < 1e-4f && k.g < 1e-4f && k.b < 1e-4f);
    const Color g = Color::oklch(0.6f, 0, 200);
    QVERIFY(std::abs(g.r - g.g) < 1e-3f && std::abs(g.g - g.b) < 1e-3f);
    // A saturated colour stays within sRGB (clipped).
    const Color c = Color::oklch(0.7f, 0.4f, 30);
    QVERIFY(c.r >= 0 && c.r <= 1 && c.g >= 0 && c.g <= 1 && c.b >= 0 && c.b <= 1);
}

void TestAtramentBase::arenaAlignmentAndReuse()
{
    Arena arena(1024);
    void *a = arena.allocate(3, 1);
    void *b = arena.allocate(8, 8);
    void *c = arena.allocate(16, 16);
    QVERIFY(a && b && c);
    QCOMPARE(reinterpret_cast<std::uintptr_t>(b) % 8, std::uintptr_t(0));
    QCOMPARE(reinterpret_cast<std::uintptr_t>(c) % 16, std::uintptr_t(0));
    QCOMPARE(arena.used(), std::size_t(27));
    const std::size_t capacity = arena.capacity();
    arena.reset();
    QCOMPARE(arena.used(), std::size_t(0));
    // The same memory again after a reset: a steady frame allocates nothing new.
    void *a2 = arena.allocate(3, 1);
    QCOMPARE(a2, a);
    for (int i = 0; i < 60; ++i)
        arena.allocate(40, 8);
    arena.reset();
    const std::size_t grown = arena.capacity();
    QVERIFY(grown > capacity);
    for (int i = 0; i < 60; ++i)
        arena.allocate(40, 8);
    QCOMPARE(arena.capacity(), grown);
}

namespace {
struct Tracked {
    std::vector<int> *log;
    int id;
    ~Tracked() { log->push_back(id); }
};
} // namespace

void TestAtramentBase::arenaDestructorsNewestFirst()
{
    std::vector<int> log;
    {
        Arena arena;
        arena.make<Tracked>(&log, 1);
        arena.make<Tracked>(&log, 2);
        arena.make<int>(5); // trivially destructible: nothing registered
        arena.make<Tracked>(&log, 3);
        arena.reset();
        QCOMPARE(log, (std::vector<int> { 3, 2, 1 }));
        arena.make<Tracked>(&log, 4);
    }
    // The arena's own destructor runs what is left.
    QCOMPARE(log, (std::vector<int> { 3, 2, 1, 4 }));
}

void TestAtramentBase::arenaLargeAllocation()
{
    Arena arena(256);
    auto *big = static_cast<char *>(arena.allocate(10000, 8));
    std::fill(big, big + 10000, 'x');
    auto *small = static_cast<char *>(arena.allocate(10, 1));
    QVERIFY(small);
    QVERIFY(small < big || small >= big + 10000);
    QVERIFY(arena.capacity() >= 10000);
}

void TestAtramentBase::arenaVector()
{
    Arena arena;
    {
        ArenaScope scope(arena);
        ArenaVector<int> v;
        for (int i = 0; i < 1000; ++i)
            v.push_back(i);
        QCOMPARE(std::accumulate(v.begin(), v.end(), 0), 999 * 1000 / 2);
        QVERIFY(arena.used() >= 1000 * sizeof(int));
    }
    QCOMPARE(Arena::current(), nullptr);
    // Without a current arena the allocator uses the heap.
    ArenaVector<int> heap { 1, 2, 3 };
    QCOMPARE(heap.size(), std::size_t(3));
}

void TestAtramentBase::fnCallsInArena()
{
    Arena arena;
    ArenaScope scope(arena);
    int calls = 0;
    std::string captured = "zażółć gęślą jaźń";
    Fn<int(int)> f = [&calls, captured](int x) {
        ++calls;
        return x + int(captured.size());
    };
    QVERIFY(bool(f));
    QCOMPARE(f(1), 1 + int(captured.size()));
    QCOMPARE(calls, 1);
    Fn<void()> empty;
    QVERIFY(!empty);
    const Fn<int(int)> copy = f; // copies share the target
    QCOMPARE(copy(0), int(captured.size()));
    QCOMPARE(calls, 2);
}

void TestAtramentBase::coverageFullPixels()
{
    CoverageRasterizer r;
    r.reset(8, 8);
    r.moveTo({ 2, 2 });
    r.lineTo({ 6, 2 });
    r.lineTo({ 6, 6 });
    r.lineTo({ 2, 6 });
    r.close();
    std::vector<std::uint8_t> out(64);
    r.accumulate(out.data(), 8);
    for (int y = 0; y < 8; ++y) {
        for (int x = 0; x < 8; ++x) {
            const bool inside = x >= 2 && x < 6 && y >= 2 && y < 6;
            QCOMPARE(int(out[std::size_t(y * 8 + x)]), inside ? 255 : 0);
        }
    }
}

void TestAtramentBase::coverageHalfPixel()
{
    // A square from 1.5 to 3.5: its edge pixels are half covered, the corners a quarter.
    CoverageRasterizer r;
    r.reset(5, 5);
    r.moveTo({ 1.5f, 1.5f });
    r.lineTo({ 3.5f, 1.5f });
    r.lineTo({ 3.5f, 3.5f });
    r.lineTo({ 1.5f, 3.5f });
    std::vector<float> out;
    r.close();
    r.accumulate(out);
    auto at = [&](int x, int y) { return out[std::size_t(y * 5 + x)]; };
    QVERIFY(std::abs(at(2, 2) - 1.0f) < 1e-5f);
    QVERIFY(std::abs(at(1, 2) - 0.5f) < 1e-5f);
    QVERIFY(std::abs(at(2, 1) - 0.5f) < 1e-5f);
    QVERIFY(std::abs(at(1, 1) - 0.25f) < 1e-5f);
    QVERIFY(std::abs(at(3, 3) - 0.25f) < 1e-5f);
    QCOMPARE(at(0, 0), 0.0f);
    QCOMPARE(at(4, 4), 0.0f);
}

void TestAtramentBase::coverageCircleArea()
{
    // A circle of radius 20 from four cubic Béziers: the covered area is πr² within 0.5 %.
    CoverageRasterizer r;
    r.reset(50, 50);
    const float k = 0.5522847f * 20;
    const Point c { 25, 25 };
    r.moveTo({ c.x + 20, c.y });
    r.cubicTo({ c.x + 20, c.y + k }, { c.x + k, c.y + 20 }, { c.x, c.y + 20 });
    r.cubicTo({ c.x - k, c.y + 20 }, { c.x - 20, c.y + k }, { c.x - 20, c.y });
    r.cubicTo({ c.x - 20, c.y - k }, { c.x - k, c.y - 20 }, { c.x, c.y - 20 });
    r.cubicTo({ c.x + k, c.y - 20 }, { c.x + 20, c.y - k }, { c.x + 20, c.y });
    r.close();
    std::vector<float> out;
    r.accumulate(out);
    const double area = std::accumulate(out.begin(), out.end(), 0.0);
    const double expected = M_PI * 400;
    QVERIFY2(std::abs(area - expected) / expected < 0.005, qPrintable(QString::number(area)));
}

void TestAtramentBase::coverageWindingOfHole()
{
    // An outer square clockwise and an inner one counter-clockwise: a ring with a hole.
    CoverageRasterizer r;
    r.reset(10, 10);
    r.moveTo({ 0, 0 });
    r.lineTo({ 10, 0 });
    r.lineTo({ 10, 10 });
    r.lineTo({ 0, 10 });
    r.close();
    r.moveTo({ 3, 3 });
    r.lineTo({ 3, 7 });
    r.lineTo({ 7, 7 });
    r.lineTo({ 7, 3 });
    r.close();
    std::vector<std::uint8_t> out(100);
    r.accumulate(out.data(), 10);
    QCOMPARE(int(out[0]), 255);
    QCOMPARE(int(out[5 * 10 + 5]), 0);
    QCOMPARE(int(out[5 * 10 + 2]), 255);
}

void TestAtramentBase::coverageClipsOutside()
{
    // A shape larger than the target, partly to the left and above: full coverage inside.
    CoverageRasterizer r;
    r.reset(4, 4);
    r.moveTo({ -10, -10 });
    r.lineTo({ 2, -10 });
    r.lineTo({ 2, 20 });
    r.lineTo({ -10, 20 });
    r.close();
    std::vector<std::uint8_t> out(16);
    r.accumulate(out.data(), 4);
    for (int y = 0; y < 4; ++y) {
        QCOMPARE(int(out[std::size_t(y * 4 + 0)]), 255);
        QCOMPARE(int(out[std::size_t(y * 4 + 1)]), 255);
        QCOMPARE(int(out[std::size_t(y * 4 + 2)]), 0);
        QCOMPARE(int(out[std::size_t(y * 4 + 3)]), 0);
    }
}

void TestAtramentBase::hashStable()
{
    QCOMPARE(hashString(std::u16string_view(u"ąę")), hashString(std::u16string_view(u"ąę")));
    QVERIFY(hashString(std::u16string_view(u"ą")) != hashString(std::u16string_view(u"a")));
    QVERIFY(hashCombine(1, 2) != hashCombine(2, 1));
    QCOMPARE(hashFloat(0.0f), hashFloat(-0.0f));
}

QTEST_GUILESS_MAIN(TestAtramentBase)
#include "tst_atrament_base.moc"
