// DesktopNotifier — reminders on the Linux desktop through the freedesktop notification service
// (org.freedesktop.Notifications, https://specifications.freedesktop.org/notification-spec/).
//
// The message itself is built by the pure function buildDesktopNotification() (tested without a
// bus); the bus is behind the small NotificationBus interface (DBusNotificationBus is the real
// one, tests use a fake):
//
//   * summary = the reminder's text ("⏰ " in front for alarms), body = page title and due info
//     (escaped when the server interprets markup — note text never becomes markup);
//   * actions (when the server supports them): "default" (a click: open the note), "snooze"
//     ("Snooze 10 min"), "ok";
//   * hints: urgency normal / critical (alarms), alarms also "sound-name" alarm-clock-elapsed and
//     "resident" (they stay until acted upon, expire timeout 0); "desktop-entry" so the server
//     shows Brulion's name and icon.
//
// An alarm rings: it is sent again (same id, sound again) every ringInterval until the user acts
// on it, closes it, or its ringMinutes are over. Without a notification service (no session
// bus, no daemon, or a failed Notify call) the notification goes to the tray instead
// (trayMessageRequested(), shown by the shell's DesktopTray).
#pragma once

#include "notifier.h"

#include <QDateTime>
#include <QHash>
#include <QPointer>
#include <QStringList>
#include <QTimer>
#include <QVariantMap>

#include <functional>

namespace brulion::notify {

/// One org.freedesktop.Notifications.Notify call.
struct DesktopNotification {
    QString appName;
    uint replacesId = 0;
    QString appIcon;
    QString summary;
    QString body;
    QStringList actions; ///< key, label, key, label, …
    QVariantMap hints;   ///< urgency is a byte (uchar), as the specification requires
    int expireTimeout = -1;
};

/// Desktop entry / icon name of the app (packaging/io.github.januszbit.brulion.desktop).
QString desktopAppId();

/// The Notify call for `notification`. `capabilities` are the server's (GetCapabilities):
/// "actions" adds the actions, "body-markup" escapes the body. `snoozeMinutes` names the snooze
/// action ("Snooze 10 min"). Labels are translated (context "brulion::notify::DesktopNotifier").
DesktopNotification buildDesktopNotification(const ScheduledNotification &notification, const QStringList &capabilities,
                                             int snoozeMinutes, uint replacesId = 0);

/// The notification service as DesktopNotifier sees it.
class NotificationBus : public QObject {
    Q_OBJECT
public:
    using QObject::QObject;
    ~NotificationBus() override;

    /// The service can be reached (as far as is known: the answer may arrive later).
    virtual bool isAvailable() const = 0;
    /// The server's capabilities ("actions", "body-markup", "sound", "persistence", …).
    virtual QStringList capabilities() const = 0;
    /// Send `notification`; `done` gets the server's id, 0 when the call failed. Asynchronous.
    virtual void notify(const DesktopNotification &notification, std::function<void(uint id)> done) = 0;
    /// Close a shown notification.
    virtual void close(uint id) = 0;

Q_SIGNALS:
    /// The user invoked an action ("default" for a click on the notification).
    void actionInvoked(uint id, const QString &actionKey);
    /// 1 expired, 2 dismissed by the user, 3 closed by CloseNotification, 4 other.
    void notificationClosed(uint id, uint reason);
    /// Wayland/X11 activation token of the action about to be invoked (focus stealing
    /// prevention: the window raised for it must present the token).
    void activationTokenReceived(uint id, const QString &token);
};

class DesktopNotifier : public Notifier {
    Q_OBJECT

public:
    static constexpr int DefaultRingIntervalMs = 30 * 1000;

    /// `bus` (owned; may be nullptr: tray only).
    explicit DesktopNotifier(NotificationBus *bus, QObject *parent = nullptr);
    ~DesktopNotifier() override;

    QString backend() const override;
    bool canShow() const override;

    // NotificationSink: timed in-app by the ReminderScheduler.
    bool firesWhenClosed() const override { return false; }
    void showNow(const ScheduledNotification &notification) override;

    /// A tray icon exists for the fallback (the shell's DesktopTray).
    void setTrayAvailable(bool available);
    /// How often a ringing alarm is sent again (default 30 s; tests use short ones).
    void setRingInterval(int msec);
    /// Clock for the ring time (default: current local time).
    void setClock(std::function<QDateTime()> clock);

    /// Notifications shown on the bus and not yet closed (tests, diagnostics).
    int shownCount() const { return int(m_shown.size()); }
    /// Alarms ringing now.
    int ringingCount() const;

Q_SIGNALS:
    /// No notification service: show this in the tray.
    void trayMessageRequested(const QString &title, const QString &body);

private:
    struct Shown {
        ScheduledNotification notification;
        QDateTime ringUntil; ///< invalid: not ringing
    };

    void onActionInvoked(uint id, const QString &action);
    void onClosed(uint id, uint reason);
    void ring();
    void updateRingTimer();
    QDateTime now() const;
    void fallback(const ScheduledNotification &notification);

    QPointer<NotificationBus> m_bus;
    bool m_trayAvailable = false;
    QHash<uint, Shown> m_shown;
    QTimer m_ringTimer;
    std::function<QDateTime()> m_clock;
    QByteArray m_activationToken;
};

} // namespace brulion::notify
