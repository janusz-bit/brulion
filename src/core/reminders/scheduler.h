// ReminderScheduler — keeps a NotificationSink supplied with the upcoming reminders and, on
// platforms without background alarms (desktop tray app, browser), fires them in-app.
// Part of the reminders module — see reminders/reminders.h.
#pragma once

#include "dates/names.h"
#include "reminders/notifications.h"

#include <QDateTime>
#include <QElapsedTimer>
#include <QHash>
#include <QList>
#include <QMetaObject>
#include <QObject>
#include <QPointer>
#include <QString>
#include <QTimer>

#include <functional>

namespace brulion {

class LocalStore;
class VaultIndex;

/// Schedules the reminders of a VaultIndex on a NotificationSink.
///
/// **Scheduling** (all sinks): refresh() hands upcomingNotifications(index, now, horizonDays,
/// maxScheduled) to NotificationSink::schedule() (whole set; skipped when unchanged, always
/// sent by start()). Refreshes happen on start(), debounced after index changes
/// (DefaultDebounceMs), every hour (the horizon slides) and after clock jumps.
///
/// **In-app firing** (sinks whose firesWhenClosed() is false — queried by start()):
/// * one precise QTimer is armed for the next reminder within the next 24 h (re-planned on every
///   refresh, i.e. at least hourly); when it fires, every due reminder is shown with
///   NotificationSink::showNow() and announced with fired();
/// * a 30 s heartbeat also fires due reminders and compares wall-clock and monotonic time:
///   when they drift apart by more than a minute (suspend/resume, clock or time-zone change) it
///   refreshes, so reminders due during sleep are caught at once;
/// * every occurrence fires once: handled ids are persisted with a timestamp in the LocalStore
///   ("reminders/handled", pruned after 7 days), so restarts and re-plans never repeat one;
/// * due reminders are looked for since the last check ("reminders/lastSeen", persisted), so
///   reminders whose time passed while the app was not running (or the computer slept) are
///   caught up on start(): shown late when at most 6 h late (alarms: at most alarmRingMinutes),
///   otherwise recorded as missed() instead. The look-back is limited to 7 days; the very first
///   start looks back one minute only.
/// * missed() is persisted ("reminders/missed") until dismissed; an entry also disappears when
///   the reminder no longer exists in the notes (deleted, edited, task completed) or after 7
///   days.
///
/// With a sink that fires when closed (Android), the platform shows reminders itself and
/// nothing is fired in-app; missed() stays empty.
///
/// Start the scheduler once the index holds the vault (after the initial scan): while the index
/// has no notes nothing is fired or marked missed, so a vault that is still loading cannot
/// swallow reminders. The index, sink and store must outlive the scheduler (the index and the
/// store are watched with QPointer; a missing store keeps the state in memory only). The clock
/// gives the local wall-clock time; it is injectable for tests (default:
/// QDateTime::currentDateTime). Single-threaded, like VaultIndex.
class ReminderScheduler : public QObject {
    Q_OBJECT

public:
    using Clock = std::function<QDateTime()>;

    static constexpr int DefaultHorizonDays = 14;
    static constexpr int DefaultMaxScheduled = 64;
    static constexpr int DefaultDebounceMs = 500;
    static constexpr int DefaultHeartbeatMs = 30 * 1000;
    static constexpr int ReplanIntervalMs = 60 * 60 * 1000;
    /// In-app timers are armed only for reminders within this window.
    static constexpr int TimerWindowHours = 24;
    /// Reminders shown later than this are recorded as missed instead.
    static constexpr int LateLimitHours = 6;
    /// Handled ids, missed entries and the catch-up look-back are kept this long.
    static constexpr int HistoryDays = 7;
    /// Wall-clock vs monotonic drift that counts as a clock jump.
    static constexpr int ClockJumpToleranceMs = 60 * 1000;
    /// Most entries kept in missed() (the oldest are dropped).
    static constexpr int MaxMissed = 100;
    /// Most handled ids kept (the oldest are dropped first).
    static constexpr int MaxHandled = 5000;

    ReminderScheduler(VaultIndex *index, NotificationSink *sink, LocalStore *store, Clock clock = {},
                      QObject *parent = nullptr);
    /// Stops the scheduler (persisting its state).
    ~ReminderScheduler() override;

    ReminderScheduler(const ReminderScheduler &) = delete;
    ReminderScheduler &operator=(const ReminderScheduler &) = delete;

    // ---- settings (each refreshes a running scheduler) -------------------------------------

    /// Days ahead handed to the sink (0–3660, default 14).
    void setHorizonDays(int days);
    int horizonDays() const;
    /// Most notifications handed to the sink (1–1000, default 64): the soonest win.
    void setMaxScheduled(int max);
    int maxScheduled() const;
    /// Ring time of alarms (1–60 minutes, default 10): written to ScheduledNotification::
    /// ringMinutes and the lateness after which an alarm is missed instead of shown.
    void setAlarmRingMinutes(int minutes);
    int alarmRingMinutes() const;
    /// Language of notification bodies (default Polish).
    void setLocale(dates::UiLocale locale);
    dates::UiLocale locale() const;
    /// Debounce of refreshes after index changes, in ms (default DefaultDebounceMs).
    void setDebounceInterval(int msec);
    /// Heartbeat period in ms (default DefaultHeartbeatMs); tests use short ones.
    void setHeartbeatInterval(int msec);

    // ---- state ------------------------------------------------------------------------------

    bool isRunning() const;
    /// The set last handed to NotificationSink::schedule().
    QList<ScheduledNotification> upcoming() const;
    /// Notifications left out of upcoming() by maxScheduled at the last refresh.
    int droppedCount() const;
    /// When the in-app timer fires next (invalid when not armed).
    QDateTime nextFireTime() const;
    /// Reminders that were due while the app was not running and too late to show, oldest
    /// first (see the class comment).
    QList<ScheduledNotification> missed() const;
    /// Forget one missed reminder (by id) / all of them.
    void dismissMissed(const QString &id);
    void clearMissed();

public Q_SLOTS:
    /// Start watching the index: hands the upcoming set to the sink and, for in-app sinks,
    /// catches up with reminders due since the app last ran (fired or missed). No-op when
    /// already running.
    void start();
    /// Stop timers and watching; persists the time of the last check. The sink keeps what it
    /// was given (Android alarms survive the app) — call sink->schedule({}) to cancel them.
    void stop();
    /// Recompute now (otherwise debounced after index changes): reschedules the sink, fires
    /// due reminders and re-arms the timer. No-op while stopped.
    void refresh();
    /// Fire reminders that are due now (normally driven by the timers; call it e.g. when the
    /// system resumes from sleep). No-op while stopped or with a sink that fires by itself.
    void checkNow();

Q_SIGNALS:
    /// A reminder was shown in-app with NotificationSink::showNow() (e.g. for a toast).
    void fired(const brulion::ScheduledNotification &notification);
    /// missed() changed.
    void missedChanged();
    /// upcoming() changed (a new set was handed to the sink).
    void upcomingChanged();

private:
    void scheduleRefresh();
    void refreshAt(const QDateTime &now);
    void processDue(const QDateTime &now);
    void armTimer(const QDateTime &now);
    void onFireTimer();
    void onHeartbeat();
    void resetHeartbeat(const QDateTime &now);
    bool hasNotes() const;
    bool isTooLate(const ScheduledNotification &n, const QDateTime &now) const;
    void applyRingMinutes(QList<ScheduledNotification> &list) const;

    void loadState(const QDateTime &now);
    void pruneHandled(const QDateTime &now);
    void pruneMissed(const QDateTime &now);
    void setLastSeen(const QDateTime &now, bool force);
    void saveHandled();
    void saveMissed();

    QPointer<VaultIndex> m_index;
    NotificationSink *m_sink = nullptr;
    QPointer<LocalStore> m_store;
    Clock m_clock;

    int m_horizonDays = DefaultHorizonDays;
    int m_maxScheduled = DefaultMaxScheduled;
    int m_ringMinutes = ScheduledNotification::DefaultRingMinutes;
    dates::UiLocale m_locale = dates::UiLocale::Polish;

    bool m_running = false;
    bool m_inApp = false;
    bool m_forceSchedule = false;
    QMetaObject::Connection m_indexConnection;

    QTimer m_debounce;
    QTimer m_fireTimer;
    QTimer m_replanTimer;
    QTimer m_heartbeat;
    QElapsedTimer m_beatClock;
    QDateTime m_beatWall;

    QList<ScheduledNotification> m_upcoming;
    int m_dropped = 0;
    QDateTime m_nextFire;
    QDateTime m_lastSeen;
    QDateTime m_lastSeenSaved;
    QHash<QString, QDateTime> m_handled; ///< id -> when it was fired or marked missed
    QList<ScheduledNotification> m_missed;
};

} // namespace brulion
