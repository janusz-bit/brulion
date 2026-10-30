// GraphItem in a real Qt Quick window. Two ctest entries run this file:
// * tst_planning_graph_sg — the scene-graph renderer on the RHI null backend (a real scene graph,
//   no pixels): the meshes are built once and rebuilt only for positions, zoom, colours and the
//   highlight — panning only moves the camera —, the world meshes are split into 16-bit chunks,
//   labels are capped and placed exactly where the QPainter path places them, and the window can
//   go away with the item;
// * tst_planning_graph_sg_software — QT_QUICK_BACKEND=software (tests, screenshots, the Raspberry
//   Pi): the QPainter frame in an image node, grabbed from the window.
// By hand under an X server, the scene graph on OpenGL (Mesa's llvmpipe in CI) with its pixels
// compared to the QPainter path's:
//   xvfb-run -a env QT_QPA_PLATFORM=xcb BRULION_GRAPH_SG_API=opengl tests/app/planning/tst_planning_graph_sg
#include "graphitem.h"
#include "graphmodel.h"
#include "planningtest.h"

#include <QElapsedTimer>
#include <QImage>
#include <QPainter>
#include <QQuickItem>
#include <QQuickWindow>
#include <QSGRendererInterface>
#include <QSignalSpy>
#include <QTest>

#include <memory>

using namespace Qt::StringLiterals;
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
            const int target = (next() % 4 == 0) ? int(next() % quint32(pages)) : (p + 1 + int(next() % 12)) % pages;
            content += u"- [[Strona %1]]\n"_s.arg(target);
        }
        files.append({u"Strona %1.md"_s.arg(p), content});
    }
    return files;
}

/// The second ctest entry asks for the software renderer.
bool softwareBackend()
{
    return qEnvironmentVariable("QT_QUICK_BACKEND") == u"software"_s;
}

/// The RHI backend asked for: Null (default) or OpenGL (BRULION_GRAPH_SG_API=opengl).
QSGRendererInterface::GraphicsApi rhiApi()
{
    return qEnvironmentVariable("BRULION_GRAPH_SG_API") == u"opengl"_s ? QSGRendererInterface::OpenGL
                                                                        : QSGRendererInterface::Null;
}

/// Sum of the channel differences.
int colourDistance(const QColor &a, const QColor &b)
{
    return std::abs(a.red() - b.red()) + std::abs(a.green() - b.green()) + std::abs(a.blue() - b.blue());
}

/// A window with a GraphItem filling it.
struct Scene {
    QQuickWindow window;
    GraphItem *item = nullptr;
    std::unique_ptr<QSignalSpy> swapped;

    Scene(GraphModel *model, QSize size = QSize(800, 600))
    {
        window.resize(size);
        item = new GraphItem(window.contentItem());
        item->setSize(QSizeF(size));
        item->setAnimationDuration(0);
        item->setBackgroundColor(QColor(u"#fbf7ee"_s));
        item->setModel(model);
        swapped = std::make_unique<QSignalSpy>(&window, &QQuickWindow::frameSwapped);
        window.show();
    }
    /// Shown, and the first frame synchronised; false when none comes.
    bool firstFrame(int timeoutMs = 5000)
    {
        if (!QTest::qWaitForWindowExposed(&window))
            return false;
        QElapsedTimer t;
        t.start();
        while (item->sceneStats().frames == 0 && t.elapsed() < timeoutMs)
            QTest::qWait(5);
        return item->sceneStats().frames > 0;
    }
    /// Waits for a frame after the current one; false when none comes.
    bool nextFrame(int timeoutMs = 5000)
    {
        const int frames = item->sceneStats().frames;
        QElapsedTimer t;
        t.start();
        while (item->sceneStats().frames <= frames && t.elapsed() < timeoutMs)
            QTest::qWait(5);
        // Let the frame finish (the sync is over once the stats changed).
        return item->sceneStats().frames > frames;
    }
};

} // namespace

class TestGraphSceneGraph : public QObject {
    Q_OBJECT

public:
    static void initMain()
    {
        // The default entry: the RHI null backend (the scene graph without a GPU). Asked for by
        // name: without a request Qt Quick takes the software renderer on platforms without
        // OpenGL (offscreen).
        if (!softwareBackend()) {
            QQuickWindow::setSceneGraphBackend(u"rhi"_s);
            QQuickWindow::setGraphicsApi(rhiApi());
        }
    }

private Q_SLOTS:
    void sceneGraphMeshes()
    {
        if (softwareBackend())
            QSKIP("the software renderer paints with QPainter (softwareImage)");
        auto index = makeIndex(smallVault());
        GraphModel m;
        m.setActive(false);
        m.setVaultIndex(index.get());
        m.step(300);
        Scene scene(&m);
        GraphItem *item = scene.item;
        if (!scene.firstFrame())
            QSKIP("no frame from the RHI null backend on this platform");
        QCOMPARE(scene.window.rendererInterface()->graphicsApi(), rhiApi());
        GraphItem::SceneStats s = item->sceneStats();
        QVERIFY(!s.software);
        QVERIFY(item->clip()); // the meshes hold nodes outside the view too
        QCOMPARE(s.meshBuilds, 1);
        QCOMPARE(s.chunks, 1);
        QVERIFY(s.vertices > 0);
        QVERIFY(s.labels > 0);

        // The labels are where the QPainter path puts them.
        const QList<QRectF> sceneLabels = s.labelRects;
        QImage img(800, 600, QImage::Format_ARGB32_Premultiplied);
        {
            QPainter p(&img);
            item->render(&p, img.size());
        }
        QCOMPARE(item->sceneStats().labelRects, sceneLabels);

        // Panning moves the camera only.
        item->panBy(40, -25);
        QVERIFY(scene.nextFrame());
        s = item->sceneStats();
        QCOMPARE(s.meshBuilds, 1);
        // Zoom, colours, the highlight and a new layout frame rebuild the meshes.
        item->zoomBy(1.6);
        QVERIFY(scene.nextFrame());
        QCOMPARE(item->sceneStats().meshBuilds, 2);
        item->setPageColor(QColor(u"#8fb8e8"_s)); // a dark theme's link colour
        item->setBackgroundColor(QColor(u"#1c2130"_s));
        QVERIFY(scene.nextFrame());
        QCOMPARE(item->sceneStats().meshBuilds, 3);
        item->setSelectedId(u"Żółw.md"_s);
        QVERIFY(scene.nextFrame());
        QCOMPARE(item->sceneStats().meshBuilds, 4);
        QVERIFY(item->sceneStats().vertices > s.vertices); // + the highlight ring
        m.reheat(0.3);
        m.step(1);
        QVERIFY(scene.nextFrame());
        QCOMPARE(item->sceneStats().meshBuilds, 5);
        item->panBy(-10, 0);
        QVERIFY(scene.nextFrame());
        QCOMPARE(item->sceneStats().meshBuilds, 5);
    }

    void bigGraphChunksAndLabels()
    {
        if (softwareBackend())
            QSKIP("the software renderer paints with QPainter (softwareImage)");
        auto index = makeIndex(bigVault(2000, 4));
        GraphModel m;
        m.setActive(false);
        m.setVaultIndex(index.get());
        m.step(30);
        Scene scene(&m, QSize(1280, 800));
        GraphItem *item = scene.item;
        if (!scene.firstFrame())
            QSKIP("no frame from the RHI null backend on this platform");
        QCOMPARE(scene.window.rendererInterface()->graphicsApi(), rhiApi());
        GraphItem::SceneStats s = item->sceneStats();
        QVERIFY2(s.vertices > 65535, qPrintable(QString::number(s.vertices)));
        QVERIFY(s.chunks >= 2);
        QVERIFY(s.labels > 0 && s.labels <= GraphItem::MaxLabels);
        // Zoomed in: every visible node wants a label — still at most MaxLabels.
        item->centerOn(m.nodes().first().id, 3.0, false);
        QVERIFY(scene.nextFrame());
        s = item->sceneStats();
        QVERIFY(s.labels > 0 && s.labels <= GraphItem::MaxLabels);
        qInfo("2000 nodes in the scene graph: %d vertices in %d chunks, %d labels, last frame %.2f ms", s.vertices,
              s.chunks, s.labels, item->lastPaintMs());
    }

    void followsAMovingLayout()
    {
        if (softwareBackend())
            QSKIP("the software renderer paints with QPainter (softwareImage)");
        auto index = makeIndex(bigVault(300, 3));
        GraphModel m;
        m.setMinRunMs(400);
        m.setVaultIndex(index.get());
        Scene scene(&m);
        GraphItem *item = scene.item;
        if (!scene.firstFrame())
            QSKIP("no frame from the RHI null backend on this platform");
        QCOMPARE(scene.window.rendererInterface()->graphicsApi(), rhiApi());
        QTRY_VERIFY_WITH_TIMEOUT(!m.running(), 20000 * BRULION_TEST_SLOWDOWN);
        QTest::qWait(100); // the settled frame
        const int builds = item->sceneStats().meshBuilds;
        QVERIFY(builds >= 2); // frames of the moving layout
        item->panBy(25, 25);
        QVERIFY(scene.nextFrame());
        QCOMPARE(item->sceneStats().meshBuilds, builds);
    }

    void windowGoesAwayWithTheItem()
    {
        auto index = makeIndex(smallVault());
        GraphModel m;
        m.setMinRunMs(200);
        m.setVaultIndex(index.get());
        {
            Scene scene(&m);
            scene.firstFrame(2000);
        } // the window, its scene graph and the item go (the layout may still run)
        QTRY_VERIFY_WITH_TIMEOUT(!m.running(), 10000);
        QVERIFY(m.alpha() < m.layout().params().alphaMin);
    }

    void openglPixelsMatchThePainter()
    {
        if (softwareBackend() || rhiApi() != QSGRendererInterface::OpenGL)
            QSKIP("run under an X server with BRULION_GRAPH_SG_API=opengl (see the file comment)");
        auto index = makeIndex(smallVault());
        GraphModel m;
        m.setActive(false);
        m.setShowMissing(true);
        m.setVaultIndex(index.get());
        m.step(300);
        Scene scene(&m);
        GraphItem *item = scene.item;
        item->setLabelZoom(1e6); // labels may cover other nodes: compare the discs
        QVERIFY(scene.firstFrame());
        QCOMPARE(scene.window.rendererInterface()->graphicsApi(), QSGRendererInterface::OpenGL);
        QTest::qWait(100);
        const QImage shot = scene.window.grabWindow().convertToFormat(QImage::Format_ARGB32);
        QVERIFY(!shot.isNull());
        const qreal dpr = shot.devicePixelRatio();
        QImage painted(shot.size(), QImage::Format_ARGB32_Premultiplied);
        painted.setDevicePixelRatio(dpr);
        {
            QPainter p(&painted);
            item->render(&p, item->size());
        }
        painted = painted.convertToFormat(QImage::Format_ARGB32);
        QVERIFY2(colourDistance(shot.pixelColor(2, 2), painted.pixelColor(2, 2)) <= 3,
                 qPrintable(shot.pixelColor(2, 2).name() + u" "_s + painted.pixelColor(2, 2).name()));
        for (const auto &node : m.nodes()) {
            const QPoint p = (item->nodePosition(node.id) * dpr).toPoint();
            const QColor a = shot.pixelColor(p);
            const QColor b = painted.pixelColor(p);
            QVERIFY2(colourDistance(a, b) <= 24, qPrintable(node.id + u": "_s + a.name() + u" vs "_s + b.name()));
        }
        // A theme switch: the same again.
        item->setBackgroundColor(QColor(u"#1c2130"_s));
        item->setPageColor(QColor(u"#8fb8e8"_s));
        QVERIFY(scene.nextFrame());
        QTest::qWait(100);
        const QImage dark = scene.window.grabWindow();
        QVERIFY(colourDistance(dark.pixelColor(2, 2), QColor(u"#1c2130"_s)) <= 3);
        const QPoint a = (item->nodePosition(u"A.md"_s) * dpr).toPoint();
        QVERIFY2(colourDistance(dark.pixelColor(a), QColor(u"#8fb8e8"_s)) <= 24, qPrintable(dark.pixelColor(a).name()));
    }

    void softwareImage()
    {
        if (!softwareBackend())
            QSKIP("run with QT_QUICK_BACKEND=software (tst_planning_graph_sg_software)");
        auto index = makeIndex(smallVault());
        GraphModel m;
        m.setActive(false);
        m.setVaultIndex(index.get());
        m.step(300);
        Scene scene(&m);
        GraphItem *item = scene.item;
        QVERIFY(scene.firstFrame());
        QCOMPARE(scene.window.rendererInterface()->graphicsApi(), QSGRendererInterface::Software);
        QVERIFY(item->sceneStats().software);
        QVERIFY(item->sceneStats().labels > 0);
        // Ink at every node (without labels, which may cover another node).
        item->setLabelZoom(1e6);
        QVERIFY(scene.nextFrame());
        QCOMPARE(item->sceneStats().labels, 0);
        const QImage shot = scene.window.grabWindow();
        QVERIFY(!shot.isNull());
        const QColor bg(u"#fbf7ee"_s);
        const qreal dpr = shot.devicePixelRatio();
        QCOMPARE(shot.pixelColor(1, 1), bg);
        for (const auto &node : m.nodes()) {
            const QPointF p = item->nodePosition(node.id) * dpr;
            const QColor c = shot.pixelColor(p.toPoint());
            const int diff = std::abs(c.red() - bg.red()) + std::abs(c.green() - bg.green()) + std::abs(c.blue() - bg.blue());
            QVERIFY2(diff > 60 || node.kind == GraphModel::Kind::Missing, qPrintable(node.id));
        }
        // Colours change at once.
        item->setBackgroundColor(QColor(u"#1c2130"_s));
        QVERIFY(scene.nextFrame());
        QCOMPARE(scene.window.grabWindow().pixelColor(1, 1), QColor(u"#1c2130"_s));
    }
};

QTEST_MAIN(TestGraphSceneGraph)
#include "tst_planning_graph_sg.moc"
