// AtramentItem inside Qt Quick on QRhi: a QQuickRenderControl draws a window into an offscreen
// Vulkan texture (the QRhi of OffscreenRhi, so no display is needed), with the item's GPU render
// node in Qt Quick's own render pass — moved, clipped by a parent, faded — and with the CPU
// renderer's texture (BRULION_ATRAMENT_RENDERER=cpu, switched at run time). Both must give what the
// CPU renderer gives for the same view on the test platform.
//
// A process of its own: the graphics API of Qt Quick is chosen once per process (Vulkan here;
// tst_atrament_qt sees the offscreen platform plugin's software backend).

#include "gpuframe.h"
#include "offscreenrhi.h"
#include "qrhirenderer.h"
#include "qtfixtures.h"
#include "qthost.h"
#include "raster/raster.h"
#include "ui/testplatform.h"

#include <QDir>
#include <QImage>
#include <QQuickGraphicsDevice>
#include <QQuickRenderControl>
#include <QQuickRenderTarget>
#include <QQuickWindow>
#include <QSGRendererInterface>
#include <QtTest>
#include <rhi/qrhi.h>

using namespace brulion::atrament;
using namespace qtfixtures;

namespace {

constexpr int kWidth = 320;
constexpr int kHeight = 240;
constexpr QSize kItemSize(280, 200);

} // namespace

class TestAtramentQtItem : public QObject {
    Q_OBJECT

private Q_SLOTS:
    void initTestCase();
    void cleanupTestCase();
    void gpuRenderNode();
    void cpuTexture();

private:
    // Renders the window once; the pixels top row first, premultiplied.
    QImage renderWindow();
    // Card at the item's size over the window's colour: the CPU renderer's frame, or faded by
    // `opacity` the GPU renderer's offscreen one.
    QImage reference(float opacity);
    // The window: `expected` at `at` within `clip`, the window's colour elsewhere.
    void compareRegion(const QImage &window, QPoint at, const QImage &expected, const QRect &clip, int tolerance);

    std::unique_ptr<OffscreenRhi> m_rhi;
    std::unique_ptr<QQuickRenderControl> m_control;
    std::unique_ptr<QQuickWindow> m_window;
    std::unique_ptr<QRhiTexture> m_texture;
    std::unique_ptr<QRhiRenderBuffer> m_depthStencil;
    std::unique_ptr<QRhiTextureRenderTarget> m_target;
    std::unique_ptr<QRhiRenderPassDescriptor> m_renderPass;
    std::shared_ptr<QtPlatform> m_platform;
    std::unique_ptr<App> m_app;
    QQuickItem *m_clip = nullptr;
    AtramentItem *m_item = nullptr;
};

void TestAtramentQtItem::initTestCase()
{
    if (!atramentGpuShadersBuilt())
        QSKIP("built without Qt Shader Tools: no GPU renderer");
    QString why;
    m_rhi = OffscreenRhi::create(OffscreenRhi::Backend::Vulkan, &why);
    if (!m_rhi)
        QSKIP(qPrintable(QStringLiteral("no Vulkan here: ") + why));
    qInfo("QRhi backend: %s", qPrintable(m_rhi->description()));
    // The QRhi adaptation even under the offscreen platform plugin (which asks for the software
    // one), on Vulkan.
    QQuickWindow::setSceneGraphBackend(QStringLiteral("rhi"));
    QQuickWindow::setGraphicsApi(QSGRendererInterface::Vulkan);
    m_control = std::make_unique<QQuickRenderControl>();
    m_window = std::make_unique<QQuickWindow>(m_control.get());
    m_window->setVulkanInstance(m_rhi->vulkanInstance());
    m_window->setGraphicsDevice(QQuickGraphicsDevice::fromRhi(m_rhi->rhi()));
    m_window->setColor(QColor(0x20, 0x30, 0x40));
    m_window->setGeometry(0, 0, kWidth, kHeight);
    m_window->contentItem()->setSize(QSizeF(kWidth, kHeight));
    QVERIFY(m_control->initialize());
    QRhi *rhi = m_rhi->rhi();
    m_texture.reset(rhi->newTexture(QRhiTexture::RGBA8, QSize(kWidth, kHeight), 1,
                                    QRhiTexture::RenderTarget | QRhiTexture::UsedAsTransferSource));
    QVERIFY(m_texture->create());
    m_depthStencil.reset(rhi->newRenderBuffer(QRhiRenderBuffer::DepthStencil, QSize(kWidth, kHeight)));
    QVERIFY(m_depthStencil->create());
    QRhiTextureRenderTargetDescription description{QRhiColorAttachment(m_texture.get())};
    description.setDepthStencilBuffer(m_depthStencil.get());
    m_target.reset(rhi->newTextureRenderTarget(description));
    m_renderPass.reset(m_target->newCompatibleRenderPassDescriptor());
    m_target->setRenderPassDescriptor(m_renderPass.get());
    QVERIFY(m_target->create());
    m_window->setRenderTarget(QQuickRenderTarget::fromRhiRenderTarget(m_target.get()));

    // The item at a fractional-free offset inside a parent that clips it (a scissor in Qt Quick).
    m_platform = std::make_shared<QtPlatform>();
    m_app = std::make_unique<App>(m_platform, loadFonts());
    m_clip = new QQuickItem(m_window->contentItem());
    m_clip->setPosition(QPointF(10, 12));
    m_clip->setSize(QSizeF(250, 190));
    m_clip->setClip(true);
    m_item = new AtramentItem(m_clip);
    m_item->setPosition(QPointF(7, 5));
    m_item->setSize(QSizeF(kItemSize));
    Entity<Card> root = m_app->newEntity<Card>();
    m_item->attach(*m_app, AnyView{root.id(), &renderView<Card>});
}

void TestAtramentQtItem::cleanupTestCase()
{
    delete m_item; // before the App
    m_item = nullptr;
    m_window.reset();
    m_control.reset();
    m_target.reset();
    m_renderPass.reset();
    m_depthStencil.reset();
    m_texture.reset();
    m_app.reset();
    m_rhi.reset();
}

QImage TestAtramentQtItem::renderWindow()
{
    m_control->polishItems();
    m_control->beginFrame();
    m_control->sync();
    m_control->render();
    QRhiReadbackResult result;
    QRhiResourceUpdateBatch *readback = m_rhi->rhi()->nextResourceUpdateBatch();
    readback->readBackTexture({m_texture.get()}, &result);
    m_control->commandBuffer()->resourceUpdate(readback);
    m_control->endFrame();
    QImage image(reinterpret_cast<const uchar *>(result.data.constData()), result.pixelSize.width(),
                 result.pixelSize.height(), QImage::Format_RGBA8888_Premultiplied);
    return image.copy();
}

QImage TestAtramentQtItem::reference(float opacity)
{
    auto platform = std::make_shared<TestPlatform>();
    App app(platform, loadFonts());
    TestWindow &tw = platform->newWindow({float(kItemSize.width()), float(kItemSize.height())}, 1);
    Entity<Card> root = app.newEntity<Card>();
    app.openWindow(tw, root);
    tw.settle();
    const QColor c = m_window->color();
    const Color background = rgb(std::uint32_t(c.rgb() & 0xffffff));
    std::vector<Pixel> pixels;
    if (opacity == 1) {
        // The CPU renderer over the window's colour: what the item composites over it.
        RasterTarget target;
        target.resize(tw.image().width, tw.image().height);
        RasterRenderer().render(*tw.lastScene(), app.atlas(), target, background);
        pixels = std::move(target.pixels);
    } else {
        // Faded, every primitive × the opacity (Qt Quick's inherited opacity, as Qt Quick fades its
        // own nodes): the GPU renderer offscreen, whose opacity tst_atrament_qt checks.
        GpuFrameBuilder builder;
        QRhiRenderer renderer;
        renderer.setFrame(std::make_shared<const GpuFrame>(
                builder.build(*tw.lastScene(), app.atlas(), kItemSize.width(), kItemSize.height())));
        renderer.syncAtlas(QRhiRenderer::AtlasSlot::Atlas, app.atlas());
        renderer.syncAtlas(QRhiRenderer::AtlasSlot::Paths, builder.pathAtlas());
        auto gpu = m_rhi->render(renderer, background, true, opacity);
        if (!gpu)
            return {};
        pixels = std::move(*gpu);
    }
    QImage image(reinterpret_cast<const uchar *>(pixels.data()), kItemSize.width(), kItemSize.height(),
                 kItemSize.width() * 4, QImage::Format_RGBA8888_Premultiplied);
    return image.copy();
}

void TestAtramentQtItem::compareRegion(const QImage &window, QPoint at, const QImage &expected, const QRect &clip,
                                       int tolerance)
{
    QVERIFY(!expected.isNull());
    const QColor background = m_window->color();
    int largest = 0;
    int over = 0;
    QPoint first;
    for (int y = 0; y < window.height(); ++y) {
        for (int x = 0; x < window.width(); ++x) {
            // The item where the clip lets it through, the window's colour elsewhere.
            const QPoint local = QPoint(x, y) - at;
            int r = background.red();
            int g = background.green();
            int b = background.blue();
            if (clip.contains(QPoint(x, y)) && expected.rect().contains(local)) {
                const uchar *p = expected.constScanLine(local.y()) + local.x() * 4;
                r = p[0];
                g = p[1];
                b = p[2];
            }
            const uchar *q = window.constScanLine(y) + x * 4;
            const int d = std::max({std::abs(int(q[0]) - r), std::abs(int(q[1]) - g), std::abs(int(q[2]) - b)});
            largest = std::max(largest, d);
            if (d > tolerance && over++ == 0)
                first = QPoint(x, y);
        }
    }
    qInfo("largest difference: %d", largest);
    if (over > 0) {
        const QString path = QDir::temp().filePath(
                QStringLiteral("atrament-qt-item-%1.png").arg(QLatin1String(QTest::currentTestFunction())));
        window.save(path);
        QFAIL(qPrintable(QStringLiteral("%1 pixels differ by more than %2, first at %3,%4 (window written to %5)")
                                                .arg(over)
                                                .arg(tolerance)
                                                .arg(first.x())
                                                .arg(first.y())
                                                .arg(path)));
    }
}

void TestAtramentQtItem::gpuRenderNode()
{
    QImage window;
    // The first frames: the view settles (its first frame asks for another).
    for (int i = 0; i < 3; ++i)
        window = renderWindow();
    QVERIFY(m_item->usesGpuRenderer());
    QVERIFY(m_item->frameImage().isNull());
    const QRect clip(10, 12, 250, 190);
    compareRegion(window, QPoint(17, 17), reference(1), clip, 2);
    if (QTest::currentTestFailed())
        return;
    // Faded by a parent: Qt Quick's inherited opacity reaches the shaders.
    m_clip->setOpacity(0.5);
    window = renderWindow();
    m_clip->setOpacity(1);
    compareRegion(window, QPoint(17, 17), reference(0.5f), clip, 2);
}

void TestAtramentQtItem::cpuTexture()
{
    qputenv("BRULION_ATRAMENT_RENDERER", "cpu");
    m_item->atramentWindow()->refresh();
    QImage window;
    for (int i = 0; i < 2; ++i)
        window = renderWindow();
    qunsetenv("BRULION_ATRAMENT_RENDERER");
    QVERIFY(!m_item->usesGpuRenderer());
    QVERIFY(!m_item->frameImage().isNull());
    const QRect clip(10, 12, 250, 190);
    compareRegion(window, QPoint(17, 17), reference(1), clip, 2);
    if (QTest::currentTestFailed())
        return;
    // And back to the GPU.
    m_item->atramentWindow()->refresh();
    window = renderWindow();
    QVERIFY(m_item->usesGpuRenderer());
    compareRegion(window, QPoint(17, 17), reference(1), clip, 2);
}

QTEST_MAIN(TestAtramentQtItem)
#include "tst_atrament_qt_item.moc"
