// The graph: ForceLayout (seeded determinism, convergence, Barnes–Hut close to the exact
// forces, pinned nodes), GraphModel (kinds, toggles for tags / missing pages / journals /
// orphans, local graph depth 1 and 2, the node cap, positions kept across index changes,
// search with Polish letters, reduced motion) and GraphItem (camera maths, hit-testing, touch
// selection, painting — also 2000 nodes, with timings).
#include "forcelayout.h"
#include "graphitem.h"
#include "graphmodel.h"
#include "planningtest.h"

#include <QElapsedTimer>
#include <QImage>
#include <QPainter>
#include <QSignalSpy>
#include <QTest>

#include <cmath>

using namespace Qt::StringLiterals;
using brulion::planning::ForceLayout;
using brulion::planning::GraphItem;
using brulion::planning::GraphModel;
using brulion::planning::test::Files;
using brulion::planning::test::makeIndex;

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

    void reducedMotionSettlesAtOnce()
    {
        auto index = makeIndex(bigVault(200, 3));
        GraphModel m;
        m.setReducedMotion(true);
        m.setActive(false);
        QElapsedTimer t;
        t.start();
        m.setVaultIndex(index.get());
        qInfo("reduced motion: settled in %lld ms", static_cast<long long>(t.elapsed()));
        if (BRULION_TEST_SLOWDOWN > 1)
            QSKIP("settling has a 150 ms budget, which sanitizer builds cannot meet");
        QVERIFY(m.alpha() < 0.01 || !m.layout().running());
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

    void runsOnTimerAndStops()
    {
        auto index = makeIndex(smallVault());
        GraphModel m;
        m.setTickInterval(1);
        QSignalSpy running(&m, &GraphModel::runningChanged);
        m.setVaultIndex(index.get());
        QVERIFY(m.running());
        QTRY_VERIFY_WITH_TIMEOUT(!m.running(), 10000);
        QVERIFY(running.count() >= 2);
        m.reheat(0.5);
        QVERIFY(m.running());
        m.setActive(false);
        QVERIFY(!m.running());
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
        qInfo("2000 nodes / %d edges: build %lld ms, tick %.2f ms, paint %.2f ms, zoomed paint %.2f ms",
              m.edgeCount(), static_cast<long long>(build), tick, paint, labelled);
        // Generous bounds (debug builds, shared CI machines); release builds are ~5x faster.
        QVERIFY2(tick < 60, qPrintable(QString::number(tick)));
        QVERIFY2(paint < 250, qPrintable(QString::number(paint)));
    }
};

QTEST_MAIN(TestGraph)
#include "tst_planning_graph.moc"
