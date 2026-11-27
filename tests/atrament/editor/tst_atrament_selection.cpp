// Block selection and moving blocks in the editor drawn by Atrament (src/app/editor/atrament/
// pageselection.cpp, pagedrag.cpp; docs/ATRAMENT.md "The block editor in Atrament (M2)", steps 3
// and 4) — the counterparts of the QML editor's selection self-test (qml/dev/SelectionSelfTest.qml,
// tst_editor_qml_selection*) on Atrament's headless test platform over FakeEditorHost: the page
// keeps the keyboard while blocks are selected — Esc from an edited block, Ctrl+Enter on a parent
// alone, Ctrl+Z keeping the selection, Shift+↓ jumping over the children, one band over the range,
// Ctrl+C copying Markdown, Esc clearing it, ↑/↓ and ↓ with nothing selected, Enter editing, Ctrl+.
// folding, Delete and Ctrl+Z byte for byte, Alt+↓ and Tab on a range, Ctrl+V over a selection,
// picked blocks with gaps neither indented nor moved, Shift+Insert / Shift+Delete / Ctrl+Insert,
// the host's part of each key (the QML editor's toasts), the page's edges; the band's geometry
// (also under a foreign row); a finger's long press dragged on, the grips dragged (also from a
// press at once inside the scrolling list), the view's edge scrolling the page while a finger or a
// mouse drags there, "One by one"; moving blocks by a bullet's drag (the drop line, one undo step)
// and by a long press on the selection.
#include "blocklistmodel.h"
#include "fakeeditorhost.h"
#include "foreignhost.h"
#include "pagecontroller.h"
#include "pageview.h"
#include "qthost.h"
#include "text/fonts.h"
#include "ui/testplatform.h"

#include <QClipboard>
#include <QGuiApplication>
#include <QQmlComponent>
#include <QQmlEngine>
#include <QQuickItem>
#include <QQuickWindow>
#include <QSGRendererInterface>
#include <QTest>

#include <chrono>
#include <memory>

using namespace brulion;
using namespace brulion::editor;
using namespace brulion::editor::atr;
using namespace Qt::StringLiterals;
using namespace std::chrono_literals;
namespace at = brulion::atrament;

namespace {

const QString PagePath = u"Notatki/Zaznaczanie.md"_s;

// The self-test's outline (EditorPlayground's renovation page, shortened): a parent with children,
// siblings after it.
const QString Outline = u"- Przygotowanie ^przyg\n"
                        "\t- [x] Zmierzyć ściany i podłogę\n"
                        "\t- [ ] Kupić płytki\n"
                        "\t- [/] Zadzwonić do hydraulika\n"
                        "- Prace\n"
                        "\t- [ ] Skuć stare płytki\n"
                        "\t- [ ] Położyć nowe\n"
                        "\t- [ ] Podlać fugę\n"
                        "- Notatki z rozmowy z ekipą\n"
                        "\t- Ekipa może zacząć 20 października\n"_s;

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

QString clipboardText()
{
    return QGuiApplication::clipboard()->text();
}

struct Harness {
    FakeEditorHost host;
    PageController controller;
    std::shared_ptr<at::TestPlatform> platform = std::make_shared<at::TestPlatform>();
    at::App app{platform, loadFonts()};
    at::TestWindow &tw;
    at::Entity<PageView> page;
    // What the host was asked (SelectionCommand names), and what it takes.
    QStringList commands;
    std::function<bool(PageView::SelectionCommand)> takes;

    explicit Harness(const QString &content, at::Size size = {600, 400}) : tw(platform->newWindow(size, 1))
    {
        host.setFile(PagePath, content);
        controller.setNow(u"2026-11-27 10:00"_s);
        controller.setSaveDelay(1000000);
        controller.setHost(&host);
        controller.setPath(PagePath);
        page = app.newEntity<PageView>(controller.blocks());
        page.update(app, [this](PageView &v, at::Context<PageView> &cx) {
            // Room for the bullets left of depth 0's text (BlockEditor's outline gutter).
            PageMetrics m = v.metrics();
            m.gutter = m.arrowColumn + m.bulletColumn;
            v.setStyle(m, v.palette());
            v.setController(&controller, cx);
            // The QML editor's Esc: editing stops, the block is selected (BlockEditor.stopEditing(true)).
            v.onEscape = [this] {
                const int row = controller.editingRow();
                controller.stopEditing();
                if (row >= 0)
                    controller.setSelectedRow(row);
            };
            v.onSelectionCommand = [this](PageView::SelectionCommand c, int) {
                commands.append(QString::fromStdU16String(std::u16string(PageView::commandName(c))));
                return takes && takes(c);
            };
            // A long press: the touch selection mode with the block selected (BlockEditor.longPressText).
            v.onLongPressRow = [this](int row) {
                if (controller.selectionCount() > 0)
                    controller.extendSelectionTo(row);
                else
                    controller.selectRange(row, row);
            };
        });
        app.openWindow(tw, page);
        tw.settle();
    }
    BlockListModel *model() const { return controller.blocks(); }
    const PageView &view() { return page.read(app); }
    int rowOf(const QString &prefix) const
    {
        for (int r = 0; r < model()->rowCount(); ++r)
            if (model()->editTextAt(r).startsWith(prefix))
                return r;
        return -1;
    }
    QString edge(int row) const { return model()->index(row).data(BlockListModel::SelectionEdgeRole).toString(); }
    QString status(int row) const { return model()->index(row).data(BlockListModel::TaskStatusRole).toString(); }
    int depth(int row) const { return model()->index(row).data(BlockListModel::DepthRole).toInt(); }
    QList<int> selected() const
    {
        QList<int> rows;
        for (const QVariant &v : controller.selectedRows())
            rows.append(v.toInt());
        return rows;
    }
    bool key(std::u16string_view keys) { return tw.simulateKeystrokes(keys); }
    // The middle of the caret before display offset `offset` of drawn row `row`.
    at::Point pointOf(int row, int offset)
    {
        const at::TextLayoutHandle *t = view().rowText(row);
        if (!t || !t->layout())
            return {-1, -1};
        const at::Rect caret = t->layout()->caretRect(offset).translated(t->bounds().origin);
        return {caret.left() + 0.5f, caret.top() + caret.height() / 2};
    }
    at::Rect bounds(int row) { return view().rowBounds(row).value_or(at::Rect{}); }
    // The bullet of an outline's list item (left of its text, in the gutter).
    at::Point bulletOf(int row)
    {
        const PageMetrics &m = view().metrics();
        const at::Rect b = bounds(row);
        const float textX = m.gutter + float(depth(row)) * m.indentStep;
        return {textX - m.bulletColumn / 2, b.top() + m.rowPad + m.lineHeight / 2};
    }
    void edit(int row, int offset)
    {
        tw.simulateClick(pointOf(row, offset));
        tw.settle();
    }
    void touchSelection(bool grips, bool picking = false)
    {
        page.update(app, [&](PageView &v, at::Context<PageView> &cx) { v.setTouchSelection(grips, picking, cx); });
        tw.settle();
    }
    // A finger held on `p` past the long-press time.
    void longPress(at::Point p)
    {
        tw.simulateTouchDown(p);
        tw.advance(600ms);
    }
};

// What AtramentView does for "page": the host places its items after every frame.
class PageHostItem : public at::AtramentItem {
public:
    using AtramentItem::AtramentItem;
    std::unique_ptr<ForeignRowHost> host;
    void frame()
    {
        requestFrame();
        updatePolish();
    }

protected:
    void placeHostedChildren() override
    {
        if (host)
            host->place();
    }
};

// A hosted row's delegate: a MouseArea over all of it (it would take any press).
constexpr const char *kDelegate = R"(
import QtQuick
Item {
    id: d
    required property int index
    required property string view
    property var model
    property int presses: 0
    width: 600
    height: 120
    MouseArea {
        anchors.fill: parent
        onPressed: d.presses++
    }
}
)";

} // namespace

class TestAtramentSelection : public QObject {
    Q_OBJECT
private Q_SLOTS:
    void escapeThenSelectionKeys();
    void arrowsMoveTheSelection();
    void enterEditsAndFoldKey();
    void deleteAndUndoByteForByte();
    void rangeMovedAndIndented();
    void pasteOverTheSelection();
    void pickedBlocksWithGaps();
    void insertAndDeleteKeys();
    void hostTakesItsCommands();
    void pageEdgesAskTheHost();
    void keysWhileEditingAreTheFields();
    void bandGeometry();
    void bandUnderAForeignRow();
    void gripOverAHostedRow();
    void longPressDraggedExtends();
    void gripsDragTheEnds();
    void pickingLongPressDoesNotDrag();
    void fingerAtTheEdgeScrolls();
    void mouseDragAtTheEdgeScrolls();
    void bulletDragMovesBlocks();
    void bulletClickStillZooms();
    void longPressOnSelectionMoves();
    void moveSiblingsOnly();
};

void TestAtramentSelection::escapeThenSelectionKeys()
{
    Harness h(Outline);
    const int a = h.rowOf(u"Przygotowanie"_s);
    h.edit(a, 3);
    QCOMPARE(h.controller.editingRow(), a);
    // Esc: the block selected (with its children), the keys the page's.
    QVERIFY(h.key(u"escape"));
    QCOMPARE(h.controller.editingRow(), -1);
    QCOMPARE(h.controller.selectedRow(), a);
    QCOMPARE(h.selected().size(), 4);
    // Ctrl+Enter on a parent selected alone changes it, not the children it brings along.
    QVERIFY(h.key(u"ctrl-enter"));
    QCOMPARE(h.status(a), u"todo"_s);
    QCOMPARE(h.status(h.rowOf(u"Zmierzyć"_s)), u"done"_s);
    QCOMPARE(h.status(h.rowOf(u"Zadzwonić"_s)), u"doing"_s);
    // Ctrl+Z: undone, still selected.
    QVERIFY(h.key(u"ctrl-z"));
    QCOMPARE(h.status(a), QString());
    QCOMPARE(h.controller.selectedRow(), a);
    QCOMPARE(h.selected().size(), 4);
    // Shift+↓ jumps over the selected children to the next block; again.
    QVERIFY(h.key(u"shift-down"));
    QCOMPARE(h.controller.selectedRow(), h.rowOf(u"Prace"_s));
    QCOMPARE(h.controller.selectionAnchorRow(), a);
    QVERIFY(h.key(u"shift-down"));
    QCOMPARE(h.controller.selectedRow(), h.rowOf(u"Notatki z rozmowy"_s));
    h.tw.settle();
    QCOMPARE(h.edge(a), u"first"_s);
    QCOMPARE(h.edge(h.rowOf(u"Prace"_s)), u"middle"_s);
    QCOMPARE(h.edge(h.rowOf(u"Ekipa może"_s)), u"last"_s);
    // Ctrl+C: the three blocks with their children as Markdown.
    QGuiApplication::clipboard()->setText(u"wcześniej"_s);
    QVERIFY(h.key(u"ctrl-c"));
    QCOMPARE(clipboardText(), QString(Outline).chopped(1));
    QCOMPARE(h.commands.count(u"copy"_s), 1); // the host was asked first (its toast)
    // Shift+↑ shrinks it again (to the row above: the last of Prace's children).
    QVERIFY(h.key(u"shift-up"));
    QCOMPARE(h.controller.selectedRow(), h.rowOf(u"Podlać"_s));
    QCOMPARE(h.edge(h.rowOf(u"Notatki z rozmowy"_s)), QString());
    QVERIFY(h.commands.contains(u"extended"_s));
    // Esc clears it.
    QVERIFY(h.key(u"escape"));
    QCOMPARE(h.controller.selectionCount(), 0);
    QCOMPARE(h.controller.selectedRow(), -1);
}

void TestAtramentSelection::arrowsMoveTheSelection()
{
    Harness h(u"- jeden\n- dwa\n- trzy\n"_s);
    // Nothing selected: ↓ selects the first block, ↑ the last.
    QVERIFY(h.key(u"down"));
    QCOMPARE(h.controller.selectedRow(), 0);
    QVERIFY(h.key(u"down"));
    QCOMPARE(h.controller.selectedRow(), 1);
    QVERIFY(h.key(u"up"));
    QVERIFY(h.key(u"up"));
    QCOMPARE(h.controller.selectedRow(), 0); // (the page's first block: it stays)
    QVERIFY(h.key(u"escape"));
    QVERIFY(h.key(u"up"));
    QCOMPARE(h.controller.selectedRow(), 2);
    // A key the selection has no meaning for goes on (to the QML around).
    QVERIFY(!h.key(u"f5"));
    QVERIFY(!h.key(u"ctrl-shift-a"));
}

void TestAtramentSelection::enterEditsAndFoldKey()
{
    Harness h(u"- rodzic gęślą\n\t- dziecko\n- sąsiad\n"_s);
    h.controller.setSelectedRow(0);
    h.tw.settle();
    // Ctrl+. folds the selected block (its child hidden), again unfolds it.
    QVERIFY(h.key(u"ctrl-."));
    QVERIFY(h.model()->isCollapsed(0));
    QVERIFY(h.key(u"ctrl-."));
    QVERIFY(!h.model()->isCollapsed(0));
    // Enter edits it, the caret at its end; typing goes on there.
    QVERIFY(h.key(u"enter"));
    QCOMPARE(h.controller.editingRow(), 0);
    QCOMPARE(h.view().editedRow(), 0);
    h.tw.simulateTyping(u"ż");
    QCOMPARE(h.model()->editTextAt(0), u"rodzic gęśląż"_s);
}

void TestAtramentSelection::deleteAndUndoByteForByte()
{
    Harness h(Outline);
    const QString before = h.controller.sourceText();
    const int from = h.rowOf(u"Zmierzyć"_s);
    const int to = h.rowOf(u"Zadzwonić"_s);
    h.controller.selectRange(from, to);
    h.tw.settle();
    QVERIFY(h.key(u"delete"));
    QVERIFY(!h.controller.sourceText().contains(u"Zadzwonić do hydraulika"_s));
    // The block before them is selected.
    QCOMPARE(h.controller.selectedRow(), h.rowOf(u"Przygotowanie"_s));
    // Ctrl+Z brings them back byte for byte, selected again.
    QVERIFY(h.key(u"ctrl-z"));
    QCOMPARE(h.controller.sourceText(), before);
    QCOMPARE(h.controller.selectionAnchorRow(), from);
    QCOMPARE(h.controller.selectedRow(), to);
    // Backspace deletes too.
    QVERIFY(h.key(u"backspace"));
    QVERIFY(!h.controller.sourceText().contains(u"Kupić płytki"_s));
}

void TestAtramentSelection::rangeMovedAndIndented()
{
    Harness h(Outline);
    const QString before = h.controller.sourceText();
    h.controller.selectRange(h.rowOf(u"Skuć"_s), h.rowOf(u"Położyć"_s));
    h.tw.settle();
    // Alt+↓: the range below its next sibling.
    QVERIFY(h.key(u"alt-down"));
    QVERIFY(h.rowOf(u"Podlać"_s) < h.rowOf(u"Skuć"_s));
    QCOMPARE(h.rowOf(u"Skuć"_s) + 1, h.rowOf(u"Położyć"_s));
    QCOMPARE(h.controller.selectionAnchorRow(), h.rowOf(u"Skuć"_s));
    QCOMPARE(h.controller.selectedRow(), h.rowOf(u"Położyć"_s));
    // Tab indents the range, Shift+Tab outdents it again.
    QVERIFY(h.key(u"tab"));
    QCOMPARE(h.depth(h.rowOf(u"Skuć"_s)), 2);
    QCOMPARE(h.depth(h.rowOf(u"Położyć"_s)), 2);
    QVERIFY(h.key(u"shift-tab"));
    QCOMPARE(h.depth(h.rowOf(u"Skuć"_s)), 1);
    // Undone step by step to the start; Alt+↑ moves it back too.
    QVERIFY(h.key(u"ctrl-z"));
    QVERIFY(h.key(u"ctrl-z"));
    QVERIFY(h.key(u"ctrl-z"));
    QCOMPARE(h.controller.sourceText(), before);
    QVERIFY(h.key(u"alt-down"));
    QVERIFY(h.key(u"alt-up"));
    QCOMPARE(h.controller.sourceText(), before);
    QVERIFY(h.commands.contains(u"moveDown"_s) && h.commands.contains(u"indent"_s));
}

void TestAtramentSelection::pasteOverTheSelection()
{
    Harness h(Outline);
    const int skuc = h.rowOf(u"Skuć"_s);
    h.controller.selectRange(skuc, skuc);
    h.tw.settle();
    QVERIFY(h.key(u"ctrl-c"));
    QCOMPARE(clipboardText(), u"- [ ] Skuć stare płytki"_s);
    h.controller.setSelectedRow(h.rowOf(u"Położyć"_s));
    h.tw.settle();
    QVERIFY(h.key(u"ctrl-v"));
    QCOMPARE(h.rowOf(u"Położyć"_s), -1);
    QCOMPARE(h.controller.sourceText().count(u"Skuć stare płytki"_s), 2);
}

void TestAtramentSelection::pickedBlocksWithGaps()
{
    Harness h(Outline);
    h.controller.toggleSelected(h.rowOf(u"Zmierzyć"_s));
    h.controller.toggleSelected(h.rowOf(u"Zadzwonić"_s));
    h.controller.toggleSelected(h.rowOf(u"Skuć"_s));
    h.tw.settle();
    QVERIFY(h.controller.selectionPicked() && !h.controller.selectionContiguous());
    QCOMPARE(h.edge(h.rowOf(u"Zmierzyć"_s)), u"single"_s);
    QCOMPARE(h.edge(h.rowOf(u"Kupić"_s)), QString());
    // Ctrl+C: the picked blocks in their order.
    QVERIFY(h.key(u"ctrl-c"));
    QCOMPARE(clipboardText(),
             u"- [x] Zmierzyć ściany i podłogę\n- [/] Zadzwonić do hydraulika\n- [ ] Skuć stare płytki"_s);
    // Tab and Alt+↑ move nothing (the host says why: the QML editor's gap toast).
    const QString before = h.controller.sourceText();
    QVERIFY(h.key(u"tab"));
    QVERIFY(h.key(u"alt-up"));
    QCOMPARE(h.controller.sourceText(), before);
    QVERIFY(h.commands.contains(u"indent"_s) && h.commands.contains(u"moveUp"_s));
}

void TestAtramentSelection::insertAndDeleteKeys()
{
    Harness h(u"- jeden\n- dwa\n- trzy\n"_s);
    h.controller.selectRange(0, 0);
    h.tw.settle();
    // Ctrl+Insert copies, Shift+Insert pastes over, Shift+Delete cuts.
    QVERIFY(h.key(u"ctrl-insert"));
    QCOMPARE(clipboardText(), u"- jeden"_s);
    h.controller.selectRange(2, 2);
    h.tw.settle();
    QVERIFY(h.key(u"shift-insert"));
    QCOMPARE(h.controller.sourceText(), u"- jeden\n- dwa\n- jeden\n"_s);
    h.controller.selectRange(1, 1);
    h.tw.settle();
    QVERIFY(h.key(u"shift-delete"));
    QCOMPARE(clipboardText(), u"- dwa"_s);
    QCOMPARE(h.controller.sourceText(), u"- jeden\n- jeden\n"_s);
}

void TestAtramentSelection::hostTakesItsCommands()
{
    Harness h(u"- jeden\n- dwa\n"_s);
    // The host takes copies (its toast, its clipboard), Delete and Esc: the view does nothing more.
    h.takes = [](PageView::SelectionCommand c) {
        return c == PageView::SelectionCommand::Copy || c == PageView::SelectionCommand::Delete
               || c == PageView::SelectionCommand::Escape || c == PageView::SelectionCommand::Edit;
    };
    h.controller.selectRange(0, 1);
    h.tw.settle();
    QGuiApplication::clipboard()->setText(u"nic"_s);
    QVERIFY(h.key(u"ctrl-c"));
    QCOMPARE(clipboardText(), u"nic"_s);
    QVERIFY(h.key(u"delete"));
    QCOMPARE(h.model()->rowCount(), 2);
    QVERIFY(h.key(u"escape"));
    QCOMPARE(h.controller.selectionCount(), 2);
    QVERIFY(h.key(u"enter"));
    QCOMPARE(h.controller.editingRow(), -1);
    // Menus and modes are the host's alone: unasked for, nothing happens and the key goes on.
    h.takes = {};
    QVERIFY(!h.key(u"ctrl-e"));
    QVERIFY(!h.key(u"ctrl-shift-c"));
    QVERIFY(h.key(u"ctrl-shift-enter"));
    QCOMPARE(h.commands, (QStringList{u"copy"_s, u"delete"_s, u"escape"_s, u"edit"_s, u"sourceMode"_s,
                                      u"copyReference"_s, u"taskMenu"_s}));
    // Android's Back: the host's (it leaves the touch selection mode).
    h.takes = [](PageView::SelectionCommand c) { return c == PageView::SelectionCommand::Back; };
    QVERIFY(h.key(u"back"));
}

void TestAtramentSelection::pageEdgesAskTheHost()
{
    Harness h(u"- jeden\n- dwa\n"_s);
    h.controller.selectRange(0, 0);
    h.tw.settle();
    // Shift+↑ at the page's first block: the host's (a journal day: the day above).
    QVERIFY(h.key(u"shift-up"));
    QVERIFY(h.commands.contains(u"extendPastTop"_s));
    QVERIFY(h.key(u"shift-down"));
    QVERIFY(h.key(u"shift-down"));
    QVERIFY(h.commands.contains(u"extendPastBottom"_s));
    QCOMPARE(h.controller.selectionCount(), 2);
    // A selection over several days (the host does it its way): the view leaves it.
    h.takes = [](PageView::SelectionCommand c) { return c == PageView::SelectionCommand::ExtendUp; };
    QVERIFY(h.key(u"shift-up"));
    QCOMPARE(h.controller.selectedRow(), 1);
}

void TestAtramentSelection::keysWhileEditingAreTheFields()
{
    Harness h(u"- jeden\n- dwa\n"_s);
    h.edit(1, 2);
    // ↓ in the page's last block is the field's (its caret to the end), nothing selected.
    h.key(u"down");
    QCOMPARE(h.controller.editingRow(), 1);
    QCOMPARE(h.controller.selectionCount(), 0);
    QVERIFY(!h.key(u"f5"));
    QCOMPARE(h.controller.editingRow(), 1);
}

void TestAtramentSelection::bandGeometry()
{
    Harness h(u"- jeden\n\t- dwa\n- trzy\n"_s);
    const at::Pixel ground = h.tw.pixel(300, int(h.bounds(1).top() + 6));
    // A block alone: framed from its own level (BlockDelegate's ownBandX: its bullet column).
    h.controller.selectRange(1, 1);
    h.tw.settle();
    QCOMPARE(h.edge(1), u"single"_s);
    const PageMetrics &m = h.view().metrics();
    const float own = m.gutter + m.indentStep - m.bulletColumn - 2;
    const int y = int(h.bounds(1).top() + h.bounds(1).height() / 2);
    auto isBand = [&](int x) {
        const at::Pixel p = h.tw.pixel(x, y);
        return p.r != ground.r || p.g != ground.g || p.b != ground.b;
    };
    QVERIFY(isBand(int(own) + 4));
    QVERIFY(!isBand(int(own) - 4));
    QVERIFY(isBand(595)); // to the column's right edge
    // A range: one straight band from the column's edge over every level.
    h.controller.selectRange(0, 2);
    h.tw.settle();
    QCOMPARE(h.edge(1), u"middle"_s);
    const float column = m.gutter - m.bulletColumn - 2;
    QVERIFY(isBand(int(column) + 4));
    // The selection's place for its tools: from the first row's top to the last one's bottom.
    const std::optional<at::Rect> band = h.view().selectionBounds();
    QVERIFY(band);
    QCOMPARE(band->top(), h.bounds(0).top());
    QCOMPARE(band->bottom(), h.bounds(2).bottom());
    h.controller.clearSelection();
    h.tw.settle();
    QVERIFY(!h.view().selectionBounds());
}

void TestAtramentSelection::bandUnderAForeignRow()
{
    // A code block is a foreign row (its QML delegate over it): the band is the page's, under it.
    Harness h(u"- jeden\n\n```\nkod\n```\n\n- dwa\n"_s);
    const int code = 1;
    QCOMPARE(h.model()->index(code).data(BlockListModel::ViewRole).toString(), u"code"_s);
    const std::vector<std::size_t> foreign = h.view().foreignRows();
    QVERIFY(std::find(foreign.begin(), foreign.end(), std::size_t(code)) != foreign.end());
    const at::Rect box = h.bounds(code);
    const int y = int(box.top() + box.height() / 2);
    const at::Pixel ground = h.tw.pixel(300, y);
    h.controller.selectRange(code, code);
    h.tw.settle();
    const at::Pixel band = h.tw.pixel(300, y);
    QVERIFY(band.r != ground.r || band.g != ground.g || band.b != ground.b);
    // The grips over it in the touch selection mode, which a finger drags at once (a delegate
    // over the row does not get that press: qthost.h "Hosted children").
    h.touchSelection(true);
    const at::Point endGrip{588, box.bottom() - 10};
    h.tw.simulateTouchDown(endGrip);
    h.tw.simulateTouchMove({588, h.bounds(2).top() + 8});
    h.tw.simulateTouchUp({588, h.bounds(2).top() + 8});
    QCOMPARE(h.controller.selectionFirstRow(), code);
    QCOMPARE(h.controller.selectionLastRow(), 2);
}

void TestAtramentSelection::gripOverAHostedRow()
{
    // A real offscreen Qt Quick window: the code block's QML delegate over its row takes any press,
    // but not one on the selection's grip there — the page drags it at once (qthost.h "Hosted
    // children", Window::claimTouchDrag).
    QQuickWindow::setGraphicsApi(QSGRendererInterface::Software);
    qputenv("QML_NO_TOUCH_COMPRESSION", "1");
    auto platform = std::make_shared<at::QtPlatform>();
    at::App app{platform, loadFonts()};
    QQmlEngine engine;
    QQuickWindow window;
    window.resize(600, 400);
    FakeEditorHost host;
    PageController controller;
    host.setFile(PagePath, u"- jeden\n\n```\nkod gęsi\n```\n\n- dwa\n- trzy\n"_s);
    controller.setSaveDelay(1000000);
    controller.setHost(&host);
    controller.setPath(PagePath);
    at::Entity<PageView> page = app.newEntity<PageView>(controller.blocks());
    page.update(app, [&](PageView &v, at::Context<PageView> &cx) { v.setController(&controller, cx); });
    auto *item = new PageHostItem(window.contentItem());
    item->setSize(QSizeF(600, 400));
    QQmlComponent delegate(&engine);
    delegate.setData(kDelegate, QUrl());
    QVERIFY2(delegate.isReady(), qPrintable(delegate.errorString()));
    item->host = std::make_unique<ForeignRowHost>(item, app, page, controller.blocks());
    item->host->setDelegate(&delegate);
    item->attach(app, at::AnyView{page.id(), &at::renderView<PageView>});
    window.show();
    auto settle = [&] {
        for (int i = 0; i < 8; ++i)
            item->frame();
        QCoreApplication::processEvents();
    };
    settle();
    const int code = 1;
    QQuickItem *hosted = item->host->rowItem(code);
    QVERIFY(hosted);
    controller.selectRange(code, code);
    page.update(app, [](PageView &v, at::Context<PageView> &cx) { v.setTouchSelection(true, false, cx); });
    settle();
    const at::Rect box = page.read(app).rowBounds(code).value_or(at::Rect{});
    QCOMPARE(qreal(box.top()), hosted->y());
    static QPointingDevice *finger = QTest::createTouchDevice();
    const QPoint grip(588, int(box.bottom() - 10));
    QTest::touchEvent(&window, finger).press(0, grip);
    settle();
    QCOMPARE(page.read(app).dragKind(), PageView::DragKind::Extend);
    const int below = int(page.read(app).rowBounds(3).value_or(at::Rect{}).top()) + 8;
    for (int y = grip.y() + 10; y <= below; y += 10) {
        QTest::touchEvent(&window, finger).move(0, QPoint(588, y));
        settle();
    }
    QTest::touchEvent(&window, finger).release(0, QPoint(588, below));
    settle();
    QCOMPARE(controller.selectionFirstRow(), code);
    QCOMPARE(controller.selectionLastRow(), 3);
    QCOMPARE(hosted->property("presses").toInt(), 0);
    // Beside the grip a press is the delegate's, as before.
    QTest::touchEvent(&window, finger).press(0, QPoint(300, int(box.top() + 30)));
    QTest::touchEvent(&window, finger).release(0, QPoint(300, int(box.top() + 30)));
    settle();
    QCOMPARE(hosted->property("presses").toInt(), 1);
    delete item;
}

void TestAtramentSelection::longPressDraggedExtends()
{
    Harness h(u"- jeden\n- dwa\n- trzy\n- cztery\n- pięć\n"_s);
    h.longPress(h.pointOf(1, 1));
    QCOMPARE(h.controller.selectedRow(), 1);
    QCOMPARE(h.view().dragKind(), PageView::DragKind::Extend);
    // The finger moving on extends the selection from the block held (no scroll, nothing edited).
    h.tw.simulateTouchMove(h.pointOf(2, 1));
    h.tw.simulateTouchMove(h.pointOf(3, 2));
    QCOMPARE(h.controller.selectionFirstRow(), 1);
    QCOMPARE(h.controller.selectionLastRow(), 3);
    // Back up past it: the other way.
    h.tw.simulateTouchMove(h.pointOf(0, 1));
    QCOMPARE(h.controller.selectionFirstRow(), 0);
    QCOMPARE(h.controller.selectionLastRow(), 1);
    h.tw.simulateTouchUp(h.pointOf(0, 1));
    QCOMPARE(h.view().dragKind(), PageView::DragKind::None);
    QCOMPARE(h.controller.editingRow(), -1);
    QCOMPARE(h.controller.selectionCount(), 2);
}

void TestAtramentSelection::gripsDragTheEnds()
{
    // (A page longer than the view: the list could scroll under the grips.)
    QString text;
    for (int i = 1; i <= 25; ++i)
        text += u"- punkt %1 żółw\n"_s.arg(i);
    Harness h(text);
    h.controller.selectRange(1, 2);
    h.touchSelection(true);
    // The end grip at the band's bottom right: a finger's press there drags it at once (the list
    // could scroll: it does not), the selection's end follows.
    const at::Point end{588, h.bounds(2).bottom() - 10};
    h.tw.simulateTouchDown(end);
    QCOMPARE(h.view().dragKind(), PageView::DragKind::Extend);
    h.tw.simulateTouchMove({588, h.bounds(3).top() + 10});
    h.tw.simulateTouchMove({588, h.bounds(4).top() + 10});
    h.tw.simulateTouchUp({588, h.bounds(4).top() + 10});
    QCOMPARE(h.controller.selectionFirstRow(), 1);
    QCOMPARE(h.controller.selectionLastRow(), 4);
    QCOMPARE(h.view().handle().scroll().offset.y, 0.0f);
    // The start grip at its top left: dragged up.
    const PageMetrics &m = h.view().metrics();
    const float bandX = m.gutter - m.bulletColumn - 2;
    const at::Point start{std::max(bandX, 12.0f), h.bounds(1).top() + 10};
    h.tw.simulateTouchDown(start);
    h.tw.simulateTouchMove({start.x, h.bounds(0).top() + 5});
    h.tw.simulateTouchUp({start.x, h.bounds(0).top() + 5});
    QCOMPARE(h.controller.selectionFirstRow(), 0);
    QCOMPARE(h.controller.selectionLastRow(), 4);
    // Without the touch selection mode there are none: a press there is the row's.
    h.touchSelection(false);
    h.tw.simulateTouchDown({588, h.bounds(4).bottom() - 10});
    QCOMPARE(h.view().dragKind(), PageView::DragKind::None);
    h.tw.simulateTouchUp({588, h.bounds(4).bottom() - 10});
}

void TestAtramentSelection::pickingLongPressDoesNotDrag()
{
    Harness h(u"- jeden\n- dwa\n- trzy\n"_s);
    h.touchSelection(false, true);
    h.longPress(h.pointOf(0, 1));
    QCOMPARE(h.controller.selectedRow(), 0);
    QCOMPARE(h.view().dragKind(), PageView::DragKind::None);
    h.tw.simulateTouchMove(h.pointOf(2, 1));
    QCOMPARE(h.controller.selectionCount(), 1);
    h.tw.simulateTouchUp(h.pointOf(2, 1));
}

void TestAtramentSelection::fingerAtTheEdgeScrolls()
{
    QString page;
    for (int i = 1; i <= 60; ++i)
        page += u"- punkt %1 źdźbło\n"_s.arg(i);
    Harness h(page);
    h.longPress(h.pointOf(2, 2));
    QCOMPARE(h.controller.selectedRow(), 2);
    // Held at the view's bottom edge: the page scrolls, the selection's end follows the rows that
    // come under the finger.
    h.tw.simulateTouchMove({200, 396});
    const int lastBefore = h.controller.selectionLastRow();
    QVERIFY(h.view().dragScrollSpeed() > 0);
    h.tw.advance(500ms);
    QVERIFY2(h.view().handle().scroll().offset.y > 200, qPrintable(QString::number(h.view().handle().scroll().offset.y)));
    QVERIFY2(h.controller.selectionLastRow() > lastBefore + 5, qPrintable(QString::number(h.controller.selectionLastRow())));
    QCOMPARE(h.controller.selectionFirstRow(), 2);
    // Back in the middle: it stops.
    h.tw.simulateTouchMove({200, 200});
    QCOMPARE(h.view().dragScrollSpeed(), 0.0f);
    const float offset = h.view().handle().scroll().offset.y;
    h.tw.advance(200ms);
    QCOMPARE(h.view().handle().scroll().offset.y, offset);
    h.tw.simulateTouchUp({200, 200});
    // Released: no more scrolling, nothing edited.
    QCOMPARE(h.view().dragKind(), PageView::DragKind::None);
    QCOMPARE(h.controller.editingRow(), -1);
}

void TestAtramentSelection::mouseDragAtTheEdgeScrolls()
{
    QString page;
    for (int i = 1; i <= 60; ++i)
        page += u"- wiersz %1 gęś\n"_s.arg(i);
    Harness h(page);
    const at::Point from = h.pointOf(1, 2);
    h.tw.simulateMouseMove(from);
    h.tw.simulateMouseDown(from);
    h.tw.simulateMouseMove(h.pointOf(2, 2), at::MouseButton::Left);
    QCOMPARE(h.controller.selectionCount(), 2);
    // Dragged to the bottom edge (and past it, out of the window): the list scrolls, the
    // selection follows.
    h.tw.simulateMouseMove({200, 420}, at::MouseButton::Left);
    h.tw.advance(400ms);
    QVERIFY(h.view().handle().scroll().offset.y > 200);
    QVERIFY(h.controller.selectionLastRow() > 10);
    QCOMPARE(h.controller.selectionFirstRow(), 1);
    h.tw.simulateMouseUp({200, 420});
    QCOMPARE(h.view().dragScrollSpeed(), 0.0f);
    QCOMPARE(h.controller.editingRow(), -1);
    // The next click edits again.
    const int middle = h.view().rowAtY(200);
    h.tw.simulateClick(h.pointOf(middle, 1));
    QCOMPARE(h.controller.editingRow(), middle);
}

void TestAtramentSelection::bulletDragMovesBlocks()
{
    Harness h(u"- jeden\n- dwa\n- trzy\n- cztery\n"_s);
    const QString before = h.controller.sourceText();
    const at::Point bullet = h.bulletOf(0);
    h.tw.simulateMouseMove(bullet);
    h.tw.simulateMouseDown(bullet);
    // A few px: not yet a drag.
    h.tw.simulateMouseMove({bullet.x + 2, bullet.y + 1}, at::MouseButton::Left);
    QCOMPARE(h.view().dragKind(), PageView::DragKind::None);
    // Down to the gap between "trzy" and "cztery": the block selected, the drop line there.
    h.tw.simulateMouseMove({bullet.x, h.bounds(3).top() + 2}, at::MouseButton::Left);
    QCOMPARE(h.view().dragKind(), PageView::DragKind::Move);
    QCOMPARE(h.controller.selectedRow(), 0);
    std::optional<PageView::DropTarget> drop = h.view().dropTarget();
    QVERIFY(drop);
    QCOMPARE(drop->sibling, 3);
    QVERIFY(!drop->after);
    QCOMPARE(drop->line.top() + 1, h.bounds(3).top());
    // Over its own place: nowhere to go.
    h.tw.simulateMouseMove({bullet.x, h.bounds(0).bottom() - 2}, at::MouseButton::Left);
    QVERIFY(!h.view().dropTarget());
    // Below the last one: after it.
    h.tw.simulateMouseMove({bullet.x, h.bounds(3).bottom() + 20}, at::MouseButton::Left);
    drop = h.view().dropTarget();
    QVERIFY(drop && drop->sibling == 3 && drop->after);
    h.tw.simulateMouseMove({bullet.x, h.bounds(3).top() + 2}, at::MouseButton::Left);
    h.tw.simulateMouseUp({bullet.x, h.bounds(3).top() + 2});
    QCOMPARE(h.controller.sourceText(), u"- dwa\n- trzy\n- jeden\n- cztery\n"_s);
    QCOMPARE(h.controller.selectedRow(), 2); // still selected, where it went
    QCOMPARE(h.controller.editingRow(), -1); // the release clicked nothing
    // One undo step.
    QVERIFY(h.key(u"ctrl-z"));
    QCOMPARE(h.controller.sourceText(), before);
}

void TestAtramentSelection::bulletClickStillZooms()
{
    Harness h(u"- rodzic\n\t- dziecko\n- sąsiad\n"_s);
    int zoomed = -1;
    h.page.update(h.app, [&](PageView &v, at::Context<PageView> &cx) {
        v.onBulletClick = [&](int row) { zoomed = row; };
        cx.notify();
    });
    h.tw.settle();
    h.tw.simulateClick(h.bulletOf(0));
    QCOMPARE(zoomed, 0);
    QCOMPARE(h.view().dragKind(), PageView::DragKind::None);
    QCOMPARE(h.controller.selectionCount(), 0);
}

void TestAtramentSelection::longPressOnSelectionMoves()
{
    Harness h(u"- jeden\n- dwa\n- trzy\n- cztery\n"_s);
    h.controller.selectRange(0, 1);
    h.touchSelection(true);
    // A finger held on a selected block while its grips show: it moves them.
    h.longPress(h.pointOf(1, 1));
    QCOMPARE(h.view().dragKind(), PageView::DragKind::Move);
    h.tw.simulateTouchMove(h.pointOf(2, 1));
    h.tw.simulateTouchMove({200, h.bounds(3).bottom() + 30});
    const std::optional<PageView::DropTarget> drop = h.view().dropTarget();
    QVERIFY(drop && drop->sibling == 3 && drop->after);
    h.tw.simulateTouchUp({200, h.bounds(3).bottom() + 30});
    QCOMPARE(h.controller.sourceText(), u"- trzy\n- cztery\n- jeden\n- dwa\n"_s);
    QCOMPARE(h.controller.selectionFirstRow(), 2);
    QCOMPARE(h.controller.selectionLastRow(), 3);
    QCOMPARE(h.controller.editingRow(), -1);
}

void TestAtramentSelection::moveSiblingsOnly()
{
    // PageController::moveSelectionNextTo: among the range's siblings only, its children along.
    Harness h(Outline);
    h.controller.selectRange(h.rowOf(u"Skuć"_s), h.rowOf(u"Skuć"_s));
    QVERIFY(!h.controller.moveSelectionNextTo(h.rowOf(u"Przygotowanie"_s), false)); // not a sibling
    QVERIFY(!h.controller.moveSelectionNextTo(h.rowOf(u"Położyć"_s), false));       // where it is
    QVERIFY(h.controller.moveSelectionNextTo(h.rowOf(u"Podlać"_s), true));
    QCOMPARE(h.rowOf(u"Skuć"_s), h.rowOf(u"Podlać"_s) + 1);
    QCOMPARE(h.depth(h.rowOf(u"Skuć"_s)), 1);
    // A parent with its children before the first sibling.
    h.controller.selectRange(h.rowOf(u"Notatki z rozmowy"_s), h.rowOf(u"Notatki z rozmowy"_s));
    QVERIFY(h.controller.moveSelectionNextTo(h.rowOf(u"Przygotowanie"_s), false));
    QCOMPARE(h.rowOf(u"Notatki z rozmowy"_s), 0);
    QCOMPARE(h.rowOf(u"Ekipa może"_s), 1);
    QVERIFY(h.controller.sourceText().startsWith(u"- Notatki z rozmowy z ekipą\n\t- Ekipa może"_s));
    // Picked blocks with gaps: never.
    h.controller.toggleSelected(h.rowOf(u"Zmierzyć"_s));
    h.controller.toggleSelected(h.rowOf(u"Zadzwonić"_s));
    QVERIFY(h.controller.selectionSiblings().isEmpty());
    QVERIFY(!h.controller.moveSelectionNextTo(h.rowOf(u"Kupić"_s), true));
}

QTEST_MAIN(TestAtramentSelection)
#include "tst_atrament_selection.moc"
