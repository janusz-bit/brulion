// The journal stream drawn by Atrament (docs/ATRAMENT.md "The journal stream as one list") with a
// long day: today, then a day of 1 000 blocks (the generator of bench_atrament_page), then a few
// ordinary days, in a JournalView on Atrament's test platform at 1280 × 800.
//
//   * longDay — the stream scrolled to the long day: the first frame that shows it (its page made,
//     its controller loaded, the rows laid out), the heap that takes, then the wheel through the
//     day in steps of 120 px — per step the dispatch and the frame built (render, layout, prepaint,
//     paint: the scene), median, p95 and the slowest —, and how many of its rows a frame lays out
//     (the most of any frame), against the 1 000 a day laid out whole has.
//
// Release builds only (scripts/bench.sh); the numbers go to stdout as `[bench] …` lines.
#include "blocklistmodel.h"
#include "fakeeditorhost.h"
#include "journalview.h"
#include "pagecontroller.h"
#include "pageview.h"
#include "text/fonts.h"
#include "ui/testplatform.h"

#include <QDate>
#include <QElapsedTimer>
#include <QStandardItemModel>
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

// A day as long as a page people write (bench_atrament_page's generator).
QString longDay(int blocks)
{
    QStringList lines;
    for (int k = 0; k < blocks; ++k) {
        if (k % 50 == 0)
            lines << u"- ## Rozdział %1: zażółć gęślą jaźń"_s.arg(k / 50 + 1);
        else if (k % 9 == 4)
            lines << u"\t- zagnieżdżony punkt %1: źdźbło trawy, `kod` i **gęś**"_s.arg(k);
        else if (k % 13 == 6)
            lines << u"- %1 Długi punkt, który zawija się na kilka linii: zażółć gęślą jaźń, pchnąć w tę łódź jeża "
                     "lub ośm skrzyń fig, a potem jeszcze raz to samo, bo tak się pisze notatki."_s.arg(k);
        else if (k % 7 == 3)
            lines << u"- [ ] Zadanie %1 do zrobienia z [[Stroną]] i #tagiem"_s.arg(k);
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

enum DayRole { DateRole = Qt::UserRole + 1, PathRole, ExistsRole, IsTodayRole };

const QDate Today(2026, 11, 27);

constexpr int Blocks = 1000;

struct Harness {
    FakeEditorHost host;
    QStandardItemModel days;
    std::shared_ptr<at::TestPlatform> platform = std::make_shared<at::TestPlatform>();
    at::App app{platform, loadFonts()};
    at::TestWindow &tw;
    at::Entity<JournalView> journal;

    Harness() : tw(platform->newWindow({1280, 800}, 1))
    {
        days.setItemRoleNames({{DateRole, "date"}, {PathRole, "path"}, {ExistsRole, "exists"}, {IsTodayRole, "isToday"}});
        for (int d = 0; d <= 6; ++d) {
            const QString date = Today.addDays(-d).toString(Qt::ISODate);
            const QString path = u"Dziennik/"_s + date + u".md"_s;
            if (d > 0)
                host.setFile(path, d == 1 ? longDay(Blocks) : u"- Dzień %1: zakupy, mleko i chleb\n- Blok\n"_s.arg(d));
            auto *item = new QStandardItem;
            item->setData(date, DateRole);
            item->setData(path, PathRole);
            item->setData(d > 0, ExistsRole);
            item->setData(d == 0, IsTodayRole);
            days.appendRow(item);
        }
        journal = app.newEntity<JournalView>(&days);
        journal.update(app, [this](JournalView &j, at::Context<JournalView> &cx) {
            j.makeController = [this](const JournalView::DayInfo &info) {
                auto c = std::make_unique<PageController>();
                c->setNow(u"2026-11-27 10:00"_s);
                c->setSaveDelay(1000000);
                c->setHost(&host);
                c->setPath(info.path);
                return c;
            };
            PageMetrics m;
            m.contentX = 240;
            m.contentWidth = 760;
            m.gutter = m.arrowColumn + m.bulletColumn;
            j.setPageStyle(m, PagePalette{}, m.gutter, cx);
        });
        tw.setRasterize(false);
        app.openWindow(tw, journal);
        tw.settle();
    }
    const PageView *longPage()
    {
        const at::Entity<PageView> *p = journal.read(app).page(1);
        return p ? &p->read(app) : nullptr;
    }
};

} // namespace

class BenchAtramentJournal : public QObject {
    Q_OBJECT
private Q_SLOTS:
    void longDay();
};

void BenchAtramentJournal::longDay()
{
    const std::size_t m0 = heapInUse();
    Harness h;
    const std::size_t m1 = heapInUse();
    QElapsedTimer t;
    t.start();
    h.journal.update(h.app, [](JournalView &j, at::Context<JournalView> &cx) {
        j.scrollToDay(1, at::ListPosition::Start, cx);
    });
    h.tw.settle();
    const double opened = double(t.nsecsElapsed()) / 1e6;
    const std::size_t m2 = heapInUse();
    QVERIFY(h.longPage());
    QCOMPARE(h.longPage()->model()->rowCount(), Blocks);
    int mostRows = h.longPage()->drawnRows();
    std::vector<double> ms;
    const std::optional<at::Rect> day = h.journal.read(h.app).dayBounds(1);
    QVERIFY(day.has_value());
    for (int step = 0; step < 4000; ++step) {
        QElapsedTimer s;
        s.start();
        h.tw.simulateScroll({640, 400}, {0, 120});
        ms.push_back(double(s.nsecsElapsed()) / 1e6);
        if (const PageView *p = h.longPage())
            mostRows = std::max(mostRows, p->drawnRows());
        const std::optional<at::Rect> b = h.journal.read(h.app).dayBounds(1);
        if (!b || b->bottom() < 0)
            break;
    }
    for (int k = 0; k < 4; ++k)
        h.tw.draw();
    const std::size_t m3 = heapInUse();
    const Percentiles p = percentiles(ms);
    std::printf("[bench] journal, a %d-block day: shown in %.1f ms; heap: stream %+.1f kB, the day opened %+.1f kB, "
                "after the wheel through it %+.1f kB\n",
                Blocks, opened, (double(m1) - double(m0)) / 1024, (double(m2) - double(m1)) / 1024,
                (double(m3) - double(m2)) / 1024);
    std::printf("[bench] journal, a %d-block day: %zu wheel steps, a step %.2f ms median, %.2f ms p95, %.2f ms max; "
                "at most %d of its rows laid out in a frame\n",
                Blocks, ms.size(), p.median, p.p95, p.max, mostRows);
    QVERIFY(!ms.empty());
}

QTEST_MAIN(BenchAtramentJournal)
#include "bench_atrament_journal.moc"
