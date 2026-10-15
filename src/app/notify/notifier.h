// Notifier — where reminders and alarms reach the user on this platform (docs/ARCHITECTURE.md
// "Platform layer"): a QObject implementing the core's reminders::NotificationSink, plus the
// signals the reminders integration (shell/workspacereminders.h) listens to.
//
//   DesktopNotifier    Linux: org.freedesktop.Notifications over QtDBus (actions "Snooze" /
//                      "OK" / click = open, alarms critical + sound + resident and ringing again
//                      for their ring time); without a notification service the tray shows them
//                      (trayMessageRequested). In-app timed: firesWhenClosed() = false.
//   AndroidNotifier    AndroidBridge::scheduleReminders() (AlarmManager: fires while the app is
//                      closed and after a reboot; firesWhenClosed() = true). Snoozing happens
//                      in the Android notification itself.
//   WasmNotifierSink   the browser's Notification API through platform::WasmNotifier (only
//                      while the page is open; firesWhenClosed() = false).
//
// createPlatformNotifier() picks the one of this build. Everything runs on the GUI thread.
#pragma once

#include "reminders/notifications.h"

#include <QList>
#include <QObject>
#include <QString>

namespace brulion::notify {

class Notifier : public QObject, public brulion::NotificationSink {
    Q_OBJECT

public:
    static constexpr int DefaultSnoozeMinutes = 10;

    explicit Notifier(QObject *parent = nullptr);
    ~Notifier() override;

    /// "dbus", "tray", "android", "browser" or "none".
    virtual QString backend() const = 0;
    /// Notifications can be shown right now (a notification service or tray exists, the
    /// permission is granted).
    virtual bool canShow() const = 0;

    /// Snooze length the notification's "Snooze" action offers (1 … 120 minutes, default 10).
    void setSnoozeMinutes(int minutes);
    int snoozeMinutes() const { return m_snoozeMinutes; }

    /// The set last handed to schedule().
    QList<ScheduledNotification> scheduled() const { return m_scheduled; }

    // NotificationSink: schedule() remembers the set (subclasses arm it); showNow() is theirs.
    void schedule(const QList<ScheduledNotification> &notifications) override;

Q_SIGNALS:
    /// The user opened the note from a notification (click, "Open" on the alarm screen).
    void activated(const QString &path, int line);
    /// The user chose "Snooze" on a notification: move the reminder `minutes` later.
    void snoozeRequested(const brulion::ScheduledNotification &notification, int minutes);
    /// Arming or showing notifications failed (translated message).
    void errorOccurred(const QString &message);

protected:
    QList<ScheduledNotification> m_scheduled;
    int m_snoozeMinutes = DefaultSnoozeMinutes;
};

/// The notifier of this build (Android, browser, or the Linux desktop's D-Bus notifier with the
/// tray as fallback), owned by `parent`.
Notifier *createPlatformNotifier(QObject *parent = nullptr);

} // namespace brulion::notify
