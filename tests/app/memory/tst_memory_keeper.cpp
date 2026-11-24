// MemoryKeeper (src/app/ui/memorykeeper.h): when it gives memory back — once the app is quiet, at
// the latest a while after the first release, at once when asked to — and PictureDecode
// (src/app/ui/picturedecode.h): the side a picture is decoded at. What the allocator itself gives
// back is tests/platform/tst_processmemory.cpp.
#include "memorykeeper.h"
#include "picturedecode.h"
#include "processmemory.h"

#include <QElapsedTimer>
#include <QSignalSpy>
#include <QTest>
#include <QTimer>

using brulion::ui::MemoryKeeper;
using brulion::ui::PictureDecode;

namespace {

constexpr int Slowdown = BRULION_TEST_SLOWDOWN;

} // namespace

class TestMemoryKeeper : public QObject {
    Q_OBJECT

private:
    bool m_can = false;

private Q_SLOTS:
    void initTestCase()
    {
        // (released() waits for quiet only where the allocator keeps what was freed: glibc.)
        m_can = brulion::platform::canReleaseFreeMemory() && brulion::platform::keepsFreedMemory();
    }

    void nothingWhereNothingCanBeGivenBack()
    {
        if (brulion::platform::canReleaseFreeMemory())
            QSKIP("This platform's allocator gives memory back");
        QVERIFY(!brulion::platform::keepsFreedMemory());
        MemoryKeeper keeper;
        keeper.setQuietMs(1);
        QSignalSpy gave(&keeper, &MemoryKeeper::gaveBack);
        keeper.released();
        QVERIFY(!keeper.pending());
        keeper.releaseNow();
        QVERIFY(!keeper.pending());
        QTest::qWait(50);
        QCOMPARE(gave.size(), 0);
        QCOMPARE(keeper.runs(), 0);
    }

    void onceTheAppIsQuiet()
    {
        if (!m_can)
            QSKIP("This platform's allocator has nothing to give back");
        MemoryKeeper keeper;
        QCOMPARE(keeper.quietMs(), 2000);
        QCOMPARE(keeper.latestMs(), 10000);
        keeper.setQuietMs(60);
        QCOMPARE(keeper.quietMs(), 60);
        QSignalSpy gave(&keeper, &MemoryKeeper::gaveBack);
        QVERIFY(!keeper.pending());
        // A page's pictures let go one after another: one run for all of them.
        for (int i = 0; i < 40; ++i)
            keeper.released();
        QVERIFY(keeper.pending());
        QCOMPARE(keeper.runs(), 0);
        QVERIFY(gave.wait(10000 * Slowdown));
        QCOMPARE(keeper.runs(), 1);
        QVERIFY(!keeper.pending());
        // The process's size before and after, and how long it took.
        const QList<QVariant> row = gave.at(0);
        if (brulion::platform::residentBytes() >= 0) {
            QVERIFY(row.at(0).toLongLong() > 0);
            QVERIFY(row.at(1).toLongLong() > 0);
        } else {
            QCOMPARE(row.at(0).toLongLong(), -1);
            QCOMPARE(row.at(1).toLongLong(), -1);
        }
        QVERIFY(row.at(2).toLongLong() >= 0);
        // Nothing more by itself.
        QTest::qWait(200);
        QCOMPARE(keeper.runs(), 1);
        QCOMPARE(gave.size(), 1);
        // The next release starts over.
        keeper.released();
        QVERIFY(gave.wait(10000 * Slowdown));
        QCOMPARE(keeper.runs(), 2);
    }

    /// Something is let go again and again (a page being scrolled for a minute): the app is
    /// never quiet, yet memory goes back.
    void atTheLatest()
    {
        if (!m_can)
            QSKIP("This platform's allocator has nothing to give back");
        MemoryKeeper keeper;
        keeper.setQuietMs(600000);
        keeper.setLatestMs(150);
        QCOMPARE(keeper.latestMs(), 150);
        QSignalSpy gave(&keeper, &MemoryKeeper::gaveBack);
        QTimer again;
        connect(&again, &QTimer::timeout, &keeper, &MemoryKeeper::released);
        again.start(10);
        keeper.released();
        QVERIFY(gave.wait(10000 * Slowdown));
        again.stop();
        QCOMPARE(keeper.runs(), 1);
    }

    void atOnce()
    {
        if (!m_can)
            QSKIP("This platform's allocator has nothing to give back");
        MemoryKeeper keeper;
        keeper.setQuietMs(600000);
        keeper.setLatestMs(600000);
        QSignalSpy gave(&keeper, &MemoryKeeper::gaveBack);
        // (Also one that was waiting for the app to be quiet.)
        keeper.released();
        keeper.releaseNow();
        QVERIFY(keeper.pending());
        QVERIFY(gave.wait(10000 * Slowdown));
        QCOMPARE(keeper.runs(), 1);
        QVERIFY(!keeper.pending());
    }

    /// What is let go while a run is under way gets a run of its own.
    void releasedDuringARun()
    {
        if (!m_can)
            QSKIP("This platform's allocator has nothing to give back");
        MemoryKeeper keeper;
        keeper.setQuietMs(20);
        QSignalSpy gave(&keeper, &MemoryKeeper::gaveBack);
        keeper.releaseNow();
        keeper.released();
        keeper.releaseNow();
        QTRY_COMPARE_WITH_TIMEOUT(keeper.runs(), 2, 10000 * Slowdown);
        QTest::qWait(100);
        QCOMPARE(keeper.runs(), 2);
        QCOMPARE(gave.size(), 2);
    }

    /// A keeper that goes with a run under way or waiting: nothing reaches it afterwards.
    void goesWhilePending()
    {
        if (!m_can)
            QSKIP("This platform's allocator has nothing to give back");
        for (int i = 0; i < 20; ++i) {
            MemoryKeeper keeper;
            keeper.setQuietMs(1);
            keeper.released();
            if (i % 2)
                keeper.releaseNow();
        }
        QTest::qWait(50);
    }

    // ---- PictureDecode ---------------------------------------------------------------------------

    void decodeSide_data()
    {
        QTest::addColumn<double>("width");
        QTest::addColumn<double>("height");
        QTest::addColumn<double>("ratio");
        QTest::addColumn<int>("side");
        QTest::newRow("nothing shown yet") << 0.0 << 0.0 << 1.0 << 64;
        QTest::newRow("negative") << -5.0 << -5.0 << 1.0 << 64;
        QTest::newRow("an icon") << 16.0 << 16.0 << 1.0 << 64;
        QTest::newRow("exactly the smallest") << 64.0 << 64.0 << 1.0 << 64;
        QTest::newRow("a pixel more") << 65.0 << 40.0 << 1.0 << 128;
        QTest::newRow("a fraction of a pixel more") << 128.2 << 40.0 << 1.0 << 192;
        QTest::newRow("a photo in a note") << 560.0 << 420.0 << 1.0 << 640;
        QTest::newRow("the taller side counts") << 315.0 << 420.0 << 1.0 << 448;
        QTest::newRow("the last small step") << 512.0 << 100.0 << 1.0 << 512;
        QTest::newRow("the first large step") << 513.0 << 100.0 << 1.0 << 640;
        QTest::newRow("on a step") << 1024.0 << 768.0 << 1.0 << 1024;
        QTest::newRow("a phone's screen, 2.625") << 360.0 << 270.0 << 2.625 << 1024; // 945 px
        QTest::newRow("a laptop's screen, 1.25") << 560.0 << 420.0 << 1.25 << 768;   // 700 px
        QTest::newRow("twice") << 560.0 << 420.0 << 2.0 << 1152;                     // 1120 px
        QTest::newRow("just below the cap") << 2000.0 << 10.0 << 1.0 << 2048;
        QTest::newRow("the cap") << 2048.0 << 10.0 << 1.0 << 2048;
        QTest::newRow("beyond the cap") << 5000.0 << 4000.0 << 3.0 << 2048;
        QTest::newRow("no ratio") << 560.0 << 420.0 << 0.0 << 640;
        QTest::newRow("a negative ratio") << 560.0 << 420.0 << -2.0 << 640;
        QTest::newRow("a side that is no number") << qQNaN() << 420.0 << 1.0 << 448;
        QTest::newRow("the other side no number") << 560.0 << qQNaN() << 1.0 << 640;
        QTest::newRow("no numbers at all") << qQNaN() << qQNaN() << 1.0 << 64;
        QTest::newRow("a ratio that is no number") << 560.0 << 420.0 << qQNaN() << 640;
        QTest::newRow("endless") << qInf() << 420.0 << 1.0 << 2048;
    }

    void decodeSide()
    {
        QFETCH(double, width);
        QFETCH(double, height);
        QFETCH(double, ratio);
        QFETCH(int, side);
        QCOMPARE(PictureDecode::sideFor(width, height, ratio), side);
        // What QML calls.
        const PictureDecode decode;
        QCOMPARE(decode.side(width, height, ratio), side);
        QCOMPARE(decode.maxSide(), 2048);
    }

    /// Never less than shown, at most a quarter more from 256 px up (sharp without mipmaps), in
    /// few steps (a window being resized decodes a picture again only now and then).
    void decodeSideSteps()
    {
        int last = 0;
        int steps = 0;
        for (int shown = 1; shown <= 4000; ++shown) {
            const int side = PictureDecode::sideFor(shown, shown * 0.75, 1);
            QVERIFY2(side >= last, qPrintable(QString::number(shown)));
            QVERIFY2(side >= qMin(shown, 2048), qPrintable(QString::number(shown)));
            if (shown >= 256 && shown <= 2048)
                QVERIFY2(side <= shown * 1.25, qPrintable(QString::number(shown)));
            if (side != last)
                ++steps;
            last = side;
        }
        QCOMPARE(last, 2048);
        QCOMPARE(steps, 8 + 12); // 64 … 512 by 64, 640 … 2048 by 128
    }

    void decodeSideWithin_data()
    {
        QTest::addColumn<double>("width");
        QTest::addColumn<int>("limit");
        QTest::addColumn<int>("side");
        QTest::newRow("a small picture in a large window") << 1200.0 << 300 << 300;
        QTest::newRow("a photo fitted into the window") << 1200.0 << 2048 << 1280;
        QTest::newRow("the limit below the smallest side") << 1200.0 << 10 << 64;
        QTest::newRow("no limit") << 1200.0 << 0 << 64;
        QTest::newRow("nothing shown") << 0.0 << 2048 << 64;
    }

    void decodeSideWithin()
    {
        QFETCH(double, width);
        QFETCH(int, limit);
        QFETCH(int, side);
        const PictureDecode decode;
        QCOMPARE(decode.sideWithin(width, width * 0.75, 1, limit), side);
    }
};

QTEST_GUILESS_MAIN(TestMemoryKeeper)
#include "tst_memory_keeper.moc"
