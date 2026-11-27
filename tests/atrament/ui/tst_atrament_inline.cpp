// Inline boxes in text (docs/ATRAMENT.md "Text", M2 "Inline boxes in text"): background shapes
// under a span's glyphs — a pill or a rounded box around the glyphs (the fonts' ascent and descent,
// not the line box), one per line when a span wraps, adjacent spans with equal backgrounds one
// shape, padding, inset and the line's extent, drawn under the glyphs and (with paintText's layers
// apart) under a selection — and inline objects: the room the line reserves, the painter called
// with the object's bounds in window coordinates, em sizes, wrapping as one unit, a link's hit
// region over it. Shapes are checked in the scene against the paragraph's own layout, and in pixels
// (a pill's corner is the page's, a square box's is the shape's). Polish text throughout.
#include "text/fonts.h"
#include "text/textsystem.h"
#include "ui/atrament.h"
#include "ui/testplatform.h"

#include <QtTest>

#include <algorithm>
#include <functional>
#include <memory>
#include <string>
#include <vector>

using namespace brulion::atrament;

namespace {

std::shared_ptr<FontCollection> loadFonts()
{
    static std::shared_ptr<FontCollection> fonts = [] {
        auto f = std::make_shared<FontCollection>();
        const std::string dir = BRULION_SOURCE_DIR "/assets/fonts/";
        f->addFontFile(dir + "ui/BrulionInter-Regular.ttf");
        f->addFontFile(dir + "ui/BrulionInter-Bold.ttf");
        f->setFallbackFamilies({u"Brulion Inter"});
        return f;
    }();
    return fonts;
}

constexpr float kPad = 10; // the paragraph's top-left in the window
constexpr float kSize = 16;
constexpr float kLine = 28;

const Color kPill = rgb(0xd9480f);
const Color kMark = rgb(0xffd43b);

// A paragraph with spans, `width` wide, in Inter 16 on lines of 28 px.
struct Para {
    std::u16string text;
    std::vector<TextSpan> spans;
    float width = 420;
    float size = kSize;
    std::vector<TextLink> links;
    int clicks = 0;
    int maxLines = 0;
    TextLayoutHandle handle;
    AnyElement render(Window &, Context<Para> &cx)
    {
        Text t = Text(text).spans(spans).maxLines(maxLines).trackLayout(handle);
        App *app = &cx.app();
        const EntityId id = cx.entityId();
        for (const TextLink &l : links)
            std::move(t).link(l.range, [app, id](const ClickEvent &, Window &, App &) {
                app->updateEntity<Para>(id, [](Para &p, Context<Para> &c) {
                    ++p.clicks;
                    c.notify();
                });
            });
        return div().sizeFull().bg(white()).p(kPad).child(div().w(width)
                                                                  .textSize(size)
                                                                  .lineHeight(kLine)
                                                                  .textColor(black())
                                                                  .fontFamily(u"Brulion Inter")
                                                                  .child(std::move(t)));
    }
};

struct Harness {
    std::shared_ptr<TestPlatform> platform = std::make_shared<TestPlatform>();
    App app{platform, loadFonts()};
    TestWindow &tw;
    Entity<Para> para;
    Harness(std::u16string text, std::vector<TextSpan> spans, float width = 420, Size size = {460, 200})
        : tw(platform->newWindow(size, 1))
    {
        para = app.newEntity<Para>();
        para.update(app, [&](Para &p, Context<Para> &) {
            p.text = std::move(text);
            p.spans = std::move(spans);
            p.width = width;
        });
        app.openWindow(tw, para);
        tw.settle();
    }
    void change(const std::function<void(Para &)> &f)
    {
        para.update(app, [&](Para &p, Context<Para> &cx) {
            f(p);
            cx.notify();
        });
        tw.settle();
    }
    std::vector<Quad> quads(Color c) const
    {
        std::vector<Quad> out;
        for (const Primitive &p : tw.lastScene()->primitives())
            if (const Quad *q = std::get_if<Quad>(&p); q && q->background.color == c)
                out.push_back(*q);
        return out;
    }
    std::size_t indexOf(Color c) const
    {
        const auto &all = tw.lastScene()->primitives();
        for (std::size_t i = 0; i < all.size(); ++i)
            if (const Quad *q = std::get_if<Quad>(&all[i]); q && q->background.color == c)
                return i;
        return all.size();
    }
    std::size_t firstGlyph() const
    {
        const auto &all = tw.lastScene()->primitives();
        for (std::size_t i = 0; i < all.size(); ++i)
            if (std::holds_alternative<MonochromeSprite>(all[i]))
                return i;
        return all.size();
    }
    // The paragraph's layout as Text makes it: the text cut at `cuts` into runs of Inter Regular
    // (one style per piece), `boxes` the pieces laid out as inline boxes.
    std::shared_ptr<const TextLayout> layout(const std::u16string &text, std::vector<int> cuts, float width,
                                             std::vector<std::pair<int, InlineBox>> boxes = {})
    {
        cuts.push_back(0);
        cuts.push_back(int(text.size()));
        std::sort(cuts.begin(), cuts.end());
        cuts.erase(std::unique(cuts.begin(), cuts.end()), cuts.end());
        std::vector<FontRun> runs;
        for (std::size_t i = 0; i + 1 < cuts.size(); ++i) {
            FontRun r;
            r.length = cuts[i + 1] - cuts[i];
            r.face = app.textSystem().fonts().match(u"Brulion Inter");
            r.fontSize = kSize;
            r.styleIndex = std::uint32_t(i);
            for (const auto &[at, size] : boxes)
                if (at == cuts[i])
                    r.inlineBox = size;
            runs.push_back(r);
        }
        return app.textSystem().layout(text, runs, ParagraphStyle{width, kLine});
    }
};

TextSpan background(Range r, TextBackground b)
{
    TextSpan s;
    s.range = r;
    s.style.background = std::optional<TextBackground>(b);
    return s;
}

TextBackground pill(Color c, std::uint32_t group = 0)
{
    TextBackground b;
    b.color = c;
    b.pill = true;
    b.group = group;
    return b;
}

Range rangeOf(std::u16string_view text, std::u16string_view piece)
{
    const int at = int(text.find(piece));
    return {at, at + int(piece.size())};
}

bool near(float a, float b, float tolerance = 1.01f)
{
    return std::abs(a - b) <= tolerance;
}

// What inline object painters were told, in order.
std::vector<InlineObjectPaint> &painted()
{
    static std::vector<InlineObjectPaint> calls;
    return calls;
}

InlineObjectPainter recorder(Color c)
{
    return [c](const InlineObjectPaint &p, Window &w) {
        painted().push_back(p);
        w.paintQuad(p.bounds, Background::solid(c), {}, transparent(), {});
    };
}

TextSpan object(Range r, InlineObjectSize size, Color c)
{
    TextSpan s;
    s.range = r;
    s.object = InlineObject{size, recorder(c)};
    return s;
}

} // namespace

class TestAtramentInline : public QObject {
    Q_OBJECT
private Q_SLOTS:
    void init() { painted().clear(); }
    void pillAroundGlyphs();
    void pillAcrossLines();
    void adjacentSpansOneShape();
    void paddingInsetAndExtent();
    void boxCorners();
    void pixelsOfAPill();
    void layersUnderASelection();
    void inlineObjectPainted();
    void inlineObjectInEms();
    void inlineObjectWrapsAsOneUnit();
    void spansAroundAnObject();
    void linkOverAnObject();
    void maxLinesEndsWithAnEllipsis();
};

void TestAtramentInline::pillAroundGlyphs()
{
    const std::u16string t = u"Spotkanie #zespół jutro o 9";
    const Range tag = rangeOf(t, u"#zespół");
    Harness h(t, {background(tag, pill(kPill))});
    const std::vector<Quad> q = h.quads(kPill);
    QCOMPARE(q.size(), std::size_t(1));
    const auto l = h.layout(t, {tag.start, tag.end}, 420);
    const FontMetrics m = h.app.textSystem().fonts().metrics(h.app.textSystem().fonts().match(u"Brulion Inter"));
    const float baseline = kPad + l->lines()[0].baseline;
    const Rect r = q[0].bounds;
    QVERIFY(near(r.left(), kPad + l->caretX(tag.start)));
    QVERIFY(near(r.right(), kPad + l->caretX(tag.end)));
    QVERIFY(near(r.top(), baseline - m.ascent * kSize));
    QVERIFY(near(r.bottom(), baseline + m.descent * kSize));
    QVERIFY(r.height() < kLine - 5); // the glyphs' extent, not the line box
    QVERIFY(near(q[0].cornerRadii.topLeft, r.height() / 2, 0.01f));
    QVERIFY(near(q[0].cornerRadii.bottomRight, r.height() / 2, 0.01f));
    // Drawn under the glyphs.
    QVERIFY(h.indexOf(kPill) < h.firstGlyph());
}

void TestAtramentInline::pillAcrossLines()
{
    // A chip with spaces inside wraps: one whole pill per line it shows on.
    const std::u16string t = u"Zebranie: \u00a0spotkanie z całym zespołem projektowym w czwartek\u00a0 i potem.";
    const Range chip = rangeOf(t, u"\u00a0spotkanie z całym zespołem projektowym w czwartek\u00a0");
    Harness h(t, {background(chip, pill(kPill))}, 150);
    const std::vector<Quad> q = h.quads(kPill);
    const auto l = h.layout(t, {chip.start, chip.end}, 150);
    const std::vector<LineRect> pieces = l->glyphRects(chip);
    QVERIFY(pieces.size() >= 2);
    QCOMPARE(q.size(), pieces.size());
    for (std::size_t i = 0; i < q.size(); ++i) {
        QVERIFY(near(q[i].bounds.left(), kPad + pieces[i].rect.left()));
        QVERIFY(near(q[i].bounds.right(), kPad + pieces[i].rect.right()));
        QVERIFY(near(q[i].bounds.top(), kPad + pieces[i].rect.top()));
        QVERIFY(near(q[i].cornerRadii.topLeft, q[i].bounds.height() / 2, 0.01f)); // each a whole pill
    }
    QVERIFY(near(q[1].bounds.left(), kPad));
    QVERIFY(near(q[0].bounds.right(), kPad + l->lines()[std::size_t(pieces[0].line)].width));
    QVERIFY(q[1].bounds.top() > q[0].bounds.bottom());
}

void TestAtramentInline::adjacentSpansOneShape()
{
    // A highlight with a bold word inside: three pieces of text, one shape.
    const std::u16string t = u"To ważne zdanie z pogrubieniem w środku.";
    const Range mark = rangeOf(t, u"ważne zdanie z pogrubieniem w środku");
    TextBackground box;
    box.color = kMark;
    box.radius = 4;
    TextSpan bold;
    bold.range = rangeOf(t, u"pogrubieniem");
    bold.style.fontWeight = FontWeight{FontWeight::Bold};
    Harness h(t, {background(mark, box), bold});
    QCOMPARE(h.quads(kMark).size(), std::size_t(1));
    QVERIFY(h.quads(kMark)[0].bounds.width() > 200);

    // Two tags side by side with one look: two shapes when their groups differ, one when equal.
    const std::u16string tags = u"Tagi: #jeden#dwa i reszta";
    const Range one = rangeOf(tags, u"#jeden");
    const Range two = rangeOf(tags, u"#dwa");
    h.change([&](Para &p) {
        p.text = tags;
        p.spans = {background(one, pill(kPill, 1)), background(two, pill(kPill, 2))};
    });
    QCOMPARE(h.quads(kPill).size(), std::size_t(2));
    h.change([&](Para &p) { p.spans = {background(one, pill(kPill, 7)), background(two, pill(kPill, 7))}; });
    QCOMPARE(h.quads(kPill).size(), std::size_t(1));
    // Equal groups but another look (a colour): two shapes.
    h.change([&](Para &p) { p.spans = {background(one, pill(kPill, 7)), background(two, pill(kMark, 7))}; });
    QCOMPARE(h.quads(kPill).size(), std::size_t(1));
    QCOMPARE(h.quads(kMark).size(), std::size_t(1));
    // A transparent background draws nothing.
    h.change([&](Para &p) { p.spans = {background(one, pill(transparent()))}; });
    QVERIFY(h.quads(transparent()).empty());
    QVERIFY(h.quads(kPill).empty());
}

void TestAtramentInline::paddingInsetAndExtent()
{
    const std::u16string t = u"Kod fn(źdźbło) w zdaniu";
    const Range code = rangeOf(t, u"fn(źdźbło)");
    Harness h(t, {background(code, pill(kPill))});
    const Rect plain = h.quads(kPill).at(0).bounds;
    TextBackground padded = pill(kPill);
    padded.paddingX = 3;
    h.change([&](Para &p) { p.spans = {background(code, padded)}; });
    const Rect wide = h.quads(kPill).at(0).bounds;
    QVERIFY(near(wide.left(), plain.left() - 3));
    QVERIFY(near(wide.right(), plain.right() + 3));
    QVERIFY(near(wide.height(), plain.height()));
    // The line's layout is the same: padding reserves no room.
    TextBackground inset = pill(kPill);
    inset.inset = 2;
    h.change([&](Para &p) { p.spans = {background(code, inset)}; });
    const Rect thin = h.quads(kPill).at(0).bounds;
    QVERIFY(near(thin.height(), plain.height() - 4));
    QVERIFY(near(thin.left(), plain.left()));
    inset.inset = -3;
    h.change([&](Para &p) { p.spans = {background(code, inset)}; });
    QVERIFY(near(h.quads(kPill).at(0).bounds.height(), plain.height() + 6));
    // The line's extent: as tall as the line box (28 px).
    TextBackground line = pill(kPill);
    line.extent = TextBackgroundExtent::Line;
    h.change([&](Para &p) { p.spans = {background(code, line)}; });
    const Rect tall = h.quads(kPill).at(0).bounds;
    QVERIFY(near(tall.height(), kLine));
    QVERIFY(near(tall.top(), kPad));
    // An inset larger than the glyphs: a 1 px line, never upside down.
    inset.inset = 40;
    h.change([&](Para &p) { p.spans = {background(code, inset)}; });
    QCOMPARE(h.quads(kPill).size(), std::size_t(1));
    QVERIFY(h.quads(kPill)[0].bounds.height() <= 1.01f);
}

void TestAtramentInline::boxCorners()
{
    const std::u16string t = u"Zwykły `kod` i ==zakreślenie==";
    TextBackground box;
    box.color = kMark;
    box.radius = 4;
    Harness h(t, {background(rangeOf(t, u"`kod`"), box)});
    const Quad q = h.quads(kMark).at(0);
    QCOMPARE(q.cornerRadii.topLeft, 4.0f);
    QCOMPARE(q.cornerRadii.bottomLeft, 4.0f);
    // A radius larger than the shape is clamped to half its height (a pill).
    box.radius = 100;
    h.change([&](Para &p) { p.spans = {background(rangeOf(t, u"`kod`"), box)}; });
    const Quad big = h.quads(kMark).at(0);
    QVERIFY(near(big.cornerRadii.topLeft, big.bounds.height() / 2, 0.01f));
}

void TestAtramentInline::pixelsOfAPill()
{
    // A pill over six no-break spaces (no ink of its own), opaque on white: its middle is the
    // pill's colour, the corner of its bounding box is the page's; a square box fills its corner.
    const std::u16string t = u"ą\u00a0\u00a0\u00a0\u00a0\u00a0\u00a0ę";
    const Range room{1, 7};
    Harness h(t, {background(room, pill(kPill))});
    const Rect r = h.quads(kPill).at(0).bounds;
    const Pixel middle = h.tw.pixel(int(r.left() + r.width() / 2), int(r.top() + r.height() / 2));
    QVERIFY2(middle.r > 200 && middle.g < 110 && middle.b < 40,
             qPrintable(QStringLiteral("%1 %2 %3").arg(middle.r).arg(middle.g).arg(middle.b)));
    const Pixel corner = h.tw.pixel(int(r.left()), int(r.top()));
    QVERIFY2(corner.g > 230 && corner.b > 230, qPrintable(QStringLiteral("%1 %2").arg(corner.g).arg(corner.b)));
    // The middle of its left end is inside the round end.
    const Pixel end = h.tw.pixel(int(r.left()) + 1, int(r.top() + r.height() / 2));
    QVERIFY(end.r > 200 && end.g < 160);
    TextBackground square;
    square.color = kPill;
    h.change([&](Para &p) { p.spans = {background(room, square)}; });
    const Rect s = h.quads(kPill).at(0).bounds;
    const Pixel filled = h.tw.pixel(int(s.left()), int(s.top()));
    QVERIFY(filled.r > 200 && filled.g < 110);
}

void TestAtramentInline::layersUnderASelection()
{
    // paintText's layers apart: the shapes, a selection over them, the glyphs over both.
    struct View {
        AnyElement render(Window &, Context<View> &)
        {
            return div().sizeFull().bg(white()).child(
                    canvas([](Rect bounds, Window &w, App &app) {
                        const std::u16string t = u"Zaznacz ważny fragment";
                        FontRun r;
                        r.length = int(t.size());
                        r.face = app.textSystem().fonts().match(u"Brulion Inter");
                        r.fontSize = kSize;
                        const FontRun runs[] = {r};
                        const auto l = app.textSystem().layout(t, runs, {});
                        TextBackground b;
                        b.color = kMark;
                        b.radius = 3;
                        const TextPaintStyle styles[] = {{black(), {}, false, b}};
                        w.paintTextBackgrounds(*l, bounds.origin, styles);
                        for (const Rect &s : l->selectionRects({0, 7}))
                            w.paintQuad(s.translated(bounds.origin), Background::solid(kPill), {}, transparent(), {});
                        w.paintTextGlyphs(*l, bounds.origin, styles);
                    })
                            .w(300)
                            .h(40));
        }
    };
    auto platform = std::make_shared<TestPlatform>();
    App app(platform, loadFonts());
    TestWindow &tw = platform->newWindow({320, 60}, 1);
    app.openWindow(tw, app.newEntity<View>());
    tw.settle();
    const auto &all = tw.lastScene()->primitives();
    std::size_t shape = all.size(), selection = all.size(), glyph = all.size();
    for (std::size_t i = 0; i < all.size(); ++i) {
        if (const Quad *q = std::get_if<Quad>(&all[i])) {
            if (q->background.color == kMark && shape == all.size())
                shape = i;
            if (q->background.color == kPill && selection == all.size())
                selection = i;
        } else if (std::holds_alternative<MonochromeSprite>(all[i]) && glyph == all.size()) {
            glyph = i;
        }
    }
    QVERIFY(shape < selection);
    QVERIFY(selection < glyph);
    QVERIFY(glyph < all.size());
}

void TestAtramentInline::inlineObjectPainted()
{
    // A checkbox at U+FFFC: 18 px wide, 14 above the baseline, 4 below.
    const std::u16string t = u"Zadanie \uFFFC gotowe na jutro";
    const Range at{8, 9};
    const Color ink = rgb(0x2f9e44);
    Harness h(t, {object(at, InlineObjectSize{18, 14, 4}, ink)});
    QVERIFY(!painted().empty());
    const InlineObjectPaint p = painted().back();
    QCOMPARE(p.range.start, 8);
    QCOMPARE(p.range.end, 9);
    QVERIFY(near(p.bounds.width(), 18, 0.01f));
    QVERIFY(near(p.bounds.height(), 18, 0.01f));
    QVERIFY(near(p.bounds.top(), p.baseline - 14, 0.01f));
    QCOMPARE(p.color, black());
    QCOMPARE(p.fontSize, kSize);
    const auto l = h.layout(t, {8, 9}, 420, {{8, InlineBox{18, 14, 4}}});
    QVERIFY(near(p.bounds.left(), kPad + l->caretX(8), 0.01f));
    QVERIFY(near(p.baseline, kPad + l->lines()[0].baseline, 0.01f));
    QVERIFY(near(l->caretX(9) - l->caretX(8), 18, 0.01f)); // the room reserved
    // The painter's quad is drawn after the text (over its shapes and glyphs).
    QVERIFY(h.indexOf(ink) > h.firstGlyph());
    QCOMPARE(h.quads(ink).size(), std::size_t(1));
    // An object without a painter keeps its room and draws nothing.
    painted().clear();
    h.change([&](Para &p2) {
        TextSpan s;
        s.range = at;
        s.object = InlineObject{InlineObjectSize{18, 14, 4}, {}};
        p2.spans = {s};
    });
    QVERIFY(painted().empty());
    QVERIFY(h.quads(ink).empty());
}

void TestAtramentInline::inlineObjectInEms()
{
    // An icon sized in ems grows with a heading's text.
    const std::u16string t = u"Nagłówek \uFFFC ze znaczkiem";
    Harness h(t, {object({9, 10}, InlineObjectSize{1, 0.75f, 0.25f, true}, rgb(0x1971c2))});
    QVERIFY(near(painted().back().bounds.width(), kSize, 0.01f));
    h.change([](Para &p) { p.size = 32; });
    const InlineObjectPaint big = painted().back();
    QVERIFY(near(big.bounds.width(), 32, 0.01f));
    QVERIFY(near(big.bounds.height(), 32, 0.01f));
    QVERIFY(near(big.bounds.bottom(), big.baseline + 8, 0.01f));
    QCOMPARE(big.fontSize, 32.0f);
}

void TestAtramentInline::inlineObjectWrapsAsOneUnit()
{
    // A page icon over three no-break spaces, glued by a fourth to the link's text: at a narrow
    // width it goes to the next line with the word, whole.
    const std::u16string t = u"Przeczytaj koniecznie \u00a0\u00a0\u00a0\u00a0Źródła";
    const int at = int(t.find(u'\u00a0'));
    Harness h(t, {object({at, at + 3}, InlineObjectSize{14, 12, 3}, rgb(0x1971c2))}, 420);
    const InlineObjectPaint wide = painted().back();
    const float firstLine = wide.baseline;
    for (float width : {200.0f, 150.0f, 110.0f}) {
        h.change([width](Para &p) { p.width = width; });
        const InlineObjectPaint p = painted().back();
        QVERIFY(near(p.bounds.width(), 14, 0.01f));
        if (width <= 150) {
            QVERIFY2(p.baseline > firstLine, qPrintable(QString::number(width)));
            QVERIFY(near(p.bounds.left(), kPad, 0.01f)); // starts its line
        }
    }
}

void TestAtramentInline::spansAroundAnObject()
{
    // A span that ends inside an object does not cut it (one run, one painter call, its whole
    // width); of two objects that overlap the first wins; a span over the whole object styles it.
    const std::u16string t = u"Strona \u00a0\u00a0\u00a0\u00a0Dom";
    const Color blue = rgb(0x1c7ed6);
    TextSpan link;
    link.range = {8, 14};
    link.style.color = blue;
    Harness h(t, {object({7, 10}, InlineObjectSize{15, 11, 2}, rgb(0x2f9e44)), link,
                  object({9, 12}, InlineObjectSize{40, 11, 2}, rgb(0xe03131))});
    for (const InlineObjectPaint &p : painted())
        QCOMPARE(p.range.start, 7); // the second object, overlapping the first, is never drawn
    QCOMPARE(h.quads(rgb(0xe03131)).size(), std::size_t(0));
    QCOMPARE(h.quads(rgb(0x2f9e44)).size(), std::size_t(1));
    QVERIFY(near(painted().back().bounds.width(), 15, 0.01f));
    QCOMPARE(painted().back().color, black()); // the link's colour starts inside it
    link.range = {7, 14};
    h.change([&](Para &p) { p.spans = {object({7, 10}, InlineObjectSize{15, 11, 2}, rgb(0x2f9e44)), link}; });
    QCOMPARE(painted().back().color, blue);
}

void TestAtramentInline::linkOverAnObject()
{
    // A link over an icon and its word: a click on the icon is the link's (its hit region covers
    // the object's room).
    const std::u16string t = u"Idź do \u00a0\u00a0\u00a0\u00a0Głównej";
    Harness h(t, {object({7, 10}, InlineObjectSize{16, 12, 3}, rgb(0x2f9e44))});
    h.change([&](Para &p) { p.links.push_back(TextLink{{7, int(t.size())}, {}}); });
    const InlineObjectPaint p = painted().back();
    h.tw.simulateClick({p.bounds.left() + p.bounds.width() / 2, p.bounds.top() + p.bounds.height() / 2});
    QCOMPARE(h.para.read(h.app).clicks, 1);
    h.tw.simulateMouseMove({p.bounds.left() + 3, p.bounds.top() + 3});
    QCOMPARE(h.tw.cursor(), CursorStyle::PointingHand);
}

void TestAtramentInline::maxLinesEndsWithAnEllipsis()
{
    // A query result's text (QueryBlockView's BRichText, maximumLineCount 4): at most N lines, the
    // last ending with "…" where its text would go on, inside the width.
    const std::u16string t = u"Zażółć gęślą jaźń, a potem pojechać do Łodzi po świeże pączki z różą, "
                             u"kupić źdźbła trawy dla żółwia i wrócić przez Gdańsk, Kraków oraz Wrocław.";
    Harness h(t, {}, 160);
    QVERIFY(h.para.read(h.app).handle.layout()->lines().size() > 3);
    QCOMPARE(h.para.read(h.app).handle.layout()->text(), t); // no limit: the whole text
    h.change([](Para &p) { p.maxLines = 3; });
    const TextLayout *shown = h.para.read(h.app).handle.layout();
    QVERIFY(shown);
    QCOMPARE(shown->lines().size(), std::size_t(3));
    const std::u16string &text = shown->text();
    QVERIFY(text.size() < t.size());
    QCOMPARE(text.back(), u'…');
    // What is shown is the text's start, no space before the ellipsis, within the width.
    QCOMPARE(text.substr(0, text.size() - 1), t.substr(0, text.size() - 1));
    QVERIFY(text[text.size() - 2] != u' ');
    QVERIFY2(shown->lines().back().width <= 160.01f, qPrintable(QString::number(shown->lines().back().width)));
    QVERIFY(near(shown->size().height, 3 * kLine, 0.01f));
    // Its last line takes as much of the text as fits with the ellipsis: one more grapheme does not.
    const std::shared_ptr<const TextLayout> full = h.layout(t, {}, 160);
    const TextLine &third = full->lines()[2];
    QVERIFY(int(text.size()) - 1 <= third.end);
    // A text of fewer lines than the limit is left as it is.
    h.change([](Para &p) { p.maxLines = 30; });
    QCOMPARE(h.para.read(h.app).handle.layout()->text(), t);
    h.change([](Para &p) {
        p.text = u"Krótki tekst";
        p.maxLines = 1;
    });
    QCOMPARE(h.para.read(h.app).handle.layout()->text(), std::u16string(u"Krótki tekst"));
}

QTEST_GUILESS_MAIN(TestAtramentInline)
#include "tst_atrament_inline.moc"
