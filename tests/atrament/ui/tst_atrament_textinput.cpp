// Atrament's TextInput on the test platform: typing, graphemes and words, selection with keys
// and the mouse, clipboard, undo, the input method's composition, single and multiple lines.
#include "text/fonts.h"
#include "ui/atrament.h"
#include "ui/testplatform.h"

#include <QtTest>

using namespace brulion::atrament;

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

QTEST_GUILESS_MAIN(TestAtramentTextInput)
#include "tst_atrament_textinput.moc"
