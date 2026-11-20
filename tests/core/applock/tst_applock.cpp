// The core module applock (src/core/applock): when the lock engages — on start, after the
// background, after idle time, on "Lock now" — and when it does not (off, a short absence, input),
// the waits after wrong PINs (also across a restart and a clock set back), and the PIN's stored
// hash (valid PINs only, a wrong one refused, a malformed record matches nothing).
#include "applock/applock.h"

#include <QTest>

using namespace Qt::StringLiterals;
using namespace brulion::applock;

namespace {

constexpr qint64 Minute = 60 * 1000;

Policy on(int backgroundMinutes = 0, int idleMinutes = 0, bool lockOnStart = true)
{
    Policy p;
    p.enabled = true;
    p.backgroundMinutes = backgroundMinutes;
    p.idleMinutes = idleMinutes;
    p.lockOnStart = lockOnStart;
    return p;
}

} // namespace

class TestAppLock : public QObject {
    Q_OBJECT

private Q_SLOTS:
    void offNeverLocks();
    void locksOnStart();
    void background_data();
    void background();
    void backgroundRepeatsAndClockBack();
    void idle();
    void lockNowAndUnlock();
    void disableUnlocks();
    void retryDelays();
    void wrongPins();
    void pinValidity_data();
    void pinValidity();
    void pinHash();
};

void TestAppLock::offNeverLocks()
{
    LockState s;
    s.started(0);
    QVERIFY(!s.locked());
    s.wentToBackground(1000);
    QVERIFY(!s.cameToForeground(100 * Minute));
    QVERIFY(!s.tick(1000 * Minute));
    s.lockNow();
    QVERIFY(!s.locked());
    QCOMPARE(s.nextDeadline(), -1);
}

void TestAppLock::locksOnStart()
{
    LockState s;
    s.setPolicy(on());
    s.started(0);
    QVERIFY(s.locked());
    s.unlocked(10);
    QVERIFY(!s.locked());
    LockState t;
    t.setPolicy(on(0, 0, false));
    t.started(0);
    QVERIFY(!t.locked());
}

void TestAppLock::background_data()
{
    QTest::addColumn<int>("minutes");
    QTest::addColumn<qint64>("away");
    QTest::addColumn<bool>("locks");
    QTest::newRow("at once, a second away") << 0 << qint64(1000) << true;
    QTest::newRow("at once, no time away") << 0 << qint64(0) << true;
    QTest::newRow("5 min, 4:59 away") << 5 << 5 * Minute - 1000 << false;
    QTest::newRow("5 min, 5:00 away") << 5 << 5 * Minute << true;
    QTest::newRow("5 min, an hour away") << 5 << 60 * Minute << true;
}

void TestAppLock::background()
{
    QFETCH(int, minutes);
    QFETCH(qint64, away);
    QFETCH(bool, locks);
    LockState s;
    s.setPolicy(on(minutes, 0, false));
    s.started(0);
    s.wentToBackground(10 * Minute);
    QCOMPARE(s.cameToForeground(10 * Minute + away), locks);
    QCOMPARE(s.locked(), locks);
}

void TestAppLock::backgroundRepeatsAndClockBack()
{
    LockState s;
    s.setPolicy(on(5, 0, false));
    s.started(0);
    // Several "background" events: the first one's time counts.
    s.wentToBackground(1000);
    s.wentToBackground(4 * Minute);
    QVERIFY(s.cameToForeground(5 * Minute + 1000));
    // A foreground without a background before is nothing.
    s.unlocked(6 * Minute);
    QVERIFY(!s.cameToForeground(100 * Minute));
    // A monotonic clock that went back (a restore of state): locked, to be safe.
    s.wentToBackground(10 * Minute);
    QVERIFY(s.cameToForeground(9 * Minute));
}

void TestAppLock::idle()
{
    LockState s;
    s.setPolicy(on(0, 10, false));
    s.started(0);
    QCOMPARE(s.nextDeadline(), 10 * Minute);
    QVERIFY(!s.tick(9 * Minute));
    s.activity(9 * Minute);
    QCOMPARE(s.nextDeadline(), 19 * Minute);
    QVERIFY(!s.tick(18 * Minute));
    QVERIFY(s.tick(19 * Minute));
    QVERIFY(s.locked());
    QCOMPARE(s.nextDeadline(), -1); // locked: nothing due
    s.unlocked(20 * Minute);
    QCOMPARE(s.nextDeadline(), 30 * Minute);
    // In the background no idle lock runs (the background's own rule does).
    s.wentToBackground(21 * Minute);
    QCOMPARE(s.nextDeadline(), -1);
    QVERIFY(!s.tick(100 * Minute));
    // No idle minutes: never.
    LockState n;
    n.setPolicy(on(0, 0, false));
    n.started(0);
    QCOMPARE(n.nextDeadline(), -1);
}

void TestAppLock::lockNowAndUnlock()
{
    LockState s;
    s.setPolicy(on(0, 0, false));
    s.started(0);
    QVERIFY(!s.locked());
    s.lockNow();
    QVERIFY(s.locked());
    // Already locked: coming back does not report a new lock.
    s.wentToBackground(1);
    QVERIFY(!s.cameToForeground(2));
    QVERIFY(s.locked());
    s.unlocked(3);
    QVERIFY(!s.locked());
}

void TestAppLock::disableUnlocks()
{
    LockState s;
    s.setPolicy(on());
    s.started(0);
    s.failedAttempt(1000);
    QVERIFY(s.locked());
    s.disable();
    QVERIFY(!s.locked());
    QCOMPARE(s.attempts().failures, 0);
    // A policy turned off unlocks too.
    s.setPolicy(on());
    s.lockNow();
    Policy off = on();
    off.enabled = false;
    s.setPolicy(off);
    QVERIFY(!s.locked());
}

void TestAppLock::retryDelays()
{
    for (int i = 0; i <= 4; ++i)
        QCOMPARE(retryDelayMs(i), 0);
    QCOMPARE(retryDelayMs(5), 30 * 1000);
    QCOMPARE(retryDelayMs(6), 1 * Minute);
    QCOMPARE(retryDelayMs(7), 2 * Minute);
    QCOMPARE(retryDelayMs(8), 5 * Minute);
    QCOMPARE(retryDelayMs(9), 15 * Minute);
    QCOMPARE(retryDelayMs(1000), 15 * Minute);
}

void TestAppLock::wrongPins()
{
    const qint64 t0 = 1763650000000; // wall clock, ms
    LockState s;
    s.setPolicy(on());
    s.started(0);
    for (int i = 0; i < 4; ++i) {
        QVERIFY(s.attemptAllowed(t0 + i));
        s.failedAttempt(t0 + i);
    }
    QVERIFY(s.attemptAllowed(t0 + 4)); // the fifth is still free
    s.failedAttempt(t0 + 4);
    QCOMPARE(s.retryAt(), t0 + 4 + 30 * 1000);
    QVERIFY(!s.attemptAllowed(t0 + 10 * 1000));
    QVERIFY(s.attemptAllowed(t0 + 4 + 30 * 1000));
    // Kept across a restart: the app stores attempts() and restores them.
    const Attempts saved = s.attempts();
    LockState again;
    again.setPolicy(on());
    again.started(0);
    again.restoreAttempts(saved);
    QVERIFY(!again.attemptAllowed(t0 + 20 * 1000));
    // A clock set back before the last failure: allowed (no lock-out for good).
    QVERIFY(again.attemptAllowed(t0 - Minute));
    // A success forgets them.
    again.unlocked(1);
    QCOMPARE(again.attempts().failures, 0);
    QCOMPARE(again.retryAt(), 0);
}

void TestAppLock::pinValidity_data()
{
    QTest::addColumn<QString>("pin");
    QTest::addColumn<bool>("valid");
    QTest::newRow("4 digits") << u"1234"_s << true;
    QTest::newRow("16 digits") << u"1234567890123456"_s << true;
    QTest::newRow("3 digits") << u"123"_s << false;
    QTest::newRow("17 digits") << u"12345678901234567"_s << false;
    QTest::newRow("letters") << u"12a4"_s << false;
    QTest::newRow("polish") << u"żółw"_s << false;
    QTest::newRow("arabic-indic digits") << u"١٢٣٤"_s << false;
    QTest::newRow("space") << u"12 34"_s << false;
    QTest::newRow("empty") << QString() << false;
}

void TestAppLock::pinValidity()
{
    QFETCH(QString, pin);
    QFETCH(bool, valid);
    QCOMPARE(isValidPin(pin), valid);
    QCOMPARE(hashPin(pin).isEmpty(), !valid);
}

void TestAppLock::pinHash()
{
    const QString stored = hashPin(u"2580"_s);
    QVERIFY(stored.startsWith(u"pbkdf2-sha512$210000$"_s));
    QVERIFY(!stored.contains(u"2580"_s));
    QVERIFY(verifyPin(stored, u"2580"_s));
    QVERIFY(!verifyPin(stored, u"2581"_s));
    QVERIFY(!verifyPin(stored, u"25800"_s));
    QVERIFY(!verifyPin(stored, u"abcd"_s));
    QVERIFY(!verifyPin(QString(), u"2580"_s));
    QVERIFY(!verifyPin(u"garbage"_s, u"2580"_s));
    // Salted: one PIN, two different records.
    QVERIFY(hashPin(u"2580"_s) != stored);
}

QTEST_GUILESS_MAIN(TestAppLock)
#include "tst_applock.moc"
