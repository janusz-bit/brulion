// SyncScheduler with a controllable clock and a fake runner: periodic syncs, debounce, offline,
// foreground/background, errors with backoff, stop/restart, progress and status.
#include "sync/scheduler.h"

#include <QMap>
#include <QNetworkInformation>
#include <QSignalSpy>
#include <QTest>

#include <memory>

using namespace brulion;
using namespace brulion::sync;
using namespace Qt::StringLiterals;

namespace {

/// Manual clock + timers.
class FakeTimers : public SchedulerTimers {
public:
    qint64 t = 1'000'000;

    qint64 now() const override { return t; }

    int start(qint64 delayMs, std::function<void()> callback) override
    {
        const int handle = ++m_seq;
        m_pending.insert(handle, Pending{t + delayMs, std::move(callback)});
        return handle;
    }

    void cancel(int handle) override { m_pending.remove(handle); }

    qsizetype count() const { return m_pending.size(); }

    /// Advance time, firing due timers in order.
    void advance(qint64 ms)
    {
        const qint64 end = t + ms;
        for (;;) {
            auto next = m_pending.end();
            for (auto it = m_pending.begin(); it != m_pending.end(); ++it) {
                if (it->at <= end && (next == m_pending.end() || it->at < next->at))
                    next = it;
            }
            if (next == m_pending.end())
                break;
            const Pending fired = next.value();
            m_pending.erase(next);
            t = fired.at;
            fired.fn();
        }
        t = end;
    }

private:
    struct Pending {
        qint64 at;
        std::function<void()> fn;
    };
    QMap<int, Pending> m_pending;
    int m_seq = 0;
};

/// Runner double: completes immediately (autoComplete) or when release() is called.
class FakeRunner : public SyncRunner {
public:
    explicit FakeRunner(FakeTimers *timers)
        : m_timers(timers)
    {
    }

    int calls = 0;
    int cancels = 0;
    bool autoComplete = true;
    bool cancelCompletes = true; ///< cancelSync() finishes a pending run (cancelled) right away
    SyncRunOptions lastOptions;
    std::function<SyncReport()> result;

    void startSync(const SyncRunOptions &options, SyncDoneCallback done) override
    {
        ++calls;
        lastOptions = options;
        if (autoComplete) {
            done(makeResult());
            return;
        }
        m_pending.append(std::move(done));
    }

    void cancelSync() override
    {
        ++cancels;
        if (cancelCompletes)
            finishCancelled();
    }

    void release()
    {
        if (!m_pending.isEmpty())
            m_pending.takeFirst()(makeResult());
    }

    void finishCancelled()
    {
        if (m_pending.isEmpty())
            return;
        SyncReport cancelled;
        cancelled.cancelled = true;
        m_pending.takeFirst()(cancelled);
    }

    qsizetype pending() const { return m_pending.size(); }

private:
    SyncReport makeResult()
    {
        if (result)
            return result();
        SyncReport r;
        r.finished = QDateTime::fromMSecsSinceEpoch(m_timers->t);
        return r;
    }

    FakeTimers *m_timers;
    QList<SyncDoneCallback> m_pending;
};

SyncReport failedReport(WebDavErrorCode code, int status, const QString &message)
{
    SyncReport r;
    r.remoteError = WebDavError{code, status, message, {}};
    return r;
}

struct Fixture {
    explicit Fixture(qint64 interval = 300'000, qint64 debounce = 20'000)
        : timers(new FakeTimers)
        , runner(timers)
        , scheduler(&runner)
    {
        scheduler.setTimers(std::unique_ptr<SchedulerTimers>(timers));
        scheduler.setInterval(interval);
        scheduler.setDebounce(debounce);
        scheduler.setOnlineCheck([this] { return online; });
        QObject::connect(&scheduler, &SyncScheduler::statusChanged, &scheduler,
                         [this] { statuses.append(scheduler.status()); });
    }

    QList<SyncState> states() const
    {
        QList<SyncState> out;
        for (const SyncStatus &s : statuses)
            out.append(s.state);
        return out;
    }

    FakeTimers *timers; // owned by the scheduler
    FakeRunner runner;
    SyncScheduler scheduler;
    bool online = true;
    QList<SyncStatus> statuses;
};

} // namespace

class TestSyncScheduler : public QObject {
    Q_OBJECT
private Q_SLOTS:
    void startsDisabledAndSyncsRightAfterStart()
    {
        Fixture f;
        QCOMPARE(f.scheduler.state(), SyncState::Disabled);
        QVERIFY(!f.scheduler.isActive());
        f.scheduler.start();
        QVERIFY(f.scheduler.isActive());
        QCOMPARE(f.scheduler.state(), SyncState::Idle);
        QCOMPARE(f.runner.calls, 0);
        f.timers->advance(0);
        QCOMPARE(f.runner.calls, 1);
        QVERIFY(f.states().contains(SyncState::Syncing));
        QCOMPARE(f.scheduler.state(), SyncState::Idle);
        QCOMPARE(f.scheduler.status().lastSync, QDateTime::fromMSecsSinceEpoch(f.timers->t));
        QVERIFY(f.scheduler.status().lastReport.has_value());
        f.scheduler.stop();
    }

    void syncsPeriodically()
    {
        Fixture f(60'000);
        f.scheduler.start();
        f.timers->advance(0);
        QCOMPARE(f.scheduler.status().nextSync, QDateTime::fromMSecsSinceEpoch(f.timers->t + 60'000));
        f.timers->advance(59'999);
        QCOMPARE(f.runner.calls, 1);
        f.timers->advance(1);
        QCOMPARE(f.runner.calls, 2);
        f.timers->advance(120'000);
        QCOMPARE(f.runner.calls, 4);
        f.scheduler.stop();
        QVERIFY(!f.scheduler.status().nextSync.isValid());
    }

    void debouncesLocalChanges()
    {
        Fixture f(300'000, 20'000);
        f.scheduler.start();
        f.timers->advance(0);
        f.scheduler.notifyLocalChange();
        f.timers->advance(10'000);
        f.scheduler.notifyLocalChange();
        f.timers->advance(19'999);
        QCOMPARE(f.runner.calls, 1);
        f.timers->advance(1);
        QCOMPARE(f.runner.calls, 2);
        f.scheduler.stop();
    }

    void notifyChangesIgnoresTheEnginesOwnWrites()
    {
        Fixture f(300'000, 1000);
        f.scheduler.start();
        f.timers->advance(0);
        const VaultChange own{ChangeType::Modified, u"a.md"_s, {}, ChangeSource::Sync};
        f.scheduler.notifyChanges({own});
        f.timers->advance(5000);
        QCOMPARE(f.runner.calls, 1);
        f.scheduler.notifyChanges({own, VaultChange{ChangeType::Created, u"b.md"_s, {}, ChangeSource::User}});
        f.timers->advance(1000);
        QCOMPARE(f.runner.calls, 2);
        f.scheduler.notifyChanges({VaultChange{ChangeType::Modified, u"ż.md"_s, {}, ChangeSource::External}});
        f.timers->advance(1000);
        QCOMPARE(f.runner.calls, 3);
        f.scheduler.stop();
    }

    void notifyLocalChangeBeforeStartDoesNothing()
    {
        Fixture f;
        f.scheduler.notifyLocalChange();
        QCOMPARE(f.timers->count(), 0);
        f.timers->advance(60'000);
        QCOMPARE(f.runner.calls, 0);
    }

    void aChangeDuringARunTriggersAnotherOneAfterwards()
    {
        Fixture f(300'000, 1000);
        f.runner.autoComplete = false;
        f.scheduler.start();
        f.timers->advance(0);
        QCOMPARE(f.scheduler.state(), SyncState::Syncing);
        f.scheduler.notifyLocalChange();
        f.runner.release();
        QCOMPARE(f.scheduler.state(), SyncState::Idle);
        f.runner.autoComplete = true;
        f.timers->advance(1000);
        QCOMPARE(f.runner.calls, 2);
        f.scheduler.stop();
    }

    void syncNowRunsJoinsAndWorksWhenNotStarted()
    {
        Fixture f;
        std::optional<SyncReport> first;
        bool firstCalled = false;
        f.scheduler.syncNow([&](const std::optional<SyncReport> &r) {
            firstCalled = true;
            first = r;
        });
        QVERIFY(firstCalled);
        QVERIFY(first.has_value());
        QCOMPARE(f.runner.calls, 1);
        QCOMPARE(f.scheduler.state(), SyncState::Disabled);
        QVERIFY(f.scheduler.status().lastSync.isValid());
        QCOMPARE(f.timers->count(), 0); // not started: nothing scheduled

        f.runner.autoComplete = false;
        int joined = 0;
        f.scheduler.syncNow([&](const std::optional<SyncReport> &r) { joined += r ? 1 : 0; });
        f.scheduler.syncNow([&](const std::optional<SyncReport> &r) { joined += r ? 1 : 0; });
        QVERIFY(f.scheduler.isRunning());
        f.runner.release();
        QCOMPARE(joined, 2);
        QCOMPARE(f.runner.calls, 2);
    }

    void nothingIsAttemptedWhileOffline()
    {
        Fixture f;
        f.online = false;
        f.scheduler.start();
        QCOMPARE(f.scheduler.state(), SyncState::Offline);
        f.timers->advance(0);
        f.timers->advance(3'600'000);
        QCOMPARE(f.runner.calls, 0);
        bool answered = false;
        f.scheduler.syncNow([&](const std::optional<SyncReport> &r) {
            answered = true;
            QVERIFY(!r);
        });
        QVERIFY(answered);
        f.scheduler.notifyLocalChange();
        f.timers->advance(60'000);
        QCOMPARE(f.runner.calls, 0);
        QCOMPARE(f.scheduler.state(), SyncState::Offline);
        f.online = true;
        f.scheduler.networkOnline();
        QCOMPARE(f.runner.calls, 1);
        QCOMPARE(f.scheduler.state(), SyncState::Idle);
        f.scheduler.stop();
    }

    void goesOfflineOnTheOfflineEventWhileIdle()
    {
        Fixture f;
        f.scheduler.start();
        f.timers->advance(0);
        f.online = false;
        f.scheduler.networkOffline();
        QCOMPARE(f.scheduler.state(), SyncState::Offline);
        f.scheduler.stop();
        // Not started: network events are ignored.
        f.scheduler.networkOnline();
        QCOMPARE(f.runner.calls, 1);
    }

    void networkSlotsDecideWhenThereIsNoOtherSource()
    {
        // Platform code that reports connectivity only through the slots (no injected check, no
        // QNetworkInformation backend): after networkOffline() nothing is attempted until
        // networkOnline() — "never while offline", not a failing sync every interval.
        if (QNetworkInformation::instance())
            QSKIP("a QNetworkInformation backend is loaded");
        auto *timers = new FakeTimers;
        FakeRunner runner(timers);
        SyncScheduler scheduler(&runner);
        scheduler.setTimers(std::unique_ptr<SchedulerTimers>(timers));
        QVERIFY(scheduler.isOnline()); // nothing known: online
        scheduler.start();
        timers->advance(0);
        QCOMPARE(runner.calls, 1);
        scheduler.networkOffline();
        QVERIFY(!scheduler.isOnline());
        QCOMPARE(scheduler.state(), SyncState::Offline);
        timers->advance(3'600'000);
        scheduler.notifyLocalChange();
        timers->advance(60'000);
        bool answered = false;
        scheduler.syncNow([&](const std::optional<SyncReport> &r) {
            answered = true;
            QVERIFY(!r);
        });
        QVERIFY(answered);
        QCOMPARE(runner.calls, 1);
        QCOMPARE(scheduler.state(), SyncState::Offline);
        scheduler.networkOnline();
        QVERIFY(scheduler.isOnline());
        QCOMPARE(runner.calls, 2);
        QCOMPARE(scheduler.state(), SyncState::Idle);
        // Reported while stopped: remembered, start() shows Offline and attempts nothing.
        scheduler.stop();
        scheduler.networkOffline();
        scheduler.start();
        QCOMPARE(scheduler.state(), SyncState::Offline);
        timers->advance(0);
        QCOMPARE(runner.calls, 2);
        scheduler.stop();
    }

    void syncsWhenTheAppComesBackIfTheLastSyncIsOld()
    {
        Fixture f(3'600'000);
        f.scheduler.start();
        f.timers->advance(0);
        f.scheduler.setAppActive(true);
        QCOMPARE(f.runner.calls, 1);
        f.timers->advance(61'000);
        f.scheduler.setAppActive(false);
        QCOMPARE(f.runner.calls, 1);
        f.scheduler.setAppActive(true);
        QCOMPARE(f.runner.calls, 2);
        f.scheduler.stop();
    }

    void flushesAPendingDebouncedSyncWhenGoingToTheBackground()
    {
        Fixture f(3'600'000, 20'000);
        f.scheduler.start();
        f.timers->advance(0);
        QCOMPARE(f.runner.calls, 1);
        f.scheduler.notifyLocalChange();
        f.timers->advance(5'000);
        QCOMPARE(f.runner.calls, 1);
        // Android suspends a backgrounded app: the edit must not wait for the next app start.
        f.scheduler.setAppActive(false);
        QCOMPARE(f.runner.calls, 2);
        // Without pending changes going to the background does nothing.
        f.scheduler.setAppActive(true);
        f.scheduler.setAppActive(false);
        QCOMPARE(f.runner.calls, 2);
        f.scheduler.stop();
    }

    void reportsErrorsAndRetriesWithExponentialBackoff()
    {
        Fixture f(300'000);
        f.runner.result = [] { return failedReport(WebDavErrorCode::Server, 503, u"PROPFIND failed: HTTP 503"_s); };
        f.scheduler.start();
        f.timers->advance(0);
        QCOMPARE(f.scheduler.state(), SyncState::Error);
        QCOMPARE(f.scheduler.status().lastError, u"PROPFIND failed: HTTP 503"_s);
        QCOMPARE(f.scheduler.status().lastErrorCode, u"server"_s);
        f.timers->advance(30'000);
        QCOMPARE(f.runner.calls, 2);
        f.timers->advance(59'999);
        QCOMPARE(f.runner.calls, 2);
        f.timers->advance(1);
        QCOMPARE(f.runner.calls, 3);
        // Backoff is capped by the interval.
        f.timers->advance(120'000);
        QCOMPARE(f.runner.calls, 4);
        f.timers->advance(240'000);
        QCOMPARE(f.runner.calls, 5);
        f.timers->advance(299'999);
        QCOMPARE(f.runner.calls, 5);
        f.timers->advance(1);
        QCOMPARE(f.runner.calls, 6);
        f.runner.result = nullptr;
        f.timers->advance(300'000);
        QCOMPARE(f.runner.calls, 7);
        QCOMPARE(f.scheduler.state(), SyncState::Idle);
        QVERIFY(f.scheduler.status().lastError.isEmpty());
        QVERIFY(f.scheduler.status().lastErrorCode.isEmpty());
        f.scheduler.stop();
    }

    void keepsSyncErrorCodesForTheUi()
    {
        Fixture f;
        f.runner.result = [] {
            SyncReport r;
            r.syncError = SyncError{SyncErrorCode::RemoteEmpty, u"pusto"_s};
            return r;
        };
        std::optional<SyncReport> result{SyncReport{}};
        f.scheduler.syncNow([&](const std::optional<SyncReport> &r) { result = r; });
        QVERIFY(!result);
        QCOMPARE(f.scheduler.state(), SyncState::Error);
        QCOMPARE(f.scheduler.status().lastErrorCode, u"remote-empty"_s);
        QCOMPARE(f.scheduler.status().lastError, u"pusto"_s);
    }

    void aNetworkFailureWhileGoingOfflineShowsOffline()
    {
        Fixture f;
        f.runner.result = [&f] {
            f.online = false;
            return failedReport(WebDavErrorCode::Network, 0, u"offline"_s);
        };
        f.scheduler.syncNow();
        QCOMPARE(f.scheduler.state(), SyncState::Offline);
    }

    void perFileErrorsAreSummarised()
    {
        Fixture f;
        f.runner.result = [] {
            SyncReport r;
            r.errors = {SyncFileError{u"zły.md"_s, u"HTTP 500"_s}, SyncFileError{u"b.md"_s, u"x"_s}};
            return r;
        };
        QSignalSpy synced(&f.scheduler, &SyncScheduler::synced);
        std::optional<SyncReport> result;
        f.scheduler.syncNow([&](const std::optional<SyncReport> &r) { result = r; });
        QVERIFY(result); // the run itself completed
        QCOMPARE(synced.size(), 1);
        QCOMPARE(f.scheduler.state(), SyncState::Error);
        QCOMPARE(f.scheduler.status().lastError, u"zły.md: HTTP 500 (+1 more)"_s);
        QCOMPARE(f.scheduler.status().lastErrorCode, u"files"_s);
    }

    void stopCancelsARunningSyncAndClearsTimers()
    {
        Fixture f;
        f.runner.autoComplete = false;
        f.scheduler.start();
        f.timers->advance(0);
        QCOMPARE(f.runner.calls, 1);
        f.scheduler.stop();
        QCOMPARE(f.runner.cancels, 1);
        QCOMPARE(f.scheduler.state(), SyncState::Disabled);
        QVERIFY(!f.scheduler.isRunning());
        QCOMPARE(f.timers->count(), 0);
        f.scheduler.notifyLocalChange();
        QCOMPARE(f.timers->count(), 0);
        QVERIFY(!f.scheduler.status().lastSync.isValid()); // a cancelled run is no sync
    }

    void aRestartWhileTheStoppedRunIsStillCancellingSyncsRightAfter()
    {
        Fixture f;
        f.runner.autoComplete = false;
        f.runner.cancelCompletes = false;
        f.scheduler.start();
        f.timers->advance(0);
        QCOMPARE(f.runner.calls, 1);
        f.scheduler.stop();
        f.scheduler.start();
        f.timers->advance(0);
        QCOMPARE(f.runner.calls, 1); // waits for the cancelled run
        f.runner.autoComplete = true;
        f.runner.finishCancelled();
        QCOMPARE(f.runner.calls, 2);
        QCOMPARE(f.scheduler.state(), SyncState::Idle);
        f.scheduler.stop();
    }

    void syncNowDuringACancellationWaitsForTheNextRun()
    {
        Fixture f;
        f.runner.autoComplete = false;
        f.runner.cancelCompletes = false;
        f.scheduler.start();
        f.timers->advance(0);
        f.scheduler.stop();
        std::optional<SyncReport> result;
        bool called = false;
        f.scheduler.syncNow([&](const std::optional<SyncReport> &r) {
            called = true;
            result = r;
        });
        QVERIFY(!called);
        f.runner.finishCancelled(); // the cancelled run ends, the requested one starts
        QCOMPARE(f.runner.calls, 2);
        QVERIFY(!called);
        f.runner.release();
        QVERIFY(called);
        QVERIFY(result);
    }

    void syncNowFromASyncedHandlerWaitsForItsOwnRun()
    {
        Fixture f;
        f.runner.autoComplete = false;
        int answers = 0;
        std::optional<SyncReport> first;
        bool asked = false;
        QObject::connect(&f.scheduler, &SyncScheduler::synced, &f.scheduler, [&] {
            if (std::exchange(asked, true))
                return;
            f.scheduler.syncNow([&](const std::optional<SyncReport> &) { ++answers; });
        });
        f.scheduler.syncNow([&](const std::optional<SyncReport> &r) { first = r; });
        f.runner.release(); // the first run completes; the handler starts a second one
        QVERIFY(first);
        QCOMPARE(f.runner.calls, 2);
        QVERIFY(f.scheduler.isRunning());
        QCOMPARE(answers, 0); // not answered with the previous run's report
        f.runner.release();
        QCOMPARE(answers, 1);
    }

    void startAndStopAreIdempotent()
    {
        Fixture f;
        f.scheduler.start();
        f.scheduler.start();
        f.timers->advance(0);
        QCOMPARE(f.runner.calls, 1);
        f.scheduler.stop();
        f.scheduler.stop();
        QVERIFY(!f.scheduler.isActive());
        QCOMPARE(f.runner.cancels, 0);
    }

    void passesProgressIntoTheStatus()
    {
        Fixture f;
        f.runner.result = [&f] {
            f.runner.lastOptions.onProgress(SyncProgress{SyncProgress::Phase::Syncing, 1, 3, u"a.md"_s});
            return SyncReport{};
        };
        f.scheduler.syncNow();
        bool seen = false;
        for (const SyncStatus &s : f.statuses)
            seen = seen || (s.progress && s.progress->path == u"a.md"_s && s.state == SyncState::Syncing);
        QVERIFY(seen);
        QVERIFY(!f.scheduler.status().progress);
    }

    void statusChangedIsEmitted()
    {
        Fixture f;
        QSignalSpy spy(&f.scheduler, &SyncScheduler::statusChanged);
        f.scheduler.start();
        QVERIFY(spy.size() >= 1);
        const qsizetype before = spy.size();
        f.timers->advance(0);
        QVERIFY(spy.size() > before);
        QCOMPARE(syncStateName(f.scheduler.state()), u"idle"_s);
        QCOMPARE(syncStateName(SyncState::Offline), u"offline"_s);
        f.scheduler.stop();
    }

    void worksWithTheDefaultTimers()
    {
        FakeTimers unused;
        FakeRunner runner(&unused);
        SyncScheduler scheduler(&runner);
        scheduler.setOnlineCheck([] { return true; });
        scheduler.setInterval(1000);
        scheduler.start();
        QTRY_COMPARE(runner.calls, 1);
        QTRY_COMPARE_WITH_TIMEOUT(runner.calls, 2, 5000);
        scheduler.stop();
        const int calls = runner.calls;
        QTest::qWait(1200);
        QCOMPARE(runner.calls, calls);
    }
};

QTEST_GUILESS_MAIN(TestSyncScheduler)
#include "tst_sync_scheduler.moc"
