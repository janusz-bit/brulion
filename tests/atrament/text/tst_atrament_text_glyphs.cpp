// Atrament's glyph rasterizer and the text system: glyph bitmaps from HarfBuzz outlines, the atlas
// cache of tiles, and the generational cache of paragraph layouts.
#include "text/textsystem.h"

#include "textfixtures.h"

#include <QtTest>

#include <hb.h>

#include <numeric>

using namespace brulion::atrament;
using namespace Qt::StringLiterals;
using textfixtures::BundledFonts;

namespace {

std::uint32_t glyphFor(const FontCollection &fonts, FaceId face, char32_t c)
{
    hb_codepoint_t glyph = 0;
    hb_font_get_nominal_glyph(fonts.hbFont(face), c, &glyph);
    return glyph;
}

double coverageSum(const GlyphBitmap &b)
{
    return std::accumulate(b.coverage.begin(), b.coverage.end(), 0.0) / 255.0;
}

FontRun run(int length, FaceId face, float size = 16, std::uint32_t style = 0)
{
    FontRun r;
    r.length = length;
    r.face = face;
    r.fontSize = size;
    r.styleIndex = style;
    return r;
}

} // namespace

class TestAtramentTextGlyphs : public QObject {
    Q_OBJECT

private Q_SLOTS:
    void initTestCase();
    void capitalA_data();
    void capitalA();
    void space();
    void subpixelVariants();
    void coverageScales();
    void otherOutlines();
    void invalidInput();
    void atlasCaches();
    void atlasTiles();
    void glyphImageInvalid();
    void layoutCacheHits();
    void layoutCacheGenerations();
    void layoutCacheRewraps();
    void layoutCacheKeys();

private:
    BundledFonts m_fonts;
};

void TestAtramentTextGlyphs::initTestCase()
{
    QVERIFY(m_fonts.allLoaded());
}

void TestAtramentTextGlyphs::capitalA_data()
{
    QTest::addColumn<float>("size");
    QTest::newRow("16 px") << 16.0f;
    QTest::newRow("32 px") << 32.0f;
    QTest::newRow("13.5 px") << 13.5f;
}

void TestAtramentTextGlyphs::capitalA()
{
    QFETCH(float, size);
    const FontCollection &fonts = *m_fonts.fonts;
    const std::uint32_t a = glyphFor(fonts, m_fonts.interRegular, U'A');
    QVERIFY(a != 0);
    const GlyphBitmap b = rasterizeGlyph(fonts, m_fonts.interRegular, a, size, 0);
    QVERIFY(b.width > 0 && b.height > 0);
    QCOMPARE(b.coverage.size(), std::size_t(b.width) * std::size_t(b.height));
    // Inter's A: cap height 1490/2048 em, advance 1413/2048 em, on the baseline.
    const float cap = 1490.0f / 2048 * size;
    QVERIFY2(std::abs(float(b.height) - cap) <= 1.5f, qPrintable(u"height %1, cap %2"_s.arg(b.height).arg(cap)));
    QVERIFY(b.width >= int(size * 0.55f) && b.width <= int(size * 0.75f) + 1);
    QVERIFY(std::abs(float(-b.top) - cap) <= 1.0f);
    QVERIFY(b.top + b.height >= 0 && b.top + b.height <= 1); // the baseline at the bottom
    QVERIFY(b.left >= -1 && b.left <= 1);
    // Fully covered pixels inside the strokes, empty ones in the corners.
    QVERIFY(*std::max_element(b.coverage.begin(), b.coverage.end()) == 255);
    QCOMPARE(int(b.coverage[0]), 0);                        // top-left: outside the A
    QCOMPARE(int(b.coverage[std::size_t(b.width - 1)]), 0); // top-right too
}

void TestAtramentTextGlyphs::space()
{
    const FontCollection &fonts = *m_fonts.fonts;
    const std::uint32_t space = glyphFor(fonts, m_fonts.interRegular, U' ');
    QVERIFY(space != 0);
    const GlyphBitmap b = rasterizeGlyph(fonts, m_fonts.interRegular, space, 16, 0);
    QCOMPARE(b.width, 0);
    QCOMPARE(b.height, 0);
    QVERIFY(b.coverage.empty());
}

void TestAtramentTextGlyphs::subpixelVariants()
{
    const FontCollection &fonts = *m_fonts.fonts;
    const std::uint32_t l = glyphFor(fonts, m_fonts.interRegular, U'l');
    std::vector<GlyphBitmap> variants;
    for (int i = 0; i < 4; ++i)
        variants.push_back(rasterizeGlyph(fonts, m_fonts.interRegular, l, 16, float(i) / 4));
    for (int i = 0; i < 4; ++i) {
        for (int j = i + 1; j < 4; ++j)
            QVERIFY2(variants[std::size_t(i)].coverage != variants[std::size_t(j)].coverage
                             || variants[std::size_t(i)].left != variants[std::size_t(j)].left,
                     qPrintable(u"%1 and %2"_s.arg(i).arg(j)));
        // The same amount of ink, moved.
        QVERIFY(std::abs(coverageSum(variants[std::size_t(i)]) - coverageSum(variants[0])) < 0.5);
    }
    // A shift by a quarter moves the ink's centre by a quarter of a pixel (give or take what
    // sampling a 1.3 px stem in whole pixels moves it).
    auto centre = [](const GlyphBitmap &b) {
        double sum = 0;
        double moment = 0;
        for (int y = 0; y < b.height; ++y)
            for (int x = 0; x < b.width; ++x) {
                const double c = b.coverage[std::size_t(y * b.width + x)];
                sum += c;
                moment += c * (x + b.left + 0.5);
            }
        return moment / sum;
    };
    for (int i = 1; i < 4; ++i)
        QVERIFY(std::abs(centre(variants[std::size_t(i)]) - centre(variants[0]) - i * 0.25) < 0.1);
}

void TestAtramentTextGlyphs::coverageScales()
{
    const FontCollection &fonts = *m_fonts.fonts;
    for (const char32_t c : {U'A', U'ż', U'@', U'Ł'}) {
        const std::uint32_t g = glyphFor(fonts, m_fonts.interRegular, c);
        const double s16 = coverageSum(rasterizeGlyph(fonts, m_fonts.interRegular, g, 16, 0));
        const double s32 = coverageSum(rasterizeGlyph(fonts, m_fonts.interRegular, g, 32, 0));
        const double s64 = coverageSum(rasterizeGlyph(fonts, m_fonts.interRegular, g, 64, 0));
        QVERIFY2(std::abs(s32 / s16 - 4) < 0.4, qPrintable(u"%1: %2"_s.arg(uint(c)).arg(s32 / s16)));
        QVERIFY2(std::abs(s64 / s16 - 16) < 1.6, qPrintable(u"%1: %2"_s.arg(uint(c)).arg(s64 / s16)));
    }
}

void TestAtramentTextGlyphs::otherOutlines()
{
    const FontCollection &fonts = *m_fonts.fonts;
    // CFF outlines (cubic curves) of Brulion Math.
    const std::uint32_t sup = glyphFor(fonts, m_fonts.math, 0x2A06);
    QVERIFY(sup != 0);
    const GlyphBitmap math = rasterizeGlyph(fonts, m_fonts.math, sup, 24, 0);
    QVERIFY(math.width > 5 && math.height > 5);
    QVERIFY(coverageSum(math) > 10);
    // Monochrome emoji outlines.
    const std::uint32_t thumb = glyphFor(fonts, m_fonts.emoji, 0x1F44D);
    const GlyphBitmap emoji = rasterizeGlyph(fonts, m_fonts.emoji, thumb, 24, 0.5f);
    QVERIFY(emoji.width > 10 && emoji.height > 10);
    QVERIFY(coverageSum(emoji) > 50);
    // Literata's italic ż, with its descender-free dot above.
    const std::uint32_t z = glyphFor(fonts, m_fonts.literataItalic, U'ż');
    const GlyphBitmap italic = rasterizeGlyph(fonts, m_fonts.literataItalic, z, 20, 0);
    QVERIFY(italic.height > 8 && -italic.top > 8);
}

void TestAtramentTextGlyphs::invalidInput()
{
    const FontCollection &fonts = *m_fonts.fonts;
    const std::uint32_t a = glyphFor(fonts, m_fonts.interRegular, U'A');
    QCOMPARE(rasterizeGlyph(fonts, kNoFace, a, 16, 0).width, 0);
    QCOMPARE(rasterizeGlyph(fonts, m_fonts.interRegular, a, 0, 0).width, 0);
    QCOMPARE(rasterizeGlyph(fonts, m_fonts.interRegular, a, -3, 0).width, 0);
    QCOMPARE(rasterizeGlyph(fonts, m_fonts.interRegular, a, std::nanf(""), 0).width, 0);
    QCOMPARE(rasterizeGlyph(fonts, m_fonts.interRegular, a, 1e9f, 0).width, 0);
    QCOMPARE(rasterizeGlyph(fonts, m_fonts.interRegular, a, 16, std::nanf("")).width, 0);
    QCOMPARE(rasterizeGlyph(fonts, m_fonts.interRegular, 999999, 16, 0).width, 0); // no such glyph
    // .notdef has an outline (a box) in Inter.
    QVERIFY(rasterizeGlyph(fonts, m_fonts.interRegular, 0, 16, 0).width > 0);
    // Tiny sizes still give at most a few pixels.
    const GlyphBitmap tiny = rasterizeGlyph(fonts, m_fonts.interRegular, a, 0.5f, 0.75f);
    QVERIFY(tiny.width <= 2 && tiny.height <= 2);
}

void TestAtramentTextGlyphs::atlasCaches()
{
    TextSystem system(m_fonts.fonts);
    Atlas atlas(256);
    const std::uint32_t a = glyphFor(system.fonts(), m_fonts.interRegular, U'A');
    const std::optional<GlyphImage> first = system.glyph(atlas, m_fonts.interRegular, a, 16, 0);
    QVERIFY(first);
    QVERIFY(!first->tile.isEmpty());
    QCOMPARE(first->tile.kind, AtlasKind::Alpha);
    QVERIFY(!first->color);
    QCOMPARE(atlas.tileCount(), std::size_t(1));
    const std::uint64_t revision = atlas.pages()[first->tile.page].revision;
    // The second time: the same tile, nothing rasterized or copied.
    const std::optional<GlyphImage> second = system.glyph(atlas, m_fonts.interRegular, a, 16, 0);
    QVERIFY(second);
    QCOMPARE(second->tile.x, first->tile.x);
    QCOMPARE(second->tile.y, first->tile.y);
    QCOMPARE(second->left, first->left);
    QCOMPARE(second->top, first->top);
    QCOMPARE(atlas.tileCount(), std::size_t(1));
    QCOMPARE(atlas.pages()[first->tile.page].revision, revision);
    // A size within the quantization step is the same tile; another variant or size is not.
    QCOMPARE(system.glyph(atlas, m_fonts.interRegular, a, 16.01f, 0)->tile.x, first->tile.x);
    QCOMPARE(atlas.tileCount(), std::size_t(1));
    QVERIFY(system.glyph(atlas, m_fonts.interRegular, a, 16, 1));
    QVERIFY(system.glyph(atlas, m_fonts.interRegular, a, 17, 0));
    QVERIFY(system.glyph(atlas, m_fonts.interBold, glyphFor(system.fonts(), m_fonts.interBold, U'A'), 16, 0));
    QCOMPARE(atlas.tileCount(), std::size_t(4));
    // Out-of-range variants are clamped: 7 is 3.
    const auto clamped = system.glyph(atlas, m_fonts.interRegular, a, 16, 7);
    const auto three = system.glyph(atlas, m_fonts.interRegular, a, 16, 3);
    QCOMPARE(clamped->tile.x, three->tile.x);
    QCOMPARE(clamped->tile.y, three->tile.y);
    // Spaces: an empty tile, remembered.
    const std::uint32_t space = glyphFor(system.fonts(), m_fonts.interRegular, U' ');
    const auto empty = system.glyph(atlas, m_fonts.interRegular, space, 16, 0);
    QVERIFY(empty);
    QVERIFY(empty->tile.isEmpty());
    const std::size_t tiles = atlas.tileCount();
    QVERIFY(system.glyph(atlas, m_fonts.interRegular, space, 16, 0)->tile.isEmpty());
    QCOMPARE(atlas.tileCount(), tiles);
    // A second text system on the same atlas knows where the tiles go.
    TextSystem other(m_fonts.fonts);
    const auto shared = other.glyph(atlas, m_fonts.interRegular, a, 16, 0);
    QCOMPARE(shared->left, first->left);
    QCOMPARE(shared->top, first->top);
    QCOMPARE(atlas.tileCount(), tiles);
}

void TestAtramentTextGlyphs::atlasTiles()
{
    TextSystem system(m_fonts.fonts);
    Atlas atlas(256);
    const QString text = u"Zażółć gęślą jaźń ĄĆĘŁŃÓŚŹŻ"_s;
    for (const QChar c : text) {
        const std::uint32_t g = glyphFor(system.fonts(), m_fonts.interRegular, c.unicode());
        for (int variant = 0; variant < 4; ++variant) {
            const auto image = system.glyph(atlas, m_fonts.interRegular, g, 20, variant);
            QVERIFY(image);
            const GlyphBitmap b = rasterizeGlyph(system.fonts(), m_fonts.interRegular, g, 20, float(variant) / 4);
            QCOMPARE(image->tile.width, b.width);
            QCOMPARE(image->tile.height, b.height);
            QCOMPARE(image->left, b.left);
            QCOMPARE(image->top, b.top);
            if (b.width == 0)
                continue;
            // The atlas holds the bitmap.
            const AtlasPage &page = atlas.pages()[image->tile.page];
            for (int y = 0; y < b.height; ++y)
                for (int x = 0; x < b.width; ++x)
                    QCOMPARE(page.alpha(image->tile.x + x, image->tile.y + y),
                             b.coverage[std::size_t(y * b.width + x)]);
        }
    }
}

void TestAtramentTextGlyphs::glyphImageInvalid()
{
    TextSystem system(m_fonts.fonts);
    Atlas atlas;
    QVERIFY(!system.glyph(atlas, kNoFace, 1, 16, 0));
    QVERIFY(!system.glyph(atlas, 9999, 1, 16, 0));
    const auto zero = system.glyph(atlas, m_fonts.interRegular, 5, 0, 0);
    QVERIFY(zero && zero->tile.isEmpty());
    const auto nan = system.glyph(atlas, m_fonts.interRegular, 5, std::nanf(""), 0);
    QVERIFY(nan && nan->tile.isEmpty());
    const auto missing = system.glyph(atlas, m_fonts.interRegular, 999999, 16, 0);
    QVERIFY(missing && missing->tile.isEmpty());
    // A huge size is clamped, not refused.
    const std::uint32_t dot = glyphFor(system.fonts(), m_fonts.interRegular, U'.');
    const auto huge = system.glyph(atlas, m_fonts.interRegular, dot, 1e6f, 0);
    QVERIFY(huge && !huge->tile.isEmpty());
    // No fonts at all.
    TextSystem none(nullptr);
    QCOMPARE(none.fonts().faceCount(), std::size_t(0));
    QVERIFY(!none.glyph(atlas, 0, 1, 16, 0));
}

void TestAtramentTextGlyphs::layoutCacheHits()
{
    TextSystem system(m_fonts.fonts);
    const std::u16string text = u"Zażółć gęślą jaźń";
    const FontRun runs[] = {run(int(text.size()), m_fonts.interRegular)};
    const auto a = system.layout(text, runs, ParagraphStyle{100.0f, 0});
    const auto b = system.layout(text, runs, ParagraphStyle{100.0f, 0});
    QCOMPARE(a.get(), b.get());
    QCOMPARE(system.cachedLayouts(), std::size_t(1));
    QCOMPARE(a->text(), text);
    QCOMPARE(a->lines().size(), std::size_t(2));
    // The same as laying it out directly.
    const TextLayout direct = layoutParagraph(system.fonts(), text, runs, ParagraphStyle{100.0f, 0});
    QCOMPARE(a->lines().size(), direct.lines().size());
    QCOMPARE(a->size(), direct.size());
    // Used in the next frame too: still the same object.
    system.endFrame();
    QCOMPARE(system.layout(text, runs, ParagraphStyle{100.0f, 0}).get(), a.get());
    QCOMPARE(system.cachedLayouts(), std::size_t(1));
}

void TestAtramentTextGlyphs::layoutCacheGenerations()
{
    TextSystem system(m_fonts.fonts);
    const std::u16string one = u"jeden";
    const std::u16string two = u"dwa";
    const FontRun r1[] = {run(int(one.size()), m_fonts.interRegular)};
    const FontRun r2[] = {run(int(two.size()), m_fonts.interRegular)};
    // Frame 1 uses both.
    const auto a = system.layout(one, r1, {});
    const auto b = system.layout(two, r2, {});
    QCOMPARE(system.cachedLayouts(), std::size_t(2));
    system.endFrame();
    QCOMPARE(system.cachedLayouts(), std::size_t(2));
    // Frame 2 uses only the first: the second survives this frame's end only as unused, then goes.
    QCOMPARE(system.layout(one, r1, {}).get(), a.get());
    QCOMPARE(system.cachedLayouts(), std::size_t(2));
    system.endFrame();
    QCOMPARE(system.cachedLayouts(), std::size_t(1));
    QVERIFY(system.layout(two, r2, {}).get() != b.get()); // laid out again
    system.endFrame();
    system.endFrame();
    QCOMPARE(system.cachedLayouts(), std::size_t(0));
    // Layouts handed out stay valid after eviction.
    QCOMPARE(a->text(), one);
    QCOMPARE(b->text(), two);
}

void TestAtramentTextGlyphs::layoutCacheRewraps()
{
    TextSystem system(m_fonts.fonts);
    const std::u16string text = u"Zażółć gęślą jaźń, źdźbło chrząszcza w trzcinie";
    const FontRun runs[] = {run(10, m_fonts.interRegular, 16, 1), run(int(text.size()), m_fonts.literataItalic, 18, 2)};
    const auto wide = system.layout(text, runs, ParagraphStyle{400.0f, 0});
    // Another width: rewrapped from the cached shaping, equal to a fresh layout.
    for (const float width : {50.0f, 120.0f, 400.0f, 10.0f}) {
        const auto l = system.layout(text, runs, ParagraphStyle{width, 0});
        const TextLayout direct = layoutParagraph(system.fonts(), text, runs, ParagraphStyle{width, 0});
        QCOMPARE(l->lines().size(), direct.lines().size());
        QCOMPARE(l->size(), direct.size());
        QCOMPARE(l->glyphs().size(), direct.glyphs().size());
        for (std::size_t i = 0; i < direct.glyphs().size(); ++i) {
            QCOMPARE(l->glyphs()[i].glyph, direct.glyphs()[i].glyph);
            QVERIFY(std::abs(l->glyphs()[i].position.x - direct.glyphs()[i].position.x) < 1e-3f);
            QVERIFY(std::abs(l->glyphs()[i].position.y - direct.glyphs()[i].position.y) < 1e-3f);
        }
    }
    QCOMPARE(system.layout(text, runs, ParagraphStyle{400.0f, 0}).get(), wide.get());
    QCOMPARE(system.cachedLayouts(), std::size_t(4));
}

void TestAtramentTextGlyphs::layoutCacheKeys()
{
    TextSystem system(m_fonts.fonts);
    const std::u16string text = u"Łódź";
    FontRun r = run(int(text.size()), m_fonts.interRegular);
    const auto base = system.layout(text, std::span(&r, 1), {});
    auto differs = [&](const FontRun &other, const ParagraphStyle &style = {}) {
        return system.layout(text, std::span(&other, 1), style).get() != base.get();
    };
    FontRun x = r;
    x.styleIndex = 3;
    QVERIFY(differs(x));
    x = r;
    x.fontSize = 17;
    QVERIFY(differs(x));
    x = r;
    x.face = m_fonts.interBold;
    QVERIFY(differs(x));
    x = r;
    x.features = u"-kern";
    QVERIFY(differs(x));
    x = r;
    x.language = u"pl";
    QVERIFY(differs(x));
    QVERIFY(differs(r, ParagraphStyle{50.0f, 0}));
    QVERIFY(differs(r, ParagraphStyle{std::nullopt, 30}));
    QCOMPARE(system.layout(u"Lodz", std::span(&r, 1), {})->text(), std::u16string(u"Lodz"));
    QVERIFY(!differs(r));
    // Several runs of different shape with the same total.
    const FontRun two[] = {run(2, m_fonts.interRegular), run(2, m_fonts.interRegular)};
    QVERIFY(system.layout(text, two, {}).get() != base.get());
    // Empty text and no runs.
    QCOMPARE(system.layout(u"", {}, {})->lines().size(), std::size_t(1));
}

QTEST_GUILESS_MAIN(TestAtramentTextGlyphs)
#include "tst_atrament_text_glyphs.moc"
