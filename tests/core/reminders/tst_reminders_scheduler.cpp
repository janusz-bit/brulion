// ReminderScheduler: scheduling on the sink, in-app firing with a controllable clock (fire once,
// restarts, catch-up after a gap, missed reminders, sleep detection), persistence and settings.
#include "reminders/reminders.h"
#include "reminderstest.h"
#include "vault/localstore.h"

#include <QJsonArray>
#include <QJsonObject>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>
#include <QTimeZone>

#include <memory>

using namespace brulion;
using namespace remtest;
using namespace Qt::StringLiterals;

namespace {

/// Everything a scheduler needs, with an in-memory store unless a file is given.
struct Env {
    explicit Env(const QDateTime &start, bool native = false, const QString &storeFile = QString())
        : store(storeFile), sink(native), clock(start)
    {
        store.setFlushDelay(0);
    }

    std::unique_ptr<ReminderScheduler> scheduler()
    {
        auto s = std::make_unique<ReminderScheduler>(&index, &sink, &store, clock.fn());
        s->setDebounceInterval(5);
        return s;
    }

    VaultIndex index;
    LocalStore store;
    FakeSink sink;
    TestClock clock;
};

QString stamp(const QDateTime &t)
{
    return t.date().toString(u"yyyy-MM-dd"_s) + u'T' + t.time().toString(u"HH:mm:ss.zzz"_s);
}

const QString KeyLastSeen = u"reminders/lastSeen"_s;
const QString KeyHandled = u"reminders/handled"_s;
const QString KeyMissed = u"reminders/missed"_s;

} // namespace

class TestRemindersScheduler : public QObject {
    Q_OBJECT

private Q_SLOTS:
    void schedulesUpcomingOnStart()
    {
        Env env(dt(2026, 10, 14, 8, 0), /*native*/ true);
        fill(env.index, vault());
        auto s = env.scheduler();
        QSignalSpy upcomingSpy(s.get(), &ReminderScheduler::upcomingChanged);
        QVERIFY(!s->isRunning());
        s->start();
        QVERIFY(s->isRunning());
        QCOMPARE(env.sink.scheduled.size(), 1);
        QCOMPARE(env.sink.scheduled.first(), upcomingNotifications(env.index, env.clock.now()));
        QCOMPARE(s->upcoming(), env.sink.scheduled.first());
        QCOMPARE(s->upcoming().size(), 24);
        QCOMPARE(s->droppedCount(), 0);
        QCOMPARE(upcomingSpy.size(), 1);
        QVERIFY(env.sink.shown.isEmpty());

        s->start(); // no-op
        QCOMPARE(env.sink.scheduled.size(), 1);

        // Index changes reschedule (debounced) ...
        env.index.update(u"Nowa.md"_s, u"- Nowe przypomnienie ⏰ 2026-10-14 10:00"_s);
        QTRY_COMPARE(env.sink.scheduled.size(), 2);
        QCOMPARE(env.sink.scheduled.last().value(1).title, u"Nowe przypomnienie"_s);
        QCOMPARE(upcomingSpy.size(), 2);
        // ... but an unchanged set is not sent again.
        env.index.update(u"Bez.md"_s, u"- nic"_s);
        QTest::qWait(30);
        s->refresh();
        QCOMPARE(env.sink.scheduled.size(), 2);

        s->stop();
        QVERIFY(!s->isRunning());
        env.index.update(u"Nowa2.md"_s, u"- Jeszcze ⏰ 2026-10-14 11:00"_s);
        QTest::qWait(30);
        s->refresh();
        QCOMPARE(env.sink.scheduled.size(), 2);
    }

    void startAlwaysSendsTheSet()
    {
        // An empty set still replaces whatever the platform kept from the previous run.
        Env env(dt(2026, 10, 14, 8, 0), true);
        fill(env.index, {{u"Pusta.md"_s, u"- brak przypomnień"_s}});
        auto s = env.scheduler();
        s->start();
        QCOMPARE(env.sink.scheduled.size(), 1);
        QVERIFY(env.sink.scheduled.first().isEmpty());
        s->stop();
        s->start();
        QCOMPARE(env.sink.scheduled.size(), 2);
    }

    void firesOnce()
    {
        Env env(dt(2026, 10, 14, 8, 59));
        fill(env.index, {{u"Praca.md"_s,
                          lines({u"- Spotkanie z Łucją ⏰ 2026-10-14 09:00"_s, u"- Później ⏰ 2026-10-14 11:00"_s})}});
        auto s = env.scheduler();
        QSignalSpy firedSpy(s.get(), &ReminderScheduler::fired);
        s->start();
        QVERIFY(env.sink.shown.isEmpty());
        QCOMPARE(s->nextFireTime(), dt(2026, 10, 14, 9, 0));

        env.clock.set(dt(2026, 10, 14, 9, 0, 0));
        s->checkNow();
        QCOMPARE(env.sink.shownTitles(), QStringList({u"Spotkanie z Łucją"_s}));
        QCOMPARE(firedSpy.size(), 1);
        QCOMPARE(firedSpy.first().first().value<ScheduledNotification>().title, u"Spotkanie z Łucją"_s);
        QCOMPARE(env.sink.shown.first().path, u"Praca.md"_s);
        QCOMPARE(env.sink.shown.first().line, 0);
        QVERIFY(env.sink.shown.first().nativeId > 0);
        QCOMPARE(s->nextFireTime(), dt(2026, 10, 14, 11, 0));

        // Never twice: later checks and refreshes keep it handled ...
        env.clock.set(dt(2026, 10, 14, 9, 0, 30));
        s->checkNow();
        s->refresh();
        QCOMPARE(env.sink.shown.size(), 1);

        // ... restarts too (handled ids are persisted) ...
        s.reset();
        env.clock.set(dt(2026, 10, 14, 9, 0, 40));
        auto again = env.scheduler();
        again->start();
        QCOMPARE(env.sink.shown.size(), 1);
        QVERIFY(again->missed().isEmpty());

        // ... and index changes (the set without 09:00 is rescheduled).
        const qsizetype calls = env.sink.scheduled.size();
        env.clock.set(dt(2026, 10, 14, 9, 1, 30));
        env.index.update(u"Inna.md"_s, u"- coś"_s);
        QTRY_COMPARE(env.sink.scheduled.size(), calls + 1);
        QCOMPARE(keys(env.sink.scheduled.last()), QStringList({u"2026-10-14 11:00 Później"_s}));
        QCOMPARE(env.sink.shown.size(), 1);
    }

    void timerFiresInRealTime()
    {
        Env env(dt(2026, 10, 14, 8, 59, 59, 700));
        fill(env.index, {{u"Dom.md"_s, u"- [ ] Wyłączyć piekarnik ⏰ 2026-10-14 09:00"_s}});
        env.clock.run();
        auto s = env.scheduler();
        s->start();
        QCOMPARE(s->nextFireTime(), dt(2026, 10, 14, 9, 0));
        QTRY_COMPARE_WITH_TIMEOUT(env.sink.shown.size(), 1, 5000);
        QCOMPARE(env.sink.shown.first().title, u"Wyłączyć piekarnik"_s);
        QVERIFY(!s->nextFireTime().isValid());
    }

    void catchUpAfterGap()
    {
        Env env(dt(2026, 10, 14, 8, 0));
        fill(env.index,
             {{u"Dzień.md"_s, lines({u"- Przed ostatnim ⏰ 2026-10-14 07:58"_s, u"- Rano ⏰ 2026-10-14 09:00"_s,
                                     u"- Południe ⏰ 2026-10-14 12:00"_s, u"- Alarm dawno ⏰! 2026-10-14 15:30"_s,
                                     u"- Alarm świeży ⏰! 2026-10-14 15:55"_s, u"- [ ] Zadanie ⏰ 2026-10-14 15:59"_s,
                                     u"- Później ⏰ 2026-10-14 17:00"_s})}});
        // The app runs at 08:00 and is closed.
        {
            auto first = env.scheduler();
            first->start();
            first->stop();
        }
        QCOMPARE(env.store.get(KeyLastSeen).toString(), u"2026-10-14T08:00:00.000"_s);
        QVERIFY(env.sink.shown.isEmpty());

        // It starts again at 16:00.
        env.clock.set(dt(2026, 10, 14, 16, 0));
        auto s = env.scheduler();
        QSignalSpy missedSpy(s.get(), &ReminderScheduler::missedChanged);
        s->start();
        // <= 6 h late: shown late; alarms only within their ring time (10 min).
        QCOMPARE(env.sink.shownKeys(), QStringList({u"2026-10-14 12:00 Południe"_s, u"2026-10-14 15:55 Alarm świeży"_s,
                                                    u"2026-10-14 15:59 Zadanie"_s}));
        QCOMPARE(keys(s->missed()), QStringList({u"2026-10-14 09:00 Rano"_s, u"2026-10-14 15:30 Alarm dawno"_s}));
        QVERIFY(s->missed().at(1).alarm);
        QVERIFY(s->missed().at(0).nativeId != s->missed().at(1).nativeId);
        QVERIFY(missedSpy.size() >= 1);
        QCOMPARE(s->nextFireTime(), dt(2026, 10, 14, 17, 0));

        // Missed reminders survive a restart, nothing fires twice.
        s.reset();
        env.clock.set(dt(2026, 10, 14, 16, 5));
        auto third = env.scheduler();
        third->start();
        QCOMPARE(env.sink.shown.size(), 3);
        QCOMPARE(keys(third->missed()), QStringList({u"2026-10-14 09:00 Rano"_s, u"2026-10-14 15:30 Alarm dawno"_s}));

        QSignalSpy spy(third.get(), &ReminderScheduler::missedChanged);
        third->dismissMissed(third->missed().first().id);
        QCOMPARE(keys(third->missed()), QStringList({u"2026-10-14 15:30 Alarm dawno"_s}));
        third->dismissMissed(u"nieznane"_s);
        QCOMPARE(spy.size(), 1);
        third->clearMissed();
        QVERIFY(third->missed().isEmpty());
        QCOMPARE(spy.size(), 2);
        QCOMPARE(env.store.get(KeyMissed).toArray().size(), 0);
    }

    void firstStartLooksBackOneMinute()
    {
        Env env(dt(2026, 10, 14, 16, 0, 30));
        fill(env.index, {{u"a.md"_s, lines({u"- A ⏰ 2026-10-14 09:00"_s, u"- B ⏰ 2026-10-14 15:59"_s,
                                            u"- C ⏰ 2026-10-14 16:00"_s})}});
        auto s = env.scheduler();
        s->start();
        QCOMPARE(env.sink.shownTitles(), QStringList({u"C"_s}));
        QVERIFY(s->missed().isEmpty());
    }

    void reminderSetForNow()
    {
        // A reminder typed for the current minute after the last check still fires.
        Env env(dt(2026, 10, 14, 10, 0, 10));
        fill(env.index, {{u"a.md"_s, u"- nic"_s}});
        auto s = env.scheduler();
        s->start();
        env.clock.set(dt(2026, 10, 14, 10, 0, 40));
        env.index.update(u"a.md"_s, u"- Teraz ⏰ 2026-10-14 10:00"_s);
        QTRY_COMPARE(env.sink.shownTitles(), QStringList({u"Teraz"_s}));
    }

    void heartbeatCatchesSleep()
    {
        Env env(dt(2026, 10, 14, 8, 0));
        fill(env.index,
             {{u"Posiłki.md"_s, lines({u"- Śniadanie ⏰ 2026-10-14 09:00"_s, u"- Obiad ⏰ 2026-10-14 13:30"_s,
                                       u"- Nocne ⏰ 2026-10-15 01:00"_s})}});
        auto s = env.scheduler();
        s->setHeartbeatInterval(20);
        s->start();
        QCOMPARE(s->nextFireTime(), dt(2026, 10, 14, 9, 0));
        QCOMPARE(env.sink.scheduled.size(), 1);

        // The computer sleeps until 13:00: the monotonic clock (and the 09:00 timer) stood still.
        env.clock.set(dt(2026, 10, 14, 13, 0));
        QTRY_COMPARE(env.sink.shownTitles(), QStringList({u"Śniadanie"_s}));
        QTRY_COMPARE(s->nextFireTime(), dt(2026, 10, 14, 13, 30));
        QTRY_COMPARE(env.sink.scheduled.size(), 2); // rescheduled after the jump

        env.clock.set(dt(2026, 10, 14, 13, 31));
        QTRY_COMPARE(env.sink.shownTitles(), QStringList({u"Śniadanie"_s, u"Obiad"_s}));

        // More than 6 h asleep: missed.
        QSignalSpy missedSpy(s.get(), &ReminderScheduler::missedChanged);
        env.clock.set(dt(2026, 10, 15, 8, 0));
        QTRY_COMPARE(missedSpy.size(), 1);
        QCOMPARE(keys(s->missed()), QStringList({u"2026-10-15 01:00 Nocne"_s}));
        QCOMPARE(env.sink.shown.size(), 2);
    }

    void recurringFiresEveryOccurrence()
    {
        Env env(dt(2026, 10, 14, 7, 59));
        fill(env.index, {{u"Zdrowie.md"_s, u"- [ ] Leki 🔁 every day ⏰ 2026-10-14 08:00"_s}});
        auto s = env.scheduler();
        s->start();
        QCOMPARE(s->nextFireTime(), dt(2026, 10, 14, 8, 0));
        env.clock.set(dt(2026, 10, 14, 8, 0, 10));
        s->checkNow();
        QCOMPARE(s->nextFireTime(), dt(2026, 10, 15, 8, 0));
        env.clock.set(dt(2026, 10, 15, 8, 0, 10));
        s->checkNow();
        QCOMPARE(env.sink.shownKeys(), QStringList({u"2026-10-14 08:00 Leki"_s, u"2026-10-15 08:00 Leki"_s}));
        QVERIFY(env.sink.shown[0].id != env.sink.shown[1].id);
    }

    void completedTaskDoesNotFire()
    {
        Env env(dt(2026, 10, 14, 8, 0));
        fill(env.index, {{u"a.md"_s, u"- [ ] Zadanie ⏰ 2026-10-14 09:00"_s}});
        auto s = env.scheduler();
        s->start();
        QCOMPARE(s->nextFireTime(), dt(2026, 10, 14, 9, 0));
        env.index.update(u"a.md"_s, u"- [x] Zadanie ⏰ 2026-10-14 09:00 ✅ 2026-10-14"_s);
        s->refresh();
        QVERIFY(!s->nextFireTime().isValid());
        QVERIFY(env.sink.scheduled.last().isEmpty());
        env.clock.set(dt(2026, 10, 14, 9, 0, 5));
        s->checkNow();
        QVERIFY(env.sink.shown.isEmpty());
    }

    void missedDisappearsWithItsReminder()
    {
        Env env(dt(2026, 10, 14, 16, 0));
        env.store.set(KeyLastSeen, stamp(dt(2026, 10, 14, 8, 0)));
        fill(env.index, {{u"a.md"_s, lines({u"- [ ] Zadanie ⏰ 2026-10-14 09:00"_s, u"- Inne ⏰ 2026-10-14 09:30"_s})},
                         {u"b.md"_s, u"- notatka"_s}});
        auto s = env.scheduler();
        s->start();
        QCOMPARE(keys(s->missed()), QStringList({u"2026-10-14 09:00 Zadanie"_s, u"2026-10-14 09:30 Inne"_s}));

        QSignalSpy spy(s.get(), &ReminderScheduler::missedChanged);
        // Completing the task removes it from missed().
        env.index.update(u"a.md"_s, lines({u"- [x] Zadanie ⏰ 2026-10-14 09:00"_s, u"- Inne ⏰ 2026-10-14 09:30"_s}));
        s->refresh();
        QCOMPARE(keys(s->missed()), QStringList({u"2026-10-14 09:30 Inne"_s}));
        QCOMPARE(spy.size(), 1);
        // Deleting the reminder too.
        env.index.remove(u"a.md"_s);
        s->refresh();
        QVERIFY(s->missed().isEmpty());
        QCOMPARE(spy.size(), 2);
    }

    void missedKeptWhileIndexIsEmpty()
    {
        Env env(dt(2026, 10, 14, 16, 0));
        env.store.set(KeyLastSeen, stamp(dt(2026, 10, 14, 16, 0)));
        QJsonObject entry{{u"id"_s, u"abc"_s},     {u"title"_s, u"Rano"_s},
                          {u"body"_s, u"Dzień"_s}, {u"at"_s, stamp(dt(2026, 10, 14, 9, 0))},
                          {u"alarm"_s, true},      {u"path"_s, u"Dzień.md"_s},
                          {u"line"_s, 3}};
        QJsonObject old = entry;
        old[u"id"_s] = u"old"_s;
        old[u"at"_s] = stamp(dt(2026, 10, 1, 9, 0));
        env.store.set(KeyMissed, QJsonArray{entry, old, QJsonObject{{u"id"_s, u"broken"_s}}});
        auto s = env.scheduler();
        s->start(); // the vault is still loading: only entries older than 7 days go
        QCOMPARE(s->missed().size(), 1);
        const ScheduledNotification n = s->missed().first();
        QCOMPARE(n.id, u"abc"_s);
        QCOMPARE(n.title, u"Rano"_s);
        QCOMPARE(n.body, u"Dzień"_s);
        QCOMPARE(n.at, dt(2026, 10, 14, 9, 0));
        QVERIFY(n.alarm);
        QCOMPARE(n.path, u"Dzień.md"_s);
        QCOMPARE(n.line, 3);
        QVERIFY(n.nativeId > 0);
    }

    void emptyIndexDoesNotConsumeWindow()
    {
        Env env(dt(2026, 10, 14, 10, 0));
        env.store.set(KeyLastSeen, stamp(dt(2026, 10, 14, 8, 0)));
        auto s = env.scheduler();
        s->start();
        QVERIFY(env.sink.shown.isEmpty());
        QCOMPARE(env.store.get(KeyLastSeen).toString(), u"2026-10-14T08:00:00.000"_s);
        // The vault finishes loading: the 09:00 reminder (1 h late) is caught up.
        fill(env.index, {{u"a.md"_s, u"- Spotkanie ⏰ 2026-10-14 09:00"_s}});
        s->refresh();
        QCOMPARE(env.sink.shownTitles(), QStringList({u"Spotkanie"_s}));
    }

    void handledAndMissedPruned()
    {
        Env env(dt(2026, 10, 14, 8, 0));
        env.store.set(KeyHandled, QJsonObject{{u"old"_s, stamp(dt(2026, 10, 6, 7, 0))},
                                              {u"recent"_s, stamp(dt(2026, 10, 13, 7, 0))},
                                              {u"broken"_s, u"wczoraj"_s}});
        fill(env.index, {{u"a.md"_s, u"- nic"_s}});
        auto s = env.scheduler();
        s->start();
        const QJsonObject handled = env.store.get(KeyHandled).toObject();
        QCOMPARE(handled.keys(), QStringList({u"recent"_s}));
    }

    void handledCapped()
    {
        Env env(dt(2026, 10, 14, 8, 0));
        QJsonObject many;
        for (int i = 0; i < ReminderScheduler::MaxHandled + 3; ++i)
            many.insert(u"id%1"_s.arg(i), stamp(dt(2026, 10, 13, 0, 0).addSecs(i)));
        env.store.set(KeyHandled, many);
        fill(env.index, {{u"a.md"_s, u"- nic"_s}});
        auto s = env.scheduler();
        s->start();
        const QJsonObject handled = env.store.get(KeyHandled).toObject();
        QCOMPARE(handled.size(), ReminderScheduler::MaxHandled);
        QVERIFY(!handled.contains(u"id0"_s)); // the oldest go first
        QVERIFY(!handled.contains(u"id2"_s));
        QVERIFY(handled.contains(u"id3"_s));
    }

    void missedCapped()
    {
        Env env(dt(2026, 10, 14, 16, 0));
        env.store.set(KeyLastSeen, stamp(dt(2026, 10, 14, 0, 0)));
        QStringList many;
        for (int i = 0; i < ReminderScheduler::MaxMissed + 5; ++i)
            many.append(u"- Nr %1 ⏰ %2"_s.arg(i).arg(
                    dt(2026, 10, 14, 1, 0).addSecs(60 * i).toString(u"yyyy-MM-dd HH:mm"_s)));
        fill(env.index, {{u"a.md"_s, many.join(u'\n')}});
        auto s = env.scheduler();
        s->start();
        QCOMPARE(s->missed().size(), ReminderScheduler::MaxMissed);
        QCOMPARE(s->missed().first().title, u"Nr 5"_s);
        QCOMPARE(s->missed().last().title, u"Nr 104"_s);
        QVERIFY(env.sink.shown.isEmpty());
    }

    void stopPersistsLastCheck()
    {
        Env env(dt(2026, 10, 14, 8, 0));
        fill(env.index, {{u"a.md"_s, u"- Później ⏰ 2026-10-14 12:00"_s}});
        auto s = env.scheduler();
        s->start();
        QCOMPARE(env.store.get(KeyLastSeen).toString(), u"2026-10-14T08:00:00.000"_s);
        env.clock.set(dt(2026, 10, 14, 8, 2));
        s->checkNow(); // saved at most every 5 minutes while running ...
        QCOMPARE(env.store.get(KeyLastSeen).toString(), u"2026-10-14T08:00:00.000"_s);
        env.clock.set(dt(2026, 10, 14, 8, 3));
        s->stop(); // ... and by stop(): the last check, not the time of stopping
        QCOMPARE(env.store.get(KeyLastSeen).toString(), u"2026-10-14T08:02:00.000"_s);
        QVERIFY(!s->nextFireTime().isValid());
        env.clock.set(dt(2026, 10, 14, 12, 0));
        s->checkNow();
        QVERIFY(env.sink.shown.isEmpty());
    }

    void nativeSinkDoesNotFireInApp()
    {
        Env env(dt(2026, 10, 14, 16, 0), /*native*/ true);
        env.store.set(KeyLastSeen, stamp(dt(2026, 10, 14, 8, 0)));
        fill(env.index, {{u"a.md"_s, lines({u"- A ⏰ 2026-10-14 09:00"_s, u"- B ⏰ 2026-10-14 15:59"_s,
                                            u"- C ⏰ 2026-10-14 17:00"_s})}});
        auto s = env.scheduler();
        s->start();
        s->checkNow();
        QVERIFY(env.sink.shown.isEmpty());
        QVERIFY(s->missed().isEmpty());
        QVERIFY(!s->nextFireTime().isValid());
        QCOMPARE(keys(env.sink.scheduled.first()), QStringList({u"2026-10-14 15:59 B"_s, u"2026-10-14 17:00 C"_s}));
    }

    void settings()
    {
        Env env(dt(2026, 10, 14, 8, 0), true);
        fill(env.index, vault());
        auto s = env.scheduler();
        s->setMaxScheduled(5);
        s->start();
        QCOMPARE(s->upcoming().size(), 5);
        QCOMPARE(s->droppedCount(), 19);
        s->setMaxScheduled(64);
        QCOMPARE(s->upcoming().size(), 24);
        QCOMPARE(s->droppedCount(), 0);
        QCOMPARE(env.sink.scheduled.size(), 2);

        s->setAlarmRingMinutes(3);
        QCOMPARE(s->alarmRingMinutes(), 3);
        for (const ScheduledNotification &n : env.sink.scheduled.last())
            QCOMPARE(n.ringMinutes, 3);

        s->setLocale(dates::UiLocale::English);
        QCOMPARE(s->locale(), dates::UiLocale::English);
        QCOMPARE(s->upcoming().value(1).body, u"2026-10-14 · due today"_s);

        s->setHorizonDays(0);
        QCOMPARE(keys(s->upcoming()), QStringList({u"2026-10-14 08:00 Łyknąć witaminy"_s}));
        const qsizetype calls = env.sink.scheduled.size();
        s->setHorizonDays(0); // unchanged: nothing happens
        QCOMPARE(env.sink.scheduled.size(), calls);

        // Clamping.
        s->setAlarmRingMinutes(500);
        QCOMPARE(s->alarmRingMinutes(), 60);
        s->setAlarmRingMinutes(0);
        QCOMPARE(s->alarmRingMinutes(), 1);
        s->setMaxScheduled(0);
        QCOMPARE(s->maxScheduled(), 1);
        s->setHorizonDays(-3);
        QCOMPARE(s->horizonDays(), 0);
        s->setHorizonDays(100000);
        QCOMPARE(s->horizonDays(), 3660);
    }

    void alarmRingMinutesDecideLateAlarms()
    {
        Env env(dt(2026, 10, 14, 16, 0));
        env.store.set(KeyLastSeen, stamp(dt(2026, 10, 14, 15, 0)));
        fill(env.index, {{u"a.md"_s, lines({u"- Alarm ⏰! 2026-10-14 15:40"_s, u"- Starszy ⏰! 2026-10-14 15:20"_s})}});
        auto s = env.scheduler();
        s->setAlarmRingMinutes(30);
        s->start();
        QCOMPARE(env.sink.shownTitles(), QStringList({u"Alarm"_s}));
        QCOMPARE(env.sink.shown.first().ringMinutes, 30);
        QVERIFY(env.sink.shown.first().alarm);
        QCOMPARE(keys(s->missed()), QStringList({u"2026-10-14 15:20 Starszy"_s}));
    }

    void clockGoesBackwards()
    {
        Env env(dt(2026, 10, 14, 8, 59));
        fill(env.index, {{u"a.md"_s, u"- Spotkanie ⏰ 2026-10-14 09:00"_s}});
        auto s = env.scheduler();
        s->start();
        env.clock.set(dt(2026, 10, 14, 9, 0, 5));
        s->checkNow();
        QCOMPARE(env.sink.shown.size(), 1);
        env.clock.set(dt(2026, 10, 14, 8, 30)); // NTP correction / time-zone change
        s->refresh();
        QCOMPARE(s->nextFireTime(), QDateTime()); // already handled
        env.clock.set(dt(2026, 10, 14, 9, 0, 10));
        s->checkNow();
        QCOMPARE(env.sink.shown.size(), 1);
    }

    void stopFromFiredSlot()
    {
        Env env(dt(2026, 10, 14, 8, 59));
        fill(env.index, {{u"a.md"_s, lines({u"- Pierwsze ⏰ 2026-10-14 09:00"_s, u"- Drugie ⏰ 2026-10-14 09:01"_s})}});
        auto s = env.scheduler();
        connect(s.get(), &ReminderScheduler::fired, s.get(), &ReminderScheduler::stop);
        s->start();
        env.clock.set(dt(2026, 10, 14, 9, 1, 30));
        s->checkNow();
        QCOMPARE(env.sink.shownTitles(), QStringList({u"Pierwsze"_s}));
        QVERIFY(!s->isRunning());
        // The second one was not consumed: it fires on the next start.
        disconnect(s.get(), &ReminderScheduler::fired, s.get(), &ReminderScheduler::stop);
        s->start();
        QCOMPARE(env.sink.shownTitles(), QStringList({u"Pierwsze"_s, u"Drugie"_s}));
    }

    void indexDestroyedFirst()
    {
        FakeSink sink;
        LocalStore store{QString()};
        TestClock clock(dt(2026, 10, 14, 8, 0));
        auto index = std::make_unique<VaultIndex>();
        fill(*index, {{u"a.md"_s, u"- A ⏰ 2026-10-14 09:00"_s}});
        ReminderScheduler s(index.get(), &sink, &store, clock.fn());
        s.start();
        index.reset();
        clock.set(dt(2026, 10, 14, 9, 0));
        s.checkNow();
        s.refresh();
        QVERIFY(sink.shown.isEmpty());
        s.stop();
    }

    void persistsInAFile()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString file = dir.filePath(u"store.json"_s);
        const Files files = {{u"a.md"_s, u"- Spotkanie ⏰ 2026-10-14 09:00"_s}};
        {
            Env env(dt(2026, 10, 14, 9, 0, 20), false, file);
            fill(env.index, files);
            auto s = env.scheduler();
            s->start();
            QCOMPARE(env.sink.shown.size(), 1);
        }
        {
            Env env(dt(2026, 10, 14, 9, 0, 50), false, file);
            QVERIFY(env.store.contains(KeyHandled));
            QCOMPARE(env.store.get(KeyLastSeen).toString(), u"2026-10-14T09:00:20.000"_s);
            fill(env.index, files);
            auto s = env.scheduler();
            s->start();
            QVERIFY(env.sink.shown.isEmpty());
        }
    }

    void clockInAnotherZone()
    {
        // A clock in another time zone (e.g. currentDateTimeUtc) gives the same instants; the
        // stamps in the store stay local wall-clock time.
        const QTimeZone zone = QTimeZone::fromSecondsAheadOfUtc(14 * 3600);
        Env env(dt(2026, 10, 14, 8, 59).toTimeZone(zone));
        fill(env.index, {{u"a.md"_s, u"- Spotkanie ⏰ 2026-10-14 09:00"_s}});
        auto s = env.scheduler();
        s->start();
        QCOMPARE(s->nextFireTime(), dt(2026, 10, 14, 9, 0));
        QCOMPARE(env.store.get(KeyLastSeen).toString(), u"2026-10-14T08:59:00.000"_s);
        env.clock.set(dt(2026, 10, 14, 9, 0, 5).toTimeZone(zone));
        s->checkNow();
        QCOMPARE(env.sink.shownTitles(), QStringList({u"Spotkanie"_s}));
        s->stop();
        QCOMPARE(env.store.get(KeyLastSeen).toString(), u"2026-10-14T09:00:05.000"_s);
        const QJsonObject handled = env.store.get(KeyHandled).toObject();
        QCOMPARE(handled.size(), 1);
        QCOMPARE(handled.begin().value().toString(), u"2026-10-14T09:00:05.000"_s);
    }

    void withoutStore()
    {
        // No LocalStore: state lives in memory across stop()/start() of the same scheduler.
        FakeSink sink;
        TestClock clock(dt(2026, 10, 14, 8, 59));
        VaultIndex index;
        fill(index, {{u"a.md"_s, u"- Spotkanie ⏰ 2026-10-14 09:00"_s}});
        ReminderScheduler s(&index, &sink, nullptr, clock.fn());
        s.start();
        clock.set(dt(2026, 10, 14, 9, 0, 5));
        s.checkNow();
        s.stop();
        clock.set(dt(2026, 10, 14, 9, 0, 30));
        s.start();
        QCOMPARE(sink.shown.size(), 1);
    }
};

QTEST_GUILESS_MAIN(TestRemindersScheduler)
#include "tst_reminders_scheduler.moc"
