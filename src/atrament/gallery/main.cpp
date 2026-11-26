// atrament-gallery (docs/ATRAMENT.md M0): Atrament's elements in a Qt Quick window, or one frame
// rendered by the CPU renderer into a PNG:
//
//   atrament-gallery [--section 0-3]
//   atrament-gallery --screenshot out.png [--size 1280x800] [--scale 2] [--section 1]
//                    [--hover x,y] [--frames n]

#include "gallery.h"
#include "qthost.h"
#include "ui/testplatform.h"

#include <QCommandLineParser>
#include <QGuiApplication>
#include <QImage>
#include <QQuickWindow>
#include <QTextStream>

#include <chrono>

using namespace brulion::atrament;

namespace {

const QStringList kFontFolders { QStringLiteral(":/atrament/fonts/ui"), QStringLiteral(":/atrament/fonts") };

int screenshot(const QString &file, Size size, float scale, int section, std::optional<Point> hover, int frames)
{
    auto platform = std::make_shared<TestPlatform>();
    App app(platform, gallery::loadFonts(kFontFolders));
    TestWindow &tw = platform->newWindow(size, scale);
    Entity<gallery::Gallery> root = app.newEntity<gallery::Gallery>();
    root.update(app, [section](gallery::Gallery &g, Context<gallery::Gallery> &) { g.section = section; });
    Window &w = app.openWindow(tw, root);
    tw.settle();
    if (hover)
        tw.simulateMouseMove(*hover);
    // Several frames: the footer shows the previous frame's statistics, and the caches warm up.
    double build = 0, present = 0;
    for (int i = 0; i < frames; ++i) {
        w.refresh();
        tw.settle();
        build += w.lastFrameStats().buildMicros;
        present += w.lastFrameStats().presentMicros;
    }
    const RasterTarget &img = tw.image();
    QImage out(reinterpret_cast<const uchar *>(img.pixels.data()), img.width, img.height, img.width * 4,
            QImage::Format_RGBA8888_Premultiplied);
    if (!out.convertToFormat(QImage::Format_ARGB32).save(file)) {
        QTextStream(stderr) << "cannot write " << file << '\n';
        return 1;
    }
    QTextStream(stdout) << "frame " << w.frameCount() << ": " << w.lastFrameStats().primitives << " primitives, build "
                        << build / frames / 1000.0 << " ms, raster " << present / frames / 1000.0 << " ms (mean of "
                        << frames << ")\n";
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
    parser.addOptions({ screenshotOption, sizeOption, scaleOption, sectionOption, hoverOption, framesOption });
    parser.process(qapp);

    const QStringList wh = parser.value(sizeOption).split(QLatin1Char('x'));
    const Size size { wh.value(0).toFloat(), wh.value(1).toFloat() };
    const int section = parser.value(sectionOption).toInt();
    if (parser.isSet(screenshotOption)) {
        std::optional<Point> hover;
        if (parser.isSet(hoverOption)) {
            const QStringList xy = parser.value(hoverOption).split(QLatin1Char(','));
            hover = Point { xy.value(0).toFloat(), xy.value(1).toFloat() };
        }
        return screenshot(parser.value(screenshotOption), size, parser.value(scaleOption).toFloat(), section, hover,
                std::max(1, parser.value(framesOption).toInt()));
    }

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
    window.show();
    const int result = qapp.exec();
    delete item; // before the App
    return result;
}
