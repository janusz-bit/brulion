// Vim keys in the block editor and in source mode (docs/UI.md "Vim mode"): VimSession with its
// engine over a PageController's page (VimPageBuffer: blocks as lines) and over source mode's
// TextEdit (VimSourceBuffer), driven by keys in Vim's notation (VimSession::keys(): what insert
// mode leaves to the editor is typed into the block). The page's text, the edited block and its
// caret, the block selection and the undo history are checked after the keys.
#include "blocklistmodel.h"
#include "editorhost.h"
#include "fakeeditorhost.h"
#include "pagecontroller.h"
#include "vimsession.h"

#include "testtiming.h"

#include <QClipboard>
#include <QGuiApplication>
#include <QQmlComponent>
#include <QQmlEngine>
#include <QQuickItem>
#include <QQuickTextDocument>
#include <QSignalSpy>
#include <QTest>
#include <QTextDocument>

using namespace brulion;
using namespace brulion::editor;
using namespace Qt::StringLiterals;

namespace {

const QString Page = u"Notatki/Vim.md"_s;

struct Editor {
    FakeEditorHost host;
    PageController page;
    VimSession vim;

    explicit Editor(const QString &content, const QString &path = Page)
    {
        host.setFile(path, content);
        page.setNow(u"2026-11-20 18:30"_s);
        page.setSaveDelay(1000000); // saved by the tests only
        page.setHost(&host);
        page.setPath(path);
        vim.setController(&page);
        vim.setEnabled(true);
    }
    /// Edit `row` at `cursor` (a click there).
    void at(int row, int cursor) { page.startEditing(row, cursor); }
    QString text() const { return page.text(); }
    /// "row:cursor" of the edited block ("-" when none).
    QString caret() const
    {
        if (page.editingRow() < 0)
            return u"-"_s;
        return u"%1:%2"_s.arg(page.editingRow()).arg(page.cursorPosition());
    }
    QString run(const QString &keys)
    {
        vim.keys(keys);
        return text();
    }
};

} // namespace

class TestEditorVim : public QObject {
    Q_OBJECT

private Q_SLOTS:
    void initTestCase();
    void blocks_data();
    void blocks();
    void jkKeepTheColumn();
    void countsAcrossBlocks();
    void undoIsOneStepPerChange();
    void dotRepeatOverBlocks();
    void visualOverBlocks();
    void visualInOneBlock();
    void foldedBlocks();
    void tablesAndFrontmatter();
    void blockIdsOnPut();
    void registersAcrossPages();
    void writeAndQuit();
    void emptyPage();
    void offChangesNothing();
    void modesTold();
    void adoptSelections();
    void sourceMode();
    void sourceModeUndo();
    void normalModeKeyCost();
};

void TestEditorVim::initTestCase()
{
    // The clipboard of the offscreen platform: "+ is read back.
    QGuiApplication::clipboard()->setText(QString());
}

void TestEditorVim::blocks_data()
{
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
    QTest::newRow("dw first word") << u"- Zażółć gęślą\n"_s << 0 << 0 << u"dw"_s << u"- gęślą\n"_s << u"0:0"_s;
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
    QTest::newRow("yyp task") << u"- [ ] zadanie\n"_s << 0 << 0 << u"yyp"_s << u"- [ ] zadanie\n- [ ] zadanie\n"_s
                              << u"1:0"_s;
    QTest::newRow("ddp") << three << 0 << 0 << u"ddp"_s << u"- dwa\n- jeden\n- trzy\n"_s << u"1:0"_s;
    QTest::newRow("J") << u"- ab\n- cd\n"_s << 0 << 0 << u"J"_s << u"- ab cd\n"_s << u"0:2"_s;
    QTest::newRow("J paragraphs") << u"Pierwszy akapit.\n\nDrugi.\n"_s << 0 << 0 << u"J"_s
                                  << u"Pierwszy akapit. Drugi.\n"_s << u"0:16"_s;
    QTest::newRow("o") << three << 0 << 0 << u"onowy<Esc>"_s << u"- jeden\n- nowy\n- dwa\n- trzy\n"_s << u"1:3"_s;
    QTest::newRow("o task") << u"- [ ] a\n"_s << 0 << 0 << u"ob<Esc>"_s << u"- [ ] a\n- [ ] b\n"_s << u"1:0"_s;
    QTest::newRow("O") << three << 1 << 0 << u"Onowy<Esc>"_s << u"- jeden\n- nowy\n- dwa\n- trzy\n"_s << u"1:3"_s;
    QTest::newRow("O first") << three << 0 << 0 << u"Onowy<Esc>"_s << u"- nowy\n- jeden\n- dwa\n- trzy\n"_s << u"0:3"_s;
    QTest::newRow(">>") << three << 1 << 0 << u">>"_s << u"- jeden\n\t- dwa\n- trzy\n"_s << u"1:0"_s;
    QTest::newRow("<<") << u"- a\n\t- b\n"_s << 1 << 0 << u"<<"_s << u"- a\n- b\n"_s << u"1:0"_s;
    QTest::newRow(">j") << three << 1 << 0 << u">j"_s << u"- jeden\n\t- dwa\n\t- trzy\n"_s << u"1:0"_s;
    QTest::newRow("i Enter splits") << u"- abcd\n"_s << 0 << 2 << u"i<CR><Esc>"_s << u"- ab\n- cd\n"_s << u"1:0"_s;
    QTest::newRow("p charwise with a line break")
            << u"- ab\n- x\n"_s << 0 << 0 << u"vjy$p"_s << u"- abab\n- x\n- x\n"_s << u"0:2"_s;
    QTest::newRow("vjd joins blocks") << u"- abc\n- def\n"_s << 0 << 1 << u"vjd"_s << u"- af\n"_s << u"0:1"_s;
    QTest::newRow("Vjd") << three << 0 << 0 << u"Vjd"_s << u"- trzy\n"_s << u"0:0"_s;
    QTest::newRow(":%s") << u"- ala ma\n- kota ala\n"_s << 0 << 0 << u":%s/ala/Ola/g<CR>"_s
                         << u"- Ola ma\n- kota Ola\n"_s << u"1:0"_s;
    QTest::newRow(":s on the line") << u"- a a\n- a\n"_s << 0 << 0 << u":s/a/b/<CR>"_s << u"- b a\n- a\n"_s << u"0:0"_s;
    QTest::newRow("r Polish") << u"- kot\n"_s << 0 << 1 << u"rą"_s << u"- kąt\n"_s << u"0:1"_s;
    QTest::newRow("~") << u"- żółw\n"_s << 0 << 0 << u"4~"_s << u"- ŻÓŁW\n"_s << u"0:3"_s;
    QTest::newRow("heading") << u"# Tytuł\n\nTekst\n"_s << 0 << 0 << u"jcwSłowo<Esc>"_s << u"# Tytuł\n\nSłowo\n"_s
                             << u"1:4"_s;
    // A line of a code block is a line of its text.
    QTest::newRow("dd in code") << u"```js\nlet a = 1\nlet b = 2\nlet c = 3\n```\n"_s << 0 << 10 << u"dd"_s
                                << u"```js\nlet a = 1\nlet c = 3\n```\n"_s << u"0:10"_s;
    QTest::newRow("o in code") << u"```js\n  a\n```\n"_s << 0 << 0 << u"ob<Esc>"_s << u"```js\n  a\n  b\n```\n"_s
                               << u"0:6"_s;
    QTest::newRow("yyp in code") << u"```\nx\ny\n```\n"_s << 0 << 0 << u"yyp"_s << u"```\nx\nx\ny\n```\n"_s << u"0:2"_s;
    QTest::newRow(">> in code") << u"```\nx\ny\n```\n"_s << 0 << 2 << u">>"_s << u"```\nx\n\ty\n```\n"_s << u"0:3"_s;
    QTest::newRow("dd whole code block") << u"- a\n\n```\nx\n```\n"_s << 1 << 0 << u"dd"_s << u"- a\n\n"_s << u"0:0"_s;
    QTest::newRow("J in code") << u"```\nx\n  y\n```\n"_s << 0 << 0 << u"J"_s << u"```\nx y\n```\n"_s << u"0:1"_s;
}

void TestEditorVim::blocks()
{
    QFETCH(QString, content);
    QFETCH(int, row);
    QFETCH(int, cursor);
    QFETCH(QString, keys);
    QFETCH(QString, after);
    QFETCH(QString, caret);
    Editor e(content);
    e.at(row, cursor);
    QCOMPARE(e.run(keys), after);
    QCOMPARE(e.caret(), caret);
    QCOMPARE(e.vim.mode(), u"normal"_s);
    QCOMPARE(e.vim.commandLine(), QString());
    // One undo step for the whole sequence's change (each test row is one change) — and back.
    if (!keys.startsWith(u"yy"_s) && !keys.startsWith(u"dd"_s) && !keys.startsWith(u"vjy"_s)
        && !keys.startsWith(u"j"_s)) {
        e.run(u"u"_s);
        QCOMPARE(e.text(), content);
    }
}

void TestEditorVim::jkKeepTheColumn()
{
    Editor e(u"- Zażółć gęślą\n- ab\n- jaźń długa\n"_s);
    e.at(0, 5);
    e.run(u"j"_s);
    QCOMPARE(e.caret(), u"1:1"_s);
    e.run(u"j"_s);
    QCOMPARE(e.caret(), u"2:5"_s);
    e.run(u"kk"_s);
    QCOMPARE(e.caret(), u"0:5"_s);
    e.run(u"$j"_s);
    QCOMPARE(e.caret(), u"1:1"_s);
    e.run(u"j"_s);
    QCOMPARE(e.caret(), u"2:9"_s);
    // A click elsewhere: that column is the one kept.
    e.at(0, 2);
    e.run(u"jj"_s);
    QCOMPARE(e.caret(), u"2:2"_s);
    // gg / G, on the page's edges nothing moves.
    e.run(u"G"_s);
    QCOMPARE(e.caret(), u"2:0"_s);
    e.run(u"j"_s);
    QCOMPARE(e.caret(), u"2:0"_s);
    e.run(u"gg"_s);
    QCOMPARE(e.caret(), u"0:0"_s);
    e.run(u"k"_s);
    QCOMPARE(e.caret(), u"0:0"_s);
    // w goes on into the next block.
    e.run(u"ww"_s);
    QCOMPARE(e.caret(), u"1:0"_s);
    e.run(u"b"_s);
    QCOMPARE(e.caret(), u"0:7"_s);
}

void TestEditorVim::countsAcrossBlocks()
{
    Editor e(u"- 1\n- 2\n- 3\n- 4\n- 5\n- 6\n"_s);
    e.at(0, 0);
    e.run(u"3j"_s);
    QCOMPARE(e.caret(), u"3:0"_s);
    e.run(u"2k"_s);
    QCOMPARE(e.caret(), u"1:0"_s);
    e.run(u"3dd"_s);
    QCOMPARE(e.text(), u"- 1\n- 5\n- 6\n"_s);
    e.run(u"5G"_s); // past the end: the last
    QCOMPARE(e.caret(), u"2:0"_s);
    e.run(u":1<CR>"_s);
    QCOMPARE(e.caret(), u"0:0"_s);
}

void TestEditorVim::undoIsOneStepPerChange()
{
    Editor e(u"- Zażółć gęślą jaźń\n- drugi\n"_s);
    e.at(0, 0);
    e.run(u"cwŻółty<Esc>"_s);
    QCOMPARE(e.text(), u"- Żółty gęślą jaźń\n- drugi\n"_s);
    e.run(u"onowy<CR>i jeszcze<Esc>"_s);
    QCOMPARE(e.text(), u"- Żółty gęślą jaźń\n- nowy\n- i jeszcze\n- drugi\n"_s);
    e.run(u"u"_s); // the o with its typing and its Enter
    QCOMPARE(e.text(), u"- Żółty gęślą jaźń\n- drugi\n"_s);
    e.run(u"u"_s); // the cw with its typing
    QCOMPARE(e.text(), u"- Zażółć gęślą jaźń\n- drugi\n"_s);
    QVERIFY(!e.page.canUndo());
    e.run(u"u"_s);
    QCOMPARE(e.vim.message(), u"Already at the oldest change"_s);
    QVERIFY(e.vim.messageError());
    e.run(u"<C-r>"_s);
    QCOMPARE(e.text(), u"- Żółty gęślą jaźń\n- drugi\n"_s);
    e.run(u"<C-r>"_s);
    QCOMPARE(e.text(), u"- Żółty gęślą jaźń\n- nowy\n- i jeszcze\n- drugi\n"_s);
    // The editor's own Ctrl+Z steps the same history.
    e.page.undo();
    QCOMPARE(e.text(), u"- Żółty gęślą jaźń\n- drugi\n"_s);
    // A substitution over every block: one step.
    e.run(u":%s/ź/rz/g<CR>"_s);
    QCOMPARE(e.text(), u"- Żółty gęślą jarzń\n- drugi\n"_s);
    e.run(u"u"_s);
    QCOMPARE(e.text(), u"- Żółty gęślą jaźń\n- drugi\n"_s);
}

void TestEditorVim::dotRepeatOverBlocks()
{
    Editor e(u"- a\n- b\n- c\n- d\n"_s);
    e.at(0, 0);
    e.run(u"dd."_s);
    QCOMPARE(e.text(), u"- c\n- d\n"_s);
    e.run(u"A!<Esc>j."_s);
    QCOMPARE(e.text(), u"- c!\n- d!\n"_s);
    e.run(u"gg>>j."_s);
    QCOMPARE(e.text(), u"- c!\n\t- d!\n"_s);
}

void TestEditorVim::visualOverBlocks()
{
    Editor e(u"- jeden\n- dwa\n- trzy\n- cztery\n"_s);
    e.at(1, 0);
    e.run(u"V"_s);
    QCOMPARE(e.vim.mode(), u"visualLine"_s);
    // One block: its text is selected in the block.
    QCOMPARE(e.vim.pageBuffer()->shownSelectionRow(), 1);
    QCOMPARE(e.page.selectionCount(), 0);
    e.run(u"j"_s);
    // Two blocks: a block selection.
    QCOMPARE(e.page.selectionCount(), 2);
    QVERIFY(e.vim.ownsBlockSelection());
    e.run(u"y"_s);
    QCOMPARE(e.vim.mode(), u"normal"_s);
    QCOMPARE(e.page.selectionCount(), 0);
    QCOMPARE(e.caret(), u"1:0"_s);
    e.run(u"Gp"_s);
    QCOMPARE(e.text(), u"- jeden\n- dwa\n- trzy\n- cztery\n- dwa\n- trzy\n"_s);
    e.run(u"ggjVj>"_s);
    QCOMPARE(e.text(), u"- jeden\n\t- dwa\n\t- trzy\n- cztery\n- dwa\n- trzy\n"_s);
    e.run(u"u"_s);
    QCOMPARE(e.text(), u"- jeden\n- dwa\n- trzy\n- cztery\n- dwa\n- trzy\n"_s);
    e.run(u"ggVjjd"_s);
    QCOMPARE(e.text(), u"- cztery\n- dwa\n- trzy\n"_s);
    // Esc in visual mode: back to editing the block at the caret.
    e.run(u"Vj<Esc>"_s);
    QCOMPARE(e.page.selectionCount(), 0);
    QCOMPARE(e.caret(), u"1:0"_s);
}

void TestEditorVim::visualInOneBlock()
{
    Editor e(u"- Zażółć gęślą\n"_s);
    e.at(0, 0);
    e.run(u"ve"_s);
    QCOMPARE(e.vim.mode(), u"visual"_s);
    VimPageBuffer *b = e.vim.pageBuffer();
    QCOMPARE(b->shownSelectionRow(), 0);
    QCOMPARE(b->shownSelectionAnchor(), 0);
    QCOMPARE(b->shownSelectionCursor(), 6); // the word's last character included
    QSignalSpy shown(&e.vim, &VimSession::textSelectionRequested);
    e.run(u"o"_s); // the ends swapped
    QCOMPARE(shown.count(), 1);
    QCOMPARE(shown.last().at(1).toInt(), 6);
    QCOMPARE(shown.last().at(2).toInt(), 0);
    e.run(u"d"_s);
    QCOMPARE(e.text(), u"- gęślą\n"_s); // (no blank after the marker: the text starts with the word)
    QCOMPARE(b->shownSelectionRow(), -1);
}

void TestEditorVim::foldedBlocks()
{
    const QString content = u"- rodzic\n  - dziecko\n  - drugie\n- dalej\n"_s;
    {
        // An open parent: dd takes the block alone, its children move up.
        Editor e(content);
        e.at(0, 0);
        e.run(u"dd"_s);
        QCOMPARE(e.text(), u"  - dziecko\n  - drugie\n- dalej\n"_s); // (as in the text)
    }
    {
        // A folded one is one line with what it hides: dd takes it all, j steps over it.
        Editor e(content);
        e.page.setFolded(0, true);
        e.at(0, 0);
        e.run(u"j"_s);
        QCOMPARE(e.caret(), u"3:0"_s);
        e.run(u"k"_s);
        QCOMPARE(e.caret(), u"0:0"_s);
        e.run(u"dd"_s);
        QCOMPARE(e.text(), u"- dalej\n"_s);
        e.run(u"u"_s);
        QCOMPARE(e.text(), content);
    }
    {
        // zc / zo / za fold the block.
        Editor e(content);
        e.at(0, 0);
        e.run(u"zc"_s);
        QVERIFY(e.page.blocks()->isCollapsed(0));
        e.run(u"zo"_s);
        QVERIFY(!e.page.blocks()->isCollapsed(0));
        e.run(u"za"_s);
        QVERIFY(e.page.blocks()->isCollapsed(0));
        e.run(u"zR"_s);
        QVERIFY(!e.page.blocks()->isCollapsed(0));
    }
    {
        // A folded block yanked brings its children along.
        Editor e(content);
        e.page.setFolded(0, true);
        e.at(0, 0);
        e.run(u"yyjp"_s);
        QCOMPARE(e.text(), u"- rodzic\n  - dziecko\n  - drugie\n- dalej\n- rodzic\n  - dziecko\n  - drugie\n"_s);
    }
}

void TestEditorVim::tablesAndFrontmatter()
{
    const QString content = u"---\ntytuł: Próba\n---\n- przed\n\n| a | b |\n|---|---|\n| 1 | 2 |\n\n- po\n"_s;
    Editor e(content);
    e.at(e.page.rowOfLine(3), 0);
    const int before = e.page.editingRow();
    e.run(u"gg"_s);
    // The frontmatter is no line of Vim's: gg is the first block.
    QCOMPARE(e.page.editingRow(), before);
    e.run(u"j"_s);
    // A table is one line: it is selected, not edited.
    QCOMPARE(e.page.editingRow(), -1);
    QCOMPARE(e.page.selectedRow(), before + 1);
    QCOMPARE(e.page.blocks()->viewAt(e.page.selectedRow()), u"table"_s);
    // Its text is not Vim's to change; dd takes the whole table.
    e.run(u"x"_s);
    QCOMPARE(e.text(), content);
    e.run(u"j"_s);
    QCOMPARE(e.page.editingRow(), before + 2);
    e.run(u"kdd"_s);
    QCOMPARE(e.text(), u"---\ntytuł: Próba\n---\n- przed\n\n- po\n"_s);
}

void TestEditorVim::blockIdsOnPut()
{
    // yy p: the copy has no id of its own (`((id))` must stay one block's); dd p is a move.
    Editor e(u"- blok ^abc123\n- inny\n"_s);
    e.at(0, 0);
    e.run(u"yyp"_s);
    QCOMPARE(e.text(), u"- blok ^abc123\n- blok\n- inny\n"_s);
    e.run(u"ggddjp"_s);
    QCOMPARE(e.text(), u"- blok\n- inny\n- blok ^abc123\n"_s);
}

void TestEditorVim::registersAcrossPages()
{
    Editor a(u"- z pierwszej strony\n"_s, u"A.md"_s);
    Editor b(u"- druga\n"_s, u"B.md"_s);
    a.at(0, 0);
    b.at(0, 0);
    a.run(u"\"qyy"_s);
    b.run(u"\"qp"_s);
    QCOMPARE(b.text(), u"- druga\n- z pierwszej strony\n"_s);
    // "+ is the system clipboard: the blocks' Markdown.
    a.run(u"\"+yy"_s);
    QCOMPARE(QGuiApplication::clipboard()->text(), u"- z pierwszej strony\n"_s);
    QGuiApplication::clipboard()->setText(u"- [ ] ze schowka\n"_s);
    b.run(u"gg\"+P"_s);
    QCOMPARE(b.text(), u"- [ ] ze schowka\n- druga\n- z pierwszej strony\n"_s);
}

void TestEditorVim::writeAndQuit()
{
    Editor e(u"- a\n"_s);
    e.at(0, 0);
    QSignalSpy quit(&e.vim, &VimSession::quitRequested);
    e.run(u"x:w<CR>"_s);
    QCOMPARE(e.host.file(Page), e.text());
    QVERIFY(!e.page.dirty());
    QCOMPARE(e.vim.message(), u"Saved"_s);
    e.run(u":q<CR>"_s);
    QCOMPARE(quit.count(), 1);
    QCOMPARE(quit.last().at(0).toBool(), false);
    e.run(u":q!<CR>"_s);
    QCOMPARE(quit.last().at(0).toBool(), true);
    e.run(u"ZZ"_s);
    QCOMPARE(quit.count(), 3);
    e.run(u":wat<CR>"_s);
    QCOMPARE(e.vim.message(), u"Not an editor command: wat"_s);
}

void TestEditorVim::emptyPage()
{
    Editor e(QString(), u"Nowa.md"_s);
    // A page of no blocks is one empty line; typing makes its first block.
    e.run(u"jkxdd"_s);
    QCOMPARE(e.text(), QString());
    e.run(u"iPierwsze słowo<Esc>"_s);
    QVERIFY(e.text().contains(u"Pierwsze słowo"_s));
    // dd of the only block leaves one empty block (Vim's one empty line).
    e.run(u"dd"_s);
    QCOMPARE(e.page.blocks()->rowCount(), 1);
    QCOMPARE(e.page.blocks()->editTextAt(0), QString());
}

void TestEditorVim::offChangesNothing()
{
    Editor e(u"- abc\n"_s);
    e.vim.setEnabled(false);
    e.at(0, 0);
    QVERIFY(!e.vim.key(Qt::Key_X, 0, u"x"_s));
    QVERIFY(!e.vim.key(Qt::Key_Escape, 0, QString()));
    QCOMPARE(e.vim.mode(), QString());
    QCOMPARE(e.vim.status(), QString());
    QVERIFY(!e.vim.commands());
    QVERIFY(!e.vim.typing());
    QCOMPARE(e.text(), u"- abc\n"_s);
}

void TestEditorVim::modesTold()
{
    Editor e(u"- abc\n"_s);
    e.at(0, 0);
    QSignalSpy state(&e.vim, &VimSession::stateChanged);
    QCOMPARE(e.vim.modeLabel(), u"NORMAL"_s);
    QVERIFY(e.vim.commands());
    e.run(u"i"_s);
    QCOMPARE(e.vim.mode(), u"insert"_s);
    QCOMPARE(e.vim.status(), u"-- INSERT --"_s);
    QVERIFY(e.vim.typing());
    QVERIFY(!e.vim.commands());
    e.run(u"<Esc>2d"_s);
    QCOMPARE(e.vim.pending(), u"2d"_s);
    QCOMPARE(e.vim.status(), u"NORMAL  2d"_s);
    e.run(u"<Esc>:s/a"_s);
    QCOMPARE(e.vim.commandLine(), u":s/a"_s);
    QCOMPARE(e.vim.status(), u":s/a"_s);
    e.run(u"<Esc>vl"_s);
    QCOMPARE(e.vim.status(), u"-- VISUAL --"_s);
    e.run(u"V"_s);
    QCOMPARE(e.vim.modeLabel(), u"VISUAL LINE"_s);
    e.run(u"<Esc>/zzz<CR>"_s);
    QCOMPARE(e.vim.message(), u"Not found: zzz"_s);
    QVERIFY(state.count() >= 8);
}

void TestEditorVim::adoptSelections()
{
    Editor e(u"- jeden\n- dwa\n- trzy\n"_s);
    // Blocks selected with the mouse or Shift+↓: Vim's keys act on them as a visual line selection.
    e.page.selectRange(0, 1);
    e.vim.adoptBlockSelection();
    QCOMPARE(e.vim.mode(), u"visualLine"_s);
    e.run(u"d"_s);
    QCOMPARE(e.text(), u"- trzy\n"_s);
    // A text selected with the mouse in a block.
    Editor f(u"- Zażółć gęślą\n"_s);
    f.at(0, 0);
    f.vim.adoptTextSelection(0, 7, 12);
    QCOMPARE(f.vim.mode(), u"visual"_s);
    f.run(u"d"_s);
    QCOMPARE(f.text(), u"- Zażółć \n"_s);
}

void TestEditorVim::sourceMode()
{
    Editor e(u"- jeden\n- dwa\n"_s);
    e.page.setSourceMode(true);
    QQmlEngine engine;
    QQmlComponent component(&engine);
    component.setData("import QtQuick\nTextEdit { textFormat: TextEdit.PlainText }", QUrl());
    std::unique_ptr<QObject> object(component.create());
    auto *edit = qobject_cast<QQuickItem *>(object.get());
    QVERIFY(edit);
    edit->setProperty("text", e.page.sourceText());
    e.vim.setSourceEdit(edit);
    const auto text = [&] { return edit->property("text").toString(); };
    const auto cursor = [&] { return edit->property("cursorPosition").toInt(); };
    e.vim.keys(u"dd"_s);
    QCOMPARE(text(), u"- dwa\n"_s);
    e.vim.keys(u"u"_s);
    QCOMPARE(text(), u"- jeden\n- dwa\n"_s);
    e.vim.keys(u"ggwcwżółw<Esc>"_s);
    QCOMPARE(text(), u"- żółw\n- dwa\n"_s);
    QCOMPARE(cursor(), 5);
    e.vim.keys(u"yyjp"_s);
    QCOMPARE(text(), u"- żółw\n- dwa\n- żółw\n"_s);
    e.vim.keys(u":%s/żółw/kot/g<CR>"_s);
    QCOMPARE(text(), u"- kot\n- dwa\n- kot\n"_s);
    e.vim.keys(u"ggVjd"_s);
    QCOMPARE(text(), u"- kot\n"_s);
    e.vim.keys(u">>"_s);
    QCOMPARE(text(), u"\t- kot\n"_s);
    e.vim.keys(u"A!<Esc>"_s);
    QCOMPARE(text(), u"\t- kot!\n"_s);
    // Visual mode shows the TextEdit's own selection.
    e.vim.keys(u"0vl"_s);
    QCOMPARE(edit->property("selectedText").toString(), u"\t-"_s);
    e.vim.keys(u"<Esc>"_s);
    QCOMPARE(edit->property("selectedText").toString(), QString());
    // The controller gets the text when it asks (SourceTextSync is the view's; here the
    // TextEdit's text is the source).
    e.page.setSourceText(text());
    e.page.setSourceMode(false);
    QCOMPARE(e.text(), u"\t- kot!\n"_s);
    // Out of source mode the keys are the blocks' again.
    e.at(0, 0);
    e.vim.keys(u"x"_s);
    QCOMPARE(e.text(), u"\t- ot!\n"_s);
}

void TestEditorVim::sourceModeUndo()
{
    Editor e(u"a\nb\n"_s);
    e.page.setSourceMode(true);
    QQmlEngine engine;
    QQmlComponent component(&engine);
    component.setData("import QtQuick\nTextEdit { textFormat: TextEdit.PlainText }", QUrl());
    std::unique_ptr<QObject> object(component.create());
    auto *edit = qobject_cast<QQuickItem *>(object.get());
    QVERIFY(edit);
    edit->setProperty("text", u"jeden dwa\ntrzy"_s);
    e.vim.setSourceEdit(edit);
    const auto text = [&] { return edit->property("text").toString(); };
    // A change and its typing are one step; adjacent typing of two changes stays two.
    e.vim.keys(u"cwraz<Esc>"_s);
    e.vim.keys(u"ax<Esc>"_s);
    QCOMPARE(text(), u"razx dwa\ntrzy"_s);
    e.vim.keys(u"u"_s);
    QCOMPARE(text(), u"raz dwa\ntrzy"_s);
    e.vim.keys(u"u"_s);
    QCOMPARE(text(), u"jeden dwa\ntrzy"_s);
    e.vim.keys(u"<C-r>"_s);
    QCOMPARE(text(), u"raz dwa\ntrzy"_s);
    e.vim.keys(u"<C-r>"_s);
    QCOMPARE(text(), u"razx dwa\ntrzy"_s);
    e.vim.keys(u"o4<CR>5<Esc>u"_s);
    QCOMPARE(text(), u"razx dwa\ntrzy"_s);
}

void TestEditorVim::normalModeKeyCost()
{
    // docs/PERFORMANCE.md "Budgets": a keystroke → frame under 16 ms (p95) on a 5000-block page.
    // A motion of Vim reads the page's lines once after a change, a key that only moves the caret
    // reads nothing again; insert mode costs a comparison.
    QString content;
    for (int i = 0; i < 5000; ++i)
        content += u"- Wiersz numer %1 z tekstem zażółć gęślą jaźń\n"_s.arg(i);
    Editor e(content);
    e.at(2500, 3);
    e.vim.keys(u"j"_s); // the lines are read
    QList<qint64> moves, edits, typing;
    for (int i = 0; i < 40; ++i) {
        test::CpuTimer t;
        t.start();
        e.vim.keys(i % 2 ? u"k"_s : u"j"_s);
        moves.append(t.nsecsElapsed());
        t.start();
        e.vim.keys(u"x"_s); // a change: the lines are read again at the next key
        edits.append(t.nsecsElapsed());
    }
    // The same edits as typing makes them (the editor's keystroke: setEditText) — x costs what a
    // keystroke in that block costs.
    QList<qint64> keystrokes;
    for (int i = 0; i < 40; ++i) {
        const int row = 2500 + (i % 2);
        const QString text = e.page.editTextOf(row);
        test::CpuTimer t;
        t.start();
        e.page.setEditText(row, text.mid(1), 0);
        keystrokes.append(t.nsecsElapsed());
    }
    e.vim.keys(u"i"_s);
    for (int i = 0; i < 40; ++i) {
        test::CpuTimer t;
        t.start();
        QVERIFY(!e.vim.key(Qt::Key_A, 0, u"a"_s));
        typing.append(t.nsecsElapsed());
    }
    e.vim.keys(u"<Esc>"_s);
    const qint64 slow = BRULION_TEST_SLOWDOWN;
    qInfo("5000 blocks — Vim key, median: move %.2f ms, x %.2f ms (a keystroke there %.2f ms); insert mode's "
          "own %.4f ms",
          double(test::medianOf(moves)) / 1e6, double(test::medianOf(edits)) / 1e6,
          double(test::medianOf(keystrokes)) / 1e6, double(test::medianOf(typing)) / 1e6);
    QVERIFY2(test::medianOf(edits) < 2 * test::medianOf(keystrokes) + 2'000'000 * slow,
             "x must cost what a keystroke costs");
    // (A Debug build: the budgets' numbers are a Release build's, docs/PERFORMANCE.md "Vim keys".)
    QVERIFY2(test::medianOf(moves) < 16'000'000 * slow, "a motion must fit into a frame");
    QVERIFY2(test::medianOf(edits) < 50'000'000 * slow, "an edit must cost no more than Enter");
    QVERIFY2(test::medianOf(typing) < 100'000 * slow, "insert mode must cost typing nothing");
}

QTEST_MAIN(TestEditorVim)
#include "tst_editor_vim.moc"
