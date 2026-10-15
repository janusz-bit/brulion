// DBusNotificationBus — NotificationBus over the session bus: org.freedesktop.Notifications
// (Notify, CloseNotification, GetCapabilities; signals ActionInvoked, NotificationClosed,
// ActivationToken). Linux desktop builds with QtDBus only (BRULION_NOTIFY_HAVE_DBUS).
//
// Nothing blocks the GUI thread: the capabilities are asked for asynchronously at construction
// (until the answer arrives the service counts as available when a session bus exists), and
// every Notify is an asynchronous call — a missing service (no daemon at all) fails it, and
// DesktopNotifier then shows the reminder in the tray.
#pragma once

#include "desktopnotifier.h"

namespace brulion::notify {

class DBusNotificationBus : public NotificationBus {
    Q_OBJECT
public:
    explicit DBusNotificationBus(QObject *parent = nullptr);
    ~DBusNotificationBus() override;

    bool isAvailable() const override;
    QStringList capabilities() const override;
    void notify(const DesktopNotification &notification, std::function<void(uint id)> done) override;
    void close(uint id) override;

private Q_SLOTS:
    void onActionInvoked(uint id, const QString &actionKey);
    void onNotificationClosed(uint id, uint reason);
    void onActivationToken(uint id, const QString &token);

private:
    void queryCapabilities();

    bool m_connected = false;
    bool m_answered = false;
    bool m_available = false;
    QStringList m_capabilities;
};

} // namespace brulion::notify
