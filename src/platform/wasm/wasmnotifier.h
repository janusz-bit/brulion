#pragma once

// WebAssembly only (compiled when EMSCRIPTEN is set, see src/platform/CMakeLists.txt).

#include <QObject>
#include <QString>

namespace brulion::platform {

struct WasmNotifierJs;

/// Reminders in the browser build: the web Notification API.
///
/// Browsers can only notify while the page (or the installed PWA) is open — there is no
/// background alarm, so `firesWhenClosed()` is false and the caller times reminders itself
/// (core `ReminderScheduler`) and calls `showNow()` when one is due.
///
/// Notifications are shown with `new Notification()`; where the page may not construct them
/// (Chrome on Android) they go through the service worker registration (wasm/sw.js), whose
/// `notificationclick` handler focuses the page and forwards the click here as `activated()`.
class WasmNotifier : public QObject
{
    Q_OBJECT

public:
    enum class Permission {
        Unsupported, ///< no Notification API (old browser, insecure http:// origin)
        Default,     ///< not asked yet (or the prompt was dismissed)
        Granted,
        Denied,
    };
    Q_ENUM(Permission)

    explicit WasmNotifier(QObject *parent = nullptr);
    ~WasmNotifier() override;

    static bool isSupported();
    /// The current browser permission (queried live).
    Permission permission() const;
    /// Always false: browsers deliver nothing once the page is closed.
    bool firesWhenClosed() const { return false; }

    struct Activation {
        QString id;
        QString path;
        int line = -1;
    };
    /// A click on a notification while no Brulion page was open makes the service worker open
    /// the app with the reminder in the URL (`?brulion-id=…&brulion-note=…&brulion-line=…`).
    /// Returns it (once) and removes the parameters from the address bar.
    static bool takeLaunchActivation(Activation *activation);

public Q_SLOTS:
    /// Shows the browser's permission prompt; the answer arrives as `permissionChanged()`.
    /// Browsers only show it in response to a user action (call it from a click handler).
    void requestPermission();

    /// Shows a notification now. `id` identifies it (a newer one with the same id replaces it);
    /// `path`/`line` locate the reminder and come back with `activated()`. Alarms stay on
    /// screen until dismissed and vibrate where supported. Returns false when not permitted.
    bool showNow(const QString &id, const QString &title, const QString &body,
                 const QString &path = QString(), int line = -1, bool alarm = false);

    /// Closes the notification with this id if it is still shown.
    void close(const QString &id);

Q_SIGNALS:
    void permissionChanged(brulion::platform::WasmNotifier::Permission permission);
    /// The user clicked a notification (the page has been focused).
    void activated(const QString &id, const QString &path, int line);

private:
    friend struct WasmNotifierJs;
};

} // namespace brulion::platform
