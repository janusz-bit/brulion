// Tests for GestureInsets (src/app/ui/gestureinsets.h): the system gesture zones at the window's
// edges — the platform's report, a simulation over it and back, malformed values and the signal —
// and the exclusion rectangle (the side panel drawer's handle).
// GUI-free (QTEST_GUILESS_MAIN).
#include "gestureinsets.h"

#include <QSignalSpy>
#include <QTest>

#include <limits>

using brulion::ui::GestureInsets;

namespace {

constexpr qreal kNaN = std::numeric_limits<qreal>::quiet_NaN();
constexpr qreal kInf = std::numeric_limits<qreal>::infinity();

} // namespace

class TestGestureInsets : public QObject {
    Q_OBJECT

private Q_SLOTS:
    void defaults();
    void platform_data();
    void platform();
    void signalOnlyOnChange();
    void simulatedWinsAndGoesBack();
    void simulateOneSide();
    void instance();
    void exclusion_data();
    void exclusion();
    void exclusionSignal();
};

void TestGestureInsets::defaults()
{
    GestureInsets insets;
    QCOMPARE(insets.left(), 0.0);
    QCOMPARE(insets.right(), 0.0);
    QVERIFY(!insets.simulated());
}

void TestGestureInsets::platform_data()
{
    QTest::addColumn<qreal>("left");
    QTest::addColumn<qreal>("right");
    QTest::addColumn<qreal>("expectedLeft");
    QTest::addColumn<qreal>("expectedRight");

    QTest::newRow("gesture navigation") << 30.0 << 30.0 << 30.0 << 30.0;
    QTest::newRow("button navigation") << 0.0 << 0.0 << 0.0 << 0.0;
    QTest::newRow("high sensitivity, one side") << 49.5 << 18.0 << 49.5 << 18.0;
    QTest::newRow("negative") << -12.0 << -1.0 << 0.0 << 0.0;
    QTest::newRow("not a number") << kNaN << 24.0 << 0.0 << 24.0;
    QTest::newRow("infinite") << kInf << -kInf << 0.0 << 0.0;
}

void TestGestureInsets::platform()
{
    QFETCH(qreal, left);
    QFETCH(qreal, right);
    QFETCH(qreal, expectedLeft);
    QFETCH(qreal, expectedRight);

    GestureInsets insets;
    insets.setPlatformInsets(left, right);
    QCOMPARE(insets.left(), expectedLeft);
    QCOMPARE(insets.right(), expectedRight);
    QVERIFY(!insets.simulated());
}

void TestGestureInsets::signalOnlyOnChange()
{
    GestureInsets insets;
    QSignalSpy spy(&insets, &GestureInsets::changed);
    insets.setPlatformInsets(0, 0);
    QCOMPARE(spy.count(), 0);
    insets.setPlatformInsets(30, 30);
    QCOMPARE(spy.count(), 1);
    insets.setPlatformInsets(30, 30); // the keyboard's slide reports the same insets every frame
    QCOMPARE(spy.count(), 1);
    insets.setPlatformInsets(30, 0);
    QCOMPARE(spy.count(), 2);
    insets.setPlatformInsets(kNaN, -5); // malformed: 0, a change
    QCOMPARE(spy.count(), 3);
    QCOMPARE(insets.left(), 0.0);
}

void TestGestureInsets::simulatedWinsAndGoesBack()
{
    GestureInsets insets;
    QSignalSpy spy(&insets, &GestureInsets::changed);
    insets.setPlatformInsets(30, 30);
    QCOMPARE(spy.count(), 1);

    insets.simulate(32, 40);
    QCOMPARE(spy.count(), 2);
    QVERIFY(insets.simulated());
    QCOMPARE(insets.left(), 32.0);
    QCOMPARE(insets.right(), 40.0);

    // The platform keeps reporting underneath: hidden while simulated, no signal.
    insets.setPlatformInsets(24, 24);
    QCOMPARE(spy.count(), 2);
    QCOMPARE(insets.left(), 32.0);

    // A simulation of no zone at all (0, 0) is still a simulation.
    insets.simulate(0, 0);
    QVERIFY(insets.simulated());
    QCOMPARE(insets.left(), 0.0);
    QCOMPARE(spy.count(), 3);

    insets.simulate(-1, -1);
    QVERIFY(!insets.simulated());
    QCOMPARE(insets.left(), 24.0);
    QCOMPARE(insets.right(), 24.0);
    QCOMPARE(spy.count(), 4);

    insets.simulate(kNaN, -kInf); // off already: nothing changes
    QCOMPARE(spy.count(), 4);
}

void TestGestureInsets::simulateOneSide()
{
    GestureInsets insets;
    insets.setPlatformInsets(30, 30);
    insets.simulate(-1, 16); // one side off counts as 0 there, the simulation is on
    QVERIFY(insets.simulated());
    QCOMPARE(insets.left(), 0.0);
    QCOMPARE(insets.right(), 16.0);
}

void TestGestureInsets::instance()
{
    QVERIFY(!GestureInsets::instance());
    {
        GestureInsets first;
        GestureInsets second;
        QCOMPARE(GestureInsets::instance(), &first);
    }
    QVERIFY(!GestureInsets::instance());
}

void TestGestureInsets::exclusion_data()
{
    QTest::addColumn<QRectF>("rect");
    QTest::addColumn<QRectF>("expected");
    QTest::newRow("whole pixels") << QRectF(386, 300, 26, 64) << QRectF(386, 300, 26, 64);
    QTest::newRow("rounded outwards") << QRectF(386.4, 299.6, 25.2, 64.1) << QRectF(386, 299, 26, 65);
    QTest::newRow("empty") << QRectF(10, 10, 0, 40) << QRectF();
    QTest::newRow("negative size") << QRectF(10, 10, -20, 40) << QRectF();
    QTest::newRow("NaN") << QRectF(kNaN, 10, 20, 40) << QRectF();
    QTest::newRow("infinite") << QRectF(0, 0, kInf, 40) << QRectF();
    QTest::newRow("invalid") << QRectF() << QRectF();
}

void TestGestureInsets::exclusion()
{
    QFETCH(QRectF, rect);
    QFETCH(QRectF, expected);
    GestureInsets insets;
    QCOMPARE(insets.exclusion(), QRectF());
    insets.setExclusion(rect);
    QCOMPARE(insets.exclusion(), expected);
}

void TestGestureInsets::exclusionSignal()
{
    GestureInsets insets;
    QSignalSpy spy(&insets, &GestureInsets::exclusionChanged);
    QSignalSpy insetsSpy(&insets, &GestureInsets::changed);
    insets.setExclusion(QRectF(380, 200, 32, 72));
    QCOMPARE(spy.size(), 1);
    insets.setExclusion(QRectF(380.2, 200, 31.8, 72)); // the same pixels
    QCOMPARE(spy.size(), 1);
    insets.setExclusion(QRectF());
    QCOMPARE(spy.size(), 2);
    insets.setExclusion(QRectF(kNaN, 0, 1, 1)); // still none
    QCOMPARE(spy.size(), 2);
    QCOMPARE(insetsSpy.size(), 0); // the zones themselves did not change
}

QTEST_GUILESS_MAIN(TestGestureInsets)
#include "tst_gestureinsets.moc"
