// Atrament's touch scrolling (docs/ATRAMENT.md "Touch") on the test platform's virtual clock: the
// slop, the content following the finger, which of nested containers takes a drag, the fling's
// distance and deceleration, a finger stopping it, taps and long presses (a field's selection),
// a drag that scrolled clicking nothing, a mouse that does not scroll, a pen that does; the
// velocity tracker and Android's fling curve on their own; a list keeping its focused row.
#include "text/fonts.h"
#include "ui/atrament.h"
#include "ui/testplatform.h"
#include "ui/touch.h"

#include <QtTest>

#include <chrono>
#include <memory>
#include <vector>

using namespace brulion::atrament;
using namespace std::chrono_literals;

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

constexpr Color kRed = rgb(0xff0000);
constexpr Color kBlue = rgb(0x0000ff);
constexpr Color kGreen = rgb(0x00ff00);

bool isGreen(Pixel p)
{
    return p.g > 200 && p.r < 60 && p.b < 60;
}

struct Harness {
    std::shared_ptr<TestPlatform> platform = std::make_shared<TestPlatform>();
    App app{platform, loadFonts()};
    TestWindow &tw;
    Window *window = nullptr;
    explicit Harness(Size size = {200, 300}) : tw(platform->newWindow(size)) { }
    template<class T>
    Window &open(const Entity<T> &root)
    {
        window = &app.openWindow(tw, root);
        tw.settle();
        return *window;
    }
    bool anyGreen() const
    {
        for (int y = 0; y < tw.image().height; y += 5)
            for (int x = 0; x < tw.image().width; x += 5)
                if (isGreen(tw.pixel(x, y)))
                    return true;
        return false;
    }
};

// 200 rows of 50 px in a 300 px scroll view; a row shows green while pressed and counts clicks.
struct Rows {
    int clicks = 0;
    int lastClicked = -1;
    ScrollHandle scroll;
    AnyElement render(Window &, Context<Rows> &cx)
    {
        ArenaVector<AnyElement> rows;
        for (int i = 0; i < 200; ++i)
            rows.push_back(div().id(ElementId(u"row", std::uint64_t(i)))
                                   .h(50)
                                   .flexShrink0()
                                   .bg(i % 2 ? kRed : kBlue)
                                   .active([](Style &s) { s.background = Background::solid(kGreen); })
                                   .onClick(cx.listener([i](Rows &r, const ClickEvent &, Window &, Context<Rows> &) {
                                       ++r.clicks;
                                       r.lastClicked = i;
                                   })));
        return div()
                .id(u"scroller")
                .sizeFull()
                .flexCol()
                .overflowYScroll()
                .hideScrollbar()
                .trackScroll(scroll)
                .children(rows);
    }
};

// Rows of 50 px in a scroll view; every third row takes a long press.
struct HeldRows {
    int clicks = 0;
    int longPressed = -1;
    Point at;
    ScrollHandle scroll;
    AnyElement render(Window &, Context<HeldRows> &cx)
    {
        ArenaVector<AnyElement> rows;
        for (int i = 0; i < 60; ++i) {
            Div row = div().id(ElementId(u"row", std::uint64_t(i)))
                              .h(50)
                              .flexShrink0()
                              .bg(i % 2 ? kRed : kBlue)
                              .onClick(cx.listener([](HeldRows &r, const ClickEvent &, Window &, Context<HeldRows> &) {
                                  ++r.clicks;
                              }));
            if (i % 3 == 0)
                std::move(row).onLongPress(cx.listener([i](HeldRows &r, Point p, Window &, Context<HeldRows> &) {
                    r.longPressed = i;
                    r.at = p;
                }));
            rows.push_back(std::move(row));
        }
        return div()
                .id(u"scroller")
                .sizeFull()
                .flexCol()
                .overflowYScroll()
                .hideScrollbar()
                .trackScroll(scroll)
                .children(rows);
    }
};

// A page (scrolling) with a header, an inner list (scrolling) and a footer: 100 + 200 + 400 px.
struct Nested {
    ScrollHandle page;
    ScrollHandle list;
    AnyElement render(Window &, Context<Nested> &)
    {
        return div()
                .id(u"page")
                .sizeFull()
                .flexCol()
                .overflowYScroll()
                .hideScrollbar()
                .trackScroll(page)
                .child(div().h(100).flexShrink0().bg(kRed))
                .child(uniformList(u"inner", 50,
                                   [](std::size_t i, Window &, App &) -> AnyElement {
                                       return div().h(20).bg(i % 2 ? kBlue : kRed);
                                   })
                               .itemHeight(20)
                               .h(200)
                               .flexShrink0()
                               .trackScroll(list))
                .child(div().h(400).flexShrink0().bg(kBlue));
    }
};

// A field at the top of a scrolling page (y 60–90), 1000 px of page below it.
struct Form {
    Entity<TextInput> field;
    ScrollHandle scroll;
    explicit Form(Context<Form> &cx)
        : field(cx.newEntity<TextInput>(
                  TextInputOptions{u"Notatka", false, InputHandler::Purpose::Text, u"Zażółć gęślą jaźń", {}}))
    { }
    AnyElement render(Window &, Context<Form> &)
    {
        return div()
                .sizeFull()
                .flexCol()
                .overflowYScroll()
                .hideScrollbar()
                .trackScroll(scroll)
                .textSize(16)
                .child(div().h(60).flexShrink0())
                .child(div().h(30).flexShrink0().px(10).child(field))
                .child(div().h(1000).flexShrink0());
    }
};

// A list of 30 rows of 40 px, a field in each.
struct FieldList {
    std::vector<Entity<TextInput>> fields;
    ScrollHandle scroll;
    explicit FieldList(Context<FieldList> &cx)
    {
        for (int i = 0; i < 30; ++i)
            fields.push_back(cx.newEntity<TextInput>(
                    TextInputOptions{u"…", false, InputHandler::Purpose::Text, u"Wiersz z polem", {}}));
    }
    AnyElement render(Window &, Context<FieldList> &)
    {
        FieldList *self = this;
        return div().sizeFull().flexCol().textSize(16).child(
                uniformList(u"fields", fields.size(),
                            [self](std::size_t i, Window &, App &) -> AnyElement {
                                return div().h(40).px(10).py(10).child(self->fields[i]);
                            })
                        .itemHeight(40)
                        .flex1()
                        .trackScroll(scroll));
    }
};

} // namespace

class TestAtramentTouch : public QObject {
    Q_OBJECT

private Q_SLOTS:
    void velocityTracker();
    void flingCurve();
    void slopThenFollowsTheFinger();
    void flingDistanceAndDeceleration();
    void touchStopsAFling();
    void edgeStopsAFling();
    void scrollIsNoClick();
    void tapsClick();
    void elementsLongPress();
    void sidewaysDragIsTheElements();
    void nestedContainers();
    void longPressInFieldSelects();
    void dragOverFieldDoesNotFocusIt();
    void mouseDoesNotScroll();
    void penScrolls();
    void cancelledTouch();
    void uniformListRevealsFocusedField();
    void scrollOnlyPress();
};

void TestAtramentTouch::velocityTracker()
{
    VelocityTracker v;
    QCOMPARE(v.velocity(), Point{});
    // Even motion: 25 px every 10 ms, upwards — 2500 px/s exactly.
    for (int i = 0; i <= 8; ++i)
        v.add(i * 0.010, {100, 280.0f - 25.0f * float(i)});
    QVERIFY(std::abs(v.velocity().y + 2500) < 1);
    QVERIFY(std::abs(v.velocity().x) < 0.01f);
    QVERIFY(std::abs(v.velocityAtRelease(0.085).y + 2500) < 1); // lifted while moving
    QCOMPARE(v.velocityAtRelease(0.130), Point{});              // rested 50 ms first
    // Slowing down (x = 1000 t − 2000 t², 1000 px/s at t = 0): the slope at the newest sample, not
    // the average over the window.
    VelocityTracker slowing;
    for (int i = 0; i <= 10; ++i) {
        const double t = i * 0.008;
        slowing.add(t, {float(1000 * t - 2000 * t * t), 0});
    }
    const double expected = 1000 - 4000 * 0.080; // 680 px/s
    QVERIFY2(std::abs(slowing.velocity().x - expected) < 2, qPrintable(QString::number(slowing.velocity().x)));
    // Only the last 100 ms count, and a pause cuts what came before it.
    VelocityTracker paused;
    paused.add(0.000, {0, 0});
    paused.add(0.010, {0, 500}); // a jerk long before
    paused.add(0.200, {0, 500});
    paused.add(0.210, {0, 510});
    paused.add(0.220, {0, 520});
    QVERIFY(std::abs(paused.velocity().y - 1000) < 1);
}

void TestAtramentTouch::flingCurve()
{
    // Android's OverScroller for 2500 dp/s: about 954 dp in about 1.09 s.
    const FlingCurve c(2500);
    QVERIFY2(std::abs(c.distance() - 953.8f) < 2, qPrintable(QString::number(c.distance())));
    QVERIFY2(std::abs(c.duration() - 1.090) < 0.01, qPrintable(QString::number(c.duration())));
    // It starts at the release velocity and slows to rest, never going back.
    QVERIFY(std::abs(c.velocityAt(0) - 2500) < 1);
    QVERIFY(std::abs(c.velocityAt(0.001) - 2500) < 40);
    float last = 0;
    float lastV = 2600;
    for (double t = 0; t <= c.duration() + 0.05; t += 0.016) {
        const float p = c.positionAt(t);
        const float v = c.velocityAt(t);
        QVERIFY(p >= last);
        QVERIFY(v <= lastV + 0.5f);
        last = p;
        lastV = v;
    }
    QCOMPARE(c.positionAt(c.duration()), c.distance());
    QCOMPARE(c.velocityAt(c.duration()), 0.0f);
    // Faster goes much further (not proportionally); the sign follows the velocity.
    const FlingCurve fast(5000);
    QVERIFY(fast.distance() > 2.5f * c.distance());
    QCOMPARE(FlingCurve(-2500).distance(), -c.distance());
    // Too slow is no fling; too fast is capped.
    QVERIFY(FlingCurve(30).isEmpty());
    QCOMPARE(FlingCurve(20000).distance(), FlingCurve(kMaxFlingVelocity).distance());
}

void TestAtramentTouch::slopThenFollowsTheFinger()
{
    Harness h;
    Entity<Rows> rows = h.app.newEntity<Rows>();
    h.open(rows);
    auto offset = [&] { return rows.read(h.app).scroll.offset().y; };
    h.tw.simulateTouchDown({100, 250});
    h.tw.advance(16ms);
    h.tw.simulateTouchMove({100, 245}); // 5 px: within the slop (8)
    QCOMPARE(offset(), 0.0f);
    QVERIFY(!h.window->touchScrolling());
    h.tw.advance(16ms);
    h.tw.simulateTouchMove({100, 240}); // 10 px: past it, the content moves from there on (no jump)
    QVERIFY(h.window->touchScrolling());
    QCOMPARE(offset(), 2.0f);
    h.tw.advance(16ms);
    h.tw.simulateTouchMove({100, 200}); // 1:1
    QCOMPARE(offset(), 42.0f);
    h.tw.advance(16ms);
    h.tw.simulateTouchMove({100, 220}); // back
    QCOMPARE(offset(), 22.0f);
    // The pixels moved with it: row 0 (blue) ends at 50 − 22.
    QVERIFY(h.tw.pixel(100, 20).b > 200);
    QVERIFY(h.tw.pixel(100, 32).r > 200);
    // Resting before it lifts: no fling.
    h.tw.advance(100ms);
    h.tw.simulateTouchUp({100, 220});
    QVERIFY(!h.window->flinging());
    QVERIFY(h.window->touchScrolled());
    h.tw.advance(1000ms);
    QCOMPARE(offset(), 22.0f);
    QCOMPARE(rows.read(h.app).clicks, 0);
}

void TestAtramentTouch::flingDistanceAndDeceleration()
{
    Harness h;
    Entity<Rows> rows = h.app.newEntity<Rows>();
    h.open(rows);
    auto offset = [&] { return rows.read(h.app).scroll.offset().y; };
    std::vector<Window::TouchTrace> traces;
    h.window->setTouchTrace([&](const Window::TouchTrace &t) { traces.push_back(t); });
    // A flick: 200 px up in 80 ms (2500 px/s), lifted while moving.
    h.tw.simulateTouchDrag({100, 280}, {100, 80}, 80ms, 8);
    QVERIFY(h.window->flinging());
    const float released = offset();
    QCOMPARE(released, 192.0f);
    const FlingCurve curve(2500);
    std::vector<float> steps;
    float last = released;
    for (int i = 0; i < 200 && h.window->flinging(); ++i) {
        h.tw.advance(16ms);
        steps.push_back(offset() - last);
        last = offset();
    }
    QVERIFY(!h.window->flinging());
    QVERIFY(h.platform->pendingTimers() == 0);
    QVERIFY(!h.tw.animationFrameRequested());
    // The whole of Android's curve for the release velocity, over its duration.
    QVERIFY2(std::abs((offset() - released) - curve.distance()) < 2, qPrintable(QString::number(offset() - released)));
    const double frames = curve.duration() / 0.016;
    QVERIFY2(std::abs(double(steps.size()) - frames) <= 2, qPrintable(QString::number(steps.size())));
    // At the finger's speed first (≈ 40 px a frame), then slower and slower.
    QVERIFY2(std::abs(steps.front() - 40) < 3, qPrintable(QString::number(steps.front())));
    for (std::size_t i = 1; i < steps.size(); ++i)
        QVERIFY2(steps[i] <= steps[i - 1] + 0.01f, qPrintable(QString::number(i)));
    QVERIFY(steps[steps.size() / 2] < steps.front() / 3);
    QCOMPARE(rows.read(h.app).clicks, 0);
    // The trace a device logs: the scroll's start, the release, the fling's end and its frames.
    QCOMPARE(traces.size(), std::size_t(3));
    QVERIFY(traces[0].kind == Window::TouchTrace::Kind::Scroll);
    QVERIFY(traces[1].kind == Window::TouchTrace::Kind::Release);
    QVERIFY(std::abs(traces[1].velocity.y + 2500) < 1);
    QVERIFY(std::abs(traces[1].distance.y - curve.distance()) < 1);
    QVERIFY(traces[2].kind == Window::TouchTrace::Kind::FlingEnd);
    QCOMPARE(std::size_t(traces[2].frames), steps.size());
    QCOMPARE(traces[2].offset.y, offset());
    h.window->setTouchTrace({});
    // A gentler flick goes less far than proportionally less.
    h.tw.simulateTouchDrag({100, 280}, {100, 180}, 80ms, 8); // 1250 px/s
    const float before = offset();
    h.tw.advance(2000ms);
    QVERIFY(offset() - before < curve.distance() / 2);
    QVERIFY(std::abs((offset() - before) - FlingCurve(1250).distance()) < 2);
}

void TestAtramentTouch::touchStopsAFling()
{
    Harness h;
    Entity<Rows> rows = h.app.newEntity<Rows>();
    h.open(rows);
    auto offset = [&] { return rows.read(h.app).scroll.offset().y; };
    h.tw.simulateTouchDrag({100, 280}, {100, 80}, 80ms, 8);
    h.tw.advance(160ms);
    QVERIFY(h.window->flinging());
    // A finger on the moving list stops it where it is…
    h.tw.simulateTouchDown({100, 150});
    QVERIFY(!h.window->flinging());
    const float caught = offset();
    h.tw.advance(300ms);
    QCOMPARE(offset(), caught);
    // …and that tap opens no row.
    h.tw.simulateTouchUp({100, 150});
    QCOMPARE(rows.read(h.app).clicks, 0);
    QVERIFY(!h.anyGreen());
    // A finger catching it drags it on at once, without the slop.
    h.tw.simulateTouchDrag({100, 280}, {100, 80}, 80ms, 8);
    h.tw.advance(100ms);
    h.tw.simulateTouchDown({100, 150});
    const float held = offset();
    h.tw.advance(16ms);
    h.tw.simulateTouchMove({100, 146});
    QCOMPARE(offset(), held + 4);
    h.tw.advance(100ms);
    h.tw.simulateTouchUp({100, 146});
    QCOMPARE(rows.read(h.app).clicks, 0);
    // A press of the mouse stops it too.
    h.tw.simulateTouchDrag({100, 280}, {100, 80}, 80ms, 8);
    h.tw.advance(50ms);
    QVERIFY(h.window->flinging());
    h.tw.simulateMouseDown({10, 10});
    QVERIFY(!h.window->flinging());
    h.tw.simulateMouseUp({10, 10});
}

void TestAtramentTouch::edgeStopsAFling()
{
    Harness h;
    Entity<Rows> rows = h.app.newEntity<Rows>();
    h.open(rows);
    auto offset = [&] { return rows.read(h.app).scroll.offset().y; };
    // Near the end (max 9700), a hard flick stops dead at it: no bounce.
    rows.read(h.app).scroll.shared()->offset = {0, 9300};
    h.window->refresh();
    h.tw.settle();
    h.tw.simulateTouchDrag({100, 280}, {100, 30}, 50ms, 5); // 5000 px/s
    QVERIFY(h.window->flinging());
    h.tw.advance(400ms);
    QCOMPARE(offset(), 9700.0f);
    QVERIFY(!h.window->flinging());
    h.tw.advance(500ms);
    QCOMPARE(offset(), 9700.0f);
    // And back at the top.
    rows.read(h.app).scroll.shared()->offset = {0, 300};
    h.window->refresh();
    h.tw.settle();
    h.tw.simulateTouchDrag({100, 30}, {100, 280}, 50ms, 5);
    h.tw.advance(400ms);
    QCOMPARE(offset(), 0.0f);
    QVERIFY(!h.window->flinging());
}

void TestAtramentTouch::scrollIsNoClick()
{
    Harness h;
    Entity<Rows> rows = h.app.newEntity<Rows>();
    h.open(rows);
    // Held still past the tap timeout: row 2 has the press and shows it.
    h.tw.simulateTouchDown({100, 125});
    h.tw.advance(150ms);
    QVERIFY(isGreen(h.tw.pixel(100, 125)));
    // Then the finger drags: a scroll — the press is cancelled, its look goes.
    h.tw.simulateTouchMove({100, 100});
    QVERIFY(h.window->touchScrolling());
    QVERIFY(!h.anyGreen());
    h.tw.advance(16ms);
    h.tw.simulateTouchMove({100, 60});
    h.tw.advance(100ms);
    // Released over the row it started on (the content moved under it, the finger came back).
    h.tw.simulateTouchMove({100, 125});
    h.tw.advance(100ms);
    h.tw.simulateTouchUp({100, 125});
    QCOMPARE(rows.read(h.app).clicks, 0);
    QVERIFY(!h.anyGreen());
    // A quick drag never gave a row the press at all.
    h.tw.simulateTouchDrag({100, 200}, {100, 150}, 40ms, 4, false);
    QVERIFY(!h.anyGreen());
    h.tw.advance(100ms);
    h.tw.simulateTouchUp({100, 150});
    QCOMPARE(rows.read(h.app).clicks, 0);
}

void TestAtramentTouch::tapsClick()
{
    Harness h;
    Entity<Rows> rows = h.app.newEntity<Rows>();
    h.open(rows);
    h.tw.simulateTap({100, 125});
    QCOMPARE(rows.read(h.app).clicks, 1);
    QCOMPARE(rows.read(h.app).lastClicked, 2);
    QVERIFY(!h.window->touchScrolled());
    // A finger that moved less than the slop still taps.
    h.tw.simulateTouchDown({100, 175});
    h.tw.advance(16ms);
    h.tw.simulateTouchMove({103, 170});
    h.tw.simulateTouchUp({103, 170});
    QCOMPARE(rows.read(h.app).clicks, 2);
    QCOMPARE(rows.read(h.app).lastClicked, 3);
    // Held, then lifted: a press shown, then a click.
    h.tw.simulateTouchDown({100, 225});
    h.tw.advance(300ms);
    QVERIFY(isGreen(h.tw.pixel(100, 225)));
    h.tw.simulateTouchUp({100, 225});
    QCOMPARE(rows.read(h.app).clicks, 3);
    QCOMPARE(rows.read(h.app).lastClicked, 4);
    QVERIFY(!h.anyGreen());
    QCOMPARE(rows.read(h.app).scroll.offset().y, 0.0f);
}

void TestAtramentTouch::elementsLongPress()
{
    Harness h;
    Entity<HeldRows> rows = h.app.newEntity<HeldRows>();
    h.open(rows);
    // Held still on a row that takes it: its long press, at the finger; the release clicks nothing,
    // the finger moving on scrolls nothing.
    h.tw.simulateTouchDown({100, 160}); // row 3
    h.tw.advance(500ms);
    QCOMPARE(rows.read(h.app).longPressed, 3);
    QCOMPARE(rows.read(h.app).at.y, 160.0f);
    h.tw.simulateTouchMove({100, 60});
    h.tw.simulateTouchUp({100, 60});
    h.tw.advance(100ms);
    QCOMPARE(rows.read(h.app).clicks, 0);
    QCOMPARE(rows.read(h.app).scroll.offset().y, 0.0f);
    // A row without one: held, then lifted, is a click (as before).
    h.tw.simulateTouchDown({100, 75}); // row 1
    h.tw.advance(500ms);
    h.tw.simulateTouchUp({100, 75});
    QCOMPARE(rows.read(h.app).clicks, 1);
    QCOMPARE(rows.read(h.app).longPressed, 3);
    // A quick tap on a row that takes long presses clicks.
    h.tw.simulateTap({100, 10}); // row 0
    QCOMPARE(rows.read(h.app).clicks, 2);
    QCOMPARE(rows.read(h.app).longPressed, 3);
    // A drag before the time scrolls, no long press.
    h.tw.simulateTouchDrag({100, 250}, {100, 100}, 80ms);
    h.tw.advance(600ms);
    QCOMPARE(rows.read(h.app).longPressed, 3);
    QVERIFY(rows.read(h.app).scroll.offset().y > 50);
}

void TestAtramentTouch::sidewaysDragIsTheElements()
{
    Harness h;
    Entity<Rows> rows = h.app.newEntity<Rows>();
    h.open(rows);
    // A vertical list does not take a sideways drag: the row has the press, and the release on it
    // clicks (as a native list's row).
    h.tw.simulateTouchDrag({60, 125}, {120, 130}, 100ms, 5, false);
    QVERIFY(!h.window->touchScrolling());
    QVERIFY(isGreen(h.tw.pixel(100, 125)));
    // Going vertical later does not make it a scroll.
    h.tw.advance(16ms);
    h.tw.simulateTouchMove({120, 60});
    QCOMPARE(rows.read(h.app).scroll.offset().y, 0.0f);
    h.tw.simulateTouchMove({120, 130});
    h.tw.simulateTouchUp({120, 130});
    QCOMPARE(rows.read(h.app).clicks, 1);
    QCOMPARE(rows.read(h.app).lastClicked, 2);
}

void TestAtramentTouch::nestedContainers()
{
    Harness h;
    Entity<Nested> view = h.app.newEntity<Nested>();
    h.open(view);
    auto page = [&] { return view.read(h.app).page.offset().y; };
    auto list = [&] { return view.read(h.app).list.offset().y; };
    auto drag = [&](Point from, Point to) {
        h.tw.simulateTouchDrag(from, to, 100ms, 5, false);
        h.tw.advance(100ms); // rest: no fling
        h.tw.simulateTouchUp(to);
    };
    // On the inner list: the list scrolls, not the page.
    drag({100, 250}, {100, 150});
    QCOMPARE(list(), 92.0f);
    QCOMPARE(page(), 0.0f);
    // On the header: the page.
    drag({100, 80}, {100, 50});
    QCOMPARE(page(), 22.0f);
    QCOMPARE(list(), 92.0f);
    drag({100, 50}, {100, 90});
    QCOMPARE(page(), 0.0f);
    // The list at its end: a drag up on it moves the page around it.
    view.read(h.app).list.shared()->offset = {0, 800};
    h.window->refresh();
    h.tw.settle();
    drag({100, 250}, {100, 150});
    QCOMPARE(page(), 92.0f);
    QCOMPARE(list(), 800.0f);
    // Down on it: the list can go back, so it does (the page stays).
    drag({100, 120}, {100, 170});
    QCOMPARE(list(), 758.0f);
    QCOMPARE(page(), 92.0f);
    // A drag that takes the list to its top stays the list's (latched: the page does not take
    // over in the middle of a drag).
    view.read(h.app).list.shared()->offset = {0, 20};
    h.window->refresh();
    h.tw.settle();
    drag({100, 60}, {100, 200});
    QCOMPARE(list(), 0.0f);
    QCOMPARE(page(), 92.0f);
    // The next drag down starts on a list at its top: the page takes it.
    drag({100, 60}, {100, 100});
    QCOMPARE(page(), 60.0f);
}

void TestAtramentTouch::longPressInFieldSelects()
{
    Harness h;
    Entity<Form> form = h.app.newEntity<Form>();
    h.open(form);
    const Entity<TextInput> field = form.read(h.app).field;
    auto offset = [&] { return form.read(h.app).scroll.offset().y; };
    // A finger held on the field's text: the press reaches it after the tap timeout (the focus,
    // the caret), then Android's input context takes the long press and selects the word.
    h.tw.simulateTouchDown({30, 75});
    h.tw.advance(500ms);
    QVERIFY(field.read(h.app).focusHandle().isFocused(*h.window));
    QVERIFY(h.tw.inputHandler());
    h.tw.inputHandler()->setSelection({0, 6});
    h.tw.settle();
    // The finger moving on scrolls nothing (the selection and its handles stay where they are).
    h.tw.simulateTouchMove({30, 40});
    h.tw.advance(16ms);
    h.tw.simulateTouchMove({30, 10});
    QCOMPARE(offset(), 0.0f);
    QVERIFY(!h.window->touchScrolling());
    QCOMPARE(field.read(h.app).selection(), (InputHandler::Selection{0, 6}));
    h.tw.simulateTouchUp({30, 10});
    QCOMPARE(field.read(h.app).selection(), (InputHandler::Selection{0, 6}));
    QVERIFY(!h.window->flinging());
    // Held as long on the page below the field, the finger still scrolls.
    h.tw.simulateTouchDown({100, 250});
    h.tw.advance(500ms);
    h.tw.simulateTouchMove({100, 150});
    QCOMPARE(offset(), 92.0f);
    h.tw.advance(100ms);
    h.tw.simulateTouchUp({100, 150});
    QCOMPARE(field.read(h.app).selection(), (InputHandler::Selection{0, 6}));
}

void TestAtramentTouch::dragOverFieldDoesNotFocusIt()
{
    Harness h;
    Entity<Form> form = h.app.newEntity<Form>();
    h.open(form);
    const Entity<TextInput> field = form.read(h.app).field;
    // A scroll that starts on a field neither focuses it (no keyboard opens) nor moves its caret.
    const InputHandler::Selection before = field.read(h.app).selection();
    h.tw.simulateTouchDrag({30, 75}, {30, 25}, 60ms, 4, false);
    h.tw.advance(100ms);
    h.tw.simulateTouchUp({30, 25});
    QCOMPARE(form.read(h.app).scroll.offset().y, 42.0f);
    QVERIFY(!field.read(h.app).focusHandle().isFocused(*h.window));
    QVERIFY(!h.tw.inputHandler());
    QCOMPARE(field.read(h.app).selection(), before);
    // A tap on it does (the field is at 60 − 42 now).
    h.tw.simulateTap({30, 33});
    QVERIFY(field.read(h.app).focusHandle().isFocused(*h.window));
    QVERIFY(h.tw.inputHandler());
}

void TestAtramentTouch::mouseDoesNotScroll()
{
    Harness h;
    Entity<Rows> rows = h.app.newEntity<Rows>();
    h.open(rows);
    h.tw.simulateMouseMove({100, 250});
    h.tw.simulateMouseDown({100, 250});
    h.tw.simulateMouseMove({100, 200}, MouseButton::Left);
    h.tw.simulateMouseMove({100, 50}, MouseButton::Left);
    h.tw.simulateMouseUp({100, 50});
    QCOMPARE(rows.read(h.app).scroll.offset().y, 0.0f);
    QVERIFY(!h.window->flinging());
    // The wheel still does.
    h.tw.simulateScroll({100, 100}, {0, 60});
    QCOMPARE(rows.read(h.app).scroll.offset().y, 60.0f);
}

void TestAtramentTouch::penScrolls()
{
    Harness h;
    Entity<Rows> rows = h.app.newEntity<Rows>();
    h.open(rows);
    h.tw.simulateTouchDown({100, 250}, PointerKind::Pen);
    h.tw.advance(16ms);
    h.tw.simulateTouchMove({100, 200});
    QCOMPARE(rows.read(h.app).scroll.offset().y, 42.0f);
    h.tw.advance(100ms);
    h.tw.simulateTouchUp({100, 200});
    QCOMPARE(rows.read(h.app).clicks, 0);
    // A pen hovering is a pointer (it may hover); a finger never hovers.
    MouseMoveEvent hover;
    hover.position = {100, 100};
    hover.pointer = PointerKind::Pen;
    QVERIFY(h.tw.simulateInput(hover));
    hover.pointer = PointerKind::Touch;
    QVERIFY(!h.tw.simulateInput(hover));
}

void TestAtramentTouch::cancelledTouch()
{
    Harness h;
    Entity<Rows> rows = h.app.newEntity<Rows>();
    h.open(rows);
    h.tw.simulateTouchDown({100, 125});
    h.tw.advance(150ms);
    QVERIFY(isGreen(h.tw.pixel(100, 125)));
    // The system takes the touch (a gesture of its own): the press ends, nothing is clicked.
    h.tw.simulateTouchCancel();
    QVERIFY(!h.anyGreen());
    QCOMPARE(rows.read(h.app).clicks, 0);
    // Cancelled while scrolling: no fling.
    h.tw.simulateTouchDrag({100, 280}, {100, 80}, 80ms, 8, false);
    h.tw.simulateTouchCancel();
    QVERIFY(!h.window->flinging());
    QVERIFY(!h.window->touchScrolling());
    // Taps work after it.
    h.tw.simulateTap({100, 125});
    QCOMPARE(rows.read(h.app).clicks, 1);
}

void TestAtramentTouch::uniformListRevealsFocusedField()
{
    Harness h({200, 300});
    Entity<FieldList> view = h.app.newEntity<FieldList>();
    h.open(view);
    auto offset = [&] { return view.read(h.app).scroll.offset().y; };
    const Entity<TextInput> field = view.read(h.app).fields[6];
    // The field in row 6 (240–280) takes the focus.
    h.tw.simulateTap({30, 260});
    QVERIFY(field.read(h.app).focusHandle().isFocused(*h.window));
    QCOMPARE(offset(), 0.0f);
    // An on-screen keyboard leaves 120 px: the row is out of view (and past the rows the list
    // draws), the list keeps it and scrolls its caret into view.
    h.tw.resize({200, 120});
    QVERIFY2(offset() >= 120, qPrintable(QString::number(offset())));
    QVERIFY(h.tw.inputHandler());
    const std::optional<Rect> caret = h.tw.inputHandler()->boundsForRange({0, 0});
    QVERIFY(caret);
    QVERIFY2(caret->top() >= 0 && caret->bottom() <= 120, qPrintable(QString::number(caret->top())));
    // Scrolled far away by hand, it stays focused and connected to the input method (its row kept,
    // unseen), until the caret moves: then it is shown again.
    h.tw.simulateScroll({100, 60}, {0, 600});
    const float away = offset();
    QVERIFY(away > 600);
    QVERIFY(h.tw.inputHandler());
    QCOMPARE(offset(), away);
    h.tw.simulateTyping(u"ż");
    QCOMPARE(field.read(h.app).text().size(), std::size_t(15));
    QVERIFY2(offset() >= 120 && offset() <= 280, qPrintable(QString::number(offset())));
    // The focus elsewhere: the row is not kept any more.
    h.window->blur();
    h.tw.settle();
    QVERIFY(!h.tw.inputHandler());
}

void TestAtramentTouch::scrollOnlyPress()
{
    // A press something over the window has (a hosted Qt Quick item: MouseDownEvent::scrollOnly):
    // the gesture follows it, and its drag scrolls, but no element ever gets it.
    Harness h;
    Entity<Rows> rows = h.app.newEntity<Rows>();
    h.open(rows);
    auto pressOnly = [&](Point p) {
        MouseDownEvent down;
        down.position = p;
        down.pointer = PointerKind::Touch;
        down.scrollOnly = true;
        return h.tw.simulateInput(down);
    };
    auto offset = [&] { return rows.read(h.app).scroll.offset().y; };
    // Held, then lifted: no press look, no click — the tap is the item's.
    QVERIFY(pressOnly({100, 125}));
    QVERIFY(h.window->touchActive());
    h.tw.advance(500ms); // past the tap timeout and the long press
    QVERIFY(!h.anyGreen());
    h.tw.simulateTouchUp({100, 125});
    QVERIFY(!h.window->touchActive());
    QCOMPARE(rows.read(h.app).clicks, 0);
    QCOMPARE(offset(), 0.0f);
    // A drag: past the slop it scrolls, 1:1, and a fling carries on.
    QVERIFY(pressOnly({100, 250}));
    h.tw.simulateTouchMove({100, 245});
    QVERIFY(!h.window->touchScrolling());
    h.tw.simulateTouchMove({100, 200});
    QVERIFY(h.window->touchScrolling());
    QCOMPARE(offset(), 50 - h.platform->touchSlop());
    h.tw.advance(100ms);
    h.tw.simulateTouchMove({100, 150});
    h.tw.simulateTouchUp({100, 150});
    QVERIFY(h.window->touchScrolled());
    QCOMPARE(rows.read(h.app).clicks, 0);
    h.tw.advance(1000ms);
    // A sideways drag nothing scrolls: the gesture lets it go (it is the item's drag).
    QVERIFY(pressOnly({100, 150}));
    h.tw.simulateTouchMove({160, 152});
    QVERIFY(!h.window->touchActive());
    h.tw.simulateTouchUp({160, 152});
    QCOMPARE(rows.read(h.app).clicks, 0);
    // A field under such a press is never focused by it, held or tapped.
    Harness still;
    Entity<Form> form = still.app.newEntity<Form>();
    still.open(form);
    MouseDownEvent onField;
    onField.position = {30, 75};
    onField.pointer = PointerKind::Touch;
    onField.scrollOnly = true;
    still.tw.simulateInput(onField);
    still.tw.advance(500ms);
    QVERIFY(!form.read(still.app).field.read(still.app).focusHandle().isFocused(*still.window));
    still.tw.simulateTouchUp({30, 75});
    QVERIFY(!form.read(still.app).field.read(still.app).focusHandle().isFocused(*still.window));
}

QTEST_GUILESS_MAIN(TestAtramentTouch)
#include "tst_atrament_touch.moc"
