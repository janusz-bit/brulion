// AppLockController — the app lock (QML singleton AppLock; docs/UI.md "App lock", docs/ANDROID.md
// "App lock"), over the core module applock: a PIN (a salted PBKDF2-SHA512 hash, this device's
// settings, never synced) or, on Android, the phone's biometrics or screen lock; locked on start,
// after a while in the background, after a while without input (the desktop), on "Lock now". While
// locked the shell hides everything behind the lock screen (Main.qml), the encrypted notes lock
// (Workspace.encryption.lockAll()), the Android window keeps its content out of screenshots and the
// recent apps (platform::setSecureWindow), the widgets show a placeholder and reminders' notifications
// leave the note's text out when the user chose so. Wrong PINs wait — 30 s after the fifth, up to 15
// minutes — and the count survives a restart.
//
// Settings (AppSettings, keys "privacy/lock/…", this device's): pin (the hash), method ("pin" |
// "device"), onStart, backgroundMinutes, idleMinutes, secureWindow ("whileLocked" | "always"),
// hideNotificationText, hideWidgets, failures, lastFailure.
#pragma once

#include "applock/applock.h"

#include <QElapsedTimer>
#include <QObject>
#include <QPointer>
#include <QTimer>
#include <QVariantMap>

#include <functional>

namespace brulion::shell {

class AppSettings;

class AppLockController : public QObject {
    Q_OBJECT
    /// The lock is on: a PIN is set (method "pin") or the phone's check chosen (method "device").
    Q_PROPERTY(bool enabled READ enabled NOTIFY changed FINAL)
    /// "pin" | "device".
    Q_PROPERTY(QString method READ method NOTIFY changed FINAL)
    /// A PIN is stored (also with the device's method: the PIN is then unused).
    Q_PROPERTY(bool hasPin READ hasPin NOTIFY changed FINAL)
    /// The phone's biometrics or screen lock can be asked for (Android 10+ with a screen lock).
    Q_PROPERTY(bool deviceAvailable READ deviceAvailable NOTIFY changed FINAL)
    /// The lock screen is up: the content is hidden.
    Q_PROPERTY(bool locked READ locked NOTIFY lockedChanged FINAL)
    Q_PROPERTY(bool lockOnStart READ lockOnStart WRITE setLockOnStart NOTIFY changed FINAL)
    /// Lock after this many minutes in the background (0: every time the app is left).
    Q_PROPERTY(int backgroundMinutes READ backgroundMinutes WRITE setBackgroundMinutes NOTIFY changed FINAL)
    /// Lock after this many minutes without input while the app is in front (0: never; the desktop).
    Q_PROPERTY(int idleMinutes READ idleMinutes WRITE setIdleMinutes NOTIFY changed FINAL)
    /// Android: "whileLocked" (FLAG_SECURE while the lock screen is up and while Brulion is not in
    /// front; the recent apps' preview left out on Android 13+ while the lock is on) | "always"
    /// (FLAG_SECURE while the lock is on: no screenshots of Brulion at all).
    Q_PROPERTY(QString secureWindow READ secureWindow WRITE setSecureWindow NOTIFY changed FINAL)
    /// Reminders' notifications say only "Reminder" and the time, never the note's text.
    Q_PROPERTY(bool hideNotificationText READ hideNotificationText WRITE setHideNotificationText NOTIFY changed FINAL)
    /// Android's home-screen widgets show "Brulion is locked" while the lock is on.
    Q_PROPERTY(bool hideWidgets READ hideWidgets WRITE setHideWidgets NOTIFY changed FINAL)
    /// Seconds before the next PIN may be tried (0: at once).
    Q_PROPERTY(int retrySeconds READ retrySeconds NOTIFY attemptsChanged FINAL)
    /// Wrong PINs in a row.
    Q_PROPERTY(int failures READ failures NOTIFY attemptsChanged FINAL)
    /// A PIN is being checked (the hash takes a moment).
    Q_PROPERTY(bool checking READ checking NOTIFY checkingChanged FINAL)

public:
    explicit AppLockController(QObject *parent = nullptr);
    ~AppLockController() override;

    /// The settings; reads the policy and the wrong-PIN count, and starts locked when it should
    /// (call once, before the window shows).
    void setSettings(AppSettings *settings);
    /// Clocks for tests (monotonic ms, wall-clock ms).
    void setClocks(std::function<qint64()> monotonic, std::function<qint64()> wall);
    /// Check PINs at once on this thread (tests).
    void setSynchronous(bool synchronous) { m_synchronous = synchronous; }
    /// The app's start: locked when the lock is on and lockOnStart (setSettings() does this).
    void start();

    bool enabled() const;
    QString method() const;
    bool hasPin() const;
    bool deviceAvailable() const;
    bool locked() const { return m_state.locked(); }
    bool lockOnStart() const;
    void setLockOnStart(bool on);
    int backgroundMinutes() const;
    void setBackgroundMinutes(int minutes);
    int idleMinutes() const;
    void setIdleMinutes(int minutes);
    QString secureWindow() const;
    void setSecureWindow(const QString &mode);
    bool hideNotificationText() const;
    void setHideNotificationText(bool on);
    bool hideWidgets() const;
    void setHideWidgets(bool on);
    int retrySeconds() const;
    int failures() const { return m_state.attempts().failures; }
    bool checking() const { return m_checking; }
    /// The widgets show the placeholder now (the lock is on and hideWidgets).
    bool hidesWidgets() const { return enabled() && hideWidgets(); }
    /// Notifications leave the note's text out now.
    bool hidesNotificationText() const { return enabled() && hideNotificationText(); }

    /// A PIN Brulion accepts (4–16 digits).
    Q_INVOKABLE bool validPin(const QString &pin) const;
    /// Turn the lock on with `pin`, or change the PIN (`current`: the one set, when one is). { ok,
    /// error } (translated). Also switches the method to "pin".
    Q_INVOKABLE QVariantMap setPin(const QString &current, const QString &pin);
    /// Use the phone's check (Android): { ok, error }. `current`: the PIN set, when one is.
    Q_INVOKABLE QVariantMap useDevice(const QString &current);
    /// Turn the lock off (`current`: the PIN, when the method is "pin"): { ok, error }.
    Q_INVOKABLE QVariantMap turnOff(const QString &current);
    /// Try a PIN on the lock screen: unlockFinished(ok, error) follows (also for a wait).
    Q_INVOKABLE void unlock(const QString &pin);
    /// Ask the phone's biometrics or screen lock: unlockFinished(ok, error) follows.
    Q_INVOKABLE void unlockWithDevice();
    /// Lock now (the command, the tray).
    Q_INVOKABLE void lockNow();
    /// The app's state changed (QGuiApplication::applicationStateChanged; the self-test calls it):
    /// in front again after the background may lock it.
    void applicationStateChanged(Qt::ApplicationState state);

Q_SIGNALS:
    void changed();
    void lockedChanged();
    void attemptsChanged();
    void checkingChanged();
    /// A PIN or the phone's check answered: unlocked (`ok`), else why (translated; "" cancelled).
    void unlockFinished(bool ok, const QString &error);

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    applock::Policy policy() const;
    void applyPolicy();
    void setLocked(bool locked);
    void applyWindowFlags();
    void storeAttempts();
    void scheduleTick();
    qint64 monotonicNow() const;
    qint64 wallNow() const;
    QString stored(const QString &key, const QString &fallback = {}) const;
    void store(const QString &key, const QVariant &value);

    QPointer<AppSettings> m_settings;
    applock::LockState m_state;
    QElapsedTimer m_elapsed;
    std::function<qint64()> m_monotonic;
    std::function<qint64()> m_wall;
    QTimer m_tick;
    bool m_synchronous = false;
    bool m_checking = false;
    bool m_filterInstalled = false;
    bool m_started = false;
    bool m_foreground = true; // the app's state is Qt::ApplicationActive
};

} // namespace brulion::shell
