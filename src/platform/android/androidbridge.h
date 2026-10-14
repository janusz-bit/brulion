// Brulion — offline-first Markdown notes. SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

// Android only (compiled when CMake's ANDROID is set): the C++ side of
// android/src/io/github/januszbit/brulion/android/BrulionAndroid.kt, see docs/ANDROID.md.

#include <QJsonArray>
#include <QObject>
#include <QString>

namespace brulion::platform {

/// Result of AndroidBridge::scheduleReminders().
struct ScheduleResult
{
    bool ok = false;     ///< false: the call failed (`error` says why) and the armed set is unchanged
    int scheduled = 0;   ///< reminders armed now (future ones, at most 400, plus pending snoozes)
    bool exact = false;  ///< exact alarms; false = the inexact fallback (no exact-alarm permission)
    QString error;
};

/// A tapped reminder notification: the note to open and the reminder's line in it.
struct Activation
{
    QString path;
    int line = 0;
    bool isValid() const { return !path.isEmpty(); }
};

/**
 * Brulion's Android integration through JNI: storage and notification permissions, exact
 * alarms, the native reminder scheduler (AlarmManager — fires while the app is closed and
 * after a reboot) and taps on reminder notifications.
 *
 * Use from the GUI thread only, after QGuiApplication exists. Calls are synchronous JNI calls
 * (cheap; `request*` only post a dialog or settings screen to Android's main thread and return
 * at once — re-check the state on permissionsChanged()).
 */
class AndroidBridge final : public QObject
{
    Q_OBJECT

public:
    /// The bridge (created on first use, child of the application object).
    static AndroidBridge *instance();
    ~AndroidBridge() override;

    // All-files access: a vault folder on shared storage (e.g. a Syncthing folder).
    bool hasAllFilesAccess() const;
    void requestAllFilesAccess();

    // Notifications (Android 13+ runtime permission, or turned off in the settings).
    bool notificationPermission() const;
    /// Shows the system dialog, or the notification settings when it would not show any more.
    /// Returns the current state (true = nothing to ask).
    bool requestNotificationPermission();

    // Exact alarms (Android 12 needs the user's consent; 13+ grants USE_EXACT_ALARM).
    bool canScheduleExact() const;
    void requestExactAlarms();

    // Full-screen alarm screen over the lock screen (Android 14+ special access).
    bool canUseFullScreenIntent() const;
    void requestFullScreenIntent();

    /**
     * Replaces every armed reminder with @p items. Each item is a JSON object
     * `{id: int, title, body, at: epoch ms, alarm: bool, ringMinutes: int (alarms, 1–60,
     * default 10), path: vault-relative note, line: int}`; ids must be stable across calls
     * (re-arming an id replaces its alarm). Past items are skipped; snoozes made in the
     * notification survive unless their id is sent again.
     */
    ScheduleResult scheduleReminders(const QJsonArray &items);

    /// The notification tap not delivered through activated() yet (cold start); empty if none.
    /// Each tap is handed out once.
    Activation takeActivation();

Q_SIGNALS:
    /// A reminder notification (or "Otwórz notatkę" on the alarm screen) was tapped.
    void activated(const QString &path, int line);
    /// A permission may have changed (dialog answered, or the app returned to the front).
    void permissionsChanged();

private:
    explicit AndroidBridge(QObject *parent);
    void deliverPendingActivation();
};

} // namespace brulion::platform
