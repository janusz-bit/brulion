// list(): rows of different heights over RowExtents (docs/ATRAMENT.md "The block editor in
// Atrament (M2)") — RowHeights' sums, a virtual list measuring what it shows, the anchor keeping
// the screen still while rows above it are measured, hidden rows, the end of the list, a new
// width, scrollToRow, rows inserted in front of the anchor, a finger's drag, the accessibility list.
#include "text/fonts.h"
#include "ui/atrament.h"
#include "ui/testplatform.h"

#include <QtTest>

#include <chrono>
#include <memory>
#include <string>

using namespace brulion::atrament;
using namespace std::chrono_literals;

namespace {

std::shared_ptr<FontCollection> loadFonts()
{
    static std::shared_ptr<FontCollection> fonts = [] {
        auto f = std::make_shared<FontCollection>();
        const std::string dir = BRULION_SOURCE_DIR "/assets/fonts/";
        f->addFontFile(dir + "ui/BrulionInter-Regular.ttf");
        f->addFontFile(dir + "BrulionEmoji.ttf");
        f->setFallbackFamilies({u"Brulion Emoji"});
        return f;
    }();
    return fonts;
}

bool near(Pixel p, Color c, int tolerance = 3)
{
    auto ch = [](float v) { return int(v * 255.0f + 0.5f); };
    return std::abs(int(p.r) - ch(c.r)) <= tolerance && std::abs(int(p.g) - ch(c.g)) <= tolerance
           && std::abs(int(p.b) - ch(c.b)) <= tolerance;
}

constexpr Color kRed = rgb(0xff0000);
constexpr Color kGreen = rgb(0x00ff00);
constexpr Color kBlue = rgb(0x0000ff);

struct Harness {
    std::shared_ptr<TestPlatform> platform = std::make_shared<TestPlatform>();
    App app{platform, loadFonts()};
    TestWindow &tw;
    explicit Harness(Size size) : tw(platform->newWindow(size, 1)) { }
    template<class T>
    Window &open(const Entity<T> &root)
    {
        Window &w = app.openWindow(tw, root);
        tw.settle();
        return w;
    }
};

// Rows 20, 40 or 60 px tall (by index), estimated at `estimate`; the marked row red, the others
// green and blue in turn.
struct Blocks {
    RowHeights heights;
    ListHandle handle;
    std::size_t marked = std::size_t(-1);
    int rendered = 0;
    float (*height)(std::size_t) = [](std::size_t i) { return 20.0f * float(1 + i % 3); };

    Blocks(std::size_t count, float estimate) : heights(count, estimate) { }
    Color colour(std::size_t i) const { return i == marked ? kRed : (i % 2 ? kBlue : kGreen); }
    AnyElement render(Window &, Context<Blocks> &)
    {
        Blocks *self = this;
        return div().sizeFull().flexCol().child(list(u"blocks", heights,
                                                     [self](std::size_t i, Window &, App &) -> AnyElement {
                                                         ++self->rendered;
                                                         return div().h(self->height(i)).bg(self->colour(i));
                                                     })
                                                        .trackScroll(handle)
                                                        .ariaLabel(u"Bloki")
                                                        .flex1());
    }
};

// Paragraphs that wrap: their height follows the width.
struct Paragraphs {
    RowHeights heights{50, 24};
    float width = 400;
    AnyElement render(Window &, Context<Paragraphs> &)
    {
        return div().sizeFull().child(list(u"text", heights,
                                           [](std::size_t i, Window &, App &) -> AnyElement {
                                               return div().textSize(16).child(
                                                       u"Akapit " + std::u16string(1, char16_t(u'0' + i % 10))
                                                       + u": zażółć gęślą jaźń, pchnąć w tę łódź jeża");
                                           })
                                              .w(width)
                                              .hFull());
    }
};

// Rows of 100 px but row 50: a part of `above` px (blue), a red stripe of 20, 60 px under it — a
// journal day whose blocks above the one on screen are laid out anew.
struct Parted {
    RowHeights heights{100, 100.0f};
    ListHandle handle;
    float above = 40;
    AnyElement render(Window &, Context<Parted> &)
    {
        Parted *self = this;
        return div().sizeFull().flexCol().child(
                list(u"parted", heights,
                     [self](std::size_t i, Window &, App &) -> AnyElement {
                         if (i != 50)
                             return div().h(100).bg(kGreen);
                         return div()
                                 .flexCol()
                                 .child(div().h(self->above).bg(kBlue))
                                 .child(div().h(20).bg(kRed))
                                 .child(div().h(60).bg(kBlue));
                     })
                        .trackScroll(handle)
                        .flex1());
    }
    // The view kept on the red stripe, `into` px below its top.
    void keepOnStripe(float into)
    {
        Parted *self = this;
        handle.anchorOnPart(50, [self](Window &) -> std::optional<RowPart> { return RowPart{self->above, 20}; }, into);
    }
};

} // namespace

class TestAtramentList : public QObject {
    Q_OBJECT
private Q_SLOTS:
    void rowHeights();
    void rowHeightsHiddenAndEdits();
    void measuresOnlyWhatItShows();
    void anchorKeepsTheScreenStill();
    void hiddenRowsAreSkipped();
    void endOfTheList();
    void newWidthMeasuresAgain();
    void scrollToRow_data();
    void scrollToRow();
    void scrollToRowPart();
    void anchorOnAPartOfARow();
    void rowsInsertedInFront();
    void fingerDrag();
    void accessibility();
    void textStyleReachesMeasuredRows();
    void headerAndFooter();
    void hiddenRowRootHasNoItem();
};

void TestAtramentList::rowHeights()
{
    RowHeights rows(5, 10);
    QCOMPARE(rows.count(), std::size_t(5));
    QCOMPARE(rows.total(), 50.0f);
    QCOMPARE(rows.measuredCount(), std::size_t(0));
    rows.setMeasured(2, 30.5f);
    QVERIFY(rows.isMeasured(2));
    QCOMPARE(rows.measuredCount(), std::size_t(1));
    QCOMPARE(rows.offsetOf(0), 0.0f);
    QCOMPARE(rows.offsetOf(3), 50.5f);
    QCOMPARE(rows.offsetOf(5), 70.5f);
    QCOMPARE(rows.offsetOf(99), 70.5f); // clamped
    QCOMPARE(rows.rowAt(-5), std::size_t(0));
    QCOMPARE(rows.rowAt(0), std::size_t(0));
    QCOMPARE(rows.rowAt(9.9f), std::size_t(0));
    QCOMPARE(rows.rowAt(10), std::size_t(1));
    QCOMPARE(rows.rowAt(20), std::size_t(2));
    QCOMPARE(rows.rowAt(50.4f), std::size_t(2));
    QCOMPARE(rows.rowAt(50.5f), std::size_t(3));
    QCOMPARE(rows.rowAt(70.5f), std::size_t(4)); // at or past the end: the last
    QCOMPARE(rows.rowAt(1e9f), std::size_t(4));
    // Fractions sum exactly (1/64 px): no drift after many updates.
    for (int k = 0; k < 1001; ++k)
        rows.setHeight(1, k % 2 ? 0.1f : 13.37f);
    QCOMPARE(rows.offsetOf(2), 10.0f + 13.375f); // 13.37 in 1/64 px
    QVERIFY(RowHeights().rowAt(0) == 0);         // no rows: count()
    RowHeights empty(3, 0);
    QCOMPARE(empty.rowAt(5), std::size_t(3)); // none has a height
}

void TestAtramentList::rowHeightsHiddenAndEdits()
{
    RowHeights rows(6, 10);
    rows.hide(0);
    rows.hide(2);
    rows.hide(3);
    QVERIFY(rows.isHidden(2));
    QVERIFY(!rows.isHidden(1));
    // Never a hidden row: above the first row with a height, that row.
    QCOMPARE(rows.rowAt(0), std::size_t(1));
    QCOMPARE(rows.rowAt(10), std::size_t(4)); // rows 2 and 3 start at 10 too, hidden
    QCOMPARE(rows.offsetOf(4), 10.0f);
    // A new width: heights are estimates again, hidden rows stay hidden.
    rows.setMeasured(1, 25);
    rows.markAllEstimated();
    QVERIFY(!rows.isMeasured(1));
    QCOMPARE(rows.heightOf(1), 25.0f);
    QVERIFY(rows.isHidden(2));
    QCOMPARE(rows.measuredCount(), std::size_t(3));
    // Inserting and removing.
    rows.insert(1, 2, 5);
    QCOMPARE(rows.count(), std::size_t(8));
    QCOMPARE(rows.offsetOf(3), 10.0f);
    QCOMPARE(rows.rowAt(7), std::size_t(2));
    rows.remove(0, 3);
    QCOMPARE(rows.count(), std::size_t(5));
    QCOMPARE(rows.heightOf(0), 25.0f);
    QCOMPARE(rows.total(), 45.0f);
    rows.remove(4, 100); // clamped
    QCOMPARE(rows.count(), std::size_t(4));
    rows.insert(100, 1, 7); // at the end
    QCOMPARE(rows.heightOf(4), 7.0f);
    rows.setEstimated(0);
    QVERIFY(!rows.isMeasured(0));
    rows.setHeight(1, -3); // negative: 0
    QCOMPARE(rows.heightOf(1), 0.0f);
}

void TestAtramentList::measuresOnlyWhatItShows()
{
    Harness h({100, 200});
    Entity<Blocks> blocks = h.app.newEntity<Blocks>(100000, 30.0f);
    h.open(blocks);
    const Blocks &b = blocks.read(h.app);
    // 200 px of rows 20/40/60 tall: rows 0..5 (0+20+40+60+20+40 = 180, row 5 cut), about ten
    // rendered per frame, not 100 000.
    QVERIFY2(b.rendered > 0 && b.rendered <= 20, qPrintable(QString::number(b.rendered)));
    QVERIFY(b.heights.measuredCount() >= 6 && b.heights.measuredCount() <= 8);
    QCOMPARE(b.heights.heightOf(2), 60.0f);
    QVERIFY(near(h.tw.pixel(50, 10), kGreen));  // row 0: 0..20
    QVERIFY(near(h.tw.pixel(50, 30), kBlue));   // row 1: 20..60
    QVERIFY(near(h.tw.pixel(50, 100), kGreen)); // row 2: 60..120
    QVERIFY(near(h.tw.pixel(50, 130), kBlue));  // row 3: 120..140
    // Far down and back: still only what is shown is laid out.
    const int before = b.rendered;
    h.tw.simulateScroll({50, 50}, {0, 30.0f * 50000});
    QVERIFY(b.rendered - before <= 20);
    QVERIFY(b.heights.measuredCount() <= 20);
    h.tw.simulateScroll({50, 50}, {0, 1e9f});
    // The last row (99 999: 20 px, odd: blue) at the bottom, measured.
    QVERIFY(near(h.tw.pixel(50, 195), kBlue));
    QVERIFY(b.heights.isMeasured(99999));
    QCOMPARE(b.handle.scroll().offset.y, b.heights.total() - 200);
}

void TestAtramentList::anchorKeepsTheScreenStill()
{
    // Rows estimated at 100 px, really 20/40/60: scrolling up into rows not measured yet must not
    // move the rows on screen.
    Harness h({100, 200});
    Entity<Blocks> blocks = h.app.newEntity<Blocks>(1000, 100.0f);
    blocks.update(h.app, [](Blocks &b, Context<Blocks> &) { b.marked = 500; });
    h.open(blocks);
    blocks.update(h.app, [](Blocks &b, Context<Blocks> &cx) {
        b.handle.scrollToRow(500);
        cx.notify();
    });
    h.tw.settle();
    QVERIFY(near(h.tw.pixel(50, 1), kRed)); // row 500 at the top
    QCOMPARE(blocks.read(h.app).handle.anchorRow(), std::size_t(500));
    QVERIFY(!blocks.read(h.app).heights.isMeasured(499));

    // 30 px up: into row 499 (estimated 100, 40 really). Row 500 ends 30 px lower, row 499 above it.
    h.tw.simulateScroll({50, 50}, {0, -30});
    QVERIFY(near(h.tw.pixel(50, 29), kBlue)); // row 499 (40 px: -10..30)
    QVERIFY(near(h.tw.pixel(50, 31), kRed));
    QVERIFY(blocks.read(h.app).heights.isMeasured(499));
    // Row 500 (40 px: 500 % 3 == 2 → 60 px) spans 30..90.
    QVERIFY(near(h.tw.pixel(50, 89), kRed));
    QVERIFY(!near(h.tw.pixel(50, 91), kRed));
    // Further up, many rows at once: row 500's top stays where the scroll puts it.
    h.tw.simulateScroll({50, 50}, {0, -100});
    QVERIFY(near(h.tw.pixel(50, 131), kRed));
    QVERIFY(!near(h.tw.pixel(50, 129), kRed));
}

void TestAtramentList::hiddenRowsAreSkipped()
{
    Harness h({100, 200});
    Entity<Blocks> blocks = h.app.newEntity<Blocks>(10, 20.0f);
    blocks.update(h.app, [](Blocks &b, Context<Blocks> &) {
        b.height = [](std::size_t) { return 20.0f; };
        b.marked = 4;
        b.heights.hide(1);
        b.heights.hide(2);
        b.heights.hide(3);
    });
    h.open(blocks);
    QVERIFY(near(h.tw.pixel(50, 10), kGreen));       // row 0
    QVERIFY(near(h.tw.pixel(50, 30), kRed));         // row 4 right after it
    QVERIFY(blocks.read(h.app).heights.isHidden(2)); // never laid out: still hidden
    QCOMPARE(blocks.read(h.app).heights.total(), 7 * 20.0f);
}

void TestAtramentList::endOfTheList()
{
    // Estimates too large: the end found by scrolling there is shorter than the estimate — the
    // last row ends at the bottom of the view, no gap below it.
    Harness h({100, 200});
    Entity<Blocks> blocks = h.app.newEntity<Blocks>(300, 200.0f);
    blocks.update(h.app, [](Blocks &b, Context<Blocks> &) { b.marked = 299; });
    h.open(blocks);
    h.tw.simulateScroll({50, 50}, {0, 1e7f});
    h.tw.settle();
    const Blocks &b = blocks.read(h.app);
    QVERIFY(near(h.tw.pixel(50, 199), kRed)); // row 299 (299 % 3 == 2: 60 px) at the bottom
    QVERIFY(near(h.tw.pixel(50, 141), kRed));
    QVERIFY(!near(h.tw.pixel(50, 139), kRed));
    QCOMPARE(b.handle.scroll().offset.y + 200, b.heights.total());
    // A list shorter than its view: at the top, nothing to scroll.
    Harness small({100, 200});
    Entity<Blocks> few = small.app.newEntity<Blocks>(2, 500.0f);
    small.open(few);
    QCOMPARE(few.read(small.app).heights.total(), 60.0f);
    QCOMPARE(few.read(small.app).handle.scroll().offset.y, 0.0f);
    small.tw.simulateScroll({50, 50}, {0, 100});
    QCOMPARE(few.read(small.app).handle.scroll().offset.y, 0.0f);
    QVERIFY(near(small.tw.pixel(50, 10), kGreen));
}

void TestAtramentList::newWidthMeasuresAgain()
{
    Harness h({600, 300});
    Entity<Paragraphs> paragraphs = h.app.newEntity<Paragraphs>();
    h.open(paragraphs);
    const float wide = paragraphs.read(h.app).heights.heightOf(0);
    QVERIFY(paragraphs.read(h.app).heights.isMeasured(0));
    paragraphs.update(h.app, [](Paragraphs &p, Context<Paragraphs> &cx) {
        p.width = 120;
        cx.notify();
    });
    h.tw.settle();
    const RowHeights &rows = paragraphs.read(h.app).heights;
    QVERIFY2(rows.heightOf(0) > wide * 2, qPrintable(QStringLiteral("%1 → %2").arg(wide).arg(rows.heightOf(0))));
    QVERIFY(rows.isMeasured(0));
    // Rows measured at the old width, out of view now, keep that height as an estimate; rows never
    // shown keep theirs.
    QVERIFY(!rows.isMeasured(10));
    QCOMPARE(rows.heightOf(10), wide);
    QCOMPARE(rows.heightOf(40), 24.0f);
}

void TestAtramentList::scrollToRow_data()
{
    QTest::addColumn<int>("position");
    QTest::addColumn<int>("top"); // where row 400 starts on screen
    // Row 400 is 40 px (400 % 3 == 1).
    QTest::newRow("start") << int(ListPosition::Start) << 0;
    QTest::newRow("center") << int(ListPosition::Center) << 80;
    QTest::newRow("end") << int(ListPosition::End) << 160;
    QTest::newRow("nearest (below the view)") << int(ListPosition::Nearest) << 160;
}

void TestAtramentList::scrollToRow()
{
    QFETCH(int, position);
    QFETCH(int, top);
    Harness h({100, 200});
    Entity<Blocks> blocks = h.app.newEntity<Blocks>(1000, 37.0f);
    blocks.update(h.app, [](Blocks &b, Context<Blocks> &) { b.marked = 400; });
    h.open(blocks);
    blocks.update(h.app, [position](Blocks &b, Context<Blocks> &cx) {
        b.handle.scrollToRow(400, ListPosition(position));
        cx.notify();
    });
    h.tw.settle();
    QVERIFY(near(h.tw.pixel(50, top + 1), kRed));
    QVERIFY(near(h.tw.pixel(50, top + 39), kRed));
    if (top > 0)
        QVERIFY(!near(h.tw.pixel(50, top - 1), kRed));
    if (top + 40 < 200)
        QVERIFY(!near(h.tw.pixel(50, top + 41), kRed));
    // Nearest of a row in view: nothing moves.
    const float y = blocks.read(h.app).handle.scroll().offset.y;
    blocks.update(h.app, [](Blocks &b, Context<Blocks> &cx) {
        b.handle.scrollToRow(400, ListPosition::Nearest);
        cx.notify();
    });
    h.tw.settle();
    QCOMPARE(blocks.read(h.app).handle.scroll().offset.y, y);
}

void TestAtramentList::scrollToRowPart()
{
    // Rows of 100 px in a view of 200: a part of row 50 (a block inside a journal day) shown.
    Harness h({100, 200});
    Entity<Blocks> blocks = h.app.newEntity<Blocks>(100, 37.0f);
    blocks.update(h.app, [](Blocks &b, Context<Blocks> &) {
        b.height = [](std::size_t) { return 100.0f; };
        b.marked = 50;
    });
    h.open(blocks);
    int asked = 0;
    int renderedWhenAsked = -1;
    auto part = [&](float top, float height) {
        return [&, top, height](Window &) -> std::optional<RowPart> {
            ++asked;
            renderedWhenAsked = blocks.read(h.app).rendered;
            return RowPart{top, height};
        };
    };
    // Start: the part's top (60 px into the row) at the view's top — asked once the row was laid out.
    const int renderedBefore = blocks.read(h.app).rendered;
    blocks.update(h.app, [&](Blocks &b, Context<Blocks> &cx) {
        b.handle.scrollToRowPart(50, part(60, 20), ListPosition::Start);
        cx.notify();
    });
    h.tw.settle();
    QCOMPARE(asked, 1);
    QVERIFY(renderedWhenAsked > renderedBefore);
    QVERIFY(near(h.tw.pixel(50, 1), kRed));
    QVERIFY(near(h.tw.pixel(50, 38), kRed));
    QVERIFY(!near(h.tw.pixel(50, 42), kRed));
    // Nearest, the part above the view, 5 px of air above it: the row's top 5 px above the view's.
    blocks.update(h.app, [&](Blocks &b, Context<Blocks> &cx) {
        b.handle.scrollToRowPart(50, part(10, 10), ListPosition::Nearest, 5, 5);
        cx.notify();
    });
    h.tw.settle();
    QVERIFY(near(h.tw.pixel(50, 1), kRed));
    QVERIFY(near(h.tw.pixel(50, 93), kRed));
    QVERIFY(!near(h.tw.pixel(50, 97), kRed));
    // Nearest, the part below the view: its end and 10 px under it at the view's bottom.
    blocks.update(h.app, [](Blocks &b, Context<Blocks> &cx) {
        b.handle.scrollToRow(40);
        cx.notify();
    });
    h.tw.settle();
    blocks.update(h.app, [&](Blocks &b, Context<Blocks> &cx) {
        b.handle.scrollToRowPart(50, part(80, 20), ListPosition::Nearest, 0, 10);
        cx.notify();
    });
    h.tw.settle();
    QVERIFY(near(h.tw.pixel(50, 92), kRed));
    QVERIFY(!near(h.tw.pixel(50, 88), kRed));
    // Nearest, the part in view: nothing moves; no part known: the whole row.
    const float y = blocks.read(h.app).handle.scroll().offset.y;
    blocks.update(h.app, [&](Blocks &b, Context<Blocks> &cx) {
        b.handle.scrollToRowPart(50, part(20, 20), ListPosition::Nearest);
        cx.notify();
    });
    h.tw.settle();
    QCOMPARE(blocks.read(h.app).handle.scroll().offset.y, y);
    blocks.update(h.app, [](Blocks &b, Context<Blocks> &cx) {
        b.handle.scrollToRowPart(50, [](Window &) -> std::optional<RowPart> { return std::nullopt; },
                                 ListPosition::Start);
        cx.notify();
    });
    h.tw.settle();
    QVERIFY(near(h.tw.pixel(50, 1), kRed));
    QVERIFY(near(h.tw.pixel(50, 98), kRed));
    QVERIFY(!near(h.tw.pixel(50, 102), kRed));
}

void TestAtramentList::anchorOnAPartOfARow()
{
    Harness h({100, 200});
    Entity<Parted> parted = h.app.newEntity<Parted>();
    h.open(parted);
    parted.update(h.app, [](Parted &p, Context<Parted> &cx) {
        p.handle.scrollToAnchor(50, 40);
        cx.notify();
    });
    h.tw.settle();
    QVERIFY(near(h.tw.pixel(50, 1), kRed)); // the stripe at the view's top
    QVERIFY(near(h.tw.pixel(50, 21), kBlue));
    // The part above it grows by 60 px: anchored on the row, the stripe goes down with it…
    parted.update(h.app, [](Parted &p, Context<Parted> &cx) {
        p.above = 100;
        cx.notify();
    });
    h.tw.settle();
    QVERIFY(near(h.tw.pixel(50, 61), kRed));
    // …kept on the part, it stays where it is.
    parted.update(h.app, [](Parted &p, Context<Parted> &cx) {
        p.handle.scrollToAnchor(50, 100);
        cx.notify();
    });
    h.tw.settle();
    QVERIFY(near(h.tw.pixel(50, 1), kRed));
    parted.update(h.app, [](Parted &p, Context<Parted> &cx) {
        p.above = 30;
        p.keepOnStripe(0);
        cx.notify();
    });
    h.tw.settle();
    QVERIFY(near(h.tw.pixel(50, 1), kRed));
    QVERIFY(near(h.tw.pixel(50, 21), kBlue));
    QCOMPARE(parted.read(h.app).handle.anchorInto(), 30.0f);
    // A scroll meanwhile moves it by the scroll only: 10 px up, the part grown again.
    parted.update(h.app, [](Parted &p, Context<Parted> &) {
        p.above = 90;
        p.keepOnStripe(0);
    });
    h.tw.simulateScroll({50, 50}, {0, -10});
    QVERIFY(near(h.tw.pixel(50, 11), kRed));
    QVERIFY(!near(h.tw.pixel(50, 9), kRed));
    // For one frame only: the part changes again, nobody keeps the view on it — it goes with the row.
    parted.update(h.app, [](Parted &p, Context<Parted> &cx) {
        p.above = 120;
        cx.notify();
    });
    h.tw.settle();
    QVERIFY(near(h.tw.pixel(50, 41), kRed));
    // A jump (scrollToRow) wins over a part kept.
    parted.update(h.app, [](Parted &p, Context<Parted> &cx) {
        p.keepOnStripe(0);
        p.handle.scrollToRow(10);
        cx.notify();
    });
    h.tw.settle();
    QCOMPARE(parted.read(h.app).handle.anchorRow(), std::size_t(10));
}

void TestAtramentList::rowsInsertedInFront()
{
    Harness h({100, 200});
    Entity<Blocks> blocks = h.app.newEntity<Blocks>(100, 20.0f);
    blocks.update(h.app, [](Blocks &b, Context<Blocks> &) {
        b.height = [](std::size_t) { return 20.0f; };
        b.marked = 50;
    });
    h.open(blocks);
    blocks.update(h.app, [](Blocks &b, Context<Blocks> &cx) {
        b.handle.scrollToRow(50);
        cx.notify();
    });
    h.tw.settle();
    QVERIFY(near(h.tw.pixel(50, 1), kRed));
    // Five rows inserted at 10 (the model's change, told to the handle): the marked row is 55 now
    // and stays at the top.
    blocks.update(h.app, [](Blocks &b, Context<Blocks> &cx) {
        b.heights.insert(10, 5, 20);
        b.handle.rowsInserted(10, 5);
        b.marked = 55;
        cx.notify();
    });
    h.tw.settle();
    QVERIFY(near(h.tw.pixel(50, 1), kRed));
    QCOMPARE(blocks.read(h.app).handle.anchorRow(), std::size_t(55));
    // Removed in front: the same.
    blocks.update(h.app, [](Blocks &b, Context<Blocks> &cx) {
        b.heights.remove(0, 30);
        b.handle.rowsRemoved(0, 30);
        b.marked = 25;
        cx.notify();
    });
    h.tw.settle();
    QVERIFY(near(h.tw.pixel(50, 1), kRed));
    // At the very top, rows inserted there are shown (the top is sticky).
    blocks.update(h.app, [](Blocks &b, Context<Blocks> &cx) {
        b.handle.scrollToRow(0);
        cx.notify();
    });
    h.tw.settle();
    blocks.update(h.app, [](Blocks &b, Context<Blocks> &cx) {
        b.heights.insert(0, 1, 20);
        b.handle.rowsInserted(0, 1);
        b.marked = 0;
        cx.notify();
    });
    h.tw.settle();
    QVERIFY(near(h.tw.pixel(50, 1), kRed));
}

void TestAtramentList::fingerDrag()
{
    Harness h({100, 200});
    Entity<Blocks> blocks = h.app.newEntity<Blocks>(1000, 40.0f);
    h.open(blocks);
    // A slow drag of 120 px (no fling): the content follows the finger.
    h.tw.simulateTouchDrag({50, 180}, {50, 60}, 600ms, 12);
    h.tw.advance(500ms);
    const float y = blocks.read(h.app).handle.scroll().offset.y;
    QVERIFY2(y > 100 && y < 140, qPrintable(QString::number(y)));
    // A flick: the fling carries on.
    h.tw.simulateTouchDrag({50, 180}, {50, 20}, 60ms, 4);
    const float released = blocks.read(h.app).handle.scroll().offset.y;
    h.tw.advance(1000ms);
    QVERIFY2(blocks.read(h.app).handle.scroll().offset.y > released + 100,
             qPrintable(QStringLiteral("%1 → %2").arg(released).arg(blocks.read(h.app).handle.scroll().offset.y)));
}

void TestAtramentList::accessibility()
{
    Harness h({100, 200});
    Entity<Blocks> blocks = h.app.newEntity<Blocks>(500, 30.0f);
    blocks.update(h.app, [](Blocks &b, Context<Blocks> &) { b.heights.hide(1); });
    Window &w = h.open(blocks);
    w.setAccessibilityEnabled(true);
    h.tw.settle();
    const AccessibilityTree &tree = w.accessibilityTree();
    const AccessibilityNode *listNode = nullptr;
    for (const AccessibilityNode *n : tree.nodes())
        if (!listNode && n->role == Role::List)
            listNode = n;
    QVERIFY(listNode);
    QCOMPARE(QString::fromStdU16String(std::u16string(listNode->name)), QStringLiteral("Bloki"));
    QCOMPARE(listNode->rowCount, std::int64_t(500));
    QVERIFY(listNode->scrolls);
    QVERIFY(listNode->childCount >= 5);
    // Items known by their row: row 1 is hidden, so the second item is row 2.
    QCOMPARE(tree.child(*listNode, 0)->rowIndex, std::int64_t(0));
    QCOMPARE(tree.child(*listNode, 1)->rowIndex, std::int64_t(2));
    QCOMPARE(tree.child(*listNode, 1)->bounds.top(), 20.0f);
}

void TestAtramentList::textStyleReachesMeasuredRows()
{
    // The list's text size applies to its rows when they are measured (the first frame), not only
    // when they are painted: 32 px text is taller than 12 px text.
    struct Sized {
        RowHeights heights{3, 10};
        float size = 12;
        AnyElement render(Window &, Context<Sized> &)
        {
            return div().sizeFull().child(
                    list(u"sized", heights,
                         [](std::size_t, Window &, App &) -> AnyElement { return div().child(u"Zażółć"); })
                            .textSize(size)
                            .hFull());
        }
    };
    Harness small({200, 200});
    Entity<Sized> a = small.app.newEntity<Sized>();
    small.open(a);
    Harness big({200, 200});
    Entity<Sized> b = big.app.newEntity<Sized>();
    b.update(big.app, [](Sized &s, Context<Sized> &) { s.size = 32; });
    big.open(b);
    QVERIFY2(b.read(big.app).heights.heightOf(0) > a.read(small.app).heights.heightOf(0) * 2,
             qPrintable(QStringLiteral("%1 vs %2") .arg(a.read(small.app).heights.heightOf(0)).arg(b.read(big.app).heights.heightOf(0))));
}

// A list with a header (a red bar) and a footer (a green one), rows blue: 20 px each.
struct Edged {
    RowHeights heights{100, 20};
    ListHandle handle;
    float header = 50;
    float footer = 70;
    AnyElement render(Window &, Context<Edged> &)
    {
        Edged *self = this;
        return div().sizeFull().flexCol().child(
                list(u"edged", heights,
                     [](std::size_t i, Window &, App &) -> AnyElement {
                         return div().h(20).bg(i % 2 ? kBlue : rgb(0x000080));
                     })
                        .header([self](Window &, App &) -> AnyElement { return div().h(self->header).bg(kRed); })
                        .footer([self](Window &, App &) -> AnyElement { return div().h(self->footer).bg(kGreen); })
                        .trackScroll(handle)
                        .flex1());
    }
};

void TestAtramentList::headerAndFooter()
{
    Harness h({100, 200});
    Entity<Edged> edged = h.app.newEntity<Edged>();
    Window &w = h.open(edged);
    // The header above row 0, the rows' offsets without it.
    QVERIFY(near(h.tw.pixel(50, 1), kRed));
    QVERIFY(near(h.tw.pixel(50, 49), kRed));
    QVERIFY(near(h.tw.pixel(50, 51), rgb(0x000080))); // row 0
    QCOMPARE(edged.read(h.app).heights.offsetOf(1), 20.0f);
    QCOMPARE(edged.read(h.app).handle.scroll().content.height, 50.0f + 100 * 20 + 70);
    // At the very top, a taller header keeps the top in view (the rows move down).
    edged.update(h.app, [](Edged &e, Context<Edged> &cx) {
        e.header = 90;
        cx.notify();
    });
    h.tw.settle();
    QVERIFY(near(h.tw.pixel(50, 1), kRed));
    QVERIFY(near(h.tw.pixel(50, 89), kRed));
    QVERIFY(near(h.tw.pixel(50, 91), rgb(0x000080)));
    // Scrolled into the rows: the header growing above moves nothing on screen.
    h.tw.simulateScroll({50, 100}, {0, 400});
    const RasterTarget before = h.tw.image();
    edged.update(h.app, [](Edged &e, Context<Edged> &cx) {
        e.header = 140;
        cx.notify();
    });
    h.tw.settle();
    int differ = 0;
    for (int y = 0; y < 200; ++y)
        for (int x = 0; x < 80; ++x) // (the scroll bar moves)
            differ += h.tw.pixel(x, y).b != before.at(x, y).b ? 1 : 0;
    QCOMPARE(differ, 0);
    // scrollToRow(0) puts row 0 at the top, the header above the view.
    edged.update(h.app, [](Edged &e, Context<Edged> &cx) {
        e.handle.scrollToRow(0);
        cx.notify();
    });
    h.tw.settle();
    QVERIFY(near(h.tw.pixel(50, 1), rgb(0x000080)));
    // scrollToTop() shows the very top: the header again (a page opened anew).
    edged.update(h.app, [](Edged &e, Context<Edged> &cx) {
        e.handle.scrollToTop();
        cx.notify();
    });
    h.tw.settle();
    QVERIFY(near(h.tw.pixel(50, 1), kRed));
    QCOMPARE(edged.read(h.app).handle.scroll().offset.y, 0.0f);
    // The end: the footer at the bottom, below the last row.
    h.tw.simulateScroll({50, 100}, {0, 1e7f});
    QVERIFY(near(h.tw.pixel(50, 199), kGreen));
    QVERIFY(near(h.tw.pixel(50, 131), kGreen));
    QVERIFY(near(h.tw.pixel(50, 129), kBlue)); // row 99
    // No list item for the header or the footer.
    w.setAccessibilityEnabled(true);
    h.tw.settle();
    const AccessibilityTree &tree = w.accessibilityTree();
    for (const AccessibilityNode *n : tree.nodes())
        if (n->role == Role::ListItem)
            QVERIFY(n->rowIndex >= 0 && n->rowIndex < 100);
    // An empty list shows both.
    Harness empty({100, 200});
    Entity<Edged> none = empty.app.newEntity<Edged>();
    none.update(empty.app, [](Edged &e, Context<Edged> &) { e.heights.reset(0, 20); });
    empty.open(none);
    QVERIFY(near(empty.tw.pixel(50, 10), kRed));
    QVERIFY(near(empty.tw.pixel(50, 60), kGreen));
}

void TestAtramentList::hiddenRowRootHasNoItem()
{
    // A row whose root is aria-hidden (a Qt Quick item has it: qthost.h "Hosted children") is no
    // list item; the others keep theirs with their indices.
    struct Hosting {
        RowHeights heights{10, 30};
        AnyElement render(Window &, Context<Hosting> &)
        {
            return div().sizeFull().child(list(u"hosting", heights, [](std::size_t i, Window &, App &) -> AnyElement {
                                              Div row = div().h(30).child(u"Wiersz z tekstem");
                                              if (i == 2)
                                                  return std::move(row).ariaHidden();
                                              return std::move(row);
                                          }).hFull());
        }
    };
    Harness h({100, 200});
    Entity<Hosting> hosting = h.app.newEntity<Hosting>();
    Window &w = h.open(hosting);
    w.setAccessibilityEnabled(true);
    h.tw.settle();
    std::vector<std::int64_t> items;
    for (const AccessibilityNode *n : w.accessibilityTree().nodes())
        if (n->role == Role::ListItem)
            items.push_back(n->rowIndex);
    QVERIFY(items.size() >= 5);
    QCOMPARE(items[0], std::int64_t(0));
    QCOMPARE(items[1], std::int64_t(1));
    QCOMPARE(items[2], std::int64_t(3));
}

QTEST_GUILESS_MAIN(TestAtramentList)
#include "tst_atrament_list.moc"
