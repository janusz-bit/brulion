#include "sync/scheduler.h"

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

void SyncScheduler::syncNow(RunCallback done)
{
    if (done) {
        if (m_running && m_cancelling)
            m_nextWaiters.append(std::move(done));
        else
            m_waiters.append(std::move(done));
    }
    trigger();
}

void SyncScheduler::syncNowConfirmingDeletions(RunCallback done)
{
    m_confirmDeletions = true;
    if (m_running) {
        // The running sync planned without the confirmation: run again once it is done.
        if (done)
            m_nextWaiters.append(std::move(done));
        m_triggerAfterRun = true;
        return;
    }
    syncNow(std::move(done));
}

void SyncScheduler::start()
{
    if (m_started)
        return;
    m_started = true;
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
    updateStatus([](SyncStatus &s) {
        s.state = SyncState::Disabled;
        s.nextSync = QDateTime();
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
        trigger();
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
    m_reportedOnline = true;
    if (m_started)
        trigger();
}

void SyncScheduler::networkOffline()
{
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
        if (m_debounceTimer != 0)
            trigger();
        return;
    }
    const qint64 lastSync = m_status.lastSync.isValid() ? m_status.lastSync.toMSecsSinceEpoch() : 0;
    if (m_timers->now() - qMax(lastSync, m_lastAttempt) >= m_activeGap)
        trigger();
}

// ---------------------------------------------------------------------------------------------
// Internals
// ---------------------------------------------------------------------------------------------

void SyncScheduler::trigger()
{
    if (m_running) {
        if (m_cancelling) // stop() then start(): sync again once the cancelled run settled
            m_triggerAfterRun = true;
        return; // joins the running sync
    }
    // A confirmation is for this run only (never for a later automatic one).
    const bool confirmDeletions = std::exchange(m_confirmDeletions, false);
    if (!m_runner) {
        notifyWaiters(std::nullopt);
        return;
    }
    if (!isOnline()) {
        updateStatus([](SyncStatus &s) {
            s.state = SyncState::Offline;
            s.progress.reset();
        });
        // Keep checking periodically (cheap, no network) in case no "online" event comes.
        schedulePeriodic(m_interval);
        notifyWaiters(std::nullopt);
        return;
    }
    cancelTimer(m_debounceTimer);
    cancelTimer(m_periodicTimer);
    m_running = true;
    m_cancelling = false;
    m_lastAttempt = m_timers->now();
    updateStatus([](SyncStatus &s) {
        s.state = SyncState::Syncing;
        s.nextSync = QDateTime();
        s.progress.reset();
    });
    SyncRunOptions options;
    options.confirmDeletions = confirmDeletions;
    options.onProgress = [guard = QPointer<SyncScheduler>(this)](const SyncProgress &progress) {
        if (guard && guard->m_running)
            guard->updateStatus([&](SyncStatus &s) { s.progress = progress; });
    };
    m_runner->startSync(options, [guard = QPointer<SyncScheduler>(this)](const SyncReport &report) {
        if (guard)
            guard->onRunFinished(report);
    });
}

void SyncScheduler::onRunFinished(const SyncReport &report)
{
    if (!m_running)
        return;
    m_running = false;
    const bool cancelling = std::exchange(m_cancelling, false);
    // Taken now: handlers of statusChanged() / synced() may call syncNow() for the next run.
    const QList<RunCallback> waiters = std::exchange(m_waiters, {});
    std::optional<SyncReport> result;
    if (report.cancelled || (cancelling && report.failed())) {
        updateStatus([this](SyncStatus &s) {
            s.state = idleState();
            s.progress.reset();
        });
        schedulePeriodic(m_interval);
    } else if (report.failed()) {
        ++m_failures;
        const bool online = isOnline();
        updateStatus([&](SyncStatus &s) {
            s.state = online ? SyncState::Error : SyncState::Offline;
            s.lastError = report.failureMessage();
            s.lastErrorCode = report.failureCode();
            s.progress.reset();
        });
        const int doublings = qMin(m_failures - 1, 30);
        schedulePeriodic(qMin(m_interval, m_retryBase * (qint64(1) << doublings)));
    } else {
        m_failures = 0;
        QString errors = summarizeErrors(report);
        QString code = errors.isEmpty() ? QString() : u"files"_s;
        if (report.needsConfirmation) {
            errors = u"%1 deletion(s) wait for confirmation"_s.arg(report.pendingDeletions.size());
            code = u"needs-confirmation"_s;
        }
        updateStatus([&](SyncStatus &s) {
            s.state = errors.isEmpty() ? idleState() : SyncState::Error;
            s.lastSync = report.finished;
            s.lastReport = report;
            s.lastError = errors;
            s.lastErrorCode = code;
            s.progress.reset();
        });
        schedulePeriodic(m_interval);
        result = report;
        Q_EMIT synced(report);
    }
    for (const RunCallback &callback : waiters)
        callback(result);
    if (m_triggerAfterRun) {
        m_triggerAfterRun = false;
        m_waiters += std::exchange(m_nextWaiters, {});
        trigger();
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

void SyncScheduler::schedulePeriodic(qint64 delay)
{
    cancelTimer(m_periodicTimer);
    if (!m_started)
        return;
    m_periodicTimer = m_timers->start(delay, [this] {
        m_periodicTimer = 0;
        trigger();
    });
    const qint64 at = m_timers->now() + delay;
    updateStatus([at](SyncStatus &s) { s.nextSync = QDateTime::fromMSecsSinceEpoch(at); });
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
