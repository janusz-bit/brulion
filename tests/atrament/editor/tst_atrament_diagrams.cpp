// Charts, Mermaid diagrams, math formulas and page icons drawn by Atrament in the block editor
// (src/app/editor/atrament/diagramrows.cpp, mathrows.cpp, diagrampaint.cpp, pageicons.cpp;
// docs/ATRAMENT.md "The block editor in Atrament (M2)", step 5): a page of a PageController over
// FakeEditorHost drawn headless — the rows Atrament's, their scenes painted as path primitives in
// the palette's colours and the labels as text, the height following the width, the error state
// with its translated reason under the source, the text alternative for screen readers, a click
// editing the source with the diagram drawn under it, Enter a line break, Esc and ↑/↓ leaving it;
// formulas drawn when Atrament's text draws them (scripts raised, the radicand overlined) and
// edited as LaTeX; a page's app icon drawn from BIcon's SVG. Polish text throughout.
#include "blocklistmodel.h"
#include "diagrampaint.h"
#include "fakeeditorhost.h"
#include "pagecontroller.h"
#include "pageicons.h"
#include "pageview.h"
#include "renderedtextitem.h"
#include "markdown/inlinerenderer.h"
#include "text/fonts.h"
#include "ui/testplatform.h"

#include <QImage>
#include <QTest>

#include <cstring>

#include <memory>

using namespace brulion;
using namespace brulion::editor;
using namespace brulion::editor::atr;
using namespace Qt::StringLiterals;
namespace at = brulion::atrament;

namespace {

const QString PagePath = u"Notatki/Wykresy.md"_s;

std::shared_ptr<at::FontCollection> loadFonts()
{
    static std::shared_ptr<at::FontCollection> fonts = [] {
        auto f = std::make_shared<at::FontCollection>();
        const std::string dir = BRULION_SOURCE_DIR "/assets/fonts/";
        for (const char *file : {"ui/BrulionInter-Regular.ttf", "ui/BrulionInter-Bold.ttf", "ui/BrulionInter-Italic.ttf",
                                 "ui/BrulionJetBrainsMono-Regular.ttf", "BrulionEmoji.ttf", "BrulionMath.otf"})
            f->addFontFile(dir + file);
        f->setFallbackFamilies({u"Brulion Emoji", u"Brulion Math", u"Brulion Inter"});
        return f;
    }();
    return fonts;
}

QString str(std::u16string_view s)
{
    return QString(reinterpret_cast<const QChar *>(s.data()), qsizetype(s.size()));
}

const QString Chart = u"Wydatki:\n\n| Miesiąc | Kwota |\n|---|---|\n| Styczeń | 120 |\n| Luty | 80 |\n\n"
                      "```chart\ntype: bar\ntitle: Wydatki źdźbła\n```\n"_s;
const QString Flowchart = u"```mermaid\ngraph TD\n  A[Początek] --> B{Decyzja}\n  B -->|tak| C[Gęś]\n"
                          "  B -.->|nie| D[Koniec]\n```\n"_s;

struct Harness {
    FakeEditorHost host;
    PageController controller;
    std::shared_ptr<at::TestPlatform> platform = std::make_shared<at::TestPlatform>();
    at::App app{platform, loadFonts()};
    at::TestWindow &tw;
    at::Entity<PageView> page;
    at::Window *window = nullptr;

    explicit Harness(const QString &content, at::Size size = {600, 500}, bool diagrams = true, bool math = true)
        : tw(platform->newWindow(size, 1))
    {
        host.setFile(PagePath, content);
        controller.setNow(u"2026-11-27 10:00"_s);
        controller.setSaveDelay(1000000);
        controller.setHost(&host);
        controller.setPath(PagePath);
        page = app.newEntity<PageView>(controller.blocks());
        page.update(app, [&](PageView &v, at::Context<PageView> &cx) {
            v.setController(&controller, cx);
            v.setViewLook(PageView::ViewLook{}, cx);
            v.setDrawsDiagrams(diagrams, cx);
            v.setDrawsMath(math, cx);
        });
        window = &app.openWindow(tw, page);
        tw.settle();
    }
    BlockListModel *model() const { return controller.blocks(); }
    const PageView &view() { return page.read(app); }
    int rowOf(const QString &viewName)
    {
        for (int r = 0; r < model()->rowCount(); ++r)
            if (model()->index(r, 0).data(BlockListModel::ViewRole).toString() == viewName)
                return r;
        return -1;
    }
    QString inputText() { return view().input() ? str(view().input()->read(app).text()) : QString(); }
    int inputCaret() { return view().input() ? view().input()->read(app).selection().focus : -1; }
    at::Rect bounds(int row) { return view().rowBounds(row).value_or(at::Rect{}); }
    // The path primitives of the last frame inside window rectangle `r`.
    std::vector<const at::PathPrimitive *> pathsIn(at::Rect r)
    {
        std::vector<const at::PathPrimitive *> out;
        for (const at::Primitive &p : tw.lastScene()->primitives())
            if (const auto *path = std::get_if<at::PathPrimitive>(&p); path && path->bounds.intersects(r))
                out.push_back(path);
        return out;
    }
    // The frame as a picture (BRULION_ATRAMENT_DUMP=<dir>: a look at what the tests draw).
    void dump(const char *name) const
    {
        const QString dir = qEnvironmentVariable("BRULION_ATRAMENT_DUMP");
        if (dir.isEmpty())
            return;
        const at::RasterTarget &t = tw.image();
        QImage image(t.width, t.height, QImage::Format_RGBA8888_Premultiplied);
        for (int y = 0; y < t.height; ++y)
            std::memcpy(image.scanLine(y), &t.pixels[std::size_t(y) * std::size_t(t.width)], std::size_t(t.width) * 4);
        image.save(dir + u'/' + QLatin1String(name) + u".png"_s);
    }
    // Monochrome sprites (glyphs) inside `r`.
    int glyphsIn(at::Rect r)
    {
        int n = 0;
        for (const at::Primitive &p : tw.lastScene()->primitives())
            if (const auto *g = std::get_if<at::MonochromeSprite>(&p); g && g->bounds.intersects(r))
                ++n;
        return n;
    }
};

// The palette PageView paints a diagram with for its default look (diagram::themedPalette).
DiagramColors defaultColors()
{
    const PageView::ViewLook look;
    auto hex = [](at::Color c) {
        return QStringLiteral("#%1%2%3")
                .arg(int(std::lround(c.r * 255)), 2, 16, QLatin1Char('0'))
                .arg(int(std::lround(c.g * 255)), 2, 16, QLatin1Char('0'))
                .arg(int(std::lround(c.b * 255)), 2, 16, QLatin1Char('0'));
    };
    diagram::ThemeColors t;
    t.text = hex(look.text);
    t.mutedText = hex(look.muted);
    t.background = hex(look.panel);
    t.border = hex(look.border);
    t.accent = hex(look.accent);
    return DiagramColors::from(diagram::themedPalette(t));
}

bool near(at::Color a, at::Color b)
{
    return std::abs(a.r - b.r) < 0.01f && std::abs(a.g - b.g) < 0.01f && std::abs(a.b - b.b) < 0.01f
           && std::abs(a.a - b.a) < 0.01f;
}

} // namespace

class TestAtramentDiagrams : public QObject {
    Q_OBJECT
private Q_SLOTS:
    void diagramColours();
    void switchesKeepTheQmlViews();
    void mermaidDrawnFromItsScene();
    void chartDrawnWithItsNumbers();
    void heightFollowsWidth();
    void errorShowsSourceAndReason();
    void textAlternativeForScreenReaders();
    void clickEditsTheSource();
    void clickOnTheShownSourcePlacesTheCaret();
    void formulasDrawn();
    void formulasAtramentCannotDrawStayForeign();
    void formulaEditedAsLatex();
    void pageIconDrawnFromItsSvg();
};

void TestAtramentDiagrams::diagramColours()
{
    QVERIFY(near(*diagramColor(u"#336699"), at::rgb(0x336699)));
    QVERIFY(near(*diagramColor(u"#33669980"), at::rgba(0x33669980))); // CSS's order, not QColor's
    QVERIFY(!diagramColor(u"336699"));
    QVERIFY(!diagramColor(u"#33669"));
    QVERIFY(!diagramColor(u"#gg6699"));
    const DiagramColors c = defaultColors();
    QVERIFY(near(c.of(diagram::Paint{diagram::Role::None, {}}), at::transparent()));
    QVERIFY(near(c.of(diagram::Paint{diagram::Role::NodeFill, u"#ff0000"_s}), at::rgb(0xff0000)));
    QVERIFY(!c.of(diagram::Paint{diagram::Role::NodeStroke, {}}).isTransparent());
    // Light and dark differ.
    diagram::ThemeColors dark;
    dark.dark = true;
    dark.background = u"#16171b"_s;
    dark.text = u"#d3d8e2"_s;
    const DiagramColors d = DiagramColors::from(diagram::themedPalette(dark));
    QVERIFY(!near(d.roles[int(diagram::Role::NodeText)], c.roles[int(diagram::Role::NodeText)]));
}

void TestAtramentDiagrams::switchesKeepTheQmlViews()
{
    Harness off(Flowchart + u"\n```math\nx^2\n```\n"_s, {600, 500}, false, false);
    const int mermaid = off.rowOf(u"mermaid"_s);
    const int math = off.rowOf(u"math"_s);
    QVERIFY(mermaid >= 0 && math >= 0);
    QVERIFY(!off.view().drawsRow(mermaid));
    QVERIFY(!off.view().drawsRow(math));
    const std::vector<std::size_t> foreign = off.view().foreignRows();
    QVERIFY(std::find(foreign.begin(), foreign.end(), std::size_t(mermaid)) != foreign.end());
    QVERIFY(!off.view().editsRow(mermaid));
    // On: Atrament's.
    off.page.update(off.app, [](PageView &v, at::Context<PageView> &cx) {
        v.setDrawsDiagrams(true, cx);
        v.setDrawsMath(true, cx);
    });
    off.tw.settle();
    QVERIFY(off.view().drawsRow(mermaid));
    QVERIFY(off.view().drawsRow(math));
    QVERIFY(off.view().editsRow(mermaid));
    QVERIFY(off.view().foreignRows().empty());
}

void TestAtramentDiagrams::mermaidDrawnFromItsScene()
{
    Harness h(u"Akapit nad schematem.\n\n"_s + Flowchart);
    const int row = h.rowOf(u"mermaid"_s);
    QVERIFY(row >= 0);
    QVERIFY(h.view().drawsRow(row));
    h.dump("mermaid");
    const PageView::ViewRowInfo info = h.view().viewRowInfo(row);
    QCOMPARE(info.kind, u"mermaid"_s);
    QCOMPARE(info.status, u"ok"_s);
    QCOMPARE(info.name, u"Flowchart"_s);
    QVERIFY(info.size.width > 50 && info.size.height > 100);
    const at::Rect b = h.bounds(row);
    QVERIFY(b.height() > 100);
    // The scene's paths in the palette's colours: nodes filled and outlined, lines stroked (the
    // dotted link in pieces), arrowheads filled.
    const DiagramColors colors = defaultColors();
    const at::Color nodeFill = colors.roles[int(diagram::Role::NodeFill)];
    const at::Color nodeStroke = colors.roles[int(diagram::Role::NodeStroke)];
    const at::Color line = colors.roles[int(diagram::Role::Line)];
    int fills = 0, strokes = 0, lines = 0, multi = 0;
    for (const at::PathPrimitive *p : h.pathsIn(b)) {
        fills += near(p->color, nodeFill) ? 1 : 0;
        strokes += near(p->color, nodeStroke) ? 1 : 0;
        lines += near(p->color, line) ? 1 : 0;
        multi += near(p->color, line) && p->contourEnds.size() > 20 ? 1 : 0;
    }
    QVERIFY2(fills >= 4, qPrintable(QString::number(fills))); // four nodes
    QVERIFY(strokes >= 4);
    QVERIFY(lines >= 3);
    QVERIFY(multi >= 1); // the dotted link: one contour per dot and dash
    // Every label drawn as text inside the row (its glyphs), and the panel under it.
    QVERIFY(h.glyphsIn(b) >= int(QStringLiteral("PoczątekDecyzjaGęśKoniectaknie").size()));
    const at::Pixel panel = h.tw.pixel(int(b.left()) + 40, int(b.top()) + 14);
    const at::Color expected = PageView::ViewLook{}.panel;
    QVERIFY(std::abs(panel.r - expected.r * 255) < 3 && std::abs(panel.b - expected.b * 255) < 3);
    // Centred in the panel.
    float left = 1e9f, right = -1e9f;
    for (const at::PathPrimitive *p : h.pathsIn(b)) {
        left = std::min(left, p->bounds.left());
        right = std::max(right, p->bounds.right());
    }
    QVERIFY2(std::abs((left + right) / 2 - (b.left() + 16 + b.right() - 16) / 2) < 30,
             qPrintable(u"%1 … %2 in %3 … %4"_s.arg(left).arg(right).arg(b.left()).arg(b.right())));
}

void TestAtramentDiagrams::chartDrawnWithItsNumbers()
{
    Harness h(Chart);
    const int row = h.rowOf(u"chart"_s);
    QVERIFY(row >= 0);
    QVERIFY2(h.model()->index(row, 0).data(BlockListModel::ChartDataRole).toMap().value(u"ok"_s).toBool(),
             qPrintable(h.model()->index(row, 0).data(BlockListModel::ChartDataRole).toMap().value(u"error"_s).toString()));
    h.dump("chart");
    const PageView::ViewRowInfo info = h.view().viewRowInfo(row);
    QCOMPARE(info.status, u"ok"_s);
    QCOMPARE(info.name, u"Bar chart"_s);
    QVERIFY2(info.description.contains(u"Styczeń"_s) && info.description.contains(u"120"_s), qPrintable(info.description));
    QVERIFY(info.description.contains(u"Wydatki źdźbła"_s));
    // Bars in the first series' colour.
    const at::Rect b = h.bounds(row);
    const at::Color bar = defaultColors().roles[int(diagram::Role::Slice0)];
    int bars = 0;
    for (const at::PathPrimitive *p : h.pathsIn(b))
        bars += near(p->color, bar) ? 1 : 0;
    QVERIFY2(bars >= 2, qPrintable(QString::number(bars)));
    // The labels and the title as text.
    QVERIFY(h.glyphsIn(b) > 20);
}

void TestAtramentDiagrams::heightFollowsWidth()
{
    // A chart is laid out at the column's width: narrower, its scene (and the row) changes height.
    Harness wide(Chart, {700, 600});
    Harness narrow(Chart, {360, 600});
    const int row = wide.rowOf(u"chart"_s);
    const float hw = wide.bounds(row).height();
    const float hn = narrow.bounds(row).height();
    QVERIFY2(std::abs(hw - hn) > 4, qPrintable(u"%1 vs %2"_s.arg(hw).arg(hn)));
    // A diagram wider than its column is scaled down — to 70 % at most, beyond that it scrolls.
    QString wideGraph = u"```mermaid\ngraph LR\n"_s;
    for (int i = 0; i < 9; ++i)
        wideGraph += u"  N%1[Węzeł numer %1] --> N%2[Węzeł numer %2]\n"_s.arg(i).arg(i + 1);
    wideGraph += u"```\n"_s;
    Harness big(wideGraph, {700, 600});
    Harness small(wideGraph, {360, 600});
    const int m = big.rowOf(u"mermaid"_s);
    const PageView::ViewRowInfo info = big.view().viewRowInfo(m);
    QVERIFY(info.size.width > 700); // wider than both windows
    float k = 0;
    const at::Size atSmall = DiagramElement::sizeAt(
            diagram::Scene{QSizeF(info.size.width, info.size.height), {}, {}}, 360 - 64, 0.7f, &k);
    QCOMPARE(k, 0.7f);
    QVERIFY(atSmall.width > 360);
    QVERIFY(small.bounds(m).height() <= big.bounds(m).height() + 0.5f);
    // The row: the panel's 12 px above the diagram and under it, its border, the row's padding and
    // a framed block's air in an outline (8 px above and below with the row's 3).
    QVERIFY2(std::abs(small.bounds(m).height() - (std::ceil(info.size.height * 0.7f) + 42)) < 2,
             qPrintable(u"%1 for %2"_s.arg(small.bounds(m).height()).arg(info.size.height)));
}

void TestAtramentDiagrams::errorShowsSourceAndReason()
{
    Harness h(u"```mermaid\ngraph TD\n  A[niezamknięty\n```\n\n```mermaid\nclassDiagram\n  class Gęś\n```\n"_s);
    const int broken = 0;
    const int unsupported = h.rowOf(u"mermaid"_s) == 0 ? 1 : 0;
    QCOMPARE(h.model()->index(unsupported, 0).data(BlockListModel::ViewRole).toString(), u"mermaid"_s);
    h.dump("errors");
    PageView::ViewRowInfo info = h.view().viewRowInfo(broken);
    QCOMPARE(info.status, u"error"_s);
    QVERIFY2(info.message.startsWith(u"Line 2"_s), qPrintable(info.message));
    // The source shown as written, the reason under it.
    const at::TextLayoutHandle *source = h.view().rowText(broken);
    QVERIFY(source && source->layout());
    QCOMPARE(str(source->layout()->text()), u"graph TD\n  A[niezamknięty"_s);
    QVERIFY(h.pathsIn(h.bounds(broken)).empty());
    info = h.view().viewRowInfo(unsupported);
    QCOMPARE(info.status, u"unsupported"_s);
    QVERIFY2(info.message.contains(u"classDiagram"_s), qPrintable(info.message));
    // The message reaches screen readers as text.
    h.window->setAccessibilityEnabled(true);
    h.tw.settle();
    const QString dump = str(h.window->accessibilityTree().dump());
    QVERIFY2(dump.contains(u"Line 2"_s), qPrintable(dump));
}

void TestAtramentDiagrams::textAlternativeForScreenReaders()
{
    Harness h(Chart + u"\n"_s + Flowchart);
    h.window->setAccessibilityEnabled(true);
    h.tw.settle();
    const QString dump = str(h.window->accessibilityTree().dump());
    QVERIFY2(dump.contains(u"image \"Bar chart\" ("_s), qPrintable(dump));
    QVERIFY(dump.contains(u"Styczeń"_s));
    QVERIFY2(dump.contains(u"image \"Flowchart\""_s), qPrintable(dump));
    // A diagram's title names it.
    Harness titled(u"```mermaid\n---\ntitle: Obieg gęsi\n---\ngraph TD\n  A --> B\n```\n"_s);
    titled.window->setAccessibilityEnabled(true);
    titled.tw.settle();
    QVERIFY(str(titled.window->accessibilityTree().dump()).contains(u"image \"Obieg gęsi\""_s));
}

void TestAtramentDiagrams::clickEditsTheSource()
{
    Harness h(u"Akapit.\n\n"_s + Flowchart + u"\nKoniec.\n"_s);
    const int row = h.rowOf(u"mermaid"_s);
    const at::Rect b = h.bounds(row);
    h.tw.simulateClick({b.left() + b.width() / 2, b.top() + b.height() / 2});
    h.tw.settle();
    QCOMPARE(h.controller.editingRow(), row);
    const QString source = h.model()->editTextAt(row);
    QCOMPARE(h.inputText(), source);
    QCOMPARE(h.inputCaret(), int(source.size())); // the QML view's startEditing(row, -1)
    h.dump("mermaid-edited");
    // The source in the code highlighter's colours, the diagram still drawn under it.
    QVERIFY(h.view().drawsRow(row));
    const at::Rect edited = h.bounds(row);
    QVERIFY(edited.height() > b.height()); // the source above the diagram
    QVERIFY(h.pathsIn(edited).size() > 8);
    // Typing changes the diagram as it is typed; Enter is a line break (no new block).
    const int blocks = h.model()->rowCount();
    h.tw.simulateKeystrokes(u"enter");
    h.tw.simulateTyping(u"  D --> E[Nowy węzeł]");
    h.tw.settle();
    QCOMPARE(h.model()->rowCount(), blocks);
    QVERIFY(h.model()->editTextAt(row).endsWith(u"\n  D --> E[Nowy węzeł]"_s));
    QCOMPARE(h.view().viewRowInfo(row).status, u"ok"_s);
    // Tab is a tab; Esc stops editing; the picture alone again.
    h.tw.simulateKeystrokes(u"tab");
    QVERIFY(h.model()->editTextAt(row).endsWith(u"\t"_s));
    h.tw.simulateKeystrokes(u"escape");
    h.tw.settle();
    QCOMPARE(h.controller.editingRow(), -1);
    // ↑ from the source's first line goes to the block above.
    h.controller.startEditing(row, 0);
    h.tw.settle();
    QCOMPARE(h.inputCaret(), 0);
    h.tw.simulateKeystrokes(u"up");
    QCOMPARE(h.controller.editingRow(), row - 1);
    // A chart's source too.
    Harness c(Chart);
    const int chart = c.rowOf(u"chart"_s);
    const at::Rect cb = c.bounds(chart);
    c.tw.simulateClick({cb.left() + cb.width() / 2, cb.top() + cb.height() / 2});
    c.tw.settle();
    QCOMPARE(c.controller.editingRow(), chart);
    QCOMPARE(c.inputText(), u"type: bar\ntitle: Wydatki źdźbła"_s);
    c.tw.simulateKeystrokes(u"ctrl-home");
    c.tw.simulateKeystrokes(u"shift-end");
    c.tw.simulateTyping(u"type: pie");
    c.tw.settle();
    QCOMPARE(c.view().viewRowInfo(chart).name, u"Pie chart"_s);
}

void TestAtramentDiagrams::clickOnTheShownSourcePlacesTheCaret()
{
    Harness h(u"```mermaid\ngraph TD\n  A[niezamknięty\n```\n"_s);
    const at::TextLayoutHandle *source = h.view().rowText(0);
    QVERIFY(source && source->layout());
    // Before "TD" (offset 6 of the source, shown as written).
    const at::Rect caret = source->layout()->caretRect(6).translated(source->bounds().origin);
    h.tw.simulateClick({caret.left() + 0.5f, caret.top() + caret.height() / 2});
    h.tw.settle();
    QCOMPARE(h.controller.editingRow(), 0);
    QCOMPARE(h.inputCaret(), 6);
}

void TestAtramentDiagrams::formulasDrawn()
{
    Harness h(u"Wzór:\n\n```math\nx^2 + \\sqrt{ab} = \\frac{1}{2}\n```\n"_s);
    const int row = h.rowOf(u"math"_s);
    QVERIFY(row >= 0);
    QVERIFY(h.view().drawsRow(row));
    h.dump("math");
    const auto rendered = h.model()->index(row, 0).data(BlockListModel::RenderedTextRole).value<RenderedTextRef>();
    QVERIFY(rendered.d);
    QVERIFY(PageView::mathDrawable(*rendered.d));
    QCOMPARE(h.view().viewRowInfo(row).name, u"x2 + √ab = ½"_s);
    // The spans: "2" smaller and raised, "ab" overlined, "x" italic.
    const std::vector<at::TextSpan> spans = PageView::mathSpans(*rendered.d, 16, PagePalette{}, u"Brulion Inter");
    auto spanAt = [&](int offset) -> const at::TextStyleRefinement * {
        for (const at::TextSpan &s : spans)
            if (s.range.start <= offset && s.range.end > offset)
                return &s.style;
        return nullptr;
    };
    const at::TextStyleRefinement *two = spanAt(1);
    QVERIFY(two && two->baselineShift && *two->baselineShift > 0 && two->fontSize && *two->fontSize < 16);
    const at::TextStyleRefinement *radicand = spanAt(6);
    QVERIFY(radicand && radicand->overline && *radicand->overline);
    const at::TextStyleRefinement *x = spanAt(0);
    QVERIFY(x && x->fontStyle == at::FontStyle::Italic);
    // Drawn centred in its panel: its glyphs about the panel's middle.
    const at::Rect b = h.bounds(row);
    float left = 1e9f, right = -1e9f;
    for (const at::Primitive &p : h.tw.lastScene()->primitives())
        if (const auto *g = std::get_if<at::MonochromeSprite>(&p); g && g->bounds.intersects(b)) {
            left = std::min(left, g->bounds.left());
            right = std::max(right, g->bounds.right());
        }
    QVERIFY(right > left);
    const float middle = (b.left() + h.view().metrics().gutter + b.right()) / 2;
    QVERIFY2(std::abs((left + right) / 2 - middle) < 12, qPrintable(u"%1 vs %2"_s.arg((left + right) / 2).arg(middle)));
    // A subscript lowered; \texttt in the code face.
    markdown::RenderedText sub = markdown::renderedTextFromHtml(u"a<sub>i</sub> <span style=\"font-family:monospace\">kod</span>"_s);
    QVERIFY(PageView::mathDrawable(sub));
    const std::vector<at::TextSpan> subSpans = PageView::mathSpans(sub, 20, PagePalette{}, u"Brulion Inter");
    bool lowered = false, mono = false;
    for (const at::TextSpan &s : subSpans) {
        lowered = lowered || (s.style.baselineShift && *s.style.baselineShift < 0);
        mono = mono || s.style.fontFamily == PagePalette{}.codeFamily;
    }
    QVERIFY(lowered && mono);
}

void TestAtramentDiagrams::formulasAtramentCannotDrawStayForeign()
{
    // Small capitals (\textsc): the QML view's.
    Harness h(u"```math\n\\textsc{Gęś} + x\n```\n"_s);
    QCOMPARE(h.model()->index(0, 0).data(BlockListModel::ViewRole).toString(), u"math"_s);
    QVERIFY(!h.view().drawsRow(0));
    QVERIFY(!h.view().editsRow(0));
    const std::vector<std::size_t> foreign = h.view().foreignRows();
    QCOMPARE(foreign.size(), std::size_t(1));
    QVERIFY(!PageView::mathDrawable(markdown::renderedTextFromHtml(u"<a href=\"x\">a</a>"_s)));
    QVERIFY(!PageView::mathDrawable(markdown::renderedTextFromHtml(u"<span style=\"color:red\">a</span>"_s)));
    // The QML view edits it, and keeps it while what is typed would be drawable.
    h.controller.startEditing(0, -1);
    h.tw.settle();
    QVERIFY(!h.view().drawsRow(0));
    QVERIFY(!h.view().input() || h.view().editedRow() != 0);
}

void TestAtramentDiagrams::formulaEditedAsLatex()
{
    Harness h(u"Akapit.\n\n```math\nE = mc^2\n```\n"_s);
    const int row = h.rowOf(u"math"_s);
    const at::Rect b = h.bounds(row);
    h.tw.simulateClick({b.left() + b.width() / 2, b.top() + b.height() / 2});
    h.tw.settle();
    QCOMPARE(h.controller.editingRow(), row);
    QCOMPARE(h.inputText(), u"E = mc^2"_s);
    // Enter: a line break of the formula.
    const int blocks = h.model()->rowCount();
    h.tw.simulateKeystrokes(u"enter");
    h.tw.simulateTyping(u"\\textsc{ź}");
    h.tw.settle();
    QCOMPARE(h.model()->rowCount(), blocks);
    QCOMPARE(h.model()->editTextAt(row), u"E = mc^2\n\\textsc{ź}"_s);
    // What it became cannot be drawn: still Atrament's while it is edited, the QML view's after.
    QVERIFY(h.view().drawsRow(row));
    h.tw.simulateKeystrokes(u"escape");
    h.tw.settle();
    QCOMPARE(h.controller.editingRow(), -1);
    QVERIFY(!h.view().drawsRow(row));
}

void TestAtramentDiagrams::pageIconDrawnFromItsSvg()
{
    setPageIconFolder(QStringLiteral(BRULION_SOURCE_DIR "/assets/icons"));
    QVERIFY(pageIcon(u"house"_s));
    QVERIFY(!pageIcon(u"../icons/house"_s)); // never outside the folder
    QVERIFY(!pageIcon(u"nie-ma-takiej"_s));
    markdown::RenderContext context;
    context.pageIcon = [](const QString &target) {
        return target == u"Dom"_s ? markdown::PageIcon{markdown::PageIcon::Kind::App, u"house"_s} : markdown::PageIcon{};
    };
    const markdown::InlineRenderer renderer(context);
    const markdown::RenderedText text = renderer.renderRuns(u"Zobacz [[Dom]] i ogród"_s);
    const std::vector<at::TextSpan> spans = renderedSpans(text, PagePalette{});
    struct View {
        std::u16string text;
        std::vector<at::TextSpan> spans;
        at::AnyElement render(at::Window &, at::Context<View> &)
        {
            return at::div().sizeFull().bg(at::white()).p(10).child(
                    at::div().w(400).textSize(16).lineHeight(24).fontFamily(u"Brulion Inter").child(at::text(text).spans(spans)));
        }
    };
    auto platform = std::make_shared<at::TestPlatform>();
    at::App app(platform, loadFonts());
    at::TestWindow &tw = platform->newWindow({420, 60}, 2);
    at::Entity<View> view = app.newEntity<View>();
    view.update(app, [&](View &v, at::Context<View> &) {
        v.text = std::u16string(u16View(text.text));
        v.spans = spans;
    });
    app.openWindow(tw, view);
    tw.settle();
    if (const QString dir = qEnvironmentVariable("BRULION_ATRAMENT_DUMP"); !dir.isEmpty()) {
        QImage image(tw.image().width, tw.image().height, QImage::Format_RGBA8888_Premultiplied);
        for (int y = 0; y < image.height(); ++y)
            std::memcpy(image.scanLine(y), &tw.image().pixels[std::size_t(y) * std::size_t(image.width())],
                        std::size_t(image.width()) * 4);
        image.save(dir + u"/icon.png"_s);
    }
    // No placeholder square: the house's strokes as a path in the link's colour, in the 12 px
    // square (24 device px at scale 2) inside the line.
    const at::Color colour = cssColor(text.objects[0].color, PagePalette{}.link);
    int placeholders = 0;
    int icons = 0;
    for (const at::Primitive &p : tw.lastScene()->primitives()) {
        if (const at::Quad *q = std::get_if<at::Quad>(&p); q && q->borderColor == colour)
            ++placeholders;
        if (const auto *path = std::get_if<at::PathPrimitive>(&p); path && near(path->color, colour)) {
            ++icons;
            QVERIFY2(path->bounds.width() <= 25 && path->bounds.height() <= 25 && path->bounds.width() > 15,
                     qPrintable(u"%1 × %2"_s.arg(path->bounds.width()).arg(path->bounds.height())));
            QVERIFY(path->bounds.top() >= 20 && path->bounds.bottom() <= 68);
        }
    }
    QCOMPARE(placeholders, 0);
    QCOMPARE(icons, 1);
    setPageIconFolder(QStringLiteral(":/icons"));
}

QTEST_MAIN(TestAtramentDiagrams)
#include "tst_atrament_diagrams.moc"
