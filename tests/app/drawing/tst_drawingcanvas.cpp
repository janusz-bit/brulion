// DrawingCanvas (src/app/drawing): input as the platforms deliver it — a stylus's tablet events
// through QWindowSystemInterface (pressure, the eraser end, the barrel button, a popup over the
// canvas; Windows' mouse events made of the pen), a mouse, fingers (drawing, panning, a pinch),
// the wheel —, undo / redo, the SVG it writes (valid for QSvgRenderer, read back equal), loading
// Excalidraw files, merging a version another device wrote and what it paints. Offscreen.
#include "drawingcanvas.h"

#include "drawing/stroke.h"
#include "drawing/svgwrite.h"

#include <QFile>
#include <QImage>
#include <QPainter>
#include <QPointingDevice>
#include <QQmlComponent>
#include <QQmlEngine>
#include <QQuickItem>
#include <QQuickWindow>
#include <QSignalSpy>
#include <QSvgRenderer>
#include <QTemporaryDir>
#include <QTest>
#include <qpa/qwindowsysteminterface.h>

#include <memory>

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
                    window.get(), pen.time(), p, window->mapToGlobal(p), Qt::LeftButton, Qt::NoButton, QEvent::MouseMove);
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
        QCOMPARE(image.pixelColor(2, 2).alpha(), 255); // the paper (a white sheet in every theme)
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
        QTest::newRow("excalidraw with shapes") << fixture(u"excalidraw_mixed.svg"_s) << u"readonly"_s << 0;
        QTest::newRow(".excalidraw") << fixture(u"excalidraw_mixed.excalidraw"_s) << u"readonly"_s << 0;
        QTest::newRow("plain svg") << QByteArray("<svg xmlns=\"http://www.w3.org/2000/svg\"/>") << u"notDrawing"_s << 0;
        QTest::newRow("photo") << QByteArray("\xff\xd8\xff\xe0 zdjęcie") << u"notDrawing"_s << 0;
        QTest::newRow("broken payload")
                << QByteArray("<svg><!-- payload-type:application/vnd.excalidraw+json --><!-- payload-start -->@@@<!-- payload-end --></svg>")
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
        QVERIFY(canvas->modified()); // still to be written
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
        QVERIFY(image.pixelColor(200, 100).lightness() < 100);               // ink
        QCOMPARE(image.pixelColor(400, 50).name(), u"#ffffff"_s);            // paper
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
        word(40, 70, 7, 26, u"#1f2430"_s, 1.0);   // a title
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
};

QTEST_MAIN(TestDrawingCanvas)
#include "tst_drawingcanvas.moc"
