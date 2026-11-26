// atrament-gallery (docs/ATRAMENT.md M0): Atrament's elements in a Qt Quick window, or one frame
// rendered into a PNG:
//
//   atrament-gallery [--section 0-3] [--renderer gpu|cpu] [--grab out.png [--frames n]]
//   atrament-gallery --screenshot out.png [--size 1280x800] [--scale 2] [--section 1]
//                    [--hover x,y] [--frames n] [--renderer cpu|gpu] [--backend vulkan|opengl]
//
// --screenshot draws headless: with the CPU renderer (the default), or with the GPU renderer into
// an offscreen QRhi (Vulkan unless --backend opengl), and prints the mean frame times.
// --grab shows the window, waits for n frames and saves what Qt Quick drew (the render node of the
// GPU renderer, or the CPU renderer's texture) — run it under a virtual display (xvfb-run).
//                    [--accessibility]   (prints the frame's semantic tree)

#include "gallery.h"
#include "offscreenrhi.h"
#include "qthost.h"
#include "ui/testplatform.h"

#include <QCommandLineParser>
#include <QGuiApplication>
#include <QImage>
#include <QQuickWindow>
#include <QSGRendererInterface>
#include <QTextStream>
#include <QTimer>

#include <chrono>

using namespace brulion::atrament;

namespace {

const QStringList kFontFolders { QStringLiteral(":/atrament/fonts/ui"), QStringLiteral(":/atrament/fonts") };

bool savePixels(const QString &file, const Pixel *pixels, int w, int h)
{
    const QImage out(reinterpret_cast<const uchar *>(pixels), w, h, w * 4, QImage::Format_RGBA8888_Premultiplied);
    if (!out.convertToFormat(QImage::Format_ARGB32).save(file)) {
        QTextStream(stderr) << "cannot write " << file << '\n';
        return false;
    }
    return true;
}

int screenshot(const QString &file, Size size, float scale, int section, std::optional<Point> hover, int frames,
               bool gpu, OffscreenRhi::Backend backend, bool accessibility)
{
    auto platform = std::make_shared<TestPlatform>();
    App app(platform, gallery::loadFonts(kFontFolders));
    TestWindow &tw = platform->newWindow(size, scale);
    Entity<gallery::Gallery> root = app.newEntity<gallery::Gallery>();
    root.update(app, [section](gallery::Gallery &g, Context<gallery::Gallery> &) { g.section = section; });
    Window &w = app.openWindow(tw, root);
    w.setAccessibilityEnabled(accessibility);
    std::unique_ptr<OffscreenRhi> rhi;
    if (gpu) {
        QString error;
        rhi = OffscreenRhi::create(backend, &error);
        if (!rhi) {
            QTextStream(stderr) << "no QRhi: " << error << '\n';
            return 1;
        }
        tw.setRasterize(false);
    }
    tw.settle();
    if (hover)
        tw.simulateMouseMove(*hover);
    const int width = int(std::ceil(size.width * scale));
    const int height = int(std::ceil(size.height * scale));
    GpuFrameBuilder builder;
    QRhiRenderer renderer;
    std::vector<Pixel> pixels;
    // Several frames: the footer shows the previous frame's statistics, and the caches warm up.
    // Timings are the means of the frames after the first (whose uploads fill the textures).
    double build = 0, present = 0, instances = 0, gpuFrame = 0;
    int timed = 0;
    for (int i = 0; i < frames; ++i) {
        w.refresh();
        tw.settle();
        const bool counted = frames == 1 || i > 0;
        if (counted) {
            build += w.lastFrameStats().buildMicros;
            present += w.lastFrameStats().presentMicros;
            ++timed;
        }
        if (!gpu)
            continue;
        const auto t0 = std::chrono::steady_clock::now();
        renderer.setFrame(std::make_shared<const GpuFrame>(builder.build(*tw.lastScene(), app.atlas(), width, height)));
        renderer.syncAtlas(QRhiRenderer::AtlasSlot::Atlas, app.atlas());
        renderer.syncAtlas(QRhiRenderer::AtlasSlot::Paths, builder.pathAtlas());
        const auto t1 = std::chrono::steady_clock::now();
        const bool last = i == frames - 1;
        std::optional<std::vector<Pixel>> image = rhi->render(renderer, gallery::theme().background, last);
        const auto t2 = std::chrono::steady_clock::now();
        if (!image) {
            QTextStream(stderr) << "the GPU renderer cannot draw on " << rhi->description() << '\n';
            return 1;
        }
        if (counted && !last) {
            instances += std::chrono::duration<double, std::micro>(t1 - t0).count();
            gpuFrame += std::chrono::duration<double, std::micro>(t2 - t1).count();
        }
        if (last)
            pixels = std::move(*image);
    }
    if (accessibility) {
        const std::u16string tree = w.accessibilityTree().dump();
        QTextStream(stdout) << QString::fromUtf16(tree.data(), qsizetype(tree.size()));
    }
    QTextStream out(stdout);
    out << "frame " << w.frameCount() << ": " << w.lastFrameStats().primitives << " primitives, build "
        << build / timed / 1000.0 << " ms";
    if (!gpu) {
        const RasterTarget &img = tw.image();
        if (!savePixels(file, img.pixels.data(), img.width, img.height))
            return 1;
        out << ", CPU raster " << present / timed / 1000.0 << " ms (mean of " << timed << ")\n";
        return 0;
    }
    if (!savePixels(file, pixels.data(), width, height))
        return 1;
    const int gpuTimed = std::max(1, timed - 1); // the last frame read the pixels back: not timed
    out << ", GPU (" << rhi->description() << "): instances " << instances / gpuTimed / 1000.0
        << " ms, record + submit + wait " << gpuFrame / gpuTimed / 1000.0 << " ms (mean of " << gpuTimed << "); "
        << renderer.stats().batches << " draw calls for " << renderer.stats().instances << " instances\n";
    return 0;
}

} // namespace

int main(int argc, char **argv)
{
    QGuiApplication qapp(argc, argv);
    QCoreApplication::setApplicationName(QStringLiteral("atrament-gallery"));
    QCommandLineParser parser;
    parser.addHelpOption();
    QCommandLineOption screenshotOption(QStringLiteral("screenshot"), QStringLiteral("Render one frame to a PNG"),
            QStringLiteral("file"));
    QCommandLineOption sizeOption(QStringLiteral("size"), QStringLiteral("Window size WxH"), QStringLiteral("size"),
            QStringLiteral("1280x800"));
    QCommandLineOption scaleOption(QStringLiteral("scale"), QStringLiteral("Scale factor"), QStringLiteral("scale"),
            QStringLiteral("1"));
    QCommandLineOption sectionOption(QStringLiteral("section"), QStringLiteral("Section 0-3"), QStringLiteral("n"),
            QStringLiteral("0"));
    QCommandLineOption hoverOption(QStringLiteral("hover"), QStringLiteral("Pointer at x,y"), QStringLiteral("x,y"));
    QCommandLineOption framesOption(QStringLiteral("frames"), QStringLiteral("Frames to render"), QStringLiteral("n"),
            QStringLiteral("3"));
    QCommandLineOption rendererOption(QStringLiteral("renderer"),
            QStringLiteral("gpu or cpu (screenshots: cpu by default; the window: gpu where Qt Quick runs on QRhi)"),
                           QStringLiteral("renderer"));
    QCommandLineOption backendOption(QStringLiteral("backend"),
                                                    QStringLiteral("vulkan or opengl (--screenshot --renderer gpu)"),
                                                                   QStringLiteral("backend"), QStringLiteral("vulkan"));
    QCommandLineOption grabOption(
            QStringLiteral("grab"), QStringLiteral("Show the window, save what it drew after --frames frames, quit"),
                                                   QStringLiteral("file"));
    QCommandLineOption accessibilityOption(QStringLiteral("accessibility"),
            QStringLiteral("Print the frame's accessibility tree (with --screenshot)"));
    parser.addOptions({screenshotOption, sizeOption, scaleOption, sectionOption, hoverOption, framesOption,
                       rendererOption, backendOption, grabOption, accessibilityOption});
    parser.process(qapp);

    const QStringList wh = parser.value(sizeOption).split(QLatin1Char('x'));
    const Size size { wh.value(0).toFloat(), wh.value(1).toFloat() };
    const int section = parser.value(sectionOption).toInt();
    const QString renderer = parser.value(rendererOption);
    const int frames = std::max(1, parser.value(framesOption).toInt());
    if (parser.isSet(screenshotOption)) {
        std::optional<Point> hover;
        if (parser.isSet(hoverOption)) {
            const QStringList xy = parser.value(hoverOption).split(QLatin1Char(','));
            hover = Point { xy.value(0).toFloat(), xy.value(1).toFloat() };
        }
        const OffscreenRhi::Backend backend = parser.value(backendOption) == QLatin1String("opengl")
                                                      ? OffscreenRhi::Backend::OpenGL
                                                      : OffscreenRhi::Backend::Vulkan;
        return screenshot(parser.value(screenshotOption), size, parser.value(scaleOption).toFloat(), section, hover,
                          frames, renderer == QLatin1String("gpu"), backend, parser.isSet(accessibilityOption));
    }
    if (renderer == QLatin1String("cpu"))
        qputenv("BRULION_ATRAMENT_RENDERER", "cpu");

    auto platform = std::make_shared<QtPlatform>();
    App app(platform, gallery::loadFonts(kFontFolders));
    Entity<gallery::Gallery> root = app.newEntity<gallery::Gallery>();
    root.update(app, [section](gallery::Gallery &g, Context<gallery::Gallery> &) { g.section = section; });

    QQuickWindow window;
    window.setTitle(QStringLiteral("Atrament"));
    window.resize(int(size.width), int(size.height));
    window.setColor(QColor(0x15, 0x16, 0x18));
    auto *item = new AtramentItem(window.contentItem());
    item->setSize(window.size());
    QObject::connect(&window, &QQuickWindow::widthChanged, item, [&] { item->setWidth(window.width()); });
    QObject::connect(&window, &QQuickWindow::heightChanged, item, [&] { item->setHeight(window.height()); });
    item->attach(app, AnyView { root.id(), &renderView<gallery::Gallery> });
    item->forceActiveFocus();
    int swapped = 0;
    if (parser.isSet(grabOption)) {
        // Frames are drawn on demand: ask for one after each that was shown, until there are enough.
        QObject::connect(
                &window, &QQuickWindow::frameSwapped, item,
                [&] {
                    if (++swapped < frames) {
                        item->atramentWindow()->refresh();
                        return;
                    }
                    QTimer::singleShot(0, item, [&] {
                        const QImage image = window.grabWindow();
                        const bool saved = image.save(parser.value(grabOption));
                        QTextStream(stdout) << (item->usesGpuRenderer() ? "GPU" : "CPU") << " renderer on "
                                            << window.rendererInterface()->graphicsApi()
                                            << " (QSGRendererInterface::GraphicsApi), " << swapped << " frames, "
                                            << (saved ? "saved " : "cannot write ") << parser.value(grabOption) << '\n';
                        qapp.exit(saved ? 0 : 1);
                    });
                },
                Qt::QueuedConnection);
    }
    window.show();
    const int result = qapp.exec();
    delete item; // before the App
    return result;
}
