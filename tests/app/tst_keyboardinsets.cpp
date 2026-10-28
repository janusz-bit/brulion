// Tests for KeyboardInsets (src/app/ui/keyboardinsets.h): the overlap of a keyboard rectangle
// with a window (native and logical pixels, split screens, malformed input), the browser's
// keyboard from its viewports (viewportOverlap, ViewportKeyboard: constructed sequences and ones
// recorded in real browsers on the Android emulator, scripts/android-emulator-test.sh
// --web-keyboard) and the choice of the source — a simulated keyboard over the platform's report
// over Qt's input method —, the signals and the clamping. GUI-free (QTEST_GUILESS_MAIN).
#include "keyboardinsets.h"

#include <QSignalSpy>
#include <QTest>

#include <cmath>
#include <limits>

using namespace Qt::StringLiterals;
using brulion::ui::KeyboardInsets;
using brulion::ui::keyboardOverlap;
using brulion::ui::viewportOverlap;
using brulion::ui::ViewportKeyboard;
using brulion::ui::ViewportSample;

namespace {

constexpr qreal kNaN = std::numeric_limits<qreal>::quiet_NaN();
constexpr qreal kInf = std::numeric_limits<qreal>::infinity();

// A 1080x2400 phone at 2.625: 411.43 x 914.29 logical pixels.
constexpr qreal kDpr = 2.625;
const QRect kPhone(0, 0, 411, 914);

// A browser's viewports (logical px): `height`, `offsetTop` of the visual viewport, Qt's container.
ViewportSample sample(qreal innerWidth, qreal innerHeight, qreal height, qreal offsetTop, qreal containerHeight,
                      bool touch = true, bool editing = true, bool settled = true, const QString &mode = u"content"_s,
                      qreal scale = 1)
{
    ViewportSample s;
    s.innerWidth = innerWidth;
    s.innerHeight = innerHeight;
    s.height = height;
    s.offsetTop = offsetTop;
    s.scale = scale;
    s.containerHeight = containerHeight;
    s.touch = touch;
    s.editing = editing;
    s.settled = settled;
    s.mode = mode;
    return s;
}

// One step of a viewport sequence: the sample, Qt's window height then, the state expected.
struct Step {
    ViewportSample sample;
    qreal windowHeight = 0;
    qreal overlap = 0;
    bool shown = false;
    bool animating = false;
};

// Shorthands for the sequences: a sample in "content" mode on a touch screen, editing or not,
// settled or still moving, and what ViewportKeyboard must make of it.
Step step(qreal width, qreal inner, qreal height, qreal container, qreal window, bool editing, bool settled,
          qreal overlap, bool shown, bool animating, const QString &mode = u"content"_s, qreal offsetTop = 0,
          bool touch = true)
{
    return {sample(width, inner, height, offsetTop, container, touch, editing, settled, mode), window, overlap,
            shown, animating};
}

} // namespace

Q_DECLARE_METATYPE(QList<Step>)

class TestKeyboardInsets : public QObject {
    Q_OBJECT

private Q_SLOTS:
    void overlap_data();
    void overlap();
    void toLogical_data();
    void toLogical();
    void viewportOverlap_data();
    void viewportOverlap();
    void viewportKeyboardSequence_data();
    void viewportKeyboardSequence();
    void viewportTallestPerWidth();
    void viewportSampleFeedsInsets();
    void viewportSampleWithoutContainer();
    void markedForSelfTests();

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

void TestKeyboardInsets::viewportOverlap_data()
{
    QTest::addColumn<qreal>("innerHeight");
    QTest::addColumn<qreal>("height");
    QTest::addColumn<qreal>("offsetTop");
    QTest::addColumn<qreal>("scale");
    QTest::addColumn<qreal>("containerHeight");
    QTest::addColumn<qreal>("windowHeight");
    QTest::addColumn<qreal>("expected");

    QTest::newRow("komputer, bez klawiatury") << 800.0 << 800.0 << 0.0 << 1.0 << 800.0 << 800.0 << 0.0;
    QTest::newRow("Chrome, resizes-visual, klawiatura 300 px") << 780.0 << 480.0 << 0.0 << 1.0 << 780.0 << 780.0 << 300.0;
    QTest::newRow("Chrome, widok przesunięty do pola (offsetTop 120)")
            << 780.0 << 480.0 << 120.0 << 1.0 << 780.0 << 780.0 << 180.0;
    // brave-browser#60074: Brave's visualViewport.height leaves out its bottom toolbar, which it
    // hid for the keyboard — 376.1 px instead of the 328.1 really covered. Why the page asks for
    // resizes-content (no page API sees the difference).
    QTest::newRow("Brave #60074, resizes-visual (pomiar zgłaszającego)")
            << 762.0 << 385.9 << 0.0 << 1.0 << 762.0 << 762.0 << 376.1;
    QTest::newRow("Brave, resizes-content (Qt jeszcze 762)") << 433.9 << 433.9 << 0.0 << 1.0 << 433.9 << 762.0 << 0.0;
    QTest::newRow("nieaktualne innerHeight w trakcie animacji") << 780.0 << 480.0 << 0.0 << 1.0 << 480.0 << 480.0 << 0.0;
    QTest::newRow("iOS Safari, strona przewinięta o 20 px") << 800.0 << 530.0 << 20.0 << 1.0 << 800.0 << 800.0 << 250.0;
    QTest::newRow("powiększenie gestem ×2") << 800.0 << 400.0 << 0.0 << 2.0 << 800.0 << 800.0 << 0.0;
    QTest::newRow("powiększenie 1,005 (zaokrąglenie skali)") << 780.0 << 480.0 << 0.0 << 1.005 << 780.0 << 780.0 << 300.0;
    QTest::newRow("zaokrąglenie 780,4 / 780") << 780.4 << 780.4 << 0.0 << 1.0 << 780.0 << 780.0 << 0.0;
    QTest::newRow("różnica 1,9 px to jeszcze zaokrąglenie") << 780.0 << 778.1 << 0.0 << 1.0 << 780.0 << 780.0 << 0.0;
    QTest::newRow("różnica 2 px to już klawiatura") << 780.0 << 778.0 << 0.0 << 1.0 << 780.0 << 780.0 << 2.0;
    QTest::newRow("ujemny offsetTop (overscroll Firefoksa)") << 780.0 << 480.0 << -15.0 << 1.0 << 780.0 << 780.0 << 300.0;
    QTest::newRow("klawiatura wyższa niż kontener") << 400.0 << 0.5 << 0.0 << 1.0 << 400.0 << 400.0 << 399.5;
    QTest::newRow("widok poniżej kontenera") << 400.0 << 300.0 << 500.0 << 1.0 << 400.0 << 400.0 << 0.0;
    QTest::newRow("bez rozmiaru kontenera: okno Qt") << 780.0 << 480.0 << 0.0 << 1.0 << 0.0 << 780.0 << 300.0;
    QTest::newRow("bez kontenera i okna: innerHeight") << 780.0 << 480.0 << 0.0 << 1.0 << 0.0 << 0.0 << 300.0;
    QTest::newRow("nic nie wiadomo o wysokości strony") << 0.0 << 480.0 << 0.0 << 1.0 << 0.0 << 0.0 << 0.0;
    // Malformed: never a crash, never a negative or endless value.
    QTest::newRow("NaN wysokości") << 780.0 << kNaN << 0.0 << 1.0 << 780.0 << 780.0 << 0.0;
    QTest::newRow("Infinity offsetTop") << 780.0 << 480.0 << kInf << 1.0 << 780.0 << 780.0 << 0.0;
    QTest::newRow("wysokość 0 po obrocie") << 780.0 << 0.0 << 0.0 << 1.0 << 780.0 << 780.0 << 0.0;
    QTest::newRow("ujemna wysokość") << 780.0 << -300.0 << 0.0 << 1.0 << 780.0 << 780.0 << 0.0;
    QTest::newRow("NaN skali") << 780.0 << 480.0 << 0.0 << kNaN << 780.0 << 780.0 << 0.0;
    QTest::newRow("skala 0") << 780.0 << 480.0 << 0.0 << 0.0 << 780.0 << 780.0 << 0.0;
    QTest::newRow("kontener NaN: okno Qt") << 780.0 << 480.0 << 0.0 << 1.0 << kNaN << 780.0 << 300.0;
    QTest::newRow("okno nieskończone: innerHeight") << 780.0 << 480.0 << 0.0 << 1.0 << -5.0 << kInf << 300.0;
}

void TestKeyboardInsets::viewportOverlap()
{
    QFETCH(qreal, innerHeight);
    QFETCH(qreal, height);
    QFETCH(qreal, offsetTop);
    QFETCH(qreal, scale);
    QFETCH(qreal, containerHeight);
    QFETCH(qreal, windowHeight);
    QFETCH(qreal, expected);
    const ViewportSample s = sample(412, innerHeight, height, offsetTop, containerHeight, true, true, true,
                                    u"visual"_s, scale);
    const qreal got = brulion::ui::viewportOverlap(s, windowHeight);
    QVERIFY2(std::isfinite(got), "finite");
    QVERIFY2(qAbs(got - expected) < 0.01, qPrintable(u"%1 != %2"_s.arg(got).arg(expected)));
}

void TestKeyboardInsets::viewportKeyboardSequence_data()
{
    QTest::addColumn<QList<Step>>("steps");
    const QString content = u"content"_s;
    const QString visual = u"visual"_s;

    // Chrome on a phone, interactive-widget=resizes-content: the page shrinks with the keyboard
    // (the container with it: no overlap), Back hides the keyboard while the block stays edited.
    QTest::newRow("Chrome, resizes-content") << QList<Step>{
            step(412, 780, 780, 780, 780, false, true, 0, false, false),
            step(412, 780, 780, 780, 780, true, false, 0, false, false), // a block tapped: no motion yet
            step(412, 780, 640, 640, 780, true, false, 0, true, true),
            step(412, 640, 480, 480, 640, true, false, 0, true, true),
            step(412, 480, 480, 480, 480, true, true, 0, true, false),
            step(412, 480, 780, 780, 480, true, false, 0, false, true), // Back
            step(412, 780, 780, 780, 780, true, true, 0, false, false),
    };
    // brave-browser#60074 with the page asking for resizes-content (the reporter's numbers: the
    // bottom toolbar hides with the keyboard, the page's own size is right).
    QTest::newRow("Brave, resizes-content (liczby zgłaszającego)") << QList<Step>{
            step(412, 762, 762.3, 762, 762, false, true, 0, false, false),
            step(412, 762, 762.3, 762, 762, true, true, 0, false, false),
            step(412, 433.9, 433.9, 433.9, 762, true, false, 0, true, true),
            step(412, 433.9, 433.9, 433.9, 433.9, true, true, 0, true, false),
    };
    // The same in Brave's default (resizes-visual): the overlap is 48 px too large — the gap the
    // user saw, for the record.
    QTest::newRow("Brave, resizes-visual (błąd #60074)") << QList<Step>{
            step(412, 762, 762.3, 762, 762, true, true, 0, false, false, visual),
            step(412, 762, 385.9, 762, 762, true, true, 376.1, true, false, visual),
    };
    // Chrome in the default mode (old Chrome, ?keyboard=visual): the visual viewport shrinks and
    // pans to Qt's <input> — the overlap ends at the visible bottom either way.
    QTest::newRow("Chrome, resizes-visual, przesunięty widok") << QList<Step>{
            step(412, 780, 780, 780, 780, true, true, 0, false, false, visual),
            step(412, 780, 600, 780, 780, true, false, 180, true, true, visual),
            step(412, 780, 480, 780, 780, true, true, 300, true, false, visual),
            step(412, 780, 480, 780, 780, true, true, 180, true, false, visual, 120),
            step(412, 780, 780, 780, 780, true, true, 0, false, false, visual),
    };
    // A rotated phone: one tallest height per width (landscape starts again).
    QTest::newRow("obrót telefonu") << QList<Step>{
            step(412, 780, 780, 780, 780, true, true, 0, false, false),
            step(915, 360, 360, 360, 360, true, true, 0, false, false),
            step(915, 180, 180, 180, 180, true, true, 0, true, false),
            step(412, 780, 780, 780, 780, true, true, 0, false, false),
            step(412, 480, 480, 480, 480, true, true, 0, true, false),
    };
    // A desktop browser window made 300 px lower: no touch screen, never a keyboard.
    QTest::newRow("komputer: okno zmniejszone o 300 px") << QList<Step>{
            step(1280, 800, 800, 800, 800, true, true, 0, false, false, content, 0, false),
            step(1280, 800, 500, 500, 800, true, false, 0, false, true, content, 0, false),
            step(1280, 500, 500, 500, 500, true, true, 0, false, false, content, 0, false),
    };
    // The browser's address bar comes back (56 px): not a keyboard.
    QTest::newRow("pasek adresu 56 px") << QList<Step>{
            step(412, 836, 836, 836, 836, true, true, 0, false, false),
            step(412, 780, 780, 780, 780, true, true, 0, false, false),
    };
    // The page shrank by 300 px but Qt's <input> has no focus (an accessibility tree, another
    // page element): not Brulion's keyboard.
    QTest::newRow("bez fokusu Qt") << QList<Step>{
            step(412, 780, 780, 780, 780, false, true, 0, false, false),
            step(412, 480, 480, 480, 480, false, true, 0, false, false),
    };
    // A focus change alone (the editor taps another block) is no keyboard motion.
    QTest::newRow("sama zmiana fokusu nie animuje") << QList<Step>{
            step(412, 480, 480, 480, 480, true, true, 0, false, false),
            step(412, 480, 480, 480, 480, false, false, 0, false, false),
            step(412, 480, 480, 480, 480, true, false, 0, false, false),
            step(412, 480, 480, 480, 480, true, true, 0, false, false),
    };
    // The first sample is still moving (no settled one before it): animating.
    QTest::newRow("pierwszy pomiar w ruchu") << QList<Step>{
            step(412, 780, 700, 780, 780, true, false, 80, true, true, visual),
            step(412, 780, 480, 780, 780, true, true, 300, true, false, visual),
    };
    // Malformed samples on the way never stick: the next good one counts.
    QTest::newRow("błędne pomiary po drodze") << QList<Step>{
            step(412, 780, 780, 780, 780, true, true, 0, false, false),
            step(412, 780, kNaN, 780, 780, true, true, 0, false, false),
            step(412, 780, 0, 780, 780, true, true, 0, false, false),
            step(kNaN, 780, -40, kInf, kNaN, true, true, 0, false, false),
            step(412, 480, 480, 480, 480, true, true, 0, true, false),
    };
}

void TestKeyboardInsets::viewportKeyboardSequence()
{
    QFETCH(QList<Step>, steps);
    ViewportKeyboard keyboard;
    for (qsizetype i = 0; i < steps.size(); ++i) {
        const Step &st = steps.at(i);
        const ViewportKeyboard::State got = keyboard.update(st.sample, st.windowHeight);
        const QString where = u"step %1: overlap %2 shown %3 animating %4, expected %5 %6 %7"_s.arg(i)
                                      .arg(got.overlap)
                                      .arg(got.shown)
                                      .arg(got.animating)
                                      .arg(st.overlap)
                                      .arg(st.shown)
                                      .arg(st.animating);
        QVERIFY2(qAbs(got.overlap - st.overlap) < 0.01, qPrintable(where));
        QVERIFY2(got.shown == st.shown, qPrintable(where));
        QVERIFY2(got.animating == st.animating, qPrintable(where));
        // The same sample again: the same state.
        QVERIFY2(keyboard.update(st.sample, st.windowHeight) == got, qPrintable(u"step %1 repeated"_s.arg(i)));
    }
}

void TestKeyboardInsets::viewportTallestPerWidth()
{
    ViewportKeyboard keyboard;
    QCOMPARE(keyboard.tallest(412), 0.0);
    keyboard.update(sample(412, 780, 780, 0, 780), 780);
    keyboard.update(sample(412, 480, 480, 0, 480), 480);
    keyboard.update(sample(915, 360, 360, 0, 360), 360);
    QCOMPARE(keyboard.tallest(412), 780.0);
    QCOMPARE(keyboard.tallest(411.6), 780.0); // rounded widths
    QCOMPARE(keyboard.tallest(915), 360.0);
    // A pinch-zoomed page is no shrink, malformed heights never become the tallest.
    const ViewportKeyboard::State zoomed = keyboard.update(sample(412, 780, 390, 0, 780, true, true, true, u"visual"_s, 2), 780);
    QCOMPARE(zoomed.overlap, 0.0);
    QVERIFY(!zoomed.shown);
    keyboard.update(sample(412, 780, kInf, 0, 780), 780);
    QCOMPARE(keyboard.tallest(412), 780.0);
    keyboard.update(sample(kNaN, 780, 700, 0, 780), 780);
    QCOMPARE(keyboard.tallest(kNaN), 700.0);
    QCOMPARE(keyboard.tallest(412), 780.0);
}

void TestKeyboardInsets::viewportSampleFeedsInsets()
{
    KeyboardInsets insets;
    insets.setWindowHeight(780);
    QSignalSpy overlap(&insets, &KeyboardInsets::overlapChanged);
    QSignalSpy shown(&insets, &KeyboardInsets::shownChanged);
    QSignalSpy animating(&insets, &KeyboardInsets::animatingChanged);
    QSignalSpy source(&insets, &KeyboardInsets::sourceChanged);
    // The input method's rectangle loses to the browser's report from its first sample on.
    insets.setInputMethodState(500, true);
    QCOMPARE(insets.sourceName(), u"inputMethod"_s);
    insets.setViewportSample(sample(412, 780, 780, 0, 780, true, false));
    QCOMPARE(insets.sourceName(), u"platform"_s);
    QVERIFY(insets.hasPlatformSource());
    QCOMPARE(insets.overlap(), 0.0);
    QVERIFY(!insets.shown());
    QCOMPARE(insets.viewportSample().containerHeight, 780.0);
    overlap.clear();
    shown.clear();
    animating.clear();
    source.clear();

    // resizes-visual: a 300 px keyboard sliding in, then settled.
    insets.setViewportSample(sample(412, 780, 600, 0, 780, true, true, false, u"visual"_s));
    QCOMPARE(insets.overlap(), 180.0);
    QVERIFY(insets.shown());
    QVERIFY(insets.animating());
    insets.setViewportSample(sample(412, 780, 480, 0, 780, true, true, false, u"visual"_s));
    insets.setViewportSample(sample(412, 780, 480, 0, 780, true, true, true, u"visual"_s));
    insets.setViewportSample(sample(412, 780, 480, 0, 780, true, true, true, u"visual"_s));
    QCOMPARE(insets.overlap(), 300.0);
    QVERIFY(!insets.animating());
    QCOMPARE(overlap.count(), 2);   // 180, 300
    QCOMPARE(shown.count(), 1);
    QCOMPARE(animating.count(), 2); // on, off
    QCOMPARE(source.count(), 0);

    // A simulated keyboard still wins; off, the browser's latest values are back.
    insets.setSimulated(100);
    QCOMPARE(insets.overlap(), 100.0);
    insets.setViewportSample(sample(412, 780, 780, 0, 780, true, true, true, u"visual"_s));
    QCOMPARE(insets.overlap(), 100.0);
    insets.setSimulated(-1);
    QCOMPARE(insets.overlap(), 0.0);
    QVERIFY(!insets.shown());

    // resizes-content: the window shrinks with the page, the keyboard shows as the shrink.
    insets.setViewportSample(sample(412, 480, 480, 0, 480, true, true, true));
    QCOMPARE(insets.overlap(), 0.0);
    QVERIFY(insets.shown());
    insets.setWindowHeight(480);
    QCOMPARE(insets.overlap(), 0.0);
    QVERIFY(insets.shown());

    // Android's own report afterwards: the browser's sample no longer counts.
    insets.setPlatformState(320, true, false);
    QCOMPARE(insets.overlap(), 320.0);
    insets.setWindowHeight(900);
    QCOMPARE(insets.overlap(), 320.0);
}

void TestKeyboardInsets::viewportSampleWithoutContainer()
{
    // No container height (a page without #screen): Qt's window is the container, and a new window
    // height evaluates the kept sample again.
    KeyboardInsets insets;
    insets.setViewportSample(sample(412, 780, 480, 0, 0, true, true, true, u"visual"_s));
    QCOMPARE(insets.overlap(), 300.0); // innerHeight, the window is not known yet
    insets.setWindowHeight(700);
    QCOMPARE(insets.overlap(), 220.0);
    QVERIFY(insets.shown());
    insets.setWindowHeight(480);
    QCOMPARE(insets.overlap(), 0.0);
    QVERIFY(!insets.shown()); // the visual viewport is the tallest seen: no shrink either
    insets.setWindowHeight(kNaN);
    QCOMPARE(insets.overlap(), 300.0);
}

void TestKeyboardInsets::markedForSelfTests()
{
    KeyboardInsets insets;
    QVERIFY(!insets.marked());
    QSignalSpy marked(&insets, &KeyboardInsets::markedChanged);
    insets.setMarked(true);
    insets.setMarked(true);
    QVERIFY(insets.marked());
    QCOMPARE(marked.count(), 1);
    insets.setMarked(false);
    QCOMPARE(marked.count(), 2);
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
