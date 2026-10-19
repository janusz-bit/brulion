// When to sync: periodically, shortly after local edits (debounced), when the device comes back
// online, when the app returns to the foreground and — with pending edits — when it goes to the
// background. Never while offline. Exposes an observable status for the UI.
#pragma once

#include "sync/synctypes.h"
#include "types.h"

#include <QDateTime>
#include <QList>
#include <QObject>
#include <QString>

#include <functional>
#include <memory>
#include <optional>

namespace brulion::sync {

enum class SyncState {
    Disabled, ///< not started (manual syncNow() still works)
    Idle,     ///< started, waiting for the next sync
    Syncing,
    Error,    ///< the last run failed (see lastError) or reported per-file errors
    Offline,  ///< no network: nothing is attempted until it comes back
};

/// "disabled", "idle", "syncing", "error", "offline".
QString syncStateName(SyncState state);

/// Observable status of a SyncScheduler.
struct SyncStatus {
    SyncState state = SyncState::Disabled;
    QDateTime lastSync;      ///< finish time of the last successful run (invalid: none yet)
    QString lastError;       ///< message of the last failure; empty after a clean run
    /// errorCodeName() of the last failure ("auth", "network", "remote-empty", …), "files" for
    /// per-file errors of an otherwise completed run, "needs-confirmation" when the mass-deletion
    /// guard held deletions back (lastReport->pendingDeletions; see syncNowConfirmingDeletions());
    /// empty after a clean run.
    QString lastErrorCode;
    std::optional<SyncReport> lastReport;   ///< report of the last completed run
    std::optional<SyncProgress> progress;   ///< progress of the running sync
    QDateTime nextSync;      ///< when the next automatic sync is planned (invalid: none)
    /// nextSync is a retry after failed runs (back-off), not the regular interval.
    bool retrying = false;
    int failures = 0;                           ///< consecutive failed runs
    SyncTrigger trigger = SyncTrigger::Unknown; ///< of the running run (or the last one)
    QDateTime runStarted;                       ///< start of the running run (invalid when idle)
    /// The running run has had no answer / progress for at least the stall threshold.
    bool stalled = false;
    qint64 stalledMs = 0;   ///< how long (while stalled)
    QStringList waitingFor; ///< SyncActivity::waitingFor while stalled
    /// The watchdog stopped the run (no progress for the stall limit): it ends as soon as the
    /// open requests time out, and is reported as failed with code "stalled".
    bool abandoning = false;
};

/// Clock and one-shot timers of the scheduler; injectable so tests control time.
class SchedulerTimers {
public:
    virtual ~SchedulerTimers() = default;
    /// Current time, ms since epoch.
    virtual qint64 now() const = 0;
    /// Run `callback` once after `delayMs`; returns a handle (> 0) for cancel().
    virtual int start(qint64 delayMs, std::function<void()> callback) = 0;
    /// Cancel a pending timer (unknown or fired handles are ignored).
    virtual void cancel(int handle) = 0;
};

/// Drives a SyncRunner (normally the SyncEngine): a first sync right after start(), then every
/// interval (default 5 min); notifyLocalChange() syncs after `debounce` (default 20 s) without
/// further edits (a change during a run schedules another one afterwards); a failed run is
/// retried after retryBase (30 s), doubling up to the interval. Online detection: an injected
/// check, or QNetworkInformation (useSystemNetworkInformation(), or automatically when the app
/// has already loaded a backend); without either, the last networkOnline() / networkOffline()
/// call decides (online until told otherwise). The app reports foreground/background changes
/// with setAppActive().
class SyncScheduler : public QObject {
    Q_OBJECT
public:
    using RunCallback = std::function<void(const std::optional<SyncReport> &)>;

    /// `runner` is not owned and must outlive the scheduler.
    explicit SyncScheduler(SyncRunner *runner, QObject *parent = nullptr);
    ~SyncScheduler() override;
    Q_DISABLE_COPY_MOVE(SyncScheduler)

    void setInterval(qint64 msec);    ///< default 300 000, at least 1 000
    void setDebounce(qint64 msec);    ///< default 20 000
    void setRetryBase(qint64 msec);   ///< default 30 000, at least 1 000
    /// Coming to the foreground syncs only when the last sync is older than this (default 60 000).
    void setActiveSyncGap(qint64 msec);
    /// A running sync without any answer / progress for this long is "stalled" (status, a log
    /// warning; default 20 000).
    void setStallThreshold(qint64 msec);
    /// ... and for this long is stopped by the watchdog: cancelled, reported as failed with code
    /// "stalled" and retried with the back-off (default 180 000; 0 = never).
    void setStallLimit(qint64 msec);
    /// How often the running sync is checked (default 2 000; the timer runs only while syncing).
    void setStallPollInterval(qint64 msec);
    qint64 stallThreshold() const { return m_stallThreshold; }
    qint64 stallLimit() const { return m_stallLimit; }
    qint64 interval() const { return m_interval; }
    qint64 debounce() const { return m_debounce; }

    /// Replace the clock/timers (tests); call before start().
    void setTimers(std::unique_ptr<SchedulerTimers> timers);
    /// Replace the online check (tests, platform code); overrides QNetworkInformation.
    void setOnlineCheck(std::function<bool()> isOnline);
    /// Load the platform's QNetworkInformation backend and follow its reachability. False when
    /// there is none (then the device counts as online unless setOnlineCheck() says otherwise).
    bool useSystemNetworkInformation();

    SyncStatus status() const { return m_status; }
    SyncState state() const { return m_status.state; }
    /// start() was called (and stop() not since).
    bool isActive() const { return m_started; }
    bool isOnline() const;
    bool isRunning() const { return m_running; }

    /// Sync now — also when not started (a "sync now" button). `done` gets the report, or
    /// std::nullopt when offline, failed or cancelled (see status().lastError); it may run
    /// synchronously when offline. Joins a running sync.
    void syncNow(RunCallback done = {}, SyncTrigger trigger = SyncTrigger::Manual);
    /// Sync now applying the deletions the mass-deletion guard held back (the user confirmed
    /// status().lastReport->pendingDeletions): SyncRunOptions::confirmDeletions for one run.
    /// While a sync is running, it runs right after it.
    void syncNowConfirmingDeletions(RunCallback done = {});

public Q_SLOTS:
    /// Start automatic syncing: a first sync right away, then periodically. Idempotent.
    void start();
    /// Stop automatic syncing and cancel a running sync (files already synced stay synced).
    void stop();
    /// A note was edited locally: sync after the debounce delay without further edits.
    void notifyLocalChange();
    /// Feed Vault::changed(); the engine's own writes (ChangeSource::Sync) are ignored.
    void notifyChanges(const QList<brulion::VaultChange> &changes);
    /// The network came back: sync now (when started). Also remembered as the online state
    /// when there is no online check and no QNetworkInformation backend.
    void networkOnline();
    /// The network went away: show Offline (unless a sync is running). Without an online check
    /// or QNetworkInformation backend, nothing is attempted until networkOnline().
    void networkOffline();
    /// The app came to the foreground (true) or went to the background (false). Background
    /// with pending edits syncs right away (Android may suspend the app); foreground syncs when
    /// the last sync is older than the active sync gap.
    void setAppActive(bool active);

Q_SIGNALS:
    /// Anything in status() changed except `progress` alone (state, errors, nextSync, stall, …).
    void statusChanged();
    /// status().progress changed (once per path while syncing: throttle in the UI).
    void progressChanged();
    /// A run completed (also with per-file errors).
    void synced(const brulion::sync::SyncReport &report);
    /// A run starts (also a run the scheduler joins is not reported twice).
    void runStarted(brulion::sync::SyncTrigger trigger);
    /// Every finished run — also failed, refused and cancelled ones (for the history). `stalled`:
    /// the watchdog stopped it (SyncRunRecord: outcome failed, code "stalled"); the status then
    /// says lastErrorCode "stalled" as well.
    void runFinished(const brulion::sync::SyncReport &report, bool stalled);

private:
    void trigger(SyncTrigger why);
    void onRunFinished(const SyncReport &report);
    void pollStall();
    void scheduleStallPoll();
    void schedulePeriodic(qint64 delay, bool retry = false);
    void cancelTimer(int &handle);
    void updateStatus(const std::function<void(SyncStatus &)> &change);
    SyncState idleState() const;
    void notifyWaiters(const std::optional<SyncReport> &result);

    SyncRunner *m_runner;
    std::unique_ptr<SchedulerTimers> m_timers;
    std::function<bool()> m_isOnline;
    SyncStatus m_status;
    qint64 m_interval = 5 * 60'000;
    qint64 m_debounce = 20'000;
    qint64 m_retryBase = 30'000;
    qint64 m_activeGap = 60'000;
    qint64 m_stallThreshold = 20'000;
    qint64 m_stallLimit = 180'000;
    qint64 m_stallPoll = 2'000;
    int m_stallTimer = 0;
    bool m_retryPlanned = false;                         ///< the periodic timer is a retry after a failure
    bool m_firstRun = true;                              ///< the next periodic run is the first after start()
    bool m_watchdogCancelled = false;                    ///< the watchdog cancelled the running run
    SyncTrigger m_afterRunTrigger = SyncTrigger::Manual; ///< kind of the run m_triggerAfterRun starts
    qint64 m_runStartedMs = 0;
    qint64 m_lastHeartbeatMs = 0; ///< run time of the last "still running" line
    qint64 m_stalledSinceMs = 0;  ///< idle time when the stall was detected
    bool m_started = false;
    bool m_running = false;
    bool m_cancelling = false;     ///< stop() cancelled the running run
    bool m_rerun = false;          ///< a local change arrived during a run
    bool m_triggerAfterRun = false; ///< restarted while the cancelled run was still finishing
    bool m_confirmDeletions = false; ///< the next run confirms held-back deletions
    bool m_networkConnected = false;
    bool m_reportedOnline = true; ///< last networkOnline() / networkOffline() (isOnline() fallback)
    int m_periodicTimer = 0;
    int m_debounceTimer = 0;
    int m_failures = 0;
    qint64 m_lastAttempt = 0;
    QList<RunCallback> m_waiters;     ///< callers waiting for the running sync
    QList<RunCallback> m_nextWaiters; ///< callers waiting for the sync after it
};

} // namespace brulion::sync
