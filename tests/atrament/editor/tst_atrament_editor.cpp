// The block editor drawn by Atrament (src/app/editor/atrament; docs/ATRAMENT.md "The block editor
// in Atrament (M2)", step 1 "Reading"): a page of BlockListModel drawn headless on Atrament's test
// platform — the rendered text's spans (bold, code, links, highlights), which rows Atrament draws
// and which are foreign (code), headings taller than paragraphs, list bullets and numbers, a task's
// box for screen readers, only the rows in view laid out on a 2 000-block page, a fold above the
// view moving nothing on screen, a narrower window laying the rows out again.
#include "blocklistmodel.h"
#include "editorhost.h"
#include "markdown/inlinerenderer.h"
#include "pageview.h"
#include "text/fonts.h"
#include "ui/testplatform.h"

#include <QDir>
#include <QFile>
#include <QGuiApplication>
#include <QTest>

#include <memory>

using namespace brulion::editor;
namespace markdown = brulion::markdown;
using namespace brulion::editor::atr;
using namespace Qt::StringLiterals;
namespace at = brulion::atrament;

namespace {

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

struct Harness {
    std::shared_ptr<at::TestPlatform> platform = std::make_shared<at::TestPlatform>();
    at::App app{platform, loadFonts()};
    at::TestWindow &tw;
    BlockListModel model;
    at::Entity<PageView> page;
    at::Window *window = nullptr;

    Harness(const QString &content, at::Size size = {600, 400}) : tw(platform->newWindow(size, 1))
    {
        model.resetContent(content);
        page = app.newEntity<PageView>(&model);
        window = &app.openWindow(tw, page);
        tw.settle();
    }
    const PageView &view() const { return page.read(const_cast<at::App &>(app)); }
    // Rows of pixels with ink (darker than the background) between y0 and y1.
    int inkRows(int y0, int y1, int x0 = 0, int x1 = 600) const
    {
        int rows = 0;
        for (int y = y0; y < y1; ++y) {
            bool ink = false;
            for (int x = x0; x < x1 && !ink; ++x)
                ink = tw.pixel(x, y).r < 160;
            rows += ink ? 1 : 0;
        }
        return rows;
    }
};

QString longPage(int blocks)
{
    QStringList lines;
    for (int k = 0; k < blocks; ++k) {
        if (k % 9 == 4)
            lines << u"  - zagnieżdżony punkt %1: źdźbło trawy i gęś"_s.arg(k);
        else if (k % 13 == 6)
            lines << u"- %1 Długi punkt, który zawija się na kilka linii: zażółć gęślą jaźń, pchnąć w tę łódź jeża "
                     "lub ośm skrzyń fig, a potem jeszcze raz to samo."_s.arg(k);
        else
            lines << u"- punkt %1 o [[Stronie]] i #tagu"_s.arg(k);
    }
    return lines.join(u'\n') + u'\n';
}

} // namespace

class TestAtramentEditor : public QObject {
    Q_OBJECT
private Q_SLOTS:
    void spansOfRenderedText();
    void cssColours();
    void bundledFaces();
    void boldAndItalicDrawn();
    void drawnAndForeignRows();
    void headingsAndLists();
    void taskForScreenReaders();
    void linksAndTaskBoxesClick();
    void longPageLaysOutWhatIsShown();
    void foldAboveKeepsTheScreen();
    void narrowerWindowReflows();
    void spansCarryShapes();
    void chipsAndCodeDrawnAsShapes();
    void pageIconAsInlineObject();
};

void TestAtramentEditor::spansOfRenderedText()
{
    markdown::RenderContext context;
    context.today = QDate(2026, 11, 26);
    const markdown::InlineRenderer renderer(context);
    const markdown::RenderedText text = renderer.renderRuns(u"**gruby** i `kod` oraz [[Strona]] ==ważne== ~~nie~~"_s);
    QVERIFY(text.drawable);
    PagePalette palette;
    const std::vector<at::TextSpan> spans = renderedSpans(text, palette);
    auto spanOf = [&](const QString &word) -> const at::TextSpan * {
        const int from = int(text.text.indexOf(word));
        for (const at::TextSpan &s : spans)
            if (s.range.start <= from && s.range.end >= from + int(word.size()))
                return &s;
        return nullptr;
    };
    const at::TextSpan *bold = spanOf(u"gruby"_s);
    QVERIFY(bold && bold->style.fontWeight && bold->style.fontWeight->value == at::FontWeight::Bold);
    const at::TextSpan *code = spanOf(u"kod"_s);
    QVERIFY(code && code->style.fontFamily == palette.codeFamily);
    QVERIFY(code->style.background && code->style.background->has_value());
    const at::TextSpan *link = spanOf(u"Strona"_s);
    QVERIFY(link && link->style.color); // the renderer's colour or the palette's
    QVERIFY(link->style.color->r != palette.text.r || link->style.color->b != palette.text.b);
    const at::TextSpan *mark = spanOf(u"ważne"_s);
    QVERIFY(mark && mark->style.background && mark->style.background->has_value());
    const at::TextSpan *strike = spanOf(u"nie"_s);
    QVERIFY(strike && strike->style.strikethrough.value_or(false));
    // Plain text has no span.
    QVERIFY(!spanOf(u" i "_s));
    // A plain paragraph: no spans at all.
    QVERIFY(renderedSpans(renderer.renderRuns(u"zażółć gęślą jaźń"_s), palette).empty());
}

void TestAtramentEditor::cssColours()
{
    const at::Color fallback = at::rgb(0x123456);
    const at::Color red = cssColor(u"#ff0000"_s, fallback);
    QCOMPARE(red.r, 1.0f);
    QCOMPARE(red.g, 0.0f);
    const at::Color half = cssColor(u"rgba(0, 0, 255, 0.5)"_s, fallback);
    QCOMPARE(half.b, 1.0f);
    QVERIFY(std::abs(half.a - 0.5f) < 0.01f);
    const at::Color spaced = cssColor(u"rgb( 255 , 128 , 0 )"_s, fallback);
    QCOMPARE(spaced.r, 1.0f);
    QVERIFY(std::abs(spaced.g - 128 / 255.0f) < 0.001f);
    QCOMPARE(spaced.a, 1.0f);
    QCOMPARE(cssColor(u"rgba(1, 2)"_s, fallback).r, fallback.r);  // malformed
    QCOMPARE(cssColor(u"#80ff0000"_s, fallback).a, 128 / 255.0f); // #aarrggbb
    QCOMPARE(cssColor(QString(), fallback).b, fallback.b);
    QCOMPARE(cssColor(u"nie-kolor"_s, fallback).r, fallback.r);
}

void TestAtramentEditor::bundledFaces()
{
    // The app's own fonts (assets/fonts/ui, ":/fonts/ui" in the app): the page's text in its
    // regular face, bold and italic runs in theirs.
    at::FontCollection fonts;
    const QDir dir(QStringLiteral(BRULION_SOURCE_DIR "/assets/fonts/ui"));
    for (const QString &f : dir.entryList({u"*.ttf"_s}, QDir::Files))
        fonts.addFontFile(dir.filePath(f).toStdString());
    for (std::size_t i = 0; i < fonts.faceCount(); ++i) {
        const at::FaceInfo &info = fonts.faceInfo(at::FaceId(i));
        qDebug() << QString::fromStdU16String(info.family) << QString::fromStdU16String(info.postscriptName)
                 << info.weight.value << int(info.style);
    }
    auto name = [&](at::FaceId face) {
        return face == at::kNoFace ? QString() : QString::fromStdU16String(fonts.faceInfo(face).postscriptName);
    };
    const PagePalette palette;
    QVERIFY2(name(fonts.match(palette.bodyFamily)).contains(u"Regular"_s),
             qPrintable(name(fonts.match(palette.bodyFamily))));
    QVERIFY(name(fonts.match(palette.bodyFamily, {at::FontWeight::Bold})).contains(u"Bold"_s));
    QVERIFY(name(fonts.match(palette.bodyFamily, {}, at::FontStyle::Italic)).contains(u"Italic"_s));
    QVERIFY(fonts.match(palette.codeFamily) != at::kNoFace);
}

void TestAtramentEditor::boldAndItalicDrawn()
{
    Harness h(u"Zwykły tekst akapitu\n\n**Zwykły tekst akapitu**\n\n*Zwykły tekst akapitu*\n"_s);
    const RowHeightTree &rows = h.view().rows().heights();
    auto ink = [&](int row) {
        const int top = int(rows.offsetOf(PageRows::treeRow(row)));
        const int bottom = int(rows.offsetOf(PageRows::treeRow(row + 1)));
        int dark = 0;
        for (int y = top; y < bottom; ++y)
            for (int x = 0; x < 600; ++x)
                dark += h.tw.pixel(x, y).r < 128 ? 1 : 0;
        return dark;
    };
    const int regular = ink(0);
    const int bold = ink(1);
    const int italic = ink(2);
    QVERIFY2(bold > regular * 1.15, qPrintable(u"%1 %2"_s.arg(regular).arg(bold)));
    QVERIFY(italic > 0 && italic < bold);
}

void TestAtramentEditor::drawnAndForeignRows()
{
    Harness h(u"Akapit o **gęsi**.\n\n```cpp\nint main() {}\n```\n\n- punkt listy\n"_s);
    QCOMPARE(h.model.rowCount(), 3);
    const PageView &v = h.view();
    QVERIFY(v.drawsRow(0));
    QVERIFY(!v.drawsRow(1)); // code: foreign
    QVERIFY(v.drawsRow(2));
    QCOMPARE(v.drawnRows(), 2);
    QCOMPARE(v.foreignRows(), std::vector<std::size_t>{1});
    // Text on screen: the paragraph's first line has ink.
    QVERIFY(h.inkRows(0, 30) > 5);
    QVERIFY(v.rows().heights().isMeasured(PageRows::treeRow(0)));
}

void TestAtramentEditor::headingsAndLists()
{
    Harness h(u"# Nagłówek strony\n\nZwykły akapit.\n\n1. pierwszy\n2. drugi\n- kropka\n"_s);
    const RowHeightTree &rows = h.view().rows().heights();
    const qreal heading = rows.height(PageRows::treeRow(0));
    const qreal paragraph = rows.height(PageRows::treeRow(1));
    QVERIFY2(heading > paragraph, qPrintable(u"%1 vs %2"_s.arg(heading).arg(paragraph)));
    // The numbered item shows its number in the bullet column, the bullet item a dot: both have
    // ink left of the text (gutter 16 + bullet column 20).
    const int top2 = int(rows.offsetOf(PageRows::treeRow(2)));
    const int top4 = int(rows.offsetOf(PageRows::treeRow(4)));
    QVERIFY(h.inkRows(top2, top2 + 24, 16, 34) > 3);
    QVERIFY(h.inkRows(top4, top4 + 24, 16, 34) > 2);
}

void TestAtramentEditor::taskForScreenReaders()
{
    Harness h(u"- [x] Kupić żurek\n- [ ] Zadzwonić do Łucji\n"_s);
    h.window->setAccessibilityEnabled(true);
    h.tw.settle();
    int checked = 0;
    int unchecked = 0;
    for (const at::AccessibilityNode *n : h.window->accessibilityTree().nodes()) {
        if (n->role != at::Role::CheckBox)
            continue;
        (n->states.checked ? checked : unchecked) += 1;
    }
    QCOMPARE(checked, 1);
    QCOMPARE(unchecked, 1);
}

void TestAtramentEditor::linksAndTaskBoxesClick()
{
    Harness h(u"Zobacz [[Strona główna]] i #projekt\n- [ ] Zadzwonić do Łucji\n"_s);
    int linkRow = -1;
    QString href;
    int toggled = -1;
    h.page.update(h.app, [&](PageView &v, at::Context<PageView> &) {
        v.onLink = [&](int row, const QString &link) {
            linkRow = row;
            href = link;
        };
        v.onToggleTask = [&](int row) { toggled = row; };
    });
    h.window->setAccessibilityEnabled(true);
    h.tw.settle();
    const at::AccessibilityNode *page = nullptr;
    const at::AccessibilityNode *tag = nullptr;
    const at::AccessibilityNode *box = nullptr;
    for (const at::AccessibilityNode *n : h.window->accessibilityTree().nodes()) {
        if (n->role == at::Role::Link && n->name.find(u"Strona") != std::u16string_view::npos)
            page = n;
        if (n->role == at::Role::Link && n->name.find(u"projekt") != std::u16string_view::npos)
            tag = n;
        if (n->role == at::Role::CheckBox)
            box = n;
    }
    QVERIFY(page && tag && box);
    h.tw.simulateClick(page->bounds.center());
    QCOMPARE(linkRow, 0);
    QVERIFY2(href.contains(u"Strona"_s), qPrintable(href));
    h.tw.simulateClick(tag->bounds.center());
    QVERIFY2(href.contains(u"projekt"_s), qPrintable(href));
    h.tw.simulateClick(box->bounds.center());
    QCOMPARE(toggled, 1);
}

void TestAtramentEditor::longPageLaysOutWhatIsShown()
{
    Harness h(longPage(2000));
    QCOMPARE(h.model.rowCount(), 2000);
    const PageView &v = h.view();
    // 400 px of ~30 px rows: a few dozen rows at most, not 2 000.
    QVERIFY2(v.drawnRows() > 5 && v.drawnRows() < 40, qPrintable(QString::number(v.drawnRows())));
    QVERIFY(v.rows().heights().measuredCount() < 60);
    h.tw.simulateScroll({300, 200}, {0, 30000});
    QVERIFY(v.drawnRows() < 40);
    QVERIFY(v.rows().heights().measuredCount() < 120);
    QVERIFY(h.inkRows(0, 400) > 100);
    h.tw.simulateScroll({300, 200}, {0, 1e8f});
    // The last block at the bottom: drawn, measured.
    QVERIFY(v.rows().heights().isMeasured(PageRows::treeRow(1999)));
    QVERIFY(h.inkRows(360, 400) > 5);
}

void TestAtramentEditor::foldAboveKeepsTheScreen()
{
    // A parent with children far above the view: folding it hides rows above, the screen stays.
    QString content = u"- rodzic\n"_s;
    for (int k = 0; k < 20; ++k)
        content += u"  - dziecko %1\n"_s.arg(k);
    for (int k = 0; k < 200; ++k)
        content += u"- punkt %1\n"_s.arg(k);
    Harness h(content);
    h.page.update(h.app, [](PageView &v, at::Context<PageView> &cx) {
        v.handle().scrollToRow(100);
        cx.notify();
    });
    h.tw.settle();
    QCOMPARE(h.view().handle().anchorRow(), std::size_t(100));
    const at::RasterTarget before = h.tw.image();
    h.model.setFoldingEnabled(true);
    h.model.setCollapsed(0, true);
    h.tw.settle();
    QVERIFY(!h.model.isRowVisible(5));
    QCOMPARE(h.view().handle().anchorRow(), std::size_t(100));
    const at::RasterTarget &after = h.tw.image();
    QCOMPARE(after.width, before.width);
    int differ = 0;
    for (int y = 0; y < after.height; ++y)
        for (int x = 0; x < after.width - 12; ++x) // the scroll bar moves: the page got shorter
            differ += after.at(x, y).r != before.at(x, y).r ? 1 : 0;
    QVERIFY2(differ < 50, qPrintable(QString::number(differ)));
}

void TestAtramentEditor::narrowerWindowReflows()
{
    QString content;
    for (int k = 0; k < 30; ++k)
        content += u"- Długi punkt %1: zażółć gęślą jaźń, pchnąć w tę łódź jeża lub ośm skrzyń fig\n"_s.arg(k);
    Harness h(content);
    const qreal wide = h.view().rows().heights().height(PageRows::treeRow(0));
    h.tw.resize({200, 400});
    h.tw.settle();
    const qreal narrow = h.view().rows().heights().height(PageRows::treeRow(0));
    QVERIFY2(narrow > wide * 2, qPrintable(u"%1 → %2"_s.arg(wide).arg(narrow)));
}

void TestAtramentEditor::spansCarryShapes()
{
    // The QML editor's shapes (RichTextPills): a tag a pill, inline code and a highlight boxes of
    // the palette's radius, around the glyphs; the runs of one tinted element (a highlight with a
    // bold word in it) one group, two elements two.
    markdown::RenderContext context;
    context.today = QDate(2026, 11, 27);
    const markdown::InlineRenderer renderer(context);
    const markdown::RenderedText text =
            renderer.renderRuns(u"Spotkanie #zespół i `kod` oraz ==ważne **bardzo** zdanie=="_s);
    QVERIFY(text.drawable);
    PagePalette palette;
    const std::vector<at::TextSpan> spans = renderedSpans(text, palette);
    auto shapeOf = [&](const QString &word) -> std::optional<at::TextBackground> {
        const int from = int(text.text.indexOf(word));
        for (const at::TextSpan &s : spans)
            if (s.range.start <= from && s.range.end >= from + int(word.size()) && s.style.background
                && s.style.background->has_value())
                return **s.style.background;
        return std::nullopt;
    };
    const std::optional<at::TextBackground> tag = shapeOf(u"zespół"_s);
    QVERIFY(tag && tag->pill);
    QCOMPARE(tag->extent, at::TextBackgroundExtent::Glyphs);
    QVERIFY(tag->group > 0);
    const std::optional<at::TextBackground> code = shapeOf(u"kod"_s);
    QVERIFY(code && !code->pill);
    QCOMPARE(code->radius, palette.boxRadius);
    const std::optional<at::TextBackground> before = shapeOf(u"ważne"_s);
    const std::optional<at::TextBackground> bold = shapeOf(u"bardzo"_s);
    const std::optional<at::TextBackground> after = shapeOf(u"zdanie"_s);
    QVERIFY(before && bold && after);
    QVERIFY(*before == *bold && *bold == *after); // one shape
    QVERIFY(before->group != code->group && before->group != tag->group);
    QVERIFY(spans.end() == std::find_if(spans.begin(), spans.end(), [](const at::TextSpan &s) { return s.object; }));
}

void TestAtramentEditor::chipsAndCodeDrawnAsShapes()
{
    // On the page: the tag one pill, the code and the highlight one box each (the bold word does
    // not split the highlight), all lower than the line (the glyphs' extent).
    Harness h(u"Spotkanie #zespół i `kod` oraz ==ważne **bardzo** zdanie==\n"_s);
    QCOMPARE(h.view().drawnRows(), 1);
    int pills = 0;
    int boxes = 0;
    for (const at::Primitive &p : h.tw.lastScene()->primitives()) {
        const at::Quad *q = std::get_if<at::Quad>(&p);
        if (!q || q->bounds.height() >= h.view().metrics().lineHeight || q->bounds.height() < 10)
            continue;
        if (std::abs(q->cornerRadii.topLeft - q->bounds.height() / 2) < 0.01f)
            ++pills;
        else if (q->cornerRadii.topLeft == h.view().palette().boxRadius)
            ++boxes;
    }
    QCOMPARE(pills, 1);
    QCOMPARE(boxes, 2);
}

void TestAtramentEditor::pageIconAsInlineObject()
{
    // A page's app icon: three no-break spaces in front of the link, an inline object of their
    // room with a placeholder drawn in the link's colour (no icons in Atrament yet).
    markdown::RenderContext context;
    context.pageIcon = [](const QString &target) {
        return target == u"Dom"_s ? markdown::PageIcon{markdown::PageIcon::Kind::App, u"home"_s} : markdown::PageIcon{};
    };
    const markdown::InlineRenderer renderer(context);
    const markdown::RenderedText text = renderer.renderRuns(u"Zobacz [[Dom]] i ogród"_s);
    QCOMPARE(text.objects.size(), 1);
    const std::vector<at::TextSpan> spans = renderedSpans(text, PagePalette{});
    const at::TextSpan *icon = nullptr;
    for (const at::TextSpan &s : spans)
        if (s.object)
            icon = &s;
    QVERIFY(icon);
    QCOMPARE(icon->range.start, int(text.objects[0].from));
    QCOMPARE(icon->range.end, int(text.objects[0].to));
    QCOMPARE(icon->object->size.width, kPageIconSize.width);
    QVERIFY(icon->object->size.em);

    struct View {
        std::u16string text;
        std::vector<at::TextSpan> spans;
        at::AnyElement render(at::Window &, at::Context<View> &)
        {
            return at::div()
                    .sizeFull()
                    .bg(at::white())
                    .p(10)
                    .child(at::div()
                                   .w(400)
                                   .textSize(16)
                                   .lineHeight(24)
                                   .fontFamily(u"Brulion Inter")
                                   .child(at::text(text).spans(spans)));
        }
    };
    auto platform = std::make_shared<at::TestPlatform>();
    at::App app(platform, loadFonts());
    at::TestWindow &tw = platform->newWindow({420, 60}, 1);
    at::Entity<View> view = app.newEntity<View>();
    view.update(app, [&](View &v, at::Context<View> &) {
        v.text = std::u16string(u16View(text.text));
        v.spans = spans;
    });
    app.openWindow(tw, view);
    tw.settle();
    const at::Color colour = cssColor(text.objects[0].color, PagePalette{}.link);
    int placeholders = 0;
    for (const at::Primitive &p : tw.lastScene()->primitives()) {
        const at::Quad *q = std::get_if<at::Quad>(&p);
        if (!q || q->borderColor != colour)
            continue;
        ++placeholders;
        // A square of 9/10 of the room (0.84 em of 16 px), inside the 24 px line.
        QVERIFY2(std::abs(q->bounds.width() - 12) <= 1 && std::abs(q->bounds.height() - 12) <= 1,
                 qPrintable(u"%1 × %2"_s.arg(q->bounds.width()).arg(q->bounds.height())));
        QVERIFY(q->bounds.top() >= 10 && q->bounds.bottom() <= 34);
    }
    QCOMPARE(placeholders, 1);
}

QTEST_MAIN(TestAtramentEditor)
#include "tst_atrament_editor.moc"
