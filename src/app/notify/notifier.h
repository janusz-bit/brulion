// Notifier — where reminders and alarms reach the user on this platform (docs/ARCHITECTURE.md
// "Platform layer"): a QObject implementing the core's reminders::NotificationSink, plus the
// signals the reminders integration (shell/workspacereminders.h) listens to.
//
//   DesktopNotifier    Linux: org.freedesktop.Notifications over QtDBus (actions "Snooze" /
//                      "OK" / click = open, alarms critical + sound + resident and ringing again
//                      for their ring time); without a notification service the tray shows them
//                      (trayMessageRequested). In-app timed: firesWhenClosed() = false.
//                      Windows: the same over toast notifications (WindowsToastBus: buttons,
//                      alarms on screen with a looping sound); the tray when they are off.
//   AndroidNotifier    AndroidBridge::scheduleReminders() (AlarmManager: fires while the app is
//                      closed and after a reboot; firesWhenClosed() = true). Snoozing happens
//                      in the Android notification itself.
//   WasmNotifierSink   the browser's Notification API through platform::WasmNotifier (only
//                      while the page is open; firesWhenClosed() = false).
//
// Sounds (soundchoice.h): ReminderSounds hands the settings' choices to setSounds(); each
// notifier plays them its way and says what its platform offers (soundModes(), a system
// ringtone picker, system sound settings, sound files, theme sounds) and previews them.
//
// createPlatformNotifier() picks the one of this build. Everything runs on the GUI thread.
#pragma once

#include "reminders/notifications.h"
#include "soundchoice.h"

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

    /// "dbus", "toast" (Windows), "tray", "android", "browser" or "none".
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

    // ---- Sounds (ReminderSounds, Settings → Reminders) ----------------------------------------

    /// The sounds of ⏰ reminders and ⏰! alarms (normalised); applied at once (soundsChanged()).
    void setSounds(const SoundChoice &reminder, const SoundChoice &alarm);
    SoundChoice sound(SoundKind kind) const;
    /// The choices this platform offers (default: Default and Silent).
    virtual QList<SoundChoice::Mode> soundModes() const;
    /// pickSound() opens the system's ringtone picker (Android).
    virtual bool hasSystemSoundPicker() const { return false; }
    /// openSystemSoundSettings() opens the system's settings of a kind's sound (Android).
    virtual bool hasSystemSoundSettings() const { return false; }
    /// A sound file may be the custom sound (Linux: "sound-file").
    virtual bool supportsSoundFiles() const { return false; }
    /// Sound-theme names a custom sound may be (Linux: desktopThemeSounds()).
    virtual QStringList themeSounds() const { return {}; }
    /// Opens the system's ringtone picker for `kind`; the choice arrives as soundPicked().
    /// False: there is none.
    virtual bool pickSound(SoundKind kind);
    /// Plays `choice` for a few seconds as `kind` would sound. False (with errorOccurred()
    /// saying why) when it cannot.
    virtual bool previewSound(SoundKind kind, const SoundChoice &choice);
    /// Opens the system's notification settings of `kind` (Android: its channel). False: none.
    virtual bool openSystemSoundSettings(SoundKind kind);

Q_SIGNALS:
    /// The user opened the note from a notification (click, "Open" on the alarm screen).
    void activated(const QString &path, int line);
    /// The user chose "Snooze" on a notification: move the reminder `minutes` later.
    void snoozeRequested(const brulion::ScheduledNotification &notification, int minutes);
    /// Arming or showing notifications failed (translated message).
    void errorOccurred(const QString &message);
    /// The system's ringtone picker answered (pickSound()).
    void soundPicked(brulion::notify::SoundKind kind, const brulion::notify::SoundChoice &choice);

protected:
    /// setSounds() was called: apply m_reminderSound / m_alarmSound (Android: the channels).
    virtual void soundsChanged() { }

    QList<ScheduledNotification> m_scheduled;
    int m_snoozeMinutes = DefaultSnoozeMinutes;
    SoundChoice m_reminderSound;
    SoundChoice m_alarmSound;
};

/// The notifier of this build (Android, browser, or the Linux desktop's D-Bus notifier with the
/// tray as fallback), owned by `parent`.
Notifier *createPlatformNotifier(QObject *parent = nullptr);

/// Texts of the notifiers' sounds. Translated in notifier.cpp (context
/// "brulion::notify::Notifier"), which every build compiles: the desktop build's lupdate also
/// sees the texts of the Android and browser notifiers.
enum class SoundText {
    ReminderSample,     ///< title of a sample notification: "Reminder sound"
    AlarmSample,        ///< "Alarm sound"
    ReminderSampleBody, ///< "This is how reminders will sound."
    AlarmSampleBody,    ///< "This is how alarms will sound."
    NoService,          ///< Linux: no notification service to play sounds
    NoPicker,           ///< Android: no ringtone picker
    CannotPlay,         ///< Android: the sound cannot be played
    CannotApply,        ///< Android: the notification channels could not be changed
    BrowserRefused,     ///< browser: notifications are not allowed
};
QString soundText(SoundText text);

} // namespace brulion::notify
