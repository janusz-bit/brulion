// WorkspaceReminders — the reminders of the open vault (QML: `Workspace.reminders`): owns the
// core's ReminderScheduler over the Workspace's VaultIndex and LocalStore and feeds the
// platform's notifier (a reminders::NotificationSink from src/app/notify, set by the shell's
// integration; tests use a fake). Missed reminders (due while the app/device was off) are
// exposed for the banner, snoozing writes the new time into the note through the Vault.
//
//   lifecycle   Workspace: attach(vault, index) on open, start() once the index holds every
//               note (a half-built index must not swallow reminders), detach() on close.
//               cancelAll() empties the platform's armed set (closing a vault on purpose —
//               quitting keeps Android alarms armed so they ring while Brulion is closed).
//   settings    enabled (off: stopped and the armed set cancelled), alarmRingMinutes,
//               snoozeMinutes, notifyMissed (off: the missed list stays empty), language
//   QML         missed / missedCount / dismissMissed(id) / clearMissed() / openMissed(id),
//               upcoming (next 20) / next, snooze(id, minutes), backend, firesWhenClosed,
//               canShow, signal fired(notification) for in-app toasts
//
// Notification maps (missed, upcoming, next, fired): { id, title, body, at ("YYYY-MM-DD HH:mm"),
// when (human: "dziś 09:00", "wt 13 paź 18:00"), alarm, path, line }.
#pragma once

#include "reminders/notifications.h"

#include <QDateTime>
#include <QObject>
#include <QPointer>
#include <QVariantList>
#include <QVariantMap>

#include <functional>
#include <memory>
#include <optional>

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

private:
    class Proxy;
    void restart();
    void stopScheduler();
    std::optional<ScheduledNotification> find(const QString &id) const;
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
};

} // namespace brulion::shell
