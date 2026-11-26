// Atrament's ui layer on the test platform: entities and effects, the frame pipeline, layout and
// painting through the CPU renderer, mouse, keyboard, focus, key bindings, the input-method
// interface, scrolling, long lists, element state, timers.
#include "text/fonts.h"
#include "ui/atrament.h"
#include "ui/testplatform.h"

#include <QtTest>

#include <memory>

using namespace brulion::atrament;

namespace {

std::shared_ptr<FontCollection> loadFonts()
{
    static std::shared_ptr<FontCollection> fonts = [] {
        auto f = std::make_shared<FontCollection>();
        const std::string dir = BRULION_SOURCE_DIR "/assets/fonts/";
        f->addFontFile(dir + "ui/BrulionInter-Regular.ttf");
        f->addFontFile(dir + "ui/BrulionInter-Bold.ttf");
        f->addFontFile(dir + "BrulionEmoji.ttf");
        f->setFallbackFamilies({ u"Brulion Emoji" });
        return f;
    }();
    return fonts;
}

bool near(Pixel p, Color c, int tolerance = 3)
{
    auto ch = [](float v) { return int(v * 255.0f + 0.5f); };
    return std::abs(int(p.r) - ch(c.r)) <= tolerance && std::abs(int(p.g) - ch(c.g)) <= tolerance
            && std::abs(int(p.b) - ch(c.b)) <= tolerance && std::abs(int(p.a) - ch(c.a)) <= tolerance;
}

QByteArray describe(Pixel p)
{
    return QByteArray::number(p.r) + ',' + QByteArray::number(p.g) + ',' + QByteArray::number(p.b) + ','
            + QByteArray::number(p.a);
}

constexpr Color kRed = rgb(0xff0000);
constexpr Color kGreen = rgb(0x00ff00);
constexpr Color kBlue = rgb(0x0000ff);

// A fixture: a test platform, an app with the bundled fonts, one window.
struct Harness {
    std::shared_ptr<TestPlatform> platform = std::make_shared<TestPlatform>();
    App app { platform, loadFonts() };
    TestWindow &tw;
    explicit Harness(Size size = { 200, 100 }, float scale = 1)
        : tw(platform->newWindow(size, scale))
    {
    }
    template<class T>
    Window &open(const Entity<T> &root)
    {
        Window &w = app.openWindow(tw, root);
        tw.settle();
        return w;
    }
};

// --- views used by the tests

struct Counter {
    int count = 0;
    int clicks = 0;
    AnyElement render(Window &, Context<Counter> &cx)
    {
        return div().sizeFull().bg(white()).child(div()
                        .id(u"button")
                        .absolute()
                        .left(10)
                        .top(10)
                        .w(50)
                        .h(30)
                        .bg(kRed)
                        .hover([](Style &s) { s.background = Background::solid(kGreen); })
                        .cursorPointer()
                        .onClick(cx.listener([](Counter &self, const ClickEvent &, Window &, Context<Counter> &cx) {
                            ++self.count;
                            ++self.clicks;
                            cx.notify();
                        })));
    }
};

struct Row {
    AnyElement render(Window &, Context<Row> &)
    {
        return div()
                .sizeFull()
                .flexRow()
                .gap(10)
                .p(10)
                .bg(white())
                .child(div().w(30).hFull().bg(kRed))
                .child(div().flex1().hFull().bg(kGreen))
                .child(div().w(30).hFull().bg(kBlue));
    }
};

struct Model {
    int value = 0;
};
struct ModelEvent {
    int value;
};

struct Watcher {
    Entity<Model> model;
    int seen = 0;
    int events = 0;
    std::vector<Subscription> subscriptions;
    Watcher(Context<Watcher> &cx, Entity<Model> m)
        : model(std::move(m))
    {
        subscriptions.push_back(cx.observe(model, [](Watcher &self, Context<Watcher> &) { ++self.seen; }));
        subscriptions.push_back(cx.subscribe<ModelEvent>(
                model, [](Watcher &self, const ModelEvent &e, Context<Watcher> &) { self.events += e.value; }));
    }
};

struct Keys {
    FocusHandle focus;
    std::vector<std::u16string> log;
    explicit Keys(Context<Keys> &cx)
        : focus(cx.newFocusHandle())
    {
    }
    AnyElement render(Window &, Context<Keys> &cx)
    {
        return div()
                .sizeFull()
                .keyContext(u"Workspace")
                .onAction(u"workspace::save", cx.listener([](Keys &self, Window &, Context<Keys> &) {
                    self.log.push_back(u"save");
                }))
                .child(div()
                                .id(u"editor")
                                .w(100)
                                .h(50)
                                .trackFocus(focus)
                                .keyContext(u"Editor")
                                .onKeyDown(cx.listener(
                                        [](Keys &self, const KeyDownEvent &e, Window &, Context<Keys> &) {
                                            self.log.push_back(u"key:" + e.keystroke.toString());
                                        }))
                                .onAction(u"editor::bold", cx.listener([](Keys &self, Window &, Context<Keys> &) {
                                    self.log.push_back(u"bold");
                                })));
    }
};

// A minimal text field: its own InputHandler over a string.
struct Field {
    FocusHandle focus;
    std::u16string text;
    InputHandler::Selection selection;
    std::optional<Range> marked;
    explicit Field(Context<Field> &cx)
        : focus(cx.newFocusHandle())
    {
    }

    struct Handler final : InputHandler {
        App *app;
        EntityId id;
        Field *field() { return app->tryGet<Field>(id); }
        std::u16string textForRange(Range r) override
        {
            return field()->text.substr(std::size_t(r.start), std::size_t(r.length()));
        }
        int textLength() override { return int(field()->text.size()); }
        std::optional<Selection> selection() override { return field()->selection; }
        std::optional<Range> markedRange() override { return field()->marked; }
        void replaceText(std::optional<Range> r, std::u16string_view t) override
        {
            Field *f = field();
            const Range range = r ? *r : (f->marked ? *f->marked : f->selection.range());
            f->text.replace(std::size_t(range.start), std::size_t(range.length()), t);
            const int caret = range.start + int(t.size());
            f->selection = { caret, caret };
            f->marked.reset();
            app->notify(id);
        }
        void replaceAndMarkText(std::optional<Range> r, std::u16string_view t, std::optional<Range>) override
        {
            Field *f = field();
            const Range range = r ? *r : (f->marked ? *f->marked : f->selection.range());
            f->text.replace(std::size_t(range.start), std::size_t(range.length()), t);
            f->marked = Range { range.start, range.start + int(t.size()) };
            const int caret = range.start + int(t.size());
            f->selection = { caret, caret };
            app->notify(id);
        }
        void unmarkText() override { field()->marked.reset(); }
        void setSelection(Selection s) override { field()->selection = s; }
        std::optional<Rect> boundsForRange(Range) override { return Rect::fromXYWH(0, 0, 10, 10); }
        std::optional<int> offsetForPoint(Point) override { return 0; }
    };

    struct FieldElement final : Element {
        FocusHandle focus;
        App *app = nullptr;
        EntityId id = 0;
        NodeId requestLayout(Window &w, App &) override
        {
            LayoutStyle s;
            s.width = Length::px(100);
            s.height = Length::px(20);
            return w.requestLayout(s, {});
        }
        void paint(Rect, Window &w, App &) override
        {
            auto *h = Arena::current()->make<Handler>();
            h->app = app;
            h->id = id;
            w.handleInput(focus, h);
        }
    };

    AnyElement render(Window &, Context<Field> &cx)
    {
        FieldElement e;
        e.focus = focus;
        e.app = &cx.app();
        e.id = cx.entityId();
        return div().sizeFull().trackFocus(focus).child(std::move(e));
    }
};

struct Scroller {
    AnyElement render(Window &, Context<Scroller> &)
    {
        ArenaVector<AnyElement> rows;
        for (int i = 0; i < 20; ++i)
            rows.push_back(div().h(50).flexShrink0().bg(i % 2 ? kRed : kBlue));
        return div().id(u"scroller").sizeFull().flexCol().overflowYScroll().hideScrollbar().children(rows);
    }
};

struct BigList {
    int rendered = 0;
    std::size_t count = 100000;
    AnyElement render(Window &, Context<BigList> &cx)
    {
        BigList *self = this;
        (void)cx;
        return div().sizeFull().flexCol().child(
                uniformList(u"rows", count, [self](std::size_t i, Window &, App &) -> AnyElement {
                    ++self->rendered;
                    return div().h(20).bg(i % 2 ? kRed : kGreen);
                }).itemHeight(20).flex1());
    }
};

struct Labels {
    std::u16string label = u"Zażółć gęślą jaźń";
    float width = 300;
    AnyElement render(Window &, Context<Labels> &)
    {
        return div().sizeFull().bg(white()).p(4).child(
                div().w(width).textSize(16).textColor(black()).child(std::u16string_view(label)));
    }
};

struct Stateful {
    bool show = true;
    AnyElement render(Window &, Context<Stateful> &)
    {
        if (!show)
            return div().sizeFull();
        return div().sizeFull().child(div()
                        .id(u"inner")
                        .w(50)
                        .h(50)
                        .bg(kRed)
                        .hover([](Style &s) { s.background = Background::solid(kGreen); }));
    }
};

} // namespace

class TestAtramentUi : public QObject {
    Q_OBJECT

private Q_SLOTS:
    void keystrokeParse_data();
    void keystrokeParse();
    void entitiesLiveAndDie();
    void observeAndSubscribe();
    void timersOnTheVirtualClock();
    void paintsTheTree();
    void flexRowLayout();
    void clickAndHover();
    void releaseOutsideIsNoClick();
    void cursorFollowsHover();
    void keysFocusAndBindings();
    void keySequences();
    void inputHandlerTyping();
    void wheelScrolls();
    void uniformListIsVirtual();
    void textWrapsInNarrowColumn();
    void scaleFactorDoublesPixels();
    void elementStateFollowsElement();
    void steadyFrameAllocatesNothingNew();
    void imageFit();
};

void TestAtramentUi::keystrokeParse_data()
{
    QTest::addColumn<QString>("text");
    QTest::addColumn<bool>("valid");
    QTest::addColumn<QString>("canonical");
    QTest::addColumn<QString>("keyChar");
    QTest::newRow("letter") << "a" << true << "a" << "a";
    QTest::newRow("shift letter") << "shift-a" << true << "shift-a" << "A";
    QTest::newRow("ctrl") << "ctrl-s" << true << "ctrl-s" << "";
    QTest::newRow("order") << "shift-ctrl-z" << true << "ctrl-shift-z" << "";
    QTest::newRow("minus") << "-" << true << "-" << "-";
    QTest::newRow("ctrl minus") << "ctrl--" << true << "ctrl--" << "";
    QTest::newRow("named") << "enter" << true << "enter" << "";
    QTest::newRow("space") << "space" << true << "space" << " ";
    QTest::newRow("polish") << "ż" << true << "ż" << "ż";
    QTest::newRow("dangling") << "ctrl-" << false << "" << "";
    QTest::newRow("unknown modifier") << "hyper-a" << false << "" << "";
    QTest::newRow("empty") << "" << false << "" << "";
}

void TestAtramentUi::keystrokeParse()
{
    QFETCH(QString, text);
    QFETCH(bool, valid);
    QFETCH(QString, canonical);
    QFETCH(QString, keyChar);
    const auto k = Keystroke::parse(std::u16string_view(reinterpret_cast<const char16_t *>(text.utf16()), text.size()));
    QCOMPARE(k.has_value(), valid);
    if (!k)
        return;
    QCOMPARE(QString::fromStdU16String(k->toString()), canonical);
    QCOMPARE(QString::fromStdU16String(k->keyChar), keyChar);
    QCOMPARE(Keystroke::parseSequence(u"ctrl-k ctrl-s").size(), std::size_t(2));
    QVERIFY(Keystroke::parseSequence(u"ctrl-k hyper-s").empty());
}

void TestAtramentUi::entitiesLiveAndDie()
{
    auto platform = std::make_shared<TestPlatform>();
    App app(platform, loadFonts());
    bool released = false;
    WeakEntity<Model> weak;
    Subscription onRelease;
    {
        Entity<Model> m = app.newEntity<Model>();
        m.update(app, [](Model &model, Context<Model> &) { model.value = 7; });
        QCOMPARE(m.read(app).value, 7);
        weak = m.downgrade();
        QVERIFY(weak.upgrade().has_value());
        onRelease = app.onRelease(m.id(), [&](App &) { released = true; });
        Entity<Model> copy = m;
        QCOMPARE(app.entityCount(), std::size_t(1));
    }
    // The last handle is gone: released when that update ended.
    QVERIFY(released);
    QVERIFY(!weak.upgrade());
    QCOMPARE(app.entityCount(), std::size_t(0));
    QVERIFY(!weak.update(app, [](Model &, Context<Model> &) { }));
    // A new entity reuses the slot under another generation: the old weak handle stays dead.
    Entity<Model> other = app.newEntity<Model>();
    QVERIFY(!weak.upgrade());
    QVERIFY(other.downgrade().upgrade());
}

void TestAtramentUi::observeAndSubscribe()
{
    auto platform = std::make_shared<TestPlatform>();
    App app(platform, loadFonts());
    Entity<Model> model = app.newEntity<Model>();
    Entity<Watcher> watcher = app.newEntity<Watcher>(model);
    model.update(app, [](Model &m, Context<Model> &cx) {
        m.value = 1;
        cx.notify();
        cx.notify(); // coalesced: one notification per update
        cx.emit(ModelEvent { 5 });
    });
    QCOMPARE(watcher.read(app).seen, 1);
    QCOMPARE(watcher.read(app).events, 5);
    // Notifications are effects: they run when the outermost update ends.
    app.update([&] {
        model.update(app, [](Model &, Context<Model> &cx) { cx.notify(); });
        QCOMPARE(watcher.read(app).seen, 1);
    });
    QCOMPARE(watcher.read(app).seen, 2);
    // A watcher gone: its subscriptions go with it.
    watcher = {};
    model.update(app, [](Model &, Context<Model> &cx) { cx.notify(); });
    QCOMPARE(app.entityCount(), std::size_t(1));
}

void TestAtramentUi::timersOnTheVirtualClock()
{
    auto platform = std::make_shared<TestPlatform>();
    App app(platform, loadFonts());
    int fired = 0;
    Subscription a = app.setTimeout(std::chrono::milliseconds(100), [&](App &) { ++fired; });
    Subscription b = app.setTimeout(std::chrono::milliseconds(50), [&](App &) { fired += 10; });
    platform->advanceClock(std::chrono::milliseconds(60));
    QCOMPARE(fired, 10);
    b = {};
    { Subscription c = app.setTimeout(std::chrono::milliseconds(10), [&](App &) { fired += 100; }); } // dropped
    platform->advanceClock(std::chrono::milliseconds(60));
    QCOMPARE(fired, 11);
    QCOMPARE(platform->pendingTimers(), std::size_t(0));
}

void TestAtramentUi::paintsTheTree()
{
    Harness h;
    h.open(h.app.newEntity<Counter>());
    QVERIFY(h.tw.framesPresented() >= 1);
    QVERIFY2(near(h.tw.pixel(30, 25), kRed), describe(h.tw.pixel(30, 25)).constData());
    QVERIFY(near(h.tw.pixel(150, 80), white()));
    QVERIFY(near(h.tw.pixel(9, 25), white())); // left of the button
}

void TestAtramentUi::flexRowLayout()
{
    Harness h({ 200, 50 });
    h.open(h.app.newEntity<Row>());
    // p(10): red 10–40, gap 10, green 50–150, gap, blue 160–190.
    QVERIFY(near(h.tw.pixel(5, 25), white()));
    QVERIFY(near(h.tw.pixel(12, 25), kRed));
    QVERIFY(near(h.tw.pixel(39, 25), kRed));
    QVERIFY(near(h.tw.pixel(45, 25), white()));
    QVERIFY(near(h.tw.pixel(52, 25), kGreen));
    QVERIFY(near(h.tw.pixel(148, 25), kGreen));
    QVERIFY(near(h.tw.pixel(155, 25), white()));
    QVERIFY2(near(h.tw.pixel(165, 25), kBlue), describe(h.tw.pixel(165, 25)).constData());
    QVERIFY(near(h.tw.pixel(195, 25), white()));
    QVERIFY(near(h.tw.pixel(100, 5), white())); // top padding
}

void TestAtramentUi::clickAndHover()
{
    Harness h;
    Entity<Counter> counter = h.app.newEntity<Counter>();
    h.open(counter);
    QVERIFY(near(h.tw.pixel(30, 25), kRed));
    h.tw.simulateMouseMove({ 30, 25 });
    QVERIFY2(near(h.tw.pixel(30, 25), kGreen), describe(h.tw.pixel(30, 25)).constData());
    h.tw.simulateClick({ 30, 25 });
    QCOMPARE(counter.read(h.app).count, 1);
    h.tw.simulateClick({ 30, 25 });
    QCOMPARE(counter.read(h.app).count, 2);
    h.tw.simulateClick({ 150, 80 }); // outside
    QCOMPARE(counter.read(h.app).count, 2);
    // Hover goes when the pointer leaves.
    QVERIFY(near(h.tw.pixel(30, 25), white()) == false);
    QVERIFY(near(h.tw.pixel(30, 25), kRed));
    h.tw.simulateMouseMove({ 30, 25 });
    h.tw.simulateMouseExit();
    QVERIFY(near(h.tw.pixel(30, 25), kRed));
}

void TestAtramentUi::releaseOutsideIsNoClick()
{
    Harness h;
    Entity<Counter> counter = h.app.newEntity<Counter>();
    h.open(counter);
    h.tw.simulateMouseMove({ 30, 25 });
    h.tw.simulateMouseDown({ 30, 25 });
    h.tw.simulateMouseMove({ 150, 80 }, MouseButton::Left);
    h.tw.simulateMouseUp({ 150, 80 });
    QCOMPARE(counter.read(h.app).count, 0);
    // Pressed outside, released over it: no click either.
    h.tw.simulateMouseDown({ 150, 80 });
    h.tw.simulateMouseMove({ 30, 25 }, MouseButton::Left);
    h.tw.simulateMouseUp({ 30, 25 });
    QCOMPARE(counter.read(h.app).count, 0);
}

void TestAtramentUi::cursorFollowsHover()
{
    Harness h;
    h.open(h.app.newEntity<Counter>());
    h.tw.simulateMouseMove({ 30, 25 });
    QCOMPARE(h.tw.cursor(), CursorStyle::PointingHand);
    h.tw.simulateMouseMove({ 150, 80 });
    QCOMPARE(h.tw.cursor(), CursorStyle::Arrow);
}

void TestAtramentUi::keysFocusAndBindings()
{
    Harness h;
    h.app.keymap().bind(u"ctrl-b", u"editor::bold", u"Editor");
    h.app.keymap().bind(u"ctrl-s", u"workspace::save", u"Workspace");
    Entity<Keys> keys = h.app.newEntity<Keys>();
    Window &w = h.open(keys);
    // Nothing focused: keys reach no element and no context's bindings (as in GPUI).
    h.tw.simulateKeystrokes(u"x ctrl-s");
    QVERIFY(keys.read(h.app).log.empty());
    // A click focuses the editor.
    h.tw.simulateClick({ 20, 20 });
    QVERIFY(keys.read(h.app).focus.isFocused(w));
    h.tw.simulateKeystrokes(u"x ctrl-b ctrl-s");
    // The element's own listener sees every key first (bubbling from it), then the bindings of
    // the deepest context that has one apply.
    const std::vector<std::u16string> expected { u"key:x", u"key:ctrl-b", u"bold", u"key:ctrl-s", u"save" };
    QCOMPARE(keys.read(h.app).log, expected);
    QVERIFY(w.dispatchAction(u"editor::bold"));
    QVERIFY(!w.dispatchAction(u"nothing::here"));
    w.blur();
    h.tw.settle();
    QVERIFY(!keys.read(h.app).focus.isFocused(w));
}

void TestAtramentUi::keySequences()
{
    Harness h;
    h.app.keymap().bind(u"ctrl-k ctrl-s", u"workspace::save");
    Entity<Keys> keys = h.app.newEntity<Keys>();
    h.open(keys);
    h.tw.simulateClick({ 20, 20 });
    auto actions = [&] {
        std::vector<std::u16string> out;
        for (const auto &e : keys.read(h.app).log) {
            if (e.rfind(u"key:", 0) != 0)
                out.push_back(e);
        }
        return out;
    };
    h.tw.simulateKeystrokes(u"ctrl-k");
    QVERIFY(actions().empty());
    h.tw.simulateKeystrokes(u"ctrl-s");
    QCOMPARE(actions(), (std::vector<std::u16string> { u"save" }));
    // A broken-off sequence: ctrl-k then x — x is handled on its own.
    h.tw.simulateKeystrokes(u"ctrl-k x ctrl-k ctrl-s");
    QCOMPARE(actions(), (std::vector<std::u16string> { u"save", u"save" }));
}

void TestAtramentUi::inputHandlerTyping()
{
    Harness h;
    Entity<Field> field = h.app.newEntity<Field>();
    Window &w = h.open(field);
    QVERIFY(!h.tw.inputHandler());
    h.tw.simulateClick({ 10, 10 });
    QVERIFY(field.read(h.app).focus.isFocused(w));
    QVERIFY(h.tw.inputHandler());
    h.tw.simulateTyping(u"żółw");
    QCOMPARE(field.read(h.app).text, std::u16string(u"żółw"));
    // Composition: marked text replaced as the input method changes its mind, then committed.
    h.tw.simulateComposition(u"ą");
    QCOMPARE(field.read(h.app).text, std::u16string(u"żółwą"));
    QVERIFY(field.read(h.app).marked.has_value());
    h.tw.simulateComposition(u"ąb");
    QCOMPARE(field.read(h.app).text, std::u16string(u"żółwąb"));
    h.tw.simulateCommit(u"ę");
    QCOMPARE(field.read(h.app).text, std::u16string(u"żółwę"));
    QVERIFY(!field.read(h.app).marked);
    // Ctrl+letter is not text.
    h.tw.simulateKeystrokes(u"ctrl-a");
    QCOMPARE(field.read(h.app).text, std::u16string(u"żółwę"));
    QVERIFY(h.tw.inputStateChanges() > 0);
}

void TestAtramentUi::wheelScrolls()
{
    Harness h({ 100, 100 });
    h.open(h.app.newEntity<Scroller>());
    // Rows of 50: blue, red, blue, …
    QVERIFY(near(h.tw.pixel(50, 10), kBlue));
    QVERIFY(near(h.tw.pixel(50, 60), kRed));
    h.tw.simulateScroll({ 50, 50 }, { 0, 50 });
    QVERIFY2(near(h.tw.pixel(50, 10), kRed), describe(h.tw.pixel(50, 10)).constData());
    // Clamped at the end: 20 rows of 50 = 1000, viewport 100 → at most 900.
    h.tw.simulateScroll({ 50, 50 }, { 0, 100000 });
    QVERIFY(near(h.tw.pixel(50, 10), kBlue)); // row 18
    QVERIFY(near(h.tw.pixel(50, 60), kRed)); // row 19
    h.tw.simulateScroll({ 50, 50 }, { 0, -100000 });
    QVERIFY(near(h.tw.pixel(50, 10), kBlue));
}

void TestAtramentUi::uniformListIsVirtual()
{
    Harness h({ 100, 200 });
    Entity<BigList> list = h.app.newEntity<BigList>();
    h.open(list);
    // 200 px of 20 px rows: about 11 rows rendered, not 100 000.
    const int first = list.read(h.app).rendered;
    QVERIFY2(first > 0 && first <= 12, qPrintable(QString::number(first)));
    QVERIFY(near(h.tw.pixel(50, 5), kGreen)); // row 0
    QVERIFY(near(h.tw.pixel(50, 25), kRed)); // row 1
    h.tw.simulateScroll({ 50, 50 }, { 0, 20 * 50000 });
    QVERIFY(near(h.tw.pixel(50, 5), kGreen)); // row 50 000
    QVERIFY(list.read(h.app).rendered - first <= 24);
    h.tw.simulateScroll({ 50, 50 }, { 0, 20 * 1e6f });
    QVERIFY(near(h.tw.pixel(50, 195), kRed)); // the last row, 99 999
}

void TestAtramentUi::textWrapsInNarrowColumn()
{
    Harness h({ 400, 200 });
    Entity<Labels> labels = h.app.newEntity<Labels>();
    Window &w = h.open(labels);
    auto inkRows = [&] {
        int rows = 0;
        for (int y = 0; y < 200; ++y) {
            bool ink = false;
            for (int x = 0; x < 400 && !ink; ++x)
                ink = h.tw.pixel(x, y).r < 128;
            rows += ink ? 1 : 0;
        }
        return rows;
    };
    const int wide = inkRows();
    QVERIFY2(wide > 8 && wide < 30, qPrintable(QString::number(wide))); // one line of 16 px text
    labels.update(h.app, [](Labels &l, Context<Labels> &cx) {
        l.width = 60;
        cx.notify();
    });
    h.tw.settle();
    const int narrow = inkRows();
    QVERIFY2(narrow > wide * 2, qPrintable(QString::number(narrow))); // three lines
    QVERIFY(w.lastFrameStats().primitives > 10); // glyphs
}

void TestAtramentUi::scaleFactorDoublesPixels()
{
    Harness h({ 200, 50 }, 2);
    h.open(h.app.newEntity<Row>());
    QCOMPARE(h.tw.image().width, 400);
    QVERIFY(near(h.tw.pixel(25, 50), kRed));
    QVERIFY(near(h.tw.pixel(85, 50), white()));
    QVERIFY(near(h.tw.pixel(105, 50), kGreen));
}

void TestAtramentUi::elementStateFollowsElement()
{
    Harness h;
    Entity<Stateful> view = h.app.newEntity<Stateful>();
    h.open(view);
    h.tw.simulateMouseMove({ 20, 20 });
    QVERIFY(near(h.tw.pixel(20, 20), kGreen));
    // The element goes for a frame: its hover state goes with it, and it comes back unhovered.
    view.update(h.app, [](Stateful &s, Context<Stateful> &cx) {
        s.show = false;
        cx.notify();
    });
    h.tw.settle();
    h.tw.simulateMouseMove({ 150, 80 });
    view.update(h.app, [](Stateful &s, Context<Stateful> &cx) {
        s.show = true;
        cx.notify();
    });
    h.tw.settle();
    QVERIFY(near(h.tw.pixel(20, 20), kRed));
}

void TestAtramentUi::steadyFrameAllocatesNothingNew()
{
    Harness h({ 400, 200 });
    Entity<Labels> labels = h.app.newEntity<Labels>();
    Window &w = h.open(labels);
    for (int i = 0; i < 3; ++i) {
        w.refresh();
        h.tw.settle();
    }
    const std::size_t bytes = w.lastFrameStats().arenaBytes;
    QVERIFY(bytes > 0);
    for (int i = 0; i < 5; ++i) {
        w.refresh();
        h.tw.settle();
        QCOMPARE(w.lastFrameStats().arenaBytes, bytes);
    }
}

void TestAtramentUi::imageFit()
{
    const Rect box = Rect::fromXYWH(0, 0, 100, 50);
    QCOMPARE(Img::fitRect({ 200, 200 }, box, ObjectFit::Contain), Rect::fromXYWH(25, 0, 50, 50));
    QCOMPARE(Img::fitRect({ 200, 200 }, box, ObjectFit::Cover), Rect::fromXYWH(0, -25, 100, 100));
    QCOMPARE(Img::fitRect({ 20, 10 }, box, ObjectFit::ScaleDown), Rect::fromXYWH(40, 20, 20, 10));
    QCOMPARE(Img::fitRect({ 20, 10 }, box, ObjectFit::Fill), box);
}

QTEST_GUILESS_MAIN(TestAtramentUi)
#include "tst_atrament_ui.moc"
