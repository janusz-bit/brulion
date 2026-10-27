// WindowsToastBus — NotificationBus over Windows toast notifications (WinRT
// Windows.UI.Notifications through C++/WinRT, Windows 10 1809 or newer; docs/WINDOWS.md
// "Powiadomienia"). Windows builds only (BRULION_NOTIFY_HAVE_WINTOAST); DesktopNotifier does the
// rest as on Linux: the message, replacing a reminder's notification, actions → snooze / open,
// ringing alarms, the sound samples, the tray when toasts are turned off.
//
//   * the toast XML: buildToastXml() (toastxml.h, unit-tested on every platform); tag = the id,
//     group "brulion", so the same id replaces its toast and close() removes it;
//   * the app's identity: the AppUserModelID platform::applyAppIdentity() registered at start
//     (src/platform/appidentity.h) — the toasts show Brulion's name and icon, also for the
//     portable ZIP without a Start menu shortcut;
//   * available while the AppUserModelID is registered and Windows allows Brulion's notifications
//     (toastsAllowed(): ToastNotifier::Setting(), asked every time — the user may switch them off
//     in Settings → System → Notifications; before the first toast it throws ERROR_NOT_FOUND,
//     which counts as allowed) — otherwise the tray icon shows the reminder;
//   * alarms with a Looping.* sound loop it on screen (ringsItself()); others are sent again;
//   * WinRT raises the toast's events on a thread pool thread: they are queued to the GUI thread
//     (Activated → actionInvoked, Dismissed → notificationClosed; a toast timed out into the
//     Action Center stays clickable there); no WinRT exception leaves this class;
//   * no COM activator: a click works only while Brulion runs, so its toasts are removed from
//     the Action Center at quit (clearAll(); missed reminders are announced again at the next
//     start).
#pragma once

#include "desktopnotifier.h"

#include <memory>

namespace brulion::notify {

class WindowsToastBus : public NotificationBus {
    Q_OBJECT
public:
    /// `appUserModelId`: the registered AppUserModelID (platform::appUserModelId()).
    explicit WindowsToastBus(const QString &appUserModelId, QObject *parent = nullptr);
    ~WindowsToastBus() override;

    bool isAvailable() const override;
    QStringList capabilities() const override;
    void notify(const DesktopNotification &notification, std::function<void(uint id)> done) override;
    void close(uint id) override;
    QString name() const override;
    QStringList themeSounds() const override;
    bool supportsSoundFiles() const override { return false; }
    bool ringsItself(const DesktopNotification &notification) const override;

    /// Removes every toast of this app from the screen and the Action Center.
    void clearAll();
    /// Tags of this app's toasts in the Action Center (diagnostics, tests).
    QStringList historyTags() const;
    QString appUserModelId() const { return m_appUserModelId; }
    /// Why the last call failed ("" after a successful one).
    QString lastError() const { return m_lastError; }

private:
    struct Private;

    void onActivated(const QString &arguments);
    void onDismissed(uint id, quint64 generation, uint reason);
    void onFailed(uint id, quint64 generation, const QString &error);
    void forget(uint id);

    std::unique_ptr<Private> d;
    QString m_appUserModelId;
    QString m_lastError;
    uint m_lastId = 0;
};

} // namespace brulion::notify
