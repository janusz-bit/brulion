// DrawingCanvas (src/app/drawing): input as the platforms deliver it — a stylus's tablet events
// through QWindowSystemInterface (pressure, the eraser end, the barrel button, a popup over the
// canvas; Windows' mouse events made of the pen), a mouse, fingers (drawing, panning, a pinch),
// the wheel —, undo / redo, the SVG it writes (valid for QSvgRenderer, read back equal), loading
// Excalidraw files, merging a version another device wrote and what it paints — light and dark
// (the drawing's theme, the app's) —, and the previews of drawings (DrawingImageProvider,
// drawingrender.h). Offscreen.
#include "drawingcanvas.h"
#include "drawingimageprovider.h"
#include "drawingrender.h"

#include "drawing/excalidraw.h"
#include "drawing/palette.h"
#include "drawing/stroke.h"
#include "drawing/svgwrite.h"

#include <QFile>
#include <QImage>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
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
#include <QtMath>
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

QStringList strokeIds(const DrawingCanvas *canvas)
{
    QStringList ids;
    for (const auto &s : canvas->currentDrawing().strokes)
        ids.append(s.id);
    return ids;
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
        QCOMPARE(canvas->load(fixture(u"excalidraw_mixed.svg"_s)), u"readonly"_s);
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
        const QList<QRgb> colours = {qRgb(255, 255, 255), qRgb(0x1f, 0x24, 0x30), qRgb(0xe0, 0x31, 0x31),
                                     qRgb(0xff, 0xd4, 0x3b), qRgb(0, 0, 0),         qRgb(0x2f, 0x9e, 0x44),
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
        QCOMPARE(qAlpha(brulion::drawingview::darkImage(translucent).convertToFormat(QImage::Format_ARGB32).pixel(0, 0)),
                 128);
        QVERIFY(brulion::drawingview::darkImage(QImage()).isNull());
    }

    void imageProvider_data()
    {
        QTest::addColumn<QByteArray>("bytes");     // the file ("" none)
        QTest::addColumn<QString>("name");         // its name in the temporary folder
        QTest::addColumn<bool>("appDark");
        QTest::addColumn<QString>("paper");        // the corner pixel ("" no picture)
        QTest::addColumn<QString>("ink");          // at the stroke ("" not checked)
        using brulion::drawing::PaperTheme;
        QTest::newRow("as the app, light") << inkFile(PaperTheme::Auto) << u"Rysunek ćwiczenia.svg"_s << false
                                           << u"#ffffff"_s << u"#1f2430"_s;
        QTest::newRow("as the app, dark") << inkFile(PaperTheme::Auto) << u"Rysunek ćwiczenia.svg"_s << true
                                          << u"#121212"_s << u"#caced8"_s;
        QTest::newRow("light paper, dark app") << inkFile(PaperTheme::Light) << u"Jasny.svg"_s << true << u"#ffffff"_s
                                               << u"#1f2430"_s;
        QTest::newRow("dark paper, light app") << inkFile(PaperTheme::Dark) << u"Ciemny #1 (100%).svg"_s << false
                                               << u"#121212"_s << u"#caced8"_s;
        // An Excalidraw drawing with shapes: its own picture, turned dark like Excalidraw does.
        QTest::newRow("excalidraw, light") << fixture(u"excalidraw_mixed.svg"_s) << u"Schemat.svg"_s << false
                                           << u"#ffffff"_s << QString();
        QTest::newRow("excalidraw, dark") << fixture(u"excalidraw_mixed.svg"_s) << u"Schemat.svg"_s << true
                                          << u"#121212"_s << QString();
        QTest::newRow(".excalidraw") << fixture(u"excalidraw_mixed.excalidraw"_s) << u"scena.excalidraw"_s << true
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
};

QTEST_MAIN(TestDrawingCanvas)
#include "tst_drawingcanvas.moc"
