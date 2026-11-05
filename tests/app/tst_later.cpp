// Tests for Later (QML type `Later`, src/app/ui/later.h): Qt.callLater() without a closure — one
// call however often it is asked for, a posted event that runs ahead of the timers due by then,
// stop() forgets it, a call asked for from its own handler comes on the next turn.
#include "later.h"

#include <QCoreApplication>
#include <QSignalSpy>
#include <QTest>
#include <QThread>
#include <QTimer>

using namespace Qt::StringLiterals;
using brulion::ui::Later;

class TstLater : public QObject {
    Q_OBJECT

private Q_SLOTS:
    void oneCallForManyRequests()
    {
        Later later;
        QSignalSpy spy(&later, &Later::triggered);
        later.restart();
        later.restart();
        later.start();
        QVERIFY(later.running());
        QCOMPARE(spy.count(), 0); // not right away
        QCoreApplication::processEvents();
        QCOMPARE(spy.count(), 1);
        QVERIFY(!later.running());
        QCoreApplication::processEvents();
        QCOMPARE(spy.count(), 1);
    }

    void stopForgetsTheCall()
    {
        Later later;
        QSignalSpy spy(&later, &Later::triggered);
        QSignalSpy running(&later, &Later::runningChanged);
        later.restart();
        later.stop();
        QVERIFY(!later.running());
        QCoreApplication::processEvents();
        QCOMPARE(spy.count(), 0);
        QCOMPARE(running.count(), 2);
        // Asked for again after stop(): one call (the forgotten one stays forgotten).
        later.restart();
        QCoreApplication::processEvents();
        QCOMPARE(spy.count(), 1);
    }

    void aheadOfTheTimersDue()
    {
        // A zero timer started first, already due: Later's call still comes first (a posted
        // event, like Qt.callLater) — a Timer { interval: 0 } would come after it.
        QStringList order;
        QTimer timer;
        timer.setSingleShot(true);
        timer.setInterval(0);
        connect(&timer, &QTimer::timeout, this, [&] { order.append(u"timer"_s); });
        Later later;
        connect(&later, &Later::triggered, this, [&] { order.append(u"later"_s); });
        timer.start();
        QThread::msleep(5); // the timer is overdue (no events processed meanwhile)
        later.restart();
        QTRY_COMPARE(order.size(), 2);
        QCOMPARE(order, (QStringList{u"later"_s, u"timer"_s}));
    }

    void askedFromItsHandler()
    {
        Later later;
        int calls = 0;
        connect(&later, &Later::triggered, this, [&] {
            if (++calls == 1)
                later.restart(); // the next turn, not now
        });
        later.restart();
        QCoreApplication::processEvents();
        QCOMPARE(calls, 1);
        QVERIFY(later.running());
        QCoreApplication::processEvents();
        QCOMPARE(calls, 2);
    }

    void destroyedWhilePending()
    {
        auto *later = new Later;
        QSignalSpy spy(later, &Later::triggered);
        later->restart();
        delete later; // the queued call goes with it
        QCoreApplication::processEvents();
        QCOMPARE(spy.count(), 0);
    }
};

QTEST_GUILESS_MAIN(TstLater)
#include "tst_later.moc"
