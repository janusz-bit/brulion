// Inline boxes and glyph rectangles in Atrament's paragraph layout (docs/ATRAMENT.md "Text"):
// TextLayout::glyphRects (what a background shape is drawn around: one rectangle per line, the
// caret positions in x, the runs' own fonts' ascent and descent in y), and runs laid out as one box
// of a given size (FontRun::inlineBox) — the room reserved, the line's metrics, wrapping as one
// unit, caret stops and hit tests that see one cluster, the cache and rewrapping, malformed boxes.
#include "text/segmentation.h"
#include "text/textlayout.h"
#include "text/textsystem.h"

#include "textfixtures.h"

#include <QtTest>

#include <cmath>
#include <limits>

using namespace brulion::atrament;
using namespace Qt::StringLiterals;
using textfixtures::BundledFonts;
using textfixtures::qs;
using textfixtures::u16;

namespace {

FontRun run(int length, FaceId face, float size = 16, std::uint32_t style = 0)
{
    FontRun r;
    r.length = length;
    r.face = face;
    r.fontSize = size;
    r.styleIndex = style;
    return r;
}

FontRun box(int length, FaceId face, InlineBox size, std::uint32_t style = 0)
{
    FontRun r = run(length, face, 16, style);
    r.inlineBox = size;
    return r;
}

// Cut points of `text` into runs: [0, cuts[0]), [cuts[0], cuts[1]), …; `boxAt` the run that is a box.
std::vector<FontRun> runsOf(int length, std::initializer_list<int> cuts, FaceId face, int boxAt = -1,
                            InlineBox size = {})
{
    std::vector<FontRun> runs;
    int at = 0;
    std::uint32_t index = 0;
    auto add = [&](int end) {
        runs.push_back(int(index) == boxAt ? box(end - at, face, size, index) : run(end - at, face, 16, index));
        ++index;
        at = end;
    };
    for (int c : cuts)
        add(c);
    add(length);
    return runs;
}

bool near(float a, float b, float tolerance = 0.01f)
{
    return std::abs(a - b) <= tolerance;
}

} // namespace

class TestAtramentTextBoxes : public QObject {
    Q_OBJECT
private Q_SLOTS:
    void initTestCase();
    void glyphRectsAroundGlyphs();
    void glyphRectsAcrossLines();
    void glyphRectsTallestRun();
    void glyphRectsOwnFontsNotFallbacks();
    void glyphRectsEdges();
    void boxReservesRoom();
    void boxRaisesTheLine();
    void boxWrapsAsOneUnit();
    void boxNarrowerLineKeepsItWhole();
    void objectReplacementBreaks();
    void caretStops();
    void hitTestsSeeOneCluster();
    void upDownSkipBoxes();
    void cacheKnowsBoxes();
    void rewrapKeepsBoxes();
    void malformedBoxes();

private:
    BundledFonts m_fonts;
};

void TestAtramentTextBoxes::initTestCase()
{
    QVERIFY(m_fonts.allLoaded());
}

void TestAtramentTextBoxes::glyphRectsAroundGlyphs()
{
    // A word in the middle of a line 30 px tall: the rectangle hugs the font's ascent and descent
    // around the baseline, not the line box; x from caret to caret.
    const std::u16string t = u"Zażółć gęślą jaźń";
    const int from = int(t.find(u"gęślą"));
    const int to = from + 5;
    const std::vector<FontRun> runs = runsOf(int(t.size()), {from, to}, m_fonts.interRegular);
    const TextLayout l = layoutParagraph(*m_fonts.fonts, t, runs, ParagraphStyle{std::nullopt, 30});
    QCOMPARE(l.lines().size(), std::size_t(1));
    const TextLine &line = l.lines()[0];
    const std::vector<LineRect> rects = l.glyphRects({from, to});
    QCOMPARE(rects.size(), std::size_t(1));
    const FontMetrics m = m_fonts.fonts->metrics(m_fonts.interRegular);
    const Rect r = rects[0].rect;
    QCOMPARE(rects[0].line, 0);
    QVERIFY(near(r.left(), l.caretX(from)));
    QVERIFY(near(r.right(), l.caretX(to)));
    QVERIFY(near(r.top(), line.baseline - m.ascent * 16));
    QVERIFY(near(r.bottom(), line.baseline + m.descent * 16));
    QVERIFY(r.height() < line.height - 5); // the line is 30 px, Inter's extent ≈ 19 px
    // The same range reversed; an empty range; a range past the end, clipped.
    QCOMPARE(l.glyphRects({to, from}).size(), std::size_t(1));
    QVERIFY(l.glyphRects({from, from}).empty());
    const std::vector<LineRect> tail = l.glyphRects({from, 1000});
    QCOMPARE(tail.size(), std::size_t(1));
    QVERIFY(near(tail[0].rect.right(), line.width));
    // The runs as laid out: the caller's three, in order, with their style indices.
    QCOMPARE(l.runs().size(), std::size_t(3));
    QCOMPARE(l.runs()[1].start, from);
    QCOMPARE(l.runs()[1].end, to);
    QCOMPARE(l.runs()[1].styleIndex, 1u);
    QVERIFY(!l.runs()[1].inlineBox);
    QVERIFY(near(l.runs()[1].ascent, m.ascent * 16));
}

void TestAtramentTextBoxes::glyphRectsAcrossLines()
{
    // A long range wrapped over several lines: one rectangle per line, the first ending at the
    // line's visible end (no hanging space), the next starting at the line's start.
    const std::u16string t = u"Pchnąć w tę łódź jeża lub ośm skrzyń fig, zażółć gęślą jaźń";
    const int from = int(t.find(u"łódź"));
    const int to = int(t.find(u"gęślą")) + 5;
    const FontRun runs[] = {run(int(t.size()), m_fonts.interRegular)};
    const TextLayout l = layoutParagraph(*m_fonts.fonts, t, runs, ParagraphStyle{120.0f, 0});
    QVERIFY(l.lines().size() >= 3);
    const std::vector<LineRect> rects = l.glyphRects({from, to});
    int touched = 0;
    for (const TextLine &line : l.lines())
        touched += from < line.end && to > line.start ? 1 : 0;
    QCOMPARE(int(rects.size()), touched);
    for (std::size_t i = 0; i < rects.size(); ++i) {
        const TextLine &line = l.lines()[std::size_t(rects[i].line)];
        if (i > 0) {
            QCOMPARE(rects[i].line, rects[i - 1].line + 1);
            QVERIFY(near(rects[i].rect.left(), 0));
        }
        if (i + 1 < rects.size())
            QVERIFY2(near(rects[i].rect.right(), line.width), qPrintable(qs(t.substr(std::size_t(line.start)))));
        QVERIFY(rects[i].rect.right() <= line.width + 0.01f);
        QVERIFY(rects[i].rect.top() >= line.top - 0.01f);
        QVERIFY(rects[i].rect.bottom() <= line.top + line.height + 0.01f);
    }
    // Only the space a line hangs at its end: nothing to draw.
    const TextLine &first = l.lines()[0];
    QCOMPARE(t[std::size_t(first.next - 1)], u' ');
    QVERIFY(l.glyphRects({first.next - 1, first.next}).empty());
    // Polish over a mandatory break: the break's character is no piece.
    const std::u16string broken = u"źdźbło\nżółw";
    const FontRun r2[] = {run(int(broken.size()), m_fonts.interRegular)};
    const TextLayout b = layoutParagraph(*m_fonts.fonts, broken, r2, ParagraphStyle{});
    const std::vector<LineRect> two = b.glyphRects({0, int(broken.size())});
    QCOMPARE(two.size(), std::size_t(2));
    QVERIFY(near(two[0].rect.right(), b.lines()[0].width));
    QVERIFY(b.glyphRects({6, 7}).empty()); // the "\n" alone
}

void TestAtramentTextBoxes::glyphRectsTallestRun()
{
    // Two sizes on one line: a range over both reaches as far as the larger; one over the small
    // run only keeps the small run's extent, though the line is taller.
    const std::u16string t = u"mały DUŻY mały";
    std::vector<FontRun> runs = {run(5, m_fonts.interRegular, 14, 0), run(4, m_fonts.interRegular, 28, 1),
                                 run(5, m_fonts.interRegular, 14, 2)};
    const TextLayout l = layoutParagraph(*m_fonts.fonts, t, runs, ParagraphStyle{});
    const FontMetrics m = m_fonts.fonts->metrics(m_fonts.interRegular);
    const float baseline = l.lines()[0].baseline;
    const std::vector<LineRect> small = l.glyphRects({0, 4});
    QCOMPARE(small.size(), std::size_t(1));
    QVERIFY(near(small[0].rect.top(), baseline - m.ascent * 14));
    QVERIFY(near(small[0].rect.bottom(), baseline + m.descent * 14));
    const std::vector<LineRect> both = l.glyphRects({0, 9});
    QCOMPARE(both.size(), std::size_t(1));
    QVERIFY(near(both[0].rect.top(), baseline - m.ascent * 28));
    QVERIFY(near(both[0].rect.bottom(), baseline + m.descent * 28));
}

void TestAtramentTextBoxes::glyphRectsOwnFontsNotFallbacks()
{
    // An emoji in an Inter run is drawn from the emoji face, but the shape around it keeps Inter's
    // extent (the run's own font), as the line's metrics do.
    const std::u16string t = u"ok \U0001F680 start";
    const FontRun runs[] = {run(int(t.size()), m_fonts.interRegular)};
    const TextLayout l = layoutParagraph(*m_fonts.fonts, t, runs, ParagraphStyle{});
    bool fallback = false;
    for (const PositionedGlyph &g : l.glyphs())
        fallback = fallback || g.face == m_fonts.emoji;
    QVERIFY(fallback);
    const FontMetrics m = m_fonts.fonts->metrics(m_fonts.interRegular);
    const std::vector<LineRect> rects = l.glyphRects({3, 5});
    QCOMPARE(rects.size(), std::size_t(1));
    QVERIFY(near(rects[0].rect.height(), (m.ascent + m.descent) * 16));
}

void TestAtramentTextBoxes::glyphRectsEdges()
{
    // Empty text, a collection without fonts: no rectangles, no crash.
    const FontRun none[] = {run(0, m_fonts.interRegular)};
    const TextLayout empty = layoutParagraph(*m_fonts.fonts, u"", none, ParagraphStyle{});
    QVERIFY(empty.glyphRects({0, 10}).empty());
    FontCollection nothing;
    const std::u16string t = u"żółć";
    const FontRun r[] = {run(int(t.size()), kNoFace)};
    const TextLayout bare = layoutParagraph(nothing, t, r, ParagraphStyle{});
    QVERIFY(bare.glyphRects({0, 4}).empty()); // no advances: nothing is half a pixel wide
    QVERIFY(bare.glyphRects({-5, 2}).empty());
}

void TestAtramentTextBoxes::boxReservesRoom()
{
    // "Zadanie \uFFFC gotowe": U+FFFC laid out as a 20 × (12 + 4) box.
    const std::u16string t = u"Zadanie \uFFFC gotowe";
    const int at = 8;
    const InlineBox size{20, 12, 4};
    const std::vector<FontRun> runs = runsOf(int(t.size()), {at, at + 1}, m_fonts.interRegular, 1, size);
    const TextLayout l = layoutParagraph(*m_fonts.fonts, t, runs, ParagraphStyle{});
    QVERIFY(near(l.caretX(at + 1) - l.caretX(at), 20));
    for (const PositionedGlyph &g : l.glyphs())
        QVERIFY(g.cluster != at); // no glyph for the box
    QCOMPARE(l.inlineBoxes().size(), std::size_t(1));
    const PlacedInlineBox &b = l.inlineBoxes()[0];
    QCOMPARE(b.start, at);
    QCOMPARE(b.end, at + 1);
    QCOMPARE(b.line, 0);
    QCOMPARE(b.styleIndex, 1u);
    const float baseline = l.lines()[0].baseline;
    QVERIFY(near(b.bounds.left(), l.caretX(at)));
    QVERIFY(near(b.bounds.width(), 20));
    QVERIFY(near(b.bounds.top(), baseline - 12));
    QVERIFY(near(b.bounds.bottom(), baseline + 4));
    QVERIFY(l.runs()[1].inlineBox);
    QVERIFY(near(l.runs()[1].ascent, 12));
    // The text around it is where it would be with a 20 px gap.
    const FontRun plain[] = {run(at, m_fonts.interRegular)};
    const float before = layoutParagraph(*m_fonts.fonts, t.substr(0, std::size_t(at)), plain, ParagraphStyle{})
                                 .caretX(at); // with its space
    QVERIFY(near(l.caretX(at), before, 0.5f));
    const std::u16string rest = t.substr(std::size_t(at + 1));
    const FontRun restRun[] = {run(int(rest.size()), m_fonts.interRegular)};
    const float after = layoutParagraph(*m_fonts.fonts, rest, restRun, ParagraphStyle{}).maxContentWidth();
    QVERIFY(near(l.maxContentWidth(), before + 20 + after, 0.5f));
    // A box around glyph rectangles: the box's own extent.
    const std::vector<LineRect> rects = l.glyphRects({at, at + 1});
    QCOMPARE(rects.size(), std::size_t(1));
    QVERIFY(near(rects[0].rect.top(), baseline - 12));
    QVERIFY(near(rects[0].rect.height(), 16));
}

void TestAtramentTextBoxes::boxRaisesTheLine()
{
    // A box taller than the text makes its line taller (its ascent and descent count as a
    // font's); a fixed line height stays fixed, the baseline moving to keep the box inside.
    const std::u16string t = u"ą\uFFFCę";
    const InlineBox tall{10, 40, 10};
    const std::vector<FontRun> runs = runsOf(3, {1, 2}, m_fonts.interRegular, 1, tall);
    const TextLayout l = layoutParagraph(*m_fonts.fonts, t, runs, ParagraphStyle{});
    const TextLine &line = l.lines()[0];
    QVERIFY(near(line.ascent, 40));
    QVERIFY(near(line.descent, 10));
    QVERIFY(line.height >= 50);
    const TextLayout fixed = layoutParagraph(*m_fonts.fonts, t, runs, ParagraphStyle{std::nullopt, 24});
    QVERIFY(near(fixed.lines()[0].height, 24));
    // A small box changes nothing of the line.
    const std::vector<FontRun> smallRuns = runsOf(3, {1, 2}, m_fonts.interRegular, 1, InlineBox{8, 6, 1});
    const FontRun plain[] = {run(3, m_fonts.interRegular)};
    const TextLayout small = layoutParagraph(*m_fonts.fonts, t, smallRuns, ParagraphStyle{});
    const TextLayout text = layoutParagraph(*m_fonts.fonts, u"ąxę", plain, ParagraphStyle{});
    QCOMPARE(small.lines()[0].height, text.lines()[0].height);
    QCOMPARE(small.lines()[0].baseline, text.lines()[0].baseline);
}

void TestAtramentTextBoxes::boxWrapsAsOneUnit()
{
    // A page icon as Brulion's renderer writes it: three no-break spaces reserved for the icon, a
    // fourth gluing it to the link's text. At every width the box stays whole, on the line of the
    // word it is glued to.
    const std::u16string t = u"Zobacz też stronę \u00a0\u00a0\u00a0\u00a0Dom i ogród, potem źródła";
    const int at = int(t.find(u'\u00a0'));
    const int word = int(t.find(u"Dom"));
    const std::vector<FontRun> runs =
            runsOf(int(t.size()), {at, at + 3}, m_fonts.interRegular, 1, InlineBox{14, 12, 2});
    const TextLayout unwrapped = layoutParagraph(*m_fonts.fonts, t, runs, ParagraphStyle{});
    // (Down to the widest unbreakable piece: narrower, a word breaks anywhere.)
    int moved = 0;
    for (float width = unwrapped.maxContentWidth() + 10; width >= unwrapped.minContentWidth(); width -= 3) {
        const TextLayout l = layoutParagraph(*m_fonts.fonts, t, runs, ParagraphStyle{width, 0});
        for (const TextLine &line : l.lines()) {
            QVERIFY2(!(line.start > at && line.start < at + 3), qPrintable(u"width %1"_s.arg(width)));
            QVERIFY(!(line.end > at && line.end < at + 3));
        }
        QCOMPARE(l.inlineBoxes().size(), std::size_t(1));
        const int boxLine = l.inlineBoxes()[0].line;
        QCOMPARE(l.lineIndex(word), boxLine); // glued to "Dom"
        moved += boxLine > 0 && l.lines()[std::size_t(boxLine)].start == at ? 1 : 0;
    }
    QVERIFY(moved > 0); // at some widths the box starts a line of its own
    // The box is part of the widest unbreakable piece ("stronę" ends before it: a space).
    const float glued = layoutParagraph(*m_fonts.fonts, t.substr(std::size_t(at)),
                                        std::vector<FontRun>{box(3, m_fonts.interRegular, InlineBox{14, 12, 2}),
                                                             run(int(t.size()) - at - 3, m_fonts.interRegular)},
                                        ParagraphStyle{0.0f, 0})
                                .lines()[0]
                                .width;
    QVERIFY(unwrapped.minContentWidth() >= glued - 0.01f);
}

void TestAtramentTextBoxes::boxNarrowerLineKeepsItWhole()
{
    // A line narrower than the box: the box is never split (a word's emergency break goes by
    // graphemes, the box is one).
    const std::u16string t = u"\u00a0\u00a0\u00a0\u00a0\u00a0";
    const std::vector<FontRun> runs = {box(5, m_fonts.interRegular, InlineBox{50, 10, 2})};
    const TextLayout l = layoutParagraph(*m_fonts.fonts, t, runs, ParagraphStyle{10.0f, 0});
    QCOMPARE(l.lines().size(), std::size_t(1));
    QVERIFY(near(l.lines()[0].width, 50));
    // A box glued to the letters around it (a no-break space between "ż" and "ź"), narrower than
    // all of them: the piece breaks by graphemes, the box one of them — a line of its own.
    const std::u16string glued = u"ż\u00a0ź";
    const TextLayout g =
            layoutParagraph(*m_fonts.fonts, glued, runsOf(3, {1, 2}, m_fonts.interRegular, 1, InlineBox{30, 10, 2}),
                            ParagraphStyle{5.0f, 0});
    QCOMPARE(g.lines().size(), std::size_t(3));
    QCOMPARE(g.lines()[1].start, 1);
    QCOMPARE(g.lines()[1].end, 2);
    QVERIFY(near(g.lines()[1].width, 30));
}

void TestAtramentTextBoxes::objectReplacementBreaks()
{
    // U+FFFC breaks like an ideograph (UAX #14 CB: a break before and after), except where the
    // neighbours forbid one (no break before a comma, none after a no-break space).
    const std::vector<BreakKind> b = lineBreaks(u"ab\uFFFCcd");
    QCOMPARE(b[2], BreakKind::Allowed);
    QCOMPARE(b[3], BreakKind::Allowed);
    const std::vector<BreakKind> comma = lineBreaks(u"ab\uFFFC,d");
    QCOMPARE(comma[3], BreakKind::None);
    const std::vector<BreakKind> glue = lineBreaks(u"ab\u00a0\uFFFCd");
    QCOMPARE(glue[3], BreakKind::None);
    // So a narrow line puts the object alone on a line.
    const std::u16string t = u"zażółć\uFFFCgęślą";
    const TextLayout l = layoutParagraph(*m_fonts.fonts, t,
                                         runsOf(int(t.size()), {6, 7}, m_fonts.interRegular, 1, InlineBox{16, 12, 3}),
                                         ParagraphStyle{20.0f, 0});
    QVERIFY(l.lines().size() >= 3);
    bool alone = false;
    for (const TextLine &line : l.lines())
        alone = alone || (line.start == 6 && line.end == 7);
    QVERIFY(alone);
}

void TestAtramentTextBoxes::caretStops()
{
    // "ź" written decomposed (z + U+0301) is one grapheme; the box over three no-break spaces is
    // one stop.
    const std::u16string t = u"źa\u00a0\u00a0\u00a0b";
    const std::vector<FontRun> runs = runsOf(int(t.size()), {3, 6}, m_fonts.interRegular, 1, InlineBox{18, 12, 2});
    const TextLayout l = layoutParagraph(*m_fonts.fonts, t, runs, ParagraphStyle{});
    QCOMPARE(l.nextCaretStop(0), 2);
    QCOMPARE(l.nextCaretStop(2), 3);
    QCOMPARE(l.nextCaretStop(3), 6); // over the box
    QCOMPARE(l.nextCaretStop(4), 6); // from inside it: to its end
    QCOMPARE(l.nextCaretStop(6), 7);
    QCOMPARE(l.nextCaretStop(7), 7);
    QCOMPARE(l.previousCaretStop(7), 6);
    QCOMPARE(l.previousCaretStop(6), 3); // over the box
    QCOMPARE(l.previousCaretStop(5), 3);
    QCOMPARE(l.previousCaretStop(3), 2);
    QCOMPARE(l.previousCaretStop(2), 0);
    QCOMPARE(l.previousCaretStop(0), 0);
    QCOMPARE(l.previousCaretStop(-4), 0);
    QCOMPARE(l.nextCaretStop(99), 7);
    // Offsets inside the box are at its start.
    QCOMPARE(l.caretX(4), l.caretX(3));
    QCOMPARE(l.caretX(5), l.caretX(3));
    QVERIFY(near(l.caretX(6) - l.caretX(3), 18));
    // Without boxes the stops are the grapheme boundaries.
    const FontRun plain[] = {run(int(t.size()), m_fonts.interRegular)};
    const TextLayout p = layoutParagraph(*m_fonts.fonts, t, plain, ParagraphStyle{});
    for (int o = 0; o < int(t.size()); ++o) {
        QCOMPARE(p.nextCaretStop(o), nextGraphemeBoundary(t, o));
        QCOMPARE(p.previousCaretStop(o + 1), previousGraphemeBoundary(t, o + 1));
    }
    // A box at the very start and one at the very end; two boxes side by side.
    const std::u16string two = u"\uFFFC\uFFFC";
    const TextLayout b = layoutParagraph(*m_fonts.fonts, two,
                                         std::vector<FontRun>{box(1, m_fonts.interRegular, InlineBox{10, 8, 2}),
                                                              box(1, m_fonts.interRegular, InlineBox{12, 8, 2}, 1)},
                                         ParagraphStyle{});
    QCOMPARE(b.inlineBoxes().size(), std::size_t(2));
    QCOMPARE(b.nextCaretStop(0), 1);
    QCOMPARE(b.nextCaretStop(1), 2);
    QCOMPARE(b.previousCaretStop(2), 1);
    QCOMPARE(b.previousCaretStop(1), 0);
    QVERIFY(near(b.caretX(2), 22));
}

void TestAtramentTextBoxes::hitTestsSeeOneCluster()
{
    const std::u16string t = u"ćma\u00a0\u00a0\u00a0ślę";
    const std::vector<FontRun> runs = runsOf(int(t.size()), {3, 6}, m_fonts.interRegular, 1, InlineBox{30, 12, 2});
    const TextLayout l = layoutParagraph(*m_fonts.fonts, t, runs, ParagraphStyle{});
    const float x0 = l.caretX(3);
    const float y = l.lines()[0].baseline - 4;
    // Left half of the box: before it; right half: after it; never inside.
    QCOMPARE(l.offsetAt({x0 + 2, y}), 3);
    QCOMPARE(l.offsetAt({x0 + 14, y}), 3);
    QCOMPARE(l.offsetAt({x0 + 16, y}), 6);
    QCOMPARE(l.offsetAt({x0 + 29, y}), 6);
    for (float x = 0; x < l.maxContentWidth() + 5; x += 0.5f) {
        const int o = l.offsetAt({x, y});
        QVERIFY2(o <= 3 || o >= 6, qPrintable(u"x %1 → %2"_s.arg(x).arg(o)));
    }
    // A selection over the box covers its width.
    const std::vector<Rect> sel = l.selectionRects({3, 6});
    QCOMPARE(sel.size(), std::size_t(1));
    QVERIFY(near(sel[0].width(), 30));
}

void TestAtramentTextBoxes::upDownSkipBoxes()
{
    // ↑/↓ land on a caret stop: never inside a box on the other line.
    const std::u16string t = u"\u00a0\u00a0\u00a0\u00a0\u00a0\u00a0 dół\nżółw";
    const std::vector<FontRun> runs = runsOf(int(t.size()), {6}, m_fonts.interRegular, 0, InlineBox{60, 12, 2});
    const TextLayout l = layoutParagraph(*m_fonts.fonts, t, runs, ParagraphStyle{});
    QCOMPARE(l.lines().size(), std::size_t(2));
    const int below = int(t.find(u"żółw"));
    for (int o = below; o <= int(t.size()); ++o) {
        const int up = l.offsetAbove(o, l.caretX(o));
        QVERIFY(up == 0 || up >= 6);
    }
}

void TestAtramentTextBoxes::cacheKnowsBoxes()
{
    // The text system's cache keys a layout by its runs' boxes too.
    TextSystem system(m_fonts.fonts);
    const std::u16string t = u"Zażółć \uFFFC jaźń";
    const std::vector<FontRun> a = runsOf(int(t.size()), {7, 8}, m_fonts.interRegular, 1, InlineBox{20, 12, 3});
    const std::vector<FontRun> b = runsOf(int(t.size()), {7, 8}, m_fonts.interRegular, 1, InlineBox{40, 12, 3});
    std::vector<FontRun> c = a;
    c[1].inlineBox.reset();
    const auto la = system.layout(t, a, ParagraphStyle{});
    const auto lb = system.layout(t, b, ParagraphStyle{});
    const auto lc = system.layout(t, c, ParagraphStyle{});
    QVERIFY(la != lb && la != lc && lb != lc);
    QVERIFY(near(lb->maxContentWidth() - la->maxContentWidth(), 20));
    QVERIFY(lc->inlineBoxes().empty());
    QCOMPARE(system.layout(t, a, ParagraphStyle{}), la);
    // Another width rewraps the shaped layout with its boxes.
    const auto narrow = system.layout(t, b, ParagraphStyle{60.0f, 0});
    QCOMPARE(narrow->inlineBoxes().size(), std::size_t(1));
    QVERIFY(near(narrow->inlineBoxes()[0].bounds.width(), 40));
}

void TestAtramentTextBoxes::rewrapKeepsBoxes()
{
    const std::u16string t = u"Pchnąć w tę \uFFFC łódź jeża lub \u00a0\u00a0\u00a0\u00a0ośm skrzyń fig";
    const int first = int(t.find(u'\uFFFC'));
    const int second = int(t.find(u'\u00a0'));
    std::vector<FontRun> runs = {
            run(first, m_fonts.interRegular, 16, 0), box(1, m_fonts.interRegular, InlineBox{16, 12, 3}, 1),
            run(second - first - 1, m_fonts.interRegular, 16, 2), box(3, m_fonts.interRegular, InlineBox{14, 11, 2}, 3),
            run(int(t.size()) - second - 3, m_fonts.interRegular, 16, 4)};
    const TextLayout wide = layoutParagraph(*m_fonts.fonts, t, runs, ParagraphStyle{});
    for (float width : {200.0f, 140.0f, 90.0f, 40.0f}) {
        const TextLayout fresh = layoutParagraph(*m_fonts.fonts, t, runs, ParagraphStyle{width, 0});
        const TextLayout re = rewrapParagraph(*m_fonts.fonts, wide, runs, ParagraphStyle{width, 0});
        QCOMPARE(re.lines().size(), fresh.lines().size());
        for (std::size_t i = 0; i < fresh.lines().size(); ++i) {
            QCOMPARE(re.lines()[i].start, fresh.lines()[i].start);
            QCOMPARE(re.lines()[i].next, fresh.lines()[i].next);
        }
        QCOMPARE(re.inlineBoxes().size(), std::size_t(2));
        for (std::size_t i = 0; i < 2; ++i) {
            QCOMPARE(re.inlineBoxes()[i].line, fresh.inlineBoxes()[i].line);
            QCOMPARE(re.inlineBoxes()[i].bounds, fresh.inlineBoxes()[i].bounds);
        }
    }
}

void TestAtramentTextBoxes::malformedBoxes()
{
    // Sizes that are not numbers or negative count as 0; an empty box run is skipped; a box past
    // the text is clipped; a box over all of the text is the whole line.
    const float nan = std::numeric_limits<float>::quiet_NaN();
    const float inf = std::numeric_limits<float>::infinity();
    const std::u16string t = u"ab\uFFFCcd";
    for (const InlineBox &bad : {InlineBox{nan, nan, nan}, InlineBox{-5, -1, -2}, InlineBox{inf, inf, inf}}) {
        const TextLayout l =
                layoutParagraph(*m_fonts.fonts, t, runsOf(5, {2, 3}, m_fonts.interRegular, 1, bad), ParagraphStyle{});
        QCOMPARE(l.inlineBoxes().size(), std::size_t(1));
        QCOMPARE(l.caretX(3), l.caretX(2));
        QVERIFY(std::isfinite(l.size().width) && std::isfinite(l.size().height));
    }
    const TextLayout empty = layoutParagraph(*m_fonts.fonts, t,
                                             std::vector<FontRun>{run(2, m_fonts.interRegular),
                                                                  box(0, m_fonts.interRegular, InlineBox{10, 5, 5}),
                                                                  run(3, m_fonts.interRegular)},
                                             ParagraphStyle{});
    QVERIFY(empty.inlineBoxes().empty());
    const TextLayout past = layoutParagraph(
            *m_fonts.fonts, t,
            std::vector<FontRun>{run(4, m_fonts.interRegular), box(10, m_fonts.interRegular, InlineBox{10, 5, 5})},
            ParagraphStyle{});
    QCOMPARE(past.inlineBoxes().size(), std::size_t(1));
    QCOMPARE(past.inlineBoxes()[0].end, 5);
    QCOMPARE(past.nextCaretStop(4), 5);
    const TextLayout all =
            layoutParagraph(*m_fonts.fonts, t, std::vector<FontRun>{box(5, m_fonts.interRegular, InlineBox{33, 9, 3})},
                            ParagraphStyle{1.0f, 0});
    QCOMPARE(all.lines().size(), std::size_t(1));
    QVERIFY(near(all.size().width, 33));
    QVERIFY(all.glyphs().empty());
    // Without fonts at all.
    FontCollection nothing;
    const TextLayout bare =
            layoutParagraph(nothing, t, runsOf(5, {2, 3}, kNoFace, 1, InlineBox{10, 5, 5}), ParagraphStyle{});
    QCOMPARE(bare.inlineBoxes().size(), std::size_t(1));
    QVERIFY(near(bare.caretX(3) - bare.caretX(2), 10));
}

QTEST_GUILESS_MAIN(TestAtramentTextBoxes)
#include "tst_atrament_text_boxes.moc"
