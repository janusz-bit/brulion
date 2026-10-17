// TouchFlick (src/app/ui/touchflick.h) and the Qt 6.11 touch faults it works around, with touch
// input delivered the way the Android QPA does (androidjniinput.cpp): through
// QWindowSystemInterface::handleTouchEvent with a touch screen that reports no velocities, here
// synchronous and with explicit timestamps, so every number is deterministic. (QTest::touchEvent
// shows the same damping, but takes its timestamps from the wall clock.)
//
// Flings are measured by where they end: Flickable rounds the distance to a whole pixel and
// adjusts the deceleration a, so the start velocity is v = sqrt(2 a d) for any frame timing.
// The rows without TouchFlick pin down Qt's own behaviour: when one fails after a Qt update,
// Qt changed (fixed?) it, and docs/UI.md "Scrolling performance" needs a look.
#include "touchflick.h"

#include <QPointingDevice>
#include <QQmlComponent>
#include <QQmlEngine>
#include <QQuickItem>
#include <QQuickWindow>
#include <QSignalSpy>
#include <QTest>
#include <QWheelEvent>
#include <QtQml/qqml.h>
#include <qpa/qwindowsysteminterface.h>

#include <cmath>
#include <memory>

using namespace Qt::StringLiterals;
using brulion::ui::TouchFlick;
using brulion::ui::TouchVelocityEstimator;

namespace {

constexpr int Slowdown = BRULION_TEST_SLOWDOWN;

/// One finger on a touch screen, delivered like the Android QPA does.
class Finger {
public:
    Finger(QWindow *window, const QPointingDevice *device, ulong startMs)
        : m_window(window), m_device(device), m_t(startMs)
    { }

    void press(QPointF scenePos) { send({point(QEventPoint::State::Pressed, scenePos)}); }
    void moveTo(QPointF scenePos, ulong dtMs, QVector2D reported = {})
    {
        m_t += dtMs;
        send({point(QEventPoint::State::Updated, scenePos, reported)});
    }
    void release(QPointF scenePos, ulong dtMs, QVector2D reported = {})
    {
        m_t += dtMs;
        send({point(QEventPoint::State::Released, scenePos, reported)});
    }
    void wait(ulong ms) { m_t += ms; }

    QWindowSystemInterface::TouchPoint point(QEventPoint::State state, QPointF scenePos, QVector2D velocity = {},
                                             int id = 1) const
    {
        QWindowSystemInterface::TouchPoint tp;
        tp.id = id;
        tp.state = state;
        const QPointF global = m_window->mapToGlobal(scenePos);
        tp.area = QRectF(global - QPointF(3, 3), QSizeF(6, 6));
        const QSizeF screen = m_window->screen()->geometry().size();
        tp.normalPosition = QPointF(global.x() / screen.width(), global.y() / screen.height());
        tp.pressure = state == QEventPoint::State::Released ? 0 : 1;
        tp.velocity = velocity;
        return tp;
    }
    void send(const QList<QWindowSystemInterface::TouchPoint> &points)
    {
        QWindowSystemInterface::handleTouchEvent<QWindowSystemInterface::SynchronousDelivery>(m_window, m_t, m_device,
                                                                                              points);
    }

private:
    QWindow *m_window;
    const QPointingDevice *m_device;
    ulong m_t;
};

/// Rows: a plain list (presses land on the Flickable itself), rows whose children take the
/// press (Flickable filters them: MouseArea, TapHandler, and text with a TapHandler like a
/// block's BlockText.qml), and a plain Flickable.
QString sceneQml(const QString &kind, qreal deceleration, bool touchFlick)
{
    const QString box = u"width: ListView.view.width; height: 40; color: index % 2 ? \"#eee\" : \"#ccc\""_s;
    QString delegate;
    if (kind == u"plain")
        delegate = u"Rectangle { %1 }"_s.arg(box);
    else if (kind == u"mousearea")
        delegate = u"Rectangle { %1; MouseArea { anchors.fill: parent } }"_s.arg(box);
    else if (kind == u"taphandler")
        delegate = u"Rectangle { %1; TapHandler {} }"_s.arg(box);
    else if (kind == u"blocktext")
        delegate = u"Text { width: ListView.view.width; height: 40; text: \"Wiersz \" + index + \" — zażółć gęślą "
                   u"jaźń\"; TapHandler { gesturePolicy: TapHandler.ReleaseWithinBounds } }"_s;
    const QString common =
            u"flickDeceleration: %1; maximumFlickVelocity: 2500; boundsBehavior: Flickable.StopAtBounds; "
            u"%2"_s.arg(deceleration)
                    .arg(touchFlick ? u"TouchFlick.enabled: true"_s : QString());
    if (kind == u"flickable")
        return u"import QtQuick\nimport BrulionTest\nFlickable { width: 400; height: 800; contentHeight: 160000; %1\n"
               u"Rectangle { width: 400; height: 160000; color: \"#ddd\" } }"_s.arg(common);
    if (kind == u"snap")
        return u"import QtQuick\nimport BrulionTest\nListView { width: 400; height: 800; model: 4000; %1; "
               u"snapMode: ListView.SnapToItem\ndelegate: Rectangle { %2 } }"_s.arg(common, box);
    return u"import QtQuick\nimport BrulionTest\nListView { width: 400; height: 800; model: 4000; %1\n"
           u"delegate: %2 }"_s.arg(common, delegate);
}

/// The samples of a stroke at constant speed: press at 0, `moves` moves `dt` ms apart, each
/// reported the way Qt 6.11 does for a device without velocities (0.7 x instantaneous).
TouchVelocityEstimator dampedStroke(double speed, int moves, qint64 dt)
{
    TouchVelocityEstimator e;
    QPointF p(100, 500);
    e.press(0, p);
    for (int i = 1; i <= moves; ++i) {
        const QPointF next = p + QPointF(0, -speed * double(dt) / 1000);
        const QVector2D instant = QVector2D(next - p) * (1000.0f / float(dt));
        e.move(i * dt, next, instant * TouchVelocityEstimator::QtGain);
        p = next;
    }
    return e;
}

/// Delivers the window's touch events itself, like the scroll probe's observe mode: other
/// filters on the window never see them.
class DeliveringFilter : public QObject {
public:
    bool eventFilter(QObject *watched, QEvent *event) override
    {
        if (!watched->isWindowType() || !event->isPointerEvent())
            return false;
        static_cast<QObject *>(watched)->event(event);
        return true;
    }
};

} // namespace

class TestTouchFlick : public QObject {
    Q_OBJECT

private:
    std::unique_ptr<QQmlEngine> m_engine;
    std::unique_ptr<QQuickWindow> m_window;
    QQuickItem *m_view = nullptr;
    QPointingDevice *m_touch = nullptr;         // like Android's: no velocities
    QPointingDevice *m_velocityTouch = nullptr; // reports velocities
    QVector2D m_reported;                       // what a velocity-reporting device sends
    ulong m_clock = 100000;

    bool makeScene(const QString &kind, qreal deceleration, bool touchFlick)
    {
        m_window = std::make_unique<QQuickWindow>();
        m_window->resize(400, 800);
        m_engine = std::make_unique<QQmlEngine>();
        QQmlComponent c(m_engine.get());
        c.setData(sceneQml(kind, deceleration, touchFlick).toUtf8(), QUrl(u"file:///touchflick.qml"_s));
        auto *item = qobject_cast<QQuickItem *>(c.create());
        if (!item) {
            qWarning() << c.errors();
            return false;
        }
        item->setParent(m_window.get());
        item->setParentItem(m_window->contentItem());
        m_view = item;
        m_window->show();
        if (!QTest::qWaitForWindowExposed(m_window.get()))
            return false;
        m_view->setProperty("contentY", 40000.0);
        QCoreApplication::processEvents();
        return true;
    }

    TouchFlick *touchFlick() const
    {
        return qobject_cast<TouchFlick *>(qmlAttachedPropertiesObject<TouchFlick>(m_view, false));
    }
    double contentY() const { return m_view->property("contentY").toDouble(); }
    bool flicking() const { return m_view->property("flicking").toBool(); }

    /// A straight stroke up the screen (the content scrolls down) at `speed` px/s: press, `moves`
    /// moves `dt` ms apart (timestamps shifted by `jitter`, as Android's arrival times are),
    /// release `releaseDt` ms later (where the finger got to, or where the last move was).
    void stroke(Finger &f, double speed, int moves, ulong dt = 16, ulong releaseDt = 8, bool releaseMoves = true,
                const QList<int> &jitter = {}, double y0 = 700)
    {
        QPointF p(200, y0);
        f.press(p);
        qint64 t = 0;
        qint64 sent = 0;
        for (int i = 0; i < moves; ++i) {
            t += qint64(dt);
            p.setY(y0 - speed * double(t) / 1000);
            const qint64 at = t + (jitter.isEmpty() ? 0 : jitter.at(i % jitter.size()));
            f.moveTo(p, ulong(at - sent), m_reported);
            sent = at;
        }
        if (releaseMoves)
            p.setY(y0 - speed * double(t + qint64(releaseDt)) / 1000);
        f.release(p, ulong(t + qint64(releaseDt) - sent), m_reported);
    }

    /// Lets the window render a frame: Qt Quick delivers the touch moves it compressed (all
    /// that arrived since the last frame, as one) at the start of the frame.
    bool waitFrame()
    {
        QSignalSpy swapped(m_window.get(), &QQuickWindow::frameSwapped);
        m_window->update();
        return swapped.wait(2000 * Slowdown);
    }

    bool waitUntilStill()
    {
        return QTest::qWaitFor([this] { return !m_view->property("moving").toBool(); }, 10000 * Slowdown);
    }

    /// The start velocity of the fling running now, from where it ends.
    double flingVelocity(double fromY)
    {
        if (!waitUntilStill())
            return -1;
        const double d = std::abs(contentY() - fromY);
        return std::sqrt(2 * m_view->property("flickDeceleration").toDouble() * d);
    }

private Q_SLOTS:
    void initTestCase()
    {
        qmlRegisterUncreatableType<TouchFlick>("BrulionTest", 1, 0, "TouchFlick", u"attached"_s);
        const auto caps = QInputDevice::Capability::Position | QInputDevice::Capability::Area
                          | QInputDevice::Capability::NormalizedPosition;
        m_touch = new QPointingDevice(u"touch screen (Android-like)"_s, 4711, QInputDevice::DeviceType::TouchScreen,
                                      QPointingDevice::PointerType::Finger, caps | QInputDevice::Capability::Pressure,
                                      10, 0, QString(), QPointingDeviceUniqueId(), this);
        QWindowSystemInterface::registerInputDevice(m_touch);
        m_velocityTouch =
                new QPointingDevice(u"touch screen with velocities"_s, 4712, QInputDevice::DeviceType::TouchScreen,
                                    QPointingDevice::PointerType::Finger, caps | QInputDevice::Capability::Velocity, 10,
                                    0, QString(), QPointingDeviceUniqueId(), this);
        QWindowSystemInterface::registerInputDevice(m_velocityTouch);
    }

    void cleanup()
    {
        m_view = nullptr;
        m_window.reset();
        m_engine.reset();
        m_reported = {};
    }

    // ---- The estimate ---------------------------------------------------------------------------

    void estimator_data()
    {
        QTest::addColumn<int>("moves");
        QTest::addColumn<qint64>("dt");
        QTest::newRow("60 Hz: the last 3 moves (Flickable's samples)") << 12 << qint64(16);
        QTest::newRow("120 Hz: 5 segments, 40 ms") << 20 << qint64(8);
        QTest::newRow("90 Hz: 4 segments") << 20 << qint64(11);
        QTest::newRow("30 Hz: 3 segments, 99 ms") << 8 << qint64(33);
        QTest::newRow("25 Hz: 2 segments within 100 ms") << 8 << qint64(40);
        QTest::newRow("short stroke: from the press") << 2 << qint64(16);
        QTest::newRow("one move") << 1 << qint64(16);
        QTest::newRow("longer than the ring") << 60 << qint64(16);
    }

    void estimator()
    {
        QFETCH(int, moves);
        QFETCH(qint64, dt);
        const TouchVelocityEstimator e = dampedStroke(1200, moves, dt);
        const auto finger = e.fingerVelocity();
        QVERIFY(finger);
        QCOMPARE(qRound(finger->y()), -1200);
        QCOMPARE(qRound(finger->x()), 0);
        const auto reported = e.reportedVelocity(2500);
        QVERIFY(reported);
        QCOMPARE(qRound(reported->y()), -840);
        // One sample proves nothing (Qt's filter starts from 0 at the first move anyway).
        QCOMPARE(e.hasQtDampedSamples(), moves >= 2);
    }

    void estimatorWindow()
    {
        // An accelerating finger: the mean over the window, newest samples only.
        TouchVelocityEstimator e;
        e.press(0, {0, 0});
        const QList<double> ys{0, 10, 30, 60, 100, 150, 210};
        for (int i = 1; i < ys.size(); ++i)
            e.move(i * 16, {0, ys.at(i)}, {});
        // 60 Hz, 3 segments (48 ms): (210 - 60) / 0.048.
        QCOMPARE(qRound(e.fingerVelocity()->y()), 3125);
        // A release where the finger moved on counts (40 ms need 4 segments now: 52 ms).
        e.release(100, {0, 230});
        QCOMPARE(qRound(e.fingerVelocity()->y()), qRound((230 - 60) / 0.052));
        // A release at the same position adds no zero-velocity tail.
        TouchVelocityEstimator same = dampedStroke(1000, 6, 16);
        const QVector2D before = *same.fingerVelocity();
        same.release(6 * 16 + 4, {100, 500 - 1000 * 0.096});
        QCOMPARE(*same.fingerVelocity(), before);
    }

    void estimatorEdges()
    {
        TouchVelocityEstimator e;
        QVERIFY(!e.fingerVelocity());
        QVERIFY(!e.reportedVelocity(2500));
        QVERIFY(!e.hasQtDampedSamples());
        e.press(1000, {10, 10});
        QVERIFY(!e.fingerVelocity());
        // A pause longer than the window before the only move: no estimate.
        e.move(1150, {10, 60}, {0, 233});
        QVERIFY(!e.fingerVelocity());
        // Out of order: ignored; the same timestamp: merged (Qt computes nothing for it).
        e.move(1100, {10, 999}, {});
        QCOMPARE(e.size(), 2);
        e.move(1150, {10, 70}, {});
        QCOMPARE(e.size(), 2);
        e.move(1166, {10, 86}, {0, 700});
        QCOMPARE(qRound(e.fingerVelocity()->y()), 1000);
        // Reported velocities are clamped like Flickable's samples.
        const TouchVelocityEstimator fast = dampedStroke(5000, 6, 16);
        QCOMPARE(qRound(fast.reportedVelocity(2500)->y()), -2500);
        QCOMPARE(qRound(fast.reportedVelocity(0)->y()), -3500);
        // Velocities Qt filtered properly (0.7 new + 0.3 previous): not the damped signature.
        TouchVelocityEstimator kalman;
        kalman.press(0, {0, 0});
        QVector2D filtered;
        for (int i = 1; i <= 6; ++i) {
            filtered = QVector2D(0, 1000) * 0.7f + filtered * 0.3f;
            kalman.move(i * 16, {0, 16.0 * i}, filtered);
        }
        QVERIFY(!kalman.hasQtDampedSamples());
        // A device reporting the real velocity: not damped either.
        TouchVelocityEstimator real;
        real.press(0, {0, 0});
        for (int i = 1; i <= 6; ++i)
            real.move(i * 16, {0, 16.0 * i}, {0, 1000});
        QVERIFY(!real.hasQtDampedSamples());
    }

    // ---- Release velocity -----------------------------------------------------------------------

    void releaseVelocity_data()
    {
        QTest::addColumn<QString>("kind");
        QTest::addColumn<double>("speed");
        QTest::addColumn<bool>("fix");
        QTest::addColumn<double>("expected"); // fling / finger speed
        for (const QString &kind : {u"plain"_s, u"mousearea"_s, u"taphandler"_s, u"blocktext"_s, u"flickable"_s}) {
            for (double speed : {300.0, 800.0, 1500.0, 2200.0}) {
                QTest::addRow("%s-%g-qt", qPrintable(kind), speed) << kind << speed << false << 0.7;
                QTest::addRow("%s-%g-touchflick", qPrintable(kind), speed) << kind << speed << true << 1.0;
            }
        }
    }

    void releaseVelocity()
    {
        QFETCH(QString, kind);
        QFETCH(double, speed);
        QFETCH(bool, fix);
        QFETCH(double, expected);
        QVERIFY(makeScene(kind, speed * speed / 300, fix)); // flings of ~150 px
        Finger f(m_window.get(), m_touch, m_clock += 10000);
        stroke(f, speed, 12);
        const double y0 = contentY();
        QVERIFY(flicking());
        const double ratio = flingVelocity(y0) / speed;
        QVERIFY2(std::abs(ratio - expected) < 0.015, qPrintable(u"fling / finger %1"_s.arg(ratio)));
        if (fix) {
            QCOMPARE(touchFlick()->corrections(), 1);
            QVERIFY(std::abs(touchFlick()->lastFactor() - 1 / 0.7) < 0.01);
        }
    }

    void releaseVariants_data()
    {
        QTest::addColumn<int>("dt");
        QTest::addColumn<int>("releaseDt");
        QTest::addColumn<bool>("releaseMoves");
        QTest::addColumn<QList<int>>("jitter");
        QTest::addColumn<double>("tolerance");
        QTest::newRow("120 Hz") << 8 << 4 << true << QList<int>() << 0.015;
        QTest::newRow("90 Hz") << 11 << 6 << true << QList<int>() << 0.015;
        QTest::newRow("release in place, 4 ms later") << 16 << 4 << false << QList<int>() << 0.015;
        QTest::newRow("release in place, 60 ms later") << 16 << 60 << false << QList<int>() << 0.015;
        // Arrival-time jitter: positions on a 16 ms grid, timestamps up to 3 ms off.
        QTest::newRow("jittery timestamps") << 16 << 8 << true << QList<int>{2, -1, 3, -3, 0, 1, -2, 3, -1, 2} << 0.08;
    }

    void releaseVariants()
    {
        QFETCH(int, dt);
        QFETCH(int, releaseDt);
        QFETCH(bool, releaseMoves);
        QFETCH(QList<int>, jitter);
        QFETCH(double, tolerance);
        const double speed = 1500;
        QVERIFY(makeScene(u"blocktext"_s, speed * speed / 300, true));
        Finger f(m_window.get(), m_touch, m_clock += 10000);
        stroke(f, speed, 200 / dt, ulong(dt), ulong(releaseDt), releaseMoves, jitter);
        const double ratio = flingVelocity(contentY()) / speed;
        QVERIFY2(std::abs(ratio - 1) < tolerance, qPrintable(u"fling / finger %1"_s.arg(ratio)));
        QCOMPARE(touchFlick()->corrections(), 1);
    }

    void acceleratingFinger()
    {
        // Qt's samples are 0.7x each move's own speed; the fling gets their undamped mean.
        QVERIFY(makeScene(u"plain"_s, 3000, true));
        Finger f(m_window.get(), m_touch, m_clock += 10000);
        QPointF p(200, 700);
        f.press(p);
        for (double step : {4, 8, 12, 16, 20, 24, 28, 32, 36}) {
            p.ry() -= step;
            f.moveTo(p, 16);
        }
        f.release(p, 4);
        const double expected = (28 + 32 + 36) / 0.048;
        const double v = flingVelocity(contentY());
        QVERIFY2(std::abs(v / expected - 1) < 0.015, qPrintable(u"fling %1, expected %2"_s.arg(v).arg(expected)));
    }

    void maximumVelocity()
    {
        // 4000 px/s: Qt's damped 2800 is capped at 2500 already; corrected, it stays 2500.
        for (bool fix : {false, true}) {
            QVERIFY(makeScene(u"blocktext"_s, 2500.0 * 2500 / 300, fix));
            Finger f(m_window.get(), m_touch, m_clock += 10000);
            stroke(f, 4000, 12);
            QCOMPARE(qRound(flingVelocity(contentY())), 2500);
            cleanup();
        }
    }

    void velocityDeviceUntouched()
    {
        QVERIFY(makeScene(u"blocktext"_s, 1500.0 * 1500 / 300, true));
        Finger f(m_window.get(), m_velocityTouch, m_clock += 10000);
        m_reported = QVector2D(0, -1500);
        stroke(f, 1500, 12);
        QCOMPARE(qRound(flingVelocity(contentY())), 1500);
        QCOMPARE(touchFlick()->corrections(), 0);
    }

    void disabled()
    {
        QVERIFY(makeScene(u"blocktext"_s, 1500.0 * 1500 / 300, true));
        touchFlick()->setEnabled(false);
        Finger f(m_window.get(), m_touch, m_clock += 10000);
        stroke(f, 1500, 12);
        QVERIFY(std::abs(flingVelocity(contentY()) - 1050) < 10);
        QCOMPARE(touchFlick()->corrections(), 0);
    }

    void otherFilterDeliversItself()
    {
        // The tracker is an application filter: it sees the events before a window filter that
        // delivers them itself (the scroll probe's observe mode).
        QVERIFY(makeScene(u"blocktext"_s, 1500.0 * 1500 / 300, true));
        DeliveringFilter filter;
        m_window->installEventFilter(&filter);
        Finger f(m_window.get(), m_touch, m_clock += 10000);
        stroke(f, 1500, 12);
        const double ratio = flingVelocity(contentY()) / 1500;
        m_window->removeEventFilter(&filter);
        QVERIFY2(std::abs(ratio - 1) < 0.015, qPrintable(u"fling / finger %1"_s.arg(ratio)));
    }

    void mouseAndWheelUntouched()
    {
        QVERIFY(makeScene(u"plain"_s, 1500.0 * 1500 / 300, true));
        QPoint p(200, 700);
        QTest::mousePress(m_window.get(), Qt::LeftButton, {}, p);
        for (int i = 0; i < 12; ++i) {
            p.ry() -= 24;
            QTest::mouseMove(m_window.get(), p, 16);
        }
        QTest::mouseRelease(m_window.get(), Qt::LeftButton, {}, p, 8);
        QVERIFY(flicking());
        QVERIFY(waitUntilStill());
        const QPointF at(200, 400);
        QWheelEvent wheel(at, m_window->mapToGlobal(at), QPoint(), QPoint(0, -120), Qt::NoButton, Qt::NoModifier,
                          Qt::NoScrollPhase, false);
        QCoreApplication::sendEvent(m_window.get(), &wheel);
        QVERIFY(waitUntilStill());
        QCOMPARE(touchFlick()->corrections(), 0);
    }

    void multiTouchUntouched()
    {
        QVERIFY(makeScene(u"plain"_s, 1500.0 * 1500 / 300, true));
        Finger f(m_window.get(), m_touch, m_clock += 10000);
        QPointF a(150, 700), b(250, 700);
        f.press(a);
        f.wait(16);
        f.send({f.point(QEventPoint::State::Stationary, a), f.point(QEventPoint::State::Pressed, b, {}, 2)});
        for (int i = 0; i < 10; ++i) {
            a.ry() -= 24;
            b.ry() -= 24;
            f.wait(16);
            f.send({f.point(QEventPoint::State::Updated, a), f.point(QEventPoint::State::Updated, b, {}, 2)});
        }
        f.wait(8);
        f.send({f.point(QEventPoint::State::Released, a), f.point(QEventPoint::State::Released, b, {}, 2)});
        QVERIFY(waitUntilStill());
        QCOMPARE(touchFlick()->corrections(), 0);
    }

    void flickSignConvention()
    {
        // Flickable::flick(): + moves the content down (contentY decreases), like a finger moving
        // down; verticalVelocity has the opposite sign. TouchFlick relies on both.
        QVERIFY(makeScene(u"plain"_s, 1500, false));
        const double y0 = contentY();
        QMetaObject::invokeMethod(m_view, "flick", Q_ARG(qreal, 0), Q_ARG(qreal, 600));
        QVERIFY(waitUntilStill());
        QCOMPARE(qRound(y0 - contentY()), 120); // 600² / (2 · 1500)
        Finger f(m_window.get(), m_touch, m_clock += 10000);
        stroke(f, 1000, 12); // up: contentY grows
        QCOMPARE(qRound(m_view->property("verticalVelocity").toDouble()), 700);
    }

    // ---- A drag started during a fling ----------------------------------------------------------

    void chainedDrag_data()
    {
        QTest::addColumn<QString>("kind");
        QTest::addColumn<bool>("fix");
        QTest::addColumn<bool>("wait"); // let the fling run a few frames before the press
        QTest::addColumn<QString>("steps");
        QTest::addColumn<int>("catches");
        // Qt 6.11: a press on the Flickable itself moves at once; on a child the first move
        // waits and comes with the second.
        QTest::newRow("plain-qt") << u"plain"_s << false << false << u"20 20 20 20"_s << 0;
        // (TouchFlick applies the first move in both cases; on the Flickable itself Qt then puts the
        // content at the same place.)
        QTest::newRow("plain-touchflick") << u"plain"_s << true << false << u"20 20 20 20"_s << 1;
        for (const QString &kind : {u"mousearea"_s, u"taphandler"_s, u"blocktext"_s}) {
            QTest::addRow("%s-qt", qPrintable(kind)) << kind << false << false << u"0 40 20 20"_s << 0;
            QTest::addRow("%s-touchflick", qPrintable(kind)) << kind << true << false << u"20 20 20 20"_s << 1;
            QTest::addRow("%s-touchflick-running", qPrintable(kind)) << kind << true << true << u"20 20 20 20"_s << 1;
        }
    }

    void chainedDrag()
    {
        QFETCH(QString, kind);
        QFETCH(bool, fix);
        QFETCH(bool, wait);
        QFETCH(QString, steps);
        QFETCH(int, catches);
        QVERIFY(makeScene(kind, 1500, fix));
        Finger f(m_window.get(), m_touch, m_clock += 10000);
        stroke(f, 1500, 12);
        QVERIFY(flicking());
        if (wait) {
            QTest::qWait(50);
            QVERIFY(flicking());
        }
        f.wait(100);
        QPointF p(200, 600);
        f.press(p);
        QVERIFY(!flicking());
        double y = contentY();
        QStringList got;
        double extra = 0;
        for (int i = 0; i < 4; ++i) {
            p.ry() -= 20;
            f.moveTo(p, 16);
            const double dy = contentY() - y;
            got.append(QString::number(qRound(dy)));
            extra += std::max(0.0, dy - 20);
            y = contentY();
        }
        f.release(p, 8);
        QCOMPARE(got.join(u' '), steps);
        if (fix) {
            QVERIFY(extra <= 3);
            QCOMPARE(touchFlick()->catches(), catches);
        }
    }

    void chainedDragCompressed_data()
    {
        QTest::addColumn<QString>("kind");
        QTest::addColumn<int>("perFrame"); // raw moves within one frame
        QTest::addColumn<bool>("fix");
        QTest::addColumn<QString>("steps");
        for (int perFrame : {1, 2, 3}) {
            QTest::addRow("plain-%d-qt", perFrame) << u"plain"_s << perFrame << false << u"20 20 20 20"_s;
            QTest::addRow("plain-%d-touchflick", perFrame) << u"plain"_s << perFrame << true << u"20 20 20 20"_s;
            for (const QString &kind : {u"mousearea"_s, u"taphandler"_s, u"blocktext"_s}) {
                QTest::addRow("%s-%d-qt", qPrintable(kind), perFrame) << kind << perFrame << false << u"0 40 20 20"_s;
                QTest::addRow("%s-%d-touchflick", qPrintable(kind), perFrame)
                        << kind << perFrame << true << u"20 20 20 20"_s;
            }
        }
    }

    void chainedDragCompressed()
    {
        // With Qt Quick's touch compression (its default; ctest tst_touch_flick_compressed) the
        // moves that arrive within one frame reach Flickable as one move at the frame's start —
        // several of them when the GUI thread was busy (a fling running into new rows). The move
        // Flickable skips is then the sum of them: TouchFlick must make up for all of it.
        if (qEnvironmentVariableIsSet("QML_NO_TOUCH_COMPRESSION"))
            QSKIP("needs Qt Quick's touch compression: ctest tst_touch_flick_compressed");
        QFETCH(QString, kind);
        QFETCH(int, perFrame);
        QFETCH(bool, fix);
        QFETCH(QString, steps);
        QVERIFY(makeScene(kind, 1500, fix));
        Finger f(m_window.get(), m_touch, m_clock += 10000);
        // The fling: one move per frame.
        QPointF p(200, 700);
        f.press(p);
        for (int i = 0; i < 12; ++i) {
            p.ry() -= 24;
            f.moveTo(p, 16);
            QVERIFY(waitFrame());
        }
        f.release(p + QPointF(0, -12), 8);
        QVERIFY(flicking());
        f.wait(100);
        p = QPointF(200, 600);
        f.press(p);
        QVERIFY(!flicking());
        double y = contentY();
        QStringList got;
        for (int frame = 0; frame < 4; ++frame) {
            for (int i = 0; i < perFrame; ++i) {
                p.ry() -= 20.0 / perFrame;
                f.moveTo(p, ulong(16 / perFrame));
            }
            QVERIFY(waitFrame());
            got.append(QString::number(qRound(contentY() - y)));
            y = contentY();
        }
        f.release(p, 8);
        QCOMPARE(got.join(u' '), steps);
        if (fix)
            QCOMPARE(touchFlick()->catches(), 1);
    }

    void snappingListUntouched()
    {
        // A ListView that snaps computes the end of a flick when it starts: a restarted flick
        // would end at the same row, so TouchFlick leaves it (and counts nothing).
        QVERIFY(makeScene(u"snap"_s, 1500, true));
        Finger f(m_window.get(), m_touch, m_clock += 10000);
        stroke(f, 1500, 12);
        QVERIFY(waitUntilStill());
        QCOMPARE(touchFlick()->corrections(), 0);
        QCOMPARE(qRound(contentY()) % 40, 0); // snapped to a row
    }

    void chainedFlickKeepsBoost()
    {
        // Flickable speeds up consecutive quick flicks of a long view (x1.25 for the second one):
        // the corrected flick keeps that. 1800 px/s: Qt's damped 1260 just passes its threshold.
        for (bool fix : {false, true}) {
            QVERIFY(makeScene(u"blocktext"_s, 1500, fix));
            Finger f(m_window.get(), m_touch, m_clock += 10000);
            stroke(f, 1800, 12);
            QVERIFY(flicking());
            f.wait(100);
            m_view->setProperty("flickDeceleration", 2250.0 * 2250 / 300);
            stroke(f, 1800, 12, 16, 8, true, {}, 750);
            const double ratio = flingVelocity(contentY()) / 1800;
            const double expected = fix ? 1.25 : 1.25 * 0.7;
            QVERIFY2(std::abs(ratio - expected) < 0.015, qPrintable(u"fling / finger %1"_s.arg(ratio)));
            cleanup();
        }
    }

    void tapDuringFling()
    {
        // A tap stops the fling: no flick, no catch, nothing corrected.
        QVERIFY(makeScene(u"taphandler"_s, 1500, true));
        Finger f(m_window.get(), m_touch, m_clock += 10000);
        stroke(f, 1500, 12);
        QVERIFY(flicking());
        f.wait(100);
        const QPointF p(200, 400);
        f.press(p);
        f.release(p, 60);
        const double y = contentY();
        QTest::qWait(50);
        QCOMPARE(contentY(), y);
        QCOMPARE(touchFlick()->catches(), 0);
        QCOMPARE(touchFlick()->corrections(), 1);
    }
};

QTEST_MAIN(TestTouchFlick)
#include "tst_touch_flick.moc"
