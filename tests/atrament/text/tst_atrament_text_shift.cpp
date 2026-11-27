// Baseline shifts in Atrament's paragraph layout (docs/ATRAMENT.md "Text", FontRun::baselineShift):
// a superscript's glyphs raised from the line's baseline and a subscript's lowered, the run's
// ascent and descent moved with them (the line grows to hold them, glyphRects follow), carets and
// hit tests unchanged in x, the layout cache telling shifts apart, rewrapping keeping them,
// malformed shifts ignored. Polish text and a formula's symbols (Brulion Math's fallback).
#include "text/textlayout.h"
#include "text/textsystem.h"

#include "textfixtures.h"

#include <QtTest>

#include <cmath>
#include <limits>

using namespace brulion::atrament;
using textfixtures::BundledFonts;

namespace {

FontRun run(int length, FaceId face, float size = 16, float shift = 0, std::uint32_t style = 0)
{
    FontRun r;
    r.length = length;
    r.face = face;
    r.fontSize = size;
    r.baselineShift = shift;
    r.styleIndex = style;
    return r;
}

// The y of the first glyph of cluster `offset`.
float glyphY(const TextLayout &l, int offset)
{
    for (const PositionedGlyph &g : l.glyphs())
        if (g.cluster == offset)
            return g.position.y;
    return std::numeric_limits<float>::quiet_NaN();
}

} // namespace

class TestAtramentTextShift : public QObject {
    Q_OBJECT
private Q_SLOTS:
    void superscriptRaised();
    void subscriptLowered();
    void lineGrowsToHoldTheShift();
    void glyphRectsFollow();
    void cacheAndRewrap();
    void malformedShift();

private:
    BundledFonts m_fonts;
};

void TestAtramentTextShift::superscriptRaised()
{
    QVERIFY(m_fonts.allLoaded());
    // "x² + gęś": the "2" a raised run at 0.7 of the size.
    const std::u16string text = u"x2 + gęś";
    const FontRun runs[] = {run(1, m_fonts.interItalic), run(1, m_fonts.interRegular, 11.2f, 6.4f),
                            run(6, m_fonts.interRegular)};
    const TextLayout l = layoutParagraph(*m_fonts.fonts, text, runs, {});
    QCOMPARE(l.lines().size(), std::size_t(1));
    const float baseline = l.lines()[0].baseline;
    QCOMPARE(glyphY(l, 0), baseline);
    QVERIFY(std::abs(glyphY(l, 1) - (baseline - 6.4f)) < 1e-3f);
    QCOMPARE(glyphY(l, 3), baseline);
    // The runs as laid out carry the shift; x is not moved by it.
    QCOMPARE(l.runs()[1].baselineShift, 6.4f);
    const TextLayout flat = layoutParagraph(*m_fonts.fonts, text,
                                            std::span<const FontRun>(std::vector<FontRun>{runs[0], run(1, m_fonts.interRegular, 11.2f), runs[2]}), {});
    QCOMPARE(l.caretX(2), flat.caretX(2));
    QCOMPARE(l.offsetAt({l.caretX(2) + 0.1f, baseline}), 2);
}

void TestAtramentTextShift::subscriptLowered()
{
    const std::u16string text = u"H2O, źdźbło";
    const FontRun runs[] = {run(1, m_fonts.interRegular), run(1, m_fonts.interRegular, 11.2f, -3.2f),
                            run(9, m_fonts.interRegular)};
    const TextLayout l = layoutParagraph(*m_fonts.fonts, text, runs, {});
    const float baseline = l.lines()[0].baseline;
    QVERIFY(std::abs(glyphY(l, 1) - (baseline + 3.2f)) < 1e-3f);
    QCOMPARE(l.runs()[1].baselineShift, -3.2f);
}

void TestAtramentTextShift::lineGrowsToHoldTheShift()
{
    const FontMetrics m = m_fonts.fonts->metrics(m_fonts.interRegular);
    const std::u16string text = u"ab";
    const FontRun plain[] = {run(1, m_fonts.interRegular), run(1, m_fonts.interRegular, 8)};
    const TextLayout base = layoutParagraph(*m_fonts.fonts, text, plain, {});
    // Raised by 20 px: the line's ascent is the raised run's (8 px Inter's ascent + 20).
    const FontRun up[] = {run(1, m_fonts.interRegular), run(1, m_fonts.interRegular, 8, 20)};
    const TextLayout raised = layoutParagraph(*m_fonts.fonts, text, up, {});
    QVERIFY(std::abs(raised.lines()[0].ascent - (m.ascent * 8 + 20)) < 1e-3f);
    QVERIFY(raised.size().height > base.size().height + 5);
    QCOMPARE(raised.lines()[0].descent, base.lines()[0].descent);
    // Lowered by 20 px: the descent grows.
    const FontRun down[] = {run(1, m_fonts.interRegular), run(1, m_fonts.interRegular, 8, -20)};
    const TextLayout lowered = layoutParagraph(*m_fonts.fonts, text, down, {});
    QVERIFY(std::abs(lowered.lines()[0].descent - (m.descent * 8 + 20)) < 1e-3f);
    QCOMPARE(lowered.lines()[0].ascent, base.lines()[0].ascent);
    // A fixed line height keeps the line; the glyphs still move.
    ParagraphStyle fixed;
    fixed.lineHeight = 24;
    const TextLayout tight = layoutParagraph(*m_fonts.fonts, text, up, fixed);
    QCOMPARE(tight.lines()[0].height, 24.0f);
    QVERIFY(std::abs(glyphY(tight, 1) - (tight.lines()[0].baseline - 20)) < 1e-3f);
}

void TestAtramentTextShift::glyphRectsFollow()
{
    const std::u16string text = u"√x̄ i x²"; // ² at 7
    QCOMPARE(text.size(), std::size_t(8));
    const FontRun runs[] = {run(7, m_fonts.interRegular), run(1, m_fonts.interRegular, 11, 6)};
    const TextLayout l = layoutParagraph(*m_fonts.fonts, text, runs, {});
    const std::vector<LineRect> plain = l.glyphRects({4, 5});
    const std::vector<LineRect> raised = l.glyphRects({7, 8});
    QCOMPARE(raised.size(), std::size_t(1));
    const FontMetrics m = m_fonts.fonts->metrics(m_fonts.interRegular);
    // The raised run's box: its own font's ascent above the shifted baseline.
    QVERIFY(std::abs(raised[0].rect.top() - (l.lines()[0].baseline - 6 - m.ascent * 11)) < 1e-3f);
    QVERIFY(raised[0].rect.bottom() < plain[0].rect.bottom());
    // The math sign comes from the fallback face, the shift stays the run's.
    QVERIFY(std::isfinite(glyphY(l, 0)));
}

void TestAtramentTextShift::cacheAndRewrap()
{
    TextSystem system(m_fonts.fonts);
    const std::u16string text = u"a² b² c² d² e² f² gęś";
    std::vector<FontRun> runs;
    for (int i = 0; i < 6; ++i) {
        runs.push_back(run(1, m_fonts.interRegular));
        runs.push_back(run(2, m_fonts.interRegular, 11, 6));
    }
    runs.push_back(run(3, m_fonts.interRegular));
    ParagraphStyle style;
    const auto a = system.layout(text, runs, style);
    std::vector<FontRun> other = runs;
    other[1].baselineShift = 3;
    const auto b = system.layout(text, other, style);
    QVERIFY(a.get() != b.get());
    QCOMPARE(system.layout(text, runs, style).get(), a.get());
    // Wrapped narrower (reshaping nothing): the shifted glyphs stay raised on every line.
    style.wrapWidth = 40;
    const auto narrow = system.layout(text, runs, style);
    QVERIFY(narrow->lines().size() > 2);
    for (const TextLine &line : narrow->lines()) {
        for (std::uint32_t g = line.firstGlyph; g < line.firstGlyph + line.glyphCount; ++g) {
            const PositionedGlyph &glyph = narrow->glyphs()[g];
            const bool shifted = text[std::size_t(glyph.cluster)] == u'²' || text[std::size_t(glyph.cluster)] == u' ';
            if (text[std::size_t(glyph.cluster)] == u'²')
                QVERIFY(std::abs(glyph.position.y - (line.baseline - 6)) < 1e-3f);
            else if (!shifted)
                QCOMPARE(glyph.position.y, line.baseline);
        }
    }
}

void TestAtramentTextShift::malformedShift()
{
    const std::u16string text = u"zażółć";
    for (const float bad : {std::numeric_limits<float>::quiet_NaN(), std::numeric_limits<float>::infinity()}) {
        const FontRun runs[] = {run(6, m_fonts.interRegular, 16, bad)};
        const TextLayout l = layoutParagraph(*m_fonts.fonts, text, runs, {});
        QCOMPARE(glyphY(l, 0), l.lines()[0].baseline);
        QVERIFY(std::isfinite(l.size().height));
    }
}

QTEST_GUILESS_MAIN(TestAtramentTextShift)
#include "tst_atrament_text_shift.moc"
