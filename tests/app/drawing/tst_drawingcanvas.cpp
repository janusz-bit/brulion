// DrawingCanvas (src/app/drawing): input as the platforms deliver it — a stylus's tablet events
// through QWindowSystemInterface (pressure, the eraser end, the barrel button, a popup over the
// canvas; Windows' mouse events made of the pen), a mouse, fingers (drawing, panning, a pinch),
// the wheel —, undo / redo, the SVG it writes (valid for QSvgRenderer, read back equal), loading
// Excalidraw files, merging a version another device wrote and what it paints — light and dark
// (the drawing's theme, the app's) —, a preview no input reaches (`interactive: false`), the
// previews of drawings (DrawingImageProvider, drawingrender.h) and backdrops (a picture or a PDF
// page under the ink: locked, dark, undo, merged, prepared from files — backdropimage.h —, the
// PDF page thumbnails, DrawingBackdrops' jobs). Offscreen.
#include "backdropimage.h"
#include "backdrops.h"
#include "drawingcanvas.h"
#include "drawingimageprovider.h"
#include "drawingrender.h"
#include "pdfpageimageprovider.h"
#include "pdfpages.h"

#include "drawing/backdrop.h"
#include "drawing/drawingfile.h"
#include "drawing/edit.h"
#include "drawing/excalidraw.h"
#include "drawing/excalidrawmd.h"
#include "drawing/geometry.h"
#include "drawing/palette.h"
#include "drawing/stroke.h"
#include "drawing/svgwrite.h"

#include <QBuffer>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QImageWriter>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPageLayout>
#include <QPageSize>
#include <QPainter>
#include <QPdfWriter>
#include <QPointingDevice>
#include <QQmlComponent>
#include <QQmlEngine>
#include <QQuickItem>
#include <QQuickWindow>
#include <QRandomGenerator>
#include <QSignalSpy>
#include <QSvgRenderer>
#include <QTemporaryDir>
#include <QTest>
#include <QThread>
#include <QtMath>
#include <qpa/qwindowsysteminterface.h>

#include <memory>
#include <tuple>

using namespace Qt::StringLiterals;
using brulion::drawingview::DrawingCanvas;

namespace {

QByteArray fixture(const QString &name)
{
    QFile f(QStringLiteral(BRULION_TEST_DATA "/drawing/") + name);
    if (!f.open(QIODevice::ReadOnly))
        qFatal("missing fixture %s", qPrintable(name));
    return f.readAll();
}

const QPointingDevice *tabletDevice(QPointingDevice::PointerType type)
{
    static QHash<int, QPointingDevice *> devices;
    const int id = 7000 + int(type);
    if (QPointingDevice *d = devices.value(id))
        return d;
    auto *d = new QPointingDevice(u"Rysik testowy"_s, id, QInputDevice::DeviceType::Stylus, type,
                                  QInputDevice::Capability::Position | QInputDevice::Capability::Pressure
                                          | QInputDevice::Capability::Hover,
                                  1, 3, QString(), QPointingDeviceUniqueId::fromNumericId(id));
    QWindowSystemInterface::registerInputDevice(d);
    devices.insert(id, d);
    return d;
}

/// A pen on a tablet, delivered as the platforms do (QWindowSystemInterface, synchronous).
class Pen {
public:
    Pen(QWindow *window, QPointingDevice::PointerType type = QPointingDevice::PointerType::Pen)
        : m_window(window), m_device(tabletDevice(type))
    { }
    void send(QPointF pos, Qt::MouseButtons buttons, qreal pressure)
    {
        m_t += 8;
        QWindowSystemInterface::handleTabletEvent(m_window, m_t, m_device, pos, m_window->mapToGlobal(pos), buttons,
                                                  pressure, 0, 0, 0, 0, 0, Qt::NoModifier);
        QWindowSystemInterface::flushWindowSystemEvents(); // (tablet events have no synchronous delivery)
    }
    /// A stroke from `a` to `b` in `steps` moves, pressure from `p0` to `p1`.
    void stroke(QPointF a, QPointF b, int steps, qreal p0, qreal p1, Qt::MouseButtons buttons = Qt::LeftButton)
    {
        send(a, buttons, p0);
        for (int i = 1; i <= steps; ++i)
            send(a + (b - a) * (double(i) / steps), buttons, p0 + (p1 - p0) * i / steps);
        send(b, Qt::NoButton, 0);
    }
    ulong time() const { return m_t; }

private:
    QWindow *m_window;
    const QPointingDevice *m_device;
    ulong m_t = 100000;
};

/// Fingers on a touch screen (QWindowSystemInterface, synchronous).
class Fingers {
public:
    explicit Fingers(QWindow *window) : m_window(window), m_device(QTest::createTouchDevice()) { }
    void send(const QList<std::pair<int, std::pair<QEventPoint::State, QPointF>>> &points)
    {
        m_t += 16;
        QList<QWindowSystemInterface::TouchPoint> tps;
        for (const auto &[id, sp] : points) {
            QWindowSystemInterface::TouchPoint tp;
            tp.id = id;
            tp.state = sp.first;
            const QPointF global = m_window->mapToGlobal(sp.second);
            tp.area = QRectF(global - QPointF(4, 4), QSizeF(8, 8));
            const QSizeF screen = m_window->screen()->geometry().size();
            tp.normalPosition = QPointF(global.x() / screen.width(), global.y() / screen.height());
            tp.pressure = sp.first == QEventPoint::State::Released ? 0 : 1;
            tps.append(tp);
        }
        QWindowSystemInterface::handleTouchEvent<QWindowSystemInterface::SynchronousDelivery>(m_window, m_t, m_device,
                                                                                              tps);
    }
    void drag(QPointF a, QPointF b, int steps)
    {
        send({{1, {QEventPoint::State::Pressed, a}}});
        for (int i = 1; i <= steps; ++i)
            send({{1, {QEventPoint::State::Updated, a + (b - a) * (double(i) / steps)}}});
        send({{1, {QEventPoint::State::Released, b}}});
    }

private:
    QWindow *m_window;
    QPointingDevice *m_device;
    ulong m_t = 500000;
};

void mouseStroke(QWindow *w, QPoint a, QPoint b, int steps, Qt::MouseButton button = Qt::LeftButton)
{
    QTest::mousePress(w, button, {}, a);
    for (int i = 1; i <= steps; ++i)
        QTest::mouseMove(w, a + (b - a) * i / steps);
    QTest::mouseRelease(w, button, {}, b);
}

QImage renderSvg(const QByteArray &svg, QSize size)
{
    QSvgRenderer r(svg);
    QImage image(size, QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::transparent);
    QPainter p(&image);
    r.render(&p);
    return image;
}

int darkPixels(const QImage &image)
{
    int n = 0;
    for (int y = 0; y < image.height(); ++y) {
        for (int x = 0; x < image.width(); ++x) {
            const QColor c = image.pixelColor(x, y);
            if (c.alpha() > 128 && c.lightness() < 100)
                ++n;
        }
    }
    return n;
}

/// A drawing file another device wrote: one horizontal stroke per id ("a" at y 50, "b" at 110,
/// "c" at 170 …, x 100–300) on the paper `background`.
QByteArray strokesFile(const QStringList &ids, const QString &background = u"plain"_s)
{
    brulion::drawing::Drawing d;
    d.background = brulion::drawing::backgroundFromName(background);
    for (const QString &id : ids) {
        brulion::drawing::Stroke s;
        s.id = id;
        const double y = 50 + 60 * double(id.at(0).unicode() - 'a');
        s.points = {{100, y, 0.5}, {300, y, 0.5}};
        d.strokes.append(s);
    }
    return brulion::drawing::toSvg(d);
}

/// The canvas painted into an image of its size.
QImage painted(DrawingCanvas *canvas)
{
    QImage image(canvas->size().toSize(), QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::magenta);
    QPainter p(&image);
    canvas->render(&p, canvas->size());
    return image;
}

/// The colours are within `tolerance` of each other in every channel.
bool near(const QColor &a, const QColor &b, int tolerance = 2)
{
    return std::abs(a.red() - b.red()) <= tolerance && std::abs(a.green() - b.green()) <= tolerance
           && std::abs(a.blue() - b.blue()) <= tolerance;
}

/// The appState of the scene embedded in a drawing file.
QJsonObject appStateOf(const QByteArray &svg)
{
    const std::optional<QByteArray> scene = brulion::drawing::svgPayload(svg);
    return scene ? QJsonDocument::fromJson(*scene).object().value(u"appState"_s).toObject() : QJsonObject();
}

/// The id the image provider gets for `url` (as Qt Quick's pixmap cache computes it).
QString imageId(const QUrl &url)
{
    return url.toString(QUrl::RemoveScheme | QUrl::RemoveAuthority).mid(1);
}

/// A drawing of one thick ink stroke across (y 100, x 60–340) in `theme`, as a file.
QByteArray inkFile(brulion::drawing::PaperTheme theme)
{
    brulion::drawing::Drawing d;
    d.theme = theme;
    brulion::drawing::Stroke s;
    s.id = u"kreska"_s;
    s.width = 4;
    for (int i = 0; i <= 28; ++i)
        s.points.append({60.0 + i * 10, 100, 0.5});
    d.strokes = {s};
    return brulion::drawing::toSvg(d);
}

/// The file with its scene made unreadable (a damaged payload: status "corrupt"); its picture
/// stays as it was.
QByteArray unreadable(QByteArray svg)
{
    const QByteArray start = "<!-- payload-start -->";
    const qsizetype from = svg.indexOf(start) + start.size();
    const qsizetype to = svg.indexOf("<!-- payload-end -->");
    if (from < start.size() || to < from)
        qFatal("no payload");
    svg.replace(from, to - from, "@@@ zepsute ąę");
    return svg;
}

/// An Excalidraw drawing with an embedded web page (its picture an orange photo at 10,10–50,50)
/// and a dark line (y 30, x 60–110) on white paper, 120 × 60: no drawing Brulion edits.
QByteArray pictureFile()
{
    brulion::drawing::Drawing d;
    d.foreign.append(QJsonObject{{u"type"_s, u"embeddable"_s},
                                 {u"id"_s, u"strona łąki"_s},
                                 {u"x"_s, 10},
                                 {u"y"_s, 10},
                                 {u"width"_s, 40},
                                 {u"height"_s, 40},
                                 {u"link"_s, u"https://example.org/łąka"_s}});
    QImage photo(8, 8, QImage::Format_RGB32);
    photo.fill(QColor(u"#ff8800"_s));
    QByteArray png;
    QBuffer buffer(&png);
    buffer.open(QIODevice::WriteOnly);
    photo.save(&buffer, "PNG");
    return "<svg version=\"1.1\" xmlns=\"http://www.w3.org/2000/svg\" xmlns:xlink=\"http://www.w3.org/1999/xlink\" "
           "viewBox=\"0 0 120 60\" width=\"120\" height=\"60\"><!-- svg-source:excalidraw --><metadata><!-- "
           "payload-type:application/vnd.excalidraw+json --><!-- payload-version:2 --><!-- payload-start -->"
           + brulion::drawing::encodePayload(brulion::drawing::sceneJson(d))
           + "<!-- payload-end --></metadata><rect x=\"0\" y=\"0\" width=\"120\" height=\"60\" fill=\"#ffffff\"/>"
             "<image x=\"10\" y=\"10\" width=\"40\" height=\"40\" preserveAspectRatio=\"none\" "
             "xlink:href=\"data:image/png;base64,"
           + png.toBase64()
           + "\"/><path d=\"M60,30 L110,30\" stroke=\"#1e1e1e\" stroke-width=\"8\" fill=\"none\"/></svg>";
}

/// The bounding box of the pixels unlike `paper` (by more than `tolerance` a channel).
QRect inkBox(const QImage &image, const QColor &paper, int tolerance = 40)
{
    QRect box;
    for (int y = 0; y < image.height(); ++y) {
        for (int x = 0; x < image.width(); ++x) {
            if (!near(image.pixelColor(x, y), paper, tolerance))
                box |= QRect(x, y, 1, 1);
        }
    }
    return box;
}

QStringList strokeIds(const DrawingCanvas *canvas)
{
    QStringList ids;
    for (const auto &s : canvas->currentDrawing().strokes)
        ids.append(s.id);
    return ids;
}

/// A drawing file of shapes: a red rectangle (100,100 200×100, width 4) "prostokat", a filled
/// blue ellipse "elipsa" (400,100 100×100), a locked grey square "tlo" far down (40,500 60×60)
/// and a text "napis" "Zażółć" (100,300, size 36).
QByteArray shapesFile()
{
    using namespace brulion::drawing;
    Drawing d;
    Stroke r;
    r.type = ElementType::Rectangle;
    r.id = u"prostokat"_s;
    r.box = QRectF(100, 100, 200, 100);
    r.color = u"#e03131"_s;
    r.width = 4;
    Stroke e = r;
    e.type = ElementType::Ellipse;
    e.id = u"elipsa"_s;
    e.box = QRectF(400, 100, 100, 100);
    e.color = u"#1f2430"_s;
    e.width = 2;
    e.fill = u"#a5d8ff"_s;
    Stroke locked = r;
    locked.id = u"tlo"_s;
    locked.box = QRectF(40, 500, 60, 60);
    locked.color = u"#868e96"_s;
    locked.locked = true;
    Stroke t;
    t.type = ElementType::Text;
    t.id = u"napis"_s;
    t.box = QRectF(100, 300, 140, 41.4);
    t.text = u"Zażółć"_s;
    t.originalText = t.text;
    t.fontSize = 36;
    t.lineHeight = 1.15;
    t.color = u"#1f2430"_s;
    d.strokes = {locked, r, e, t};
    return toSvg(d);
}

const brulion::drawing::Stroke *elementOf(const DrawingCanvas *canvas, const QString &id, brulion::drawing::Drawing &keep)
{
    keep = canvas->currentDrawing();
    for (const auto &s : keep.strokes) {
        if (s.id == id)
            return &s;
    }
    return nullptr;
}

/// The pen along a polyline (a lasso), then lifted.
void penPath(Pen &pen, const QList<QPointF> &points)
{
    pen.send(points.first(), Qt::LeftButton, 0.5);
    for (qsizetype i = 1; i < points.size(); ++i) {
        const QPointF a = points.at(i - 1), b = points.at(i);
        for (int k = 1; k <= 6; ++k)
            pen.send(a + (b - a) * (k / 6.0), Qt::LeftButton, 0.5);
    }
    pen.send(points.last(), Qt::NoButton, 0);
}

int pixelsLike(const QImage &image, const QRect &area, const QColor &color, int tolerance = 40)
{
    int n = 0;
    for (int y = area.top(); y <= area.bottom(); ++y) {
        for (int x = area.left(); x <= area.right(); ++x) {
            if (near(image.pixelColor(x, y), color, tolerance))
                ++n;
        }
    }
    return n;
}

/// A prepared backdrop (DrawingBackdrops' item) of a `size` picture of one colour.
QVariantMap backdropOf(const QColor &color, QSize size = QSize(400, 300), const QString &path = u"Załączniki/slajd.png"_s,
                       int page = 0)
{
    QImage image(size, QImage::Format_RGB32);
    image.fill(color);
    brulion::drawingview::PreparedBackdrop p;
    p.image = brulion::drawingview::encodeBackdrop(image);
    p.source = brulion::drawing::BackdropSource{path, page};
    p.name = u"slajd"_s;
    return brulion::drawingview::toVariant(p);
}

/// A PDF of two A4 pages: text on the first, a red 20 mm square (40–60 mm across, 30–50 mm down)
/// on the second, landscape.
bool writeTwoPagePdf(const QString &path)
{
    QPdfWriter writer(path);
    writer.setResolution(72);
    writer.setPageSize(QPageSize(QPageSize::A4));
    writer.setPageMargins(QMarginsF(0, 0, 0, 0));
    QPainter p;
    if (!p.begin(&writer))
        return false;
    p.drawText(QPointF(60, 120), u"Zażółć gęślą jaźń"_s);
    writer.setPageOrientation(QPageLayout::Landscape);
    if (!writer.newPage())
        return false;
    const double mm = 72 / 25.4;
    p.fillRect(QRectF(40 * mm, 30 * mm, 20 * mm, 20 * mm), Qt::red);
    p.end();
    return QFile::exists(path);
}

} // namespace

class TestDrawingCanvas : public QObject {
    Q_OBJECT

    std::unique_ptr<QQuickWindow> window;
    DrawingCanvas *canvas = nullptr;

private Q_SLOTS:
    void init()
    {
        window = std::make_unique<QQuickWindow>();
        window->resize(800, 600);
        canvas = new DrawingCanvas(window->contentItem());
        canvas->setSize(QSizeF(800, 600));
        canvas->setClock([] { return qint64(1761568000000); });
        window->show();
        QVERIFY(QTest::qWaitForWindowExposed(window.get()));
        QCOMPARE(canvas->load({}), QString());
    }
    void cleanup()
    {
        window.reset();
        canvas = nullptr;
    }

    void mouseDraws()
    {
        QSignalSpy changed(canvas, &DrawingCanvas::contentChanged);
        mouseStroke(window.get(), {100, 100}, {300, 160}, 20);
        QCOMPARE(canvas->strokeCount(), 1);
        QVERIFY(canvas->modified());
        QVERIFY(canvas->canUndo());
        QCOMPARE(changed.size(), 1);
        const brulion::drawing::Drawing d = canvas->currentDrawing();
        const brulion::drawing::Stroke &s = d.strokes.first();
        QVERIFY(s.simulatePressure);
        QCOMPARE(s.tool, brulion::drawing::Tool::Pen);
        QCOMPARE(s.id.size(), 21);
        QCOMPARE(s.updated, qint64(1761568000000));
        // A new drawing has the scene's origin at the top left, zoom 1.
        QCOMPARE(s.points.first().x, 100.0);
        QCOMPARE(s.points.first().y, 100.0);
        QCOMPARE(s.points.last().x, 300.0);
        QVERIFY(!canvas->stylusSeen());
        canvas->undo();
        QCOMPARE(canvas->strokeCount(), 0);
        QVERIFY(canvas->canRedo());
        canvas->redo();
        QCOMPARE(canvas->strokeCount(), 1);
        // A click is a dot.
        QTest::mouseClick(window.get(), Qt::LeftButton, {}, QPoint(400, 400));
        QCOMPARE(canvas->strokeCount(), 2);
        QCOMPARE(canvas->currentDrawing().strokes.last().points.size(), 1);
    }

    void stylusDrawsWithPressure()
    {
        Pen pen(window.get());
        pen.stroke({100, 300}, {500, 320}, 30, 0.1, 0.9);
        QCOMPARE(canvas->strokeCount(), 1);
        QVERIFY(canvas->stylusSeen());
        const brulion::drawing::Stroke s = canvas->currentDrawing().strokes.first();
        QVERIFY(!s.simulatePressure);
        QVERIFY(s.points.first().pressure < 0.2);
        QVERIFY(s.points.last().pressure > 0.8);
        // Pressure widens the line: its end is wider than its start.
        const auto heightNear = [&](double x) {
            double lo = 1e9, hi = -1e9;
            const QList<QPointF> outline = brulion::drawing::strokeOutline(s);
            for (const QPointF &p : outline) {
                if (std::abs(p.x() - x) < 8) {
                    lo = std::min(lo, p.y());
                    hi = std::max(hi, p.y());
                }
            }
            return hi - lo;
        };
        QVERIFY2(heightNear(460) > heightNear(150) * 1.5,
                 qPrintable(u"%1 %2"_s.arg(heightNear(460)).arg(heightNear(150))));
        // No mouse events were made of it: one stroke only, and the window kept no grab.
        QTest::mouseClick(window.get(), Qt::LeftButton, {}, QPoint(700, 500));
        QCOMPARE(canvas->strokeCount(), 2);
    }

    void stylusWithoutPressureChange()
    {
        // A stylus that reports one pressure only draws as a mouse does (speed thins the line).
        Pen pen(window.get());
        pen.stroke({100, 300}, {500, 300}, 20, 1.0, 1.0);
        QCOMPARE(canvas->strokeCount(), 1);
        QVERIFY(canvas->currentDrawing().strokes.first().simulatePressure);
    }

    void eraserEndErases()
    {
        mouseStroke(window.get(), {100, 100}, {300, 100}, 10);
        mouseStroke(window.get(), {100, 300}, {300, 300}, 10);
        QCOMPARE(canvas->strokeCount(), 2);
        canvas->markSaved();
        // The pen turned over crosses the first line only.
        Pen eraser(window.get(), QPointingDevice::PointerType::Eraser);
        eraser.stroke({200, 50}, {200, 150}, 10, 0.5, 0.5);
        QCOMPARE(canvas->strokeCount(), 1);
        QCOMPARE(canvas->currentDrawing().strokes.first().points.first().y, 300.0);
        QVERIFY(canvas->modified());
        canvas->undo();
        QCOMPARE(canvas->strokeCount(), 2);
        // The barrel button erases too, whatever the tool.
        Pen pen(window.get());
        pen.stroke({250, 250}, {250, 350}, 10, 0.5, 0.5, Qt::LeftButton | Qt::RightButton);
        QCOMPARE(canvas->strokeCount(), 1);
    }

    void eraserEndOfTheBrowsersPen()
    {
        // Qt for WebAssembly: one pen device; the eraser end is the web's button 5
        // (`buttons & 32`), which Qt passes as ExtraButton3.
        Pen pen(window.get());
        pen.stroke({100, 200}, {400, 200}, 20, 0.2, 0.8);
        QCOMPARE(canvas->strokeCount(), 1);
        QSignalSpy history(canvas, &DrawingCanvas::historyChanged);
        pen.stroke({250, 120}, {250, 280}, 12, 0.5, 0.5, Qt::ExtraButton3);
        QCOMPARE(canvas->strokeCount(), 0);
        canvas->undo(); // one step
        QCOMPARE(canvas->strokeCount(), 1);
        // On empty paper it draws nothing.
        pen.stroke({100, 450}, {400, 450}, 12, 0.3, 0.9, Qt::ExtraButton3);
        QCOMPARE(canvas->strokeCount(), 1);
        // With the tip's button too (a browser that reports both).
        pen.stroke({300, 120}, {300, 280}, 12, 0.5, 0.5, Qt::ExtraButton3 | Qt::LeftButton);
        QCOMPARE(canvas->strokeCount(), 0);
        QVERIFY(history.size() >= 2);
    }

    void eraserEndOfQtForAndroid()
    {
        // Qt for Android names the pointer type with Qt 5's values: the pen 1, the eraser end 3
        // (Generic | Finger in Qt 6). Its barrel button (BUTTON_STYLUS_PRIMARY, 32) comes as
        // ExtraButton3 (scripts/android-emulator-test.sh --stylus).
        Pen pen(window.get(), QPointingDevice::PointerType(1));
        pen.stroke({100, 200}, {400, 200}, 20, 0.2, 0.8);
        pen.stroke({100, 400}, {400, 400}, 20, 0.2, 0.8);
        QCOMPARE(canvas->strokeCount(), 2);
        QVERIFY(!canvas->currentDrawing().strokes.first().simulatePressure);
        Pen eraser(window.get(), QPointingDevice::PointerType(3));
        eraser.stroke({250, 150}, {250, 250}, 10, 0.5, 0.5);
        QCOMPARE(canvas->strokeCount(), 1);
        QCOMPARE(canvas->currentDrawing().strokes.first().points.first().y, 400.0);
        pen.stroke({250, 350}, {250, 450}, 10, 0.5, 0.5, Qt::ExtraButton3);
        QCOMPARE(canvas->strokeCount(), 0);
    }

    void releaseKeepsThePressure_data()
    {
        QTest::addColumn<qreal>("p0");
        QTest::addColumn<qreal>("p1");
        QTest::addColumn<bool>("simulated");
        QTest::newRow("nacisk 0,2 → 0,9") << 0.2 << 0.9 << false;
        QTest::newRow("stały nacisk 1,0 (adb)") << 1.0 << 1.0 << true;
        QTest::newRow("stały nacisk 0,4") << 0.4 << 0.4 << true;
    }
    void releaseKeepsThePressure()
    {
        // The release reports the pen lifted (pressure 0) — on Android a pixel or two beside the
        // last move (it resamples the moves): not the line's pressure.
        QFETCH(qreal, p0);
        QFETCH(qreal, p1);
        QFETCH(bool, simulated);
        Pen pen(window.get());
        pen.send({100, 300}, Qt::LeftButton, p0);
        for (int i = 1; i <= 20; ++i)
            pen.send({100 + 15.0 * i, 300}, Qt::LeftButton, p0 + (p1 - p0) * i / 20);
        pen.send({402, 301}, Qt::NoButton, 0);
        QCOMPARE(canvas->strokeCount(), 1);
        const brulion::drawing::Stroke s = canvas->currentDrawing().strokes.first();
        QCOMPARE(s.simulatePressure, simulated);
        QCOMPARE(s.points.last().x, 402.0); // the release's place is kept
        if (!simulated) {
            QVERIFY2(s.points.last().pressure >= 0.85, qPrintable(QString::number(s.points.last().pressure)));
            QVERIFY(s.points.first().pressure <= 0.25);
        }
    }

    void eraserToolWithMouse()
    {
        mouseStroke(window.get(), {100, 100}, {300, 100}, 10);
        mouseStroke(window.get(), {100, 200}, {300, 200}, 10);
        canvas->setTool(u"eraser"_s);
        mouseStroke(window.get(), {150, 50}, {150, 250}, 20); // both, one gesture
        QCOMPARE(canvas->strokeCount(), 0);
        canvas->undo(); // one step
        QCOMPARE(canvas->strokeCount(), 2);
        // Erasing nothing adds no step.
        const bool before = canvas->canRedo();
        mouseStroke(window.get(), {600, 500}, {650, 550}, 5);
        QCOMPARE(canvas->strokeCount(), 2);
        QCOMPARE(canvas->canRedo(), before);
    }

    void highlighter()
    {
        canvas->setTool(u"highlighter"_s);
        canvas->setColor(QColor(u"#ffd43b"_s));
        canvas->setPenWidth(5);
        Pen pen(window.get());
        pen.stroke({100, 100}, {400, 100}, 20, 0.2, 1.0);
        QCOMPARE(canvas->strokeCount(), 1);
        const brulion::drawing::Stroke s = canvas->currentDrawing().strokes.first();
        QCOMPARE(s.tool, brulion::drawing::Tool::Highlighter);
        QCOMPARE(s.opacity, DrawingCanvas::HighlighterOpacity);
        QVERIFY(!s.simulatePressure);
        for (const auto &p : s.points)
            QCOMPARE(p.pressure, 0.5); // an even width
        QCOMPARE(s.color, u"#ffd43b"_s);
        QCOMPARE(s.width, 5.0);
    }

    void popupOverTheCanvasGetsThePen()
    {
        // An item that takes input lies over the canvas (a menu, a toast): the pen is its.
        QQuickItem cover(window->contentItem());
        cover.setPosition(QPointF(0, 0));
        cover.setSize(QSizeF(800, 200));
        cover.setZ(10);
        cover.setAcceptedMouseButtons(Qt::LeftButton);
        Pen pen(window.get());
        pen.stroke({100, 100}, {300, 120}, 10, 0.3, 0.6);
        QCOMPARE(canvas->strokeCount(), 0);
        pen.stroke({100, 400}, {300, 420}, 10, 0.3, 0.6);
        QCOMPARE(canvas->strokeCount(), 1);
    }

    void popupsOfQtQuickControls()
    {
        // Qt Quick Controls put popups into the window's overlay, which is visible over the whole
        // window whenever a Drawer exists (the shell's, on phones) and takes every button: the pen
        // still draws beside a closed drawer and a non-modal popup, never under a popup, nor while
        // a modal one is open.
        QQmlEngine engine;
        QQmlComponent component(&engine);
        component.setData("import QtQuick\nimport QtQuick.Templates as T\nItem {\n"
                          "  property alias drawer: drawer; property alias menu: menu; property alias dialog: dialog\n"
                          "  T.Drawer { id: drawer; width: 120; height: 600; edge: Qt.LeftEdge }\n"
                          "  T.Popup { id: menu; x: 0; y: 0; width: 300; height: 150; modal: false\n"
                          "    background: Rectangle { color: \"#eee\" } }\n"
                          "  T.Popup { id: dialog; x: 500; y: 400; width: 100; height: 100; modal: true\n"
                          "    background: Rectangle { color: \"#ddd\" } }\n"
                          "}",
                          QUrl());
        std::unique_ptr<QObject> root(component.create());
        QVERIFY2(root, qPrintable(component.errorString()));
        auto *item = qobject_cast<QQuickItem *>(root.get());
        item->setParentItem(window->contentItem());
        item->setSize(QSizeF(800, 600));
        QCoreApplication::processEvents();
        Pen pen(window.get());
        // A closed drawer: the overlay is visible, the pen draws.
        pen.stroke({200, 300}, {400, 320}, 10, 0.3, 0.6);
        QCOMPARE(canvas->strokeCount(), 1);
        // A non-modal popup: the pen is its over it, draws beside it.
        QMetaObject::invokeMethod(root->property("menu").value<QObject *>(), "open");
        QTRY_VERIFY(root->property("menu").value<QObject *>()->property("opened").toBool());
        pen.stroke({100, 100}, {200, 120}, 10, 0.3, 0.6);
        QCOMPARE(canvas->strokeCount(), 1);
        pen.stroke({200, 400}, {400, 420}, 10, 0.3, 0.6);
        QCOMPARE(canvas->strokeCount(), 2);
        QMetaObject::invokeMethod(root->property("menu").value<QObject *>(), "close");
        QTRY_VERIFY(!root->property("menu").value<QObject *>()->property("visible").toBool());
        // A modal popup: nowhere.
        QMetaObject::invokeMethod(root->property("dialog").value<QObject *>(), "open");
        QTRY_VERIFY(root->property("dialog").value<QObject *>()->property("opened").toBool());
        pen.stroke({200, 200}, {400, 220}, 10, 0.3, 0.6);
        QCOMPARE(canvas->strokeCount(), 2);
        QMetaObject::invokeMethod(root->property("dialog").value<QObject *>(), "close");
        QTRY_VERIFY(!root->property("dialog").value<QObject *>()->property("visible").toBool());
        pen.stroke({200, 200}, {400, 220}, 10, 0.3, 0.6);
        QCOMPARE(canvas->strokeCount(), 3);
    }

    void itemsInTheOverlay()
    {
        // The shell puts its toasts' host into the overlay, filling the window (Main.qml
        // BToastHost), and the overlay is visible whenever a Drawer exists (the shell's): only a
        // toast takes the pen, where it is — the host itself took it everywhere (found by
        // tests/web/stylus.spec.mjs in the real app: the pen drew without pressure there).
        QQmlEngine engine;
        QQmlComponent component(&engine);
        component.setData("import QtQuick\nimport QtQuick.Templates as T\nItem {\n"
                          "  property alias toast: toast; property alias host: host\n"
                          "  T.Drawer { width: 120; height: 600; edge: Qt.LeftEdge }\n"
                          "  Item { id: host; z: 1000\n"
                          "    MouseArea { id: toast; x: 500; y: 450; width: 250; height: 100; visible: false }\n"
                          "  }\n"
                          "}",
                          QUrl());
        std::unique_ptr<QObject> root(component.create());
        QVERIFY2(root, qPrintable(component.errorString()));
        auto *item = qobject_cast<QQuickItem *>(root.get());
        item->setParentItem(window->contentItem());
        item->setSize(QSizeF(800, 600));
        QCoreApplication::processEvents();
        // Into the overlay (the drawer made it), as Main.qml's `parent: T.Overlay.overlay` does.
        QQuickItem *overlay = nullptr;
        for (QQuickItem *child : window->contentItem()->childItems()) {
            if (child->inherits("QQuickOverlay"))
                overlay = child;
        }
        QVERIFY(overlay);
        auto *host = root->property("host").value<QQuickItem *>();
        host->setParentItem(overlay);
        host->setSize(overlay->size());
        QCoreApplication::processEvents();
        QVERIFY(overlay->isVisible());
        Pen pen(window.get());
        pen.stroke({100, 300}, {600, 320}, 20, 0.2, 0.9);
        QCOMPARE(canvas->strokeCount(), 1);
        QVERIFY(!canvas->currentDrawing().strokes.first().simulatePressure); // the tablet's, no mouse
        // A toast shown: the pen over it is the toast's, beside it the canvas's.
        auto *toast = root->property("toast").value<QQuickItem *>();
        toast->setVisible(true);
        pen.stroke({550, 480}, {700, 520}, 10, 0.3, 0.6);
        QCOMPARE(canvas->strokeCount(), 1);
        pen.stroke({100, 100}, {300, 120}, 10, 0.3, 0.6);
        QCOMPARE(canvas->strokeCount(), 2);
    }

    void windowsMouseOfThePenIsIgnored()
    {
        // Windows makes mouse messages of the pen itself (no pressure): the tablet events drew it.
        Pen pen(window.get());
        pen.send({100, 100}, Qt::LeftButton, 0.4);
        QWindowSystemInterface::handleMouseEvent<QWindowSystemInterface::SynchronousDelivery>(
                window.get(), pen.time(), QPointF(100, 100), window->mapToGlobal(QPointF(100, 100)), Qt::LeftButton,
                Qt::LeftButton, QEvent::MouseButtonPress);
        for (int i = 1; i <= 10; ++i) {
            const QPointF p(100 + 20 * i, 100);
            pen.send(p, Qt::LeftButton, 0.4 + i * 0.04);
            QWindowSystemInterface::handleMouseEvent<QWindowSystemInterface::SynchronousDelivery>(
                    window.get(), pen.time(), p, window->mapToGlobal(p), Qt::LeftButton, Qt::NoButton,
                    QEvent::MouseMove);
        }
        pen.send({300, 100}, Qt::NoButton, 0);
        QWindowSystemInterface::handleMouseEvent<QWindowSystemInterface::SynchronousDelivery>(
                window.get(), pen.time(), QPointF(300, 100), window->mapToGlobal(QPointF(300, 100)), Qt::NoButton,
                Qt::LeftButton, QEvent::MouseButtonRelease);
        QCOMPARE(canvas->strokeCount(), 1);
        QVERIFY(!canvas->currentDrawing().strokes.first().simulatePressure);
        // The mouse itself still draws a moment later elsewhere.
        QTest::qWait(1);
        QWindowSystemInterface::handleMouseEvent<QWindowSystemInterface::SynchronousDelivery>(
                window.get(), pen.time() + 2000, QPointF(600, 500), window->mapToGlobal(QPointF(600, 500)),
                Qt::LeftButton, Qt::LeftButton, QEvent::MouseButtonPress);
        QWindowSystemInterface::handleMouseEvent<QWindowSystemInterface::SynchronousDelivery>(
                window.get(), pen.time() + 2010, QPointF(650, 520), window->mapToGlobal(QPointF(650, 520)),
                Qt::NoButton, Qt::LeftButton, QEvent::MouseButtonRelease);
        QCOMPARE(canvas->strokeCount(), 2);
    }

    void fingerDrawsOrPans_data()
    {
        QTest::addColumn<bool>("fingerDraws");
        QTest::newRow("finger draws") << true;
        QTest::newRow("finger moves the paper") << false;
    }
    void fingerDrawsOrPans()
    {
        QFETCH(bool, fingerDraws);
        canvas->setFingerDraws(fingerDraws);
        const QPointF centre(canvas->centerX(), canvas->centerY());
        Fingers fingers(window.get());
        fingers.drag({200, 200}, {300, 260}, 12);
        QCOMPARE(canvas->strokeCount(), fingerDraws ? 1 : 0);
        if (fingerDraws) {
            QCOMPARE(QPointF(canvas->centerX(), canvas->centerY()), centre);
            QVERIFY(canvas->currentDrawing().strokes.first().simulatePressure);
        } else {
            QCOMPARE(QPointF(canvas->centerX(), canvas->centerY()), centre - QPointF(100, 60));
        }
    }

    void pinchZooms()
    {
        canvas->setFingerDraws(true);
        Fingers f(window.get());
        using S = QEventPoint::State;
        f.send({{1, {S::Pressed, {300, 300}}}});
        f.send({{1, {S::Updated, {302, 301}}}, {2, {S::Pressed, {500, 300}}}}); // second finger at once
        for (int i = 1; i <= 10; ++i)
            f.send({{1, {S::Updated, {302 - 10.0 * i, 301}}}, {2, {S::Updated, {500 + 10.0 * i, 300}}}});
        f.send({{1, {S::Released, {202, 301}}}, {2, {S::Released, {600, 300}}}});
        QCOMPARE(canvas->strokeCount(), 0); // the stroke begun became the pinch
        QVERIFY2(canvas->zoom() > 1.8 && canvas->zoom() < 2.2, qPrintable(QString::number(canvas->zoom())));
        // The scene point between the fingers stayed between them.
        const QPointF scene = canvas->toScene(QPointF(401, 300.5));
        QVERIFY(std::abs(scene.x() - 401) < 3);
    }

    void mouseButtonsAndWheelMoveThePaper()
    {
        const QPointF c0(canvas->centerX(), canvas->centerY());
        mouseStroke(window.get(), {300, 300}, {350, 320}, 5, Qt::MiddleButton);
        QCOMPARE(QPointF(canvas->centerX(), canvas->centerY()), c0 - QPointF(50, 20));
        mouseStroke(window.get(), {300, 300}, {250, 300}, 5, Qt::RightButton);
        QCOMPARE(QPointF(canvas->centerX(), canvas->centerY()), c0 - QPointF(0, 20));
        canvas->setPanMode(true); // Space held
        mouseStroke(window.get(), {300, 300}, {300, 400}, 5);
        canvas->setPanMode(false);
        QCOMPARE(canvas->strokeCount(), 0);
        QCOMPARE(QPointF(canvas->centerX(), canvas->centerY()), c0 - QPointF(0, 120));
        // The wheel scrolls; Ctrl+wheel zooms at the pointer.
        QWheelEvent down(QPointF(400, 300), window->mapToGlobal(QPointF(400, 300)), QPoint(), QPoint(0, -120),
                         Qt::NoButton, Qt::NoModifier, Qt::NoScrollPhase, false);
        QCoreApplication::sendEvent(window.get(), &down);
        QCOMPARE(canvas->centerY(), c0.y() - 120 + 48);
        const QPointF under = canvas->toScene(QPointF(200, 100));
        QWheelEvent zoomIn(QPointF(200, 100), window->mapToGlobal(QPointF(200, 100)), QPoint(), QPoint(0, 240),
                           Qt::NoButton, Qt::ControlModifier, Qt::NoScrollPhase, false);
        QCoreApplication::sendEvent(window.get(), &zoomIn);
        QVERIFY(canvas->zoom() > 1.3);
        const QPointF after = canvas->toScene(QPointF(200, 100));
        QVERIFY(std::abs(after.x() - under.x()) < 0.01 && std::abs(after.y() - under.y()) < 0.01);
        // Zoom stays within its limits.
        canvas->zoomBy(1000);
        QCOMPARE(canvas->zoom(), DrawingCanvas::MaxZoom);
        canvas->zoomBy(0.000001);
        QCOMPARE(canvas->zoom(), DrawingCanvas::MinZoom);
    }

    void backgroundIsAnUndoStep()
    {
        QSignalSpy spy(canvas, &DrawingCanvas::backgroundChanged);
        canvas->setBackground(u"lines"_s);
        QCOMPARE(canvas->background(), u"lines"_s);
        QVERIFY(canvas->modified());
        canvas->setBackground(u"grid"_s);
        canvas->undo();
        QCOMPARE(canvas->background(), u"lines"_s);
        canvas->undo();
        QCOMPARE(canvas->background(), u"plain"_s);
        canvas->setBackground(u"nonsense"_s); // = plain: nothing changes
        QVERIFY(canvas->canRedo());
        QCOMPARE(spy.size(), 4);
    }

    void svgIsAPictureAndADrawing()
    {
        canvas->setBackground(u"grid"_s);
        mouseStroke(window.get(), {100, 100}, {300, 180}, 20);
        Pen pen(window.get());
        pen.stroke({120, 260}, {420, 230}, 25, 0.2, 0.8);
        canvas->setTool(u"highlighter"_s);
        canvas->setColor(QColor(u"#ffd43b"_s));
        canvas->setPenWidth(5);
        mouseStroke(window.get(), {100, 140}, {320, 140}, 10);
        const QByteArray svg = canvas->svg();
        QVERIFY(svg.startsWith("<svg "));
        // Qt's renderer (the editor's embeds, the picture view) reads it, with the drawing's size.
        QSvgRenderer renderer(svg);
        QVERIFY(renderer.isValid());
        const QSize size = renderer.defaultSize();
        const QRectF bounds = canvas->contentBounds();
        QVERIFY(std::abs(size.width() - (bounds.width() + 32)) <= 2);
        QVERIFY(std::abs(size.height() - (bounds.height() + 32)) <= 2);
        // The ink is there: dark pixels where the pen went, the paper elsewhere.
        const QImage image = renderSvg(svg, size);
        QVERIFY(darkPixels(image) > 500);
        const QPointF inkScene(200, 140); // on the first line (100,100 → 300,180)
        const QPoint ink = (inkScene - bounds.topLeft() + QPointF(16, 16)).toPoint();
        QVERIFY2(image.pixelColor(ink).lightness() < 140, qPrintable(image.pixelColor(ink).name()));
        QCOMPARE(image.pixelColor(2, 2).alpha(), 255); // the paper (Qt draws the light picture)
        // Loaded again: the same drawing and the same file.
        DrawingCanvas other;
        QCOMPARE(other.load(svg), QString());
        QCOMPARE(other.strokeCount(), 3);
        QCOMPARE(other.background(), u"grid"_s);
        QCOMPARE(other.svg(), svg);
        QVERIFY(!other.modified());
    }

    void loadStatuses_data()
    {
        QTest::addColumn<QByteArray>("bytes");
        QTest::addColumn<QString>("status");
        QTest::addColumn<int>("strokes");
        QTest::newRow("new") << QByteArray() << QString() << 0;
        QTest::newRow("excalidraw freehand") << fixture(u"excalidraw_freedraw.svg"_s) << QString() << 2;
        // Shapes, text and pictures are edited too; a frame (or an embedded page) is not.
        QTest::newRow("excalidraw with shapes") << fixture(u"excalidraw_mixed.svg"_s) << QString() << 3;
        QTest::newRow(".excalidraw") << fixture(u"excalidraw_mixed.excalidraw"_s) << QString() << 3;
        QTest::newRow("every kind of shape") << fixture(u"excalidraw_shapes.svg"_s) << QString() << 16;
        QTest::newRow("a frame") << fixture(u"excalidraw_frame.svg"_s) << u"readonly"_s << 0;
        QTest::newRow("plain svg") << QByteArray("<svg xmlns=\"http://www.w3.org/2000/svg\"/>") << u"notDrawing"_s << 0;
        QTest::newRow("photo") << QByteArray("\xff\xd8\xff\xe0 zdjęcie") << u"notDrawing"_s << 0;
        QTest::newRow("broken payload") << QByteArray("<svg><!-- payload-type:application/vnd.excalidraw+json --><!-- "
                                                      "payload-start -->@@@<!-- payload-end --></svg>")
                                        << u"corrupt"_s << 0;
    }
    void loadStatuses()
    {
        QFETCH(QByteArray, bytes);
        QFETCH(QString, status);
        QFETCH(int, strokes);
        mouseStroke(window.get(), {10, 10}, {50, 50}, 3); // something to replace
        QSignalSpy loaded(canvas, &DrawingCanvas::loadedChanged);
        QCOMPARE(canvas->load(bytes), status);
        QCOMPARE(loaded.size(), 1);
        QCOMPARE(canvas->editable(), status.isEmpty());
        QCOMPARE(canvas->strokeCount(), strokes);
        QVERIFY(!canvas->modified());
        QVERIFY(!canvas->canUndo());
        if (!status.isEmpty()) {
            QVERIFY(canvas->svg().isEmpty()); // never written over
            mouseStroke(window.get(), {100, 100}, {200, 200}, 5);
            QCOMPARE(canvas->strokeCount(), 0);
        }
    }

    void loadPlacesTheView()
    {
        // Strokes far from the origin: shown from their top left (zoom 1) when they fit …
        QCOMPARE(canvas->load(fixture(u"excalidraw_freedraw.svg"_s)), QString());
        QCOMPARE(canvas->zoom(), 1.0);
        const QRectF b = canvas->contentBounds();
        const QPointF topLeft = canvas->toItem(b.topLeft());
        QVERIFY(std::abs(topLeft.x() - 24) < 0.5 && std::abs(topLeft.y() - 24) < 0.5);
        // … zoomed out to the view's width when wider (a phone), still from the top left …
        canvas->setSize(QSizeF(120, 90));
        QCOMPARE(canvas->load(fixture(u"excalidraw_freedraw.svg"_s)), QString());
        QVERIFY(canvas->zoom() < 1);
        QPointF a = canvas->toItem(b.topLeft());
        QPointF z = canvas->toItem(b.bottomRight());
        QVERIFY(std::abs(a.x() - 24) < 0.5 && std::abs(a.y() - 24) < 0.5);
        QVERIFY(std::abs(z.x() - 96) < 0.5);
        // … and "the whole drawing" fits both ways, centred across, from the top.
        canvas->fitAll();
        a = canvas->toItem(b.topLeft());
        z = canvas->toItem(b.bottomRight());
        QVERIFY(a.x() >= 23.5 && z.x() <= 96.5 && std::abs(a.y() - 24) < 0.5 && z.y() <= 66.5);
        QVERIFY(std::abs((a.x() + z.x()) / 2 - 60) < 0.5);
    }

    void previewIsNotInteractive()
    {
        // The sync center's previews of a conflict: a mouse, a pen (its tip and its eraser end),
        // fingers, the wheel and Ctrl+wheel change nothing — the drawing, the view, the history.
        QCOMPARE(canvas->load(strokesFile({u"a"_s, u"b"_s, u"c"_s})), QString());
        QSignalSpy interactive(canvas, &DrawingCanvas::interactiveChanged);
        canvas->setInteractive(false);
        QCOMPARE(interactive.size(), 1);
        QVERIFY(!canvas->interactive());
        QCOMPARE(canvas->acceptedMouseButtons(), Qt::MouseButtons(Qt::NoButton));
        QVERIFY(!canvas->acceptTouchEvents());
        // The whole drawing in view, whatever the size (no fitAll() needed).
        canvas->setSize(QSizeF(300, 160));
        const auto inView = [this] {
            const QRectF b = canvas->contentBounds();
            const QPointF a = canvas->toItem(b.topLeft());
            const QPointF z = canvas->toItem(b.bottomRight());
            return a.x() >= -0.5 && a.y() >= -0.5 && z.x() <= canvas->width() + 0.5 && z.y() <= canvas->height() + 0.5;
        };
        QVERIFY(inView());
        QVERIFY(canvas->zoom() < 1);
        // Centred both ways: no empty paper left below, where nothing is written in a preview.
        const auto centred = [this] {
            const QPointF c = canvas->toItem(canvas->contentBounds().center());
            return qAbs(c.x() - canvas->width() / 2) < 0.5 && qAbs(c.y() - canvas->height() / 2) < 0.5;
        };
        QVERIFY(centred());
        canvas->setSize(QSizeF(120, 300));
        QVERIFY(inView());
        QVERIFY(centred());
        canvas->setSize(QSizeF(300, 160));
        QCOMPARE(canvas->load(strokesFile({u"a"_s, u"b"_s, u"c"_s})), QString());
        QVERIFY(inView());
        const QPointF center(canvas->centerX(), canvas->centerY());
        const qreal zoom = canvas->zoom();
        mouseStroke(window.get(), {20, 20}, {280, 140}, 10);
        mouseStroke(window.get(), {100, 80}, {200, 100}, 5, Qt::MiddleButton);
        Pen pen(window.get());
        pen.stroke({30, 40}, {260, 120}, 10, 0.3, 0.9);
        Pen eraser(window.get(), QPointingDevice::PointerType::Eraser);
        eraser.stroke({150, 10}, {150, 150}, 10, 0.5, 0.5);
        Fingers fingers(window.get());
        fingers.drag({60, 60}, {200, 120}, 6);
        for (const Qt::KeyboardModifiers modifiers : {Qt::KeyboardModifiers(), Qt::KeyboardModifiers(Qt::ControlModifier)}) {
            QWheelEvent wheel(QPointF(150, 80), window->mapToGlobal(QPointF(150, 80)), QPoint(), QPoint(0, -240),
                              Qt::NoButton, modifiers, Qt::NoScrollPhase, false);
            QCoreApplication::sendEvent(window.get(), &wheel);
        }
        QCOMPARE(canvas->strokeCount(), 3);
        QVERIFY(!canvas->modified());
        QVERIFY(!canvas->canUndo());
        QVERIFY(!canvas->busy());
        QVERIFY(!canvas->stylusSeen());
        QCOMPARE(QPointF(canvas->centerX(), canvas->centerY()), center);
        QCOMPARE(canvas->zoom(), zoom);
        QVERIFY(inView());
        // Interactive again: the mouse draws.
        canvas->setInteractive(true);
        QCOMPARE(interactive.size(), 2);
        QCOMPARE(canvas->acceptedMouseButtons(), Qt::LeftButton | Qt::RightButton | Qt::MiddleButton);
        mouseStroke(window.get(), {20, 150}, {280, 150}, 10);
        QCOMPARE(canvas->strokeCount(), 4);
    }

    void externalChangesMerge()
    {
        // Another device drew stroke "c" and erased nothing; here "b" was drawn and "a" erased.
        DrawingCanvas source;
        source.setSize(QSizeF(800, 600));
        QCOMPARE(source.load({}), QString());
        auto file = [](const QStringList &ids) {
            brulion::drawing::Drawing d;
            for (const QString &id : ids) {
                brulion::drawing::Stroke s;
                s.id = id;
                const double y = 50 + 60 * double(id.at(0).unicode() - 'a');
                s.points = {{100, y, 0.5}, {300, y, 0.5}};
                d.strokes.append(s);
            }
            return brulion::drawing::toSvg(d);
        };
        QCOMPARE(canvas->load(file({u"a"_s})), QString());
        // Not modified here: the other version replaces this one.
        QVERIFY(canvas->takeExternal(file({u"a"_s, u"c"_s})));
        QCOMPARE(canvas->strokeCount(), 2);
        QVERIFY(!canvas->modified());
        // Modified here: merged.
        canvas->setTool(u"eraser"_s);
        mouseStroke(window.get(), {200, 30}, {200, 70}, 4); // erases "a" (y 50)
        canvas->setTool(u"pen"_s);
        mouseStroke(window.get(), {100, 500}, {300, 500}, 6); // a new stroke
        QCOMPARE(canvas->strokeCount(), 2);
        QVERIFY(canvas->modified());
        QVERIFY(canvas->takeExternal(file({u"a"_s, u"c"_s, u"d"_s})));
        QStringList ids;
        for (const auto &s : canvas->currentDrawing().strokes)
            ids.append(s.id);
        QCOMPARE(ids.size(), 3);
        QCOMPARE(ids.mid(0, 2), (QStringList{u"c"_s, u"d"_s}));
        QCOMPARE(ids.at(2).size(), 21); // the stroke drawn here, after theirs
        QVERIFY(canvas->modified());    // still to be written
        QVERIFY(!canvas->canUndo());
        // Not a drawing (a half-written file): nothing changes.
        QVERIFY(!canvas->takeExternal("<svg"));
        QCOMPARE(canvas->strokeCount(), 3);
    }

    void paintsInkAndLines()
    {
        canvas->setBackground(u"lines"_s);
        mouseStroke(window.get(), {100, 100}, {300, 100}, 10);
        QImage image(800, 600, QImage::Format_ARGB32_Premultiplied);
        image.fill(Qt::black);
        {
            QPainter p(&image);
            canvas->render(&p, QSizeF(800, 600));
        }
        QVERIFY(image.pixelColor(200, 100).lightness() < 100);                              // ink
        QCOMPARE(image.pixelColor(400, 50).name(), u"#ffffff"_s);                           // paper
        QCOMPARE(image.pixelColor(400, 64).name(), brulion::drawing::RuleColor.toString()); // a line (32 × 2)
        // The stroke being drawn shows before it ends.
        QTest::mousePress(window.get(), Qt::LeftButton, {}, QPoint(100, 400));
        for (int i = 1; i <= 10; ++i)
            QTest::mouseMove(window.get(), QPoint(100 + 20 * i, 400));
        QVERIFY(canvas->busy());
        QImage live(800, 600, QImage::Format_ARGB32_Premultiplied);
        {
            QPainter p(&live);
            canvas->render(&p, QSizeF(800, 600));
        }
        QVERIFY(live.pixelColor(200, 400).lightness() < 120);
        QTest::mouseRelease(window.get(), Qt::LeftButton, {}, QPoint(300, 400));
        QCOMPARE(canvas->strokeCount(), 2);
    }

    void manyStrokesRenderQuickly()
    {
        // A page of handwriting: 600 strokes of 60 points.
        brulion::drawing::Drawing d;
        for (int i = 0; i < 600; ++i) {
            brulion::drawing::Stroke s;
            s.id = u"s%1"_s.arg(i);
            for (int k = 0; k < 60; ++k)
                s.points.append({20 + (i % 20) * 38.0 + k * 0.6, 20 + (i / 20) * 30.0 + 8 * std::sin(k / 6.0), 0.5});
            d.strokes.append(s);
        }
        QElapsedTimer t;
        t.start();
        QCOMPARE(canvas->load(brulion::drawing::toSvg(d)), QString());
        const qint64 loadMs = t.elapsed();
        QImage image(800, 600, QImage::Format_ARGB32_Premultiplied);
        QPainter p(&image);
        canvas->render(&p, QSizeF(800, 600));
        qInfo("600 strokes: load %lld ms, render %.1f ms", loadMs, canvas->lastRenderMs());
        QVERIFY(canvas->lastRenderMs() < 1000.0 * BRULION_TEST_SLOWDOWN);
    }

    void handwritingSample()
    {
        // A page of lecture notes as a stylus writes them: script-like words, an underline, a
        // circle, an arrow, a highlighter swipe, a small chart, on lined paper. Checked as a
        // picture; `BRULION_DRAWING_SAMPLE=<file>` writes it there (tests/app/drawing/screenshots.sh).
        using brulion::drawing::Stroke;
        using brulion::drawing::StrokePoint;
        brulion::drawing::Drawing d;
        d.background = brulion::drawing::Background::Lines;
        int n = 0;
        const auto add = [&](QList<StrokePoint> points, const QString &color, double width,
                             brulion::drawing::Tool tool = brulion::drawing::Tool::Pen) {
            Stroke s;
            s.id = u"probka-%1"_s.arg(++n);
            s.points = std::move(points);
            s.color = color;
            s.width = width;
            s.tool = tool;
            s.opacity = tool == brulion::drawing::Tool::Highlighter ? DrawingCanvas::HighlighterOpacity : 100;
            s.simulatePressure = false;
            brulion::drawing::normalizeStroke(s);
            d.strokes.append(s);
        };
        // Script-like words: loops along a line, the pressure rising and falling.
        const auto word = [&](double x0, double y0, int loops, double height, const QString &color, double width) {
            QList<StrokePoint> pts;
            const int steps = loops * 16;
            for (int i = 0; i <= steps; ++i) {
                const double t = double(i) / 16 * 2 * M_PI;
                // A prolate cycloid: loops like joined-up writing.
                const double x = x0 + t * 3.4 - 6 * std::sin(t);
                const double y = y0 - height * (0.5 - 0.5 * std::cos(t)) * (0.75 + 0.25 * std::sin(t * 0.37));
                pts.append({x, y, 0.35 + 0.35 * std::sin(i * 0.21) * std::sin(i * 0.05 + 1)});
            }
            add(pts, color, width);
        };
        word(40, 70, 7, 26, u"#1f2430"_s, 1.0); // a title
        word(240, 70, 4, 26, u"#1f2430"_s, 1.0);
        for (int line = 0; line < 4; ++line) {
            double x = 40;
            for (int w = 0; w < 4; ++w) {
                const int loops = 2 + (line * 3 + w) % 4;
                word(x, 134 + line * 32, loops, 14, u"#1f2430"_s, 0.5);
                x += loops * 21.4 + 20;
            }
        }
        // An underline (blue), a circle (red), an arrow.
        QList<StrokePoint> under;
        for (int i = 0; i <= 60; ++i)
            under.append({40 + i * 5.2, 82 + 2 * std::sin(i * 0.3), 0.4 + 0.3 * i / 60.0});
        add(under, u"#2f6fdd"_s, 1.0);
        QList<StrokePoint> circle;
        for (int i = 0; i <= 64; ++i) {
            const double a = i / 60.0 * 2 * M_PI - 0.4;
            circle.append({330 + 58 * std::cos(a), 165 + 26 * std::sin(a), 0.5 + 0.3 * std::sin(a * 2)});
        }
        add(circle, u"#e03131"_s, 1.0);
        QList<StrokePoint> shaft;
        for (int i = 0; i <= 30; ++i)
            shaft.append({392 + i * 3.0, 160 - i * 1.6 - 0.02 * i * i, 0.5});
        add(shaft, u"#e03131"_s, 1.0);
        add({{470, 106, 0.6}, {482, 107, 0.6}, {490, 110, 0.5}, {484, 120, 0.5}, {479, 130, 0.4}}, u"#e03131"_s, 1.0);
        // A highlighter over the second line.
        QList<StrokePoint> swipe;
        for (int i = 0; i <= 40; ++i)
            swipe.append({36 + i * 6.0, 160 + 0.8 * std::sin(i * 0.4), 0.5});
        add(swipe, u"#ffd43b"_s, 3, brulion::drawing::Tool::Highlighter);
        // A small chart: axes and a curve (green).
        QList<StrokePoint> axisY, axisX;
        for (int i = 0; i <= 34; ++i)
            axisY.append({560 + 0.6 * std::sin(i * 0.5), 60 + i * 5.0, 0.45 + 0.004 * i});
        for (int i = 0; i <= 40; ++i)
            axisX.append({560 + i * 5.0, 230 + 0.6 * std::sin(i * 0.4), 0.55});
        add(axisY, u"#1f2430"_s, 0.5);
        add(axisX, u"#1f2430"_s, 0.5);
        QList<StrokePoint> curve;
        for (int i = 0; i <= 50; ++i) {
            const double x = i / 50.0;
            curve.append({565 + x * 185, 225 - 160 * x * x + 20 * std::sin(x * 9) * x, 0.3 + 0.5 * x});
        }
        add(curve, u"#2f9e44"_s, 1.0);
        const QByteArray svg = brulion::drawing::toSvg(d);
        QSvgRenderer renderer(svg);
        QVERIFY(renderer.isValid());
        QVERIFY(darkPixels(renderSvg(svg, renderer.defaultSize())) > 2000);
        QCOMPARE(canvas->load(svg), QString());
        QCOMPARE(canvas->strokeCount(), int(d.strokes.size()));
        if (const QByteArray out = qgetenv("BRULION_DRAWING_SAMPLE"); !out.isEmpty()) {
            QFile f(QString::fromLocal8Bit(out));
            QVERIFY(f.open(QIODevice::WriteOnly));
            f.write(svg);
        }
    }

    // Palm rejection: a hand on the screen while the pen is down does nothing — it neither draws,
    // nor moves the paper, nor ends, cuts or erases the pen's stroke —, also after the pen lifts
    // until the hand lifts; then fingers work again.
    void palmIsIgnoredWhileThePenIsDown_data()
    {
        QTest::addColumn<QString>("palm");
        QTest::addColumn<bool>("fingerDraws");
        QTest::newRow("a tap, fingers move the paper") << u"tap"_s << false;
        QTest::newRow("a tap, fingers draw") << u"tap"_s << true;
        QTest::newRow("two contacts, fingers move the paper") << u"two"_s << false;
        QTest::newRow("two contacts, fingers draw") << u"two"_s << true;
        QTest::newRow("a sliding palm") << u"slide"_s << false;
        QTest::newRow("a palm still down after the pen") << u"stays"_s << false;
        QTest::newRow("a palm still down after the pen, fingers draw") << u"stays"_s << true;
    }
    void palmIsIgnoredWhileThePenIsDown()
    {
        QFETCH(QString, palm);
        QFETCH(bool, fingerDraws);
        canvas->setFingerDraws(fingerDraws);
        const QPointF centre(canvas->centerX(), canvas->centerY());
        Pen pen(window.get());
        Fingers hand(window.get());
        using S = QEventPoint::State;
        pen.send({100, 300}, Qt::LeftButton, 0.3);
        for (int i = 1; i <= 10; ++i)
            pen.send({100 + 10.0 * i, 300}, Qt::LeftButton, 0.3 + 0.02 * i);
        if (palm == u"tap") {
            hand.send({{1, {S::Pressed, {600, 500}}}});
            hand.send({{1, {S::Released, {600, 500}}}});
        } else if (palm == u"two") {
            hand.send({{1, {S::Pressed, {600, 500}}}, {2, {S::Pressed, {660, 520}}}});
            for (int i = 1; i <= 5; ++i)
                hand.send({{1, {S::Updated, {600 - 10.0 * i, 500}}}, {2, {S::Updated, {660 + 10.0 * i, 520}}}});
            hand.send({{1, {S::Released, {550, 500}}}, {2, {S::Released, {710, 520}}}});
        } else {
            hand.send({{1, {S::Pressed, {600, 500}}}});
            for (int i = 1; i <= 5; ++i)
                hand.send({{1, {S::Updated, {600 + 8.0 * i, 500 + 4.0 * i}}}});
            if (palm == u"slide")
                hand.send({{1, {S::Released, {640, 520}}}});
        }
        for (int i = 11; i <= 30; ++i)
            pen.send({100 + 10.0 * i, 300}, Qt::LeftButton, 0.3 + 0.02 * i / 3);
        pen.send({400, 300}, Qt::NoButton, 0);
        if (palm == u"stays") {
            // The pen lifted; the hand moves on and lifts: still nothing.
            for (int i = 6; i <= 12; ++i)
                hand.send({{1, {S::Updated, {600 + 8.0 * i, 500 + 4.0 * i}}}});
            hand.send({{1, {S::Released, {696, 548}}}});
        }
        QCOMPARE(canvas->strokeCount(), 1);
        const brulion::drawing::Stroke s = canvas->currentDrawing().strokes.first();
        for (const auto &p : s.points)
            QVERIFY2(std::abs(p.y - 300) < 0.5, qPrintable(u"%1, %2"_s.arg(p.x).arg(p.y)));
        QVERIFY2(s.points.last().x > 390, qPrintable(QString::number(s.points.last().x))); // drawn to its end
        QVERIFY(!s.simulatePressure);
        QCOMPARE(QPointF(canvas->centerX(), canvas->centerY()), centre);
        QCOMPARE(canvas->zoom(), 1.0);
        QVERIFY(!canvas->busy());
        // The hand lifted: a finger works again.
        hand.drag({200, 450}, {300, 500}, 10);
        QCOMPARE(canvas->strokeCount(), fingerDraws ? 2 : 1);
        if (!fingerDraws)
            QCOMPARE(QPointF(canvas->centerX(), canvas->centerY()), centre - QPointF(100, 50));
    }

    void penTakesOverFromTheHand_data()
    {
        QTest::addColumn<bool>("fingerDraws");
        QTest::newRow("the palm was moving the paper") << false;
        QTest::newRow("the palm had begun a stroke") << true;
    }
    void penTakesOverFromTheHand()
    {
        // The palm lands first, then the pen writes: the pen draws, the palm's gesture ends.
        QFETCH(bool, fingerDraws);
        canvas->setFingerDraws(fingerDraws);
        const QPointF centre(canvas->centerX(), canvas->centerY());
        Pen pen(window.get());
        Fingers hand(window.get());
        using S = QEventPoint::State;
        hand.send({{1, {S::Pressed, {600, 500}}}});
        hand.send({{1, {S::Updated, {601, 500}}}});
        pen.stroke({100, 300}, {400, 300}, 20, 0.2, 0.8);
        for (int i = 1; i <= 5; ++i)
            hand.send({{1, {S::Updated, {601 + 10.0 * i, 500}}}});
        hand.send({{1, {S::Released, {651, 500}}}});
        QCOMPARE(canvas->strokeCount(), 1); // the pen's — the palm's begun stroke dropped
        const brulion::drawing::Stroke s = canvas->currentDrawing().strokes.first();
        QVERIFY(std::abs(s.points.first().y - 300) < 0.5 && std::abs(s.points.last().y - 300) < 0.5);
        QVERIFY(!s.simulatePressure);
        QVERIFY(std::abs(canvas->centerX() - centre.x()) <= 1.01); // (the palm's 1 px before the pen)
        QCOMPARE(canvas->centerY(), centre.y());
    }

    void palmDoesNotEraseWithThePen()
    {
        mouseStroke(window.get(), {100, 100}, {300, 100}, 10);
        mouseStroke(window.get(), {500, 500}, {700, 500}, 10);
        QCOMPARE(canvas->strokeCount(), 2);
        Pen eraser(window.get(), QPointingDevice::PointerType::Eraser);
        Fingers hand(window.get());
        using S = QEventPoint::State;
        eraser.send({200, 50}, Qt::LeftButton, 0.5);
        eraser.send({200, 80}, Qt::LeftButton, 0.5);
        hand.send({{1, {S::Pressed, {600, 500}}}}); // on the second stroke
        hand.send({{1, {S::Updated, {610, 500}}}});
        eraser.send({200, 150}, Qt::LeftButton, 0.5);
        hand.send({{1, {S::Released, {610, 500}}}});
        eraser.send({200, 150}, Qt::NoButton, 0);
        QCOMPARE(canvas->strokeCount(), 1);
        QCOMPARE(canvas->currentDrawing().strokes.first().points.first().y, 500.0);
    }

    void externalChangeDuringAGesture_data()
    {
        QTest::addColumn<QString>("input");
        QTest::addColumn<bool>("modifiedBefore");
        QTest::newRow("pen, saved") << u"pen"_s << false;
        QTest::newRow("pen, a stroke not saved") << u"pen"_s << true;
        QTest::newRow("finger, saved") << u"finger"_s << false;
        QTest::newRow("mouse, saved") << u"mouse"_s << false;
    }
    void externalChangeDuringAGesture()
    {
        // Syncthing delivers another device's version while a stroke is being drawn: the stroke
        // goes on and is kept, on top of the other version.
        QFETCH(QString, input);
        QFETCH(bool, modifiedBefore);
        canvas->setFingerDraws(true);
        QCOMPARE(canvas->load(strokesFile({u"a"_s})), QString());
        if (modifiedBefore)
            mouseStroke(window.get(), {100, 560}, {300, 560}, 6);
        QCOMPARE(canvas->modified(), modifiedBefore);
        Pen pen(window.get());
        Fingers finger(window.get());
        using S = QEventPoint::State;
        const auto at = [](int i) { return QPointF(100 + 12.0 * i, 400); };
        const auto send = [&](int i, int phase) { // 0 press, 1 move, 2 release
            if (input == u"pen")
                pen.send(at(i), phase == 2 ? Qt::NoButton : Qt::LeftButton, 0.3 + 0.01 * i);
            else if (input == u"finger")
                finger.send({{1, {phase == 0 ? S::Pressed : phase == 1 ? S::Updated : S::Released, at(i)}}});
            else if (phase == 0)
                QTest::mousePress(window.get(), Qt::LeftButton, {}, at(i).toPoint());
            else if (phase == 1)
                QTest::mouseMove(window.get(), at(i).toPoint());
            else
                QTest::mouseRelease(window.get(), Qt::LeftButton, {}, at(i).toPoint());
        };
        send(0, 0);
        for (int i = 1; i <= 10; ++i)
            send(i, 1);
        QVERIFY(canvas->busy());
        QVERIFY(canvas->takeExternal(strokesFile({u"a"_s, u"c"_s})));
        QVERIFY(canvas->busy()); // the stroke goes on
        for (int i = 11; i <= 25; ++i)
            send(i, 1);
        send(25, 2);
        QVERIFY(!canvas->busy());
        const QStringList ids = strokeIds(canvas);
        QCOMPARE(ids.size(), modifiedBefore ? 4 : 3);
        QCOMPARE(ids.mid(0, 2), (QStringList{u"a"_s, u"c"_s}));
        const brulion::drawing::Stroke s = canvas->currentDrawing().strokes.last();
        // The whole stroke, from the press to the release.
        const double first = canvas->toScene(at(0)).x(), last = canvas->toScene(at(25)).x();
        QVERIFY2(
                std::abs(s.points.first().x - first) < 1 && std::abs(s.points.last().x - last) < 1,
                qPrintable(u"%1 … %2 (%3 … %4)"_s.arg(s.points.first().x).arg(s.points.last().x).arg(first).arg(last)));
        QVERIFY(canvas->modified());
        QVERIFY(canvas->canUndo()); // the stroke, after the history was cleared
    }

    void externalChangeDuringErasing()
    {
        // The strokes the eraser has touched so far stay erased on the other version.
        QCOMPARE(canvas->load(strokesFile({u"a"_s, u"c"_s})), QString());
        canvas->setTool(u"eraser"_s);
        QTest::mousePress(window.get(), Qt::LeftButton, {}, QPoint(200, 30));
        QTest::mouseMove(window.get(), QPoint(200, 70)); // "a" (y 50)
        QVERIFY(canvas->busy());
        QVERIFY(canvas->takeExternal(strokesFile({u"a"_s, u"c"_s, u"d"_s})));
        QTest::mouseMove(window.get(), QPoint(200, 90));
        QTest::mouseRelease(window.get(), Qt::LeftButton, {}, QPoint(200, 90));
        QCOMPARE(strokeIds(canvas), (QStringList{u"c"_s, u"d"_s}));
        QVERIFY(canvas->modified());
    }

    void externalPaperMerge_data()
    {
        QTest::addColumn<QString>("here");   // the paper chosen here ("" none)
        QTest::addColumn<bool>("savedHere"); // and saved before the other version came
        QTest::addColumn<QString>("theirs"); // the other version's paper
        QTest::addColumn<QString>("expected");
        QTest::newRow("not changed here: theirs") << QString() << false << u"grid"_s << u"grid"_s;
        QTest::newRow("changed here: ours") << u"lines"_s << false << u"grid"_s << u"lines"_s;
        QTest::newRow("changed here and saved: theirs") << u"lines"_s << true << u"grid"_s << u"grid"_s;
        QTest::newRow("changed back here: theirs") << u"plain"_s << false << u"grid"_s << u"grid"_s;
    }
    void externalPaperMerge()
    {
        QFETCH(QString, here);
        QFETCH(bool, savedHere);
        QFETCH(QString, theirs);
        QFETCH(QString, expected);
        QCOMPARE(canvas->load(strokesFile({u"a"_s})), QString());
        if (here == u"plain") {
            canvas->setBackground(u"lines"_s);
            canvas->setBackground(u"plain"_s); // as it was loaded
        } else if (!here.isEmpty()) {
            canvas->setBackground(here);
        }
        if (savedHere)
            canvas->markSaved(canvas->svg());
        mouseStroke(window.get(), {100, 500}, {300, 500}, 6); // a stroke not saved: a merge
        QVERIFY(canvas->modified());
        QVERIFY(canvas->takeExternal(strokesFile({u"a"_s, u"ż"_s}, theirs)));
        QCOMPARE(canvas->background(), expected);
        QCOMPARE(canvas->strokeCount(), 3);
    }

    void farAwayStrokesAreNotEdited()
    {
        // A synced scene with a stroke at 1e18 on lined paper: shown as a picture (not edited),
        // so the editor never draws lines out there; the view never goes beyond the limit.
        const QByteArray scene = R"({"type":"excalidraw","version":2,"elements":[{"type":"freedraw","id":"daleko",)"
                                 R"("x":1e18,"y":1e18,"points":[[0,0],[1024,1024]]}],)"
                                 R"("appState":{"brulionBackground":"lines"}})";
        QCOMPARE(canvas->load(scene), u"readonly"_s);
        QCOMPARE(canvas->load({}), QString());
        canvas->setBackground(u"grid"_s);
        canvas->setCenterY(1e18);
        canvas->setCenterX(-1e18);
        QCOMPARE(canvas->centerY(), brulion::drawing::CoordinateLimit);
        QCOMPARE(canvas->centerX(), -brulion::drawing::CoordinateLimit);
        QImage image(800, 600, QImage::Format_ARGB32_Premultiplied);
        {
            QPainter p(&image);
            canvas->render(&p, QSizeF(800, 600)); // ends (the lines counted, not stepped)
        }
        // A stroke drawn out there stays within the limit: the file can be edited again.
        mouseStroke(window.get(), {100, 100}, {700, 100}, 10);
        QCOMPARE(canvas->strokeCount(), 1);
        DrawingCanvas again;
        QCOMPARE(again.load(canvas->svg()), QString());
    }

    // --- Light and dark ---------------------------------------------------------------------

    void darkRendering_data()
    {
        QTest::addColumn<QString>("theme");
        QTest::addColumn<bool>("appDark");
        QTest::addColumn<bool>("dark");
        QTest::newRow("as the app, light") << u"auto"_s << false << false;
        QTest::newRow("as the app, dark") << u"auto"_s << true << true;
        QTest::newRow("light in a dark app") << u"light"_s << true << false;
        QTest::newRow("dark in a light app") << u"dark"_s << false << true;
        QTest::newRow("dark in a dark app") << u"dark"_s << true << true;
    }
    void darkRendering()
    {
        QFETCH(QString, theme);
        QFETCH(bool, appDark);
        QFETCH(bool, dark);
        canvas->setAppDark(appDark);
        canvas->setBackground(u"lines"_s);
        canvas->setTheme(theme);
        QCOMPARE(canvas->theme(), theme);
        QCOMPARE(canvas->dark(), dark);
        mouseStroke(window.get(), {100, 100}, {300, 100}, 10);
        const QImage image = painted(canvas);
        const QColor paper = image.pixelColor(400, 50);
        const QColor ink = image.pixelColor(200, 100);
        QCOMPARE(paper.name(), dark ? u"#121212"_s : u"#ffffff"_s);
        QCOMPARE(canvas->paperColor().name(), paper.name());
        QVERIFY2(near(ink, QColor(dark ? u"#caced8"_s : u"#1f2430"_s)), qPrintable(ink.name()));
        QCOMPARE(image.pixelColor(400, 64).name(), dark ? u"#323a45"_s : brulion::drawing::RuleColor.toString());
        // The stroke keeps its colour as drawn.
        QCOMPARE(canvas->currentDrawing().strokes.last().color, u"#1f2430"_s);
    }

    void seeThroughPaper_data()
    {
        QTest::addColumn<QString>("paper");
        QTest::addColumn<bool>("appDark");
        QTest::addColumn<QString>("shown");
        // Excalidraw's background picker takes any colour, "#rrggbbaa" too: the paper is opaque,
        // as it shows over white (Qt would read the nine characters as #aarrggbb: a see-through
        // navy in the dark, an opaque yellow in the light).
        QTest::newRow("half white, light") << u"#ffffff80"_s << false << u"#ffffff"_s;
        QTest::newRow("half white, dark") << u"#ffffff80"_s << true << u"#121212"_s;
        QTest::newRow("invisible, dark") << u"#00000000"_s << true << u"#121212"_s;
        QTest::newRow("half ink, light") << u"#1f243080"_s << false << u"#8f9197"_s;
    }
    void seeThroughPaper()
    {
        QFETCH(QString, paper);
        QFETCH(bool, appDark);
        QFETCH(QString, shown);
        brulion::drawing::Drawing d;
        d.paper = paper;
        canvas->setAppDark(appDark);
        QCOMPARE(canvas->load(brulion::drawing::toSvg(d)), QString());
        QCOMPARE(canvas->paperColor().name(QColor::HexArgb), u"#ff"_s + shown.mid(1));
        mouseStroke(window.get(), {100, 100}, {300, 100}, 10);
        QImage image = painted(canvas);
        QCOMPARE(image.pixelColor(400, 50).name(QColor::HexArgb), u"#ff"_s + shown.mid(1));
        QVERIFY(!near(image.pixelColor(200, 100), QColor(shown), 20));
        // The stroke taken back: the paper again where it was (the reused cache is painted over).
        canvas->undo();
        image = painted(canvas);
        QCOMPARE(image.pixelColor(200, 100).name(QColor::HexArgb), u"#ff"_s + shown.mid(1));
        // The previews' renderer too.
        const QImage preview = brulion::drawingview::renderDrawing(canvas->currentDrawing(), QSize(64, 36), appDark);
        QCOMPARE(preview.pixelColor(1, 1).name(QColor::HexArgb), u"#ff"_s + shown.mid(1));
        QCOMPARE(brulion::drawingview::toQColor(u"#12345680").name(QColor::HexArgb), u"#80123456"_s);
        QVERIFY(!brulion::drawingview::toQColor(u"granatowy").isValid());
    }

    void themeIsAnUndoStepAndSaved()
    {
        mouseStroke(window.get(), {100, 100}, {300, 100}, 10);
        canvas->markSaved(canvas->svg());
        QCOMPARE(canvas->theme(), u"auto"_s);
        QSignalSpy themes(canvas, &DrawingCanvas::themeChanged);
        QSignalSpy content(canvas, &DrawingCanvas::contentChanged);
        canvas->setTheme(u"dark"_s);
        QCOMPARE(canvas->theme(), u"dark"_s);
        QVERIFY(canvas->dark());
        QVERIFY(canvas->modified());
        QVERIFY(canvas->canUndo());
        QCOMPARE(themes.size(), 1);
        QCOMPARE(content.size(), 1);
        const QByteArray svg = canvas->svg();
        QCOMPARE(appStateOf(svg).value(u"brulionTheme"_s).toString(), u"dark"_s);
        QVERIFY(svg.contains("fill=\"#121212\"")); // the picture is dark for every program
        // The stroke in the scene as drawn.
        QCOMPARE(brulion::drawing::readDrawing(svg)->strokes.first().color, u"#1f2430"_s);
        DrawingCanvas other;
        QCOMPARE(other.load(svg), QString());
        QCOMPARE(other.theme(), u"dark"_s);
        QVERIFY(other.dark());
        canvas->undo();
        QCOMPARE(canvas->theme(), u"auto"_s);
        QVERIFY(!canvas->dark());
        QVERIFY(!appStateOf(canvas->svg()).contains(u"brulionTheme"_s));
        canvas->redo();
        QCOMPARE(canvas->theme(), u"dark"_s);
        // Unknown names are "as the app"; the same theme again changes nothing.
        canvas->setTheme(u"dark"_s);
        QCOMPARE(themes.size(), 3);
        canvas->setTheme(u"sepia"_s);
        QCOMPARE(canvas->theme(), u"auto"_s);
        // While a stroke is being drawn too (only the look changes); undo waits for its end.
        QTest::mousePress(window.get(), Qt::LeftButton, {}, QPoint(100, 300));
        QTest::mouseMove(window.get(), QPoint(150, 300));
        QVERIFY(canvas->busy());
        canvas->setTheme(u"light"_s);
        QCOMPARE(canvas->theme(), u"light"_s);
        QTest::mouseMove(window.get(), QPoint(200, 300));
        QTest::mouseRelease(window.get(), Qt::LeftButton, {}, QPoint(200, 300));
        QCOMPARE(canvas->strokeCount(), 2);
        canvas->undo();
        canvas->undo();
        QCOMPARE(canvas->theme(), u"auto"_s);
        // Not editable: refused.
        QCOMPARE(canvas->load(fixture(u"excalidraw_frame.svg"_s)), u"readonly"_s);
        canvas->setTheme(u"dark"_s);
        QCOMPARE(canvas->theme(), u"auto"_s);
        QVERIFY(!canvas->modified());
    }

    void appDarkIsNoEdit()
    {
        mouseStroke(window.get(), {100, 100}, {300, 100}, 10);
        canvas->markSaved(canvas->svg());
        QSignalSpy render(canvas, &DrawingCanvas::renderChanged);
        QSignalSpy content(canvas, &DrawingCanvas::contentChanged);
        canvas->setAppDark(true);
        QVERIFY(canvas->dark());
        QCOMPARE(render.size(), 1);
        canvas->setAppDark(true);
        QCOMPARE(render.size(), 1);
        QVERIFY(!canvas->modified());
        QVERIFY(content.isEmpty());
        QCOMPARE(canvas->paperColor().name(), u"#121212"_s);
        QVERIFY(!appStateOf(canvas->svg()).contains(u"brulionTheme"_s));
        // A light drawing stays light: nothing to repaint.
        canvas->setTheme(u"light"_s);
        QCOMPARE(render.size(), 2);
        canvas->setAppDark(false);
        canvas->setAppDark(true);
        QCOMPARE(render.size(), 2);
        // Loading repaints (another drawing, another paper).
        canvas->load(inkFile(brulion::drawing::PaperTheme::Dark));
        QCOMPARE(render.size(), 3);
        QCOMPARE(canvas->theme(), u"dark"_s);
    }

    void themeMerge_data()
    {
        QTest::addColumn<QString>("here");   // the theme chosen here ("" none)
        QTest::addColumn<bool>("savedHere"); // and saved before the other version came
        QTest::addColumn<QString>("theirs"); // the other version's theme
        QTest::addColumn<QString>("expected");
        QTest::newRow("not changed here: theirs") << QString() << false << u"dark"_s << u"dark"_s;
        QTest::newRow("changed here: ours") << u"light"_s << false << u"dark"_s << u"light"_s;
        QTest::newRow("changed here and saved: theirs") << u"light"_s << true << u"dark"_s << u"dark"_s;
        QTest::newRow("changed back here: theirs") << u"auto"_s << false << u"dark"_s << u"dark"_s;
        QTest::newRow("theirs as the app") << u"dark"_s << true << u"auto"_s << u"auto"_s;
    }
    void themeMerge()
    {
        QFETCH(QString, here);
        QFETCH(bool, savedHere);
        QFETCH(QString, theirs);
        QFETCH(QString, expected);
        QCOMPARE(canvas->load(strokesFile({u"a"_s})), QString());
        if (here == u"auto") {
            canvas->setTheme(u"dark"_s);
            canvas->setTheme(u"auto"_s); // as it was loaded
        } else if (!here.isEmpty()) {
            canvas->setTheme(here);
        }
        if (savedHere)
            canvas->markSaved(canvas->svg());
        mouseStroke(window.get(), {100, 500}, {300, 500}, 6); // a stroke not saved: a merge
        QVERIFY(canvas->modified());
        brulion::drawing::Drawing other = *brulion::drawing::readDrawing(strokesFile({u"a"_s, u"ż"_s}));
        other.theme = brulion::drawing::paperThemeFromName(theirs);
        QSignalSpy themes(canvas, &DrawingCanvas::themeChanged);
        QVERIFY(canvas->takeExternal(brulion::drawing::toSvg(other)));
        QCOMPARE(canvas->theme(), expected);
        QCOMPARE(themes.size(), 1);
        QCOMPARE(canvas->strokeCount(), 3);
        // Not modified here: the other version as it is.
        canvas->markSaved(canvas->svg());
        other.theme = brulion::drawing::PaperTheme::Light;
        QVERIFY(canvas->takeExternal(brulion::drawing::toSvg(other)));
        QCOMPARE(canvas->theme(), u"light"_s);
    }

    void liveStrokeIsShownDark()
    {
        canvas->setAppDark(true);
        QTest::mousePress(window.get(), Qt::LeftButton, {}, QPoint(100, 400));
        for (int i = 1; i <= 10; ++i)
            QTest::mouseMove(window.get(), QPoint(100 + 20 * i, 400));
        QVERIFY(canvas->busy());
        QImage live = painted(canvas);
        QVERIFY2(near(live.pixelColor(200, 400), QColor(u"#caced8"_s)), qPrintable(live.pixelColor(200, 400).name()));
        // The app turns light mid-stroke: the stroke goes on in the light ink.
        canvas->setAppDark(false);
        live = painted(canvas);
        QVERIFY2(near(live.pixelColor(200, 400), QColor(u"#1f2430"_s)), qPrintable(live.pixelColor(200, 400).name()));
        QTest::mouseRelease(window.get(), Qt::LeftButton, {}, QPoint(300, 400));
        QCOMPARE(canvas->strokeCount(), 1);
        // A highlighter keeps its colour in the dark, a little stronger.
        canvas->setAppDark(true);
        canvas->setTool(u"highlighter"_s);
        canvas->setColor(QColor(u"#ffd43b"_s));
        canvas->setPenWidth(8);
        mouseStroke(window.get(), {100, 200}, {300, 200}, 10);
        const QColor seen = painted(canvas).pixelColor(200, 200);
        const QColor expected = [] {
            const auto c = brulion::drawing::over({0xff, 0xd4, 0x3b, 255}, 50, {0x12, 0x12, 0x12, 255});
            return QColor(c.r, c.g, c.b);
        }();
        QVERIFY2(near(seen, expected, 3), qPrintable(seen.name() + u" ≠ "_s + expected.name()));
        // The eraser's ring shows on the dark paper.
        canvas->setTool(u"eraser"_s);
        QTest::mouseMove(window.get(), QPoint(500, 500));
        const QImage ring = painted(canvas);
        QVERIFY(ring.pixelColor(500 + int(DrawingCanvas::EraserRadius), 500).lightness() > 60);
    }

    void displayColorOfSwatches()
    {
        const QColor ink(u"#1f2430"_s);
        QCOMPARE(canvas->displayColor(ink), ink);
        canvas->setAppDark(true);
        QCOMPARE(canvas->displayColor(ink).name(), u"#caced8"_s);
        QCOMPARE(canvas->displayColor(QColor(u"#f08c00"_s)).name(), u"#b86200"_s);
        QCOMPARE(canvas->displayColor(QColor(u"#ffd43b"_s), true).name(), u"#ffd43b"_s);
        QVERIFY(!canvas->displayColor(QColor()).isValid());
        canvas->setTheme(u"light"_s);
        QCOMPARE(canvas->displayColor(ink), ink);
    }

    void qtSvgIgnoresDarkCss()
    {
        // The file of an "as the app" drawing holds the dark colours for browsers in an @media
        // block; Qt's SVG renderer (other pictures' previews, the old embeds) draws the light one.
        const QByteArray svg = inkFile(brulion::drawing::PaperTheme::Auto);
        QVERIFY(svg.contains("@media (prefers-color-scheme: dark)"));
        QSvgRenderer renderer(svg);
        QVERIFY(renderer.isValid());
        const QImage image = renderSvg(svg, renderer.defaultSize());
        QCOMPARE(image.pixelColor(2, 2).name(), u"#ffffff"_s);
        const QPointF inkScene(200, 100);
        const QRectF rect = brulion::drawing::pictureRect(*brulion::drawing::readDrawing(svg));
        const QColor ink = image.pixelColor((inkScene - rect.topLeft()).toPoint());
        QVERIFY2(near(ink, QColor(u"#1f2430"_s)), qPrintable(ink.name()));
    }

    void renderDrawingFitsThePicture()
    {
        const brulion::drawing::Drawing d = *brulion::drawing::readDrawing(inkFile(brulion::drawing::PaperTheme::Auto));
        const QRectF rect = brulion::drawing::pictureRect(d);
        QVERIFY(brulion::drawingview::renderDrawing(d, QSize(), false).isNull());
        // At its own size: the stroke where the SVG has it.
        QImage image = brulion::drawingview::renderDrawing(d, rect.size().toSize(), false);
        QCOMPARE(image.size(), rect.size().toSize());
        QVERIFY(near(image.pixelColor((QPointF(200, 100) - rect.topLeft()).toPoint()), QColor(u"#1f2430"_s)));
        QCOMPARE(image.pixelColor(1, 1).name(), u"#ffffff"_s);
        // Into a box of another shape: aspect kept, centred on the paper.
        image = brulion::drawingview::renderDrawing(d, QSize(int(rect.width()), int(rect.height()) * 3), true);
        QCOMPARE(image.pixelColor(1, 1).name(), u"#121212"_s);
        const QPoint centre(int((200 - rect.left())), int(rect.height() * 1.5 + (100 - rect.center().y())));
        QVERIFY2(near(image.pixelColor(centre), QColor(u"#caced8"_s)), qPrintable(image.pixelColor(centre).name()));
    }

    void darkImageMatchesDarkColor()
    {
        QImage light(4, 2, QImage::Format_ARGB32);
        const QList<QRgb> colours = {qRgb(255, 255, 255),    qRgb(0x1f, 0x24, 0x30), qRgb(0xe0, 0x31, 0x31),
                                     qRgb(0xff, 0xd4, 0x3b), qRgb(0, 0, 0),          qRgb(0x2f, 0x9e, 0x44),
                                     qRgb(0x12, 0x34, 0x56), qRgb(0xc9, 0xd3, 0xe0)};
        for (int i = 0; i < colours.size(); ++i)
            light.setPixel(i % 4, i / 4, colours.at(i));
        const QImage dark = brulion::drawingview::darkImage(light).convertToFormat(QImage::Format_ARGB32);
        QCOMPARE(dark.size(), light.size());
        for (int i = 0; i < colours.size(); ++i) {
            const QRgb c = colours.at(i);
            const brulion::drawing::Rgba expected = brulion::drawing::darkRgba({qRed(c), qGreen(c), qBlue(c), 255});
            const QRgb got = dark.pixel(i % 4, i / 4);
            QCOMPARE(QColor(got).name(), QColor(expected.r, expected.g, expected.b).name());
            QCOMPARE(qAlpha(got), 255);
        }
        // Translucent pixels keep their alpha; nothing for nothing.
        QImage translucent(1, 1, QImage::Format_ARGB32);
        translucent.setPixel(0, 0, qRgba(255, 255, 255, 128));
        QCOMPARE(
                qAlpha(brulion::drawingview::darkImage(translucent).convertToFormat(QImage::Format_ARGB32).pixel(0, 0)),
                128);
        QVERIFY(brulion::drawingview::darkImage(QImage()).isNull());
    }

    void imageProvider_data()
    {
        QTest::addColumn<QByteArray>("bytes"); // the file ("" none)
        QTest::addColumn<QString>("name");     // its name in the temporary folder
        QTest::addColumn<bool>("appDark");
        QTest::addColumn<QString>("paper"); // the corner pixel ("" no picture)
        QTest::addColumn<QString>("ink");   // at the stroke ("" not checked)
        using brulion::drawing::PaperTheme;
        QTest::newRow("as the app, light")
                << inkFile(PaperTheme::Auto) << u"Rysunek ćwiczenia.svg"_s << false << u"#ffffff"_s << u"#1f2430"_s;
        QTest::newRow("as the app, dark")
                << inkFile(PaperTheme::Auto) << u"Rysunek ćwiczenia.svg"_s << true << u"#121212"_s << u"#caced8"_s;
        QTest::newRow("light paper, dark app")
                << inkFile(PaperTheme::Light) << u"Jasny.svg"_s << true << u"#ffffff"_s << u"#1f2430"_s;
        QTest::newRow("dark paper, light app")
                << inkFile(PaperTheme::Dark) << u"Ciemny #1 (100%).svg"_s << false << u"#121212"_s << u"#caced8"_s;
        // An Excalidraw drawing with shapes: drawn by Brulion, in the file's frame.
        QTest::newRow("excalidraw, light")
                << fixture(u"excalidraw_mixed.svg"_s) << u"Schemat.svg"_s << false << u"#ffffff"_s << QString();
        QTest::newRow("excalidraw, dark")
                << fixture(u"excalidraw_mixed.svg"_s) << u"Schemat.svg"_s << true << u"#121212"_s << QString();
        // A .excalidraw scene with shapes: Brulion's renderer draws it.
        QTest::newRow(".excalidraw") << fixture(u"excalidraw_mixed.excalidraw"_s) << u"scena.excalidraw"_s << true
                                     << u"#121212"_s << QString();
        QTest::newRow("a frame: its own picture, dark")
                << fixture(u"excalidraw_frame.svg"_s) << u"Ramka.svg"_s << true << u"#121212"_s << QString();
        // A freehand drawing Excalidraw wrote: Brulion's renderer (in the file's frame).
        QTest::newRow("excalidraw freehand, light")
                << fixture(u"excalidraw_freedraw.svg"_s) << u"Odręczny.svg"_s << false << u"#ffffff"_s << QString();
        QTest::newRow("excalidraw freehand, dark")
                << fixture(u"excalidraw_freedraw.svg"_s) << u"Odręczny.svg"_s << true << u"#121212"_s << QString();
        // A scene that cannot be read (damaged, too large): the file's picture — turned dark in a
        // dark app, but a "dark" drawing's picture is dark already and stays so.
        QTest::newRow("unreadable, as the app, light")
                << unreadable(inkFile(PaperTheme::Auto)) << u"Zepsuty.svg"_s << false << u"#ffffff"_s << QString();
        QTest::newRow("unreadable, as the app, dark")
                << unreadable(inkFile(PaperTheme::Auto)) << u"Zepsuty.svg"_s << true << u"#121212"_s << QString();
        QTest::newRow("unreadable, dark paper, dark app")
                << unreadable(inkFile(PaperTheme::Dark)) << u"Zepsuty ciemny.svg"_s << true << u"#121212"_s
                << QString();
        QTest::newRow("unreadable, dark paper, light app")
                << unreadable(inkFile(PaperTheme::Dark)) << u"Zepsuty ciemny.svg"_s << false << u"#121212"_s
                << QString();
        // Drawings of Obsidian's Excalidraw plugin: Brulion's renderer; one with a frame: its
        // elements and a dashed box (no export of the plugin beside it).
        QTest::newRow("plugin, light") << fixture(u"obsidian_freedraw.excalidraw.md"_s)
                                       << u"Szkic ćwiczeń.excalidraw.md"_s << false << u"#ffffff"_s << QString();
        QTest::newRow("plugin, dark") << fixture(u"obsidian_mixed.excalidraw.md"_s) << u"Schemat.excalidraw.md"_s
                                      << true << u"#121212"_s << QString();
        QTest::newRow("plugin, CRLF") << fixture(u"obsidian_crlf.excalidraw.md"_s) << u"Plan.md"_s << false
                                      << u"#ffffff"_s << QString();
        QTest::newRow("plugin with a frame") << fixture(u"obsidian_frame.excalidraw.md"_s) << u"Ramka.excalidraw.md"_s
                                             << true << u"#121212"_s << QString();
        QTest::newRow("plugin's oldest form") << fixture(u"obsidian_legacy.excalidraw.md"_s) << u"Stary.excalidraw.md"_s
                                              << false << u"#ffffff"_s << QString();
        QTest::newRow("a note") << QByteArray("---\ntytul: Notatka\n---\n# Notatka ąę\n") << u"Notatka.md"_s << false
                                << QString() << QString();
        QTest::newRow("empty file") << QByteArray() << u"Pusty.svg"_s << false << QString() << QString();
        QTest::newRow("not svg") << QByteArray("<svg/>") << u"notatka.txt"_s << false << QString() << QString();
        QTest::newRow("garbage") << QByteArray("\x89PNG zażółć") << u"Zepsuty.svg"_s << true << QString() << QString();
        QTest::newRow("missing") << QByteArray("-") << u"Brak.svg"_s << false << QString() << QString();
    }
    void imageProvider()
    {
        QFETCH(QByteArray, bytes);
        QFETCH(QString, name);
        QFETCH(bool, appDark);
        QFETCH(QString, paper);
        QFETCH(QString, ink);
        QTemporaryDir dir;
        const QString path = dir.filePath(name);
        if (bytes != "-") {
            QFile f(path);
            QVERIFY(f.open(QIODevice::WriteOnly));
            f.write(bytes);
        }
        brulion::drawingview::DrawingImageProvider provider;
        QCOMPARE(provider.imageType(), QQmlImageProviderBase::Image);
        const QUrl url = brulion::drawingview::DrawingImageProvider::url(path, u"123-456"_s, appDark);
        QSize size;
        const QImage natural = provider.requestImage(imageId(url), &size, QSize());
        QCOMPARE(natural.isNull(), paper.isEmpty());
        if (paper.isEmpty())
            return;
        QCOMPARE(natural.size(), size);
        QCOMPARE(natural.pixelColor(2, 2).name(), paper);
        if (!ink.isEmpty()) {
            const QRectF rect = brulion::drawing::pictureRect(*brulion::drawing::readDrawing(bytes));
            const QColor at = natural.pixelColor((QPointF(200, 100) - rect.topLeft()).toPoint());
            QVERIFY2(near(at, QColor(ink)), qPrintable(at.name()));
        }
        // Fitted into a requested size, aspect kept; one side: from the other; huge: capped.
        QImage fitted = provider.requestImage(imageId(url), &size, QSize(size.width() / 2, 10000));
        QCOMPARE(fitted.width(), size.width() / 2);
        QVERIFY(std::abs(fitted.height() - size.height() / 2) <= 1);
        fitted = provider.requestImage(imageId(url), nullptr, QSize(0, size.height() * 2));
        QVERIFY(std::abs(fitted.width() - size.width() * 2) <= 1);
        fitted = provider.requestImage(imageId(url), nullptr, QSize(100000, 100000));
        QVERIFY(fitted.width() <= brulion::drawingview::DrawingImageProvider::MaxSide
                && fitted.height() <= brulion::drawingview::DrawingImageProvider::MaxSide);
        // Through Qt Quick's Image, as the embeds load it (the id as Qt hands it over).
        QQmlEngine engine;
        engine.addImageProvider(u"drawing"_s, new brulion::drawingview::DrawingImageProvider);
        QQmlComponent component(&engine);
        component.setData("import QtQuick\nImage { asynchronous: true; retainWhileLoading: true }", QUrl());
        std::unique_ptr<QObject> image(component.create());
        QVERIFY2(image, qPrintable(component.errorString()));
        image->setProperty("source", url);
        QTRY_COMPARE(image->property("status").toInt(), 1); // Image.Ready
        QCOMPARE(image->property("implicitWidth").toInt(), size.width());
        // The other theme (the app's switched): the shown picture and its size stay until the
        // other one is there — what EmbedView's frame keeps its size by.
        const int width = image->property("implicitWidth").toInt();
        const int height = image->property("implicitHeight").toInt();
        image->setProperty("source", brulion::drawingview::DrawingImageProvider::url(path, u"123-456"_s, !appDark));
        QVERIFY(image->property("status").toInt() == 2 || image->property("status").toInt() == 1);
        QCOMPARE(image->property("implicitWidth").toInt(), width);
        QCOMPARE(image->property("implicitHeight").toInt(), height);
        QTRY_COMPARE(image->property("status").toInt(), 1);
        QCOMPARE(image->property("implicitWidth").toInt(), width);
    }

    void imageProviderKeepsPicturesInTheDark_data()
    {
        QTest::addColumn<bool>("appDark");
        QTest::newRow("light") << false;
        QTest::newRow("dark") << true;
    }
    void imageProviderKeepsPicturesInTheDark()
    {
        // An Excalidraw drawing Brulion does not draw itself, in the dark: its colours one by one
        // as Excalidraw's dark mode shows them — a pasted photo keeps its own (the filter on
        // every pixel would show it as a negative).
        QFETCH(bool, appDark);
        const QByteArray bytes = pictureFile();
        const std::optional<brulion::drawing::Drawing> d = brulion::drawing::readDrawing(bytes);
        QVERIFY(d && !brulion::drawing::editable(*d));
        QSize natural;
        const QImage image = brulion::drawingview::DrawingImageProvider::picture(bytes, appDark, QSize(), &natural);
        QCOMPARE(natural, QSize(120, 60));
        QCOMPARE(image.size(), QSize(120, 60));
        QCOMPARE(image.pixelColor(2, 2).name(), appDark ? u"#121212"_s : u"#ffffff"_s);
        QVERIFY2(near(image.pixelColor(30, 30), QColor(u"#ff8800"_s)), qPrintable(image.pixelColor(30, 30).name()));
        const QColor line(appDark ? brulion::drawing::darkColor(u"#1e1e1e") : u"#1e1e1e"_s);
        QVERIFY2(near(image.pixelColor(85, 30), line, 3), qPrintable(image.pixelColor(85, 30).name()));
    }

    void imageProviderKeepsTheFilesFrame_data()
    {
        QTest::addColumn<bool>("appDark");
        QTest::newRow("light") << false;
        QTest::newRow("dark") << true;
    }
    void imageProviderKeepsTheFilesFrame()
    {
        // A freehand drawing Excalidraw wrote names its own size (240 × 230: its margins, its
        // bounds) — what the embeds, the picture view and the notice are sized by. Brulion draws
        // it in that frame: the strokes at one unit a pixel, centred; the frame never changes
        // shape when the picture has loaded.
        QFETCH(bool, appDark);
        const QByteArray bytes = fixture(u"excalidraw_freedraw.svg"_s);
        const QSize own(240, 230);
        QCOMPARE(QSvgRenderer(bytes).defaultSize(), own);
        const std::optional<brulion::drawing::Drawing> d = brulion::drawing::readDrawing(bytes);
        QVERIFY(d && brulion::drawing::editable(*d));
        const QRectF rect = brulion::drawing::pictureRect(*d);
        QVERIFY(rect.size().toSize() != own);
        QSize natural;
        const QImage image = brulion::drawingview::DrawingImageProvider::picture(bytes, appDark, QSize(), &natural);
        QCOMPARE(natural, own);
        QCOMPARE(image.size(), own);
        // An embed's sourceSize in a narrow column: the same shape.
        QCOMPARE(brulion::drawingview::DrawingImageProvider::picture(bytes, appDark, QSize(120, 0)).size(),
                 QSize(120, 115));
        // The ink as large as drawn (Brulion's picture less its margins), in the middle.
        const QColor paper(appDark ? u"#121212"_s : u"#ffffff"_s);
        QCOMPARE(image.pixelColor(0, 0).name(), paper.name());
        QCOMPARE(image.pixelColor(own.width() - 1, own.height() - 1).name(), paper.name());
        const QRect ink = inkBox(image, paper);
        const QSizeF drawn = rect.size() - 2 * QSizeF(brulion::drawing::SvgMargin, brulion::drawing::SvgMargin);
        QVERIFY2(std::abs(ink.width() - drawn.width()) <= 4 && std::abs(ink.height() - drawn.height()) <= 4,
                 qPrintable(u"%1 x %2 drawn %3 x %4"_s.arg(ink.width())
                                    .arg(ink.height())
                                    .arg(drawn.width())
                                    .arg(drawn.height())));
        QVERIFY(std::abs(ink.center().x() - own.width() / 2.0) <= 3);
        QVERIFY(std::abs(ink.center().y() - own.height() / 2.0) <= 3);
        // Brulion's own file of it (saved again) names its picture's size: the frame is that.
        const QByteArray again = brulion::drawing::toSvg(*d);
        QCOMPARE(QSvgRenderer(again).defaultSize(), rect.size().toSize());
        QCOMPARE(brulion::drawingview::DrawingImageProvider::picture(again, appDark, QSize(), &natural).size(),
                 rect.size().toSize());
        QCOMPARE(natural, rect.size().toSize());
    }

    void imageProviderPluginPreviews()
    {
        // A plugin drawing Brulion does not draw (a frame): its elements with a dashed grey box for
        // the frame — never a blank picture — or the plugin's export beside it when there is one.
        QTemporaryDir dir;
        const QString path = dir.filePath(u"Szkic ćwiczeń.excalidraw.md"_s);
        QFile f(path);
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write(fixture(u"obsidian_frame.excalidraw.md"_s));
        f.close();
        brulion::drawingview::DrawingImageProvider provider;
        const QUrl url = brulion::drawingview::DrawingImageProvider::url(path, u"1-1"_s, false);
        QSize size;
        QImage image = provider.requestImage(imageId(url), &size, QSize());
        QVERIFY(!image.isNull());
        const brulion::drawing::Drawing preview = brulion::drawing::previewDrawing(
                *brulion::drawing::readDrawing(fixture(u"obsidian_frame.excalidraw.md"_s)));
        const QRectF rect = brulion::drawing::pictureRect(preview);
        QCOMPARE(size, rect.size().toSize());
        // The frame's dashed grey outline on its left edge (x 360): nothing else is drawn there.
        bool grey = false;
        const int edge = int(std::round(360 - rect.left()));
        for (int y = int(180 - rect.top()); y < int(480 - rect.top()) && !grey; ++y) {
            for (int x = edge - 2; x <= edge + 2 && !grey; ++x) {
                const QColor c = image.pixelColor(x, y);
                grey = c.red() < 235 && std::abs(c.red() - c.blue()) < 40;
            }
        }
        QVERIFY(grey);
        // The plugin's export: its picture and size instead (an SVG, then a PNG).
        const QByteArray red = "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"300\" height=\"150\">"
                               "<rect width=\"300\" height=\"150\" fill=\"#e03131\"/></svg>";
        QFile svg(dir.filePath(u"Szkic ćwiczeń.excalidraw.svg"_s));
        QVERIFY(svg.open(QIODevice::WriteOnly));
        svg.write(red);
        svg.close();
        image = provider.requestImage(imageId(url), &size, QSize());
        QCOMPARE(size, QSize(300, 150));
        QVERIFY(near(image.pixelColor(150, 75), QColor(u"#e03131"_s)));
        QVERIFY(QFile::remove(svg.fileName()));
        QImage png(40, 20, QImage::Format_RGB32);
        png.fill(QColor(u"#2f9e44"_s));
        QVERIFY(png.save(dir.filePath(u"Szkic ćwiczeń.png"_s)));
        image = provider.requestImage(imageId(url), &size, QSize(80, 0));
        QCOMPARE(size, QSize(40, 20));
        QCOMPARE(image.size(), QSize(80, 40));
        QVERIFY(near(image.pixelColor(40, 20), QColor(u"#2f9e44"_s)));
    }

    void imageProviderRefuses_data()
    {
        QTest::addColumn<QString>("id");
        QTest::newRow("empty") << QString();
        QTest::newRow("relative") << u"assets%2FRysunek.svg?dark=1"_s;
        QTest::newRow("dot dot") << u"%2Ftmp%2F..%2Fetc%2Fpasswd.svg?dark=0"_s;
        QTest::newRow("another kind of file") << u"%2Fetc%2Fpasswd?v=1"_s;
        QTest::newRow("a folder") << u"%2Ftmp.svg"_s;
        QTest::newRow("nul") << u"%2Ftmp%2Fa%00b.svg"_s;
        QTest::newRow("no such file") << u"%2Fnie%2Fma%2Ftakiego%2Fpliku ąę.svg?v=0-0&dark=1"_s;
    }
    void imageProviderRefuses()
    {
        QFETCH(QString, id);
        brulion::drawingview::DrawingImageProvider provider;
        QSize size(5, 5);
        QVERIFY(provider.requestImage(id, &size, QSize(100, 100)).isNull());
        QVERIFY(!size.isValid());
    }

    void localFileUrlWithAQueryLoads()
    {
        // Embeds of drawings carry ?v=<mtime>-<size> (Workspace::assetUrl): Qt's pixmap cache keys
        // by the whole URL, so a drawing saved again is loaded again; the file is still found.
        QTemporaryDir dir;
        const QString path = dir.filePath(u"Rysunek ćwiczenia.svg"_s);
        QFile f(path);
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write(brulion::drawing::toSvg(brulion::drawing::Drawing()));
        f.close();
        QUrl url = QUrl::fromLocalFile(path);
        url.setQuery(u"v=1-2"_s);
        QQmlEngine engine;
        QQmlComponent component(&engine);
        component.setData("import QtQuick\nImage { asynchronous: false }", QUrl());
        std::unique_ptr<QObject> image(component.create());
        QVERIFY2(image, qPrintable(component.errorString()));
        image->setProperty("source", url);
        QTRY_COMPARE(image->property("status").toInt(), 1); // Image.Ready
        QCOMPARE(image->property("implicitWidth").toInt(), brulion::drawing::EmptyWidth);
    }

    void excalidrawShapesAreEditable()
    {
        // An Excalidraw drawing with a rectangle and text: edited, written back in its order.
        const QByteArray bytes = fixture(u"excalidraw_mixed.svg"_s);
        QCOMPARE(canvas->load(bytes), QString());
        QVERIFY(canvas->editable());
        QCOMPARE(strokeIds(canvas), (QStringList{u"Qk8_3vL1nYbXc0Rr2mT9a"_s, u"rect_00000000000000001"_s,
                                                 u"text_00000000000000001"_s}));
        mouseStroke(window.get(), {100, 100}, {300, 160}, 10);
        const std::optional<brulion::drawing::Drawing> written = brulion::drawing::readDrawing(canvas->svg());
        QVERIFY(written && brulion::drawing::editable(*written));
        QCOMPARE(written->strokes.size(), 4);
        QCOMPARE(written->strokes.at(1).type, brulion::drawing::ElementType::Rectangle);
        QCOMPARE(written->strokes.at(2).text, u"Zażółć gęślą jaźń"_s);
        // Untouched elements as Excalidraw wrote them (rough stays rough there).
        QCOMPARE(written->strokes.at(1).element, brulion::drawing::readDrawing(bytes)->strokes.at(1).element);
    }

    void excalidrawMarkdownLoadsAndSaves()
    {
        // A drawing of Obsidian's Excalidraw plugin: edited, written back as the plugin's file —
        // its Markdown around the scene as it was.
        const QByteArray bytes = fixture(u"obsidian_freedraw.excalidraw.md"_s);
        QSignalSpy loaded(canvas, &DrawingCanvas::loadedChanged);
        QCOMPARE(canvas->load(bytes), QString());
        QCOMPARE(loaded.size(), 1);
        QCOMPARE(canvas->format(), u"excalidrawMd"_s);
        QVERIFY(!canvas->legacy());
        QVERIFY(canvas->isKnownFile(bytes));
        QCOMPARE(canvas->strokeCount(), 2);
        const std::optional<brulion::drawing::ExcalidrawMarkdownRead> layout =
                brulion::drawing::readExcalidrawMarkdown(bytes);
        QVERIFY(layout);
        mouseStroke(window.get(), {100, 400}, {300, 460}, 10);
        QCOMPARE(canvas->strokeCount(), 3);
        const QByteArray written = canvas->fileBytes();
        QCOMPARE(written.left(layout->layout.sceneFrom), bytes.left(layout->layout.sceneFrom));
        QVERIFY(written.endsWith("\n```\n%%"));
        const std::optional<brulion::drawing::DrawingFile> back = brulion::drawing::readDrawingFile(written);
        QVERIFY(back);
        QCOMPARE(back->format, brulion::drawing::FileFormat::ExcalidrawMarkdown);
        QCOMPARE(back->drawing.strokes.size(), 3);
        const QString drawn = back->drawing.strokes.last().id;
        // (The SVG of it too: "export as SVG".)
        QVERIFY(brulion::drawing::looksLikeExcalidrawSvg(canvas->svg()));
        canvas->markSaved(written);
        QVERIFY(canvas->isKnownFile(written));
        QVERIFY(!canvas->modified());
        // Erased after that save: the next save marks it deleted (the plugin, showing the drawing,
        // removes it), the file's own deleted element kept.
        canvas->setTool(u"eraser"_s);
        mouseStroke(window.get(), {200, 380}, {200, 480}, 6);
        QCOMPARE(canvas->strokeCount(), 2);
        const std::optional<brulion::drawing::ExcalidrawMarkdownRead> erased =
                brulion::drawing::readExcalidrawMarkdown(canvas->fileBytes());
        QVERIFY(erased);
        QStringList buried;
        for (const QJsonValue &v : erased->layout.tombstones)
            buried.append(v.toObject().value(u"id"_s).toString());
        QCOMPARE(buried, (QStringList{u"deleted_stroke_0000001"_s, drawn}));
    }

    void excalidrawMarkdownTakesOtherDevice()
    {
        // The plugin's file changed in Obsidian while drawn on here: the drawings merged, written
        // into *their* file (their back of the card).
        const QByteArray mine = fixture(u"obsidian_freedraw.excalidraw.md"_s);
        QCOMPARE(canvas->load(mine), QString());
        std::optional<brulion::drawing::ExcalidrawMarkdownRead> read = brulion::drawing::readExcalidrawMarkdown(mine);
        QVERIFY(read);
        brulion::drawing::Drawing other = read->drawing;
        brulion::drawing::Stroke stroke;
        stroke.id = u"zObsidianu"_s;
        stroke.points = {{500, 100, 0.5}, {600, 140, 0.5}};
        other.strokes.append(stroke);
        QByteArray theirs = brulion::drawing::writeExcalidrawMarkdown(read->layout, other, 1761600000000);
        theirs.replace("Notatka z tyłu: Zażółć gęślą jaźń", "Notatka z tyłu: dopisana w Obsidianie");
        mouseStroke(window.get(), {100, 400}, {300, 460}, 10);
        QVERIFY(canvas->modified());
        QVERIFY(canvas->takeExternal(theirs));
        QCOMPARE(canvas->strokeCount(), 4);
        QVERIFY(canvas->modified());
        const QByteArray written = canvas->fileBytes();
        QVERIFY(written.contains("Notatka z tyłu: dopisana w Obsidianie"));
        const std::optional<brulion::drawing::DrawingFile> back = brulion::drawing::readDrawingFile(written);
        QVERIFY(back);
        QCOMPARE(back->drawing.strokes.size(), 4);
        QCOMPARE(back->drawing.strokes.at(2).id, u"zObsidianu"_s);
        // A legacy file is no version Brulion edits: refused, nothing changes.
        QVERIFY(!canvas->takeExternal(fixture(u"obsidian_legacy.excalidraw.md"_s)));
        QCOMPARE(canvas->strokeCount(), 4);
        QCOMPARE(canvas->format(), u"excalidrawMd"_s);
    }

    void excalidrawMarkdownTextsKeepTheirMarkdown()
    {
        // The plugin reads a text's "## Text Elements" entry (its Markdown, a link here) first.
        // Moved — in one save or across saves (markSaved) — the entry stays as it is; edited, the
        // new text is its entry and rawText, also in the saves after that one.
        QByteArray bytes = fixture(u"obsidian_mixed.excalidraw.md"_s);
        QVERIFY(bytes.contains("\nZażółć gęślą jaźń ^Zt8xQw1a\n"));
        bytes.replace("\nZażółć gęślą jaźń ^Zt8xQw1a\n", "\nZażółć [[gęślą]] jaźń ^Zt8xQw1a\n");
        QCOMPARE(canvas->load(bytes), QString());
        const std::optional<brulion::drawing::ExcalidrawMarkdownRead> read =
                brulion::drawing::readExcalidrawMarkdown(bytes);
        QVERIFY(read);
        const qsizetype sceneFrom = read->layout.sceneFrom;
        const auto layoutOf = [](const QByteArray &file) {
            const std::optional<brulion::drawing::ExcalidrawMarkdownRead> r =
                    brulion::drawing::readExcalidrawMarkdown(file);
            return r ? r->layout : brulion::drawing::ExcalidrawMarkdown();
        };
        const auto rawText = [&layoutOf](const QByteArray &file) {
            const brulion::drawing::ExcalidrawMarkdown layout = layoutOf(file);
            for (const QJsonValue &v : layout.elements) {
                if (v.toObject().value(u"id"_s).toString() == u"Zt8xQw1a")
                    return v.toObject().value(u"rawText"_s).toString();
            }
            return QString();
        };
        for (int save = 0; save < 2; ++save) {
            canvas->selectAll();
            canvas->nudgeSelection(10, 0);
            QVERIFY(canvas->modified());
            const QByteArray written = canvas->fileBytes();
            QCOMPARE(written.left(sceneFrom), bytes.left(sceneFrom));
            QCOMPARE(rawText(written), u"Zażółć gęślą jaźń"_s);
            canvas->markSaved(written);
        }
        canvas->clearSelection();
        QSignalSpy requests(canvas, &DrawingCanvas::textEditRequested);
        canvas->editText(u"Zt8xQw1a"_s);
        QCOMPARE(requests.size(), 1);
        canvas->commitText(requests.takeFirst().at(0).toMap(), u"Żółw"_s);
        QByteArray written = canvas->fileBytes();
        QVERIFY(written.contains("\n## Text Elements\nŻółw ^Zt8xQw1a\n"));
        QCOMPARE(rawText(written), u"Żółw"_s);
        canvas->markSaved(written);
        // The next save (a stroke drawn): the text's raw text as written, not the one first read.
        mouseStroke(window.get(), {100, 400}, {300, 460}, 10);
        written = canvas->fileBytes();
        QVERIFY(written.contains("\n## Text Elements\nŻółw ^Zt8xQw1a\n"));
        QCOMPARE(rawText(written), u"Żółw"_s);
    }

    void excalidrawMarkdownReadOnly_data()
    {
        QTest::addColumn<QByteArray>("bytes");
        QTest::addColumn<QString>("status");
        QTest::addColumn<bool>("legacy");
        QTest::newRow("the plugin's oldest form")
                << fixture(u"obsidian_legacy.excalidraw.md"_s) << u"readonly"_s << true;
        QTest::newRow("a frame") << fixture(u"obsidian_frame.excalidraw.md"_s) << u"readonly"_s << false;
        QTest::newRow("a note") << QByteArray("---\ntytul: Notatka\n---\n# Notatka\n\nTekst ąę.\n") << u"notDrawing"_s
                                << false;
    }
    void excalidrawMarkdownReadOnly()
    {
        QFETCH(QByteArray, bytes);
        QFETCH(QString, status);
        QFETCH(bool, legacy);
        QCOMPARE(canvas->load(bytes), status);
        QVERIFY(!canvas->editable());
        QCOMPARE(canvas->legacy(), legacy);
        QCOMPARE(canvas->format(), status == u"notDrawing"_s ? QString() : u"excalidrawMd"_s);
        QVERIFY(canvas->fileBytes().isEmpty());
        mouseStroke(window.get(), {100, 100}, {300, 160}, 6);
        QCOMPARE(canvas->strokeCount(), 0);
    }

    void excalidrawJsonIsEditable()
    {
        // A .excalidraw scene (excalidraw.com's "Save to disk"): written back as one, not as SVG.
        QCOMPARE(canvas->load(fixture(u"excalidraw_freedraw.excalidraw"_s)), QString());
        QCOMPARE(canvas->format(), u"excalidraw"_s);
        mouseStroke(window.get(), {100, 400}, {300, 460}, 10);
        const QByteArray written = canvas->fileBytes();
        QVERIFY(written.startsWith("{\n  \""));
        QVERIFY(written.endsWith("}\n"));
        const std::optional<brulion::drawing::DrawingFile> back = brulion::drawing::readDrawingFile(written);
        QVERIFY(back);
        QCOMPARE(back->format, brulion::drawing::FileFormat::Excalidraw);
        QCOMPARE(back->drawing.strokes.size(), 3);
        // A new drawing is an SVG.
        QCOMPARE(canvas->load({}), QString());
        QCOMPARE(canvas->format(), u"svg"_s);
        mouseStroke(window.get(), {100, 400}, {300, 460}, 10);
        QVERIFY(brulion::drawing::looksLikeExcalidrawSvg(canvas->fileBytes()));
    }

    void shapesWithMouse_data()
    {
        using brulion::drawing::ElementType;
        QTest::addColumn<QString>("tool");
        QTest::addColumn<QPoint>("to");
        QTest::addColumn<bool>("shift");
        QTest::addColumn<int>("type"); // -1: nothing made
        QTest::addColumn<QRectF>("box");
        QTest::newRow("rectangle") << u"rectangle"_s << QPoint(300, 200) << false << int(ElementType::Rectangle)
                                   << QRectF(100, 100, 200, 100);
        QTest::newRow("ellipse") << u"ellipse"_s << QPoint(300, 200) << false << int(ElementType::Ellipse)
                                 << QRectF(100, 100, 200, 100);
        QTest::newRow("diamond") << u"diamond"_s << QPoint(300, 200) << false << int(ElementType::Diamond)
                                 << QRectF(100, 100, 200, 100);
        QTest::newRow("line") << u"line"_s << QPoint(300, 150) << false << int(ElementType::Line)
                              << QRectF(100, 100, 200, 50);
        QTest::newRow("arrow") << u"arrow"_s << QPoint(300, 150) << false << int(ElementType::Arrow)
                               << QRectF(100, 100, 200, 50);
        QTest::newRow("Shift: a square") << u"rectangle"_s << QPoint(300, 180) << true << int(ElementType::Rectangle)
                                         << QRectF(100, 100, 200, 200);
        QTest::newRow("a click: nothing") << u"rectangle"_s << QPoint(101, 101) << false << -1 << QRectF();
    }
    void shapesWithMouse()
    {
        QFETCH(QString, tool);
        QFETCH(QPoint, to);
        QFETCH(bool, shift);
        QFETCH(int, type);
        QFETCH(QRectF, box);
        canvas->setTool(tool);
        QCOMPARE(canvas->tool(), tool);
        canvas->setColor(QColor(u"#2f6fdd"_s));
        canvas->setPenWidth(2);
        QTest::mousePress(window.get(), Qt::LeftButton, {}, QPoint(100, 100));
        for (int i = 1; i <= 8; ++i)
            QTest::mouseMove(window.get(), QPoint(100, 100) + (to - QPoint(100, 100)) * i / 8);
        if (type >= 0)
            QVERIFY(canvas->busy());
        QTest::mouseRelease(window.get(), Qt::LeftButton, shift ? Qt::ShiftModifier : Qt::NoModifier, to);
        QVERIFY(!canvas->busy());
        QCOMPARE(canvas->strokeCount(), type < 0 ? 0 : 1);
        if (type < 0)
            return;
        const brulion::drawing::Stroke s = canvas->currentDrawing().strokes.first();
        QCOMPARE(int(s.type), type);
        QCOMPARE(s.color, u"#2f6fdd"_s);
        QCOMPARE(s.width, 4.0); // the pen's width × 2
        QCOMPARE(s.roughness, 0);
        if (brulion::drawing::isLinear(s.type))
            QCOMPARE(brulion::drawing::pointsRect(s.points), box);
        else
            QCOMPARE(s.box, box);
        QCOMPARE(canvas->tool(), tool); // the tool stays
        QCOMPARE(canvas->selectionCount(), 0);
        QVERIFY(canvas->canUndo());
        canvas->undo();
        QCOMPARE(canvas->strokeCount(), 0);
        canvas->redo();
        QCOMPARE(canvas->strokeCount(), 1);
    }

    void shapesWithStylus()
    {
        canvas->setTool(u"ellipse"_s);
        Pen pen(window.get());
        pen.stroke({200, 200}, {320, 260}, 10, 0.3, 0.8);
        QCOMPARE(canvas->strokeCount(), 1);
        QCOMPARE(canvas->currentDrawing().strokes.first().type, brulion::drawing::ElementType::Ellipse);
        QCOMPARE(canvas->currentDrawing().strokes.first().box, QRectF(200, 200, 120, 60));
        // Its eraser end erases whatever the tool is.
        Pen eraser(window.get(), QPointingDevice::PointerType::Eraser);
        eraser.stroke({180, 230}, {220, 230}, 4, 0.5, 0.5);
        QCOMPARE(canvas->strokeCount(), 0);
    }

    void shapesWithFinger_data()
    {
        QTest::addColumn<bool>("fingerDraws");
        QTest::newRow("finger draws: a shape") << true;
        QTest::newRow("finger pans: no shape") << false;
    }
    void shapesWithFinger()
    {
        QFETCH(bool, fingerDraws);
        canvas->setFingerDraws(fingerDraws);
        canvas->setTool(u"rectangle"_s);
        const QPointF centre(canvas->centerX(), canvas->centerY());
        Fingers fingers(window.get());
        fingers.drag({200, 200}, {300, 280}, 8);
        QCOMPARE(canvas->strokeCount(), fingerDraws ? 1 : 0);
        QCOMPARE(QPointF(canvas->centerX(), canvas->centerY()) != centre, !fingerDraws);
        // A second finger while a shape is dragged: a pinch, no shape.
        canvas->setFingerDraws(true);
        fingers.send({{1, {QEventPoint::State::Pressed, {200, 400}}}});
        fingers.send({{1, {QEventPoint::State::Updated, {260, 440}}}});
        QVERIFY(canvas->busy());
        fingers.send({{1, {QEventPoint::State::Stationary, {260, 440}}}, {2, {QEventPoint::State::Pressed, {400, 440}}}});
        fingers.send({{1, {QEventPoint::State::Updated, {250, 440}}}, {2, {QEventPoint::State::Updated, {420, 440}}}});
        fingers.send({{1, {QEventPoint::State::Released, {250, 440}}}, {2, {QEventPoint::State::Released, {420, 440}}}});
        QCOMPARE(canvas->strokeCount(), fingerDraws ? 1 : 0);
    }

    void lassoSelectsAndMoves()
    {
        mouseStroke(window.get(), {100, 100}, {300, 100}, 10);
        mouseStroke(window.get(), {100, 200}, {300, 200}, 10);
        mouseStroke(window.get(), {100, 450}, {300, 450}, 10);
        const brulion::drawing::Drawing before = canvas->currentDrawing();
        canvas->setZoom(1);
        canvas->setTool(u"select"_s);
        QSignalSpy selection(canvas, &DrawingCanvas::selectionChanged);
        // The pen's lasso around the first two.
        Pen pen(window.get());
        penPath(pen, {{60, 60}, {360, 60}, {360, 260}, {60, 260}, {60, 70}});
        QCOMPARE(canvas->selectionCount(), 2);
        QVERIFY(!selection.isEmpty());
        QCOMPARE(canvas->selection(), (QStringList{before.strokes.at(0).id, before.strokes.at(1).id}));
        QVERIFY(canvas->selectionRect().contains(QPointF(200, 150)));
        QCOMPARE(canvas->strokeCount(), 3); // the lasso drew nothing
        // A drag inside the selection moves it: one undo step.
        mouseStroke(window.get(), {200, 150}, {260, 190}, 6);
        const brulion::drawing::Drawing moved = canvas->currentDrawing();
        for (int i = 0; i < 2; ++i) {
            QCOMPARE(moved.strokes.at(i).points.first().x, before.strokes.at(i).points.first().x + 60);
            QCOMPARE(moved.strokes.at(i).points.first().y, before.strokes.at(i).points.first().y + 40);
            QCOMPARE(moved.strokes.at(i).version, before.strokes.at(i).version + 1);
        }
        QCOMPARE(moved.strokes.at(2).points, before.strokes.at(2).points);
        QCOMPARE(canvas->selectionCount(), 2); // still selected
        canvas->undo();
        QCOMPARE(canvas->currentDrawing().strokes.at(0).points, before.strokes.at(0).points);
        QCOMPARE(canvas->selectionCount(), 2);
        canvas->redo();
        QCOMPARE(canvas->currentDrawing().strokes.at(1).points, moved.strokes.at(1).points);
        // Zoomed: the drag ÷ zoom.
        canvas->setZoom(2);
        const QRectF frame = canvas->selectionRect();
        const QPoint inside = frame.center().toPoint();
        const double x0 = canvas->currentDrawing().strokes.at(0).points.first().x;
        mouseStroke(window.get(), inside, inside + QPoint(40, 0), 4);
        QCOMPARE(canvas->currentDrawing().strokes.at(0).points.first().x, x0 + 20);
        // A tap on nothing clears the selection; a tap on a stroke selects it alone.
        canvas->setZoom(1);
        canvas->fitAll();
        canvas->setZoom(1);
        canvas->clearSelection();
        const QPointF third = canvas->toItem(QPointF(200, 450));
        QTest::mouseClick(window.get(), Qt::LeftButton, {}, third.toPoint());
        QCOMPARE(canvas->selection(), QStringList{before.strokes.at(2).id});
        QTest::mouseClick(window.get(), Qt::LeftButton, {}, QPoint(780, 580)); // nothing there
        QCOMPARE(canvas->selectionCount(), 0);
    }

    void selectionActions_data()
    {
        QTest::addColumn<QString>("action");
        QTest::newRow("delete") << u"delete"_s;
        QTest::newRow("duplicate") << u"duplicate"_s;
        QTest::newRow("recolour") << u"recolour"_s;
        QTest::newRow("to the front") << u"front"_s;
        QTest::newRow("to the back") << u"back"_s;
        QTest::newRow("nudge") << u"nudge"_s;
    }
    void selectionActions()
    {
        QFETCH(QString, action);
        QCOMPARE(canvas->load(shapesFile()), QString());
        canvas->setTool(u"select"_s);
        // A locked element is never selected.
        canvas->selectAll();
        QCOMPARE(canvas->selection(), (QStringList{u"prostokat"_s, u"elipsa"_s, u"napis"_s}));
        canvas->select({u"prostokat"_s});
        QCOMPARE(canvas->selectionCount(), 1);
        const QStringList before = strokeIds(canvas);
        QSignalSpy history(canvas, &DrawingCanvas::historyChanged);
        brulion::drawing::Drawing keep;
        if (action == u"delete") {
            canvas->deleteSelection();
            QCOMPARE(strokeIds(canvas), (QStringList{u"tlo"_s, u"elipsa"_s, u"napis"_s}));
            QCOMPARE(canvas->selectionCount(), 0);
        } else if (action == u"duplicate") {
            canvas->duplicateSelection();
            QCOMPARE(canvas->strokeCount(), 5);
            const QStringList copy = canvas->selection();
            QCOMPARE(copy.size(), 1);
            QVERIFY(copy.first() != u"prostokat"_s);
            QCOMPARE(elementOf(canvas, copy.first(), keep)->box, QRectF(110, 110, 200, 100));
            QCOMPARE(strokeIds(canvas).indexOf(copy.first()), 2); // right above the original
        } else if (action == u"recolour") {
            canvas->recolorSelection(QColor(u"#2f9e44"_s));
            QCOMPARE(elementOf(canvas, u"prostokat"_s, keep)->color, u"#2f9e44"_s);
            QCOMPARE(elementOf(canvas, u"elipsa"_s, keep)->color, u"#1f2430"_s);
        } else if (action == u"front") {
            canvas->reorderSelection(u"front"_s);
            QCOMPARE(strokeIds(canvas), (QStringList{u"tlo"_s, u"elipsa"_s, u"napis"_s, u"prostokat"_s}));
        } else if (action == u"back") {
            canvas->reorderSelection(u"front"_s);
            history.clear();
            canvas->reorderSelection(u"back"_s);
            // Above the locked backdrop.
            QCOMPARE(strokeIds(canvas), (QStringList{u"tlo"_s, u"prostokat"_s, u"elipsa"_s, u"napis"_s}));
        } else if (action == u"nudge") {
            canvas->nudgeSelection(1, 0);
            canvas->nudgeSelection(0, 10);
            QCOMPARE(elementOf(canvas, u"prostokat"_s, keep)->box, QRectF(101, 110, 200, 100));
        }
        QVERIFY(canvas->modified());
        QVERIFY(canvas->canUndo());
        QVERIFY(!history.isEmpty());
        // Each one undo step.
        canvas->undo();
        if (action == u"nudge")
            canvas->undo();
        if (action == u"back")
            canvas->undo();
        QCOMPARE(strokeIds(canvas), before);
        QCOMPARE(elementOf(canvas, u"prostokat"_s, keep)->box, QRectF(100, 100, 200, 100));
        QCOMPARE(elementOf(canvas, u"prostokat"_s, keep)->color, u"#e03131"_s);
        // Escape's way out: no gesture to cancel, the selection cleared.
        QVERIFY(!canvas->cancelGesture());
        canvas->clearSelection();
        QCOMPARE(canvas->selectionCount(), 0);
        // Another tool: no selection.
        canvas->select({u"elipsa"_s});
        canvas->setTool(u"pen"_s);
        QCOMPARE(canvas->selectionCount(), 0);
    }

    void textCreateEditDelete()
    {
        canvas->setTool(u"text"_s);
        canvas->setFontSize(28);
        canvas->setColor(QColor(u"#7048e8"_s));
        QSignalSpy requests(canvas, &DrawingCanvas::textEditRequested);
        QTest::mouseClick(window.get(), Qt::LeftButton, {}, QPoint(200, 200));
        QCOMPARE(requests.size(), 1);
        QVERIFY(canvas->textEditing());
        QVariantMap request = requests.takeFirst().at(0).toMap();
        QCOMPARE(request.value(u"id"_s).toString(), QString());
        QCOMPARE(request.value(u"fontSize"_s).toDouble(), 28.0);
        QCOMPARE(request.value(u"align"_s).toString(), u"left"_s);
        // While the text is written, a press on the paper only asks QML to commit it.
        QSignalSpy commits(canvas, &DrawingCanvas::textCommitRequested);
        QTest::mouseClick(window.get(), Qt::LeftButton, {}, QPoint(500, 500));
        QCOMPARE(commits.size(), 1);
        QCOMPARE(requests.size(), 0);
        canvas->commitText(request, u"Zażółć gęślą jaźń 🙂"_s);
        QVERIFY(!canvas->textEditing());
        QCOMPARE(canvas->strokeCount(), 1);
        brulion::drawing::Stroke t = canvas->currentDrawing().strokes.first();
        QCOMPARE(t.type, brulion::drawing::ElementType::Text);
        QCOMPARE(t.text, u"Zażółć gęślą jaźń 🙂"_s);
        QCOMPARE(t.color, u"#7048e8"_s);
        QCOMPARE(t.fontSize, 28.0);
        QVERIFY(t.box.width() > 100);
        QVERIFY(std::abs(t.box.height() - 28 * 1.15) < 0.01);
        QVERIFY(std::abs(t.box.top() + t.box.height() / 2 - 200) < 0.01); // its line's middle at the tap
        QVERIFY(canvas->canUndo());
        // A double click with the select tool: edited with its text.
        canvas->setTool(u"select"_s);
        const QPoint at = canvas->toItem(t.box.center()).toPoint();
        QTest::mouseClick(window.get(), Qt::LeftButton, {}, at);
        QTest::mouseClick(window.get(), Qt::LeftButton, {}, at);
        QCOMPARE(requests.size(), 1);
        request = requests.takeFirst().at(0).toMap();
        QCOMPARE(request.value(u"id"_s).toString(), t.id);
        QCOMPARE(request.value(u"text"_s).toString(), u"Zażółć gęślą jaźń 🙂"_s);
        canvas->commitText(request, u"Ćma"_s);
        QCOMPARE(canvas->currentDrawing().strokes.first().text, u"Ćma"_s);
        QVERIFY(canvas->currentDrawing().strokes.first().box.width() < t.box.width());
        // Emptied: deleted; one undo step brings it back.
        canvas->editText(t.id);
        QCOMPARE(requests.size(), 1);
        canvas->commitText(requests.takeFirst().at(0).toMap(), QString());
        QCOMPARE(canvas->strokeCount(), 0);
        canvas->undo();
        QCOMPARE(canvas->currentDrawing().strokes.first().text, u"Ćma"_s);
        // Cancelled: nothing changes.
        canvas->editText(t.id);
        QCOMPARE(requests.size(), 1);
        requests.clear();
        canvas->cancelText();
        QVERIFY(!canvas->textEditing());
        QCOMPARE(canvas->strokeCount(), 1);
        // A label on a rectangle: the text tool's tap inside it; centred in it.
        canvas->setTool(u"rectangle"_s);
        mouseStroke(window.get(), {100, 400}, {300, 500}, 6);
        canvas->setTool(u"text"_s);
        QTest::mouseClick(window.get(), Qt::LeftButton, {}, QPoint(150, 470));
        QCOMPARE(requests.size(), 1);
        request = requests.takeFirst().at(0).toMap();
        const QString rect = canvas->currentDrawing().strokes.last().id;
        QCOMPARE(request.value(u"containerId"_s).toString(), rect);
        QCOMPARE(request.value(u"align"_s).toString(), u"center"_s);
        canvas->commitText(request, u"Etykieta"_s);
        const brulion::drawing::Drawing d = canvas->currentDrawing();
        QCOMPARE(d.strokes.size(), 3);
        const brulion::drawing::Stroke &label = d.strokes.last();
        QCOMPARE(label.containerId, rect);
        QCOMPARE(d.strokes.at(1).boundIds, QStringList{label.id});
        QVERIFY(std::abs(label.box.center().x() - 200) < 0.6 && std::abs(label.box.center().y() - 450) < 0.6);
        // The label goes with its shape.
        canvas->setTool(u"select"_s);
        canvas->select({rect});
        QCOMPARE(canvas->selectionCount(), 2);
        canvas->deleteSelection();
        QCOMPARE(canvas->strokeCount(), 1);
    }

    void eraserErasesShapesNotLocked()
    {
        QCOMPARE(canvas->load(shapesFile()), QString());
        canvas->setZoom(1);
        canvas->setCenterX(400); // (scene = item coordinates)
        canvas->setCenterY(300);
        canvas->setTool(u"eraser"_s);
        // Across the rectangle's edge, the ellipse's inside (filled), the locked square.
        mouseStroke(window.get(), {80, 150}, {120, 150}, 4);
        mouseStroke(window.get(), {440, 150}, {460, 150}, 4);
        mouseStroke(window.get(), {50, 520}, {90, 540}, 4);
        // The inside of an unfilled rectangle: nothing.
        QCOMPARE(strokeIds(canvas), (QStringList{u"tlo"_s, u"napis"_s}));
        mouseStroke(window.get(), {150, 320}, {170, 320}, 3);
        QCOMPARE(strokeIds(canvas), QStringList{u"tlo"_s});
    }

    void paintsShapesAndText()
    {
        QCOMPARE(canvas->load(shapesFile()), QString());
        canvas->setZoom(1);
        canvas->setCenterX(400);
        canvas->setCenterY(300);
        QImage image = painted(canvas);
        // The rectangle's red outline, its white inside; the ellipse filled; the text's ink.
        QVERIFY2(near(image.pixelColor(200, 100), QColor(u"#e03131"_s), 30), qPrintable(image.pixelColor(200, 100).name()));
        QCOMPARE(image.pixelColor(200, 150).name(), u"#ffffff"_s);
        QVERIFY2(near(image.pixelColor(450, 150), QColor(u"#a5d8ff"_s), 3), qPrintable(image.pixelColor(450, 150).name()));
        QVERIFY(pixelsLike(image, QRect(100, 300, 160, 42), QColor(u"#1f2430"_s), 60) > 40);
        QVERIFY(pixelsLike(image, QRect(100, 260, 160, 30), QColor(u"#1f2430"_s), 60) == 0); // nothing above it
        // Dark: the colours as the dark theme shows them; the paper dark.
        canvas->setTheme(u"dark"_s);
        image = painted(canvas);
        QCOMPARE(image.pixelColor(200, 150).name(), u"#121212"_s);
        const QColor darkRed(brulion::drawing::darkColor(u"#e03131"));
        QVERIFY2(near(image.pixelColor(200, 100), darkRed, 30), qPrintable(image.pixelColor(200, 100).name()));
        QVERIFY(pixelsLike(image, QRect(100, 300, 160, 42), QColor(brulion::drawing::darkColor(u"#1f2430")), 60) > 40);
        // The previews draw the same.
        const QImage preview = brulion::drawingview::renderDrawing(canvas->currentDrawing(), QSize(800, 600), true,
                                                                   QRectF(0, 0, 800, 600));
        QVERIFY2(near(preview.pixelColor(200, 100), darkRed, 30), qPrintable(preview.pixelColor(200, 100).name()));
        // A shape being dragged shows before it ends.
        canvas->setTheme(u"light"_s);
        canvas->setTool(u"diamond"_s);
        QTest::mousePress(window.get(), Qt::LeftButton, {}, QPoint(560, 300));
        for (int i = 1; i <= 6; ++i)
            QTest::mouseMove(window.get(), QPoint(560 + 30 * i, 300 + 30 * i));
        image = painted(canvas);
        QVERIFY(pixelsLike(image, QRect(560, 380, 30, 20), QColor(u"#1f2430"_s), 80) > 0); // its left edge
        QTest::mouseRelease(window.get(), Qt::LeftButton, {}, QPoint(740, 480));
        QCOMPARE(canvas->strokeCount(), 5);
    }

    void svgOfShapesRendersInQtSvg()
    {
        QCOMPARE(canvas->load(shapesFile()), QString());
        const QByteArray svg = canvas->svg();
        QSvgRenderer renderer(svg);
        QVERIFY(renderer.isValid());
        const QRectF picture = brulion::drawing::pictureRect(canvas->currentDrawing());
        const QImage image = renderSvg(svg, picture.size().toSize());
        const QPoint outline = (QPointF(200, 100) - picture.topLeft()).toPoint();
        QVERIFY2(near(image.pixelColor(outline), QColor(u"#e03131"_s), 30), qPrintable(image.pixelColor(outline).name()));
        const QPoint fill = (QPointF(450, 150) - picture.topLeft()).toPoint();
        QVERIFY2(near(image.pixelColor(fill), QColor(u"#a5d8ff"_s), 3), qPrintable(image.pixelColor(fill).name()));
        // Text: some ink where it is (in whatever font Qt's SVG renderer finds).
        QVERIFY(darkPixels(image.copy(QRect((QPointF(100, 300) - picture.topLeft()).toPoint(), QSize(160, 42)))) > 20);
    }

    void externalMergeWithShapes()
    {
        // Here the rectangle moved right; there the ellipse moved down: both moves in the merge.
        const QByteArray base = shapesFile();
        QCOMPARE(canvas->load(base), QString());
        canvas->setTool(u"select"_s);
        canvas->select({u"prostokat"_s});
        canvas->nudgeSelection(50, 0);
        QVERIFY(canvas->modified());
        brulion::drawing::Drawing theirs = *brulion::drawing::readDrawing(base);
        brulion::drawing::EditStamp stamp;
        stamp.now = 1761600000000;
        brulion::drawing::moveElements(theirs, {u"elipsa"_s}, QPointF(0, 50), stamp);
        QVERIFY(canvas->takeExternal(brulion::drawing::toSvg(theirs)));
        brulion::drawing::Drawing keep;
        QCOMPARE(elementOf(canvas, u"prostokat"_s, keep)->box, QRectF(150, 100, 200, 100));
        QCOMPARE(elementOf(canvas, u"elipsa"_s, keep)->box, QRectF(400, 150, 100, 100));
        QCOMPARE(canvas->selection(), QStringList{u"prostokat"_s}); // still selected
        QVERIFY(canvas->modified());
        QVERIFY(!canvas->canUndo());
    }

    void textRendersInWorkerThread()
    {
        // The image provider draws in Qt Quick's image reader thread: text too.
        const std::optional<brulion::drawing::Drawing> d = brulion::drawing::readDrawing(shapesFile());
        QVERIFY(d);
        QImage image;
        std::unique_ptr<QThread> thread(QThread::create([&] {
            image = brulion::drawingview::renderDrawing(*d, QSize(800, 600), false, QRectF(0, 0, 800, 600));
        }));
        thread->start();
        QVERIFY(thread->wait(10000));
        QVERIFY(!image.isNull());
        QVERIFY(pixelsLike(image, QRect(100, 300, 160, 42), QColor(u"#1f2430"_s), 60) > 40);
    }

    void preparedFollowsTheElements()
    {
        using namespace brulion::drawing;
        const auto square = [](const QString &id, double x, int version, int nonce) {
            Stroke s;
            s.type = ElementType::Rectangle;
            s.id = id;
            s.box = QRectF(x, x, 10, 10);
            s.version = version;
            s.versionNonce = nonce;
            return s;
        };
        // Two elements of one id (a broken file) are each drawn where they are, also after an edit
        // of another element.
        Drawing twins;
        twins.strokes = {square(u"bliźniak"_s, 0, 1, 7), square(u"bliźniak"_s, 500, 1, 7), square(u"c"_s, 200, 1, 3)};
        QCOMPARE(canvas->load(toSvg(twins)), QString());
        QCOMPARE(canvas->strokeCount(), 3);
        QCOMPARE(canvas->contentBounds(), bounds(canvas->currentDrawing()));
        canvas->select({u"c"_s});
        canvas->nudgeSelection(5, 0);
        QCOMPARE(canvas->contentBounds(), bounds(canvas->currentDrawing()));
        QVERIFY(canvas->contentBounds().contains(QPointF(5, 5)));
        QVERIFY(canvas->contentBounds().contains(QPointF(505, 505)));
        // Another device's edit of the same version (another nonce) taken in: drawn where it is now.
        Drawing here;
        here.strokes = {square(u"x"_s, 0, 2, 11)};
        Drawing there;
        there.strokes = {square(u"x"_s, 300, 2, 22)};
        QCOMPARE(canvas->load(toSvg(here)), QString());
        QVERIFY(canvas->takeExternal(toSvg(there)));
        QCOMPARE(canvas->currentDrawing().strokes.first().box.topLeft(), QPointF(300, 300));
        QCOMPARE(canvas->contentBounds(), bounds(canvas->currentDrawing()));
        QVERIFY(!canvas->contentBounds().contains(QPointF(5, 5)));
        // …and hit where it is shown.
        canvas->setZoom(1);
        canvas->setCenterX(400);
        canvas->setCenterY(300);
        canvas->setTool(u"select"_s);
        QTest::mouseClick(window.get(), Qt::LeftButton, {}, canvas->toItem(QPointF(300, 305)).toPoint());
        QCOMPARE(canvas->selection(), QStringList{u"x"_s});
    }

    void unchangedTextIsNoEdit()
    {
        // Text from excalidraw.com opened in the editor and closed unchanged: nothing is changed or
        // wrapped again in Brulion's fonts, no undo step, nothing to save.
        using namespace brulion::drawing;
        Drawing d;
        Stroke t;
        t.type = ElementType::Text;
        t.id = u"napis"_s;
        t.box = QRectF(100, 100, 123.45, 25);
        t.text = u"Zażółć gęślą"_s;
        t.originalText = t.text;
        t.fontFamily = 5;
        t.version = 3;
        Stroke r;
        r.type = ElementType::Rectangle;
        r.id = u"ramka"_s;
        r.box = QRectF(100, 300, 80, 40);
        r.boundIds = {u"etykieta"_s};
        Stroke l = t;
        l.id = u"etykieta"_s;
        l.containerId = r.id;
        l.text = u"Ala ma kota\ni psa"_s; // (as Excalidraw wrapped it in Virgil)
        l.originalText = u"Ala ma kota i psa"_s;
        l.box = QRectF(105, 302, 70, 36);
        l.textAlign = TextAlign::Center;
        l.verticalAlign = VerticalAlign::Middle;
        d.strokes = {t, r, l};
        QCOMPARE(canvas->load(toSvg(d)), QString());
        const Drawing before = canvas->currentDrawing();
        QSignalSpy requests(canvas, &DrawingCanvas::textEditRequested);
        for (const QString &id : {u"napis"_s, u"etykieta"_s}) {
            canvas->editText(id);
            QCOMPARE(requests.size(), 1);
            const QVariantMap request = requests.takeFirst().at(0).toMap();
            canvas->commitText(request, request.value(u"text"_s).toString());
            QVERIFY(!canvas->textEditing());
        }
        QVERIFY(!canvas->modified());
        QVERIFY(!canvas->canUndo());
        const Drawing after = canvas->currentDrawing();
        for (qsizetype i = 0; i < before.strokes.size(); ++i) {
            QCOMPARE(after.strokes.at(i).version, before.strokes.at(i).version);
            QCOMPARE(after.strokes.at(i).box, before.strokes.at(i).box);
            QCOMPARE(after.strokes.at(i).text, before.strokes.at(i).text);
        }
        // A label's editor is as wide as the label wraps in its shape.
        canvas->editText(u"etykieta"_s);
        QCOMPARE(requests.size(), 1);
        const QVariantMap request = requests.takeFirst().at(0).toMap();
        QCOMPARE(request.value(u"width"_s).toDouble(), 70.0);
        QCOMPARE(request.value(u"text"_s).toString(), u"Ala ma kota i psa"_s);
        canvas->cancelText();
    }

    void lockedShapesKeepTheirLabels()
    {
        // A locked backdrop with a label: neither the eraser nor the text tool touches the label.
        using namespace brulion::drawing;
        Drawing d;
        Stroke r;
        r.type = ElementType::Rectangle;
        r.id = u"tlo"_s;
        r.box = QRectF(100, 100, 200, 100);
        r.locked = true;
        r.boundIds = {u"podpis"_s};
        Stroke l;
        l.type = ElementType::Text;
        l.id = u"podpis"_s;
        l.containerId = r.id;
        l.box = QRectF(170, 138, 60, 24);
        l.text = u"Tło"_s;
        l.originalText = l.text;
        l.textAlign = TextAlign::Center;
        l.verticalAlign = VerticalAlign::Middle;
        d.strokes = {r, l};
        QCOMPARE(canvas->load(toSvg(d)), QString());
        canvas->setZoom(1);
        canvas->setCenterX(400); // (scene = item coordinates)
        canvas->setCenterY(300);
        canvas->setTool(u"eraser"_s);
        mouseStroke(window.get(), {160, 150}, {240, 150}, 6);
        QCOMPARE(strokeIds(canvas), (QStringList{u"tlo"_s, u"podpis"_s}));
        QVERIFY(!canvas->modified());
        // The text tool on it: a new text there, not the label.
        QSignalSpy requests(canvas, &DrawingCanvas::textEditRequested);
        canvas->setTool(u"text"_s);
        QTest::mouseClick(window.get(), Qt::LeftButton, {}, QPoint(200, 150));
        QCOMPARE(requests.size(), 1);
        const QVariantMap request = requests.takeFirst().at(0).toMap();
        QCOMPARE(request.value(u"id"_s).toString(), QString());
        QCOMPARE(request.value(u"containerId"_s).toString(), QString());
        canvas->cancelText();
        // A double click with the select tool: nothing to edit, nothing selected.
        canvas->setTool(u"select"_s);
        QTest::mouseClick(window.get(), Qt::LeftButton, {}, QPoint(200, 150));
        QTest::mouseClick(window.get(), Qt::LeftButton, {}, QPoint(200, 150));
        QCOMPARE(requests.size(), 0);
        QCOMPARE(canvas->selectionCount(), 0);
        QVERIFY(!canvas->modified());
    }

    void hugeHatchedScenesStayBounded()
    {
        // A small file of many huge hair-thin cross-hatched squares (a hostile scene): the editor
        // and the preview prepare at most DrawingHatchLines hatch lines, quickly.
        using namespace brulion::drawing;
        Drawing d;
        for (int i = 0; i < 300; ++i) {
            Stroke s;
            s.type = ElementType::Rectangle;
            s.id = u"k%1"_s.arg(i);
            s.box = QRectF(0, 0, 55000, 55000);
            s.width = 0.05;
            s.fill = u"#ffc9c9"_s;
            s.fillStyle = FillStyle::CrossHatch;
            d.strokes.append(s);
        }
        const QByteArray file = toSvg(d);
        QVERIFY(file.size() < 16 * 1024 * 1024);
        QElapsedTimer t;
        t.start();
        QCOMPARE(canvas->load(file), QString());
        QImage image(800, 600, QImage::Format_ARGB32_Premultiplied);
        QPainter p(&image);
        canvas->render(&p, QSizeF(800, 600));
        p.end();
        const qint64 editorMs = t.elapsed();
        t.restart();
        const QImage preview = brulion::drawingview::renderDrawing(canvas->currentDrawing(), QSize(400, 400), false);
        const qint64 previewMs = t.elapsed();
        QVERIFY(!preview.isNull());
        qInfo("300 huge hatched squares: editor %lld ms, preview %lld ms", editorMs, previewMs);
        QVERIFY(editorMs < 15000 * BRULION_TEST_SLOWDOWN);
        QVERIFY(previewMs < 15000 * BRULION_TEST_SLOWDOWN);
        // The squares beyond the lines' budget are filled: the fill shows.
        QVERIFY(pixelsLike(image, QRect(300, 200, 200, 200), QColor(u"#ffc9c9"_s), 10) > 1000);
    }

    void shapesSample()
    {
        // A diagram drawn with the editor's own tools as a user draws it: shapes, a line, an
        // arrow, free text, labels in a rectangle and an ellipse, a pen stroke, a duplicate, a
        // recoloured and a moved shape. `BRULION_DRAWING_SHAPES_SAMPLE=<file>` writes it there
        // (tests/app/drawing/excalidraw-roundtrip.mjs takes it to excalidraw.com and back).
        canvas->setZoom(1);
        canvas->setCenterX(400); // (scene = item coordinates)
        canvas->setCenterY(300);
        QSignalSpy requests(canvas, &DrawingCanvas::textEditRequested);
        const auto write = [&](QPoint at, const QString &text) {
            canvas->setTool(u"text"_s);
            QTest::mouseClick(window.get(), Qt::LeftButton, {}, at);
            QVERIFY(requests.size() == 1);
            canvas->commitText(requests.takeFirst().at(0).toMap(), text);
        };
        const QList<std::tuple<QString, QPoint, QPoint>> shapes{
                {u"rectangle"_s, {60, 60}, {260, 160}}, {u"ellipse"_s, {320, 60}, {500, 170}},
                {u"diamond"_s, {560, 50}, {700, 190}},  {u"line"_s, {60, 240}, {260, 300}},
                {u"arrow"_s, {260, 110}, {320, 115}},   {u"arrow"_s, {320, 250}, {560, 330}}};
        for (const auto &[tool, from, to] : shapes) {
            canvas->setTool(tool);
            mouseStroke(window.get(), from, to, 8);
        }
        write(QPoint(160, 110), u"Zażółć gęślą jaźń"_s);
        write(QPoint(410, 115), u"Elipsa z dłuższym napisem"_s);
        write(QPoint(60, 400), u"Notatka: ąćęłńóśźż 🙂\nDruga linia"_s);
        canvas->setTool(u"pen"_s);
        mouseStroke(window.get(), {60, 480}, {400, 520}, 12);
        const auto idOf = [this](brulion::drawing::ElementType type) {
            for (const auto &e : canvas->currentDrawing().strokes) {
                if (e.type == type)
                    return e.id;
            }
            return QString();
        };
        canvas->select({idOf(brulion::drawing::ElementType::Diamond)});
        canvas->duplicateSelection();
        canvas->select({idOf(brulion::drawing::ElementType::Ellipse)}); // (with its label)
        canvas->recolorSelection(QColor(u"#2f6fdd"_s));
        canvas->select({idOf(brulion::drawing::ElementType::Line)});
        canvas->nudgeSelection(0, 20);
        canvas->clearSelection();
        const QByteArray svg = canvas->svg();
        // 6 shapes, 2 labels, a text, a pen stroke, a copy.
        QCOMPARE(canvas->strokeCount(), 11);
        QCOMPARE(canvas->load(svg), QString());
        QCOMPARE(canvas->strokeCount(), 11);
        if (const QByteArray out = qgetenv("BRULION_DRAWING_SHAPES_SAMPLE"); !out.isEmpty()) {
            QFile f(QString::fromLocal8Bit(out));
            QVERIFY(f.open(QIODevice::WriteOnly));
            f.write(svg);
        }
    }

    void excalidrawsVersionIsEditable()
    {
        // `BRULION_DRAWING_CHECK=<file>`: a scene as excalidraw.com keeps it (exported by
        // excalidraw-roundtrip.mjs) opens editable, and its file written again opens too.
        const QByteArray in = qgetenv("BRULION_DRAWING_CHECK");
        if (in.isEmpty())
            QSKIP("BRULION_DRAWING_CHECK not set");
        QFile f(QString::fromLocal8Bit(in));
        QVERIFY(f.open(QIODevice::ReadOnly));
        QCOMPARE(canvas->load(f.readAll()), QString());
        const int n = canvas->strokeCount();
        QVERIFY(n > 0);
        canvas->select({canvas->currentDrawing().strokes.first().id});
        canvas->nudgeSelection(5, 5); // (an edit: the scene written from the model too)
        const QByteArray again = canvas->svg();
        QCOMPARE(canvas->load(again), QString());
        QCOMPARE(canvas->strokeCount(), n);
        if (const QByteArray out = qgetenv("BRULION_DRAWING_CHECK_OUT"); !out.isEmpty()) {
            QFile o(QString::fromLocal8Bit(out));
            QVERIFY(o.open(QIODevice::WriteOnly));
            o.write(again);
        }
        qInfo("%d elements editable", n);
    }

    void manyShapesRenderQuickly()
    {
        using namespace brulion::drawing;
        Drawing d;
        for (int i = 0; i < 600; ++i) {
            Stroke s;
            s.id = u"k%1"_s.arg(i);
            const double x = 20 + (i % 20) * 38.0, y = 20 + (i / 20) * 30.0;
            switch (i % 4) {
            case 0: s.type = ElementType::Rectangle; break;
            case 1:
                s.type = ElementType::Ellipse;
                s.fill = u"#ffec99"_s;
                s.fillStyle = FillStyle::Hachure;
                break;
            case 2: s.type = ElementType::Arrow; break;
            default:
                s.type = ElementType::Text;
                s.text = u"Ąę"_s;
                break;
            }
            s.box = QRectF(x, y, 30, 22);
            s.points = {{x, y, 0.5}, {x + 30, y + 22, 0.5}};
            if (s.type == ElementType::Arrow)
                s.endArrowhead = Arrowhead::Arrow;
            else if (s.type != ElementType::Text)
                s.points.clear();
            d.strokes.append(s);
        }
        QElapsedTimer t;
        t.start();
        QCOMPARE(canvas->load(toSvg(d)), QString());
        const qint64 loadMs = t.elapsed();
        QImage image(800, 600, QImage::Format_ARGB32_Premultiplied);
        QPainter p(&image);
        canvas->render(&p, QSizeF(800, 600));
        qInfo("600 shapes: load %lld ms, render %.1f ms", loadMs, canvas->lastRenderMs());
        QVERIFY(canvas->lastRenderMs() < 2000.0 * BRULION_TEST_SLOWDOWN);
    }

    // --- Backdrops (core drawing/backdrop.h, backdropimage.h, backdrops.h) --------------------

    void backdropUnderInk()
    {
        QVERIFY(canvas->setBackdrop(backdropOf(QColor(u"#ff0000"_s))));
        QVERIFY(canvas->hasBackdrop());
        QCOMPARE(canvas->backdropPath(), u"Załączniki/slajd.png"_s);
        QCOMPARE(canvas->backdropPage(), 0);
        // 400 × 300 px: 1200 × 900 scene units from the origin (the view's top left at zoom 1).
        QCOMPARE(canvas->contentBounds(), QRectF(0, 0, 1200, 900));
        canvas->setColor(QColor(u"#1f2430"_s));
        canvas->setPenWidth(4);
        mouseStroke(window.get(), {100, 100}, {300, 100}, 20);
        QCOMPARE(canvas->strokeCount(), 2);
        QCOMPARE(canvas->currentDrawing().strokes.first().type, brulion::drawing::ElementType::Image);
        const QImage image = painted(canvas);
        QVERIFY2(image.pixelColor(200, 100).lightness() < 80, qPrintable(image.pixelColor(200, 100).name()));
        QVERIFY2(near(image.pixelColor(200, 200), QColor(u"#ff0000"_s), 12), qPrintable(image.pixelColor(200, 200).name()));
        // Beyond the backdrop: the paper.
        canvas->setCenterX(1400);
        QVERIFY(near(painted(canvas).pixelColor(780, 300), Qt::white));
    }

    void backdropIsLocked()
    {
        QVERIFY(canvas->setBackdrop(backdropOf(QColor(u"#2f9e44"_s))));
        mouseStroke(window.get(), {100, 400}, {300, 400}, 12);
        QCOMPARE(canvas->strokeCount(), 2);
        const QString backdrop = canvas->currentDrawing().strokes.first().id;
        const QString stroke = strokeIds(canvas).last();
        // The eraser across it (away from the stroke) takes nothing.
        canvas->setTool(u"eraser"_s);
        mouseStroke(window.get(), {100, 150}, {500, 200}, 12);
        QCOMPARE(canvas->strokeCount(), 2);
        // All of it in view (1200 × 900 at zoom 0.5: 100,75–700,525), a lasso around everything:
        // only the stroke.
        canvas->setZoom(0.5);
        canvas->setCenterX(600);
        canvas->setCenterY(450);
        canvas->setTool(u"select"_s);
        Pen pen(window.get());
        penPath(pen, {{20, 20}, {780, 20}, {780, 580}, {20, 580}, {20, 30}});
        QCOMPARE(canvas->selection(), QStringList{stroke});
        canvas->clearSelection();
        // A tap on it (scene 1000, 250) selects nothing; select all takes the stroke only.
        QTest::mouseClick(window.get(), Qt::LeftButton, {}, QPoint(600, 200));
        QCOMPARE(canvas->selectionCount(), 0);
        canvas->selectAll();
        QCOMPARE(canvas->selection(), QStringList{stroke});
        // To the back: above it. Moved with the arrows: it stays.
        canvas->reorderSelection(u"back"_s);
        QCOMPARE(strokeIds(canvas).first(), backdrop);
        canvas->nudgeSelection(10, 0);
        QCOMPARE(canvas->currentDrawing().strokes.first().box.topLeft(), QPointF(0, 0));
    }

    void backdropDarkInverted()
    {
        QVERIFY(canvas->setBackdrop(backdropOf(Qt::white)));
        QVERIFY(near(painted(canvas).pixelColor(300, 300), Qt::white));
        canvas->setTheme(u"dark"_s);
        QVERIFY(canvas->dark());
        const QColor dark = painted(canvas).pixelColor(300, 300);
        QVERIFY2(near(dark, QColor(u"#121212"_s), 2), qPrintable(dark.name()));
        // "auto" follows the app.
        canvas->setTheme(u"auto"_s);
        canvas->setAppDark(true);
        QVERIFY(near(painted(canvas).pixelColor(300, 300), QColor(u"#121212"_s), 2));
        canvas->setAppDark(false);
        QVERIFY(near(painted(canvas).pixelColor(300, 300), Qt::white));
        // A Dark drawing's file shows the backdrop dark in other programs; its scene keeps the
        // original raster.
        canvas->setTheme(u"dark"_s);
        const QByteArray svg = canvas->svg();
        const QImage picture = renderSvg(svg, QSize(1232, 932));
        QVERIFY2(near(picture.pixelColor(300, 300), QColor(u"#121212"_s), 3), qPrintable(picture.pixelColor(300, 300).name()));
        const std::optional<brulion::drawing::Drawing> back = brulion::drawing::readDrawing(svg);
        QVERIFY(back);
        const QString fileId = back->strokes.first().fileId;
        QCOMPARE(back->files.value(fileId).toObject().value(u"dataURL"_s).toString(),
                 canvas->currentDrawing().files.value(fileId).toObject().value(u"dataURL"_s).toString());
        QCOMPARE(back->theme, brulion::drawing::PaperTheme::Dark);
        // The light picture of a Light drawing.
        canvas->setTheme(u"light"_s);
        QVERIFY(near(renderSvg(canvas->svg(), QSize(1232, 932)).pixelColor(300, 300), Qt::white));
    }

    void backdropPreviewDark()
    {
        // The previews (DrawingImageProvider) show a backdrop dark as the paper does.
        QVERIFY(canvas->setBackdrop(backdropOf(Qt::white, QSize(800, 600))));
        const QByteArray file = canvas->fileBytes(); // Auto: no style sheet, the light picture
        QVERIFY(!file.contains("prefers-color-scheme"));
        QSize natural;
        const QImage light = brulion::drawingview::DrawingImageProvider::picture(file, false, QSize(400, 0), &natural);
        const QImage dark = brulion::drawingview::DrawingImageProvider::picture(file, true, QSize(400, 0));
        QCOMPARE(natural, QSize(1232, 932));
        QVERIFY(near(light.pixelColor(200, 150), Qt::white));
        QVERIFY2(near(dark.pixelColor(200, 150), QColor(u"#121212"_s), 3), qPrintable(dark.pixelColor(200, 150).name()));
    }

    void setRemoveBackdropUndo()
    {
        QSignalSpy changed(canvas, &DrawingCanvas::contentChanged);
        QVERIFY(canvas->setBackdrop(backdropOf(QColor(u"#ff0000"_s), QSize(400, 300), u"Załączniki/Fizyka – wykład 3.pdf"_s, 3)));
        QCOMPARE(changed.size(), 1);
        QVERIFY(canvas->modified());
        QCOMPARE(canvas->backdropPage(), 3);
        const brulion::drawing::Stroke first = canvas->currentDrawing().strokes.first();
        canvas->undo();
        QVERIFY(!canvas->hasBackdrop());
        QCOMPARE(canvas->strokeCount(), 0);
        canvas->redo();
        QVERIFY(canvas->hasBackdrop());
        // Replaced: the same element, a new version and file; undo brings the old one back.
        QVERIFY(canvas->setBackdrop(backdropOf(QColor(u"#2f6fdd"_s), QSize(300, 400), u"Załączniki/Fizyka – wykład 3.pdf"_s, 4)));
        const brulion::drawing::Stroke second = canvas->currentDrawing().strokes.first();
        QCOMPARE(canvas->strokeCount(), 1);
        QCOMPARE(second.id, first.id);
        QCOMPARE(second.version, first.version + 1);
        QVERIFY(second.fileId != first.fileId);
        QCOMPARE(second.box, QRectF(0, 0, 900, 1200));
        canvas->undo();
        QCOMPARE(canvas->currentDrawing().strokes.first().fileId, first.fileId);
        QCOMPARE(canvas->backdropPage(), 3);
        canvas->redo();
        // Removed in one step; undo restores it (its file was kept).
        canvas->removeBackdrop();
        QVERIFY(!canvas->hasBackdrop());
        canvas->undo();
        QCOMPARE(canvas->currentDrawing().strokes.first().fileId, second.fileId);
        // Written and read back.
        const QByteArray svg = canvas->svg();
        QCOMPARE(canvas->load(svg), QString());
        QVERIFY(canvas->hasBackdrop());
        QCOMPARE(canvas->backdropPath(), u"Załączniki/Fizyka – wykład 3.pdf"_s);
        QCOMPARE(canvas->backdropPage(), 4);
        // Bad data changes nothing.
        QVERIFY(!canvas->setBackdrop({}));
        QVariantMap broken = backdropOf(Qt::red);
        broken.insert(u"mimeType"_s, u"text/html"_s);
        QVERIFY(!canvas->setBackdrop(broken));
        QVERIFY(!canvas->modified());
    }

    void backdropMerge()
    {
        // A backdrop set here while another device added a stroke: both kept, the backdrop at the
        // bottom.
        QCOMPARE(canvas->load(strokesFile({u"a"_s})), QString());
        QVERIFY(canvas->setBackdrop(backdropOf(QColor(u"#ff0000"_s))));
        QVERIFY(canvas->takeExternal(strokesFile({u"a"_s, u"c"_s})));
        QVERIFY(canvas->hasBackdrop());
        QCOMPARE(canvas->strokeCount(), 3);
        QCOMPARE(strokeIds(canvas).mid(1), (QStringList{u"a"_s, u"c"_s}));
        QVERIFY(canvas->modified());
        // Their removal of the backdrop (a file without it) wins only when nothing changed here.
        canvas->markSaved(canvas->fileBytes());
        QVERIFY(canvas->takeExternal(strokesFile({u"a"_s, u"c"_s})));
        QVERIFY(!canvas->hasBackdrop());
    }

    void svgPictureShowsBackdrop()
    {
        QVERIFY(canvas->setBackdrop(backdropOf(QColor(u"#ff0000"_s))));
        mouseStroke(window.get(), {100, 100}, {300, 100}, 10);
        const QByteArray svg = canvas->svg();
        QVERIFY(QSvgRenderer(svg).isValid());
        // The picture: the margin, then the slide; self-contained (a data URL).
        const QImage picture = renderSvg(svg, QSize(1232, 932));
        QVERIFY2(near(picture.pixelColor(600, 600), QColor(u"#ff0000"_s), 12), qPrintable(picture.pixelColor(600, 600).name()));
        QVERIFY(svg.contains("xlink:href=\"data:image/"));
    }

    void prepareBackdropPicture_data()
    {
        QTest::addColumn<QString>("kind");
        QTest::addColumn<QSize>("pixels");
        QTest::addColumn<QString>("mime");
        QTest::addColumn<int>("sameBytes"); // 1 kept as they are, 0 encoded again, -1 either
        QTest::addColumn<QString>("error");
        QTest::newRow("PNG with alpha: kept") << u"alpha.png"_s << QSize(64, 64) << u"image/png"_s << 1 << QString();
        QTest::newRow("small JPEG: kept") << u"small.jpg"_s << QSize(320, 200) << u"image/jpeg"_s << 1 << QString();
        QTest::newRow("a large photo: shrunk") << u"photo.jpg"_s << QSize(2400, 1800) << QString() << 0 << QString();
        QTest::newRow("an SVG: drawn 2400 px") << u"wykres.svg"_s << QSize(2400, 1200) << QString() << 0 << QString();
        // (Turned by its EXIF orientation — or by the writer itself where JPEG has none.)
        QTest::newRow("EXIF-turned JPEG: upright") << u"obrócone.jpg"_s << QSize(100, 200) << QString() << -1
                                                   << QString();
        QTest::newRow("BMP: encoded again") << u"kwadrat.bmp"_s << QSize(50, 50) << QString() << 0 << QString();
        QTest::newRow("10 random bytes") << u"szum.png"_s << QSize() << QString() << 0 << u"format"_s;
        QTest::newRow("HEIC") << u"zdjęcie.heic"_s << QSize() << QString() << 0 << u"format"_s;
        QTest::newRow("empty file") << u"pusty.png"_s << QSize() << QString() << 0 << u"format"_s;
        QTest::newRow("missing file") << u"nie ma.png"_s << QSize() << QString() << 0 << u"unreadable"_s;
    }

    void prepareBackdropPicture()
    {
        using namespace brulion::drawingview;
        QFETCH(QString, kind);
        QFETCH(QSize, pixels);
        QFETCH(QString, mime);
        QFETCH(int, sameBytes);
        QFETCH(QString, error);
        QTemporaryDir dir;
        const QString file = dir.filePath(kind);
        if (kind == u"alpha.png") {
            QImage image(64, 64, QImage::Format_ARGB32);
            image.fill(Qt::transparent);
            QPainter(&image).fillRect(10, 10, 20, 20, Qt::red);
            QVERIFY(image.save(file, "PNG"));
        } else if (kind == u"small.jpg") {
            QImage image(320, 200, QImage::Format_RGB32);
            image.fill(QColor(u"#74c0fc"_s));
            QVERIFY(image.save(file, "JPEG", 80));
        } else if (kind == u"photo.jpg") {
            // A photo's size, 4000 × 3000: gradients.
            QImage image(4000, 3000, QImage::Format_RGB32);
            for (int y = 0; y < image.height(); ++y) {
                auto *line = reinterpret_cast<QRgb *>(image.scanLine(y));
                for (int x = 0; x < image.width(); ++x)
                    line[x] = qRgb((x / 16) % 256, (y / 12) % 256, (x + y) / 28 % 256);
            }
            QVERIFY(image.save(file, "JPEG", 95));
        } else if (kind == u"wykres.svg") {
            QFile f(file);
            QVERIFY(f.open(QIODevice::WriteOnly));
            f.write(R"(<svg xmlns="http://www.w3.org/2000/svg" width="100" height="50"><rect width="100" height="50" fill="#e03131"/></svg>)");
        } else if (kind == u"obrócone.jpg") {
            QImage image(200, 100, QImage::Format_RGB32);
            image.fill(Qt::green);
            QImageWriter writer(file, "jpeg");
            writer.setTransformation(QImageIOHandler::TransformationRotate90);
            QVERIFY(writer.write(image));
        } else if (kind == u"kwadrat.bmp") {
            QImage image(50, 50, QImage::Format_RGB32);
            image.fill(Qt::blue);
            QVERIFY(image.save(file, "BMP"));
        } else if (kind == u"szum.png" || kind == u"zdjęcie.heic") {
            QFile f(file);
            QVERIFY(f.open(QIODevice::WriteOnly));
            f.write("\x13\x37\xfe\x00\x11\x22\x33\x44\x55\x66", 10);
        } else if (kind == u"pusty.png") {
            QFile f(file);
            QVERIFY(f.open(QIODevice::WriteOnly));
        }
        const QString vaultPath = u"Załączniki/"_s + kind;
        const PreparedBackdrop p = prepareBackdropPicture(file, vaultPath);
        QCOMPARE(p.error, error);
        QCOMPARE(p.source.path, vaultPath);
        QCOMPARE(p.source.page, 0);
        QCOMPARE(p.name, QFileInfo(kind).completeBaseName());
        if (!error.isEmpty())
            return;
        QCOMPARE(p.image.pixels, pixels);
        if (!mime.isEmpty())
            QCOMPARE(p.image.mimeType, mime);
        QVERIFY(p.image.bytes.size() <= brulion::drawing::MaxBackdropBytes);
        QVERIFY(brulion::drawing::validBackdropImage(p.image));
        QFile f(file);
        QVERIFY(f.open(QIODevice::ReadOnly));
        if (sameBytes >= 0)
            QCOMPARE(p.image.bytes == f.readAll(), sameBytes == 1);
        // What is stored decodes to the size stated.
        QCOMPARE(QImage::fromData(p.image.bytes).size(), pixels);
        // Round trip through QML's map.
        const PreparedBackdrop back = fromVariant(toVariant(p));
        QCOMPARE(back.image.bytes, p.image.bytes);
        QCOMPARE(back.image.pixels, p.image.pixels);
        QCOMPARE(back.source, p.source);
    }

    void encodeBackdropStaysWithinTheLimit()
    {
        // Noise compresses badly: shrunk until it fits (never below BackdropMinPixels), as JPEG.
        QImage noise(2400, 2400, QImage::Format_RGB32);
        for (int y = 0; y < noise.height(); ++y) {
            auto *line = reinterpret_cast<QRgb *>(noise.scanLine(y));
            for (int x = 0; x < noise.width(); ++x)
                line[x] = QRandomGenerator::global()->generate() | 0xff000000;
        }
        const brulion::drawing::BackdropImage out = brulion::drawingview::encodeBackdrop(noise);
        QVERIFY(!out.bytes.isEmpty());
        QVERIFY(out.bytes.size() <= brulion::drawing::MaxBackdropBytes);
        QVERIFY(out.pixels.width() >= brulion::drawing::BackdropMinPixels);
        QCOMPARE(out.mimeType, u"image/jpeg"_s);
        QVERIFY(brulion::drawingview::encodeBackdrop(QImage()).bytes.isEmpty());
    }

    void prepareBackdropPdfPage()
    {
        using namespace brulion::drawingview;
        QTemporaryDir dir;
        const QString pdf = dir.filePath(u"Wykład ćwiczenia.pdf"_s);
        QVERIFY(writeTwoPagePdf(pdf));
        const QString vaultPath = u"Załączniki/Wykład ćwiczenia.pdf"_s;
        if (!brulion::platform::pdfAvailable()) {
            QCOMPARE(prepareBackdropPdfPage(pdf, vaultPath, 1).error, u"noPdf"_s);
            QSKIP("no PDF renderer in this build");
        }
        const PreparedBackdrop p = prepareBackdropPdfPage(pdf, vaultPath, 2);
        QCOMPARE(p.error, QString());
        QCOMPARE(p.image.pixels, QSize(2400, 1697));
        QVERIFY(p.image.mimeType == u"image/png" || p.image.mimeType == u"image/jpeg");
        QCOMPARE(p.source, (brulion::drawing::BackdropSource{vaultPath, 2}));
        QCOMPARE(p.name, u"Wykład ćwiczenia"_s);
        // The red square of page 2 (40–60 mm across, 30–50 mm down of 297 × 210 mm).
        const QImage image = QImage::fromData(p.image.bytes);
        const QColor centre = image.pixelColor(int(2400 * 50 / 297.0), int(1697 * 40 / 210.0));
        QVERIFY2(centre.red() > 200 && centre.green() < 60, qPrintable(centre.name()));
        QCOMPARE(prepareBackdropPdfPage(pdf, vaultPath, 3).error, u"pdf"_s);
        QCOMPARE(prepareBackdropPdfPage(dir.filePath(u"nie ma.pdf"_s), vaultPath, 1).error, u"unreadable"_s);
        // The page picker's thumbnail (image://pdfpage/…).
        PdfPageImageProvider provider;
        const QUrl url = PdfPageImageProvider::url(pdf, 1, u"1"_s);
        QSize size;
        const QImage thumbnail = provider.requestImage(imageId(url), &size, QSize(120, 0));
        QCOMPARE(thumbnail.height(), 120);
        QCOMPARE(size, thumbnail.size());
        // Only absolute paths of PDFs.
        QVERIFY(provider.requestImage(u"Wyk%C5%82ad.pdf?page=1"_s, &size, QSize()).isNull());
        QVERIFY(provider.requestImage(imageId(PdfPageImageProvider::url(dir.filePath(u"../x.pdf"_s), 1, u"1"_s)), &size,
                                      QSize())
                        .isNull());
        QVERIFY(provider.requestImage(imageId(PdfPageImageProvider::url(dir.filePath(u"a.svg"_s), 1, u"1"_s)), &size,
                                      QSize())
                        .isNull());
        QVERIFY(provider.requestImage(imageId(PdfPageImageProvider::url(pdf, 0, u"1"_s)), &size, QSize()).isNull());
    }

    void backdropsJobs()
    {
        // DrawingBackdrops: vault paths through the resolver, a job per file, cancelled between pages.
        using namespace brulion::drawingview;
        QTemporaryDir dir;
        QImage image(120, 90, QImage::Format_RGB32);
        image.fill(Qt::yellow);
        QVERIFY(QDir(dir.path()).mkpath(u"Załączniki"_s));
        QVERIFY(image.save(dir.filePath(u"Załączniki/żółty.png"_s)));
        QVERIFY(writeTwoPagePdf(dir.filePath(u"Załączniki/wykład.pdf"_s)));
        DrawingBackdrops::setPathResolver([&dir](const QString &path) { return dir.filePath(path); });
        DrawingBackdrops backdrops;
        QVERIFY(backdrops.isPdf(u"a/B.PDF"_s));
        QVERIFY(!backdrops.isPdf(u"a/b.png"_s));
        QSignalSpy finished(&backdrops, &DrawingBackdrops::finished);
        QSignalSpy progress(&backdrops, &DrawingBackdrops::progress);
        const int job = backdrops.prepare(u"Załączniki/żółty.png"_s, {});
        QVERIFY(job > 0);
        QVERIFY(finished.wait(10000 * BRULION_TEST_SLOWDOWN));
        QCOMPARE(finished.first().at(0).toInt(), job);
        const QVariantList items = finished.first().at(1).toList();
        QCOMPARE(items.size(), 1);
        QCOMPARE(items.first().toMap().value(u"error"_s).toString(), QString());
        QCOMPARE(items.first().toMap().value(u"width"_s).toInt(), 120);
        QCOMPARE(items.first().toMap().value(u"name"_s).toString(), u"żółty"_s);
        QCOMPARE(progress.size(), 1);
        // A file that is not there: the item says why.
        finished.clear();
        backdrops.prepare(u"Załączniki/nie ma.png"_s, {});
        QVERIFY(finished.wait(10000 * BRULION_TEST_SLOWDOWN));
        QCOMPARE(finished.first().at(1).toList().first().toMap().value(u"error"_s).toString(), u"unreadable"_s);
        QVERIFY(!backdrops.errorText(u"unreadable"_s).isEmpty());
        // Pages typed and shown.
        QCOMPARE(backdrops.parsePages(u"3–1, 7"_s, 10), (QList<int>{1, 2, 3, 7}));
        QCOMPARE(backdrops.formatPages({1, 2, 3, 7}), u"1-3, 7"_s);
        if (backdrops.pdfAvailable()) {
            const QVariantMap info = backdrops.pdfInfo(u"Załączniki/wykład.pdf"_s);
            QCOMPARE(info.value(u"pages"_s).toInt(), 2);
            QCOMPARE(info.value(u"error"_s).toString(), QString());
            QVERIFY(backdrops.pageUrl(u"Załączniki/wykład.pdf"_s, 2).toString().startsWith(u"image://pdfpage/"_s));
            QVERIFY(backdrops.nameFilters().first().contains(u"*.pdf"_s));
            // Two pages, cancelled at once: finished as cancelled with at most what was done.
            finished.clear();
            const int two = backdrops.prepare(u"Załączniki/wykład.pdf"_s, {2, 1, 2, -4});
            backdrops.cancel(two);
            QVERIFY(finished.wait(10000 * BRULION_TEST_SLOWDOWN));
            QCOMPARE(finished.first().at(2).toBool(), true);
            QVERIFY(finished.first().at(1).toList().size() < 2);
            // Not cancelled: both, in the order asked, duplicates dropped.
            finished.clear();
            backdrops.prepare(u"Załączniki/wykład.pdf"_s, {2, 1, 2});
            QVERIFY(finished.wait(20000 * BRULION_TEST_SLOWDOWN));
            const QVariantList pages = finished.first().at(1).toList();
            QCOMPARE(pages.size(), 2);
            QCOMPARE(pages.at(0).toMap().value(u"page"_s).toInt(), 2);
            QCOMPARE(pages.at(1).toMap().value(u"page"_s).toInt(), 1);
            QCOMPARE(pages.at(1).toMap().value(u"height"_s).toInt(), 2400);
        } else {
            QCOMPARE(backdrops.pdfInfo(u"Załączniki/wykład.pdf"_s).value(u"pages"_s).toInt(), -1);
            QVERIFY(!backdrops.nameFilters().first().contains(u"*.pdf"_s));
            QVERIFY(backdrops.pageUrl(u"Załączniki/wykład.pdf"_s, 1).isEmpty());
        }
        QVERIFY(backdrops.pdfInfo(u"Załączniki/nie ma.pdf"_s).value(u"pages"_s).toInt() < 0);
        QCOMPARE(backdrops.pickInBrowser(), 0); // (not the browser)
        DrawingBackdrops::setPathResolver({});
    }

    void saveTimeWithBackdrop()
    {
        // 500 strokes on a 2.5 MB backdrop: every autosave writes the raster twice (scene + picture).
        QImage noise(1100, 760, QImage::Format_RGB32);
        for (int y = 0; y < noise.height(); ++y) {
            auto *line = reinterpret_cast<QRgb *>(noise.scanLine(y));
            for (int x = 0; x < noise.width(); ++x)
                line[x] = QRandomGenerator::global()->generate() | 0xff000000;
        }
        QByteArray png;
        QBuffer buffer(&png);
        QVERIFY(buffer.open(QIODevice::WriteOnly));
        QVERIFY(noise.save(&buffer, "PNG"));
        QVERIFY(png.size() > 2 * 1024 * 1024);
        brulion::drawing::Drawing d;
        QVERIFY(brulion::drawing::setBackdrop(d, {png, u"image/png"_s, noise.size()}, {u"slajd.png"_s, 0}, {}));
        for (int i = 0; i < 500; ++i) {
            brulion::drawing::Stroke s;
            s.id = u"s%1"_s.arg(i);
            for (int k = 0; k < 20; ++k)
                s.points.append({50.0 + k * 10, 20.0 + i * 1.5, 0.5});
            d.strokes.append(s);
        }
        QElapsedTimer t;
        t.start();
        const QByteArray svg = brulion::drawing::toSvg(d);
        const qint64 ms = t.elapsed();
        qInfo("toSvg of 500 strokes on a %.1f MB backdrop: %lld ms, %.1f MB", png.size() / 1048576.0, ms,
              svg.size() / 1048576.0);
#ifdef NDEBUG
        QVERIFY2(ms < 1000, qPrintable(QString::number(ms)));
#endif
    }
};

QTEST_MAIN(TestDrawingCanvas)
#include "tst_drawingcanvas.moc"
