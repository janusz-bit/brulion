// Drags an element keeps for itself (docs/ATRAMENT.md "Touch": claimed drags, a drag after a long
// press, edge scrolling) on the test platform's virtual clock: a finger pressed on an element that
// claims its drag (a grip) gives it the press at once and every move, the list around it scrolls
// nothing, and its release reaches it; elsewhere the same drag scrolls as before; a long press that
// goes on as a drag gives its moves and its release to the elements (no scroll, no click); the
// speed of the edge scrolling along a view's edges, and its steps while the speed holds.
#include "text/fonts.h"
#include "ui/atrament.h"
#include "ui/edgescroll.h"
#include "ui/testplatform.h"

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

// 100 rows of 50 px in a 300 px scroll view; row 1 has a grip (claims its drag) at its left,
// every row takes a long press that goes on as a drag (when `dragOnLongPress`), the page follows
// the pointer's moves and releases (its own listeners, as a view's root does).
struct Page {
    ScrollHandle scroll;
    int gripDowns = 0;
    std::vector<Point> moves;
    int ups = 0;
    bool lastUpCancelled = false;
    int cancels = 0;
    int clicks = 0;
    int longPressed = -1;
    bool dragOnLongPress = true;
    AnyElement render(Window &, Context<Page> &cx)
    {
        ArenaVector<AnyElement> rows;
        for (int i = 0; i < 100; ++i) {
            Div row = div().id(ElementId(u"row", std::uint64_t(i)))
                              .h(50)
                              .flexShrink0()
                              .bg(i % 2 ? rgb(0xff0000) : rgb(0x0000ff))
                              .onClick(cx.listener([](Page &p, const ClickEvent &, Window &, Context<Page> &) {
                                  ++p.clicks;
                              }))
                              .onLongPress(cx.listener([i](Page &p, Point, Window &w, Context<Page> &) {
                                  p.longPressed = i;
                                  if (p.dragOnLongPress)
                                      w.dragAfterLongPress();
                              }));
            if (i == 1)
                std::move(row).child(div()
                                             .id(u"grip")
                                             .size(44)
                                             .claimTouchDrag()
                                             .onMouseDown(MouseButton::Left, cx.listener([](Page &p, const MouseDownEvent &,
                                                                                            Window &w, Context<Page> &) {
                                                 ++p.gripDowns;
                                                 w.stopPropagation();
                                             })));
            rows.push_back(std::move(row));
        }
        return div()
                .sizeFull()
                .onMouseMove(cx.listener([](Page &p, const MouseMoveEvent &e, Window &, Context<Page> &) {
                    if (e.pressedButton == MouseButton::Left)
                        p.moves.push_back(e.position);
                }))
                .onMouseUp(MouseButton::Left, cx.listener([](Page &p, const MouseUpEvent &e, Window &, Context<Page> &) {
                    ++p.ups;
                    p.lastUpCancelled = e.cancelled;
                }))
                // A cancelled release is dispatched over nothing: a window-level listener sees it.
                .child(canvas([this](Rect, Window &w, App &) {
                           Page *self = this;
                           w.onMouseEvent([self](const PlatformInput &input, DispatchPhase phase, Window &, App &) {
                               const auto *up = std::get_if<MouseUpEvent>(&input);
                               if (up && up->cancelled && phase == DispatchPhase::Capture)
                                   ++self->cancels;
                           });
                       })
                               .absolute()
                               .size(0))
                .child(div()
                               .id(u"scroller")
                               .sizeFull()
                               .flexCol()
                               .overflowYScroll()
                               .hideScrollbar()
                               .trackScroll(scroll)
                               .children(rows));
    }
};

struct Harness {
    std::shared_ptr<TestPlatform> platform = std::make_shared<TestPlatform>();
    App app{platform, loadFonts()};
    TestWindow &tw;
    Entity<Page> page;
    Harness() : tw(platform->newWindow({200, 300}))
    {
        tw.setRasterize(false);
        page = app.newEntity<Page>();
        app.openWindow(tw, page);
        tw.settle();
    }
    Page &state()
    {
        Page *p = nullptr;
        page.update(app, [&p](Page &x, Context<Page> &) { p = &x; });
        return *p;
    }
    float offset() { return state().scroll.offset().y; }
};

} // namespace

class TestAtramentDrag : public QObject {
    Q_OBJECT
private Q_SLOTS:
    void claimedDragIsTheElements();
    void claimedDragCancelled();
    void unclaimedDragScrolls();
    void dragAfterLongPress();
    void longPressWithoutDragStaysStill();
    void edgeScrollSpeed();
    void edgeScrollerSteps();
};

void TestAtramentDrag::claimedDragIsTheElements()
{
    Harness h;
    // On the grip (row 1, its first 44 px): the press at once, inside a list that can scroll.
    h.tw.simulateTouchDown({20, 70});
    QCOMPARE(h.state().gripDowns, 1);
    // Moved far down: the moves are the elements', the list stays where it is.
    for (int y = 90; y <= 250; y += 20) {
        h.tw.advance(16ms);
        h.tw.simulateTouchMove({20, float(y)});
    }
    QCOMPARE(h.offset(), 0.0f);
    QVERIFY(h.state().moves.size() >= 8);
    QCOMPARE(h.state().moves.back().y, 250.0f);
    // Held still past the long-press time: no long press.
    h.tw.advance(800ms);
    QCOMPARE(h.state().longPressed, -1);
    h.tw.simulateTouchUp({20, 250});
    QCOMPARE(h.state().ups, 1);
    QVERIFY(!h.state().lastUpCancelled);
    QCOMPARE(h.state().clicks, 0); // released over another row: nothing clicked
    QCOMPARE(h.offset(), 0.0f);    // no fling either
}

void TestAtramentDrag::claimedDragCancelled()
{
    Harness h;
    h.tw.simulateTouchDown({20, 70});
    h.tw.simulateTouchMove({20, 120});
    // The platform takes the finger away: the elements hear of it (their drag ends), a release
    // cancelled, over nothing.
    h.tw.simulateTouchCancel();
    QCOMPARE(h.state().cancels, 1);
    QCOMPARE(h.state().ups, 0);
    // The next finger is a new gesture: beside the grip, a scroll.
    h.tw.simulateTouchDrag({100, 250}, {100, 70}, 200ms);
    QVERIFY(h.offset() > 100);
}

void TestAtramentDrag::unclaimedDragScrolls()
{
    Harness h;
    // The same drag beside the grip (row 1, x 100): a scroll, the elements see no press.
    h.tw.simulateTouchDrag({100, 250}, {100, 70}, 200ms);
    QVERIFY2(h.offset() > 100, qPrintable(QString::number(h.offset())));
    QCOMPARE(h.state().gripDowns, 0);
    QVERIFY(h.state().moves.empty());
    QCOMPARE(h.state().clicks, 0);
}

void TestAtramentDrag::dragAfterLongPress()
{
    Harness h;
    h.tw.simulateTouchDown({100, 120}); // row 2
    h.tw.advance(600ms);
    QCOMPARE(h.state().longPressed, 2);
    // The finger moving on: the elements' moves, no scroll.
    for (int y = 140; y <= 280; y += 20) {
        h.tw.advance(16ms);
        h.tw.simulateTouchMove({100, float(y)});
    }
    QCOMPARE(h.offset(), 0.0f);
    QVERIFY(h.state().moves.size() >= 7);
    QCOMPARE(h.state().moves.back().y, 280.0f);
    const int upsBefore = h.state().ups; // (the long press cancelled the elements' press)
    h.tw.simulateTouchUp({100, 280});
    QCOMPARE(h.state().ups, upsBefore + 1);
    QVERIFY(!h.state().lastUpCancelled);
    QCOMPARE(h.state().clicks, 0);
    QCOMPARE(h.offset(), 0.0f);
}

void TestAtramentDrag::longPressWithoutDragStaysStill()
{
    Harness h;
    h.state().dragOnLongPress = false;
    h.tw.simulateTouchDown({100, 120});
    h.tw.advance(600ms);
    QCOMPARE(h.state().longPressed, 2);
    const std::size_t movesBefore = h.state().moves.size();
    h.tw.simulateTouchMove({100, 200});
    h.tw.simulateTouchMove({100, 260});
    // Taken by the long press: no moves for the elements, no scroll.
    QCOMPARE(h.state().moves.size(), movesBefore);
    QCOMPARE(h.offset(), 0.0f);
    h.tw.simulateTouchUp({100, 260});
    QCOMPARE(h.state().clicks, 0);
}

void TestAtramentDrag::edgeScrollSpeed()
{
    // A view from 0 to 500: zones of 56 px (a fifth is 100, at most 56).
    QCOMPARE(brulion::atrament::edgeScrollSpeed(250, 0, 500), 0.0f);
    QCOMPARE(brulion::atrament::edgeScrollSpeed(57, 0, 500), 0.0f);
    QCOMPARE(brulion::atrament::edgeScrollSpeed(55, 0, 500), -2.5f);
    QCOMPARE(brulion::atrament::edgeScrollSpeed(0, 0, 500), -30.0f);
    QCOMPARE(brulion::atrament::edgeScrollSpeed(-100, 0, 500), -40.0f); // above the view: at most 40
    QCOMPARE(brulion::atrament::edgeScrollSpeed(446, 0, 500), 3.0f);
    QCOMPARE(brulion::atrament::edgeScrollSpeed(520, 0, 500), 40.0f);
    // A short view: zones of 16 px.
    QCOMPARE(brulion::atrament::edgeScrollSpeed(30, 0, 60), 0.0f);
    QCOMPARE(brulion::atrament::edgeScrollSpeed(10, 0, 60), -5.0f);
}

void TestAtramentDrag::edgeScrollerSteps()
{
    auto platform = std::make_shared<TestPlatform>();
    App app(platform, loadFonts());
    EdgeScroller scroller;
    std::vector<float> steps;
    auto step = [&](float dy) { steps.push_back(dy); };
    scroller.update(app, 10, step);
    QVERIFY(scroller.running());
    platform->advanceClock(50ms); // three steps of 16 ms
    QCOMPARE(steps.size(), std::size_t(3));
    QCOMPARE(steps.front(), 10.0f);
    // A new speed keeps the pace.
    scroller.update(app, -4, step);
    platform->advanceClock(16ms);
    QCOMPARE(steps.size(), std::size_t(4));
    QCOMPARE(steps.back(), -4.0f);
    // 0 stops it; so does stop().
    scroller.update(app, 0, step);
    QVERIFY(!scroller.running());
    platform->advanceClock(100ms);
    QCOMPARE(steps.size(), std::size_t(4));
    scroller.update(app, 5, step);
    scroller.stop();
    platform->advanceClock(100ms);
    QCOMPARE(steps.size(), std::size_t(4));
}

QTEST_GUILESS_MAIN(TestAtramentDrag)
#include "tst_atrament_drag.moc"
