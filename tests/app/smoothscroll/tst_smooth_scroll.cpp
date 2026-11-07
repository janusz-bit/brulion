// SmoothScroll (src/app/ui/smoothscroll.h): the plan of a smooth scroll (SmoothScrollAxis, pure
// and deterministic), and views in an offscreen window scrolled by wheel events sent to the
// window as the platform sends them (Qt Quick delivers them to the items under the pointer).
// The views' motion runs on Qt's animation clock: the checks wait for it to stop and look at
// where it ended and how it got there.
#include "smoothscroll.h"

#include <QQmlComponent>
#include <QQmlEngine>
#include <QQuickItem>
#include <QQuickWindow>
#include <QStyleHints>
#include <QElapsedTimer>
#include <QTest>
#include <QTimer>
#include <QWheelEvent>

#include <cmath>
#include <memory>

using namespace Qt::StringLiterals;
using brulion::ui::SmoothScroll;
using brulion::ui::SmoothScrollAxis;

namespace {

constexpr int Slowdown = BRULION_TEST_SLOWDOWN;

double notch()
{
    return QGuiApplication::styleHints()->wheelScrollLines() * SmoothScroll::LinePx;
}

/// A list of 4000 rows of 40 px ("vertical"), a wide Flickable ("horizontal"), a list that snaps
/// ("snap"), a short list inside a Flickable that scrolls ("nested": the outer one is the root).
QString sceneQml(const QString &kind)
{
    const QString row = u"Rectangle { width: ListView.view.width; height: 40; color: index % 2 ? \"#eee\" : \"#ccc\"; "
                        u"Text { text: \"Wiersz \" + index + \" — zażółć gęślą jaźń\" } }"_s;
    if (kind == u"horizontal")
        return u"import QtQuick\nFlickable { width: 400; height: 400; contentWidth: 40000; contentHeight: 400\n"
               u"Rectangle { width: 40000; height: 400; color: \"#ddd\" } }"_s;
    if (kind == u"snap")
        return u"import QtQuick\nListView { width: 400; height: 800; model: 4000; snapMode: ListView.SnapToItem\n"
               u"delegate: %1 }"_s.arg(row);
    if (kind == u"nested")
        return u"import QtQuick\nFlickable { width: 400; height: 800; contentHeight: 4000; boundsBehavior: "
               u"Flickable.StopAtBounds\nListView { objectName: \"inner\"; y: 0; width: 400; height: 400; "
               u"model: 20; boundsBehavior: Flickable.StopAtBounds\ndelegate: %1 } }"_s.arg(row);
    return u"import QtQuick\nListView { width: 400; height: 800; model: 4000; boundsBehavior: Flickable.StopAtBounds\n"
           u"delegate: %1 }"_s.arg(row);
}

} // namespace

class TestSmoothScroll : public QObject {
    Q_OBJECT

private:
    std::unique_ptr<QQmlEngine> m_engine;
    std::unique_ptr<QQuickWindow> m_window;
    QQuickItem *m_view = nullptr;
    QElapsedTimer m_clock; // the events' timestamps: Qt's wheel flicks take their velocity from them

    bool makeScene(const QString &kind)
    {
        m_window = std::make_unique<QQuickWindow>();
        m_window->resize(400, 800);
        m_engine = std::make_unique<QQmlEngine>();
        QQmlComponent c(m_engine.get());
        c.setData(sceneQml(kind).toUtf8(), QUrl(u"file:///smoothscroll.qml"_s));
        auto *item = qobject_cast<QQuickItem *>(c.create());
        if (!item) {
            qWarning() << c.errors();
            return false;
        }
        item->setParent(m_window.get());
        item->setParentItem(m_window->contentItem());
        m_view = item;
        m_window->show();
        return QTest::qWaitForWindowExposed(m_window.get());
    }

    void wheel(QPoint angle, Qt::KeyboardModifiers modifiers = Qt::NoModifier, Qt::ScrollPhase phase = Qt::NoScrollPhase,
               QPoint pixel = {}, QPointF at = QPointF(200, 200))
    {
        QWheelEvent ev(at, m_window->mapToGlobal(at), pixel, angle, Qt::NoButton, modifiers, phase, false,
                       phase == Qt::NoScrollPhase ? Qt::MouseEventNotSynthesized : Qt::MouseEventSynthesizedBySystem);
        ev.setTimestamp(ulong(100000 + m_clock.elapsed()));
        QCoreApplication::sendEvent(m_window.get(), &ev);
    }
    double real(const char *name, QObject *on = nullptr) const
    {
        return (on ? on : m_view)->property(name).toDouble();
    }
    bool waitUntilStill(QObject *view = nullptr)
    {
        QObject *v = view ? view : m_view;
        return QTest::qWaitFor([v] { return !v->property("moving").toBool(); }, 10000 * Slowdown);
    }

private Q_SLOTS:
    void initTestCase()
    {
        QVERIFY(SmoothScroll::instance());
        QVERIFY(!SmoothScroll::switchedOff());
        m_clock.start();
    }

    // ---- SmoothScrollAxis ------------------------------------------------------------------------

    void axisOneNotchFromRest()
    {
        SmoothScrollAxis a;
        const auto f = a.add(1000, 500, 57, 0, 10000, false);
        QVERIFY(f);
        QCOMPARE(f->distance, 57.0);
        QCOMPARE(f->durationMs, SmoothScrollAxis::MaxDurationMs); // the first notch: 200 ms
        QCOMPARE(f->velocity, 2 * 57 / 0.2);                      // even deceleration: v = 2d / T
        QCOMPARE(f->deceleration, f->velocity * f->velocity / (2 * 57));
        QCOMPARE(a.target(), 557.0);
        QVERIFY(a.isActive());
        // Halfway through its time it moves at half its speed, at the end it stands.
        QCOMPARE(a.velocityAt(1100), f->velocity / 2);
        QCOMPARE(a.velocityAt(1200), 0.0);
        QCOMPARE(a.velocityAt(1500), 0.0);
        // Upwards.
        SmoothScrollAxis up;
        const auto u = up.add(0, 500, -57, 0, 10000, false);
        QVERIFY(u && u->velocity < 0 && u->distance == -57);
    }

    void axisNotchesAddUp_data()
    {
        QTest::addColumn<int>("intervalMs");
        QTest::addColumn<double>("longestMs");
        QTest::newRow("slow wheel, 150 ms") << 150 << 200.0; // 2 × 150 → at most 200
        QTest::newRow("60 ms") << 60 << 120.0;
        QTest::newRow("fast wheel, 20 ms") << 20 << 50.0; // 2 × 20 → at least 50
        QTest::newRow("hi-res wheel, 4 ms") << 4 << 50.0;
    }

    void axisNotchesAddUp()
    {
        QFETCH(int, intervalMs);
        QFETCH(double, longestMs);
        SmoothScrollAxis a;
        double position = 1000;
        std::optional<SmoothScrollAxis::Flick> f;
        for (int i = 0; i < 8; ++i) {
            const qint64 t = qint64(i) * intervalMs;
            // The view moved as planned since the last notch: the rest of the way is v² / 2a.
            const double before = a.velocityAt(t);
            if (f)
                position = a.target() - before * before / (2 * f->deceleration);
            QVERIFY(position <= a.target() || !f);
            f = a.add(t, position, 57, 0, 1e9, true);
            QVERIFY(f);
            // A notch never slows the scroll down, and every notch counts exactly once.
            QVERIFY2(f->velocity >= before - 1e-9, qPrintable(u"notch %1: %2 < %3"_s.arg(i).arg(f->velocity).arg(before)));
            QCOMPARE(a.target(), 1000 + 57.0 * (i + 1));
            // From the fourth notch on the duration follows the notches' rate (or less: the
            // velocity it already had).
            if (i >= 3)
                QVERIFY2(f->durationMs <= longestMs + 1e-9, qPrintable(u"notch %1: %2 ms"_s.arg(i).arg(f->durationMs)));
        }
    }

    void axisDurationFollowsRate()
    {
        // Notches 20 ms apart: the history from rest (100 ms apart) fades out over three notches.
        SmoothScrollAxis a;
        QList<double> durations;
        for (int i = 0; i < 5; ++i) {
            const qint64 t = qint64(i) * 20;
            const auto f = a.add(t, double(i) * 10, 57, 0, 1e9, true);
            QVERIFY(f);
            durations.append(f->durationMs);
        }
        // 200, (20+100+100)/3·2 = 146.7, (20+20+100)/3·2 = 93.3, 2·20 = 40 → 50, 50.
        QCOMPARE(qRound(durations.at(0)), 200);
        QVERIFY(durations.at(1) <= 147 + 0.5);
        QVERIFY(durations.at(2) <= 94);
        QVERIFY(durations.at(3) <= 50 + 1e-9);
        QVERIFY(durations.at(4) <= 50 + 1e-9);
    }

    void axisBounds()
    {
        SmoothScrollAxis a;
        // Clamped to the end.
        auto f = a.add(0, 980, 57, 0, 1000, false);
        QVERIFY(f);
        QCOMPARE(f->distance, 20.0);
        QCOMPARE(a.target(), 1000.0);
        // Already there: nothing to do, and the scroll is over.
        f = a.add(10, 1000, 57, 0, 1000, true);
        QVERIFY(!f);
        QVERIFY(!a.isActive());
        // Turning back from a target clamped at the end starts from that end, not beyond it.
        f = a.add(20, 1000, -57, 0, 1000, false);
        QVERIFY(f);
        QCOMPARE(a.target(), 943.0);
        // The beginning; a view shorter than its content's bounds (highest < lowest).
        SmoothScrollAxis b;
        f = b.add(0, 10, -57, 0, 1000, false);
        QVERIFY(f && f->distance == -10);
        SmoothScrollAxis c;
        QVERIFY(!c.add(0, 0, 57, 0, -50, false));
    }

    void axisReverses()
    {
        SmoothScrollAxis a;
        QVERIFY(a.add(0, 0, 57, 0, 1e9, false));
        QVERIFY(a.add(20, 10, 57, 0, 1e9, true)); // heading for 114
        // A notch the other way: back from where it was heading, at once in the other direction.
        const auto f = a.add(40, 30, -57, 0, 1e9, true);
        QVERIFY(f);
        QCOMPARE(a.target(), 57.0);
        QVERIFY(f->velocity > 0); // 57 is still ahead of 30: the same direction, slower
        const auto g = a.add(60, 40, -57, 0, 1e9, true);
        QVERIFY(g);
        QCOMPARE(a.target(), 0.0);
        QVERIFY(g->velocity < 0);
        // Not running any more (the view stopped): from the position.
        const auto h = a.add(500, 300, 57, 0, 1e9, false);
        QVERIFY(h);
        QCOMPARE(a.target(), 357.0);
        QCOMPARE(h->durationMs, SmoothScrollAxis::MaxDurationMs);
    }

    // ---- Views -----------------------------------------------------------------------------------

    void oneNotch_data()
    {
        QTest::addColumn<int>("angle");
        QTest::newRow("down") << -120;
        QTest::newRow("up") << 120;
        QTest::newRow("hi-res eighth") << -15;
    }

    void oneNotch()
    {
        QFETCH(int, angle);
        QVERIFY(makeScene(u"vertical"_s));
        m_view->setProperty("contentY", 40000.0);
        const double start = real("contentY");
        const int handled = SmoothScroll::instance()->wheelEvents();
        // Sampled every 4 ms (Flickable is a private class: no member pointer to contentYChanged).
        QList<double> positions;
        QTimer sampler;
        sampler.setInterval(4);
        QObject::connect(&sampler, &QTimer::timeout, this, [&] { positions.append(real("contentY")); });
        sampler.start();
        wheel(QPoint(0, angle));
        QCOMPARE(SmoothScroll::instance()->wheelEvents(), handled + 1);
        QVERIFY(m_view->property("moving").toBool()); // the scroll bar shows, movementStarted…
        QVERIFY(m_view->property("flicking").toBool());
        QVERIFY(waitUntilStill());
        sampler.stop();
        const double expected = start - angle / 120.0 * notch();
        QVERIFY2(std::abs(real("contentY") - expected) <= 1,
                 qPrintable(u"%1, expected %2"_s.arg(real("contentY")).arg(expected)));
        // Eased into it: monotonic, never past the end.
        for (int i = 1; i < positions.size(); ++i) {
            if (angle < 0)
                QVERIFY(positions.at(i) >= positions.at(i - 1) && positions.at(i) <= expected + 1);
            else
                QVERIFY(positions.at(i) <= positions.at(i - 1) && positions.at(i) >= expected - 1);
        }
    }

    void fastWheelNoAcceleration_data()
    {
        QTest::addColumn<int>("intervalMs");
        QTest::newRow("150 ms") << 150;
        QTest::newRow("40 ms") << 40;
        QTest::newRow("10 ms") << 10;
    }

    void fastWheelNoAcceleration()
    {
        // Qt's wheel flicks lost distance on a fast-turning wheel (qtWheelLosesNotches).
        QFETCH(int, intervalMs);
        QVERIFY(makeScene(u"vertical"_s));
        m_view->setProperty("contentY", 40000.0);
        const double start = real("contentY");
        for (int i = 0; i < 6; ++i) {
            wheel(QPoint(0, -120));
            QTest::qWait(intervalMs);
        }
        QVERIFY(waitUntilStill());
        const double expected = start + 6 * notch();
        QVERIFY2(std::abs(real("contentY") - expected) <= 1,
                 qPrintable(u"%1, expected %2"_s.arg(real("contentY")).arg(expected)));
    }

    void qtWheelLosesNotches_data() { fastWheelNoAcceleration_data(); }

    void qtWheelLosesNotches()
    {
        // Pins down Qt 6.11's own wheel (the filter removed): when this fails after a Qt update,
        // Qt changed its wheel flicks, and docs/UI.md "Wheel and scroll bar" needs a look.
        QFETCH(int, intervalMs);
        QVERIFY(makeScene(u"vertical"_s));
        m_view->setProperty("contentY", 40000.0);
        const double start = real("contentY");
        QCoreApplication::instance()->removeEventFilter(SmoothScroll::instance());
        for (int i = 0; i < 6; ++i) {
            wheel(QPoint(0, -120));
            QTest::qWait(intervalMs);
        }
        const bool still = waitUntilStill();
        QCoreApplication::instance()->installEventFilter(SmoothScroll::instance());
        QVERIFY(still);
        const double qtNotch = QGuiApplication::styleHints()->wheelScrollLines() * 24;
        const double moved = real("contentY") - start;
        qInfo("Qt: 6 notches %d ms apart: %.0f px (%.0f per notch at rest)", intervalMs, moved, qtNotch);
        // Notches close together lose most of their distance (measured: 118 px for 6 × 72 at
        // 10 ms, 284 px at 40 ms, 420 px at 150 ms).
        if (intervalMs <= 40)
            QVERIFY2(moved < 0.8 * 6 * qtNotch, qPrintable(QString::number(moved)));
    }

    void stopsAtTheEnds()
    {
        QVERIFY(makeScene(u"vertical"_s));
        m_view->setProperty("contentY", 30.0);
        wheel(QPoint(0, 120));
        QVERIFY(waitUntilStill());
        QCOMPARE(real("contentY"), real("originY"));
        QVERIFY(m_view->property("atYBeginning").toBool());
        // At the beginning a notch upwards is left to Qt (it passes it on): nothing moves.
        const int handled = SmoothScroll::instance()->wheelEvents();
        wheel(QPoint(0, 120));
        QCOMPARE(SmoothScroll::instance()->wheelEvents(), handled);
        QVERIFY(waitUntilStill());
        QCOMPARE(real("contentY"), real("originY"));
    }

    void endPassesOnToOuterView()
    {
        QVERIFY(makeScene(u"nested"_s));
        auto *inner = m_view->findChild<QQuickItem *>(u"inner"_s);
        QVERIFY(inner);
        // The inner list scrolls first…
        wheel(QPoint(0, -120));
        QVERIFY(waitUntilStill(inner));
        QVERIFY(real("contentY", inner) > 0);
        QCOMPARE(real("contentY"), 0.0);
        // …and at its end the wheel scrolls the view around it, smoothly as well.
        inner->setProperty("contentY", 20 * 40 - 400.0);
        QVERIFY(inner->property("atYEnd").toBool());
        const int handled = SmoothScroll::instance()->wheelEvents();
        wheel(QPoint(0, -120));
        QCOMPARE(SmoothScroll::instance()->wheelEvents(), handled + 1);
        QVERIFY(waitUntilStill());
        QVERIFY2(std::abs(real("contentY") - notch()) <= 1, qPrintable(QString::number(real("contentY"))));
        QCOMPARE(real("contentY", inner), 400.0);
    }

    void leftToQt_data()
    {
        QTest::addColumn<QString>("kind");
        QTest::addColumn<int>("modifiers");
        QTest::addColumn<int>("phase");
        QTest::newRow("Ctrl+wheel") << u"vertical"_s << int(Qt::ControlModifier) << int(Qt::NoScrollPhase);
        QTest::newRow("Alt+wheel") << u"vertical"_s << int(Qt::AltModifier) << int(Qt::NoScrollPhase);
        QTest::newRow("touchpad") << u"vertical"_s << int(Qt::NoModifier) << int(Qt::ScrollUpdate);
        QTest::newRow("snapping list") << u"snap"_s << int(Qt::NoModifier) << int(Qt::NoScrollPhase);
    }

    void leftToQt()
    {
        QFETCH(QString, kind);
        QFETCH(int, modifiers);
        QFETCH(int, phase);
        QVERIFY(makeScene(kind));
        m_view->setProperty("contentY", 4000.0);
        const int handled = SmoothScroll::instance()->wheelEvents();
        if (phase == Qt::ScrollUpdate)
            wheel({}, Qt::NoModifier, Qt::ScrollBegin);
        wheel(QPoint(0, -120), Qt::KeyboardModifiers(modifiers), Qt::ScrollPhase(phase),
              phase == Qt::ScrollUpdate ? QPoint(0, -30) : QPoint());
        if (phase == Qt::ScrollUpdate)
            wheel({}, Qt::NoModifier, Qt::ScrollEnd);
        QCOMPARE(SmoothScroll::instance()->wheelEvents(), handled);
        QVERIFY(waitUntilStill());
    }

    void shiftScrollsSideways()
    {
        QVERIFY(makeScene(u"horizontal"_s));
        m_view->setProperty("contentX", 1000.0);
        wheel(QPoint(0, -120), Qt::ShiftModifier);
        QVERIFY(waitUntilStill());
        QVERIFY2(std::abs(real("contentX") - (1000 + notch())) <= 1, qPrintable(QString::number(real("contentX"))));
        QCOMPARE(real("contentY"), 0.0);
        // A tilting wheel (angleDelta.x) as well.
        wheel(QPoint(120, 0));
        QVERIFY(waitUntilStill());
        QVERIFY2(std::abs(real("contentX") - 1000) <= 1, qPrintable(QString::number(real("contentX"))));
    }

    void scrollByPages()
    {
        QVERIFY(makeScene(u"vertical"_s));
        m_view->setProperty("contentY", 4000.0);
        QVERIFY(SmoothScroll::instance()->scrollBy(m_view, 0, 700));
        QVERIFY(m_view->property("moving").toBool());
        // A second page while the first is under way adds to it.
        QTest::qWait(30);
        QVERIFY(SmoothScroll::instance()->scrollBy(m_view, 0, 700));
        QVERIFY(waitUntilStill());
        QVERIFY2(std::abs(real("contentY") - 5400) <= 1, qPrintable(QString::number(real("contentY"))));
        // Not past the beginning; nothing left to scroll there.
        QVERIFY(SmoothScroll::instance()->scrollBy(m_view, 0, -100000));
        QVERIFY(waitUntilStill());
        QCOMPARE(real("contentY"), real("originY"));
        QVERIFY(!SmoothScroll::instance()->scrollBy(m_view, 0, -700));
        // Not a Flickable.
        QVERIFY(!SmoothScroll::instance()->scrollBy(m_window->contentItem(), 0, 700));
        QVERIFY(!SmoothScroll::instance()->scrollBy(nullptr, 0, 700));
    }

    void pressStopsIt()
    {
        // A drag or a new position stops the scroll as it stops a flick; the next notch starts
        // from where the view is.
        QVERIFY(makeScene(u"vertical"_s));
        m_view->setProperty("contentY", 4000.0);
        wheel(QPoint(0, -120));
        wheel(QPoint(0, -120));
        m_view->setProperty("contentY", 8000.0); // positionViewAtIndex, the scroll bar dragged
        QVERIFY(!m_view->property("flicking").toBool());
        wheel(QPoint(0, -120));
        QVERIFY(waitUntilStill());
        QVERIFY2(std::abs(real("contentY") - (8000 + notch())) <= 1, qPrintable(QString::number(real("contentY"))));
    }
};

QTEST_MAIN(TestSmoothScroll)
#include "tst_smooth_scroll.moc"
