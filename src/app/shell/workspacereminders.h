// WorkspaceReminders — the reminders of the open vault (QML: `Workspace.reminders`): owns the
// core's ReminderScheduler over the Workspace's VaultIndex and LocalStore and feeds the
// platform's notifier (a reminders::NotificationSink from src/app/notify, set by the shell's
// integration; tests use a fake). Missed reminders (due while the app/device was off) are
// exposed for the banner, snoozing writes the new time into the note through the Vault, "Done"
// (a notification's or the banner's) marks the reminder's task done there.
//
//   lifecycle   Workspace: attach(vault, index) on open, start() once the index holds every
//               note (a half-built index must not swallow reminders), detach() on close.
//               cancelAll() empties the platform's armed set (closing a vault on purpose —
//               quitting keeps Android alarms armed so they ring while Brulion is closed).
//   settings    enabled (off: stopped and the armed set cancelled), alarmRingMinutes,
//               snoozeMinutes, notifyMissed (off: the missed list stays empty), language
//   QML         missed / missedCount / dismissMissed(id) / clearMissed() / openMissed(id),
//               upcoming (next 20) / next, snooze(id, minutes), complete(id), backend, firesWhenClosed,
//               canShow, signal fired(notification) for in-app toasts
//
// Notification maps (missed, upcoming, next, fired): { id, title, body, at ("YYYY-MM-DD HH:mm"),
// when (human: "dziś 09:00", "wt 13 paź 18:00"), alarm, path, line, task (the reminder is on an
// open task: "Done" can complete it) }.
#pragma once

#include "reminders/notifications.h"

#include <QDate>
#include <QDateTime>
#include <QList>
#include <QObject>
#include <QPointer>
#include <QVariantList>
#include <QVariantMap>

#include <functional>
#include <memory>
#include <optional>
#include <utility>

namespace brulion {
class ReminderScheduler;
class Vault;
class VaultIndex;
} // namespace brulion

namespace brulion::shell {

class WorkspaceReminders : public QObject {
    Q_OBJECT

    /// Reminders are on (settings).
    Q_PROPERTY(bool enabled READ enabled NOTIFY settingsChanged FINAL)
    /// The scheduler runs (a vault is open, its index is complete, reminders are on).
    Q_PROPERTY(bool running READ isRunning NOTIFY runningChanged FINAL)
    /// Reminders that were due while Brulion was not running (oldest first), see the file comment.
    Q_PROPERTY(QVariantList missed READ missed NOTIFY missedChanged FINAL)
    Q_PROPERTY(int missedCount READ missedCount NOTIFY missedChanged FINAL)
    /// The next reminders handed to the platform (at most 20, soonest first).
    Q_PROPERTY(QVariantList upcoming READ upcoming NOTIFY upcomingChanged FINAL)
    /// The next reminder ({} when none).
    Q_PROPERTY(QVariantMap next READ next NOTIFY upcomingChanged FINAL)
    /// "dbus", "toast", "tray", "android", "browser", "none" (from the notifier), "" without one.
    Q_PROPERTY(QString backend READ backend NOTIFY sinkChanged FINAL)
    /// The platform rings reminders while Brulion is closed (Android).
    Q_PROPERTY(bool firesWhenClosed READ firesWhenClosed NOTIFY sinkChanged FINAL)
    /// Snooze length of notifications (minutes).
    Q_PROPERTY(int snoozeMinutes READ snoozeMinutes NOTIFY settingsChanged FINAL)

public:
    using Clock = std::function<QDateTime()>;
    static constexpr int MaxUpcoming = 20;

    explicit WorkspaceReminders(Clock clock, QObject *parent = nullptr);
    ~WorkspaceReminders() override;

    // ---- wiring (Workspace, the shell's integration, tests) ----------------------------------
    /// Where notifications go. `sink` must stay valid while `lifetime` exists (a QObject: the
    /// notifier itself); nullptr = nowhere. Restarts a running scheduler.
    void setSink(NotificationSink *sink, QObject *lifetime);
    /// Same, for a sink that outlives this object (tests).
    void setSink(NotificationSink *sink);
    NotificationSink *sink() const;
    /// The notifier's backend name and whether it can show notifications now (for the UI).
    void setBackendInfo(std::function<QString()> backend, std::function<bool()> canShow);
    void attach(Vault *vault, VaultIndex *index);
    void detach();
    /// Start the scheduler (the index is complete). No-op when disabled or not attached.
    void start();
    bool isRunning() const;
    /// Cancel everything the platform has armed (sink->schedule({})).
    void cancelAll();
    /// The scheduler (nullptr when not running); tests.
    ReminderScheduler *scheduler() const { return m_scheduler.get(); }

    // ---- settings -----------------------------------------------------------------------------
    bool enabled() const { return m_enabled; }
    void setEnabled(bool on);
    void setAlarmRingMinutes(int minutes);
    int snoozeMinutes() const { return m_snoozeMinutes; }
    void setSnoozeMinutes(int minutes);
    void setNotifyMissed(bool on);
    /// "pl" | "en": language of notification bodies and of `when`.
    void setLanguage(const QString &language);
    /// In-app timer settings for tests (heartbeat, debounce in ms; 0 = scheduler defaults).
    void setTestIntervals(int heartbeatMs, int debounceMs);

    // ---- QML ----------------------------------------------------------------------------------
    QVariantList missed() const;
    int missedCount() const;
    QVariantList upcoming() const;
    QVariantMap next() const;
    QString backend() const;
    bool firesWhenClosed() const;
    /// The notifier can show notifications right now.
    Q_INVOKABLE bool canShow() const;

    Q_INVOKABLE void dismissMissed(const QString &id);
    Q_INVOKABLE void clearMissed();
    /// Open the note of a missed reminder (navigationRequested) and dismiss it.
    Q_INVOKABLE void openMissed(const QString &id);
    /// Move the reminder occurrence `id` (upcoming or missed) `minutes` later: the note gets the
    /// new time (reminders::snoozeReminder; a recurring one a one-off child bullet). False when
    /// the reminder is not found or the note cannot be written.
    Q_INVOKABLE bool snooze(const QString &id, int minutes = 0);
    /// Same for a notification the platform handed back (path/line from when it was scheduled;
    /// the line is looked up again by id when the note changed meanwhile).
    bool snoozeNotification(const ScheduledNotification &notification, int minutes = 0);
    /// "Done" on the reminder occurrence `id` (upcoming or missed): its task is marked done in
    /// the note (markdown::setTaskStatus, ✅ today — as the editor's status change) and the
    /// missed entry dismissed. False when it is not found, not on a task or the note cannot be
    /// written.
    Q_INVOKABLE bool complete(const QString &id);
    /// Same for a notification the platform handed back: the occurrence is looked up again by id
    /// (its task's line may have moved; not found — edited, moved, completed elsewhere or deleted
    /// meanwhile — errorOccurred() and false). ✅ `day` (invalid: today; Android: the day of the
    /// tap). Before start() (the index is not complete yet) the request is kept and applied by
    /// start(), then it returns true; detach() forgets such requests. A notification of another
    /// notebook (`vault` set and not this one's id) is refused. Every failure is reported by
    /// errorOccurred(): the platform has closed the notification already.
    bool completeNotification(const ScheduledNotification &notification, QDate day = {});
    /// The id (Vault::id()) of the notebook whose "Done" requests are applied right away: set
    /// from start() to detach(), empty otherwise (no notebook, still indexing, reminders paused).
    /// The shell hands it to the notifier (Notifier::setDoneTarget(): Android keeps the taps
    /// queued while Brulion was closed until their notebook is ready). doneTargetChanged().
    QString doneTarget() const;
    /// Show a notification now through the sink (the settings page's test notification).
    bool sendTestNotification(const QString &title, const QString &body);

    /// Map of a notification for QML (see the file comment).
    QVariantMap toMap(const ScheduledNotification &notification) const;

Q_SIGNALS:
    void settingsChanged();
    void runningChanged();
    void missedChanged();
    void upcomingChanged();
    void sinkChanged();
    /// A reminder was shown in-app (desktop, browser) — e.g. for a toast in the window.
    void fired(const QVariantMap &notification);
    /// Open a route (the Workspace forwards it to the shell).
    void navigationRequested(const QVariantMap &route);
    /// Something failed that the user should know about (translated).
    void errorOccurred(const QString &message);
    /// A task was marked done from a notification or the missed banner (its reminder's title).
    void taskCompleted(const QString &title);
    void doneTargetChanged();

private:
    class Proxy;
    void restart();
    void startScheduler();
    void stopScheduler();
    std::optional<ScheduledNotification> find(const QString &id) const;
    /// The occurrence `notification` names, as the index has it now (nullopt: gone or changed).
    std::optional<ScheduledNotification> current(const ScheduledNotification &notification) const;
    enum class EditResult {
        Written,   ///< the note holds the edit
        Gone,      ///< the occurrence is not in the note any more (edited, moved, deleted)
        Unchanged, ///< the edit changes nothing at the occurrence
        Failed,    ///< the note cannot be read or written (Vault::errorString())
    };
    /// Apply `edit` (content and the occurrence as the content has it → new content) to the
    /// note of `notification`'s occurrence, looked up again by id. The edit works on the content
    /// the index has and is written only when the file still holds it (compare-and-swap): another
    /// program's change the vault has not taken in yet (an in-place save, a sync while Brulion
    /// was in the background) is taken in first and the occurrence looked up again in it — a
    /// line number from a stale index never lands on another line. `target` receives the
    /// occurrence edited.
    EditResult editOccurrence(const ScheduledNotification &notification,
                              const std::function<QString(const QString &, const ScheduledNotification &)> &edit,
                              ScheduledNotification *target);
    /// The notification belongs to another notebook than the open one.
    bool foreign(const ScheduledNotification &notification) const;
    bool applyCompletion(const ScheduledNotification &notification, QDate day);
    QDateTime now() const;

    Clock m_clock;
    std::unique_ptr<Proxy> m_proxy;
    std::unique_ptr<ReminderScheduler> m_scheduler;
    QPointer<Vault> m_vault;
    QPointer<VaultIndex> m_index;
    std::function<QString()> m_backend;
    std::function<bool()> m_canShow;
    bool m_enabled = true;
    bool m_notifyMissed = true;
    bool m_wantRunning = false;
    int m_ringMinutes = ScheduledNotification::DefaultRingMinutes;
    int m_snoozeMinutes = 10;
    QString m_language = QStringLiteral("pl");
    int m_testHeartbeatMs = 0;
    int m_testDebounceMs = 0;
    /// "Done" requests that came after attach(), before start() (applied by it, dropped by detach()).
    QList<std::pair<ScheduledNotification, QDate>> m_pendingDone;
};

} // namespace brulion::shell
