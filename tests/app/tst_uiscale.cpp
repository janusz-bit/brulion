// The interface scale (src/app/ui/uiscale.h): what "Automatic" picks for a screen, the settings'
// steps, the zoom commands' steps and the factor applied to Qt.
#include "uiscale.h"

#include <QGuiApplication>
#include <QTest>

#include <cmath>

using namespace brulion::ui;

class TestUiScale : public QObject {
    Q_OBJECT

private Q_SLOTS:
    void automatic_data()
    {
        QTest::addColumn<QSize>("screen");
        QTest::addColumn<qreal>("expected");
        QTest::newRow("no screen") << QSize() << 1.0;
        QTest::newRow("laptop 1366×768") << QSize(1366, 728) << 1.0;
        QTest::newRow("1080p (taskbar)") << QSize(1920, 1040) << 1.0;
        QTest::newRow("1200p") << QSize(1920, 1200) << 1.0;
        QTest::newRow("1440p") << QSize(2560, 1400) << 1.15;
        QTest::newRow("ultrawide 3440×1440") << QSize(3440, 1410) << 1.15;
        QTest::newRow("1600p") << QSize(2560, 1600) << 1.25;
        QTest::newRow("Surface 3000×2000 at 100 %") << QSize(3000, 1960) << 1.35;
        QTest::newRow("4K at 100 %") << QSize(3840, 2120) << 1.5;
        QTest::newRow("4K at 200 % (the system scales)") << QSize(1920, 1080) << 1.0;
        QTest::newRow("5K at 100 %") << QSize(5120, 2880) << 1.75;
        QTest::newRow("portrait 1440×2560") << QSize(1440, 2520) << 1.75;
    }
    void automatic()
    {
        QFETCH(QSize, screen);
        QFETCH(qreal, expected);
        QCOMPARE(autoInterfaceScale(screen), expected);
    }

    void cleaning_data()
    {
        QTest::addColumn<qreal>("input");
        QTest::addColumn<qreal>("expected");
        QTest::newRow("automatic") << 0.0 << 0.0;
        QTest::newRow("negative") << -1.5 << 0.0;
        QTest::newRow("not a number") << qreal(std::nan("")) << 0.0;
        QTest::newRow("100 %") << 1.0 << 1.0;
        QTest::newRow("115 %") << 1.15 << 1.15;
        QTest::newRow("rounded to 5 %") << 1.123 << 1.1;
        QTest::newRow("rounded up") << 1.128 << 1.15;
        QTest::newRow("too small") << 0.1 << 0.8;
        QTest::newRow("too large") << 3.0 << 2.0;
        QTest::newRow("infinite") << qreal(INFINITY) << 2.0;
    }
    void cleaning()
    {
        QFETCH(qreal, input);
        QFETCH(qreal, expected);
        QCOMPARE(cleanInterfaceScale(input), expected);
    }

    void effective()
    {
        QCOMPARE(effectiveInterfaceScale(0, 1.15), 1.15);  // "Automatic"
        QCOMPARE(effectiveInterfaceScale(1.25, 1.15), 1.25); // chosen
        QCOMPARE(effectiveInterfaceScale(0, 0), 1.0);        // nothing known
        QCOMPARE(effectiveInterfaceScale(7, 1.0), 2.0);      // a damaged setting, kept in range
    }

    void steps_data()
    {
        QTest::addColumn<qreal>("current");
        QTest::addColumn<int>("steps");
        QTest::addColumn<qreal>("expected");
        QTest::newRow("100 → 110") << 1.0 << 1 << 1.1;
        QTest::newRow("110 → 115") << 1.1 << 1 << 1.15;
        QTest::newRow("115 → 125") << 1.15 << 1 << 1.25;
        QTest::newRow("125 → 115") << 1.25 << -1 << 1.15;
        QTest::newRow("100 → 90") << 1.0 << -1 << 0.9;
        QTest::newRow("two steps") << 1.0 << 2 << 1.15;
        QTest::newRow("largest stays") << 2.0 << 1 << 2.0;
        QTest::newRow("smallest stays") << 0.8 << -1 << 0.8;
        QTest::newRow("far beyond") << 1.0 << 50 << 2.0;
        // Between two steps (a hand-written 1.2): the next one in the asked direction.
        QTest::newRow("between, up") << 1.2 << 1 << 1.25;
        QTest::newRow("between, down") << 1.2 << -1 << 1.15;
        QTest::newRow("no step") << 1.15 << 0 << 1.15;
    }
    void steps()
    {
        QFETCH(qreal, current);
        QFETCH(int, steps);
        QFETCH(qreal, expected);
        QCOMPARE(steppedInterfaceScale(current, steps), expected);
    }

    void applied()
    {
        // Before any window: what QT_SCALE_FACTOR would have been.
        QCOMPARE(appliedInterfaceScale(), 1.0);
        QVERIFY(!applyInterfaceScale(1.0));
        QVERIFY(applyInterfaceScale(1.25));
        QCOMPARE(appliedInterfaceScale(), 1.25);
        QVERIFY(!applyInterfaceScale(1.25));
        QVERIFY(applyInterfaceScale(0)); // none: 1
        QCOMPARE(appliedInterfaceScale(), 1.0);
    }
};

QTEST_MAIN(TestUiScale)
#include "tst_uiscale.moc"
