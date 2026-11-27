// Vector paths, icons and the text decorations they came with, painted by a window (docs/ATRAMENT.md
// "Vector paths and icons", "Text"): Window::fillPath / strokePath / paintIcon as path primitives —
// pixels of the CPU renderer at scale 1 and 2, the transform, the content mask, an icon fitted and
// snapped into its square, its outline reused across frames — and a Text's spans raising a
// superscript (TextStyle::baselineShift) and overlining a radicand (TextStyle::overline).
#include "text/fonts.h"
#include "ui/atrament.h"
#include "ui/testplatform.h"

#include <QtTest>

#include <functional>
#include <memory>

using namespace brulion::atrament;

namespace {

std::shared_ptr<FontCollection> loadFonts()
{
    static std::shared_ptr<FontCollection> fonts = [] {
        auto f = std::make_shared<FontCollection>();
        const std::string dir = BRULION_SOURCE_DIR "/assets/fonts/";
        f->addFontFile(dir + "ui/BrulionInter-Regular.ttf");
        f->addFontFile(dir + "BrulionMath.otf");
        f->setFallbackFamilies({u"Brulion Math", u"Brulion Inter"});
        return f;
    }();
    return fonts;
}

// A view drawing whatever the test gives it.
struct Painting {
    std::function<void(Rect, Window &)> paint;
    std::u16string text;
    std::vector<TextSpan> spans;
    std::shared_ptr<TextLayoutHandle> layout = std::make_shared<TextLayoutHandle>();
    AnyElement render(Window &, Context<Painting> &)
    {
        Painting *self = this;
        return div()
                .sizeFull()
                .bg(white())
                .flexCol()
                .child(canvas([self](Rect b, Window &w, App &) {
                           if (self->paint)
                               self->paint(b, w);
                       })
                               .w(200)
                               .h(100))
                .child(div().p(10).textSize(20).lineHeight(40).fontFamily(u"Brulion Inter").child(
                        Text(text).spans(spans).trackLayout(*layout)));
    }
};

StrokeStyle widthOf(float w)
{
    StrokeStyle s;
    s.width = w;
    return s;
}

bool dark(Pixel p)
{
    return p.r < 80 && p.g < 80 && p.b < 80;
}

bool white(Pixel p)
{
    return p.r > 245 && p.g > 245 && p.b > 245;
}

} // namespace

class TestAtramentVectorPaint : public QObject {
    Q_OBJECT
private Q_SLOTS:
    void fillAndStrokeInPixels();
    void scaledAndMasked();
    void iconFittedAndReused();
    void superscriptAndOverline();
};

void TestAtramentVectorPaint::fillAndStrokeInPixels()
{
    auto platform = std::make_shared<TestPlatform>();
    App app(platform, loadFonts());
    TestWindow &tw = platform->newWindow({240, 200}, 1);
    auto view = app.newEntity<Painting>();
    view.update(app, [](Painting &p, Context<Painting> &) {
        p.paint = [](Rect b, Window &w) {
            VectorPath square;
            square.addRect(Rect::fromXYWH(0, 0, 20, 20));
            w.fillPath(square, {1, b.origin + Point{10, 10}}, black());
            VectorPath line;
            line.moveTo({0, 0});
            line.lineTo({60, 0});
            // Twice the scale: a 4 px wide line, 120 px long, from (60, 50).
            w.strokePath(line, {2, b.origin + Point{60, 50}}, widthOf(2), black());
            StrokeStyle dashed = widthOf(2);
            dashed.dashes = {5, 5};
            w.strokePath(line, {1, b.origin + Point{10, 80}}, dashed, black());
        };
    });
    app.openWindow(tw, view);
    tw.settle();
    QVERIFY(dark(tw.pixel(15, 15)) && dark(tw.pixel(29, 29)));
    QVERIFY(white(tw.pixel(31, 15)));
    QVERIFY(dark(tw.pixel(100, 49)) && dark(tw.pixel(100, 51)) && dark(tw.pixel(179, 50)));
    QVERIFY(white(tw.pixel(100, 53)) && white(tw.pixel(100, 46)) && white(tw.pixel(182, 50)));
    QVERIFY(dark(tw.pixel(12, 80)) && white(tw.pixel(17, 80)) && dark(tw.pixel(22, 80)));
    // Paths in the scene: one per call.
    int paths = 0;
    for (const Primitive &p : tw.lastScene()->primitives())
        paths += std::holds_alternative<PathPrimitive>(p) ? 1 : 0;
    QCOMPARE(paths, 3);
}

void TestAtramentVectorPaint::scaledAndMasked()
{
    auto platform = std::make_shared<TestPlatform>();
    App app(platform, loadFonts());
    TestWindow &tw = platform->newWindow({240, 200}, 2);
    auto view = app.newEntity<Painting>();
    view.update(app, [](Painting &p, Context<Painting> &) {
        p.paint = [](Rect b, Window &w) {
            VectorPath square;
            square.addRect(Rect::fromXYWH(0, 0, 40, 40));
            // Cut to the left half by a content mask.
            w.pushContentMask(Rect::fromXYWH(b.left(), b.top(), 30, 100));
            w.fillPath(square, {1, b.origin + Point{10, 10}}, rgb(0x1864ab));
            w.popContentMask();
        };
    });
    app.openWindow(tw, view);
    tw.settle();
    // Device pixels: the square from 20 to 100, cut at 60.
    const Pixel inside = tw.pixel(40, 40);
    QVERIFY(inside.b > 150 && inside.r < 60);
    QVERIFY(white(tw.pixel(70, 40)));
    QVERIFY(white(tw.pixel(15, 40)));
}

void TestAtramentVectorPaint::iconFittedAndReused()
{
    const std::shared_ptr<const VectorIcon> icon = VectorIcon::parseSvg(
            "<svg viewBox='0 0 24 24' fill='none' stroke='#000' stroke-width='2' stroke-linecap='round'>"
            "<path d='M4 12h16'/><circle cx='12' cy='5' r='1.5' fill='#000'/></svg>");
    QVERIFY(icon);
    auto platform = std::make_shared<TestPlatform>();
    App app(platform, loadFonts());
    TestWindow &tw = platform->newWindow({240, 200}, 1);
    auto view = app.newEntity<Painting>();
    view.update(app, [icon](Painting &p, Context<Painting> &) {
        // A 48 × 30 box: the icon in its middle square of 30 (at 9 … 39 in x).
        p.paint = [icon](Rect b, Window &w) { w.paintIcon(*icon, Rect::fromXYWH(b.left() + 20, b.top() + 20, 48, 30), rgb(0xc92a2a)); };
    });
    app.openWindow(tw, view);
    tw.settle();
    const auto red = [](Pixel p) { return p.r > 150 && p.g < 90 && p.b < 90; };
    // The line across the middle (× 1.25): from 29 + 5 to 29 + 25 at y 20 + 15, 2.5 px wide, round.
    QVERIFY(red(tw.pixel(44, 35)) && red(tw.pixel(52, 35)));
    QVERIFY(white(tw.pixel(44, 39)) && white(tw.pixel(30, 35)) && white(tw.pixel(57, 35)));
    // The dot above it (filled and stroked).
    QVERIFY(red(tw.pixel(44, 26)));
    // A second frame copies the same outline: the same primitives, in place.
    std::vector<PathPrimitive> first;
    for (const Primitive &p : tw.lastScene()->primitives())
        if (const auto *path = std::get_if<PathPrimitive>(&p))
            first.push_back(*path);
    QCOMPARE(first.size(), std::size_t(2)); // what is filled, what is stroked
    const auto cached = icon->outline(30);
    QCOMPARE(first[1].points.size(), cached->stroke.points.size());
    QCOMPARE(first[1].points[0], cached->stroke.points[0] + Point(29, 20));
    view.update(app, [](Painting &, Context<Painting> &cx) { cx.notify(); });
    tw.settle();
    QCOMPARE(icon->outline(30).get(), cached.get());
}

void TestAtramentVectorPaint::superscriptAndOverline()
{
    auto platform = std::make_shared<TestPlatform>();
    App app(platform, loadFonts());
    TestWindow &tw = platform->newWindow({240, 200}, 1);
    auto view = app.newEntity<Painting>();
    view.update(app, [](Painting &p, Context<Painting> &) {
        p.text = u"x2 + √ab"; // "2" a superscript, "ab" overlined
        TextStyleRefinement sup;
        sup.fontSize = 14.0f;
        sup.baselineShift = 8.0f;
        TextStyleRefinement over;
        over.overline = true;
        p.spans = {{Range{1, 2}, sup}, {Range{6, 8}, over}};
    });
    app.openWindow(tw, view);
    tw.settle();
    const TextLayoutHandle &h = *view.read(app).layout;
    QVERIFY(h.layout());
    const TextLayout &l = *h.layout();
    const TextLine &line = l.lines()[0];
    float y0 = 0, y1 = 0;
    for (const PositionedGlyph &g : l.glyphs()) {
        if (g.cluster == 0)
            y0 = g.position.y;
        if (g.cluster == 1)
            y1 = g.position.y;
    }
    QCOMPARE(y0, line.baseline);
    QVERIFY(std::abs(y1 - (line.baseline - 8)) < 1e-3f);
    // The overline: a quad over "ab" at the font's ascent above the baseline, as wide as the run.
    const float x6 = h.bounds().left() + l.caretX(6);
    const float x8 = h.bounds().left() + l.caretX(8);
    bool found = false;
    for (const Primitive &p : tw.lastScene()->primitives()) {
        const auto *q = std::get_if<Quad>(&p);
        if (!q || std::abs(q->bounds.left() - x6) > 0.5f || std::abs(q->bounds.right() - x8) > 0.5f)
            continue;
        found = q->bounds.bottom() < h.bounds().top() + line.baseline - 10 && q->bounds.height() >= 1;
    }
    QVERIFY(found);
}

QTEST_MAIN(TestAtramentVectorPaint)
#include "tst_atrament_vectorpaint.moc"
