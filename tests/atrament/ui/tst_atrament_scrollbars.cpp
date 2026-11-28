// A scroll view's thumbs dragged with the mouse (docs/ATRAMENT.md "Views and entities": a div with
// overflowXScroll / overflowYScroll paints its thumbs): a press on a thumb and a move scroll the
// content by as much as the thumb moved along its track, the content gets no press nor click, a
// release ends it; a press beside the thumb is the content's as before.
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
        f->addFontFile(BRULION_SOURCE_DIR "/assets/fonts/ui/BrulionInter-Regular.ttf");
        return f;
    }();
    return fonts;
}

// A view `w` × `h` of content `contentW` × `contentH` (by default 200 × 100 of 800 × 400: 4 × the
// view both ways, scrolling both ways), at (`left`, `top`) of the window, its offset shared.
struct View {
    ScrollHandle scroll;
    float w = 200, h = 100, contentW = 800, contentH = 400, left = 0, top = 0;
    bool scrollX = true, scrollY = true;
    int clicks = 0, presses = 0;
    AnyElement render(Window &, Context<View> &cx)
    {
        Div v = div().id(u"view")
                        .w(w)
                        .h(h)
                        .trackScroll(scroll)
                        .onClick(cx.listener([](View &v, const ClickEvent &, Window &, Context<View> &) { ++v.clicks; }))
                        .onMouseDown(MouseButton::Left, cx.listener([](View &v, const MouseDownEvent &, Window &, Context<View> &) {
                            ++v.presses;
                        }))
                        .child(div().w(contentW).h(contentH).flexShrink0().bg(rgb(0x3366cc)));
        if (scrollX)
            std::move(v).overflowXScroll(); // in place
        if (scrollY)
            std::move(v).overflowYScroll();
        return div().sizeFull().bg(white()).pl(left).pt(top).child(std::move(v));
    }
};

constexpr Color thumbColor = rgba(0x8080808c);

struct Harness {
    std::shared_ptr<TestPlatform> platform = std::make_shared<TestPlatform>();
    App app{platform, loadFonts()};
    TestWindow &tw;
    Entity<View> view;
    explicit Harness(std::function<void(View &)> setUp = {}) : tw(platform->newWindow({300, 200}, 1))
    {
        view = app.newEntity<View>();
        if (setUp)
            view.update(app, [&](View &v, Context<View> &) { setUp(v); });
        app.openWindow(tw, view);
        tw.settle();
    }
    Point offset() { return view.read(app).scroll.offset(); }
    void setOffset(Point p)
    {
        view.update(app, [p](View &v, Context<View> &cx) {
            v.scroll.setOffset(p);
            cx.notify();
        });
        tw.settle();
    }
    // The quads painted in `c`, in the order painted.
    std::vector<Quad> quads(Color c) const
    {
        std::vector<Quad> out;
        for (const Primitive &p : tw.lastScene()->primitives())
            if (const Quad *q = std::get_if<Quad>(&p); q && q->background.color == c)
                out.push_back(*q);
        return out;
    }
    // Presses on `from`, moves by each of `steps` in turn, releases where it ended.
    void drag(Point from, std::initializer_list<Point> steps, MouseButton button = MouseButton::Left)
    {
        tw.simulateMouseDown(from, button);
        Point at = from;
        for (Point d : steps) {
            at += d;
            tw.simulateMouseMove(at, button);
        }
        tw.simulateMouseUp(at, button);
    }
};

} // namespace

class TestAtramentScrollbars : public QObject {
    Q_OBJECT
private Q_SLOTS:
    void dragTheSidewaysThumb();
    void dragTheVerticalThumb();
    void pressBesideTheThumbIsTheContents();
    void thumbsArePaintedWhereTheOffsetPutsThem();
    void dragFromAnOffsetBothWays();
    void onlyTheLeftMouseButtonDrags();
    void theWheelScrollsDuringADrag();
    void aViewScrollingOneWayLeavesTheOtherAlone();
    void contentThatShrinksPullsTheOffsetIn();
    void aShortTrack_data();
    void aShortTrack();
};

namespace {

bool near(float a, float b)
{
    return std::abs(a - b) < 0.01f;
}

QString describe(Rect r)
{
    return QStringLiteral("%1,%2 %3×%4").arg(r.left()).arg(r.top()).arg(r.width()).arg(r.height());
}

bool sameRect(Rect a, Rect b)
{
    return near(a.left(), b.left()) && near(a.top(), b.top()) && near(a.width(), b.width()) && near(a.height(), b.height());
}

} // namespace

// The thumbs (Div::paintScrollbars): 6 px thick, 2 px in from the edges; along a track of the
// view's length less 4 px, as long as the view's share of the content (here a quarter: 49 px of 196
// sideways, 24 px — the shortest — of 96 down).
void TestAtramentScrollbars::dragTheSidewaysThumb()
{
    Harness h;
    QCOMPARE(h.offset(), Point{});
    const Point onThumb{2 + 20, 100 - 2 - 3};
    h.tw.simulateMouseDown(onThumb);
    QCOMPARE(h.view.read(h.app).presses, 0); // the thumb's, not the content's
    h.tw.simulateMouseMove({onThumb.x + 49, onThumb.y}, MouseButton::Left);
    // The thumb moved a third of its free track (196 − 49 = 147 px): a third of the 600 px of offset.
    QVERIFY2(std::abs(h.offset().x - 200) < 1, qPrintable(QString::number(h.offset().x)));
    QCOMPARE(h.offset().y, 0.0f);
    h.tw.simulateMouseMove({onThumb.x + 1000, onThumb.y}, MouseButton::Left);
    QCOMPARE(h.offset().x, 600.0f); // clamped at the end
    h.tw.simulateMouseUp({onThumb.x + 1000, onThumb.y});
    QCOMPARE(h.view.read(h.app).clicks, 0);
    // Released: moving the mouse scrolls nothing more.
    h.tw.simulateMouseMove({onThumb.x, onThumb.y});
    QCOMPARE(h.offset().x, 600.0f);
}

void TestAtramentScrollbars::dragTheVerticalThumb()
{
    Harness h;
    const Point onThumb{200 - 2 - 3, 2 + 10};
    h.tw.simulateMouseDown(onThumb);
    QCOMPARE(h.view.read(h.app).presses, 0);
    h.tw.simulateMouseMove({onThumb.x, onThumb.y + 24}, MouseButton::Left);
    // 24 px of a free track of 96 − 24 = 72 px: a third of the 300 px of offset.
    QVERIFY2(std::abs(h.offset().y - 100) < 1, qPrintable(QString::number(h.offset().y)));
    h.tw.simulateMouseMove({onThumb.x, onThumb.y + 48}, MouseButton::Left);
    QVERIFY2(std::abs(h.offset().y - 200) < 1, qPrintable(QString::number(h.offset().y)));
    h.tw.simulateMouseUp({onThumb.x, onThumb.y + 48});
    QCOMPARE(h.view.read(h.app).clicks, 0);
}

void TestAtramentScrollbars::pressBesideTheThumbIsTheContents()
{
    Harness h;
    h.tw.simulateClick({100, 40});
    QCOMPARE(h.view.read(h.app).clicks, 1);
    h.tw.simulateMouseDown({100, 40});
    h.tw.simulateMouseMove({150, 40}, MouseButton::Left);
    h.tw.simulateMouseUp({150, 40});
    QCOMPARE(h.offset(), Point{});
}

// Each thumb along its edge of the view's box, placed along its free track by the offset's share of
// its largest value, with round ends; a drag paints it where it went.
void TestAtramentScrollbars::thumbsArePaintedWhereTheOffsetPutsThem()
{
    Harness h([](View &v) {
        v.left = 30;
        v.top = 20;
    });
    h.setOffset({400, 150});
    std::vector<Quad> thumbs = h.quads(thumbColor);
    QCOMPARE(thumbs.size(), std::size_t(2));
    // Sideways: two thirds of 600 → two thirds of the free 147 px; 8 px up from the box's bottom.
    QVERIFY2(sameRect(thumbs[0].bounds, Rect::fromXYWH(30 + 2 + 98, 20 + 100 - 8, 49, 6)), qPrintable(describe(thumbs[0].bounds)));
    // Down: half of 300 → half of the free 72 px; 8 px in from the box's right.
    QVERIFY2(sameRect(thumbs[1].bounds, Rect::fromXYWH(30 + 200 - 8, 20 + 2 + 36, 6, 24)), qPrintable(describe(thumbs[1].bounds)));
    for (const Quad &q : thumbs) {
        QCOMPARE(q.cornerRadii.topLeft, 3.0f);
        QCOMPARE(q.cornerRadii.bottomRight, 3.0f);
    }
    // Painted where it went while the button is still down.
    const Point from{30 + 200 - 5, 20 + 2 + 36 + 10};
    h.tw.simulateMouseDown(from);
    h.tw.simulateMouseMove({from.x, from.y + 12}, MouseButton::Left); // 12 of 72 px: 50 px more
    QVERIFY2(near(h.offset().y, 200), qPrintable(QString::number(h.offset().y)));
    thumbs = h.quads(thumbColor);
    QCOMPARE(thumbs.size(), std::size_t(2));
    QVERIFY2(sameRect(thumbs[1].bounds, Rect::fromXYWH(30 + 200 - 8, 20 + 2 + 48, 6, 24)), qPrintable(describe(thumbs[1].bounds)));
    h.tw.simulateMouseUp({from.x, from.y + 12});
}

// Dragging one thumb keeps the other axis's offset and starts from where its own was.
void TestAtramentScrollbars::dragFromAnOffsetBothWays()
{
    Harness h;
    h.setOffset({300, 150});
    h.drag({2 + 73.5f + 5, 100 - 5}, {{49, 0}}); // a third of the free track: 200 px more
    QVERIFY2(near(h.offset().x, 500), qPrintable(QString::number(h.offset().x)));
    QCOMPARE(h.offset().y, 150.0f);
    h.drag({200 - 5, 2 + 36 + 2}, {{0, 24}});
    QVERIFY2(near(h.offset().y, 250), qPrintable(QString::number(h.offset().y)));
    QVERIFY2(near(h.offset().x, 500), qPrintable(QString::number(h.offset().x)));
}

// The right button on a thumb is the content's (its menu): no drag.
void TestAtramentScrollbars::onlyTheLeftMouseButtonDrags()
{
    Harness h;
    h.drag({22, 95}, {{49, 0}}, MouseButton::Right);
    QCOMPARE(h.offset(), Point{});
}

// A drag takes the presses, moves and the release; the wheel still scrolls the view meanwhile.
void TestAtramentScrollbars::theWheelScrollsDuringADrag()
{
    Harness h;
    h.tw.simulateMouseDown({22, 95});
    h.tw.simulateScroll({100, 40}, {0, 30});
    QCOMPARE(h.offset(), (Point{0, 30}));
    h.tw.simulateMouseMove({22 + 49, 95}, MouseButton::Left);
    QVERIFY2(near(h.offset().x, 200), qPrintable(QString::number(h.offset().x)));
    h.tw.simulateMouseUp({22 + 49, 95});
}

// A view that scrolls down only paints no sideways thumb and moves its content only down (its
// content wider than it overhangs); one that scrolls sideways only, the other way round; one whose
// content fits sideways, though it could scroll so, paints only the thumb down.
void TestAtramentScrollbars::aViewScrollingOneWayLeavesTheOtherAlone()
{
    {
        Harness h([](View &v) {
            v.scrollX = false;
            v.left = 30;
            v.top = 20;
        });
        h.setOffset({0, 50});
        const std::vector<Quad> thumbs = h.quads(thumbColor);
        QCOMPARE(thumbs.size(), std::size_t(1));
        QCOMPARE(thumbs[0].bounds.width(), 6.0f);
        const std::vector<Quad> content = h.quads(rgb(0x3366cc));
        QCOMPARE(content.size(), std::size_t(1));
        QCOMPARE(content[0].bounds.left(), 30.0f);
        QCOMPARE(content[0].bounds.top(), 20.0f - 50);
    }
    {
        Harness h([](View &v) {
            v.scrollY = false;
            v.left = 30;
            v.top = 20;
        });
        h.setOffset({100, 0});
        const std::vector<Quad> thumbs = h.quads(thumbColor);
        QCOMPARE(thumbs.size(), std::size_t(1));
        QCOMPARE(thumbs[0].bounds.height(), 6.0f);
        const std::vector<Quad> content = h.quads(rgb(0x3366cc));
        QCOMPARE(content[0].bounds.left(), 30.0f - 100);
        QCOMPARE(content[0].bounds.top(), 20.0f);
    }
    // Content within half a px of the view fits (a layout's rounding); a px more does not.
    for (const auto &[contentW, count] : {std::pair{200.5f, 1}, std::pair{201.0f, 2}}) {
        Harness h([contentW](View &v) { v.contentW = contentW; });
        const std::vector<Quad> thumbs = h.quads(thumbColor);
        QCOMPARE(int(thumbs.size()), count);
        QCOMPARE(thumbs.back().bounds.width(), 6.0f); // the thumb down, painted last
    }
}

// The content getting narrower than the offset reaches: the offset comes back to its new end.
void TestAtramentScrollbars::contentThatShrinksPullsTheOffsetIn()
{
    Harness h;
    h.setOffset({600, 0});
    QCOMPARE(h.offset().x, 600.0f);
    h.view.update(h.app, [](View &v, Context<View> &cx) {
        v.contentW = 300;
        cx.notify();
    });
    h.tw.settle();
    QCOMPARE(h.offset().x, 100.0f);
}

// A view so short that its thumb is as long as the track (24 px) cannot be dragged — the thumb still
// takes the press; half a px of free track drags (743 px of content per px).
void TestAtramentScrollbars::aShortTrack_data()
{
    QTest::addColumn<float>("height");
    QTest::addColumn<float>("move");
    QTest::addColumn<float>("offset");
    QTest::newRow("no room") << 24.0f << 5.0f << 0.0f;
    QTest::newRow("half a px of room") << 28.5f << 0.25f << 185.75f;
}

void TestAtramentScrollbars::aShortTrack()
{
    QFETCH(float, height);
    QFETCH(float, move);
    QFETCH(float, offset);
    Harness h([height](View &v) {
        v.scrollX = false;
        v.h = height;
    });
    h.tw.simulateMouseDown({200 - 5, 10});
    QCOMPARE(h.view.read(h.app).presses, 0);
    h.tw.simulateMouseMove({200 - 5, 10 + move}, MouseButton::Left);
    QVERIFY2(near(h.offset().y, offset), qPrintable(QString::number(h.offset().y)));
    h.tw.simulateMouseUp({200 - 5, 10 + move});
}

QTEST_GUILESS_MAIN(TestAtramentScrollbars)
#include "tst_atrament_scrollbars.moc"
