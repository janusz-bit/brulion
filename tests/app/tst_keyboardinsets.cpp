// Tests for KeyboardInsets (src/app/ui/keyboardinsets.h): the overlap of a keyboard rectangle
// with a window (native and logical pixels, split screens, malformed input) and the choice of the
// source — a simulated keyboard over the platform's report over Qt's input method —, the signals
// and the clamping. GUI-free (QTEST_GUILESS_MAIN).
#include "keyboardinsets.h"

#include <QSignalSpy>
#include <QTest>

#include <cmath>
#include <limits>

using namespace Qt::StringLiterals;
using brulion::ui::KeyboardInsets;
using brulion::ui::keyboardOverlap;

namespace {

constexpr qreal kNaN = std::numeric_limits<qreal>::quiet_NaN();
constexpr qreal kInf = std::numeric_limits<qreal>::infinity();

// A 1080x2400 phone at 2.625: 411.43 x 914.29 logical pixels.
constexpr qreal kDpr = 2.625;
const QRect kPhone(0, 0, 411, 914);

} // namespace

class TestKeyboardInsets : public QObject {
    Q_OBJECT

private Q_SLOTS:
    void overlap_data();
    void overlap();
    void toLogical_data();
    void toLogical();

    void defaults();
    void inputMethodFallback();
    void platformWinsOverInputMethod();
    void simulatedWinsOverEverything();
    void slideFrameByFrame();
    void signalsOnlyOnChange();
    void clampedToTheWindow();
    void malformedPlatformValues_data();
    void malformedPlatformValues();
    void instanceIsTheFirstOne();
};

void TestKeyboardInsets::overlap_data()
{
    QTest::addColumn<QRectF>("keyboard");
    QTest::addColumn<bool>("native");
    QTest::addColumn<qreal>("dpr");
    QTest::addColumn<QRect>("window");
    QTest::addColumn<bool>("visible");
    QTest::addColumn<qreal>("expected");

    // Logical keyboards (desktop virtual keyboards, the browser).
    QTest::newRow("schowana klawiatura → 0") << QRectF(0, 600, 411, 314) << false << 1.0 << kPhone << false << 0.0;
    QTest::newRow("klawiatura na dole okna") << QRectF(0, 600, 411, 314) << false << 1.0 << kPhone << true << 314.0;
    QTest::newRow("pusty prostokąt") << QRectF() << false << 1.0 << kPhone << true << 0.0;
    QTest::newRow("zerowa wysokość") << QRectF(0, 914, 411, 0) << false << 1.0 << kPhone << true << 0.0;
    QTest::newRow("ujemna szerokość") << QRectF(411, 600, -411, 314) << false << 1.0 << kPhone << true << 0.0;
    // Android: physical pixels of the screen (a 900 px tall keyboard on a 2400 px screen).
    QTest::newRow("Android, natywne piksele, dpr 2,625")
            << QRectF(0, 1500, 1080, 900) << true << kDpr << QRect(0, 0, 411, 914) << true << 914 - 1500 / kDpr;
    QTest::newRow("okno już zmniejszone nad klawiaturą")
            << QRectF(0, 1500, 1080, 900) << true << kDpr << QRect(0, 0, 411, int(1500 / kDpr)) << true << 0.0;
    QTest::newRow("okno kończy się w połowie klawiatury")
            << QRectF(0, 600, 411, 314) << false << 1.0 << QRect(0, 0, 411, 700) << true << 100.0;
    // Split screen: the app's window is the bottom half, starting 460 px down.
    QTest::newRow("podzielony ekran — okno na dole")
            << QRectF(0, 600, 411, 314) << false << 1.0 << QRect(0, 460, 411, 454) << true << 314.0;
    QTest::newRow("podzielony ekran — okno na górze, klawiatura pod nim")
            << QRectF(0, 600, 411, 314) << false << 1.0 << QRect(0, 0, 411, 450) << true << 0.0;
    QTest::newRow("klawiatura większa niż okno")
            << QRectF(0, 100, 411, 2000) << false << 1.0 << QRect(0, 460, 411, 454) << true << 454.0;
    QTest::newRow("klawiatura obok okna (inny ekran)")
            << QRectF(500, 600, 411, 314) << false << 1.0 << kPhone << true << 0.0;
    QTest::newRow("landscape: 915x412, klawiatura 180 px")
            << QRectF(0, 232, 915, 180) << false << 1.0 << QRect(0, 0, 915, 412) << true << 180.0;
    QTest::newRow("landscape natywnie") << QRectF(0, 609, 2400, 471) << true << kDpr << QRect(0, 0, 914, 411) << true
                                        << 411 - 609 / kDpr;
    // Malformed: never a crash, never a negative or endless value.
    QTest::newRow("ujemne y") << QRectF(0, -20, 411, 314) << false << 1.0 << kPhone << true << 0.0;
    QTest::newRow("NaN") << QRectF(0, kNaN, 411, 314) << false << 1.0 << kPhone << true << 0.0;
    QTest::newRow("nieskończoność") << QRectF(0, 600, 411, kInf) << false << 1.0 << kPhone << true << 0.0;
    QTest::newRow("dpr 0 liczy się jak 1") << QRectF(0, 600, 411, 314) << true << 0.0 << kPhone << true << 314.0;
    QTest::newRow("dpr NaN liczy się jak 1") << QRectF(0, 600, 411, 314) << true << kNaN << kPhone << true << 314.0;
    QTest::newRow("puste okno") << QRectF(0, 600, 411, 314) << false << 1.0 << QRect() << true << 0.0;
}

void TestKeyboardInsets::overlap()
{
    QFETCH(QRectF, keyboard);
    QFETCH(bool, native);
    QFETCH(qreal, dpr);
    QFETCH(QRect, window);
    QFETCH(bool, visible);
    QFETCH(qreal, expected);
    const qreal got = keyboardOverlap(keyboard, native, dpr, window, visible);
    QVERIFY2(std::isfinite(got), "finite");
    QVERIFY2(qAbs(got - expected) < 0.01, qPrintable(u"%1 != %2"_s.arg(got).arg(expected)));
}

void TestKeyboardInsets::toLogical_data()
{
    QTest::addColumn<qreal>("native");
    QTest::addColumn<qreal>("dpr");
    QTest::addColumn<qreal>("expected");
    QTest::newRow("900 px / 2,625") << 900.0 << kDpr << 900 / kDpr;
    QTest::newRow("dpr 1") << 320.0 << 1.0 << 320.0;
    QTest::newRow("zero") << 0.0 << kDpr << 0.0;
    QTest::newRow("ujemne → 0") << -40.0 << kDpr << 0.0;
    QTest::newRow("NaN → 0") << kNaN << kDpr << 0.0;
    QTest::newRow("nieskończoność → 0") << kInf << kDpr << 0.0;
    QTest::newRow("zły dpr liczy się jak 1") << 300.0 << -2.0 << 300.0;
}

void TestKeyboardInsets::toLogical()
{
    QFETCH(qreal, native);
    QFETCH(qreal, dpr);
    QFETCH(qreal, expected);
    QCOMPARE(KeyboardInsets::toLogical(native, dpr), expected);
}

void TestKeyboardInsets::defaults()
{
    KeyboardInsets insets;
    QCOMPARE(insets.overlap(), 0.0);
    QVERIFY(!insets.shown());
    QVERIFY(!insets.animating());
    QCOMPARE(insets.simulated(), -1.0);
    QCOMPARE(insets.sourceName(), u"none"_s);
    QVERIFY(!insets.hasPlatformSource());
}

void TestKeyboardInsets::inputMethodFallback()
{
    KeyboardInsets insets;
    insets.setInputMethodState(314, true);
    QCOMPARE(insets.overlap(), 314.0);
    QVERIFY(insets.shown());
    QCOMPARE(insets.sourceName(), u"inputMethod"_s);
    insets.setInputMethodState(0, false);
    QCOMPARE(insets.overlap(), 0.0);
    QVERIFY(!insets.shown());
    QCOMPARE(insets.sourceName(), u"none"_s);
}

void TestKeyboardInsets::platformWinsOverInputMethod()
{
    KeyboardInsets insets;
    insets.setInputMethodState(500, true);
    QCOMPARE(insets.overlap(), 500.0);
    // The platform reports (a resized window: 0 although the keyboard is up)…
    insets.setPlatformState(0, true, false);
    QCOMPARE(insets.sourceName(), u"platform"_s);
    QCOMPARE(insets.overlap(), 0.0);
    QVERIFY(insets.shown());
    QVERIFY(insets.hasPlatformSource());
    // …and Qt's rectangle no longer counts, even when the platform says hidden.
    insets.setInputMethodState(450, true);
    QCOMPARE(insets.overlap(), 0.0);
    insets.setPlatformState(0, false, false);
    insets.setInputMethodState(450, true);
    QCOMPARE(insets.overlap(), 0.0);
    QVERIFY(!insets.shown());
    QCOMPARE(insets.sourceName(), u"platform"_s);
}

void TestKeyboardInsets::simulatedWinsOverEverything()
{
    KeyboardInsets insets;
    insets.setPlatformState(342.86, true, false);
    QSignalSpy simulated(&insets, &KeyboardInsets::simulatedChanged);
    insets.setSimulated(300);
    QCOMPARE(simulated.count(), 1);
    QCOMPARE(insets.overlap(), 300.0);
    QCOMPARE(insets.sourceName(), u"simulated"_s);
    // Platform reports change nothing while simulated.
    insets.setPlatformState(100, true, true);
    QCOMPARE(insets.overlap(), 300.0);
    QVERIFY(!insets.animating());
    // A simulated height of 0: no keyboard (and no real one either).
    insets.setSimulated(0);
    QCOMPARE(insets.overlap(), 0.0);
    QVERIFY(!insets.shown());
    QCOMPARE(insets.sourceName(), u"simulated"_s);
    // Off: the platform's latest report again.
    insets.setSimulated(-1);
    QCOMPARE(insets.overlap(), 100.0);
    QVERIFY(insets.animating());
    QCOMPARE(insets.sourceName(), u"platform"_s);
    insets.setSimulated(kNaN); // also off
    QCOMPARE(insets.simulated(), -1.0);
}

void TestKeyboardInsets::slideFrameByFrame()
{
    KeyboardInsets insets;
    QSignalSpy overlap(&insets, &KeyboardInsets::overlapChanged);
    QSignalSpy animating(&insets, &KeyboardInsets::animatingChanged);
    QSignalSpy shown(&insets, &KeyboardInsets::shownChanged);
    // Android 11+: every frame of the slide, then the settled value.
    const QList<qreal> frames{0, 100, 200, 320};
    for (const qreal frame : frames)
        insets.simulate(frame, true);
    QCOMPARE(insets.overlap(), 320.0);
    QVERIFY(insets.animating());
    QVERIFY(insets.shown());
    insets.simulate(320, false);
    QVERIFY(!insets.animating());
    QCOMPARE(insets.overlap(), 320.0);
    QCOMPARE(overlap.count(), 3);   // 100, 200, 320 (0 is no change)
    QCOMPARE(animating.count(), 2); // on, off
    QCOMPARE(shown.count(), 1);
    // The same through the platform.
    KeyboardInsets platform;
    QSignalSpy platformAnimating(&platform, &KeyboardInsets::animatingChanged);
    platform.setPlatformState(0, false, false);
    for (const qreal frame : frames)
        platform.setPlatformState(frame, true, true);
    QVERIFY(platform.animating());
    platform.setPlatformState(320, true, false);
    QVERIFY(!platform.animating());
    QCOMPARE(platform.overlap(), 320.0);
    QCOMPARE(platformAnimating.count(), 2);
    // Hiding: shown goes false at the start of the slide down.
    platform.setPlatformState(200, false, true);
    QVERIFY(!platform.shown());
    platform.setPlatformState(0, false, false);
    QCOMPARE(platform.overlap(), 0.0);
}

void TestKeyboardInsets::signalsOnlyOnChange()
{
    KeyboardInsets insets;
    QSignalSpy overlap(&insets, &KeyboardInsets::overlapChanged);
    QSignalSpy shown(&insets, &KeyboardInsets::shownChanged);
    QSignalSpy source(&insets, &KeyboardInsets::sourceChanged);
    insets.setPlatformState(0, false, false);
    QCOMPARE(overlap.count(), 0);
    QCOMPARE(shown.count(), 0);
    QCOMPARE(source.count(), 1);
    insets.setPlatformState(300, true, false);
    insets.setPlatformState(300, true, false);
    QCOMPARE(overlap.count(), 1);
    QCOMPARE(shown.count(), 1);
    insets.simulate(-1); // was off already
    QCOMPARE(source.count(), 1);
}

void TestKeyboardInsets::clampedToTheWindow()
{
    KeyboardInsets insets;
    insets.setWindowHeight(914);
    insets.setPlatformState(2000, true, false);
    QCOMPARE(insets.overlap(), 914.0);
    // A smaller window (rotated): what is known is clamped again.
    insets.setWindowHeight(411);
    QCOMPARE(insets.overlap(), 411.0);
    insets.setSimulated(500);
    QCOMPARE(insets.overlap(), 411.0);
    // Unknown window height: no clamping.
    insets.setWindowHeight(kNaN);
    insets.setSimulated(500);
    QCOMPARE(insets.overlap(), 500.0);
}

void TestKeyboardInsets::malformedPlatformValues_data()
{
    QTest::addColumn<qreal>("value");
    QTest::newRow("NaN") << kNaN;
    QTest::newRow("nieskończoność") << kInf;
    QTest::newRow("minus nieskończoność") << -kInf;
    QTest::newRow("ujemna") << -120.0;
}

void TestKeyboardInsets::malformedPlatformValues()
{
    QFETCH(qreal, value);
    KeyboardInsets insets;
    insets.setPlatformState(value, true, false);
    QCOMPARE(insets.overlap(), 0.0);
    QVERIFY(insets.shown());
    insets.setInputMethodState(value, true);
    QCOMPARE(insets.overlap(), 0.0);
    insets.simulate(value, true);
    QVERIFY(std::isfinite(insets.overlap()));
    QVERIFY(insets.overlap() >= 0);
}

void TestKeyboardInsets::instanceIsTheFirstOne()
{
    QCOMPARE(KeyboardInsets::instance(), nullptr);
    {
        KeyboardInsets first;
        KeyboardInsets second;
        QCOMPARE(KeyboardInsets::instance(), &first);
    }
    QCOMPARE(KeyboardInsets::instance(), nullptr);
}

QTEST_GUILESS_MAIN(TestKeyboardInsets)
#include "tst_keyboardinsets.moc"
