// The graph: ForceLayout (seeded determinism, convergence, Barnes–Hut close to the exact
// forces, pinned nodes, tick estimates), GraphModel (kinds, toggles for tags / missing pages /
// journals / orphans, local graph depth 1 and 2, the node cap, positions kept across index
// changes, search with Polish letters), the worker (GraphSimulation: threaded and inline runs equal
// to manual steps, fast cooling, pause / resume, coalesced frames, dragging during a run, rebuilds
// and destruction mid-run, reduced motion, pacing), the scene-graph geometry (discs, rings,
// segments, chunks of 16-bit indices, label placement) and GraphItem (camera maths,
// hit-testing, touch selection, painting — also 2000 nodes, with timings).
#include "forcelayout.h"
#include "graphgeometry.h"
#include "graphitem.h"
#include "graphmodel.h"
#include "graphsimulation.h"
#include "planningtest.h"

#include <QElapsedTimer>
#include <QFontMetricsF>
#include <QImage>
#include <QPainter>
#include <QSignalSpy>
#include <QTest>

#include <cmath>
#include <limits>

using namespace Qt::StringLiterals;
using brulion::planning::ForceLayout;
using brulion::planning::GraphItem;
using brulion::planning::GraphModel;
using brulion::planning::GraphSimulation;
using brulion::planning::test::Files;
using brulion::planning::test::makeIndex;
namespace geometry = brulion::planning::geometry;

namespace {

const Files &smallVault()
{
    static const Files files = {
            {u"A.md"_s, u"Link do [[B]] i [[C]] #projekt\n"_s},
            {u"B.md"_s, u"Wraca do [[A]], dalej [[D]]\n"_s},
            {u"C.md"_s, u"Tylko [[A]] i [[Nieistniejąca strona]] #projekt\n"_s},
            {u"D.md"_s, u"Na końcu łańcucha, [[E]]\n"_s},
            {u"E.md"_s, u"Ostatnia\n"_s},
            {u"Samotna.md"_s, u"Bez linków\n"_s},
            {u"journals/2026-10-15.md"_s, u"- Dziś o [[A]]\n"_s},
            {u"Żółw.md"_s, u"Strona o żółwiu, link do [[A]]\n"_s},
    };
    return files;
}

/// A random-ish vault of `pages` pages with about `linksPerPage` links each (deterministic).
Files bigVault(int pages, int linksPerPage)
{
    Files files;
    quint32 s = 12345;
    const auto next = [&s] {
        s = 1664525u * s + 1013904223u;
        return s;
    };
    for (int p = 0; p < pages; ++p) {
        QString content = u"Strona %1\n"_s.arg(p);
        for (int l = 0; l < linksPerPage; ++l) {
            // Mostly local links (clusters), some long ones.
            const int target = (next() % 4 == 0) ? int(next() % quint32(pages)) : (p + 1 + int(next() % 12)) % pages;
            content += u"- [[Strona %1]]\n"_s.arg(target);
        }
        files.append({u"Strona %1.md"_s.arg(p), content});
    }
    return files;
}

QList<std::pair<int, int>> ring(int n)
{
    QList<std::pair<int, int>> edges;
    for (int i = 0; i < n; ++i)
        edges.append({i, (i + 1) % n});
    return edges;
}

/// Simulations created while it lives run inline (the browser's way) or on their worker thread.
struct InlineRuns {
    explicit InlineRuns(bool inlineRuns) { GraphSimulation::setInlineForTests(inlineRuns); }
    ~InlineRuns() { GraphSimulation::setInlineForTests(false); }
    InlineRuns(const InlineRuns &) = delete;
    InlineRuns &operator=(const InlineRuns &) = delete;
};

bool hasInk(const QImage &img, const QColor &bg)
{
    for (int y = 0; y < img.height(); y += 2) {
        const QRgb *line = reinterpret_cast<const QRgb *>(img.constScanLine(y));
        for (int x = 0; x < img.width(); x += 2) {
            if (qAbs(qRed(line[x]) - bg.red()) + qAbs(qGreen(line[x]) - bg.green()) + qAbs(qBlue(line[x]) - bg.blue()) > 60)
                return true;
        }
    }
    return false;
}

} // namespace

class TestGraph : public QObject {
    Q_OBJECT

private Q_SLOTS:
    // ---- ForceLayout ---------------------------------------------------------------------------

    void layoutIsDeterministic()
    {
        ForceLayout a, b, c;
        a.reset(60, ring(60), 7);
        b.reset(60, ring(60), 7);
        c.reset(60, ring(60), 8);
        a.tick(120);
        b.tick(120);
        c.tick(120);
        bool different = false;
        for (int i = 0; i < 60; ++i) {
            QCOMPARE(a.x(i), b.x(i));
            QCOMPARE(a.y(i), b.y(i));
            different |= a.x(i) != c.x(i);
        }
        QVERIFY(different);
        // The edge order of the input does not matter.
        QList<std::pair<int, int>> reversed = ring(60);
        std::reverse(reversed.begin(), reversed.end());
        for (auto &e : reversed)
            std::swap(e.first, e.second);
        ForceLayout d;
        d.reset(60, reversed, 7);
        d.tick(120);
        for (int i = 0; i < 60; ++i)
            QCOMPARE(d.x(i), a.x(i));
    }

    void layoutConverges()
    {
        ForceLayout l;
        // Two triangles joined by one edge, plus an isolated node.
        l.reset(7, {{0, 1}, {1, 2}, {2, 0}, {3, 4}, {4, 5}, {5, 3}, {2, 3}, {1, 1}, {0, 99}}, 3);
        QCOMPARE(l.edgeCount(), 7); // self-loop and out-of-range edge dropped
        int ticks = 0;
        while (l.running() && ticks < 1000) {
            l.tick();
            ++ticks;
        }
        QVERIFY2(ticks > 250 && ticks < 350, qPrintable(QString::number(ticks)));
        QVERIFY(!l.running());
        const auto dist = [&](int i, int j) { return std::hypot(l.x(i) - l.x(j), l.y(i) - l.y(j)); };
        // Linked nodes sit near the spring length; the isolated node stays in reach.
        QVERIFY(dist(0, 1) > 15 && dist(0, 1) < 90);
        QVERIFY2(dist(0, 5) > dist(0, 1), qPrintable(u"%1 %2 | %3 %4 %5 %6 %7"_s.arg(dist(0, 5)).arg(dist(0, 1)).arg(dist(0, 3)).arg(dist(2, 3)).arg(dist(1, 4)).arg(dist(3, 5)).arg(dist(2, 5))));
        QVERIFY(std::hypot(l.x(6), l.y(6)) < 400);
        for (int i = 0; i < 7; ++i)
            QVERIFY(std::isfinite(l.x(i)) && std::isfinite(l.y(i)));
    }

    void barnesHutApproximatesExactForces()
    {
        ForceLayout bh, exact;
        bh.reset(300, ring(300), 5);
        exact.reset(300, ring(300), 5);
        exact.setExactRepulsion(true);
        bh.tick(1);
        exact.tick(1);
        double maxError = 0, maxSpeed = 0;
        for (int i = 0; i < 300; ++i) {
            maxError = std::max(maxError, std::hypot(bh.vx(i) - exact.vx(i), bh.vy(i) - exact.vy(i)));
            maxSpeed = std::max(maxSpeed, std::hypot(exact.vx(i), exact.vy(i)));
        }
        QVERIFY2(maxError < 0.1 * maxSpeed, qPrintable(u"%1 vs %2"_s.arg(maxError).arg(maxSpeed)));
    }

    void coincidentNodesSeparate()
    {
        ForceLayout l;
        l.reset(5, {}, 1);
        for (int i = 0; i < 5; ++i)
            l.setPosition(i, 10, 10);
        l.tick(30);
        QVERIFY(std::hypot(l.x(0) - l.x(1), l.y(0) - l.y(1)) > 0.5);
        for (int i = 0; i < 5; ++i)
            QVERIFY(std::isfinite(l.x(i)));
    }

    void pinnedNodesStay()
    {
        ForceLayout l;
        l.reset(10, ring(10), 2);
        l.fix(3, 500, -200);
        l.tick(50);
        QCOMPARE(l.x(3), 500.0);
        QCOMPARE(l.y(3), -200.0);
        l.unfix(3);
        l.setAlpha(0.5);
        l.tick(20);
        QVERIFY(l.x(3) < 500.0);
    }

    void tickEstimates()
    {
        ForceLayout l;
        l.reset(40, ring(40), 9);
        const int expected = l.expectedTicks();
        QCOMPARE(expected, ForceLayout::ticksToCool(1.0, 0.001, 0.0228));
        int ticks = 0;
        while (l.running()) {
            l.tick();
            ++ticks;
        }
        QCOMPARE(ticks, expected);
        QCOMPARE(l.expectedTicks(), 0);
        l.setAlphaTarget(0.3);
        QCOMPARE(l.expectedTicks(), -1); // warm until the target is lowered
        QCOMPARE(ForceLayout::ticksToCool(0.5, 0.001, 0.109), 54);
        QCOMPARE(ForceLayout::ticksToCool(0.0005, 0.001, 0.0228), 0);
        QCOMPARE(ForceLayout::ticksToCool(std::numeric_limits<double>::quiet_NaN(), 0.001, 0.0228), 0);
        QCOMPARE(ForceLayout::ticksToCool(1, 0.001, 0), -1);
    }

    void setPositionsKeepsAlphaAndPins()
    {
        ForceLayout l;
        l.reset(4, ring(4), 2);
        l.tick(5);
        l.fix(1, 10, 20);
        const double alpha = l.alpha();
        std::vector<double> xs{1, 2, 3, 4}, ys{-1, -2, -3, -4};
        l.setPositions(xs, ys);
        QCOMPARE(l.alpha(), alpha);
        QCOMPARE(l.x(2), 3.0);
        QCOMPARE(l.vx(2), 0.0);
        QVERIFY(l.isFixed(1));
        l.tick();
        QCOMPARE(l.x(1), 2.0); // pinned where setPositions put it
        QCOMPARE(l.y(1), -2.0);
        l.setPositions({1, 2}, {3, 4}); // wrong size: nothing happens
        QCOMPARE(l.nodeCount(), 4);
        QCOMPARE(l.x(1), 2.0);
    }

    // ---- GraphModel ----------------------------------------------------------------------------

    void modelKindsAndToggles()
    {
        auto index = makeIndex(smallVault());
        GraphModel m;
        m.setActive(false);
        m.setVaultIndex(index.get());
        QCOMPARE(m.pageCount(), 8);
        QCOMPARE(m.nodeCount(), 8);
        // A-B (both ways = one edge), A-C, B-D, D-E, journal-A, Żółw-A.
        QCOMPARE(m.edgeCount(), 6);
        QCOMPARE(m.nodeInfo(u"A.md"_s).value(u"degree"_s).toInt(), 4);
        QCOMPARE(m.nodeInfo(u"journals/2026-10-15.md"_s).value(u"kind"_s).toString(), u"journal"_s);
        // Daily notes are labelled with their date in the UI language.
        QVERIFY(m.nodeInfo(u"journals/2026-10-15.md"_s).value(u"title"_s).toString().contains(u"15 paź"_s));
        m.setLocale(u"en"_s);
        QVERIFY(m.nodeInfo(u"journals/2026-10-15.md"_s).value(u"title"_s).toString().contains(u"Oct 15"_s));
        QCOMPARE(m.find(u"2026-10-15"_s).size(), 1); // found by its path too
        QCOMPARE(m.nodeInfo(u"A.md"_s).value(u"kind"_s).toString(), u"page"_s);
        QCOMPARE(m.neighbors(u"D.md"_s), (QStringList{u"B.md"_s, u"E.md"_s}));

        m.setShowTags(true);
        QCOMPARE(m.nodeInfo(u"#projekt"_s).value(u"kind"_s).toString(), u"tag"_s);
        QCOMPARE(m.nodeInfo(u"#projekt"_s).value(u"degree"_s).toInt(), 2);
        m.setShowMissing(true);
        QCOMPARE(m.nodeInfo(u"Nieistniejąca strona"_s).value(u"kind"_s).toString(), u"missing"_s);
        QCOMPARE(m.nodeInfo(u"Nieistniejąca strona"_s).value(u"exists"_s).toBool(), false);
        QCOMPARE(m.nodeCount(), 10);
        m.setShowJournals(false);
        QVERIFY(m.nodeInfo(u"journals/2026-10-15.md"_s).isEmpty());
        QCOMPARE(m.nodeInfo(u"A.md"_s).value(u"degree"_s).toInt(), 4); // B, C, Żółw, #projekt
        m.setShowOrphans(false);
        QVERIFY(m.nodeInfo(u"Samotna.md"_s).isEmpty());
        QCOMPARE(m.nodeCount(), 8);
    }

    void templatesAreHiddenByDefault()
    {
        // Templates link to what they will be used for ([[Zakupy]], #dziennik, pages to write):
        // left out unless asked for, with the tags and missing pages only they link to.
        Files files = smallVault();
        files.append({u"templates/journal.md"_s, u"## Plan dnia\n- [[A]] #dziennik\n- [[Przyszła strona]]\n"_s});
        files.append({u"Templates/Zakupy.md"_s, u"- [[B]] #zakupy\n"_s}); // another device's capitals
        files.append({u"Notatki/templates.md"_s, u"- [[E]]\n"_s});        // a page named so, not the folder
        auto index = makeIndex(files);
        GraphModel m;
        m.setActive(false);
        m.setVaultIndex(index.get());
        m.setShowTags(true);
        m.setShowMissing(true);
        QVERIFY(!m.showTemplates());
        QVERIFY(m.nodeInfo(u"templates/journal.md"_s).isEmpty());
        QVERIFY(m.nodeInfo(u"Templates/Zakupy.md"_s).isEmpty());
        QVERIFY(!m.nodeInfo(u"Notatki/templates.md"_s).isEmpty());
        QVERIFY(m.nodeInfo(u"#dziennik"_s).isEmpty());
        QVERIFY(m.nodeInfo(u"#zakupy"_s).isEmpty());
        QVERIFY(m.nodeInfo(u"Przyszła strona"_s).isEmpty());
        QCOMPARE(m.nodeInfo(u"A.md"_s).value(u"degree"_s).toInt(), 5); // B, C, journal, Żółw, #projekt
        m.setShowTemplates(true);
        QVERIFY(!m.nodeInfo(u"templates/journal.md"_s).isEmpty());
        QVERIFY(!m.nodeInfo(u"Templates/Zakupy.md"_s).isEmpty());
        QVERIFY(!m.nodeInfo(u"#dziennik"_s).isEmpty());
        QCOMPARE(m.nodeInfo(u"A.md"_s).value(u"degree"_s).toInt(), 6);
        // The local graph of a template shows it, whatever the toggle.
        m.setShowTemplates(false);
        m.setFocusPath(u"templates/journal.md"_s);
        QVERIFY(!m.focusMissing());
        QVERIFY(!m.nodeInfo(u"templates/journal.md"_s).isEmpty());
        QVERIFY(!m.nodeInfo(u"A.md"_s).isEmpty());
    }

    void localGraph()
    {
        auto index = makeIndex(smallVault());
        GraphModel m;
        m.setActive(false);
        m.setVaultIndex(index.get());
        m.setFocusPath(u"D.md"_s);
        m.setDepth(1);
        QStringList ids;
        for (const auto &n : m.nodes())
            ids << n.id;
        QCOMPARE(ids, (QStringList{u"B.md"_s, u"D.md"_s, u"E.md"_s}));
        m.setDepth(2);
        QCOMPARE(m.nodeCount(), 4); // + A
        QVERIFY(m.indexOf(u"A.md"_s) >= 0);
        QVERIFY(!m.focusMissing());
        // An orphan is its own local graph.
        m.setFocusPath(u"Samotna.md"_s);
        QCOMPARE(m.nodeCount(), 1);
        m.setShowOrphans(false);
        QCOMPARE(m.nodeCount(), 1); // the focus always stays
        // Unknown focus: the whole graph, flagged.
        m.setFocusPath(u"Nie ma.md"_s);
        QVERIFY(m.focusMissing());
        QCOMPARE(m.nodeCount(), 7);
    }

    void nodeCap()
    {
        auto index = makeIndex(smallVault());
        GraphModel m;
        m.setActive(false);
        m.setVaultIndex(index.get());
        m.setMaxNodes(3);
        QVERIFY(m.truncated());
        QCOMPARE(m.totalNodeCount(), 8);
        QCOMPARE(m.nodeCount(), 3);
        QVERIFY(m.indexOf(u"A.md"_s) >= 0); // best connected
        m.setFocusPath(u"E.md"_s);
        m.setDepth(3);
        QVERIFY(m.indexOf(u"E.md"_s) >= 0); // the focus always stays
        m.setMaxNodes(100);
        QVERIFY(!m.truncated());
    }

    void positionsSurviveIndexChanges()
    {
        auto index = makeIndex(smallVault());
        GraphModel m;
        m.setActive(false);
        m.setRefreshDelay(0);
        m.setVaultIndex(index.get());
        m.step(400);
        QVERIFY(!m.layout().running());
        const QPointF a = m.position(m.indexOf(u"A.md"_s));
        const QPointF e = m.position(m.indexOf(u"E.md"_s));
        index->update(u"F.md"_s, u"Nowa strona o [[A]]\n"_s);
        QTRY_VERIFY(m.indexOf(u"F.md"_s) >= 0);
        QCOMPARE(m.position(m.indexOf(u"A.md"_s)), a);
        QCOMPARE(m.position(m.indexOf(u"E.md"_s)), e);
        const QPointF f = m.position(m.indexOf(u"F.md"_s));
        QVERIFY(std::hypot(f.x() - a.x(), f.y() - a.y()) < 40); // starts next to its neighbour
        QVERIFY(m.layout().running());                          // reheated
        QVERIFY(m.alpha() <= 0.36);
        // Content edits that change no links keep the layout cold.
        m.step(400);
        index->update(u"E.md"_s, u"Ostatnia, zmieniona\n"_s);
        QTest::qWait(20);
        QVERIFY(!m.layout().running());
    }

    void modelDeterminism()
    {
        auto index = makeIndex(bigVault(120, 3));
        GraphModel a, b;
        a.setActive(false);
        b.setActive(false);
        a.setSeed(42);
        b.setSeed(42);
        a.setVaultIndex(index.get());
        b.setVaultIndex(index.get());
        a.step(80);
        b.step(80);
        QCOMPARE(a.nodeCount(), 120);
        for (int i = 0; i < a.nodeCount(); ++i)
            QCOMPARE(a.position(i), b.position(i));
        b.setSeed(43);
        b.step(80);
        QVERIFY(a.position(0) != b.position(0));
    }

    void findNodes()
    {
        auto index = makeIndex(smallVault());
        GraphModel m;
        m.setActive(false);
        m.setShowTags(true);
        m.setVaultIndex(index.get());
        QVariantList hits = m.find(u"zolw"_s);
        QCOMPARE(hits.size(), 1);
        QCOMPARE(hits.first().toMap().value(u"id"_s).toString(), u"Żółw.md"_s);
        hits = m.find(u"proj"_s);
        QCOMPARE(hits.first().toMap().value(u"id"_s).toString(), u"#projekt"_s);
        hits = m.find(u"a"_s);
        QCOMPARE(hits.first().toMap().value(u"id"_s).toString(), u"A.md"_s); // exact first
        QVERIFY(m.find(u""_s).isEmpty());
        QCOMPARE(m.find(u"e"_s, 2).size(), 2);
    }

    void dragging()
    {
        auto index = makeIndex(smallVault());
        GraphModel m;
        m.setActive(false);
        m.setVaultIndex(index.get());
        const int a = m.indexOf(u"A.md"_s);
        m.beginDrag(a);
        QVERIFY(m.layout().isFixed(a));
        QCOMPARE(m.layout().alphaTarget(), 0.3);
        m.dragTo(a, QPointF(300, 300));
        m.step(5);
        QCOMPARE(m.position(a), QPointF(300, 300));
        m.endDrag(a);
        QVERIFY(!m.layout().isFixed(a));
        QCOMPARE(m.layout().alphaTarget(), 0.0);
    }

    // ---- the worker (GraphSimulation) ------------------------------------------------------------

    void runsOnWorkerAndStops_data()
    {
        QTest::addColumn<bool>("inlineRuns");
        QTest::newRow("worker thread") << false;
        QTest::newRow("inline (the browser)") << true;
    }

    void runsOnWorkerAndStops()
    {
        QFETCH(bool, inlineRuns);
        InlineRuns guard(inlineRuns);
        auto index = makeIndex(smallVault());
        GraphModel m;
        m.setMinRunMs(200);
        QCOMPARE(m.threaded(), !inlineRuns);
        QSignalSpy running(&m, &GraphModel::runningChanged);
        m.setVaultIndex(index.get());
        QVERIFY(m.running());
        QVERIFY(m.runActive());
        QTRY_VERIFY_WITH_TIMEOUT(!m.running(), 10000);
        QVERIFY(running.count() >= 2);
        QVERIFY(m.framesApplied() >= 2);
        QVERIFY(!m.layout().running()); // the final state came back from the worker
        QCOMPARE(m.position(0), QPointF(m.layout().x(0), m.layout().y(0)));
        m.reheat(0.5);
        QVERIFY(m.running());
        m.setActive(false); // paused at once
        QVERIFY(!m.running());
        m.setActive(true);
        QVERIFY(m.running());
        QTRY_VERIFY_WITH_TIMEOUT(!m.running(), 10000);
        QVERIFY(m.alpha() < m.layout().params().alphaMin);
        // Stepping by hand takes the layout back from the worker.
        m.reheat(0.4);
        m.step(1);
        QVERIFY(m.runActive()); // and hands it over again (still warm)
        QTRY_VERIFY_WITH_TIMEOUT(!m.running(), 10000);
    }

    void workerRunMatchesManualSteps_data()
    {
        QTest::addColumn<int>("pages"); // 0: the small vault
        QTest::addColumn<int>("seed");
        QTest::addColumn<double>("slowTickMs");
        QTest::addColumn<bool>("inlineRuns");
        QTest::newRow("small, seed 1, normal cooling") << 0 << 1 << 1e9 << false;
        QTest::newRow("small, seed 7, fast cooling, inline") << 0 << 7 << -1.0 << true;
        QTest::newRow("400 pages, seed 12345, fast cooling") << 400 << 12345 << -1.0 << false;
        QTest::newRow("400 pages, seed 7, normal cooling, inline") << 400 << 7 << 1e9 << true;
        QTest::newRow("400 pages, seed 1, normal cooling") << 400 << 1 << 1e9 << false;
        QTest::newRow("400 pages, seed 7, fast cooling, inline") << 400 << 7 << -1.0 << true;
    }

    void workerRunMatchesManualSteps()
    {
        // Pacing, coalescing and threads change when frames are shown, never the layout: the run
        // ends exactly where the same ForceLayout steps end (3 with the normal cooling, then the
        // fast one when the first steps were "slow").
        QFETCH(int, pages);
        QFETCH(int, seed);
        QFETCH(double, slowTickMs);
        QFETCH(bool, inlineRuns);
        InlineRuns guard(inlineRuns);
        auto index = makeIndex(pages == 0 ? smallVault() : bigVault(pages, 3));
        GraphModel m;
        m.setSeed(seed);
        m.setSlowTickMs(slowTickMs);
        m.setMinRunMs(150);
        QSignalSpy fast(&m, &GraphModel::fastCoolingChanged);
        m.setVaultIndex(index.get());
        QVERIFY(m.running());
        QVERIFY(!m.fastCooling());
        QTRY_VERIFY_WITH_TIMEOUT(!m.running(), 30000 * BRULION_TEST_SLOWDOWN);
        const bool fastCooling = slowTickMs < 0;
        QCOMPARE(m.fastCooling(), fastCooling);
        QCOMPARE(fast.count(), fastCooling ? 1 : 0);
        QVERIFY(m.tickCostMs() > 0);

        GraphModel manual;
        manual.setActive(false);
        manual.setSeed(seed);
        manual.setVaultIndex(index.get());
        ForceLayout &layout = manual.layout();
        int steps = 0;
        while (layout.running()) {
            if (steps == 3 && fastCooling)
                layout.params().alphaDecay = 0.067;
            layout.tick();
            ++steps;
        }
        QVERIFY2(fastCooling ? steps > 80 && steps < 120 : steps > 280 && steps < 320,
                 qPrintable(QString::number(steps)));
        QCOMPARE(m.runTicks(), steps);
        QCOMPARE(manual.nodeCount(), m.nodeCount());
        for (int i = 0; i < m.nodeCount(); ++i) {
            QCOMPARE(m.position(i).x(), layout.x(i));
            QCOMPARE(m.position(i).y(), layout.y(i));
        }

        // A new run decides again, starting with the normal cooling.
        m.reheat(0.5);
        QVERIFY(!m.fastCooling());
        QTRY_VERIFY_WITH_TIMEOUT(!m.running(), 30000 * BRULION_TEST_SLOWDOWN);
        QCOMPARE(m.fastCooling(), fastCooling);
    }

    void pauseResumeKeepsDeterminism()
    {
        auto index = makeIndex(bigVault(300, 3));
        GraphModel straight;
        straight.setSeed(3);
        straight.setSlowTickMs(1e9);
        straight.setMinRunMs(0);
        straight.setVaultIndex(index.get());
        QTRY_VERIFY_WITH_TIMEOUT(!straight.running(), 30000 * BRULION_TEST_SLOWDOWN);

        GraphModel m;
        m.setSeed(3);
        m.setSlowTickMs(1e9);
        m.setMinRunMs(600);
        m.setVaultIndex(index.get());
        QTRY_VERIFY_WITH_TIMEOUT(m.framesApplied() >= 3, 10000 * BRULION_TEST_SLOWDOWN);
        m.setActive(false); // the view is hidden mid-run
        QVERIFY(!m.running());
        QTest::qWait(60); // a frame computed before the pause may still arrive
        const int ticks = m.runTicks();
        const QPointF p = m.position(0);
        QTest::qWait(100);
        QCOMPARE(m.runTicks(), ticks); // nothing ran while hidden
        QCOMPARE(m.position(0), p);
        QVERIFY(ticks < 300);
        m.setActive(true);
        QVERIFY(m.running());
        QTRY_VERIFY_WITH_TIMEOUT(!m.running(), 30000 * BRULION_TEST_SLOWDOWN);
        QCOMPARE(m.runTicks(), straight.runTicks());
        for (int i = 0; i < m.nodeCount(); ++i)
            QCOMPARE(m.position(i), straight.position(i));
    }

    void framesCoalesceWhenTheGuiIsBusy()
    {
        auto index = makeIndex(bigVault(300, 3));
        GraphModel m;
        m.setSeed(5);
        m.setSlowTickMs(-1); // ~100 steps …
        m.setMinRunMs(1500); // … over 1.5 s: one or two per frame
        int lastTicks = 0;
        int maxJump = 0;
        int changes = 0;
        connect(&m, &GraphModel::positionsChanged, &m, [&] {
            ++changes;
            maxJump = std::max(maxJump, m.runTicks() - lastTicks);
            lastTicks = m.runTicks();
        });
        m.setVaultIndex(index.get());
        QTRY_VERIFY_WITH_TIMEOUT(m.framesApplied() >= 3, 10000 * BRULION_TEST_SLOWDOWN);
        const int jumpBefore = maxJump;
        QTest::qSleep(300); // the GUI thread is busy; the worker is not
        QTRY_VERIFY_WITH_TIMEOUT(!m.running(), 30000 * BRULION_TEST_SLOWDOWN);
        // The frame after the busy spell carried the newest positions, many steps ahead.
        QVERIFY2(maxJump >= jumpBefore + 5, qPrintable(u"%1 → %2"_s.arg(jumpBefore).arg(maxJump)));
        QVERIFY(changes <= m.runTicks() + 1);

        GraphModel manual;
        manual.setActive(false);
        manual.setSeed(5);
        manual.setVaultIndex(index.get());
        ForceLayout &layout = manual.layout();
        layout.tick(3);
        layout.params().alphaDecay = 0.067;
        while (layout.running())
            layout.tick();
        for (int i = 0; i < m.nodeCount(); ++i)
            QCOMPARE(m.position(i), QPointF(layout.x(i), layout.y(i)));
    }

    void dragDuringRun()
    {
        auto index = makeIndex(smallVault());
        GraphModel m;
        m.setMinRunMs(800);
        m.setVaultIndex(index.get());
        QVERIFY(m.running());
        const int a = m.indexOf(u"A.md"_s);
        m.beginDrag(a);
        QVERIFY(m.layout().isFixed(a));
        m.dragTo(a, QPointF(300, 300));
        QCOMPARE(m.position(a), QPointF(300, 300)); // at once, not with the next frame
        const int frames = m.framesApplied();
        QTRY_VERIFY_WITH_TIMEOUT(m.framesApplied() >= frames + 3, 5000);
        QCOMPARE(m.position(a), QPointF(300, 300)); // and in the worker's frames
        QVERIFY(m.running());                       // kept warm while dragging
        // The neighbours follow it.
        const int b = m.indexOf(u"B.md"_s);
        const auto dist = [&] { return std::hypot(m.position(b).x() - 300, m.position(b).y() - 300); };
        const double before = dist();
        QTRY_VERIFY_WITH_TIMEOUT(dist() < before - 1, 5000);
        m.endDrag(a);
        QVERIFY(!m.layout().isFixed(a));
        QTRY_VERIFY_WITH_TIMEOUT(m.position(a) != QPointF(300, 300), 5000); // released: it moves on
        QTRY_VERIFY_WITH_TIMEOUT(!m.running(), 10000);
        QVERIFY(!m.layout().isFixed(a));
        QCOMPARE(m.layout().alphaTarget(), 0.0);

        // A drag on a settled graph warms it up again.
        m.beginDrag(a);
        QVERIFY(m.running());
        m.dragTo(a, QPointF(-200, 50));
        QTRY_VERIFY_WITH_TIMEOUT(m.framesApplied() >= frames + 10, 5000);
        QCOMPARE(m.position(a), QPointF(-200, 50));
        m.endDrag(a);
        QTRY_VERIFY_WITH_TIMEOUT(!m.running(), 10000);
    }

    void rebuildDuringRunDropsStaleFrames()
    {
        auto index = makeIndex(bigVault(200, 3));
        GraphModel m;
        m.setRefreshDelay(0);
        m.setMinRunMs(1000);
        bool consistent = true;
        connect(&m, &GraphModel::positionsChanged, &m, [&] {
            consistent = consistent && m.xs().size() == size_t(m.nodeCount()) && m.ys().size() == size_t(m.nodeCount());
        });
        m.setVaultIndex(index.get());
        QTRY_VERIFY_WITH_TIMEOUT(m.framesApplied() >= 2, 5000);
        const int nodes = m.nodeCount();
        index->update(u"Nowa strona.md"_s, u"Łączy [[Strona 1]] i [[Strona 2]]\n"_s);
        QTRY_COMPARE(m.nodeCount(), nodes + 1);
        QVERIFY(m.running());
        QTRY_VERIFY_WITH_TIMEOUT(!m.running(), 20000 * BRULION_TEST_SLOWDOWN);
        QVERIFY(consistent);
        QVERIFY(m.staleFramesDropped() >= 0);
        QCOMPARE(m.layout().nodeCount(), m.nodeCount());
        const QPointF p = m.position(m.indexOf(u"Nowa strona.md"_s));
        QVERIFY(std::isfinite(p.x()) && std::isfinite(p.y()));
    }

    void destroyWhileRunning()
    {
        auto index = makeIndex(bigVault(2000, 4));
        auto *m = new GraphModel;
        m->setVaultIndex(index.get());
        QVERIFY(m->running());
        QTest::qWait(20);
        QElapsedTimer t;
        t.start();
        delete m; // joins the worker within a step
        QVERIFY2(t.elapsed() < 500 * BRULION_TEST_SLOWDOWN, qPrintable(QString::number(t.elapsed())));
    }

    void reducedMotionShowsOnlyTheFinalLayout()
    {
        auto index = makeIndex(bigVault(200, 3));
        GraphModel m;
        m.setRefreshDelay(0);
        m.setReducedMotion(true);
        QSignalSpy frames(&m, &GraphModel::positionsChanged);
        QSignalSpy settling(&m, &GraphModel::settlingChanged);
        m.setVaultIndex(index.get());
        // A fresh layout: nothing to show until it has settled.
        QVERIFY(m.settling());
        QVERIFY(m.running());
        GraphItem item;
        item.setSize(QSizeF(400, 300));
        item.setModel(&m);
        const QColor bg(u"#fbf7ee");
        item.setBackgroundColor(bg);
        QImage img(400, 300, QImage::Format_ARGB32_Premultiplied);
        {
            QPainter p(&img);
            item.render(&p, img.size());
        }
        QVERIFY(!hasInk(img, bg));
        geometry::MeshChunks mesh;
        item.buildMesh(mesh, 1.0);
        QVERIFY(mesh.isEmpty());
        QCOMPARE(item.nodeAt(200, 150), QString()); // nothing to hover or tap either
        QVERIFY(item.labelsFor(QSizeF(400, 300)).empty());
        QTRY_VERIFY_WITH_TIMEOUT(!m.running(), 20000 * BRULION_TEST_SLOWDOWN);
        QVERIFY(!m.settling());
        QCOMPARE(settling.count(), 2);
        QCOMPARE(frames.count(), 2); // the rebuild and the final layout
        QVERIFY(m.alpha() < m.layout().params().alphaMin);
        {
            QPainter p(&img);
            item.render(&p, img.size());
        }
        QVERIFY(hasInk(img, bg));
        // Carried positions are shown as they are and jump once to the result: no settling.
        frames.clear();
        settling.clear();
        index->update(u"Żółta strona.md"_s, u"[[Strona 3]]\n"_s);
        QTRY_VERIFY(m.indexOf(u"Żółta strona.md"_s) >= 0);
        QVERIFY(!m.settling());
        QVERIFY(m.running());
        QTRY_VERIFY_WITH_TIMEOUT(!m.running(), 20000 * BRULION_TEST_SLOWDOWN);
        QCOMPARE(settling.count(), 0);
        QCOMPARE(frames.count(), 2);
    }

    void pacingUnfoldsSmallGraphs()
    {
        auto index = makeIndex(smallVault());
        {
            GraphModel m;
            m.setMinRunMs(300);
            QSignalSpy frames(&m, &GraphModel::positionsChanged);
            QElapsedTimer t;
            t.start();
            m.setVaultIndex(index.get());
            QTRY_VERIFY_WITH_TIMEOUT(!m.running(), 10000);
            QVERIFY2(t.elapsed() >= 280, qPrintable(QString::number(t.elapsed())));
            QVERIFY2(frames.count() >= 10, qPrintable(QString::number(frames.count())));
        }
        {
            GraphModel m;
            m.setMinRunMs(0); // as fast as it can: a few frames
            QSignalSpy frames(&m, &GraphModel::positionsChanged);
            m.setVaultIndex(index.get());
            QTRY_VERIFY_WITH_TIMEOUT(!m.running(), 10000);
            QVERIFY2(frames.count() <= 4, qPrintable(QString::number(frames.count())));
        }
    }

    // ---- slow software painting ------------------------------------------------------------------

    void slowFramesLeaveOutLabelsWhileMoving()
    {
        auto index = makeIndex(bigVault(400, 3));
        GraphModel m;
        m.setActive(false);
        m.setVaultIndex(index.get());
        m.step(3);
        GraphItem item;
        item.setSize(QSizeF(800, 600));
        item.setModel(&m);
        item.setSlowPaintMs(0);  // every frame counts as slow
        item.setLabelZoom(0.01); // labels for every visible node
        item.setAutoFit(true);
        QImage still(800, 600, QImage::Format_ARGB32_Premultiplied);
        QImage moving(800, 600, QImage::Format_ARGB32_Premultiplied);
        QImage bare(800, 600, QImage::Format_ARGB32_Premultiplied);
        {
            QPainter p(&still);
            item.render(&p, still.size());
        }
        QVERIFY(item.sceneStats().labels > 0);
        m.setActive(true); // moving (no frame is applied before the event loop runs)
        QVERIFY(m.running());
        {
            QPainter p(&moving);
            item.render(&p, moving.size());
        }
        QCOMPARE(item.sceneStats().labels, 0);
        m.setActive(false);
        item.setLabelZoom(1e6); // no labels at all
        {
            QPainter p(&bare);
            item.render(&p, bare.size());
        }
        QVERIFY(still != moving);
        QCOMPARE(moving, bare);
        QVERIFY(item.lastPaintMs() > 0);
    }

    void movingRepaintInterval()
    {
        QCOMPARE(GraphItem::movingRepaintInterval(0), 66);
        QCOMPARE(GraphItem::movingRepaintInterval(10), 66);
        QCOMPARE(GraphItem::movingRepaintInterval(100), 300);
        QCOMPARE(GraphItem::movingRepaintInterval(150.2), 451);
        QCOMPARE(GraphItem::movingRepaintInterval(5000), 600);
    }

    // ---- scene-graph geometry --------------------------------------------------------------------

    void discSegments_data()
    {
        QTest::addColumn<double>("radius");
        QTest::addColumn<int>("segments");
        QTest::newRow("half a pixel") << 0.5 << 8;
        QTest::newRow("the smallest disc") << 1.6 << 8;
        QTest::newRow("4 px") << 4.0 << 9;
        QTest::newRow("22 px") << 22.0 << 47;
        QTest::newRow("132 px (capped)") << 132.0 << 64;
        QTest::newRow("zero") << 0.0 << 8;
        QTest::newRow("negative") << -3.0 << 8;
        QTest::newRow("NaN") << std::numeric_limits<double>::quiet_NaN() << 8;
        QTest::newRow("infinite") << std::numeric_limits<double>::infinity() << 8;
    }

    void discSegments()
    {
        QFETCH(double, radius);
        QFETCH(int, segments);
        QCOMPARE(geometry::discSegments(radius), segments);
    }

    void discMesh()
    {
        geometry::MeshChunks chunks;
        const QRgb colour = geometry::premultiplied(QColor(200, 100, 50, 128));
        QCOMPARE(qAlpha(colour), 128);
        QCOMPARE(qRed(colour), 100); // premultiplied: 200 · 128 / 255
        geometry::appendDisc(chunks, QPointF(10, 20), 5, colour, 1, 12);
        QCOMPARE(int(chunks.chunks().size()), 1);
        const geometry::Mesh &m = chunks.chunks()[0];
        QCOMPARE(int(m.vertices.size()), 1 + 2 * 12);
        QCOMPARE(int(m.indices.size()), 12 * 9);
        for (const quint16 i : m.indices)
            QVERIFY(i < m.vertices.size());
        QCOMPARE(m.vertices[0].x, 10.0f);
        QCOMPARE(m.vertices[0].y, 20.0f);
        QCOMPARE(int(m.vertices[0].r), qRed(colour));
        QCOMPARE(int(m.vertices[0].a), 128);
        for (int k = 1; k <= 12; ++k) {
            QCOMPARE(int(m.vertices[size_t(k)].a), 128);
            QVERIFY(std::abs(std::hypot(m.vertices[size_t(k)].x - 10, m.vertices[size_t(k)].y - 20) - 4.5) < 1e-4);
        }
        for (int k = 13; k <= 24; ++k) {
            QCOMPARE(int(m.vertices[size_t(k)].a), 0); // the fringe fades out
            QVERIFY(std::abs(std::hypot(m.vertices[size_t(k)].x - 10, m.vertices[size_t(k)].y - 20) - 5.5) < 1e-4);
        }
        // Malformed input adds nothing.
        geometry::appendDisc(chunks, QPointF(std::numeric_limits<double>::quiet_NaN(), 0), 5, colour, 1, 12);
        geometry::appendDisc(chunks, QPointF(0, 0), 0, colour, 1, 12);
        geometry::appendDisc(chunks, QPointF(0, 0), std::numeric_limits<double>::infinity(), colour, 1, 12);
        QCOMPARE(chunks.vertexCount(), 25);
    }

    void ringMesh()
    {
        geometry::MeshChunks chunks;
        geometry::appendRing(chunks, QPointF(0, 0), 10, 2, geometry::premultiplied(Qt::red), 1, 16);
        const geometry::Mesh &m = chunks.chunks()[0];
        QCOMPARE(int(m.vertices.size()), 4 * 16);
        QCOMPARE(int(m.indices.size()), 3 * 16 * 6);
        for (int k = 0; k < 16; ++k) {
            QCOMPARE(int(m.vertices[size_t(k)].a), 0); // inner fringe
            QCOMPARE(int(m.vertices[size_t(16 + k)].a), 255);
            QCOMPARE(int(m.vertices[size_t(48 + k)].a), 0); // outer fringe
        }
        QVERIFY(std::abs(std::hypot(m.vertices[0].x, m.vertices[0].y) - 8.5) < 1e-4);
        QVERIFY(std::abs(std::hypot(m.vertices[48].x, m.vertices[48].y) - 11.5) < 1e-4);
        // Thinner than the fringe: it fades instead of shrinking.
        chunks.clear();
        QVERIFY(chunks.isEmpty());
        geometry::appendRing(chunks, QPointF(0, 0), 10, 0.5, geometry::premultiplied(Qt::red), 1, 16);
        QCOMPARE(int(chunks.chunks()[0].vertices[16].a), 170); // 255 · 2 · 0.5 / 1.5
    }

    void segmentMesh_data()
    {
        QTest::addColumn<QPointF>("a");
        QTest::addColumn<QPointF>("b");
        QTest::addColumn<double>("width");
        QTest::addColumn<int>("vertices");
        QTest::addColumn<int>("innerAlpha");
        const double nan = std::numeric_limits<double>::quiet_NaN();
        QTest::newRow("horizontal") << QPointF(0, 0) << QPointF(10, 0) << 2.0 << 8 << 255;
        QTest::newRow("vertical") << QPointF(0, 0) << QPointF(0, -10) << 2.0 << 8 << 255;
        QTest::newRow("diagonal") << QPointF(-3, 4) << QPointF(5, -2) << 1.0 << 8 << 255;
        QTest::newRow("thin (fades)") << QPointF(0, 0) << QPointF(10, 0) << 0.25 << 8 << 102; // 255 · 0.5 / 1.25
        QTest::newRow("zero length") << QPointF(3, 3) << QPointF(3, 3) << 2.0 << 0 << 0;
        QTest::newRow("NaN end") << QPointF(0, 0) << QPointF(nan, 1) << 2.0 << 0 << 0;
        QTest::newRow("zero width") << QPointF(0, 0) << QPointF(10, 0) << 0.0 << 0 << 0;
    }

    void segmentMesh()
    {
        QFETCH(QPointF, a);
        QFETCH(QPointF, b);
        QFETCH(double, width);
        QFETCH(int, vertices);
        QFETCH(int, innerAlpha);
        geometry::MeshChunks chunks;
        geometry::appendSegment(chunks, a, b, width, geometry::premultiplied(Qt::black), 1);
        QCOMPARE(chunks.vertexCount(), vertices);
        if (vertices == 0)
            return;
        const geometry::Mesh &m = chunks.chunks()[0];
        QCOMPARE(int(m.indices.size()), 18);
        QCOMPARE(int(m.vertices[0].a), 0);
        QCOMPARE(int(m.vertices[2].a), innerAlpha);
        QCOMPARE(int(m.vertices[7].a), 0);
        // The band is perpendicular to the segment and as wide as the line plus the fringe.
        const QPointF outer0(m.vertices[0].x, m.vertices[0].y);
        const QPointF outer3(m.vertices[6].x, m.vertices[6].y);
        const double across = std::hypot(outer3.x() - outer0.x(), outer3.y() - outer0.y());
        QVERIFY2(std::abs(across - (width + 1)) < 1e-4, qPrintable(QString::number(across)));
        const QPointF d = b - a;
        const QPointF n = outer3 - outer0;
        QVERIFY(std::abs(d.x() * n.x() + d.y() * n.y()) < 1e-3);
        for (const geometry::Vertex &v : m.vertices)
            QVERIFY(std::isfinite(v.x) && std::isfinite(v.y));
    }

    void lineMesh()
    {
        geometry::MeshChunks chunks;
        // 1 px wide with a 1 px fringe: one 2 px quad at half the ink — the same coverage.
        geometry::appendLine(chunks, QPointF(0, 0), QPointF(10, 0), 1, geometry::premultiplied(Qt::black), 1);
        const geometry::Mesh &m = chunks.chunks()[0];
        QCOMPARE(int(m.vertices.size()), 4);
        QCOMPARE(int(m.indices.size()), 6);
        for (const geometry::Vertex &v : m.vertices) {
            QCOMPARE(int(v.a), 128);
            QCOMPARE(std::abs(v.y), 1.0f);
        }
        QCOMPARE(m.vertices[0].x, 0.0f);
        QCOMPARE(m.vertices[1].x, 10.0f);
        // A diagonal one stays perpendicular, malformed ones add nothing.
        chunks.clear();
        geometry::appendLine(chunks, QPointF(-3, 4), QPointF(5, -2), 0.5, geometry::premultiplied(Qt::black), 0.25);
        const geometry::Mesh &d = chunks.chunks()[0];
        const double across = std::hypot(d.vertices[3].x - d.vertices[0].x, d.vertices[3].y - d.vertices[0].y);
        QVERIFY(std::abs(across - 0.75) < 1e-5);
        const double nan = std::numeric_limits<double>::quiet_NaN();
        geometry::appendLine(chunks, QPointF(1, 1), QPointF(1, 1), 1, geometry::premultiplied(Qt::black), 1);
        geometry::appendLine(chunks, QPointF(nan, 1), QPointF(1, 1), 1, geometry::premultiplied(Qt::black), 1);
        geometry::appendLine(chunks, QPointF(0, 0), QPointF(1, 1), 0, geometry::premultiplied(Qt::black), 1);
        QCOMPARE(chunks.vertexCount(), 4);
    }

    void chunksSplitAt65535()
    {
        geometry::MeshChunks chunks;
        for (int i = 0; i < 6000; ++i)
            geometry::appendDisc(chunks, QPointF(i % 100, i / 100), 0.4, geometry::premultiplied(Qt::blue), 0.1, 16);
        QCOMPARE(chunks.vertexCount(), 6000 * 33);
        QVERIFY(chunks.chunks().size() >= 4);
        for (const geometry::Mesh &m : chunks.chunks()) {
            QVERIFY(m.vertices.size() <= 65535);
            QCOMPARE(m.vertices.size() % 33, size_t(0)); // a disc never spans two chunks
            for (const quint16 i : m.indices)
                QVERIFY(i < m.vertices.size());
        }
        // clear() keeps the allocations and starts over.
        chunks.clear();
        QVERIFY(chunks.isEmpty());
        geometry::appendDisc(chunks, QPointF(0, 0), 1, geometry::premultiplied(Qt::blue), 0.1, 8);
        QCOMPARE(int(chunks.chunks().size()), 1);
        QCOMPARE(chunks.vertexCount(), 17);
    }

    void roundedRectMesh()
    {
        geometry::MeshChunks chunks;
        geometry::appendRoundedRect(chunks, QRectF(10, 10, 60, 16), 3, geometry::premultiplied(Qt::white), 1);
        const geometry::Mesh &m = chunks.chunks()[0];
        QCOMPARE(int(m.vertices.size()), 41);
        for (const quint16 i : m.indices)
            QVERIFY(i < m.vertices.size());
        // The opaque outline stays inside the rect, the fringe reaches half a pixel beyond it.
        for (int k = 1; k <= 20; ++k)
            QVERIFY(QRectF(9.99, 9.99, 60.02, 16.02)
                            .contains(QPointF(m.vertices[size_t(k)].x, m.vertices[size_t(k)].y)));
        for (int k = 21; k <= 40; ++k) {
            QCOMPARE(int(m.vertices[size_t(k)].a), 0);
            QVERIFY(QRectF(9.49, 9.49, 61.02, 17.02)
                            .contains(QPointF(m.vertices[size_t(k)].x, m.vertices[size_t(k)].y)));
        }
        chunks.clear();
        geometry::appendRoundedRect(chunks, QRectF(0, 0, 0, 10), 3, geometry::premultiplied(Qt::white), 1);
        geometry::appendRoundedRect(chunks, QRectF(std::numeric_limits<double>::quiet_NaN(), 0, 5, 5), 3,
                                    geometry::premultiplied(Qt::white), 1);
        QVERIFY(chunks.isEmpty());
    }

    void placeLabels_data()
    {
        QTest::addColumn<QList<QPointF>>("centres");
        QTest::addColumn<QList<bool>>("always");
        QTest::addColumn<double>("radius");
        QTest::addColumn<int>("maxLabels");
        QTest::addColumn<QList<int>>("slots"); // 0 below, 1 above, 2 right, 3 left, -1 not placed
        QTest::newRow("far apart: all below") << QList<QPointF>{{0, 0}, {300, 0}, {0, 300}}
                                              << QList<bool>{false, false, false} << 5.0 << 260 << QList<int>{0, 0, 0};
        QTest::newRow("neighbours: the second above") << QList<QPointF>{{100, 100}, {104, 110}}
                                                      << QList<bool>{false, false} << 5.0 << 260 << QList<int>{0, 1};
        // A big disc: its four slots do not touch each other.
        QTest::newRow("the four slots in order")
                << QList<QPointF>{{100, 100}, {100, 100}, {100, 100}, {100, 100}, {100, 100}} << QList<bool>(5, false)
                << 30.0 << 260 << QList<int>{0, 1, 2, 3, -1};
        QTest::newRow("the highlighted one overlaps")
                << QList<QPointF>{{100, 100}, {100, 100}} << QList<bool>{false, true} << 5.0 << 260 << QList<int>{0, 0};
        QTest::newRow("cut at maxLabels") << QList<QPointF>{{0, 0}, {300, 0}, {600, 0}, {900, 0}}
                                          << QList<bool>(4, false) << 5.0 << 2 << QList<int>{0, 0, -1, -1};
        const double nan = std::numeric_limits<double>::quiet_NaN();
        QTest::newRow("malformed centre")
                << QList<QPointF>{{nan, 0}, {0, 0}} << QList<bool>{false, false} << 5.0 << 260 << QList<int>{-1, 0};
    }

    void placeLabels()
    {
        QFETCH(QList<QPointF>, centres);
        QFETCH(QList<bool>, always);
        QFETCH(double, radius);
        QFETCH(int, maxLabels);
        QFETCH(QList<int>, slots);
        QFont font;
        font.setPixelSize(11);
        const QFontMetricsF fm(font);
        const QStringList titles{u"Żółw"_s, u"Zażółć gęślą jaźń"_s, u"wt 13 paź"_s, u"#projekt"_s, u"Łódź"_s};
        std::vector<geometry::LabelCandidate> candidates;
        for (qsizetype k = 0; k < centres.size(); ++k) {
            geometry::LabelCandidate c;
            c.node = int(k);
            c.centre = centres.at(k);
            c.radius = radius;
            const QString title = titles.at(k % titles.size());
            c.text = QSizeF(fm.horizontalAdvance(title), fm.height());
            c.always = always.at(k);
            candidates.push_back(c);
        }
        const QList<QRectF> rects = geometry::placeLabels(candidates, maxLabels);
        QCOMPARE(rects.size(), centres.size());
        for (qsizetype k = 0; k < rects.size(); ++k) {
            const QRectF &r = rects.at(k);
            const int slot = slots.at(k);
            if (slot < 0) {
                QVERIFY2(r.isNull(), qPrintable(QString::number(k)));
                continue;
            }
            const geometry::LabelCandidate &c = candidates[size_t(k)];
            QCOMPARE(r.size(), QSizeF(c.text.width() + 6, c.text.height() + 2));
            const QPointF p = c.centre;
            switch (slot) {
            case 0:
                QCOMPARE(r.top(), p.y() + c.radius + 2);
                QCOMPARE(r.center().x(), p.x());
                break;
            case 1:
                QCOMPARE(r.bottom(), p.y() - c.radius - 2);
                QCOMPARE(r.center().x(), p.x());
                break;
            case 2: QCOMPARE(r.left(), p.x() + c.radius + 3); break;
            case 3: QCOMPARE(r.right(), p.x() - c.radius - 3); break;
            }
        }
        // Placed labels never overlap, except a highlighted one.
        for (qsizetype i = 0; i < rects.size(); ++i) {
            for (qsizetype j = i + 1; j < rects.size(); ++j) {
                if (!rects.at(i).isNull() && !rects.at(j).isNull() && !always.at(i) && !always.at(j))
                    QVERIFY(!rects.at(i).intersects(rects.at(j)));
            }
        }
    }

    void labelsElideLongTitles()
    {
        Files files = smallVault();
        files.append({u"Zażółć gęślą jaźń — notatki ze spotkania zespołu projektowego o planach na drugi kwartał.md"_s,
                      u"[[A]]\n"_s});
        auto index = makeIndex(files);
        GraphModel m;
        m.setActive(false);
        m.setVaultIndex(index.get());
        GraphItem item;
        QFont font;
        font.setPixelSize(11);
        item.setFont(font);
        item.setModel(&m);
        const int longOne = m.indexOf(files.last().first);
        QVERIFY(longOne >= 0);
        QVERIFY(item.labelSize(longOne).width() <= 220);
        QVERIFY(item.labelSize(longOne).width() > 100);
        const int turtle = m.indexOf(u"Żółw.md"_s);
        QCOMPARE(item.labelSize(turtle),
                 QSizeF(QFontMetricsF(font).horizontalAdvance(u"Żółw"_s), QFontMetricsF(font).height()));
    }

    void painterLabelsUseTheSharedRule()
    {
        // The QPainter path draws exactly the boxes of labelsFor() (the scene graph's too).
        auto index = makeIndex(bigVault(120, 3));
        GraphModel m;
        m.setActive(false);
        m.setVaultIndex(index.get());
        m.step(300);
        GraphItem item;
        item.setSize(QSizeF(800, 600));
        item.setModel(&m);
        item.setSelectedId(m.nodes().at(5).id);
        for (const double zoom : {0.5, 1.0, 2.5}) {
            item.setZoom(zoom);
            QImage img(800, 600, QImage::Format_ARGB32_Premultiplied);
            {
                QPainter p(&img);
                item.render(&p, img.size());
            }
            QList<QRectF> expected;
            for (const auto &l : item.labelsFor(QSizeF(800, 600)))
                expected.append(l.rect);
            QVERIFY(!expected.isEmpty());
            QCOMPARE(item.sceneStats().labelRects, expected);
            QVERIFY(expected.size() <= GraphItem::MaxLabels);
        }
    }

    void meshOfTheGraph()
    {
        auto index = makeIndex(smallVault());
        GraphModel m;
        m.setActive(false);
        m.setShowMissing(true);
        m.setVaultIndex(index.get());
        m.step(300);
        GraphItem item;
        item.setSize(QSizeF(800, 600));
        item.setModel(&m);
        geometry::MeshChunks mesh;
        item.buildMesh(mesh, 1.0);
        // 7 edges (one quad each) and 9 discs; the missing page is a disc and a ring.
        int discVertices = 0;
        for (int i = 0; i < m.nodeCount(); ++i) {
            const double r = std::max(1.6, m.nodes().at(i).radius * item.zoom());
            const int segments = geometry::discSegments(r);
            discVertices += 1 + 2 * segments;
            if (m.nodes().at(i).kind == GraphModel::Kind::Missing)
                discVertices += 4 * segments;
        }
        QCOMPARE(mesh.vertexCount(), m.edgeCount() * 4 + discVertices);
        // Selecting a node adds its highlight ring (and draws its edges strong, on top).
        item.setSelectedId(u"A.md"_s);
        geometry::MeshChunks selected;
        item.buildMesh(selected, 1.0);
        QVERIFY(selected.vertexCount() > mesh.vertexCount());
        // Every vertex is finite and inside the graph's bounds (plus the rings and fringes).
        const QRectF b = m.bounds().adjusted(-20, -20, 20, 20);
        for (const geometry::Mesh &chunk : selected.chunks()) {
            for (const geometry::Vertex &v : chunk.vertices)
                QVERIFY(b.contains(QPointF(v.x, v.y)));
        }
        // A higher device pixel ratio makes the fringe thinner, not the shapes.
        geometry::MeshChunks hidpi;
        item.buildMesh(hidpi, 2.0);
        QCOMPARE(hidpi.vertexCount(), selected.vertexCount());
    }

    void emptyVault()
    {
        auto index = makeIndex({});
        GraphModel m;
        m.setVaultIndex(index.get());
        QCOMPARE(m.nodeCount(), 0);
        QCOMPARE(m.pageCount(), 0);
        QVERIFY(!m.running());
        QVERIFY(m.bounds().isNull());
        GraphItem item;
        item.setSize(QSizeF(400, 300));
        item.setModel(&m);
        QImage img(400, 300, QImage::Format_ARGB32_Premultiplied);
        QPainter p(&img);
        item.render(&p, img.size()); // nothing to draw, no crash
    }

    // ---- GraphItem -----------------------------------------------------------------------------

    void cameraAndHitTesting()
    {
        auto index = makeIndex(smallVault());
        GraphModel m;
        m.setActive(false);
        m.setVaultIndex(index.get());
        m.step(300);
        GraphItem item;
        item.setSize(QSizeF(800, 600));
        item.setAnimationDuration(0);
        item.setModel(&m);
        QVERIFY(item.autoFit());
        // Auto-fit: every node is inside the item.
        for (const auto &n : m.nodes()) {
            const QPointF p = item.nodePosition(n.id);
            QVERIFY(p.x() >= 0 && p.x() <= 800 && p.y() >= 0 && p.y() <= 600);
        }
        const QPointF a = item.nodePosition(u"A.md"_s);
        QCOMPARE(item.nodeAt(a.x(), a.y()), u"A.md"_s);
        QCOMPARE(item.nodeAt(a.x() + 2, a.y() - 2), u"A.md"_s);
        QCOMPARE(item.nodeAt(-50, -50), QString());

        // Zooming around a point keeps that point under the cursor.
        const QPointF before = item.nodePosition(u"B.md"_s);
        item.zoomBy(2.0, before.x(), before.y());
        QVERIFY(!item.autoFit()); // the user took over
        const QPointF after = item.nodePosition(u"B.md"_s);
        QVERIFY(std::abs(after.x() - before.x()) < 0.01 && std::abs(after.y() - before.y()) < 0.01);
        item.panBy(30, -10);
        QVERIFY(std::abs(item.nodePosition(u"B.md"_s).x() - (after.x() + 30)) < 0.01);
        item.centerOn(u"E.md"_s, 1.5, false);
        QCOMPARE(item.zoom(), 1.5);
        QVERIFY(std::abs(item.nodePosition(u"E.md"_s).x() - 400) < 0.01);
        // While the layout still moves, the camera follows the node it was centred on.
        m.reheat(0.5);
        m.step(20);
        QVERIFY(std::abs(item.nodePosition(u"E.md"_s).x() - 400) < 0.01);
        item.panBy(10, 0); // the user takes over: no more following
        m.step(20);
        QVERIFY(std::abs(item.nodePosition(u"E.md"_s).x() - 400) > 0.01);
        // Animated: the camera arrives at the node.
        item.setAnimationDuration(50);
        item.centerOn(u"C.md"_s, 1.0, true);
        QTRY_VERIFY(std::abs(item.nodePosition(u"C.md"_s).x() - 400) < 0.5);
        item.setZoom(1000);
        QCOMPARE(item.zoom(), item.maxZoom());
        item.resetView();
        QVERIFY(item.autoFit());
    }

    void selectionFollowsRebuilds()
    {
        auto index = makeIndex(smallVault());
        GraphModel m;
        m.setActive(false);
        m.setRefreshDelay(0);
        m.setVaultIndex(index.get());
        GraphItem item;
        item.setSize(QSizeF(800, 600));
        item.setModel(&m);
        QSignalSpy highlighted(&item, &GraphItem::highlightedIdChanged);
        item.setSelectedId(u"C.md"_s);
        QCOMPARE(item.highlightedId(), u"C.md"_s);
        QCOMPARE(highlighted.count(), 1);
        QVERIFY(!item.highlightedPosition().isNull());
        // Rebuild with other indices: the selection is kept by id.
        index->update(u"0.md"_s, u"[[C]]\n"_s);
        QTRY_VERIFY(m.indexOf(u"0.md"_s) >= 0);
        QCOMPARE(item.selectedId(), u"C.md"_s);
        // The selected page goes away: the selection is cleared.
        index->remove(u"C.md"_s);
        QTRY_COMPARE(item.selectedId(), QString());
    }

    void paintsNodesEdgesAndLabels()
    {
        auto index = makeIndex(smallVault());
        GraphModel m;
        m.setActive(false);
        m.setVaultIndex(index.get());
        m.step(300);
        GraphItem item;
        item.setSize(QSizeF(600, 400));
        item.setModel(&m);
        const QColor bg(u"#fbf7ee");
        item.setBackgroundColor(bg);
        QImage img(600, 400, QImage::Format_ARGB32_Premultiplied);
        {
            QPainter p(&img);
            item.render(&p, img.size());
        }
        QVERIFY(hasInk(img, bg));
        QCOMPARE(img.pixelColor(2, 2), bg);
        // Dark theme colours apply at once.
        item.setBackgroundColor(QColor(u"#1c2130"));
        {
            QPainter p(&img);
            item.render(&p, img.size());
        }
        QCOMPARE(img.pixelColor(2, 2), QColor(u"#1c2130"));
    }

    void bigGraphPerformance()
    {
        // 2000 pages, ~4 links each: the budget for a smooth desktop graph.
        auto index = makeIndex(bigVault(2000, 4));
        GraphModel m;
        m.setActive(false);
        QElapsedTimer t;
        t.start();
        m.setVaultIndex(index.get());
        const qint64 build = t.elapsed();
        QCOMPARE(m.nodeCount(), 2000);
        QVERIFY(m.edgeCount() > 3000);
        t.restart();
        m.step(30);
        const double tick = double(t.elapsed()) / 30.0;

        GraphItem item;
        item.setSize(QSizeF(1280, 800));
        item.setModel(&m);
        QImage img(1280, 800, QImage::Format_ARGB32_Premultiplied);
        double paint = 0;
        for (int k = 0; k < 5; ++k) {
            QPainter p(&img);
            t.restart();
            item.render(&p, img.size());
            paint += double(t.nsecsElapsed()) / 1e6;
        }
        paint /= 5;
        // Zoomed in: labels for every visible node.
        item.centerOn(m.nodes().first().id, 2.0, false);
        QPainter p(&img);
        t.restart();
        item.render(&p, img.size());
        const double labelled = double(t.nsecsElapsed()) / 1e6;
        // The scene graph's meshes of the whole graph (what a moving frame rebuilds), and the
        // labels of a frame.
        item.resetView();
        geometry::MeshChunks mesh;
        double meshMs = 0;
        for (int k = 0; k < 5; ++k) {
            t.restart();
            item.buildMesh(mesh, 1.0);
            meshMs += double(t.nsecsElapsed()) / 1e6;
        }
        meshMs /= 5;
        t.restart();
        const auto labels = item.labelsFor(QSizeF(1280, 800));
        const double labelMs = double(t.nsecsElapsed()) / 1e6;
        qInfo("2000 nodes / %d edges: build %lld ms, tick %.2f ms, paint %.2f ms, zoomed paint %.2f ms, "
              "mesh %.2f ms (%d vertices, %d chunks), labels %.2f ms (%d)",
              m.edgeCount(), static_cast<long long>(build), tick, paint, labelled, meshMs, mesh.vertexCount(),
              int(mesh.chunks().size()), labelMs, int(labels.size()));
        // Generous bounds (debug builds, shared CI machines); release builds are ~5x faster.
        QVERIFY2(tick < 60 * BRULION_TEST_SLOWDOWN, qPrintable(QString::number(tick)));
        QVERIFY2(paint < 250 * BRULION_TEST_SLOWDOWN, qPrintable(QString::number(paint)));
        QVERIFY2(meshMs < 40 * BRULION_TEST_SLOWDOWN, qPrintable(QString::number(meshMs)));
    }
};

QTEST_MAIN(TestGraph)
#include "tst_planning_graph.moc"
