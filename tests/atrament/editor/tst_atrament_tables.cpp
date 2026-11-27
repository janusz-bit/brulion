// Tables in the block editor drawn by Atrament (src/app/editor/atrament/tablerows.cpp,
// tablelayout.h; docs/ATRAMENT.md "The block editor in Atrament (M2)", step 5, tables): a page of
// a PageController over FakeEditorHost drawn headless on Atrament's test platform — the grid as
// TableBlockView lays it out (the header's band, the lines, the columns' natural widths shared out
// in proportion, alignments, a formula's tint and result, a wide table scrolled sideways), a link
// of a cell clicked, a click editing the right cell at the right character, typing written to the
// file (one undo step per cell), the keys of TableBlockView's cell field (Tab / Shift+Tab, Enter,
// ↑/↓ across rows and out of the table, ←/→ at the text's edge, Ctrl+Enter, Alt+arrows, Esc
// cancelling, Ctrl+Z / Ctrl+Y with the cell edited on), "+ Row" / "+ Column", the table for screen
// readers, and the QML delegate kept where Atrament does not draw the table. The checks of
// TableSelfTest.qml (tst_editor_qml_table) that apply to the keys are ported here.
#include "blocklistmodel.h"
#include "fakeeditorhost.h"
#include "pagecontroller.h"
#include "pageview.h"
#include "tablelayout.h"
#include "text/fonts.h"
#include "text/textsystem.h"
#include "ui/testplatform.h"

#include <QTest>

#include <memory>

using namespace brulion;
using namespace brulion::editor;
using namespace brulion::editor::atr;
using namespace Qt::StringLiterals;
namespace at = brulion::atrament;

namespace {

const QString PagePath = u"Notatki/Remont.md"_s;

// A paragraph long enough to reach over the table's second column, the table, a paragraph.
const QString Sample = u"Akapit nad tabelą, dość długi, żeby sięgał nad drugą kolumnę.\n"
                       "\n"
                       "| Pozycja | Ilość | Cena | Razem |\n"
                       "| --- | ---: | :-: | --- |\n"
                       "| **Płytki** z [[Castorama]] | 6,5 | 89 | =[Ilość]*[Cena] |\n"
                       "| Klej *elastyczny* | 4 | 45 | =B2*C2 |\n"
                       "| Fuga #zakupy | 2 | 38 | =B3*C3 |\n"
                       "\n"
                       "Akapit pod tabelą, też dość długi, żeby sięgał nad kolumny.\n"_s;
constexpr int TableRow = 1;

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

// Colours a test tells apart.
PageView::TableLook testLook()
{
    PageView::TableLook look;
    look.background = at::rgb(0xffffff);
    look.headerBackground = at::rgb(0xffe0b0);
    look.border = at::rgb(0x808080);
    look.borderStrong = at::rgb(0xff0000);
    look.borderSubtle = at::rgb(0x0000ff);
    look.formula = at::rgba(0x00ff0040);
    look.focusRing = at::rgb(0x00a000);
    look.input = at::rgb(0xf0f0ff);
    return look;
}

struct Harness {
    FakeEditorHost host;
    PageController controller;
    std::shared_ptr<at::TestPlatform> platform = std::make_shared<at::TestPlatform>();
    at::App app{platform, loadFonts()};
    at::TestWindow &tw;
    at::Entity<PageView> page;
    QStringList links;

    explicit Harness(const QString &content = Sample, at::Size size = {600, 400}, bool tables = true)
        : tw(platform->newWindow(size, 1))
    {
        host.setFile(PagePath, content);
        controller.setNow(u"2026-11-27 10:00"_s);
        controller.setSaveDelay(1000000); // saved by the tests only
        controller.setHost(&host);
        controller.setPath(PagePath);
        page = app.newEntity<PageView>(controller.blocks());
        page.update(app, [this, tables](PageView &v, at::Context<PageView> &cx) {
            v.setController(&controller, cx);
            v.setDrawsTables(tables, testLook(), cx);
            v.onLink = [this](int, const QString &href) { links.append(href); };
        });
        app.openWindow(tw, page);
        tw.settle();
    }
    BlockListModel *model() const { return controller.blocks(); }
    const PageView &view() { return page.read(app); }
    at::Window &window() { return *app.windows().front(); }
    QString inputText() { return view().cellInput() ? str(view().cellInput()->read(app).text()) : QString(); }
    int inputCaret() { return view().cellInput() ? view().cellInput()->read(app).selection().focus : -1; }
    bool editing(int r, int c)
    {
        const PageView::TableCell cell = view().editedCell();
        return cell.row == TableRow && cell.tableRow == r && cell.column == c;
    }
    QString editedText() { return editing(view().editedCell().tableRow, view().editedCell().column) ? inputText() : QString(); }
    at::Rect cell(int r, int c, int row = TableRow)
    {
        const std::optional<at::Rect> b = view().cellBounds(row, r, c);
        return b.value_or(at::Rect{});
    }
    // The text a drawn cell shows.
    QString shown(int r, int c, int row = TableRow)
    {
        const at::TextLayoutHandle *t = view().cellText(row, r, c);
        return t && t->layout() ? str(t->layout()->text()) : QString();
    }
    // The middle of the caret before display offset `offset` of a drawn cell's text.
    at::Point pointOf(int r, int c, int offset, int row = TableRow)
    {
        const at::TextLayoutHandle *t = view().cellText(row, r, c);
        if (!t || !t->layout())
            return {-1, -1};
        const at::Rect caret = t->layout()->caretRect(offset).translated(t->bounds().origin);
        return {caret.left() + 0.5f, caret.top() + caret.height() / 2};
    }
    void clickCell(int r, int c, int offset)
    {
        const at::Point p = pointOf(r, c, offset);
        QVERIFY2(p.x >= 0, "the cell is drawn");
        tw.simulateClick(p);
        tw.settle();
    }
    // A cell of the table as the model has it (−1: the header).
    QString raw(int r, int c)
    {
        const TableGrid g = readTableGrid(model()->index(TableRow).data(BlockListModel::TableDataRole).toMap());
        const TableGridCell *cell = g.cell(r, c);
        return cell ? cell->text : u"<none>"_s;
    }
    int tableRows() { return readTableGrid(model()->index(TableRow).data(BlockListModel::TableDataRole).toMap()).rowCount(); }
    void keys(std::u16string_view k)
    {
        tw.simulateKeystrokes(k);
        tw.settle();
    }
    bool near(const at::Color &c, at::Pixel p, int tolerance = 40)
    {
        auto ch = [](float v) { return int(std::lround(v * 255)); };
        return std::abs(ch(c.r) - p.r) <= tolerance && std::abs(ch(c.g) - p.g) <= tolerance
               && std::abs(ch(c.b) - p.b) <= tolerance;
    }
};

} // namespace

class TestAtramentTables : public QObject {
    Q_OBJECT
private Q_SLOTS:
    void tableDrawnByAtrament();
    void gridGeometry();
    void gridLook();
    void columnAlignment();
    void wideTableScrollsSideways();
    void linkInACellClicked();
    void clickEditsTheCellAtTheCharacter();
    void typingWritesTheFile();
    void tabAndEnterBetweenCells();
    void arrowsAcrossRowsAndOutOfTheTable();
    void escapeCancelsTheCell();
    void undoKeepsTheCellEdited();
    void rowsAndColumnsByKeys();
    void addButtons();
    void tablesForScreenReaders();
    void readOnlyTableIsNotEdited();
    void foreignWhenNotDrawn();
};

void TestAtramentTables::tableDrawnByAtrament()
{
    Harness h;
    QCOMPARE(h.model()->index(TableRow).data(BlockListModel::ViewRole).toString(), u"table"_s);
    QVERIFY(h.view().drawsRow(TableRow));
    QVERIFY(!h.view().editsRow(TableRow)); // (its cells are edited, not its Markdown)
    // No QML delegate over it.
    for (const ForeignFrame &f : h.view().foreignFrames())
        QVERIFY(f.kind != ForeignFrame::Kind::Row || f.row != TableRow);
    // Each cell's rendering: the header as written, Markdown rendered (bold, a page link's title,
    // a tag), a formula's result.
    QCOMPARE(h.shown(-1, 0), u"Pozycja"_s);
    QCOMPARE(h.shown(-1, 1), u"Ilość"_s);
    QCOMPARE(h.shown(0, 0), u"Płytki z\u00a0Castorama"_s); // (the renderer's no-break space before a link)
    QCOMPARE(h.shown(1, 0), u"Klej elastyczny"_s);
    QVERIFY2(h.shown(2, 0).contains(u"zakupy"_s), qPrintable(h.shown(2, 0)));
    QCOMPARE(h.shown(0, 3), u"578,5"_s);
    QCOMPARE(h.shown(1, 3), u"180"_s);
    // No cell is edited, no block either.
    QCOMPARE(h.view().editedCell().row, -1);
    QCOMPARE(h.controller.editingRow(), -1);
}

void TestAtramentTables::gridGeometry()
{
    Harness h;
    const PageView::TableLook look = testLook();
    const PageMetrics &m = h.view().metrics();
    // The natural widths of TableBlockView's rule, measured with the same fonts.
    const TableGrid grid = readTableGrid(h.model()->index(TableRow).data(BlockListModel::TableDataRole).toMap());
    QCOMPARE(grid.columnCount(), 4);
    QCOMPARE(grid.rowCount(), 3);
    at::TextSystem &ts = h.app.textSystem();
    const std::vector<float> natural = tableColumnWidths(
            grid,
            [&](QStringView text, bool header) {
                at::FontRun run;
                run.length = int(text.size());
                run.face = ts.fonts().match(look.fontFamily,
                                            at::FontWeight{header ? at::FontWeight::SemiBold : at::FontWeight::Normal});
                run.fontSize = look.fontSize;
                return ts.layout(std::u16string_view(reinterpret_cast<const char16_t *>(text.utf16()), std::size_t(text.size())),
                                 std::span<const at::FontRun>(&run, 1), at::ParagraphStyle{})
                        ->size()
                        .width;
            },
            look.cellPadding);
    float total = 0;
    for (const float w : natural) {
        QVERIFY2(w >= 56 && w <= 280, qPrintable(QString::number(w)));
        total += w;
    }
    // "Płytki z Castorama" is the widest of its column: its width plus the padding.
    QVERIFY(natural[0] > natural[1]);
    // The grid at the text's place, as wide as the page beyond it: the columns share it in
    // proportion to their natural widths (TableBlockView's stretch).
    const float left = m.contentX + m.gutter;
    const float width = 600 - left;
    QVERIFY(total < width);
    QVERIFY2(std::abs(h.cell(-1, 0).left() - left) < 0.5f, qPrintable(QString::number(h.cell(-1, 0).left())));
    QVERIFY2(std::abs(h.cell(-1, 3).right() - 600) < 1, qPrintable(QString::number(h.cell(-1, 3).right())));
    for (int c = 0; c < 4; ++c) {
        const float expected = natural[std::size_t(c)] * width / total;
        QVERIFY2(std::abs(h.cell(-1, c).width() - expected) < 1,
                 qPrintable(u"column %1: %2 vs %3"_s.arg(c).arg(h.cell(-1, c).width()).arg(expected)));
        // Every row's cell of a column is where the header's is.
        for (int r = 0; r < 3; ++r) {
            QCOMPARE(h.cell(r, c).left(), h.cell(-1, c).left());
            QCOMPARE(h.cell(r, c).width(), h.cell(-1, c).width());
        }
    }
    // Rows of one line are rowHeight high, one under the other; the header too (one line).
    QCOMPARE(h.cell(-1, 0).height(), look.rowHeight);
    for (int r = 0; r < 3; ++r) {
        QCOMPARE(h.cell(r, 0).height(), look.rowHeight);
        QCOMPARE(h.cell(r, 0).top(), h.cell(r - 1, 0).bottom());
    }
    // The text inside its cell's padding, its one line in the middle.
    const at::TextLayoutHandle *t = h.view().cellText(TableRow, 0, 0);
    QVERIFY(t && t->layout());
    QCOMPARE(int(t->layout()->lines().size()), 1);
    QVERIFY2(std::abs(t->bounds().left() - (h.cell(0, 0).left() + look.cellPadding)) < 0.5f,
             qPrintable(QString::number(t->bounds().left())));
    QVERIFY2(std::abs(t->bounds().center().y - h.cell(0, 0).center().y) < 1, qPrintable(QString::number(t->bounds().top())));
    // A header of two lines (`<br>`): its row a line taller.
    Harness two(u"| Nazwa<br>pozycji | Cena |\n| --- | --- |\n| Klej | 45 |\n"_s);
    QCOMPARE(two.cell(-1, 0, 0).height(), look.rowHeight + look.lineHeight);
    QCOMPARE(two.cell(-1, 1, 0).height(), look.rowHeight + look.lineHeight);
    QCOMPARE(two.shown(-1, 0, 0), u"Nazwa\npozycji"_s);
    // A cell longer than the widest column (280 px) wraps inside it: its row grows.
    Harness wrapped(u"| Opis | Ilość |\n| --- | --- |\n| Bardzo długi opis pozycji, który na pewno nie zmieści się w jednej "
                    "linii kolumny tabeli | 1 |\n"_s);
    QVERIFY2(wrapped.cell(0, 0, 0).width() >= 280, qPrintable(QString::number(wrapped.cell(0, 0, 0).width())));
    const at::TextLayoutHandle *long_ = wrapped.view().cellText(0, 0, 0);
    QVERIFY(long_ && long_->layout() && long_->layout()->lines().size() >= 2);
    QVERIFY(wrapped.cell(0, 0, 0).height() > look.rowHeight);
    QCOMPARE(wrapped.cell(0, 1, 0).height(), wrapped.cell(0, 0, 0).height());
}

void TestAtramentTables::gridLook()
{
    Harness h;
    const PageView::TableLook look = testLook();
    auto pixel = [&](at::Point p) { return h.tw.pixel(int(p.x), int(p.y)); };
    // The header on its band, the rows on the grid's background.
    const at::Rect header = h.cell(-1, 1);
    QVERIFY(h.near(look.headerBackground, pixel({header.left() + 4, header.top() + 4}), 8));
    const at::Rect body = h.cell(1, 1);
    QVERIFY(h.near(look.background, pixel({body.left() + 4, body.top() + 5}), 8));
    // The strong line under the header, the subtle ones between the rows and the columns.
    QVERIFY(h.near(look.borderStrong, pixel({h.cell(0, 1).center().x, h.cell(0, 1).top()})));
    QVERIFY(h.near(look.borderSubtle, pixel({h.cell(1, 1).center().x, h.cell(1, 1).top()})));
    QVERIFY(h.near(look.borderSubtle, pixel({h.cell(1, 2).left(), h.cell(1, 2).top() + 8})));
    // A formula's cell tinted.
    const auto formula = pixel({h.cell(1, 3).left() + 4, h.cell(1, 3).top() + 5});
    QVERIFY2(formula.g > formula.r + 20, qPrintable(u"%1 %2 %3"_s.arg(formula.r).arg(formula.g).arg(formula.b)));
    // The frame around the grid.
    QVERIFY(h.near(look.border, pixel({h.cell(1, 0).left(), h.cell(1, 0).center().y}), 50));
}

void TestAtramentTables::columnAlignment()
{
    Harness h;
    const float pad = testLook().cellPadding;
    auto text = [&](int r, int c) { return h.view().cellText(TableRow, r, c); };
    auto glyphs = [&](int r, int c) {
        // The drawn text's extent: its caret at the start and at the end.
        const at::TextLayoutHandle *t = text(r, c);
        const float x0 = t->bounds().left() + t->layout()->caretX(0);
        const float x1 = t->bounds().left() + t->layout()->caretX(int(t->layout()->text().size()));
        return std::pair<float, float>{x0, x1};
    };
    // `---` (no alignment): left, at the padding.
    QVERIFY2(std::abs(glyphs(1, 0).first - (h.cell(1, 0).left() + pad)) < 1, qPrintable(QString::number(glyphs(1, 0).first)));
    // `---:`: right, ending at the padding.
    QVERIFY2(std::abs(glyphs(1, 1).second - (h.cell(1, 1).right() - pad)) < 1.5f,
             qPrintable(u"%1 vs %2"_s.arg(glyphs(1, 1).second).arg(h.cell(1, 1).right() - pad)));
    // `:-:`: centred.
    const auto centred = glyphs(1, 2);
    QVERIFY2(std::abs((centred.first + centred.second) / 2 - h.cell(1, 2).center().x) < 1.5f,
             qPrintable(u"%1 vs %2"_s.arg((centred.first + centred.second) / 2).arg(h.cell(1, 2).center().x)));
    // A formula in a column without one: right.
    QVERIFY(std::abs(glyphs(1, 3).second - (h.cell(1, 3).right() - pad)) < 1.5f);
    // (The header's text aligned as its column too.)
    QVERIFY(std::abs(glyphs(-1, 1).second - (h.cell(-1, 1).right() - pad)) < 1.5f);
}

void TestAtramentTables::wideTableScrollsSideways()
{
    // Eight columns of long words: wider than the page.
    QString table = u"| "_s;
    QString rule = u"| "_s;
    QString row = u"| "_s;
    for (int c = 0; c < 8; ++c) {
        table += u"Kolumna numer %1 | "_s.arg(c);
        rule += u"--- | "_s;
        row += u"źdźbło%1 i gałąź | "_s.arg(c);
    }
    Harness h(table.trimmed() + u"\n"_s + rule.trimmed() + u"\n"_s + row.trimmed() + u"\n"_s);
    const at::Rect first = h.cell(-1, 0, 0);
    const at::Rect last = h.cell(-1, 7, 0);
    // Columns keep their natural widths (no squeezing); the last one is beyond the page.
    QVERIFY2(last.right() > 600, qPrintable(QString::number(last.right())));
    QVERIFY(first.width() >= 56);
    // Shift+wheel (or a sideways wheel) scrolls the table, not the page.
    const float pageY = h.view().handle().scroll().offset.y;
    h.tw.simulateScroll(h.cell(0, 1, 0).center(), {0, 120}, at::Modifiers{false, false, true});
    h.tw.settle();
    QVERIFY2(h.cell(-1, 0, 0).left() < first.left() - 50, qPrintable(QString::number(h.cell(-1, 0, 0).left())));
    QCOMPARE(h.view().handle().scroll().offset.y, pageY);
    // Editing the last cell brings its caret into view.
    h.page.update(h.app, [](PageView &v, at::Context<PageView> &cx) { QVERIFY(v.editCell(0, 0, 7, -1, cx)); });
    h.tw.settle();
    const at::TextInput &input = h.view().cellInput()->read(h.app);
    const std::optional<at::Rect> caret = input.boundsForRange(at::Range{input.selection().focus, input.selection().focus});
    QVERIFY(caret);
    QVERIFY2(caret->left() >= 0 && caret->right() <= 600, qPrintable(QString::number(caret->left())));
    QVERIFY2(h.cell(0, 7, 0).left() < 600, qPrintable(QString::number(h.cell(0, 7, 0).left())));
}

void TestAtramentTables::linkInACellClicked()
{
    Harness h;
    const QString shown = h.shown(0, 0);
    const int k = int(shown.indexOf(u"Castorama"_s));
    QVERIFY(k > 0);
    h.clickCell(0, 0, k + 3);
    QCOMPARE(h.links.size(), 1);
    QVERIFY2(h.links.front().startsWith(u"page:"_s) && h.links.front().contains(u"Castorama"_s), qPrintable(h.links.front()));
    // A link's click edits nothing.
    QCOMPARE(h.view().editedCell().row, -1);
    // The cell's link for the host (with its place), when one is set.
    int cellRow = -2, cellColumn = -2;
    h.page.update(h.app, [&](PageView &v, at::Context<PageView> &) {
        v.onCellLink = [&](int row, int r, int c, const QString &, at::Point) {
            QCOMPARE(row, TableRow);
            cellRow = r;
            cellColumn = c;
        };
    });
    h.clickCell(0, 0, k + 3);
    QCOMPARE(cellRow, 0);
    QCOMPARE(cellColumn, 0);
    // The tag of another cell too.
    h.page.update(h.app, [&](PageView &v, at::Context<PageView> &) { v.onCellLink = nullptr; });
    const int tag = int(h.shown(2, 0).indexOf(u"zakupy"_s));
    h.clickCell(2, 0, tag + 2);
    QVERIFY2(h.links.size() == 2 && h.links.back().startsWith(u"tag:"_s), qPrintable(h.links.join(u' ')));
}

void TestAtramentTables::clickEditsTheCellAtTheCharacter()
{
    Harness h;
    // "Pł|ytki z Castorama" is "**Pł|ytki** z [[Castorama]]" (the bold markers before it).
    h.clickCell(0, 0, 2);
    QVERIFY(h.editing(0, 0));
    QCOMPARE(h.inputText(), u"**Płytki** z [[Castorama]]"_s);
    QCOMPARE(h.inputCaret(), 4);
    // The controller edits no block meanwhile (TableBlockView.startCell), the field has the keys.
    QCOMPARE(h.controller.editingRow(), -1);
    QVERIFY(h.view().cellInput()->read(h.app).focusHandle().isFocused(h.window()));
    // Another cell: the one clicked, at its character ("Kl|ej").
    h.clickCell(1, 0, 2);
    QVERIFY(h.editing(1, 0));
    QCOMPARE(h.inputText(), u"Klej *elastyczny*"_s);
    QCOMPARE(h.inputCaret(), 2);
    // A formula: edited as written, the caret at its end.
    h.clickCell(1, 3, 1);
    QVERIFY(h.editing(1, 3));
    QCOMPARE(h.inputText(), u"=B2*C2"_s);
    QCOMPARE(h.inputCaret(), 6);
    // The header's cell.
    h.clickCell(-1, 2, 1);
    QVERIFY(h.editing(-1, 2));
    QCOMPARE(h.inputText(), u"Cena"_s);
    QCOMPARE(h.inputCaret(), 1);
    // A click on a text block edits it: the cell's editing ends.
    const at::TextLayoutHandle *para = h.view().rowText(0);
    QVERIFY(para && para->layout());
    h.tw.simulateClick({para->bounds().left() + 10, para->bounds().center().y});
    h.tw.settle();
    QCOMPARE(h.controller.editingRow(), 0);
    QCOMPARE(h.view().editedCell().row, -1);
    // The host decides about a click when it wants to (Shift+click selects blocks in the QML editor).
    int asked = -2;
    h.page.update(h.app, [&](PageView &v, at::Context<PageView> &) {
        v.onCellEditRequest = [&](int, int r, int, int, const at::Modifiers &) {
            asked = r;
            return true;
        };
    });
    h.clickCell(2, 1, 0);
    QCOMPARE(asked, 2);
    QCOMPARE(h.view().editedCell().row, -1);
}

void TestAtramentTables::typingWritesTheFile()
{
    Harness h;
    const QString before = h.controller.text();
    // A click at the end of the text: the caret after its last letter (before the hidden "*").
    h.clickCell(1, 0, int(h.shown(1, 0).size()));
    QCOMPARE(h.inputCaret(), int(u"Klej *elastyczny"_s.size()));
    h.keys(u"end");
    h.tw.simulateTyping(u" ząbkowany");
    h.tw.settle();
    // Typed into the document at once (PageController::setTableCell, typing).
    QCOMPARE(h.raw(1, 0), u"Klej *elastyczny* ząbkowany"_s);
    QVERIFY2(h.controller.text().contains(u"| Klej *elastyczny* ząbkowany |"_s), qPrintable(h.controller.text()));
    QVERIFY(h.editing(1, 0));
    QCOMPARE(h.inputText(), u"Klej *elastyczny* ząbkowany"_s);
    // The column grew with it; the cell stays edited at its place.
    QCOMPARE(h.inputCaret(), int(u"Klej *elastyczny* ząbkowany"_s.size()));
    // Saved as any text.
    QVERIFY(h.controller.save());
    QVERIFY(h.host.file(PagePath).contains(u"| Klej *elastyczny* ząbkowany |"_s));
    // The cell's typing is one undo step.
    h.page.update(h.app, [&](PageView &v, at::Context<PageView> &cx) { v.stopEditingCell(cx); });
    h.controller.undo();
    QCOMPARE(h.controller.text(), before);
    // An input method's composition reaches the document when committed.
    h.tw.settle(); // (the columns narrower again)
    h.clickCell(2, 1, 1);
    QVERIFY(h.editing(2, 1));
    h.tw.simulateComposition(u"żó");
    QCOMPARE(h.raw(2, 1), u"2"_s);
    h.tw.simulateCommit(u"żółw");
    h.tw.settle();
    QCOMPARE(h.raw(2, 1), u"2żółw"_s);
    // A line break pasted becomes a space; an input method's Enter is the cell below.
    h.tw.simulateCommit(u"\n");
    h.tw.settle();
    QCOMPARE(h.raw(2, 1), u"2żółw"_s);
    QVERIFY2(h.editing(3, 1) && h.tableRows() == 4, qPrintable(h.controller.text()));
}

void TestAtramentTables::tabAndEnterBetweenCells()
{
    // (TableSelfTest's "Between cells with the keys".)
    Harness h;
    const QString before = h.controller.text();
    h.clickCell(0, 1, 0);
    QVERIFY2(h.editing(0, 1), "a click edits the cell");
    h.keys(u"tab");
    QVERIFY2(h.editing(0, 2), "Tab moves on");
    h.keys(u"tab tab");
    QVERIFY2(h.editing(1, 0), "Tab in a row's last cell goes to the next row");
    h.keys(u"shift-tab");
    QVERIFY2(h.editing(0, 3), "Shift+Tab goes back");
    h.keys(u"shift-tab shift-tab shift-tab shift-tab");
    QVERIFY2(h.editing(-1, 3), "Shift+Tab from a row's first cell goes to the row above's last");
    h.keys(u"tab");
    QVERIFY2(h.editing(0, 0), "Tab in the header's last cell goes to the next row");
    h.keys(u"enter");
    QVERIFY2(h.editing(1, 0), "Enter edits the cell below");
    QCOMPARE(h.controller.text(), before); // moving between cells wrote nothing
    QCOMPARE(h.tableRows(), 3);
    // Enter in the last row: a new row, its cell edited.
    h.keys(u"enter enter");
    QVERIFY(h.editing(3, 0));
    QCOMPARE(h.tableRows(), 4);
    h.tw.simulateTyping(u"Silikon");
    h.tw.settle();
    QCOMPARE(h.raw(3, 0), u"Silikon"_s);
    // Tab in the last cell: another row.
    h.keys(u"tab tab tab tab");
    QVERIFY(h.editing(4, 0));
    QCOMPARE(h.tableRows(), 5);
    // Shift+Tab at the header's first cell stays.
    h.page.update(h.app, [](PageView &v, at::Context<PageView> &cx) { QVERIFY(v.editCell(TableRow, -1, 0, 0, cx)); });
    h.keys(u"shift-tab");
    QVERIFY(h.editing(-1, 0));
}

void TestAtramentTables::arrowsAcrossRowsAndOutOfTheTable()
{
    // (TableSelfTest's arrow keys; past the header and the last row Atrament goes on to the blocks
    // there, where the QML table stops.)
    Harness h;
    const QString before = h.controller.text();
    h.clickCell(0, 1, 1);
    QVERIFY(h.editing(0, 1));
    h.keys(u"down");
    QVERIFY2(h.editing(1, 1), "Down edits the cell below");
    h.keys(u"up");
    QVERIFY2(h.editing(0, 1), "Up edits the cell above");
    h.keys(u"up");
    QVERIFY2(h.editing(-1, 1), "Up goes on to the header");
    // Left at the start of the text: the cell on the left, its caret at the end; Right at the end
    // the cell on the right, its caret at the start.
    h.keys(u"home left");
    QVERIFY2(h.editing(-1, 0), "Left at the start of the text edits the cell on the left");
    QCOMPARE(h.inputCaret(), int(u"Pozycja"_s.size()));
    h.keys(u"left");
    QVERIFY2(h.editing(-1, 0) && h.inputCaret() == int(u"Pozycja"_s.size()) - 1, "Left in the text moves the caret");
    h.keys(u"home left");
    QVERIFY2(h.editing(-1, 0), "nothing left of the first column");
    h.keys(u"end right");
    QVERIFY2(h.editing(-1, 1), "Right at the end of the text edits the cell on the right");
    QCOMPARE(h.inputCaret(), 0);
    // Up from the header: the paragraph above, the caret over the cell.
    const float cellX = h.cell(-1, 1).left() + testLook().cellPadding;
    h.keys(u"up");
    QCOMPARE(h.view().editedCell().row, -1);
    QCOMPARE(h.controller.editingRow(), 0);
    const at::TextInput &para = h.view().input()->read(h.app);
    const float caretX = para.bounds()->left() + para.layout()->caretX(para.selection().focus);
    QVERIFY2(std::abs(caretX - cellX) < 12, qPrintable(u"%1 vs %2"_s.arg(caretX).arg(cellX)));
    // Down from the paragraph: the header's cell under the caret.
    h.keys(u"down");
    QVERIFY2(h.editing(-1, 1), qPrintable(u"%1 %2"_s.arg(h.view().editedCell().tableRow).arg(h.view().editedCell().column)));
    QCOMPARE(h.controller.editingRow(), -1);
    // Down in the last row: the paragraph below; Up from it: the last row's cell.
    h.keys(u"down down down");
    QVERIFY(h.editing(2, 1));
    h.keys(u"down");
    QCOMPARE(h.controller.editingRow(), 2);
    QCOMPARE(h.view().editedCell().row, -1);
    QCOMPARE(h.tableRows(), 3); // (no row added)
    h.keys(u"up");
    QVERIFY2(h.editing(2, 1), qPrintable(u"%1 %2"_s.arg(h.view().editedCell().tableRow).arg(h.view().editedCell().column)));
    QCOMPARE(h.controller.text(), before); // moving wrote nothing
}

void TestAtramentTables::escapeCancelsTheCell()
{
    Harness h;
    const QString before = h.controller.text();
    h.clickCell(1, 2, 2);
    h.tw.simulateTyping(u"0 zł");
    h.tw.settle();
    QCOMPARE(h.raw(1, 2), u"450 zł"_s);
    // Esc: the cell's typing undone (TableBlockView's cancel), the table selected, no cell edited.
    bool escaped = false;
    h.page.update(h.app, [&](PageView &v, at::Context<PageView> &) { v.onEscape = [&] { escaped = true; }; });
    h.keys(u"escape");
    QVERIFY(escaped);
    QCOMPARE(h.controller.text(), before);
    QCOMPARE(h.view().editedCell().row, -1);
    QCOMPARE(h.controller.selectedRow(), TableRow);
    QCOMPARE(h.controller.editingRow(), -1);
    // Esc in a cell nothing was typed into changes nothing.
    h.clickCell(0, 2, 0);
    h.keys(u"escape");
    QCOMPARE(h.controller.text(), before);
}

void TestAtramentTables::undoKeepsTheCellEdited()
{
    Harness h;
    const QString before = h.controller.text();
    h.clickCell(2, 0, int(h.shown(2, 0).size()));
    h.tw.simulateTyping(u" szara");
    h.tw.settle();
    QCOMPARE(h.raw(2, 0), u"Fuga #zakupy szara"_s);
    // Ctrl+Z: the page's undo, the cell edited on with the text the step left.
    int undos = 0;
    h.page.update(h.app, [&](PageView &v, at::Context<PageView> &) {
        v.onUndo = [&](bool redo) {
            ++undos;
            if (redo)
                h.controller.redo();
            else
                h.controller.undo();
        };
    });
    h.keys(u"ctrl-z");
    QCOMPARE(undos, 1);
    QCOMPARE(h.controller.text(), before);
    QVERIFY(h.editing(2, 0));
    QCOMPARE(h.inputText(), u"Fuga #zakupy"_s);
    QVERIFY(h.view().cellInput()->read(h.app).focusHandle().isFocused(h.window()));
    // Ctrl+Y (and Ctrl+Shift+Z): again.
    h.keys(u"ctrl-y");
    QCOMPARE(h.raw(2, 0), u"Fuga #zakupy szara"_s);
    QCOMPARE(h.inputText(), u"Fuga #zakupy szara"_s);
    h.keys(u"ctrl-z ctrl-shift-z");
    QCOMPARE(h.raw(2, 0), u"Fuga #zakupy szara"_s);
    // Typing goes on after an undo: a new step.
    h.keys(u"ctrl-z");
    h.tw.simulateTyping(u"!");
    h.tw.settle();
    QCOMPARE(h.raw(2, 0), u"Fuga #zakupy!"_s);
    h.keys(u"ctrl-z");
    QCOMPARE(h.controller.text(), before);
}

void TestAtramentTables::rowsAndColumnsByKeys()
{
    // (TableSelfTest's keySteps: rows and columns move, the cell stays edited.)
    Harness h;
    const QString before = h.controller.text();
    h.clickCell(0, 1, 0);
    h.keys(u"alt-down");
    QVERIFY2(h.editing(1, 1), "Alt+Down moves the row, the cell goes on being edited");
    QCOMPARE(h.raw(1, 1), u"6,5"_s);
    QCOMPARE(h.inputText(), u"6,5"_s);
    QVERIFY2(h.raw(0, 3) == u"=B1*C1"_s, qPrintable(h.raw(0, 3))); // the formula of the row it passed follows
    h.keys(u"alt-up");
    QVERIFY2(h.editing(0, 1), "Alt+Up moves it back");
    h.keys(u"alt-up");
    QVERIFY2(h.editing(0, 1), "the first row stays");
    h.keys(u"alt-right");
    QVERIFY2(h.editing(0, 2), "Alt+Right moves the column");
    QCOMPARE(h.raw(-1, 2), u"Ilość"_s);
    QCOMPARE(h.raw(-1, 1), u"Cena"_s);
    QVERIFY2(h.raw(1, 3) == u"=C2*B2"_s, qPrintable(h.raw(1, 3))); // formulas follow
    h.keys(u"alt-left");
    QVERIFY2(h.editing(0, 1), "Alt+Left moves it back");
    h.keys(u"ctrl-enter");
    QVERIFY2(h.editing(1, 1) && h.tableRows() == 4, "Ctrl+Enter inserts a row below, its cell edited");
    QCOMPARE(h.raw(1, 1), u""_s);
    QVERIFY2(h.raw(2, 3) == u"=B3*C3"_s, qPrintable(h.raw(2, 3))); // the rows below move down
    h.keys(u"escape");
    QCOMPARE(h.view().editedCell().row, -1);
    // Each step undone brings the page back byte for byte.
    for (int k = 0; k < 5; ++k)
        h.controller.undo();
    QCOMPARE(h.controller.text(), before);
}

void TestAtramentTables::addButtons()
{
    Harness h;
    h.window().setAccessibilityEnabled(true);
    h.tw.settle();
    auto button = [&](const char16_t *name) -> const at::AccessibilityNode * {
        for (const at::AccessibilityNode *n : h.window().accessibilityTree().nodes())
            if (n->role == at::Role::Button && n->name == std::u16string_view(name))
                return n;
        return nullptr;
    };
    // Not while the table is only read.
    QVERIFY(!button(u"Row"));
    h.clickCell(1, 0, 0);
    const at::AccessibilityNode *row = button(u"Row");
    QVERIFY(row && button(u"Column"));
    // Below the grid.
    QVERIFY(row->bounds.top() >= h.cell(2, 0).bottom());
    h.tw.simulateClick(row->bounds.center());
    h.tw.settle();
    QCOMPARE(h.tableRows(), 4);
    QVERIFY(h.editing(1, 0)); // the cell stays edited
    const at::AccessibilityNode *column = button(u"Column");
    QVERIFY(column);
    h.tw.simulateClick(column->bounds.center());
    h.tw.settle();
    QCOMPARE(h.raw(-1, 4), u"Kolumna 5"_s);
    QVERIFY(h.editing(1, 0));
    // The table selected shows them too.
    h.keys(u"escape");
    QCOMPARE(h.controller.selectedRow(), TableRow);
    QVERIFY(button(u"Row"));
}

void TestAtramentTables::tablesForScreenReaders()
{
    Harness h;
    h.window().setAccessibilityEnabled(true);
    h.tw.settle();
    const QString tree = str(h.window().accessibilityTree().dump());
    // A table of rows of cells, the header's cells naming their columns, the cells their column.
    QVERIFY2(tree.contains(u"table \"Table\""_s), qPrintable(tree));
    QVERIFY2(tree.contains(u"row"_s), qPrintable(tree));
    QVERIFY2(tree.contains(u"columnHeader \"Pozycja\""_s), qPrintable(tree));
    QVERIFY2(tree.contains(u"columnHeader \"Ilość\""_s), qPrintable(tree));
    QVERIFY2(tree.contains(u"cell \"4\" (Ilość)"_s), qPrintable(tree));
    QVERIFY2(tree.contains(u"cell \"578,5\" (Razem)"_s), qPrintable(tree));
    // The links of a cell are links.
    QVERIFY2(tree.contains(u"link \"Castorama\""_s), qPrintable(tree));
    int rows = 0;
    for (const at::AccessibilityNode *n : h.window().accessibilityTree().nodes())
        rows += n->role == at::Role::Row ? 1 : 0;
    QCOMPARE(rows, 4); // the header and three rows
    // A screen reader activating a cell edits it; the field is named after it.
    const at::AccessibilityNode *cell = nullptr;
    for (const at::AccessibilityNode *n : h.window().accessibilityTree().nodes())
        if (n->role == at::Role::Cell && n->name == std::u16string_view(u"45"))
            cell = n;
    QVERIFY(cell);
    QVERIFY(h.window().performAccessibilityAction(cell->id, at::AccessibilityAction::Click));
    h.tw.settle();
    QVERIFY(h.editing(1, 2));
    const QString edited = str(h.window().accessibilityTree().dump());
    QVERIFY2(edited.contains(u"textInput \"Cell 45\" = \"45\""_s), qPrintable(edited));
}

void TestAtramentTables::readOnlyTableIsNotEdited()
{
    Harness h;
    h.controller.setReadOnly(true);
    h.tw.settle();
    h.clickCell(1, 0, 1);
    QCOMPARE(h.view().editedCell().row, -1);
    h.page.update(h.app, [](PageView &v, at::Context<PageView> &cx) { QVERIFY(!v.editCell(TableRow, 0, 0, 0, cx)); });
    // Its links still work.
    h.clickCell(0, 0, int(h.shown(0, 0).indexOf(u"Castorama"_s)) + 2);
    QCOMPARE(h.links.size(), 1);
}

void TestAtramentTables::foreignWhenNotDrawn()
{
    // Off: the QML delegate's (a foreign row), as before.
    Harness off(Sample, {600, 400}, false);
    QVERIFY(!off.view().drawsRow(TableRow));
    bool foreign = false;
    for (const ForeignFrame &f : off.view().foreignFrames())
        foreign = foreign || (f.kind == ForeignFrame::Kind::Row && f.row == TableRow);
    QVERIFY(foreign);
    // A table showing its filter row (TableBlockView's fields) stays the QML view's too.
    Harness h;
    QVERIFY(h.view().drawsRow(TableRow));
    h.model()->setTableFilterOpen(TableRow, true);
    h.tw.settle();
    QVERIFY(!h.view().drawsRow(TableRow));
    h.model()->setTableFilterOpen(TableRow, false);
    h.tw.settle();
    QVERIFY(h.view().drawsRow(TableRow));
    // A table being edited stops being edited when the page stops drawing tables.
    h.clickCell(0, 0, 0);
    QVERIFY(h.editing(0, 0));
    h.page.update(h.app, [](PageView &v, at::Context<PageView> &cx) { v.setDrawsTables(false, testLook(), cx); });
    h.tw.settle();
    QCOMPARE(h.view().editedCell().row, -1);
    QVERIFY(!h.view().drawsRow(TableRow));
}

QTEST_MAIN(TestAtramentTables)
#include "tst_atrament_tables.moc"
