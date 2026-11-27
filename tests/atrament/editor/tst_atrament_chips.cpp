// Reminder chips while a block is edited in the block editor drawn by Atrament
// (src/app/editor/atrament/pagechips.cpp; docs/ATRAMENT.md "The block editor in Atrament (M2)",
// step 2; InlineEditor.qml's chips): a page of a PageController over FakeEditorHost on Atrament's
// test platform — every ⏰ / ⏰! token of the edited text laid out as the pill of its words in the
// palette's chip colours while the text keeps the token as written, one unit for the caret (→ / ←
// over it, Backspace removing it, a caret put inside going to its end), a click opening its dialog
// and a right-click or a finger held on it its menu without moving the caret, its words in the
// UI's language relative to today.
#include "blocklistmodel.h"
#include "editorhost.h"
#include "editstyles.h"
#include "fakeeditorhost.h"
#include "pagecontroller.h"
#include "pageview.h"
#include "text/fonts.h"
#include "ui/testplatform.h"

#include <QTest>

#include <memory>

using namespace brulion;
using namespace brulion::editor;
using namespace brulion::editor::atr;
using namespace Qt::StringLiterals;
using namespace std::chrono_literals;
namespace at = brulion::atrament;

namespace {

const QString PagePath = u"Notatki/Przypomnienia.md"_s;
const QString Token = u"⏰ 2026-11-28 18:00"_s;

std::shared_ptr<at::FontCollection> loadFonts()
{
    static std::shared_ptr<at::FontCollection> fonts = [] {
        auto f = std::make_shared<at::FontCollection>();
        const std::string dir = BRULION_SOURCE_DIR "/assets/fonts/";
        for (const char *file : {"ui/BrulionInter-Regular.ttf", "ui/BrulionInter-Bold.ttf", "BrulionEmoji.ttf"})
            f->addFontFile(dir + file);
        f->setFallbackFamilies({u"Brulion Emoji", u"Brulion Inter"});
        return f;
    }();
    return fonts;
}

QString str(std::u16string_view s)
{
    return QString(reinterpret_cast<const QChar *>(s.data()), qsizetype(s.size()));
}

struct Chip {
    int row = -1;
    int from = 0;
    int to = 0;
    bool menu = false;
};

struct Harness {
    FakeEditorHost host;
    PageController controller;
    std::shared_ptr<at::TestPlatform> platform = std::make_shared<at::TestPlatform>();
    at::App app{platform, loadFonts()};
    at::TestWindow &tw;
    at::Entity<PageView> page;
    QList<Chip> chips;

    explicit Harness(const QString &content, at::Size size = {600, 400}) : tw(platform->newWindow(size, 1))
    {
        host.setFile(PagePath, content);
        controller.setNow(u"2026-11-27 10:00"_s);
        controller.setSaveDelay(1000000); // saved by the tests only
        controller.setHost(&host);
        controller.setPath(PagePath);
        page = app.newEntity<PageView>(controller.blocks());
        page.update(app, [this](PageView &v, at::Context<PageView> &cx) {
            v.setController(&controller, cx);
            // The pill in magenta, its words in green: found on the frame.
            const QVariantMap palette{{u"reminderBackground"_s, u"#ff00ff"_s}, {u"reminderChip"_s, u"#00a000"_s}};
            v.setEditStyles(std::make_shared<const EditStyler>(palette, std::u16string(u"Brulion Inter")), cx);
            v.onReminderChip = [this](int row, int from, int to, bool menu, at::Point) {
                chips.append({row, from, to, menu});
            };
        });
        app.openWindow(tw, page);
        tw.settle();
    }
    BlockListModel *model() const { return controller.blocks(); }
    const PageView &view() { return page.read(app); }
    const at::TextInput &input() { return view().input()->read(app); }
    QString inputText() { return view().input() ? str(input().text()) : QString(); }
    int caret() { return input().selection().focus; }
    void edit(int row, int cursor)
    {
        controller.startEditing(row, cursor);
        tw.settle();
    }
    // The chip's box in the window (the field's inline box over the token).
    at::Rect chipBox(int from)
    {
        const at::TextLayout *layout = input().layout();
        if (!layout)
            return {};
        for (const at::PlacedInlineBox &b : layout->inlineBoxes())
            if (b.start == from)
                return b.bounds.translated(input().bounds().value_or(at::Rect{}).origin);
        return {};
    }
};

bool magentaIn(const at::TestWindow &tw, at::Rect r)
{
    for (int y = int(r.top()) + 1; y < int(r.bottom()) - 1; ++y)
        for (int x = int(r.left()) - 1; x < int(r.left()) + 3; ++x) {
            const auto p = tw.pixel(x, y);
            if (p.r > 200 && p.g < 80 && p.b > 200)
                return true;
        }
    return false;
}

} // namespace

class TestAtramentChips : public QObject {
    Q_OBJECT
private Q_SLOTS:
    void chipWhileEditing();
    void caretTreatsTheChipAsOne();
    void clicksOpenTheDialogOrTheMenu();
    void wordsFollowTheLocaleAndTheDay();
};

void TestAtramentChips::chipWhileEditing()
{
    Harness h(u"- Zadzwonić do mamy %1 koniecznie\n- drugi\n"_s.arg(Token));
    h.edit(0, 0);
    // The text keeps the token as written; it is laid out as the pill of its words.
    const QString text = h.inputText();
    QCOMPARE(text, u"Zadzwonić do mamy %1 koniecznie"_s.arg(Token));
    const int from = int(text.indexOf(Token));
    QCOMPARE(h.view().editChips().size(), std::size_t(1));
    const LineChip chip = h.view().editChips().front();
    QCOMPARE(chip.from, from);
    QCOMPARE(chip.to, from + int(Token.size()));
    QCOMPARE(chip.label, u"⏰ jutro 18:00"_s);
    QVERIFY(!chip.alarm);
    const at::Rect box = h.chipBox(from);
    QVERIFY(box.width() > 40);
    QVERIFY2(magentaIn(h.tw, box), "the pill under the chip's words");
    // Its words in the chip's colour.
    bool green = false;
    for (int y = int(box.top()); y < int(box.bottom()) && !green; ++y)
        for (int x = int(box.left()); x < int(box.right()) && !green; ++x) {
            const auto p = h.tw.pixel(x, y);
            green = p.g > 100 && p.r < 120 && p.b < 120;
        }
    QVERIFY(green);
    // The text after it follows the words' width, not the token's.
    const at::TextLayout *layout = h.input().layout();
    QCOMPARE(layout->caretX(chip.to), box.right() - h.input().bounds()->left());
    // Another block edited: no chips (the rendered row draws its own).
    h.edit(1, 0);
    QVERIFY(h.view().editChips().empty());
}

void TestAtramentChips::caretTreatsTheChipAsOne()
{
    Harness h(u"- Kup %1 mleko\n"_s.arg(Token));
    const int from = 4;
    const int to = from + int(Token.size());
    h.edit(0, from);
    QCOMPARE(h.caret(), from);
    h.tw.simulateKeystrokes(u"right");
    QCOMPARE(h.caret(), to);
    h.tw.simulateKeystrokes(u"left");
    QCOMPARE(h.caret(), from);
    // Shift+→ takes it whole.
    h.tw.simulateKeystrokes(u"shift-right");
    QCOMPARE(h.input().selection().range().length(), to - from);
    // A caret put inside it (the controller, Vim, the find bar) goes to its end.
    h.controller.setCursorPosition(from + 3);
    h.controller.startEditing(0, from + 3);
    h.tw.settle();
    QCOMPARE(h.caret(), to);
    QCOMPARE(h.controller.cursorPosition(), to);
    // Backspace after it removes it whole (one undo step of the page's).
    h.tw.simulateKeystrokes(u"backspace");
    QCOMPARE(h.model()->editTextAt(0), u"Kup  mleko"_s);
    QCOMPARE(h.caret(), from);
    QVERIFY(h.view().editChips().empty());
    h.controller.undo();
    h.tw.settle();
    QCOMPARE(h.model()->editTextAt(0), u"Kup %1 mleko"_s.arg(Token));
}

void TestAtramentChips::clicksOpenTheDialogOrTheMenu()
{
    Harness h(u"- Kup %1 mleko\n"_s.arg(Token));
    h.edit(0, 0);
    const int from = 4;
    const at::Rect box = h.chipBox(from);
    const at::Point middle{box.left() + box.width() / 2, box.top() + box.height() / 2};
    // A click: the reminder's dialog; the caret stays.
    h.tw.simulateClick(middle);
    QCOMPARE(h.chips.size(), 1);
    QCOMPARE(h.chips[0].row, 0);
    QCOMPARE(h.chips[0].from, from);
    QCOMPARE(h.chips[0].to, from + int(Token.size()));
    QVERIFY(!h.chips[0].menu);
    QCOMPARE(h.caret(), 0);
    // A right-click: its menu (not the block's).
    h.tw.simulateClick(middle, at::MouseButton::Right);
    QCOMPARE(h.chips.size(), 2);
    QVERIFY(h.chips[1].menu);
    // A finger held on it: its menu too, not the platform's selection.
    h.tw.simulateTouchDown(middle);
    h.tw.advance(600ms);
    h.tw.simulateTouchUp(middle);
    QCOMPARE(h.chips.size(), 3);
    QVERIFY(h.chips[2].menu);
    QCOMPARE(h.caret(), 0);
}

void TestAtramentChips::wordsFollowTheLocaleAndTheDay()
{
    Harness h(u"- Kup %1 mleko\n"_s.arg(Token));
    h.edit(0, 0);
    QCOMPARE(h.view().editChips().front().label, u"⏰ jutro 18:00"_s);
    h.controller.setLocale(u"en"_s);
    h.tw.settle();
    QCOMPARE(h.view().editChips().front().label, u"⏰ tomorrow 18:00"_s);
    h.controller.setNow(u"2026-11-28 08:00"_s);
    h.tw.settle();
    QCOMPARE(h.view().editChips().front().label, u"⏰ today 18:00"_s);
    // Off: the token is text.
    h.page.update(h.app, [](PageView &v, at::Context<PageView> &cx) { v.setReminderChips(false, cx); });
    h.tw.settle();
    QVERIFY(h.view().editChips().empty());
    QVERIFY(h.input().layout()->inlineBoxes().empty());
}

QTEST_MAIN(TestAtramentChips)
#include "tst_atrament_chips.moc"
