// Atrament's TextInput on the test platform: typing, graphemes and words, selection with keys
// and the mouse, clipboard, undo, the input method's composition, single and multiple lines; the
// caret's shapes (bar, block, underline), a read-only field, inline objects in its spans (one unit
// for the caret, their own clicks and long presses).
#include "text/fonts.h"
#include "ui/atrament.h"
#include "ui/testplatform.h"

#include <QtTest>

using namespace brulion::atrament;
using namespace std::chrono_literals;

namespace {

std::shared_ptr<FontCollection> loadFonts()
{
    static std::shared_ptr<FontCollection> fonts = [] {
        auto f = std::make_shared<FontCollection>();
        f->addFontFile(BRULION_SOURCE_DIR "/assets/fonts/ui/BrulionInter-Regular.ttf");
        f->addFontFile(BRULION_SOURCE_DIR "/assets/fonts/BrulionEmoji.ttf");
        f->setFallbackFamilies({ u"Brulion Emoji" });
        return f;
    }();
    return fonts;
}

struct Form {
    Entity<TextInput> field;
    int changes = 0;
    std::vector<std::u16string> submitted;
    std::vector<Subscription> subs;
    Form(Context<Form> &cx, bool multiline)
        : field(cx.newEntity<TextInput>(TextInputOptions{u"Szukaj", multiline, InputHandler::Purpose::Text, {}, {}}))
    {
        subs.push_back(cx.subscribe<TextInput::Changed>(
                field, [](Form &f, const TextInput::Changed &, Context<Form> &) { ++f.changes; }));
        subs.push_back(cx.subscribe<TextInput::Submitted>(
                field, [](Form &f, const TextInput::Submitted &s, Context<Form> &) { f.submitted.push_back(s.text); }));
    }
    AnyElement render(Window &, Context<Form> &)
    {
        return div().sizeFull().p(10).textSize(16).child(div().w(200).child(field));
    }
};

struct Harness {
    std::shared_ptr<TestPlatform> platform = std::make_shared<TestPlatform>();
    App app { platform, loadFonts() };
    TestWindow &tw = platform->newWindow({ 300, 200 });
    Entity<Form> form;
    explicit Harness(bool multiline = false)
        : form(app.newEntity<Form>(multiline))
    {
        app.openWindow(tw, form);
        tw.settle();
        tw.simulateClick({ 20, 20 }); // focus the field
    }
    const TextInput &input() { return form.read(app).field.read(app); }
    std::u16string text() { return input().text(); }
    InputHandler::Selection sel() { return input().selection(); }
};

} // namespace

class TestAtramentTextInput : public QObject {
    Q_OBJECT

private Q_SLOTS:
    void typesPolishText();
    void backspaceByGrapheme();
    void wordMovementAndDeletion();
    void shiftSelectsAndTypingReplaces();
    void clipboardRoundTrip();
    void undoCoalescesTyping();
    void composition();
    void enterSubmitsOrBreaksLines();
    void mouseSelectsWords();
    void caretBlinksOnlyWhenFocused();
    void imeBoundsFollowCaret();
    void caretStaysInView();
    void fingerDragsNoSelection();
    void stylerSpansAndSelectionEvents();
    void textLayoutHandle();
    void caretShapes();
    void readOnlyField();
    void inlineObjectsAreOneUnit();
    void inlineObjectsTakeTheirPresses();
};

void TestAtramentTextInput::typesPolishText()
{
    Harness h;
    QVERIFY(h.tw.inputHandler());
    h.tw.simulateTyping(u"Zażółć gęślą jaźń");
    QCOMPARE(h.text(), std::u16string(u"Zażółć gęślą jaźń"));
    QCOMPARE(h.sel().focus, int(h.text().size()));
    QVERIFY(h.form.read(h.app).changes >= 1);
}

void TestAtramentTextInput::backspaceByGrapheme()
{
    Harness h;
    // A family emoji (ZWJ sequence, 8 code units) and an "a" with a combining ogonek go whole.
    h.tw.simulateCommit(u"x👨‍👩‍👧a\u0328");
    QCOMPARE(h.text().size(), std::size_t(1 + 8 + 2));
    h.tw.simulateKeystrokes(u"backspace");
    QCOMPARE(h.text(), std::u16string(u"x👨‍👩‍👧"));
    h.tw.simulateKeystrokes(u"backspace");
    QCOMPARE(h.text(), std::u16string(u"x"));
    h.tw.simulateKeystrokes(u"left delete");
    QCOMPARE(h.text(), std::u16string());
    h.tw.simulateKeystrokes(u"backspace delete"); // nothing to delete: no crash, no change
    QCOMPARE(h.text(), std::u16string());
}

void TestAtramentTextInput::wordMovementAndDeletion()
{
    Harness h;
    h.tw.simulateCommit(u"ala ma kota");
    h.tw.simulateKeystrokes(u"ctrl-left");
    QCOMPARE(h.sel().focus, 7);
    h.tw.simulateKeystrokes(u"ctrl-left ctrl-left");
    QCOMPARE(h.sel().focus, 0);
    h.tw.simulateKeystrokes(u"ctrl-right");
    QCOMPARE(h.sel().focus, 3);
    h.tw.simulateKeystrokes(u"end ctrl-backspace");
    QCOMPARE(h.text(), std::u16string(u"ala ma "));
    h.tw.simulateKeystrokes(u"home ctrl-delete");
    QCOMPARE(h.text(), std::u16string(u" ma "));
}

void TestAtramentTextInput::shiftSelectsAndTypingReplaces()
{
    Harness h;
    h.tw.simulateCommit(u"żółty kot");
    h.tw.simulateKeystrokes(u"home shift-right shift-right shift-right");
    QCOMPARE(h.sel().range(), (Range { 0, 3 }));
    h.tw.simulateTyping(u"Ż");
    QCOMPARE(h.text(), std::u16string(u"Żty kot"));
    h.tw.simulateKeystrokes(u"ctrl-a");
    QCOMPARE(h.sel().range(), (Range { 0, 7 }));
    h.tw.simulateKeystrokes(u"right");
    QCOMPARE(h.sel().range(), (Range { 7, 7 })); // collapsed to the end
}

void TestAtramentTextInput::clipboardRoundTrip()
{
    Harness h;
    h.tw.simulateCommit(u"kopiuj mnie");
    h.tw.simulateKeystrokes(u"ctrl-a ctrl-c");
    QCOMPARE(h.platform->readClipboard().value_or(u""), std::u16string(u"kopiuj mnie"));
    h.tw.simulateKeystrokes(u"ctrl-x");
    QCOMPARE(h.text(), std::u16string());
    h.tw.simulateKeystrokes(u"ctrl-v ctrl-v");
    QCOMPARE(h.text(), std::u16string(u"kopiuj mniekopiuj mnie"));
    // A single-line field turns pasted line breaks into spaces.
    h.platform->writeClipboard(u"a\nb");
    h.tw.simulateKeystrokes(u"ctrl-a ctrl-v");
    QCOMPARE(h.text(), std::u16string(u"a b"));
}

void TestAtramentTextInput::undoCoalescesTyping()
{
    Harness h;
    h.tw.simulateTyping(u"abc");
    h.tw.simulateKeystrokes(u"left");
    h.tw.simulateTyping(u"X");
    QCOMPARE(h.text(), std::u16string(u"abXc"));
    h.tw.simulateKeystrokes(u"ctrl-z");
    QCOMPARE(h.text(), std::u16string(u"abc"));
    h.tw.simulateKeystrokes(u"ctrl-z");
    QCOMPARE(h.text(), std::u16string()); // "abc" was typed as one step
    h.tw.simulateKeystrokes(u"ctrl-shift-z");
    QCOMPARE(h.text(), std::u16string(u"abc"));
    h.tw.simulateKeystrokes(u"ctrl-y");
    QCOMPARE(h.text(), std::u16string(u"abXc"));
    h.tw.simulateKeystrokes(u"ctrl-y"); // nothing left to redo
    QCOMPARE(h.text(), std::u16string(u"abXc"));
}

void TestAtramentTextInput::composition()
{
    Harness h;
    h.tw.simulateTyping(u"za");
    h.tw.simulateComposition(u"ż");
    QCOMPARE(h.text(), std::u16string(u"zaż"));
    QCOMPARE(h.input().markedRange(), std::optional<Range>(Range { 2, 3 }));
    h.tw.simulateComposition(u"żó");
    QCOMPARE(h.text(), std::u16string(u"zażó"));
    h.tw.simulateCommit(u"żółw");
    QCOMPARE(h.text(), std::u16string(u"zażółw"));
    QVERIFY(!h.input().markedRange());
    // A cancelled composition leaves nothing behind.
    h.tw.simulateComposition(u"x");
    h.tw.simulateComposition(u"");
    QCOMPARE(h.text(), std::u16string(u"zażółw"));
    // The whole composition is one undo step.
    h.tw.simulateKeystrokes(u"ctrl-z");
    QCOMPARE(h.text(), std::u16string(u"za"));
}

void TestAtramentTextInput::enterSubmitsOrBreaksLines()
{
    {
        Harness h(false);
        h.tw.simulateTyping(u"szukaj");
        h.tw.simulateKeystrokes(u"enter");
        QCOMPARE(h.form.read(h.app).submitted, (std::vector<std::u16string> { u"szukaj" }));
        QCOMPARE(h.text(), std::u16string(u"szukaj"));
    }
    {
        Harness h(true);
        h.tw.simulateTyping(u"jeden");
        h.tw.simulateKeystrokes(u"enter");
        h.tw.simulateTyping(u"dwa");
        QCOMPARE(h.text(), std::u16string(u"jeden\ndwa"));
        h.tw.simulateKeystrokes(u"up");
        QVERIFY(h.sel().focus <= 5);
        h.tw.simulateKeystrokes(u"down");
        QVERIFY(h.sel().focus >= 6);
        h.tw.simulateKeystrokes(u"ctrl-enter");
        QCOMPARE(h.form.read(h.app).submitted.size(), std::size_t(1));
    }
}

void TestAtramentTextInput::mouseSelectsWords()
{
    Harness h;
    h.tw.simulateCommit(u"ala ma kota");
    // The field starts at x = 10; "ala" is at its start.
    h.tw.simulateMouseMove({ 14, 20 });
    h.tw.simulateMouseDown({ 14, 20 }, MouseButton::Left, {}, 2);
    h.tw.simulateMouseUp({ 14, 20 }, MouseButton::Left, {}, 2);
    QCOMPARE(h.sel().range(), (Range { 0, 3 }));
    h.tw.simulateMouseDown({ 14, 20 }, MouseButton::Left, {}, 3);
    h.tw.simulateMouseUp({ 14, 20 }, MouseButton::Left, {}, 3);
    QCOMPARE(h.sel().range(), (Range { 0, 11 }));
    // A drag from the start to past the end selects all.
    h.tw.simulateMouseDown({ 10, 20 });
    h.tw.simulateMouseMove({ 290, 20 }, MouseButton::Left);
    h.tw.simulateMouseUp({ 290, 20 });
    QCOMPARE(h.sel().range(), (Range { 0, 11 }));
}

void TestAtramentTextInput::caretBlinksOnlyWhenFocused()
{
    Harness h;
    QVERIFY(h.input().caretVisible());
    h.platform->advanceClock(std::chrono::milliseconds(600));
    QVERIFY(!h.input().caretVisible());
    h.platform->advanceClock(std::chrono::milliseconds(530));
    QVERIFY(h.input().caretVisible());
    h.tw.simulateTyping(u"a"); // typing shows it at once
    QVERIFY(h.input().caretVisible());
    // Focus away: no more timers.
    h.app.windows().front()->blur();
    h.tw.settle();
    h.platform->advanceClock(std::chrono::milliseconds(2000));
    QCOMPARE(h.platform->pendingTimers(), std::size_t(0));
}

void TestAtramentTextInput::imeBoundsFollowCaret()
{
    Harness h;
    InputHandler *handler = h.tw.inputHandler();
    QVERIFY(handler);
    const auto start = handler->boundsForRange({ 0, 0 });
    h.tw.simulateTyping(u"wwww");
    handler = h.tw.inputHandler();
    const auto end = handler->boundsForRange({ 4, 4 });
    QVERIFY(start && end);
    QVERIFY(end->left() > start->left() + 20);
    QCOMPARE(handler->textForRange({ 1, 3 }), std::u16string(u"ww"));
    QCOMPARE(handler->textLength(), 4);
}

namespace {

// A field below a tall block in a scrolling column: an on-screen keyboard (a shorter window) must
// not hide it while it has the focus.
struct Tall {
    Entity<TextInput> field;
    ScrollHandle scroll;
    explicit Tall(Context<Tall> &cx)
        : field(cx.newEntity<TextInput>(TextInputOptions{u"Notatka", true, InputHandler::Purpose::Multiline, {}, {}}))
    { }
    AnyElement render(Window &, Context<Tall> &)
    {
        return div()
                .sizeFull()
                .flexCol()
                .overflowYScroll()
                .trackScroll(scroll)
                .textSize(16)
                .child(div().h(150).flexShrink0().child(u"Zażółć gęślą jaźń"))
                .child(div().w(200).flexShrink0().child(field))
                .child(div().h(300).flexShrink0());
    }
};

} // namespace

void TestAtramentTextInput::caretStaysInView()
{
    auto platform = std::make_shared<TestPlatform>();
    App app{platform, loadFonts()};
    TestWindow &tw = platform->newWindow({300, 200});
    Entity<Tall> tall = app.newEntity<Tall>();
    Window &window = app.openWindow(tw, tall);
    tw.settle();
    // Tapped where it shows: the focus, nothing to scroll.
    QVERIFY(tw.simulateClick({20, 160}));
    tw.settle();
    QVERIFY(tw.inputHandler());
    QCOMPARE(tall.read(app).scroll.offset().y, 0.0f);
    // The keyboard comes up: the window is 120 px shorter, the caret goes up into what is left.
    tw.resize({300, 80});
    tw.settle();
    const float offset = tall.read(app).scroll.offset().y;
    QVERIFY(offset > 60);
    const std::optional<Rect> caret = tw.inputHandler()->boundsForRange({0, 0});
    QVERIFY(caret);
    QVERIFY(caret->top() >= 0 && caret->bottom() <= 80);
    // Scrolled away by hand, it stays where it was until the caret moves or the text changes.
    tall.read(app).scroll.shared()->offset = {};
    window.refresh();
    tw.settle();
    QCOMPARE(tall.read(app).scroll.offset().y, 0.0f);
    tw.simulateTyping(u"ą");
    tw.settle();
    QVERIFY(tall.read(app).scroll.offset().y > 60);
}

void TestAtramentTextInput::fingerDragsNoSelection()
{
    Harness h;
    h.tw.simulateTyping(u"Zażółć gęślą");
    // A finger presses and holds: the caret goes there; the input method's long press then selects
    // the word, and the finger moving a little while held must not undo it.
    MouseDownEvent down;
    down.position = {20, 20};
    down.pointer = PointerKind::Touch;
    h.tw.simulateInput(down);
    const int caret = h.sel().focus;
    QVERIFY(caret < 6);
    h.tw.inputHandler()->setSelection({0, 6});
    MouseMoveEvent move;
    move.position = {24, 21};
    move.pressedButton = MouseButton::Left;
    move.pointer = PointerKind::Touch;
    h.tw.simulateInput(move);
    QCOMPARE(h.sel(), (InputHandler::Selection{0, 6}));
    MouseUpEvent up;
    up.position = {24, 21};
    up.pointer = PointerKind::Touch;
    h.tw.simulateInput(up);
    QCOMPARE(h.sel(), (InputHandler::Selection{0, 6}));
    // A mouse still drags a selection.
    h.tw.simulateMouseDown({20, 20});
    h.tw.simulateMouseMove({120, 20}, MouseButton::Left);
    h.tw.simulateMouseUp({120, 20});
    QVERIFY(h.sel().anchor != h.sel().focus);
}


void TestAtramentTextInput::stylerSpansAndSelectionEvents()
{
    Harness h(true);
    int asked = 0;
    int moved = 0;
    InputHandler::Selection last;
    Subscription sub = h.app.subscribe<TextInput::SelectionChanged>(
            h.form.read(h.app).field.id(), [&](const TextInput::SelectionChanged &e, App &) {
                ++moved;
                last = e.selection;
            });
    // Every "ż" red, asked once per text.
    h.form.read(h.app).field.update(h.app, [&](TextInput &t, Context<TextInput> &cx) {
        t.setStyler(
                [&asked](std::u16string_view text) {
                    ++asked;
                    std::vector<TextSpan> spans;
                    for (std::size_t i = 0; i < text.size(); ++i) {
                        if (text[i] == u'ż') {
                            TextStyleRefinement red;
                            red.color = rgb(0xff0000);
                            spans.push_back({{int(i), int(i) + 1}, red});
                        }
                    }
                    return spans;
                },
                cx);
        t.setColors(rgb(0x00ff00), rgba(0x0000ff40), cx);
    });
    h.tw.simulateTyping(u"żżż");
    QVERIFY(asked >= 1);
    const int afterTyping = asked;
    h.tw.settle();
    h.tw.draw();
    QCOMPARE(asked, afterTyping); // the same text: no new spans
    QCOMPARE(h.input().spans().size(), std::size_t(3));
    bool red = false;
    for (int y = 10; y < 40 && !red; ++y)
        for (int x = 10; x < 50 && !red; ++x)
            red = h.tw.pixel(x, y).r > 180 && h.tw.pixel(x, y).g < 100;
    QVERIFY(red);
    // The caret moved by a key: one event with the new selection; the text unchanged.
    moved = 0;
    h.tw.simulateKeystrokes(u"left");
    QCOMPARE(moved, 1);
    QCOMPARE(last.focus, 2);
    h.tw.simulateKeystrokes(u"shift-left");
    QCOMPARE(last.anchor, 2);
    QCOMPARE(last.focus, 1);
    // A layout is there for an owner to ask (↑/↓ across fields of an editor).
    QVERIFY(h.input().layout());
    QCOMPARE(h.input().layout()->text(), std::u16string(u"żżż"));
    // The owner keeps the undo history: the field forgets its own.
    h.form.read(h.app).field.update(h.app, [](TextInput &t, Context<TextInput> &cx) {
        t.clearUndo();
        t.undo(cx);
    });
    QCOMPARE(h.text(), std::u16string(u"żżż"));
}

namespace {

struct Tracked {
    TextLayoutHandle handle;
    AnyElement render(Window &, Context<Tracked> &)
    {
        return div().sizeFull().p(20).textSize(16).child(div().w(120).child(text(u"Zażółć gęślą jaźń").trackLayout(handle)));
    }
};

} // namespace

void TestAtramentTextInput::textLayoutHandle()
{
    std::shared_ptr<TestPlatform> platform = std::make_shared<TestPlatform>();
    App app{platform, loadFonts()};
    TestWindow &tw = platform->newWindow({300, 200});
    Entity<Tracked> view = app.newEntity<Tracked>();
    Window &w = app.openWindow(tw, view);
    QVERIFY(!view.read(app).handle.layout());
    tw.settle();
    const TextLayoutHandle &handle = view.read(app).handle;
    QVERIFY(handle.layout());
    QCOMPARE(handle.frame(), w.frameCount());
    QCOMPARE(handle.bounds().left(), 20.0f);
    QVERIFY(handle.layout()->lines().size() >= 2); // wrapped at 120 px
    // A point before "gęślą" is offset 7; points left of the text clamp to its start.
    const Rect caret = handle.layout()->caretRect(7).translated(handle.bounds().origin);
    QCOMPARE(handle.offsetAt({caret.left() + 0.5f, caret.top() + caret.height() / 2}).value_or(-1), 7);
    QCOMPARE(handle.offsetAt({0, handle.bounds().top() + 2}).value_or(-1), 0);
}

namespace {

// An object over [from, to) of the field's text: 30 px wide, its clicks and long presses counted.
struct ObjectLog {
    int painted = 0;
    int clicks = 0;
    int rightClicks = 0;
    int longPresses = 0;
    Rect box;
};

TextInput::Styler objectStyler(int from, int to, ObjectLog *log)
{
    return [from, to, log](std::u16string_view text) {
        std::vector<TextSpan> spans;
        if (int(text.size()) < to)
            return spans;
        InlineObject object;
        object.size = InlineObjectSize{30, 12, 4};
        object.paint = [log](const InlineObjectPaint &p, Window &w) {
            ++log->painted;
            log->box = p.bounds;
            w.paintQuad(p.bounds, Background::solid(rgb(0x00c000)), {}, transparent(), {});
        };
        object.onClick = [log](const ClickEvent &click, Rect, Window &) {
            if (click.up.button == MouseButton::Right)
                ++log->rightClicks;
            else
                ++log->clicks;
        };
        object.onLongPress = [log](Point, Rect, Window &) { ++log->longPresses; };
        spans.push_back(TextSpan{Range{from, to}, {}, std::move(object)});
        return spans;
    };
}

} // namespace

void TestAtramentTextInput::caretShapes()
{
    Harness h;
    h.tw.simulateTyping(u"Zażółć");
    auto field = [&] { return h.form.read(h.app).field; };
    field().update(h.app, [](TextInput &t, Context<TextInput> &cx) { t.setSelection(0, 0, cx); });
    h.tw.settle();
    // The bar: thin, before the character.
    const Rect bar = h.input().caretBox().value_or(Rect{});
    QVERIFY(bar.width() > 0 && bar.width() < 2);
    const TextLayout *layout = h.input().layout();
    QVERIFY(layout);
    const float zWidth = layout->caretX(1) - layout->caretX(0);
    // A block: over "Z", as wide as it, as tall as the line; steady.
    field().update(h.app, [](TextInput &t, Context<TextInput> &cx) { t.setCaret(CaretShape::Block, false, cx); });
    h.tw.settle();
    const Rect block = h.input().caretBox().value_or(Rect{});
    QCOMPARE(block.width(), zWidth);
    QCOMPARE(block.left(), bar.left() + 0.5f);
    QVERIFY(block.height() >= 14);
    h.tw.advance(1200ms);
    QVERIFY(h.input().caretVisible());
    // Drawn over the glyph, in the caret's colour (translucent): the box is tinted.
    const Pixel inside = h.tw.pixel(int(block.left() + block.width() / 2), int(block.top() + 2));
    QVERIFY2(inside.b > inside.r + 20, "a block of the caret's colour over the character");
    // At the text's end: half an em wide.
    h.tw.simulateKeystrokes(u"end");
    const Rect atEnd = h.input().caretBox().value_or(Rect{});
    QCOMPARE(atEnd.width(), 8.0f); // 16 px text
    // An underline: under the character, 2 px.
    field().update(h.app, [](TextInput &t, Context<TextInput> &cx) {
        t.setCaret(CaretShape::Underline, false, cx);
        t.setSelection(1, 1, cx);
    });
    h.tw.settle();
    const Rect line = h.input().caretBox().value_or(Rect{});
    QCOMPARE(line.height(), 2.0f);
    QCOMPARE(line.width(), layout->caretX(2) - layout->caretX(1));
    QVERIFY(line.top() > block.top() + block.height() / 2);
    // A selection shows the place instead: no block, no underline.
    h.tw.simulateKeystrokes(u"shift-right");
    QVERIFY(!h.input().caretBox());
    // Back to the bar: it blinks again.
    field().update(h.app, [](TextInput &t, Context<TextInput> &cx) { t.setCaret(CaretShape::Bar, true, cx); });
    h.tw.settle();
    QVERIFY(h.input().caretBox());
    h.tw.advance(600ms);
    QVERIFY(!h.input().caretVisible());
}

void TestAtramentTextInput::readOnlyField()
{
    Harness h;
    h.tw.simulateTyping(u"gęślą");
    auto field = [&] { return h.form.read(h.app).field; };
    field().update(h.app, [](TextInput &t, Context<TextInput> &cx) { t.setReadOnly(true, cx); });
    h.tw.settle();
    // No input method: the window has no input handler; typed keys change nothing.
    QVERIFY(!h.tw.inputHandler());
    h.tw.simulateTyping(u"ż");
    h.tw.simulateKeystrokes(u"backspace delete ctrl-x ctrl-z");
    QCOMPARE(h.text(), std::u16string(u"gęślą"));
    // The caret still moves and selects.
    h.tw.simulateKeystrokes(u"home shift-right shift-right");
    QCOMPARE(h.sel().anchor, 0);
    QCOMPARE(h.sel().focus, 2);
    h.platform->writeClipboard(u"x");
    h.tw.simulateKeystrokes(u"ctrl-v");
    QCOMPARE(h.text(), std::u16string(u"gęślą"));
    // The owner's own edits still go in.
    field().update(h.app, [](TextInput &t, Context<TextInput> &cx) { t.replace(Range{0, 1}, u"G", cx); });
    QCOMPARE(h.text(), std::u16string(u"Gęślą"));
    // Editable again: typing, and the input method.
    field().update(h.app, [](TextInput &t, Context<TextInput> &cx) { t.setReadOnly(false, cx); });
    h.tw.settle();
    QVERIFY(h.tw.inputHandler());
    h.tw.simulateKeystrokes(u"end");
    h.tw.simulateTyping(u"ż");
    QCOMPARE(h.text(), std::u16string(u"Gęśląż"));
}

void TestAtramentTextInput::inlineObjectsAreOneUnit()
{
    Harness h;
    ObjectLog log;
    // "ab" then an object over "⏰ jutro" (7 code units), then "cd".
    h.tw.simulateTyping(u"ab⏰ jutrocd");
    h.form.read(h.app).field.update(h.app, [&](TextInput &t, Context<TextInput> &cx) {
        t.setStyler(objectStyler(2, 9, &log), cx);
        t.setSelection(2, 2, cx);
    });
    h.tw.settle();
    QVERIFY(log.painted > 0);
    QCOMPARE(log.box.width(), 30.0f);
    // → over the object at once, ← back.
    h.tw.simulateKeystrokes(u"right");
    QCOMPARE(h.sel().focus, 9);
    h.tw.simulateKeystrokes(u"left");
    QCOMPARE(h.sel().focus, 2);
    // Shift+→ selects it whole.
    h.tw.simulateKeystrokes(u"shift-right");
    QCOMPARE(h.sel().anchor, 2);
    QCOMPARE(h.sel().focus, 9);
    // Backspace after it removes it whole; Delete before it too.
    h.tw.simulateKeystrokes(u"right backspace");
    QCOMPARE(h.text(), std::u16string(u"abcd"));
    h.form.read(h.app).field.update(h.app, [](TextInput &t, Context<TextInput> &cx) {
        t.setText(u"ab⏰ jutrocd", cx);
        t.setSelection(2, 2, cx);
    });
    h.tw.settle();
    h.tw.simulateKeystrokes(u"delete");
    QCOMPARE(h.text(), std::u16string(u"abcd"));
}

void TestAtramentTextInput::inlineObjectsTakeTheirPresses()
{
    Harness h;
    ObjectLog log;
    h.tw.simulateTyping(u"ab⏰ jutrocd");
    h.form.read(h.app).field.update(h.app, [&](TextInput &t, Context<TextInput> &cx) {
        t.setStyler(objectStyler(2, 9, &log), cx);
        t.setSelection(11, 11, cx);
    });
    h.tw.settle();
    const Point middle{log.box.left() + log.box.width() / 2, log.box.top() + log.box.height() / 2};
    // A click on the object is its own: the caret stays at the end.
    h.tw.simulateClick(middle);
    QCOMPARE(log.clicks, 1);
    QCOMPARE(h.sel().focus, 11);
    h.tw.simulateClick(middle, MouseButton::Right);
    QCOMPARE(log.rightClicks, 1);
    QCOMPARE(h.sel().focus, 11);
    // A press on it released elsewhere is no click.
    h.tw.simulateMouseDown(middle);
    h.tw.simulateMouseUp({log.box.right() + 40, middle.y});
    QCOMPARE(log.clicks, 1);
    // Next to it the field's own: the caret goes there.
    h.tw.simulateClick({log.box.right() + 1, middle.y});
    QCOMPARE(h.sel().focus, 9);
    // A finger held on it in the focused field: its long press, not the platform's selection.
    h.tw.simulateTouchDown(middle);
    h.tw.advance(600ms);
    QCOMPARE(log.longPresses, 1);
    h.tw.simulateTouchUp(middle);
    QCOMPARE(log.clicks, 1);
    QCOMPARE(h.sel().focus, 9);
    // A tap on it: a click.
    h.tw.simulateTap(middle);
    h.tw.advance(200ms);
    QCOMPARE(log.clicks, 2);
}

QTEST_GUILESS_MAIN(TestAtramentTextInput)
#include "tst_atrament_textinput.moc"
