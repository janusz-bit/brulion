// Brulion — offline-first Markdown notes. SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

// Android only (compiled when CMake's ANDROID is set): the C++ side of
// android/src/io/github/januszbit/brulion/android/BrulionAndroid.kt, see docs/ANDROID.md.

#include <QJsonArray>
#include <QJsonObject>
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

/// Result of AndroidBridge::saveToDownloads().
struct DownloadResult {
    bool ok = false;
    bool unsupported = false; ///< Android 9: no MediaStore downloads (write the file directly)
    QString name;             ///< the file's name in Downloads ("Notatki (1).zip" when taken)
    QString error;
};

/// A document picked through the system's picker (Storage Access Framework), for imports.
struct DocumentInfo {
    QString uri;  ///< document URI (a picked tree: the URI of its root document)
    QString name; ///< display name
    bool isDir = false;
    qint64 size = -1;       ///< -1 = unknown
    bool isVirtual = false; ///< no bytes of its own (Google Docs in Drive)
};

/// Result of AndroidBridge::documentInfo() / listDocuments().
struct DocumentsResult {
    bool ok = false;
    /// The JNI call itself failed (no context, the Kotlin method missing or throwing): the
    /// caller may fall back to Qt's content file engine. `ok` false without it: Android
    /// answered with an error (`error`: permission gone, document deleted…).
    bool bridgeFailed = false;
    QString error;
    QList<DocumentInfo> documents; ///< documentInfo(): one; listDocuments(): the folder's entries
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

    /// The share of another app not delivered through shared() yet (cold start: ShareActivity
    /// started the process), as JSON (notebook/share.h: `{"subject", "text", "dir", "files"}`);
    /// empty if none. Each share is handed out once.
    QByteArray takeShare();

    /**
     * The "Zrobione" taps on reminder notifications and the alarm screen not handed over yet
     * (queued by DoneQueue.kt — while Brulion was closed, too): a JSON array of
     * `{key, local, path, line, title, day, vault}` (src/app/notify/androidpayload.h). Only the
     * taps of notebook @p vault (and those without one: reminders armed by an older version);
     * the others stay queued. Empty @p vault: all. Each tap is handed out once;
     * doneRequestsQueued() says when there may be new ones.
     */
    QJsonArray takeDoneRequests(const QString &vault = {});

    /**
     * Saves @p data as @p name in the public Download folder through MediaStore (Android 10+,
     * API 29): no storage permission is needed — with scoped storage a plain file write there
     * fails without "All files access". The system adds " (1)" to a taken name (`name` of the
     * result). Android 9: `unsupported` (the Download folder is written as a file there).
     */
    DownloadResult saveToDownloads(const QString &name, const QString &mimeType, const QByteArray &data);

    /**
     * Offers the file at @p path to other apps through the system's share sheet (ACTION_SEND
     * with a content:// URI of Qt's FileProvider "<package>.qtprovider", read permission
     * granted to the receiver). The file must lie below the app's files dir (the provider's
     * only internal path). @p subject goes into EXTRA_SUBJECT (mail apps), @p chooserTitle
     * above the sheet. False when the intent could not be started.
     */
    bool shareFile(const QString &path, const QString &mimeType, const QString &subject, const QString &chooserTitle);

    // Documents picked with the system's picker (content:// URIs) for imports: one
    // ContentResolver query per call. Static and thread-safe — import listings run on worker
    // threads (JNI attaches them); no bridge instance is needed. The bytes are read with QFile
    // on the URI (Qt's content file engine).

    /// Name, kind and size of a picked document, a picked tree (answered with its root
    /// document) or any openable content URI.
    static DocumentsResult documentInfo(const QString &uri);
    /// The entries of a folder document of a picked tree (one level).
    static DocumentsResult listDocuments(const QString &folderUri);

    // Sounds of reminders (kind 0) and alarms (kind 1): ReminderSounds.kt. A choice is
    // `{mode: "default" | "custom" | "vibrate" | "silent", uri, title}` (src/app/notify/
    // androidpayload.h); the Kotlin side keeps them in SharedPreferences (alarms ring while the
    // app is closed) and gives a kind whose sound changed a new notification channel.

    /// Applies `{reminder: choice, alarm: choice}`. Returns the kinds' channel ids now
    /// (`{"reminderChannel", "alarmChannel"}`), or `{"error": …}`; an empty object when the
    /// call failed.
    QJsonObject setReminderSounds(const QJsonObject &sounds);
    /// Opens the system's ringtone picker for `kind` showing `current`; the answer arrives as
    /// soundPicked() (nothing when cancelled). False when it could not be asked for.
    bool pickSound(int kind, const QJsonObject &current);
    /// Plays `{kind: "reminder" | "alarm", mode, uri}` for about three seconds (vibrates for
    /// "vibrate"). False when there is nothing to play.
    bool previewSound(const QJsonObject &choice);
    /// Opens the system's settings of the notification channel of `kind`.
    void openSoundSettings(int kind);

    /// Asks the keyboard observer (ImeInsets.kt) to report the keyboard's state and the system
    /// gesture insets again: they arrive as imeChanged() and systemGesturesChanged().
    void requestImeState();

    /// The system bars' icons over a window drawn behind them (Android 11+): dark over a light
    /// window (`light`), light over a dark one.
    void setSystemBarsLight(bool light);

Q_SIGNALS:
    /// A reminder notification (or "Otwórz notatkę" on the alarm screen) was tapped.
    void activated(const QString &path, int line);
    /// Another app shared text or files with Brulion ("Udostępnij → Brulion": ShareActivity.kt
    /// copied the files to files/share-in/<stamp>/); `json` as takeShare() gives it.
    void shared(const QByteArray &json);
    /// A permission may have changed (dialog answered, or the app returned to the front).
    void permissionsChanged();
    /// "Zrobione" was tapped (or the app returned to the front): takeDoneRequests() may have
    /// some. Also emitted once after start-up, from the event loop.
    void doneRequestsQueued();
    /// The ringtone picker of pickSound() answered: `choice` is `{mode, uri, title}` or
    /// `{"error": …}` (no picker on this phone).
    void soundPicked(int kind, const QJsonObject &choice);
    /// The on-screen keyboard covers `overlap` physical pixels of Qt's view (0: hidden, or the
    /// window was resized above it); `shown` while it is up or coming up; `animating` for every
    /// frame of its slide (Android 11+), then once more settled (ImeInsets.kt; KeyboardInsets).
    void imeChanged(int overlap, bool shown, bool animating);
    /// The system keeps `left` and `right` physical pixels of Qt's view at its edges for its own
    /// swipes (gesture navigation's "back"; 0 with buttons) (ImeInsets.kt; GestureInsets).
    void systemGesturesChanged(int left, int right);

private:
    explicit AndroidBridge(QObject *parent);
    void deliverPendingActivation();
    void deliverPendingShare();
};

} // namespace brulion::platform
