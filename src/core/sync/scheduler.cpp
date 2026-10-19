#include "sync/scheduler.h"

#include "log/categories.h"
#include "log/redact.h"
#include "log/slowscope.h"

#include <QHash>
#include <QNetworkInformation>
#include <QPointer>
#include <QTimer>

#include <algorithm>
#include <limits>

using namespace Qt::StringLiterals;

namespace brulion::sync {

namespace {

/// SchedulerTimers on QTimer and the system clock.
class QtSchedulerTimers : public SchedulerTimers {
public:
    explicit QtSchedulerTimers(QObject *owner)
        : m_owner(owner)
    {
    }

    qint64 now() const override { return QDateTime::currentMSecsSinceEpoch(); }

    int start(qint64 delayMs, std::function<void()> callback) override
    {
        const int handle = ++m_next;
        auto *timer = new QTimer(m_owner);
        timer->setSingleShot(true);
        m_timers.insert(handle, timer);
        QObject::connect(timer, &QTimer::timeout, m_owner, [this, handle, callback = std::move(callback)] {
            if (QTimer *fired = m_timers.take(handle))
                fired->deleteLater();
            callback();
        });
        const qint64 clamped = std::clamp<qint64>(delayMs, 0, std::numeric_limits<int>::max());
        timer->start(static_cast<int>(clamped));
        return handle;
    }

    void cancel(int handle) override
    {
        if (QTimer *timer = m_timers.take(handle)) {
            timer->stop();
            timer->deleteLater();
        }
    }

private:
    QObject *m_owner;
    QHash<int, QTimer *> m_timers;
    int m_next = 0;
};

bool reachable(const QNetworkInformation *info)
{
    return !info || info->reachability() != QNetworkInformation::Reachability::Disconnected;
}

/// One-line summary of the per-file errors of a run ("" when there are none).
QString summarizeErrors(const SyncReport &report)
{
    if (report.errors.isEmpty())
        return {};
    const SyncFileError &first = report.errors.first();
    QString summary = first.path + u": "_s + first.message;
    if (report.errors.size() > 1)
        summary += u" (+%1 more)"_s.arg(report.errors.size() - 1);
    return summary;
}

QString runTag(int number)
{
    return number > 0 ? u"#%1 "_s.arg(number) : QString();
}

/// "connecting", "listing 12 of 40", "syncing 120 of 3000" — for the heartbeat line.
QString progressText(const std::optional<SyncProgress> &progress)
{
    if (!progress)
        return u"starting"_s;
    switch (progress->phase) {
    case SyncProgress::Phase::Connecting: return u"connecting"_s;
    case SyncProgress::Phase::Listing:
        return u"listing the server, folder %1 of %2"_s.arg(progress->done).arg(progress->total);
    case SyncProgress::Phase::ListingLocal: return u"listing this device"_s;
    case SyncProgress::Phase::Comparing: return u"comparing"_s;
    case SyncProgress::Phase::Syncing: return u"syncing %1 of %2"_s.arg(progress->done).arg(progress->total);
    case SyncProgress::Phase::Saving: return u"saving the sync state"_s;
    case SyncProgress::Phase::Done: return u"done"_s;
    }
    return {};
}

/// Seconds, rounded up, for messages ("20 s").
QString seconds(qint64 ms)
{
    return u"%1 s"_s.arg((ms + 999) / 1000);
}

} // namespace

QString syncStateName(SyncState state)
{
    switch (state) {
    case SyncState::Disabled: return u"disabled"_s;
    case SyncState::Idle: return u"idle"_s;
    case SyncState::Syncing: return u"syncing"_s;
    case SyncState::Error: return u"error"_s;
    case SyncState::Offline: return u"offline"_s;
    }
    return {};
}

SyncScheduler::SyncScheduler(SyncRunner *runner, QObject *parent)
    : QObject(parent)
    , m_runner(runner)
    , m_timers(std::make_unique<QtSchedulerTimers>(this))
{
    Q_ASSERT(runner);
    // The app may already have loaded a network information backend: follow it.
    if (QNetworkInformation::instance())
        useSystemNetworkInformation();
}

SyncScheduler::~SyncScheduler()
{
    // Pending timers die with this object; a running sync keeps going without us.
    m_started = false;
}

void SyncScheduler::setInterval(qint64 msec)
{
    m_interval = qMax<qint64>(1000, msec);
}

void SyncScheduler::setDebounce(qint64 msec)
{
    m_debounce = qMax<qint64>(0, msec);
}

void SyncScheduler::setRetryBase(qint64 msec)
{
    m_retryBase = qMax<qint64>(1000, msec);
}

void SyncScheduler::setActiveSyncGap(qint64 msec)
{
    m_activeGap = qMax<qint64>(0, msec);
}

void SyncScheduler::setStallThreshold(qint64 msec)
{
    m_stallThreshold = qMax<qint64>(1000, msec);
}

void SyncScheduler::setStallLimit(qint64 msec)
{
    m_stallLimit = qMax<qint64>(0, msec);
}

void SyncScheduler::setStallPollInterval(qint64 msec)
{
    m_stallPoll = qMax<qint64>(100, msec);
}

void SyncScheduler::setTimers(std::unique_ptr<SchedulerTimers> timers)
{
    if (!timers)
        return;
    cancelTimer(m_periodicTimer);
    cancelTimer(m_debounceTimer);
    m_timers = std::move(timers);
}

void SyncScheduler::setOnlineCheck(std::function<bool()> isOnline)
{
    m_isOnline = std::move(isOnline);
}

bool SyncScheduler::useSystemNetworkInformation()
{
    if (!QNetworkInformation::instance())
        QNetworkInformation::loadDefaultBackend();
    QNetworkInformation *info = QNetworkInformation::instance();
    if (!info || !info->supports(QNetworkInformation::Feature::Reachability))
        return false;
    if (!m_networkConnected) {
        m_networkConnected = true;
        connect(info, &QNetworkInformation::reachabilityChanged, this,
                [this](QNetworkInformation::Reachability reachability) {
                    if (reachability == QNetworkInformation::Reachability::Disconnected)
                        networkOffline();
                    else
                        networkOnline();
                });
    }
    if (!m_isOnline)
        m_isOnline = [] { return reachable(QNetworkInformation::instance()); };
    return true;
}

bool SyncScheduler::isOnline() const
{
    if (m_isOnline)
        return m_isOnline();
    if (const QNetworkInformation *info = QNetworkInformation::instance();
        info && info->supports(QNetworkInformation::Feature::Reachability))
        return reachable(info);
    // No other source: what networkOnline() / networkOffline() reported last (online if nothing).
    return m_reportedOnline;
}

void SyncScheduler::syncNow(RunCallback done, SyncTrigger why)
{
    if (done) {
        if (m_running && m_cancelling)
            m_nextWaiters.append(std::move(done));
        else
            m_waiters.append(std::move(done));
    }
    trigger(why);
}

void SyncScheduler::syncNowConfirmingDeletions(RunCallback done)
{
    m_confirmDeletions = true;
    if (m_running) {
        // The running sync planned without the confirmation: run again once it is done.
        if (done)
            m_nextWaiters.append(std::move(done));
        m_triggerAfterRun = true;
        m_afterRunTrigger = SyncTrigger::Confirmation;
        return;
    }
    syncNow(std::move(done), SyncTrigger::Confirmation);
}

void SyncScheduler::start()
{
    if (m_started)
        return;
    m_started = true;
    m_firstRun = true;
    const bool online = isOnline();
    updateStatus([&](SyncStatus &s) { s.state = m_running ? SyncState::Syncing : online ? SyncState::Idle : SyncState::Offline; });
    schedulePeriodic(0);
}

void SyncScheduler::stop()
{
    if (!m_started)
        return;
    m_started = false;
    cancelTimer(m_periodicTimer);
    cancelTimer(m_debounceTimer);
    m_rerun = false;
    m_retryPlanned = false;
    updateStatus([](SyncStatus &s) {
        s.state = SyncState::Disabled;
        s.nextSync = QDateTime();
        s.retrying = false;
        s.progress.reset();
    });
    if (m_running) {
        m_cancelling = true;
        m_runner->cancelSync();
    }
}

void SyncScheduler::notifyLocalChange()
{
    if (!m_started)
        return;
    if (m_running) {
        m_rerun = true;
        return;
    }
    cancelTimer(m_debounceTimer);
    m_debounceTimer = m_timers->start(m_debounce, [this] {
        m_debounceTimer = 0;
        trigger(SyncTrigger::LocalChange);
    });
}

void SyncScheduler::notifyChanges(const QList<VaultChange> &changes)
{
    const bool local = std::any_of(changes.cbegin(), changes.cend(),
                                   [](const VaultChange &c) { return c.source != ChangeSource::Sync; });
    if (local)
        notifyLocalChange();
}

void SyncScheduler::networkOnline()
{
    if (!m_reportedOnline)
        qCInfo(logging::lcSync) << "network: online";
    m_reportedOnline = true;
    if (m_started)
        trigger(SyncTrigger::NetworkBack);
}

void SyncScheduler::networkOffline()
{
    if (m_reportedOnline)
        qCInfo(logging::lcSync) << "network: offline";
    m_reportedOnline = false;
    if (!m_started || m_running)
        return;
    updateStatus([](SyncStatus &s) { s.state = SyncState::Offline; });
}

void SyncScheduler::setAppActive(bool active)
{
    if (!m_started)
        return;
    if (!active) {
        // Going to the background (Android may suspend the app): pending edits must not wait
        // for the debounce timer, which would only fire at the next app start.
        if (m_debounceTimer != 0) {
            qCInfo(logging::lcSync) << "app in the background with pending edits: syncing now";
            trigger(SyncTrigger::Background);
        }
        return;
    }
    const qint64 lastSync = m_status.lastSync.isValid() ? m_status.lastSync.toMSecsSinceEpoch() : 0;
    if (m_timers->now() - qMax(lastSync, m_lastAttempt) >= m_activeGap)
        trigger(SyncTrigger::Foreground);
}

// ---------------------------------------------------------------------------------------------
// Internals
// ---------------------------------------------------------------------------------------------

void SyncScheduler::trigger(SyncTrigger why)
{
    if (m_running) {
        if (m_cancelling) { // stop() then start(): sync again once the cancelled run settled
            m_triggerAfterRun = true;
            m_afterRunTrigger = why;
        } else {
            qCDebug(logging::lcSync).noquote() << u"joined the running sync (%1)"_s.arg(syncTriggerName(why));
        }
        return; // joins the running sync
    }
    // A confirmation is for this run only (never for a later automatic one).
    const bool confirmDeletions = std::exchange(m_confirmDeletions, false);
    if (!m_runner) {
        notifyWaiters(std::nullopt);
        return;
    }
    if (!isOnline()) {
        qCInfo(logging::lcSync).noquote() << u"sync skipped: offline"_s;
        updateStatus([](SyncStatus &s) {
            s.state = SyncState::Offline;
            s.progress.reset();
        });
        // Keep checking periodically (cheap, no network) in case no "online" event comes.
        schedulePeriodic(m_interval, m_retryPlanned);
        notifyWaiters(std::nullopt);
        return;
    }
    cancelTimer(m_debounceTimer);
    cancelTimer(m_periodicTimer);
    m_running = true;
    m_cancelling = false;
    m_watchdogCancelled = false;
    m_retryPlanned = false;
    m_lastAttempt = m_timers->now();
    m_runStartedMs = m_lastAttempt;
    m_lastHeartbeatMs = 0;
    updateStatus([&](SyncStatus &s) {
        s.state = SyncState::Syncing;
        s.nextSync = QDateTime();
        s.retrying = false;
        s.progress.reset();
        s.trigger = why;
        s.runStarted = QDateTime::fromMSecsSinceEpoch(m_runStartedMs);
        s.stalled = false;
        s.stalledMs = 0;
        s.waitingFor.clear();
        s.abandoning = false;
    });
    Q_EMIT runStarted(why);
    scheduleStallPoll();
    SyncRunOptions options;
    options.confirmDeletions = confirmDeletions;
    options.trigger = why;
    options.onProgress = [guard = QPointer<SyncScheduler>(this)](const SyncProgress &progress) {
        if (guard && guard->m_running) {
            guard->m_status.progress = progress;
            Q_EMIT guard->progressChanged();
        }
    };
    m_runner->startSync(options, [guard = QPointer<SyncScheduler>(this)](const SyncReport &report) {
        if (guard)
            guard->onRunFinished(report);
    });
}

void SyncScheduler::scheduleStallPoll()
{
    cancelTimer(m_stallTimer);
    if (!m_running)
        return;
    m_stallTimer = m_timers->start(m_stallPoll, [this] {
        m_stallTimer = 0;
        pollStall();
    });
}

void SyncScheduler::pollStall()
{
    if (!m_running)
        return;
    const SyncActivity activity = m_runner->activity();
    const QString tag = runTag(activity.runNumber);
    const qint64 runMs = m_timers->now() - m_runStartedMs;
    // A heartbeat every 30 s: a long run shows what it is doing.
    if (runMs - m_lastHeartbeatMs >= 30'000) {
        m_lastHeartbeatMs = runMs - runMs % 30'000;
        QString line =
                u"%1still running after %2: %3"_s.arg(tag, logging::formatMs(runMs), progressText(m_status.progress));
        if (activity.requestsInFlight > 0)
            line += u", %1 request(s) in flight (%2)"_s.arg(activity.requestsInFlight)
                            .arg(activity.waitingFor.join(u", "_s));
        qCInfo(logging::lcSync).noquote() << line;
    }
    if (activity.running) {
        const qint64 idle = activity.idleMs;
        if (idle >= m_stallThreshold) {
            const bool first = !m_status.stalled;
            if (first) {
                qCWarning(logging::lcSync).noquote() << u"%1no answer from the server for %2 (waiting for: %3)"_s.arg(
                        tag, seconds(idle),
                        activity.waitingFor.isEmpty() ? u"nothing; local work"_s : activity.waitingFor.join(u", "_s));
            }
            updateStatus([&](SyncStatus &s) {
                s.stalled = true;
                s.stalledMs = idle;
                s.waitingFor = activity.waitingFor;
            });
        } else if (m_status.stalled) {
            qCInfo(logging::lcSync).noquote()
                    << u"%1the server answered again after %2"_s.arg(tag, seconds(m_status.stalledMs));
            updateStatus([](SyncStatus &s) {
                s.stalled = false;
                s.stalledMs = 0;
                s.waitingFor.clear();
            });
        }
        if (m_stallLimit > 0 && idle >= m_stallLimit && !m_watchdogCancelled) {
            m_watchdogCancelled = true;
            qCWarning(logging::lcSync).noquote()
                    << u"%1no answer or progress for %2: stopping the run (it ends when the open requests time out)"_s
                               .arg(tag, seconds(idle));
            m_stalledSinceMs = idle;
            updateStatus([](SyncStatus &s) { s.abandoning = true; });
            m_runner->cancelSync();
        }
    }
    scheduleStallPoll();
}

void SyncScheduler::onRunFinished(const SyncReport &report)
{
    if (!m_running)
        return;
    m_running = false;
    cancelTimer(m_stallTimer);
    const bool cancelling = std::exchange(m_cancelling, false);
    const bool stalled = std::exchange(m_watchdogCancelled, false);
    // Taken now: handlers of statusChanged() / synced() may call syncNow() for the next run.
    const QList<RunCallback> waiters = std::exchange(m_waiters, {});
    std::optional<SyncReport> result;
    const auto clearRun = [](SyncStatus &s) {
        s.progress.reset();
        s.runStarted = QDateTime();
        s.stalled = false;
        s.stalledMs = 0;
        s.waitingFor.clear();
        s.abandoning = false;
    };
    const auto planRetry = [this](const QString &code) {
        const int doublings = qMin(m_failures - 1, 30);
        const qint64 delay = qMin(m_interval, m_retryBase * (qint64(1) << doublings));
        qCInfo(logging::lcSync).noquote()
                << u"retry in %1 after %2 failed run(s) (%3)"_s.arg(logging::formatMs(delay)).arg(m_failures).arg(code);
        schedulePeriodic(delay, true);
    };
    if (stalled && !cancelling) {
        ++m_failures;
        const bool online = isOnline();
        const qint64 limit = m_stallLimit;
        updateStatus([&](SyncStatus &s) {
            clearRun(s);
            s.state = online ? SyncState::Error : SyncState::Offline;
            s.lastError =
                    u"No answer or progress for %1; the run was stopped"_s.arg(seconds(qMax(limit, m_stalledSinceMs)));
            s.lastErrorCode = u"stalled"_s;
            s.failures = m_failures;
        });
        Q_EMIT runFinished(report, true);
        planRetry(u"stalled"_s);
    } else if (report.cancelled || (cancelling && report.failed())) {
        updateStatus([&](SyncStatus &s) {
            clearRun(s);
            s.state = idleState();
        });
        Q_EMIT runFinished(report, false);
        schedulePeriodic(m_interval);
    } else if (report.failed()) {
        ++m_failures;
        const bool online = isOnline();
        updateStatus([&](SyncStatus &s) {
            clearRun(s);
            s.state = online ? SyncState::Error : SyncState::Offline;
            s.lastError = logging::redact(report.failureMessage());
            s.lastErrorCode = report.failureCode();
            s.failures = m_failures;
        });
        Q_EMIT runFinished(report, false);
        planRetry(report.failureCode());
    } else {
        m_failures = 0;
        QString errors = summarizeErrors(report);
        QString code = errors.isEmpty() ? QString() : u"files"_s;
        if (report.needsConfirmation) {
            errors = u"%1 deletion(s) wait for confirmation"_s.arg(report.pendingDeletions.size());
            code = u"needs-confirmation"_s;
        }
        updateStatus([&](SyncStatus &s) {
            clearRun(s);
            s.state = errors.isEmpty() ? idleState() : SyncState::Error;
            s.lastSync = report.finished;
            s.lastReport = report;
            s.lastError = logging::redact(errors);
            s.lastErrorCode = code;
            s.failures = 0;
        });
        Q_EMIT runFinished(report, false);
        schedulePeriodic(m_interval);
        result = report;
        Q_EMIT synced(report);
    }
    for (const RunCallback &callback : waiters)
        callback(result);
    if (m_triggerAfterRun) {
        m_triggerAfterRun = false;
        m_waiters += std::exchange(m_nextWaiters, {});
        trigger(std::exchange(m_afterRunTrigger, SyncTrigger::Manual));
    } else if (m_rerun && m_started) {
        m_rerun = false;
        notifyLocalChange();
    }
}

void SyncScheduler::notifyWaiters(const std::optional<SyncReport> &result)
{
    const QList<RunCallback> waiters = std::exchange(m_waiters, {});
    for (const RunCallback &callback : waiters)
        callback(result);
}

void SyncScheduler::schedulePeriodic(qint64 delay, bool retry)
{
    cancelTimer(m_periodicTimer);
    if (!m_started)
        return;
    m_retryPlanned = retry;
    m_periodicTimer = m_timers->start(delay, [this] {
        m_periodicTimer = 0;
        const SyncTrigger why = m_retryPlanned ? SyncTrigger::Retry
                                : m_firstRun   ? SyncTrigger::Start
                                               : SyncTrigger::Timer;
        m_firstRun = false;
        trigger(why);
    });
    const qint64 at = m_timers->now() + delay;
    if (delay > 0)
        qCDebug(logging::lcSync).noquote()
                << u"next sync at %1"_s.arg(QDateTime::fromMSecsSinceEpoch(at).toString(u"HH:mm:ss"_s));
    updateStatus([at, retry](SyncStatus &s) {
        s.nextSync = QDateTime::fromMSecsSinceEpoch(at);
        s.retrying = retry;
    });
}

void SyncScheduler::cancelTimer(int &handle)
{
    if (handle != 0)
        m_timers->cancel(handle);
    handle = 0;
}

void SyncScheduler::updateStatus(const std::function<void(SyncStatus &)> &change)
{
    change(m_status);
    Q_EMIT statusChanged();
}

SyncState SyncScheduler::idleState() const
{
    return m_started ? SyncState::Idle : SyncState::Disabled;
}

} // namespace brulion::sync
