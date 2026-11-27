// Vim keys in the block editor drawn by Atrament (src/app/editor/atrament/pagevim.cpp; docs/ATRAMENT.md
// "The block editor in Atrament (M2)", step 2 and "Open questions"; docs/UI.md "Vim mode"): a page of
// a PageController over FakeEditorHost on Atrament's test platform with the editor's VimSession,
// every key typed into the Atrament window as a keyboard gives it (tst_editor_vim drives the session
// with VimSession::keys; here the keys go through the edited block's field and PageView::editorKey,
// insert mode's typing into the field). The page's text, the edited block and its caret, the field's
// text, selection and caret shape, the input method's absence in the command modes, the band of a
// visual selection over blocks, zz and H/L over the list are checked after the keys.
#include "blocklistmodel.h"
#include "editorhost.h"
#include "editstyles.h"
#include "fakeeditorhost.h"
#include "pagecontroller.h"
#include "pageview.h"
#include "text/fonts.h"
#include "ui/testplatform.h"
#include "vimsession.h"

#include <QClipboard>
#include <QGuiApplication>
#include <QTest>

#include <memory>

using namespace brulion;
using namespace brulion::editor;
using namespace brulion::editor::atr;
using namespace Qt::StringLiterals;
namespace at = brulion::atrament;

namespace {

const QString PagePath = u"Notatki/Vim.md"_s;

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

std::u16string u16(const QString &s)
{
    return std::u16string(reinterpret_cast<const char16_t *>(s.utf16()), std::size_t(s.size()));
}

struct Harness {
    FakeEditorHost host;
    PageController controller;
    VimSession vim;
    std::shared_ptr<at::TestPlatform> platform = std::make_shared<at::TestPlatform>();
    at::App app{platform, loadFonts()};
    at::TestWindow &tw;
    at::Entity<PageView> page;
    int vimKeys = 0;
    int escapes = 0;

    explicit Harness(const QString &content, at::Size size = {600, 400}) : tw(platform->newWindow(size, 1))
    {
        host.setFile(PagePath, content);
        controller.setNow(u"2026-11-20 18:30"_s);
        controller.setSaveDelay(1000000); // saved by the tests only
        controller.setHost(&host);
        controller.setPath(PagePath);
        vim.setController(&controller);
        vim.setEnabled(true);
        page = app.newEntity<PageView>(controller.blocks());
        page.update(app, [this](PageView &v, at::Context<PageView> &cx) {
            v.setController(&controller, cx);
            // Code blocks are Atrament's too (a line of one is a line of Vim's).
            v.setDrawsCode(true, PageView::CodeLook{}, cx);
            v.setVim(&vim, cx);
            v.onVimKey = [this] { ++vimKeys; };
            v.onEscape = [this] {
                ++escapes;
                controller.stopEditing();
            };
        });
        app.openWindow(tw, page);
        tw.settle();
    }
    BlockListModel *model() const { return controller.blocks(); }
    const PageView &view() { return page.read(app); }
    const at::TextInput &input() { return view().input()->read(app); }
    QString inputText() { return view().input() ? str(input().text()) : QString(); }
    QString text() const { return controller.text(); }
    // "row:cursor" of the edited block ("-" when none), as tst_editor_vim.
    QString caret() const
    {
        if (controller.editingRow() < 0)
            return u"-"_s;
        return u"%1:%2"_s.arg(controller.editingRow()).arg(controller.cursorPosition());
    }
    // Edit `row` at `cursor` (as a click there does).
    void at(int row, int cursor)
    {
        controller.startEditing(row, cursor);
        tw.settle();
    }
    // Keys in Vim's notation ("dw", "ciwnowe<Esc>", ":s/a/b/<CR>", "<C-r>") typed into the window
    // one by one, as a keyboard gives them.
    void keys(const QString &notation)
    {
        for (int i = 0; i < notation.size();) {
            if (notation[i] == u'<') {
                const int j = int(notation.indexOf(u'>', i + 1));
                if (j > i + 1) {
                    const QString name = notation.mid(i + 1, j - i - 1).toLower();
                    QString stroke;
                    if (name == u"esc")
                        stroke = u"escape"_s;
                    else if (name == u"cr")
                        stroke = u"enter"_s;
                    else if (name == u"bs")
                        stroke = u"backspace"_s;
                    else if (name == u"del")
                        stroke = u"delete"_s;
                    else if (name == u"tab" || name == u"left" || name == u"right" || name == u"up" || name == u"down")
                        stroke = name;
                    else if (name.size() == 3 && name.startsWith(u"c-"))
                        stroke = u"ctrl-"_s + name[2];
                    if (name == u"lt" || name == u"space") {
                        tw.simulateTyping(name == u"lt" ? u"<" : u" ");
                        i = j + 1;
                        continue;
                    }
                    if (!stroke.isEmpty()) {
                        tw.simulateKeystrokes(u16(stroke));
                        i = j + 1;
                        continue;
                    }
                }
            }
            const int n = notation[i].isHighSurrogate() && i + 1 < notation.size() ? 2 : 1;
            tw.simulateTyping(u16(notation.mid(i, n)));
            i += n;
        }
        tw.settle();
    }
    QString run(const QString &k)
    {
        keys(k);
        return text();
    }
};

} // namespace

class TestAtramentVim : public QObject {
    Q_OBJECT
private Q_SLOTS:
    void initTestCase();
    void blocks_data();
    void blocks();
    void fieldFollowsVim();
    void modesShapeTheCaret();
    void nothingTypedInCommandModes();
    void visualSelectionIsTheFields();
    void visualOverBlocksDrawsTheBand();
    void mouseSelectionBecomesVisual();
    void idleEscapeGoesToTheEditor();
    void editorShortcutsPass();
    void scrollAndViewRows();
    void offChangesNothing();
};

void TestAtramentVim::initTestCase()
{
    // The clipboard of the offscreen platform: "+ is read back.
    QGuiApplication::clipboard()->setText(QString());
}

void TestAtramentVim::blocks_data()
{
    // tst_editor_vim's rows, typed into the Atrament window.
    QTest::addColumn<QString>("content");
    QTest::addColumn<int>("row");
    QTest::addColumn<int>("cursor");
    QTest::addColumn<QString>("keys");
    QTest::addColumn<QString>("after");
    QTest::addColumn<QString>("caret");

    const QString three = u"- jeden\n- dwa\n- trzy\n"_s;
    QTest::newRow("x") << u"- Zażółć gęślą\n"_s << 0 << 0 << u"x"_s << u"- ażółć gęślą\n"_s << u"0:0"_s;
    QTest::newRow("dw") << u"- Zażółć gęślą jaźń\n"_s << 0 << 0 << u"dw"_s << u"- gęślą jaźń\n"_s << u"0:0"_s;
    QTest::newRow("cw") << u"- Zażółć gęślą\n"_s << 0 << 0 << u"cwŻółw<Esc>"_s << u"- Żółw gęślą\n"_s << u"0:3"_s;
    QTest::newRow("cw last word") << u"- Zażółć gęślą\n"_s << 0 << 7 << u"cwjaźń<Esc>"_s << u"- Zażółć jaźń\n"_s
                                  << u"0:10"_s;
    QTest::newRow("ciw first word") << u"- Zażółć gęślą\n"_s << 0 << 2 << u"ciwŻółw<Esc>"_s << u"- Żółw gęślą\n"_s
                                    << u"0:3"_s;
    QTest::newRow("ciw in a task") << u"- [ ] kupić mleko\n"_s << 0 << 7 << u"ciwmasło<Esc>"_s
                                   << u"- [ ] kupić masło\n"_s << u"0:10"_s;
    QTest::newRow("A") << u"- ab\n"_s << 0 << 0 << u"A!<Esc>"_s << u"- ab!\n"_s << u"0:2"_s;
    QTest::newRow("dd") << three << 1 << 0 << u"dd"_s << u"- jeden\n- trzy\n"_s << u"1:0"_s;
    QTest::newRow("dd last") << three << 2 << 0 << u"dd"_s << u"- jeden\n- dwa\n"_s << u"1:0"_s;
    QTest::newRow("2dd") << three << 0 << 0 << u"2dd"_s << u"- trzy\n"_s << u"0:0"_s;
    QTest::newRow("dj") << three << 0 << 0 << u"dj"_s << u"- trzy\n"_s << u"0:0"_s;
    QTest::newRow("dG") << three << 1 << 0 << u"dG"_s << u"- jeden\n"_s << u"0:0"_s;
    QTest::newRow("yyp") << three << 0 << 0 << u"yyp"_s << u"- jeden\n- jeden\n- dwa\n- trzy\n"_s << u"1:0"_s;
    QTest::newRow("yyP") << three << 1 << 0 << u"yyP"_s << u"- jeden\n- dwa\n- dwa\n- trzy\n"_s << u"1:0"_s;
    QTest::newRow("ddp") << three << 0 << 0 << u"ddp"_s << u"- dwa\n- jeden\n- trzy\n"_s << u"1:0"_s;
    QTest::newRow("J") << u"- ab\n- cd\n"_s << 0 << 0 << u"J"_s << u"- ab cd\n"_s << u"0:2"_s;
    QTest::newRow("J paragraphs") << u"Pierwszy akapit.\n\nDrugi.\n"_s << 0 << 0 << u"J"_s
                                  << u"Pierwszy akapit. Drugi.\n"_s << u"0:16"_s;
    QTest::newRow("o") << three << 0 << 0 << u"onowy<Esc>"_s << u"- jeden\n- nowy\n- dwa\n- trzy\n"_s << u"1:3"_s;
    QTest::newRow("o task") << u"- [ ] a\n"_s << 0 << 0 << u"ob<Esc>"_s << u"- [ ] a\n- [ ] b\n"_s << u"1:0"_s;
    QTest::newRow("O") << three << 1 << 0 << u"Onowy<Esc>"_s << u"- jeden\n- nowy\n- dwa\n- trzy\n"_s << u"1:3"_s;
    QTest::newRow(">>") << three << 1 << 0 << u">>"_s << u"- jeden\n\t- dwa\n- trzy\n"_s << u"1:0"_s;
    QTest::newRow("<<") << u"- a\n\t- b\n"_s << 1 << 0 << u"<<"_s << u"- a\n- b\n"_s << u"1:0"_s;
    QTest::newRow("i Enter splits") << u"- abcd\n"_s << 0 << 2 << u"i<CR><Esc>"_s << u"- ab\n- cd\n"_s << u"1:0"_s;
    QTest::newRow("vjd joins blocks") << u"- abc\n- def\n"_s << 0 << 1 << u"vjd"_s << u"- af\n"_s << u"0:1"_s;
    QTest::newRow("Vjd") << three << 0 << 0 << u"Vjd"_s << u"- trzy\n"_s << u"0:0"_s;
    QTest::newRow(":%s") << u"- ala ma\n- kota ala\n"_s << 0 << 0 << u":%s/ala/Ola/g<CR>"_s
                         << u"- Ola ma\n- kota Ola\n"_s << u"1:0"_s;
    QTest::newRow("r Polish") << u"- kot\n"_s << 0 << 1 << u"rą"_s << u"- kąt\n"_s << u"0:1"_s;
    QTest::newRow("~") << u"- żółw\n"_s << 0 << 0 << u"4~"_s << u"- ŻÓŁW\n"_s << u"0:3"_s;
    QTest::newRow("heading") << u"# Tytuł\n\nTekst\n"_s << 0 << 0 << u"jcwSłowo<Esc>"_s << u"# Tytuł\n\nSłowo\n"_s
                             << u"1:4"_s;
    QTest::newRow("dd in code") << u"```js\nlet a = 1\nlet b = 2\nlet c = 3\n```\n"_s << 0 << 10 << u"dd"_s
                                << u"```js\nlet a = 1\nlet c = 3\n```\n"_s << u"0:10"_s;
    QTest::newRow("o in code") << u"```js\n  a\n```\n"_s << 0 << 0 << u"ob<Esc>"_s << u"```js\n  a\n  b\n```\n"_s
                               << u"0:6"_s;
    QTest::newRow("C-a") << u"- zadanie 5 z 9\n"_s << 0 << 0 << u"<C-a>"_s << u"- zadanie 6 z 9\n"_s << u"0:8"_s;
    QTest::newRow("C-x count") << u"- [ ] kupić 12 jaj\n"_s << 0 << 0 << u"5<C-x>"_s << u"- [ ] kupić 7 jaj\n"_s
                               << u"0:6"_s;
    QTest::newRow(") into the next block") << u"- Zażółć.\n- Gęślą.\n"_s << 0 << 0 << u"))"_s
                                           << u"- Zażółć.\n- Gęślą.\n"_s << u"1:5"_s;
    QTest::newRow("cis") << u"- Pierwsze zdanie. Drugie zdanie.\n"_s << 0 << 0 << u"cisNowe.<Esc>"_s
                         << u"- Nowe. Drugie zdanie.\n"_s << u"0:4"_s;
}

void TestAtramentVim::blocks()
{
    QFETCH(QString, content);
    QFETCH(int, row);
    QFETCH(int, cursor);
    QFETCH(QString, keys);
    QFETCH(QString, after);
    QFETCH(QString, caret);
    Harness h(content);
    h.at(row, cursor);
    QCOMPARE(h.view().editedRow(), row);
    QCOMPARE(h.run(keys), after);
    QCOMPARE(h.caret(), caret);
    QCOMPARE(h.vim.mode(), u"normal"_s);
    QCOMPARE(h.vim.commandLine(), QString());
    // The field shows the edited block as the controller has it, the caret where Vim put it.
    const int edited = h.controller.editingRow();
    QCOMPARE(h.view().editedRow(), edited);
    QCOMPARE(h.inputText(), h.model()->editTextAt(edited));
    QCOMPARE(h.input().selection().focus, h.controller.cursorPosition());
    QVERIFY(h.vimKeys > 0);
    // One undo step for the whole change — and back.
    if (!keys.startsWith(u"yy"_s) && !keys.startsWith(u"dd"_s) && !keys.startsWith(u"))"_s)) {
        h.run(u"u"_s);
        QCOMPARE(h.text(), content);
    }
}

void TestAtramentVim::fieldFollowsVim()
{
    Harness h(u"- Zażółć gęślą\n- ab\n- jaźń długa\n"_s);
    h.at(0, 5);
    // j / k keep the column, the field moves with the caret from block to block.
    h.keys(u"j"_s);
    QCOMPARE(h.caret(), u"1:1"_s);
    QCOMPARE(h.view().editedRow(), 1);
    QCOMPARE(h.inputText(), u"ab"_s);
    h.keys(u"j"_s);
    QCOMPARE(h.caret(), u"2:5"_s);
    QCOMPARE(h.input().selection().focus, 5);
    h.keys(u"gg"_s);
    QCOMPARE(h.caret(), u"0:0"_s);
    h.keys(u"ww"_s);
    QCOMPARE(h.caret(), u"1:0"_s);
    h.keys(u"$"_s);
    QCOMPARE(h.caret(), u"1:1"_s);
    // The text Vim changes is the field's at once.
    h.keys(u"x"_s);
    QCOMPARE(h.inputText(), u"a"_s);
}

void TestAtramentVim::modesShapeTheCaret()
{
    Harness h(u"- Zażółć gęślą\n"_s);
    h.at(0, 2);
    // Normal mode: a steady block over "ż", no typing, no input method.
    QCOMPARE(h.vim.mode(), u"normal"_s);
    QCOMPARE(h.input().caretShape(), at::CaretShape::Block);
    QVERIFY(h.input().readOnly());
    QVERIFY(!h.tw.inputHandler());
    const at::Rect block = h.input().caretBox().value_or(at::Rect{});
    const at::TextLayout *layout = h.input().layout();
    QVERIFY(layout);
    QCOMPARE(block.width(), layout->caretX(3) - layout->caretX(2));
    // Insert mode: the bar, typing and the input method.
    h.keys(u"i"_s);
    QCOMPARE(h.vim.mode(), u"insert"_s);
    QCOMPARE(h.input().caretShape(), at::CaretShape::Bar);
    QVERIFY(!h.input().readOnly());
    QVERIFY(h.tw.inputHandler());
    h.keys(u"<Esc>"_s);
    QCOMPARE(h.input().caretShape(), at::CaretShape::Block);
    QCOMPARE(h.input().selection().focus, 1); // (Esc steps back onto the character before)
    // Replace mode: an underline; what is typed replaces, as Vim's.
    h.keys(u"R"_s);
    QCOMPARE(h.vim.mode(), u"replace"_s);
    QCOMPARE(h.input().caretShape(), at::CaretShape::Underline);
    QVERIFY(h.input().readOnly());
    h.keys(u"ąę<Esc>"_s);
    QCOMPARE(h.text(), u"- Ząęółć gęślą\n"_s);
    // Visual mode: the block again (hidden while the selection shows the place).
    h.keys(u"v"_s);
    QCOMPARE(h.vim.mode(), u"visual"_s);
    QCOMPARE(h.input().caretShape(), at::CaretShape::Block);
    h.keys(u"<Esc>"_s);
    // Vim off: the bar, typing again.
    h.vim.setEnabled(false);
    h.tw.settle();
    QCOMPARE(h.input().caretShape(), at::CaretShape::Bar);
    QVERIFY(!h.input().readOnly());
}

void TestAtramentVim::nothingTypedInCommandModes()
{
    Harness h(u"- kot\n"_s);
    h.at(0, 0);
    // A key Vim has no command for is dropped, never typed.
    h.keys(u"q"_s); // (q waits for a register)
    h.keys(u"<Esc>"_s);
    h.keys(u"Q"_s);
    QCOMPARE(h.text(), u"- kot\n"_s);
    QCOMPARE(h.inputText(), u"kot"_s);
    // An input method's text cannot reach the field either: it has none in normal mode.
    QVERIFY(!h.tw.simulateCommit(u"ż"));
    QCOMPARE(h.inputText(), u"kot"_s);
    // Insert mode types into the field, which tells the controller.
    h.keys(u"ażółw<Esc>"_s);
    QCOMPARE(h.text(), u"- kżółwot\n"_s);
    QCOMPARE(h.inputText(), u"kżółwot"_s);
    QCOMPARE(h.caret(), u"0:4"_s);
    // Typed with an input method in insert mode: the composition is committed by Esc.
    h.keys(u"A"_s);
    QVERIFY(h.tw.simulateComposition(u"źdź"));
    h.keys(u"<Esc>"_s);
    QCOMPARE(h.text(), u"- kżółwotźdź\n"_s);
}

void TestAtramentVim::visualSelectionIsTheFields()
{
    Harness h(u"- Zażółć gęślą\n"_s);
    h.at(0, 0);
    h.keys(u"ve"_s);
    QCOMPARE(h.vim.mode(), u"visual"_s);
    // The word with its last character: the field's selection, drawn by it.
    QCOMPARE(h.input().selection().anchor, 0);
    QCOMPARE(h.input().selection().focus, 6);
    QVERIFY(!h.input().caretBox()); // (the selection shows the place)
    h.keys(u"o"_s); // the ends swapped
    QCOMPARE(h.input().selection().anchor, 6);
    QCOMPARE(h.input().selection().focus, 0);
    h.keys(u"d"_s);
    QCOMPARE(h.text(), u"- gęślą\n"_s);
    QVERIFY(h.input().selection().range().isEmpty());
    QCOMPARE(h.input().caretShape(), at::CaretShape::Block);
}

void TestAtramentVim::visualOverBlocksDrawsTheBand()
{
    Harness h(u"- jeden\n- dwa\n- trzy\n- cztery\n"_s);
    h.at(1, 0);
    h.keys(u"V"_s);
    QCOMPARE(h.vim.mode(), u"visualLine"_s);
    // One block: its text selected in the field.
    QCOMPARE(h.input().selection().range().length(), 3);
    h.keys(u"j"_s);
    // Two blocks: drawn as selected (the band) while the caret's block stays edited.
    QCOMPARE(h.caret(), u"2:0"_s);
    QVERIFY(h.model()->isRowSelected(1));
    QVERIFY(h.model()->isRowSelected(2));
    QVERIFY(!h.model()->isRowSelected(3));
    QCOMPARE(h.model()->index(1).data(BlockListModel::SelectionEdgeRole).toString(), u"first"_s);
    QCOMPARE(h.view().editedRow(), 2);
    h.keys(u"d"_s);
    QCOMPARE(h.text(), u"- jeden\n- cztery\n"_s);
    QVERIFY(!h.model()->isRowSelected(1));
}

void TestAtramentVim::mouseSelectionBecomesVisual()
{
    Harness h(u"- Zażółć gęślą\n"_s);
    h.at(0, 0);
    // A mouse's drag over "gęślą" in normal mode, then a key: Vim's visual selection.
    const at::TextInput &input = h.input();
    const at::Rect from = input.boundsForRange(at::Range{7, 7}).value_or(at::Rect{});
    const at::Rect to = input.boundsForRange(at::Range{12, 12}).value_or(at::Rect{});
    const float y = from.top() + from.height() / 2;
    h.tw.simulateMouseDown({from.left() + 0.5f, y});
    h.tw.simulateMouseMove({to.left() + 0.5f, y}, at::MouseButton::Left);
    h.tw.simulateMouseUp({to.left() + 0.5f, y});
    QCOMPARE(h.input().selection().range().start, 7);
    QCOMPARE(h.input().selection().range().end, 12);
    h.keys(u"d"_s);
    QCOMPARE(h.text(), u"- Zażółć \n"_s);
}

void TestAtramentVim::idleEscapeGoesToTheEditor()
{
    Harness h(u"- jeden\n- dwa\n"_s);
    h.at(1, 0);
    // Esc with a command typed ends the command; with nothing of Vim's it is the editor's.
    h.keys(u"d"_s);
    QCOMPARE(h.vim.pending(), u"d"_s);
    h.keys(u"<Esc>"_s);
    QCOMPARE(h.escapes, 0);
    QCOMPARE(h.vim.pending(), QString());
    h.keys(u"<Esc>"_s);
    QCOMPARE(h.escapes, 1);
    QCOMPARE(h.controller.editingRow(), -1);
}

void TestAtramentVim::editorShortcutsPass()
{
    Harness h(u"- jeden\n- dwa\n"_s);
    h.at(1, 1);
    // Tab and Shift+Tab are the editor's in normal mode (vimPasses): the block indents.
    h.keys(u"<Tab>"_s);
    QCOMPARE(h.text(), u"- jeden\n\t- dwa\n"_s);
    h.tw.simulateKeystrokes(u"shift-tab");
    QCOMPARE(h.text(), u"- jeden\n- dwa\n"_s);
    // Alt+↑ moves the block (the editor's), Ctrl+Z undoes it (the page's undo, Vim's history too).
    h.tw.simulateKeystrokes(u"alt-up");
    QCOMPARE(h.text(), u"- dwa\n- jeden\n"_s);
    QCOMPARE(h.caret(), u"0:1"_s);
    h.tw.simulateKeystrokes(u"ctrl-z");
    QCOMPARE(h.text(), u"- jeden\n- dwa\n"_s);
    // Ctrl+R is Vim's redo.
    h.keys(u"<C-r>"_s);
    QCOMPARE(h.text(), u"- dwa\n- jeden\n"_s);
}

void TestAtramentVim::scrollAndViewRows()
{
    QString content;
    for (int i = 0; i < 80; ++i)
        content += u"- wiersz %1 zażółć\n"_s.arg(i);
    Harness h(content, {600, 300});
    h.at(40, 0);
    h.tw.settle();
    // zt: the row to the view's top.
    h.keys(u"zt"_s);
    const int top = h.view().rowAtY(5);
    QVERIFY2(top == 40 || top == 39, qPrintable(QString::number(top)));
    // H / L: the first and the last row in view.
    h.keys(u"L"_s);
    const int last = h.controller.editingRow();
    QVERIFY2(last > 45 && last < 52, qPrintable(QString::number(last)));
    h.keys(u"H"_s);
    QVERIFY2(std::abs(h.controller.editingRow() - top) <= 1, qPrintable(h.caret()));
    // zz: the row in the middle.
    h.at(70, 0);
    h.keys(u"zz"_s);
    const int middle = h.view().rowAtY(150);
    QVERIFY2(std::abs(middle - 70) <= 1, qPrintable(QString::number(middle)));
}

void TestAtramentVim::offChangesNothing()
{
    Harness h(u"- kot\n"_s);
    h.vim.setEnabled(false);
    h.at(0, 3);
    QCOMPARE(h.input().caretShape(), at::CaretShape::Bar);
    h.keys(u"dw"_s);
    QCOMPARE(h.text(), u"- kotdw\n"_s);
    QCOMPARE(h.vimKeys, 0);
}

QTEST_MAIN(TestAtramentVim)
#include "tst_atrament_vim.moc"
