// Editing text rows in the block editor drawn by Atrament (src/app/editor/atrament/pageediting.cpp;
// docs/ATRAMENT.md "The block editor in Atrament (M2)", step 2): a page of a PageController over
// FakeEditorHost drawn headless on Atrament's test platform — a click edits a block at the
// character under it (through the rendered text's display ↔ source map), typing reaches the
// controller (its undo, its normalisation coming back), Enter splits, Backspace at the start
// merges, Delete at the end joins, Tab / Shift+Tab, Alt+↑/↓, ↑/↓ across blocks at the caret's x,
// Ctrl+Z / Ctrl+Y through the page's undo, Esc, an input method's line break and composition, a
// task's marker, the highlighter's spans in the field.
#include "blocklistmodel.h"
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
namespace at = brulion::atrament;

namespace {

const QString PagePath = u"Notatki/Edycja.md"_s;

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

struct Harness {
    FakeEditorHost host;
    PageController controller;
    std::shared_ptr<at::TestPlatform> platform = std::make_shared<at::TestPlatform>();
    at::App app{platform, loadFonts()};
    at::TestWindow &tw;
    at::Entity<PageView> page;

    explicit Harness(const QString &content, at::Size size = {600, 400}) : tw(platform->newWindow(size, 1))
    {
        host.setFile(PagePath, content);
        controller.setNow(u"2026-11-27 10:00"_s);
        controller.setSaveDelay(1000000); // saved by the tests only
        controller.setHost(&host);
        controller.setPath(PagePath);
        page = app.newEntity<PageView>(controller.blocks());
        page.update(app, [this](PageView &v, at::Context<PageView> &cx) { v.setController(&controller, cx); });
        app.openWindow(tw, page);
        tw.settle();
    }
    BlockListModel *model() const { return controller.blocks(); }
    const PageView &view() { return page.read(app); }
    QString editText(int row) const { return model()->editTextAt(row); }
    // The field's text and caret.
    QString inputText() { return view().input() ? str(view().input()->read(app).text()) : QString(); }
    int inputCaret() { return view().input() ? view().input()->read(app).selection().focus : -1; }
    // The middle of the caret before display offset `offset` of drawn row `row`.
    at::Point pointOf(int row, int offset)
    {
        const at::TextLayoutHandle *t = view().rowText(row);
        if (!t || !t->layout())
            return {-1, -1};
        const at::Rect caret = t->layout()->caretRect(offset).translated(t->bounds().origin);
        return {caret.left() + 0.5f, caret.top() + caret.height() / 2};
    }
    // Starts editing row `row` by a click before its display offset `offset`.
    void clickInto(int row, int offset)
    {
        const at::Point p = pointOf(row, offset);
        QVERIFY2(p.x >= 0, "the row is drawn");
        tw.simulateClick(p);
        tw.settle();
    }
};

} // namespace

class TestAtramentEditing : public QObject {
    Q_OBJECT
private Q_SLOTS:
    void clickEditsAtTheCharacter();
    void clickMapsThroughMarkup();
    void typingReachesTheController();
    void enterSplits();
    void backspaceAtStartMerges();
    void deleteAtEndJoins();
    void tabIndentsAndOutdents();
    void altArrowsMoveTheBlock();
    void arrowsGoAcrossBlocks();
    void undoIsThePages();
    void escapeStopsEditing();
    void inputMethodLineBreakSplits();
    void compositionCommittedBeforeEnter();
    void shiftEnterIsALineBreak();
    void taskMarkerRemovedByBackspace();
    void controllerEditingFromOutside();
    void stylerColoursTheField();
    void readOnlyPageDoesNotEdit();
    void foldArrowFolds();
    void selectedBlocksOnABand();
    void shiftArrowsSelectBlocks();
};

void TestAtramentEditing::clickEditsAtTheCharacter()
{
    Harness h(u"- Zażółć gęślą jaźń\n- Drugi punkt\n"_s);
    QCOMPARE(h.controller.editingRow(), -1);
    QVERIFY(h.view().editsRow(0));
    // Before "gęślą" (display offset 7: the text is shown as written).
    h.clickInto(0, 7);
    QCOMPARE(h.controller.editingRow(), 0);
    QCOMPARE(h.view().editedRow(), 0);
    QCOMPARE(h.inputText(), u"Zażółć gęślą jaźń"_s);
    QCOMPARE(h.inputCaret(), 7);
    QCOMPARE(h.controller.cursorPosition(), 7);
    // Another row's click moves the field there.
    h.clickInto(1, 5);
    QCOMPARE(h.controller.editingRow(), 1);
    QCOMPARE(h.inputText(), u"Drugi punkt"_s);
    QCOMPARE(h.inputCaret(), 5);
}

void TestAtramentEditing::clickMapsThroughMarkup()
{
    // Shown "Zażółć gęślą i Strona", written with emphasis markers and a wiki link.
    Harness h(u"- Zażółć **gęślą** i [[Strona]] koniec\n"_s);
    const auto *t = h.view().rowText(0);
    QVERIFY(t && t->layout());
    const QString shown = str(t->layout()->text());
    const int g = int(shown.indexOf(u"ęślą"_s)); // inside the bold word
    QVERIFY(g > 0);
    h.clickInto(0, g);
    QCOMPARE(h.controller.editingRow(), 0);
    const QString edit = h.inputText();
    QCOMPARE(edit, u"Zażółć **gęślą** i [[Strona]] koniec"_s);
    QCOMPARE(h.inputCaret(), int(edit.indexOf(u"ęślą"_s)));
    // After the link (its text a construct: its end maps to the link's end in the source).
    h.controller.stopEditing();
    h.tw.settle();
    const int k = int(shown.indexOf(u" koniec"_s));
    h.clickInto(0, k);
    QCOMPARE(h.inputCaret(), int(edit.indexOf(u" koniec"_s)));
}

void TestAtramentEditing::typingReachesTheController()
{
    Harness h(u"- Zażółć\n- Drugi\n"_s);
    h.clickInto(0, 6);
    h.tw.simulateTyping(u" gęślą");
    QCOMPARE(h.editText(0), u"Zażółć gęślą"_s);
    QCOMPARE(h.inputText(), u"Zażółć gęślą"_s);
    QCOMPARE(h.controller.cursorPosition(), 12);
    QVERIFY(h.controller.dirty());
    h.tw.simulateKeystrokes(u"left left");
    QCOMPARE(h.controller.cursorPosition(), 10);
}

void TestAtramentEditing::enterSplits()
{
    Harness h(u"- Zażółć gęślą jaźń\n- Drugi\n"_s);
    h.clickInto(0, 7);
    h.tw.simulateKeystrokes(u"enter");
    QCOMPARE(h.model()->rowCount(), 3);
    QVERIFY(h.editText(0).startsWith(u"Zażółć"_s));
    QCOMPARE(h.editText(1), u"gęślą jaźń"_s);
    QCOMPARE(h.controller.editingRow(), 1);
    QCOMPARE(h.view().editedRow(), 1);
    QCOMPARE(h.inputText(), u"gęślą jaźń"_s);
    QCOMPARE(h.inputCaret(), 0);
    // Typing goes on in the new block.
    h.tw.simulateTyping(u"ą");
    QCOMPARE(h.editText(1), u"ągęślą jaźń"_s);
}

void TestAtramentEditing::backspaceAtStartMerges()
{
    Harness h(u"- Pierwszy\n- drugi blok\n"_s);
    h.clickInto(1, 0);
    QCOMPARE(h.inputCaret(), 0);
    h.tw.simulateKeystrokes(u"backspace");
    QCOMPARE(h.model()->rowCount(), 1);
    QCOMPARE(h.editText(0), u"Pierwszydrugi blok"_s);
    QCOMPARE(h.controller.editingRow(), 0);
    QCOMPARE(h.inputText(), u"Pierwszydrugi blok"_s);
    QCOMPARE(h.inputCaret(), 8);
    // Inside the text Backspace is the field's.
    h.tw.simulateKeystrokes(u"backspace");
    QCOMPARE(h.editText(0), u"Pierwszdrugi blok"_s);
}

void TestAtramentEditing::deleteAtEndJoins()
{
    Harness h(u"- Pierwszy\n- drugi\n"_s);
    h.clickInto(0, 8);
    QCOMPARE(h.inputCaret(), 8);
    h.tw.simulateKeystrokes(u"delete");
    QCOMPARE(h.model()->rowCount(), 1);
    QCOMPARE(h.editText(0), u"Pierwszydrugi"_s);
    QCOMPARE(h.inputText(), u"Pierwszydrugi"_s);
}

void TestAtramentEditing::tabIndentsAndOutdents()
{
    Harness h(u"- Pierwszy\n- drugi\n"_s);
    h.clickInto(1, 2);
    h.tw.simulateKeystrokes(u"tab");
    QCOMPARE(h.model()->index(1).data(BlockListModel::DepthRole).toInt(), 1);
    QCOMPARE(h.controller.editingRow(), 1);
    QCOMPARE(h.inputCaret(), 2); // the caret stays
    h.tw.simulateKeystrokes(u"shift-tab");
    QCOMPARE(h.model()->index(1).data(BlockListModel::DepthRole).toInt(), 0);
    QCOMPARE(h.view().editedRow(), 1);
}

void TestAtramentEditing::altArrowsMoveTheBlock()
{
    Harness h(u"- jeden\n- dwa\n- trzy\n"_s);
    h.clickInto(2, 1);
    h.tw.simulateKeystrokes(u"alt-up");
    QCOMPARE(h.editText(1), u"trzy"_s);
    QCOMPARE(h.controller.editingRow(), 1);
    QCOMPARE(h.view().editedRow(), 1);
    h.tw.simulateKeystrokes(u"alt-down");
    QCOMPARE(h.editText(2), u"trzy"_s);
    QCOMPARE(h.view().editedRow(), 2);
}

void TestAtramentEditing::arrowsGoAcrossBlocks()
{
    Harness h(u"- Zażółć gęślą jaźń\n- Pchnąć w tę łódź\n- koniec\n"_s);
    h.clickInto(1, 4);
    QCOMPARE(h.inputCaret(), 4);
    // ↑ on the first line: the block above, the caret at about the same x.
    h.tw.simulateKeystrokes(u"up");
    QCOMPARE(h.controller.editingRow(), 0);
    QVERIFY2(std::abs(h.inputCaret() - 4) <= 1, qPrintable(QString::number(h.inputCaret())));
    // ↓ on the last line: the block below.
    h.tw.simulateKeystrokes(u"down");
    QCOMPARE(h.controller.editingRow(), 1);
    h.tw.simulateKeystrokes(u"down");
    QCOMPARE(h.controller.editingRow(), 2);
    // The page's last block: ↓ is the field's (the caret goes to the end).
    h.tw.simulateKeystrokes(u"down");
    QCOMPARE(h.controller.editingRow(), 2);
}

void TestAtramentEditing::undoIsThePages()
{
    Harness h(u"- Zażółć\n- Drugi\n"_s);
    h.clickInto(0, 6);
    h.tw.simulateTyping(u" ąę");
    QCOMPARE(h.editText(0), u"Zażółć ąę"_s);
    h.tw.simulateKeystrokes(u"ctrl-z");
    QCOMPARE(h.editText(0), u"Zażółć"_s);
    QCOMPARE(h.inputText(), u"Zażółć"_s);
    h.tw.simulateKeystrokes(u"ctrl-y");
    QCOMPARE(h.editText(0), u"Zażółć ąę"_s);
    QCOMPARE(h.inputText(), u"Zażółć ąę"_s);
    // A split undone: one step.
    h.tw.simulateKeystrokes(u"enter");
    QCOMPARE(h.model()->rowCount(), 3);
    h.tw.simulateKeystrokes(u"ctrl-z");
    QCOMPARE(h.model()->rowCount(), 2);
    QCOMPARE(h.editText(0), u"Zażółć ąę"_s);
    h.tw.simulateKeystrokes(u"ctrl-shift-z");
    QCOMPARE(h.model()->rowCount(), 3);
}

void TestAtramentEditing::escapeStopsEditing()
{
    Harness h(u"- Zażółć\n"_s);
    h.clickInto(0, 2);
    QCOMPARE(h.view().editedRow(), 0);
    h.tw.simulateKeystrokes(u"escape");
    QCOMPARE(h.controller.editingRow(), -1);
    QCOMPARE(h.view().editedRow(), -1);
    // The row is drawn as rendered text again.
    QVERIFY(h.view().rowText(0));
}

void TestAtramentEditing::inputMethodLineBreakSplits()
{
    Harness h(u"- Zażółćgęślą\n"_s);
    h.clickInto(0, 6);
    // Android's keyboards commit Enter as "\n" text.
    h.tw.simulateCommit(u"\n");
    h.tw.settle();
    QCOMPARE(h.model()->rowCount(), 2);
    QCOMPARE(h.editText(0), u"Zażółć"_s);
    QCOMPARE(h.editText(1), u"gęślą"_s);
    QCOMPARE(h.controller.editingRow(), 1);
    QCOMPARE(h.inputCaret(), 0);
}

void TestAtramentEditing::compositionCommittedBeforeEnter()
{
    Harness h(u"- Zażółć \n"_s);
    h.clickInto(0, 7);
    h.tw.simulateComposition(u"gęś");
    QCOMPARE(h.inputText(), u"Zażółć gęś"_s);
    h.tw.simulateKeystrokes(u"enter");
    QCOMPARE(h.model()->rowCount(), 2);
    QCOMPARE(h.editText(0), u"Zażółć gęś"_s);
    QCOMPARE(h.editText(1), QString());
    QCOMPARE(h.controller.editingRow(), 1);
    QVERIFY(!h.view().input()->read(h.app).markedRange());
}

void TestAtramentEditing::shiftEnterIsALineBreak()
{
    Harness h(u"- Zażółć gęślą\n"_s);
    h.clickInto(0, 6);
    h.tw.simulateKeystrokes(u"shift-enter");
    QCOMPARE(h.model()->rowCount(), 1);
    QCOMPARE(h.editText(0), u"Zażółć\n gęślą"_s);
    QCOMPARE(h.inputCaret(), 7);
}

void TestAtramentEditing::taskMarkerRemovedByBackspace()
{
    Harness h(u"- [ ] Zadanie do zrobienia\n"_s);
    QCOMPARE(h.model()->index(0).data(BlockListModel::TaskStatusRole).toString(), u"todo"_s);
    h.clickInto(0, 0);
    QCOMPARE(h.inputText(), u"Zadanie do zrobienia"_s); // the marker is the box, not the text
    h.tw.simulateKeystrokes(u"backspace");
    QCOMPARE(h.model()->index(0).data(BlockListModel::TaskStatusRole).toString(), QString());
    QCOMPARE(h.model()->rowCount(), 1);
    h.tw.simulateKeystrokes(u"ctrl-enter");
    QCOMPARE(h.model()->index(0).data(BlockListModel::TaskStatusRole).toString(), u"todo"_s);
}

void TestAtramentEditing::controllerEditingFromOutside()
{
    // The QML editor (a new block, the find bar) starts editing through the controller.
    Harness h(u"- jeden\n- dwa\n"_s);
    bool started = false;
    h.page.update(h.app, [&](PageView &v, at::Context<PageView> &) { v.onEditingStarted = [&] { started = true; }; });
    h.controller.startEditing(1, 2);
    h.tw.settle();
    QVERIFY(started);
    QCOMPARE(h.view().editedRow(), 1);
    QCOMPARE(h.inputText(), u"dwa"_s);
    QCOMPARE(h.inputCaret(), 2);
    h.tw.simulateTyping(u"ż");
    QCOMPARE(h.editText(1), u"dwża"_s);
}

void TestAtramentEditing::stylerColoursTheField()
{
    Harness h(u"- **gruby** tekst\n"_s);
    int asked = 0;
    h.page.update(h.app, [&](PageView &v, at::Context<PageView> &cx) {
        v.setEditStyler(
                [&asked](std::u16string_view text) {
                    ++asked;
                    std::vector<at::TextSpan> spans;
                    const std::u16string_view marks = u"**";
                    for (std::size_t at = text.find(marks); at != std::u16string_view::npos; at = text.find(marks, at + 2)) {
                        at::TextStyleRefinement dim;
                        dim.color = at::rgb(0xff0000);
                        spans.push_back({{int(at), int(at) + 2}, dim});
                    }
                    return spans;
                },
                cx);
    });
    h.clickInto(0, 0);
    QVERIFY(asked > 0);
    const int before = asked;
    h.tw.settle();
    QCOMPARE(asked, before); // once per text
    // The markers are red where the field draws them.
    const auto *input = h.view().input();
    QVERIFY(input);
    const at::Rect b = input->read(h.app).bounds().value_or(at::Rect{});
    bool red = false;
    for (int y = int(b.top()); y < int(b.bottom()) && !red; ++y)
        for (int x = int(b.left()); x < int(b.left()) + 16 && !red; ++x) {
            const auto p = h.tw.pixel(x, y);
            red = p.r > 180 && p.g < 120 && p.b < 120;
        }
    QVERIFY(red);
    h.tw.simulateTyping(u"x");
    QVERIFY(asked > before);
}

void TestAtramentEditing::readOnlyPageDoesNotEdit()
{
    Harness h(u"- jeden\n"_s);
    h.controller.setReadOnly(true);
    h.tw.settle();
    const at::Point p = h.pointOf(0, 2);
    h.tw.simulateClick(p);
    h.tw.settle();
    QCOMPARE(h.controller.editingRow(), -1);
    QCOMPARE(h.view().editedRow(), -1);
}

void TestAtramentEditing::foldArrowFolds()
{
    Harness h(u"- rodzic z dziećmi\n  - dziecko pierwsze\n  - dziecko drugie\n- sąsiad\n"_s);
    // The app's outline gutter: the arrow's column left of the bullet's.
    h.page.update(h.app, [](PageView &v, at::Context<PageView> &cx) {
        PageMetrics m = v.metrics();
        m.gutter = m.arrowColumn + m.bulletColumn;
        v.setStyle(m, v.palette());
        cx.notify();
    });
    h.tw.settle();
    QVERIFY(h.model()->isRowVisible(1));
    // The arrow's middle: its column left of the bullet, on the first line.
    const at::Point arrow{2 + 16 / 2.0f - 2 + 0.0f, 3 + 12};
    auto inkAt = [&](at::Point p) {
        for (int y = int(p.y) - 4; y <= int(p.y) + 4; ++y)
            for (int x = int(p.x) - 4; x <= int(p.x) + 4; ++x)
                if (h.tw.pixel(x, y).r < 200)
                    return true;
        return false;
    };
    // Hidden until the mouse is over the row.
    QVERIFY(!inkAt(arrow));
    h.tw.simulateMouseMove({200, 15});
    h.tw.settle();
    QVERIFY(inkAt(arrow));
    h.tw.simulateClick(arrow);
    h.tw.settle();
    QVERIFY(!h.model()->isRowVisible(1));
    QVERIFY(!h.model()->isRowVisible(2));
    QCOMPARE(h.controller.editingRow(), -1); // the arrow's click edits nothing
    // Folded: shown without the mouse.
    h.tw.simulateMouseMove({200, 300});
    h.tw.settle();
    QVERIFY(inkAt(arrow));
    h.tw.simulateClick(arrow);
    h.tw.settle();
    QVERIFY(h.model()->isRowVisible(1));
}

void TestAtramentEditing::selectedBlocksOnABand()
{
    Harness h(u"- jeden\n- dwa\n- trzy\n"_s);
    const at::Pixel before = h.tw.pixel(590, 14);
    h.controller.selectRange(0, 1);
    h.tw.settle();
    QCOMPARE(h.model()->index(0).data(BlockListModel::SelectionEdgeRole).toString(), u"first"_s);
    // The band under the selected rows, none under the third.
    const at::Pixel band = h.tw.pixel(590, 14);
    QVERIFY(band.r != before.r || band.g != before.g || band.b != before.b);
    const auto *third = h.view().rowText(2);
    QVERIFY(third);
    const at::Pixel free = h.tw.pixel(590, int(third->bounds().top() + 4));
    QCOMPARE(free.r, before.r);
}

void TestAtramentEditing::shiftArrowsSelectBlocks()
{
    Harness h(u"- jeden\n- dwa trzy\n- cztery\n"_s);
    h.clickInto(1, 4);
    // Shift+↑ on the first line: the text to the block's start first…
    h.tw.simulateKeystrokes(u"shift-up");
    QCOMPARE(h.view().input()->read(h.app).selection().focus, 0);
    QCOMPARE(h.controller.editingRow(), 1);
    // … then the blocks: this one and the one above.
    h.tw.simulateKeystrokes(u"shift-up");
    QCOMPARE(h.controller.editingRow(), -1);
    QCOMPARE(h.controller.selectionCount(), 2);
    // Ctrl+A twice: the block's text, then every block.
    h.clickInto(2, 2);
    h.tw.simulateKeystrokes(u"ctrl-a");
    QCOMPARE(h.controller.editingRow(), 2);
    QCOMPARE(h.view().input()->read(h.app).selection().range().end, 6);
    h.tw.simulateKeystrokes(u"ctrl-a");
    QCOMPARE(h.controller.selectionCount(), 3);
}

QTEST_MAIN(TestAtramentEditing)
#include "tst_atrament_editing.moc"
