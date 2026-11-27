// The block editor drawn by Atrament against M2's gate (docs/ATRAMENT.md "Roadmap": memory per
// block < 2 KB, scrolling a 1 000-block page at 120 fps on the desktop): a generated 1 000-block
// page (paragraphs, nested list items, tasks, headings, links, tags, emphasis, long wrapped items)
// in a PageView on Atrament's test platform at 1280 × 800.
//
//   * memoryPerBlock — the heap (glibc's mallinfo2) the model takes for the page, what the view
//     adds when it opens, and what stays per block once every row was shown (the wheel through
//     the whole page): the model's rendered-text caches and the view's own.
//   * scrollFrames — the wheel through the page in steps of 120 px: per step the dispatch, the
//     frame built (render, layout, prepaint, paint: the scene) and, in a second pass, rasterized
//     by the CPU renderer; median, p95 and the slowest step. 120 fps leaves 8.3 ms a frame.
//   * typing — a keystroke in a block in the page's middle: the field, PageController's
//     setEditText, the model's row, the frame.
//   * everyKind — a page with code, tables, calculations, diagrams, formulas, rules and callouts
//     between the text blocks: the rows left foreign, memory per block, the scroll steps.
//
// Release builds only (scripts/bench.sh); the numbers go to stdout as `[bench] …` lines.
#include "blocklistmodel.h"
#include "fakeeditorhost.h"
#include "pagecontroller.h"
#include "pageview.h"
#include "text/fonts.h"
#include "ui/testplatform.h"

#include <QElapsedTimer>
#include <QTest>

#include <algorithm>
#include <memory>
#include <vector>

#if defined(__GLIBC__)
#include <malloc.h>
#endif

using namespace brulion;
using namespace brulion::editor;
using namespace brulion::editor::atr;
using namespace Qt::StringLiterals;
namespace at = brulion::atrament;

namespace {

std::shared_ptr<at::FontCollection> loadFonts()
{
    static std::shared_ptr<at::FontCollection> fonts = [] {
        auto f = std::make_shared<at::FontCollection>();
        const std::string dir = BRULION_SOURCE_DIR "/assets/fonts/";
        for (const char *file : {"ui/BrulionInter-Regular.ttf", "ui/BrulionInter-Bold.ttf", "ui/BrulionInter-Italic.ttf",
                                 "ui/BrulionJetBrainsMono-Regular.ttf", "BrulionEmoji.ttf"})
            f->addFontFile(dir + file);
        f->setFallbackFamilies({u"Brulion Emoji", u"Brulion Inter"});
        return f;
    }();
    return fonts;
}

std::size_t heapInUse()
{
#if defined(__GLIBC__)
    return mallinfo2().uordblks;
#else
    return 0;
#endif
}

// A 1 000-block page as people write them (no randomness: the same page everywhere).
QString page(int blocks)
{
    QStringList lines;
    for (int k = 0; k < blocks; ++k) {
        if (k % 50 == 0)
            lines << u"## Rozdział %1: zażółć gęślą jaźń"_s.arg(k / 50 + 1);
        else if (k % 9 == 4)
            lines << u"  - zagnieżdżony punkt %1: źdźbło trawy, `kod` i **gęś**"_s.arg(k);
        else if (k % 13 == 6)
            lines << u"- %1 Długi punkt, który zawija się na kilka linii: zażółć gęślą jaźń, pchnąć w tę łódź jeża "
                     "lub ośm skrzyń fig, a potem jeszcze raz to samo, bo tak się pisze notatki."_s.arg(k);
        else if (k % 7 == 3)
            lines << u"- [ ] Zadanie %1 do zrobienia z [[Stroną]] i #tagiem"_s.arg(k);
        else if (k % 11 == 5)
            lines << u"- [x] Zrobione %1 ~~dawno~~"_s.arg(k);
        else
            lines << u"- punkt %1 o [[Stronie %2]] i #tag%3, *kursywą* oraz ==ważne=="_s.arg(k).arg(k % 17).arg(k % 5);
    }
    return lines.join(u'\n') + u'\n';
}

// The same page with blocks of every kind Atrament draws (M2 step 5): code, tables, calculations,
// Mermaid diagrams, formulas, rules, callouts between the text blocks.
QString mixedPage(int blocks)
{
    QStringList lines;
    for (int k = 0; k < blocks; ++k) {
        if (k % 20 == 7)
            lines << u"\n```luau\nlocal gęś = %1\nfor i = 1, 10 do print(i * gęś) end\n```\n"_s.arg(k);
        else if (k % 33 == 11)
            lines << u"\n| Pozycja | Ilość | Cena |\n| --- | ---: | ---: |\n| **Płytki** %1 | 6,5 | 89 |\n| Klej | 4 | 45 |\n"_s.arg(k);
        else if (k % 50 == 13)
            lines << u"\n```calc\npowierzchnia = 6,5\ncena = 89 zł\npowierzchnia * cena\n```\n"_s;
        else if (k % 71 == 17)
            lines << u"\n```mermaid\ngraph TD\n  A[Zakupy %1] --> B{Płytki?}\n  B --> C[Kuchnia]\n```\n"_s.arg(k);
        else if (k % 60 == 19)
            lines << u"\n```math\nx_%1^2 + \\sqrt{y} = z\n```\n"_s.arg(k % 9);
        else if (k % 45 == 23)
            lines << u"\n---\n"_s;
        else if (k % 40 == 29)
            lines << u"\n> [!tip] Wskazówka %1\n> Do płytek dolicz **10%** zapasu.\n"_s.arg(k);
        else
            lines << u"- punkt %1 o [[Stronie %2]] i #tag%3, *kursywą* oraz ==ważne=="_s.arg(k).arg(k % 17).arg(k % 5);
    }
    return lines.join(u'\n') + u'\n';
}

struct Percentiles {
    double median = 0;
    double p95 = 0;
    double max = 0;
};

Percentiles percentiles(std::vector<double> v)
{
    if (v.empty())
        return {};
    std::sort(v.begin(), v.end());
    auto at = [&](double q) { return v[std::min(v.size() - 1, std::size_t(q * double(v.size() - 1) + 0.5))]; };
    return {at(0.5), at(0.95), v.back()};
}

struct Harness {
    FakeEditorHost host;
    PageController controller;
    std::shared_ptr<at::TestPlatform> platform = std::make_shared<at::TestPlatform>();
    at::App app{platform, loadFonts()};
    at::TestWindow &tw;
    at::Entity<PageView> view;

    explicit Harness(const QString &content) : tw(platform->newWindow({1280, 800}, 1))
    {
        host.setFile(u"Strona.md"_s, content);
        controller.setNow(u"2026-11-27 10:00"_s);
        controller.setSaveDelay(1000000);
        controller.setHost(&host);
        controller.setPath(u"Strona.md"_s);
    }
    void open(bool everyKind = false)
    {
        view = app.newEntity<PageView>(controller.blocks());
        view.update(app, [this, everyKind](PageView &v, at::Context<PageView> &cx) {
            PageMetrics m = v.metrics();
            m.contentX = 240;
            m.contentWidth = 760;
            m.gutter = m.arrowColumn + m.bulletColumn;
            v.setStyle(m, v.palette());
            v.setController(&controller, cx);
            if (everyKind) {
                v.setDrawsCode(true, PageView::CodeLook{}, cx);
                v.setDrawsCalc(true, PageView::CalcLook{}, cx);
                v.setDrawsTables(true, PageView::TableLook{}, cx);
                v.setViewLook(PageView::ViewLook{}, cx);
                v.setDrawsDiagrams(true, cx);
                v.setDrawsMath(true, cx);
            }
        });
        app.openWindow(tw, view);
        tw.settle();
    }
    // The wheel through the whole page; per step the time of dispatch + frame.
    std::vector<double> scrollThrough()
    {
        std::vector<double> ms;
        const at::ListHandle &handle = view.read(app).handle();
        for (int step = 0; step < 4000; ++step) {
            QElapsedTimer t;
            t.start();
            tw.simulateScroll({640, 400}, {0, 120});
            ms.push_back(double(t.nsecsElapsed()) / 1e6);
            if (handle.anchorRow() + 1 >= std::size_t(controller.blocks()->rowCount())
                || handle.scroll().offset.y >= handle.scroll().maxOffset().y - 0.5f)
                break;
        }
        return ms;
    }
};

constexpr int Blocks = 1000;

} // namespace

class BenchAtramentPage : public QObject {
    Q_OBJECT
private Q_SLOTS:
    void memoryPerBlock();
    void scrollFrames();
    void typing();
    void everyKind();
};

void BenchAtramentPage::memoryPerBlock()
{
#if !defined(__GLIBC__)
    QSKIP("mallinfo2 is glibc's");
#endif
    const QString content = page(Blocks);
    const std::size_t m0 = heapInUse();
    auto h = std::make_unique<Harness>(content);
    QCOMPARE(h->controller.blocks()->rowCount(), Blocks);
    const std::size_t m1 = heapInUse();
    h->tw.setRasterize(false);
    h->open();
    const std::size_t m2 = heapInUse();
    const std::size_t scrolled = h->scrollThrough().size();
    // Atrament's caches are generational: what the last frames did not use goes.
    for (int k = 0; k < 4; ++k)
        h->tw.draw();
    const std::size_t m3 = heapInUse();
    auto perBlock = [](std::size_t a, std::size_t b) { return (double(b) - double(a)) / Blocks; };
    std::printf("[bench] memory: model %.0f B/block; view opened +%.1f kB; after %zu wheel steps through the page "
                "+%.0f B/block (the model's rendered text, the rows' heights, Atrament's caches)\n",
                perBlock(m0, m1), (double(m2) - double(m1)) / 1024, scrolled, perBlock(m2, m3));
    // The view's own share: the same scroll with a view whose model kept its rendered text from
    // the first pass (only Atrament's part grows).
    auto h2 = std::make_unique<Harness>(content);
    h2->tw.setRasterize(false);
    h2->open();
    h2->scrollThrough();
    const std::size_t a = heapInUse();
    h2->view.update(h2->app, [](PageView &v, at::Context<PageView> &cx) {
        v.handle().scrollToRow(0);
        cx.notify();
    });
    h2->tw.settle();
    h2->scrollThrough();
    for (int k = 0; k < 4; ++k)
        h2->tw.draw();
    const std::size_t b = heapInUse();
    std::printf("[bench] memory: a second pass through the page (model caches warm): %+.0f B/block\n", perBlock(a, b));
    // A third: what a pass keeps for good (a warm-up's allocations happen once).
    h2->view.update(h2->app, [](PageView &v, at::Context<PageView> &cx) {
        v.handle().scrollToRow(0);
        cx.notify();
    });
    h2->tw.settle();
    h2->scrollThrough();
    for (int k = 0; k < 4; ++k)
        h2->tw.draw();
    const std::size_t c = heapInUse();
    std::printf("[bench] memory: a third pass: %+.0f B/block\n", perBlock(b, c));
    QVERIFY(perBlock(m2, m3) < 64 * 1024); // (a sanity bound; the gate is read from the output)
}

void BenchAtramentPage::scrollFrames()
{
    const QString content = page(Blocks);
    for (const bool raster : {false, true}) {
        Harness h(content);
        h.tw.setRasterize(raster);
        h.open();
        // A first pass lays every row out once (the model renders its text): what a page opened
        // for the first time costs; the second pass is the steady scroll.
        const Percentiles first = percentiles(h.scrollThrough());
        h.view.update(h.app, [](PageView &v, at::Context<PageView> &cx) {
            v.handle().scrollToRow(0);
            cx.notify();
        });
        h.tw.settle();
        const std::vector<double> steps = h.scrollThrough();
        const Percentiles second = percentiles(steps);
        std::printf("[bench] scroll %s: first pass median %.2f ms, p95 %.2f ms, max %.2f ms; again (%zu steps): "
                    "median %.2f ms, p95 %.2f ms, max %.2f ms\n",
                    raster ? "with the CPU renderer at 1280×800" : "(the scene, not rasterized)", first.median, first.p95,
                    first.max, steps.size(), second.median, second.p95, second.max);
        QVERIFY(!steps.empty());
    }
}

void BenchAtramentPage::typing()
{
    Harness h(page(Blocks));
    h.tw.setRasterize(false);
    h.open();
    h.controller.startEditing(500, -1);
    h.tw.settle();
    QCOMPARE(h.view.read(h.app).editedRow(), 500);
    std::vector<double> ms;
    for (char16_t c : std::u16string(u"zażółć gęślą jaźń pchnąć w tę łódź jeża")) {
        QElapsedTimer t;
        t.start();
        h.tw.simulateTyping(std::u16string(1, c));
        ms.push_back(double(t.nsecsElapsed()) / 1e6);
    }
    const Percentiles p = percentiles(ms);
    std::printf("[bench] typing in block 500 of %d: median %.2f ms, p95 %.2f ms, max %.2f ms a keystroke\n", Blocks,
                p.median, p.p95, p.max);
    QVERIFY(h.controller.blocks()->editTextAt(500).endsWith(u"łódź jeża"_s));
}

// The page with every kind Atrament draws: how many rows stay foreign (QML delegates), the memory
// per block after a pass, the scroll steps with the CPU renderer.
void BenchAtramentPage::everyKind()
{
    const QString content = mixedPage(Blocks);
    const std::size_t m0 = heapInUse();
    Harness h(content);
    h.tw.setRasterize(true);
    h.open(true);
    std::size_t foreign = 0;
    std::vector<double> steps = h.scrollThrough();
    for (int k = 0; k < 4; ++k)
        h.tw.draw();
    const std::size_t m1 = heapInUse();
    // Every row of the page once: which are foreign.
    for (int r = 0; r < h.controller.blocks()->rowCount(); ++r)
        foreign += h.view.read(h.app).drawsRow(r) ? 0 : 1;
    h.view.update(h.app, [](PageView &v, at::Context<PageView> &cx) {
        v.handle().scrollToRow(0);
        cx.notify();
    });
    h.tw.settle();
    steps = h.scrollThrough();
    const Percentiles p = percentiles(steps);
    std::printf("[bench] every kind (%d rows: code, tables, calculations, diagrams, formulas, rules, callouts): "
                "%zu foreign; model + view after a pass %.0f B/block; scroll with the CPU renderer at 1280×800 "
                "(%zu steps): median %.2f ms, p95 %.2f ms, max %.2f ms\n",
                h.controller.blocks()->rowCount(), foreign, (double(m1) - double(m0)) / h.controller.blocks()->rowCount(),
                steps.size(), p.median, p.p95, p.max);
    QVERIFY(!steps.empty());
}

QTEST_MAIN(BenchAtramentPage)
#include "bench_atrament_page.moc"
