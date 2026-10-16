// The sound of ⏰ reminders and ⏰! alarms — a device-local setting (ReminderSounds keeps it in
// AppSettings; a ringtone URI belongs to one phone) applied by each platform's notifier:
//
//   Default      the system's sound: Android the channel's default (notification sound for
//                reminders, alarm sound for alarms); Linux no sound hint for reminders (the
//                desktop decides), "alarm-clock-elapsed" for alarms; the browser its own
//   Custom       `uri`: Android a ringtone URI from the system picker (content://…); Linux a
//                freedesktop sound-theme name ("bell", hint "sound-name") or a sound file
//                (file:///…/x.oga, hint "sound-file")
//   VibrateOnly  Android: no sound, vibration only (desktops: like Silent)
//   Silent       no sound and no vibration ("suppress-sound" on Linux)
//
// A custom uri/title is kept while another mode is selected, so switching back to Custom brings
// the chosen ringtone back. Pure functions, unit-tested (tests/app/notify).
#pragma once

#include <QList>
#include <QMetaType>
#include <QString>
#include <QStringList>
#include <QVariantMap>

namespace brulion::notify {

/// Which sound setting: ⏰ reminders or ⏰! alarms.
enum class SoundKind { Reminder, Alarm };

struct SoundChoice {
    enum class Mode { Default, Custom, VibrateOnly, Silent };

    Mode mode = Mode::Default;
    /// The custom sound (see the file comment); kept while another mode is selected.
    QString uri;
    /// Shown name of the custom sound (ringtone title, file name); "" = derived by the UI.
    QString title;

    /// A custom sound plays (mode Custom with a uri).
    bool isCustom() const { return mode == Mode::Custom && !uri.isEmpty(); }

    friend bool operator==(const SoundChoice &, const SoundChoice &) = default;
};

/// "reminder" | "alarm".
QString soundKindKey(SoundKind kind);
/// The kind of a key; false when unknown.
bool soundKindFromKey(const QString &key, SoundKind *kind);

/// "default" | "custom" | "vibrate" | "silent" (settings, QML, the Android JSON).
QString soundModeKey(SoundChoice::Mode mode);
/// The mode of a key; false (and Default) when unknown.
bool soundModeFromKey(const QString &key, SoundChoice::Mode *mode);

/// Trimmed; Custom without a uri becomes Default.
SoundChoice normalizedSound(SoundChoice choice);

/// {mode, uri, title}.
QVariantMap soundToVariant(const SoundChoice &choice);
/// From {mode, uri, title} (normalised; anything unknown is the default).
SoundChoice soundFromVariant(const QVariantMap &map);

/// Linux: the custom uri is a sound file (a file: URL or an absolute path), not a theme name.
bool isSoundFile(const QString &uri);
/// The local path of a sound file uri ("" for theme names).
QString soundFilePath(const QString &uri);

/// Freedesktop sound-theme names the settings offer (sound-naming specification; labels are
/// the UI's): message-new-instant, bell, complete, alarm-clock-elapsed, phone-incoming-call.
QStringList desktopThemeSounds();
/// The theme sound of alarms with the default sound.
QString defaultAlarmThemeSound();

/// Sets the freedesktop notification's sound hints for `choice` (removes earlier ones):
/// "sound-name" / "sound-file", or "suppress-sound" for Silent and VibrateOnly. The Default of
/// an alarm is "alarm-clock-elapsed"; the Default of a reminder sets nothing.
void applyDesktopSoundHints(QVariantMap &hints, const SoundChoice &choice, SoundKind kind);

} // namespace brulion::notify

Q_DECLARE_METATYPE(brulion::notify::SoundChoice)
Q_DECLARE_METATYPE(brulion::notify::SoundKind)
