// ReminderSounds — the sound settings of ⏰ reminders and ⏰! alarms (QML singleton
// `ReminderSounds`, Settings → Reminders: qml/notify/ReminderSoundSetting.qml). Device-local: a
// ringtone URI belongs to one phone, so the choices live in AppSettings (the shell's wiring hands
// over AppSettings::value / setValue), under
//
//   reminders/sound/<reminder|alarm>/mode    default | custom | vibrate | silent
//   reminders/sound/<reminder|alarm>/uri     the custom sound (kept while another mode is set)
//   reminders/sound/<reminder|alarm>/title   its shown name
//
// Every change goes to the platform's notifier at once (Notifier::setSounds(): the D-Bus hints,
// Android's notification channels — persisted on the Kotlin side too, alarms ring while the app
// is closed —, the browser's tone). The notifier also says what the platform offers (modes, the
// system ringtone picker, sound files, theme sounds), opens its picker and previews sounds.
// Kinds and modes are strings in QML: "reminder" / "alarm", "default" / "custom" / "vibrate" /
// "silent" (soundchoice.h).
#pragma once

#include "soundchoice.h"

#include <QObject>
#include <QPointer>
#include <QStringList>
#include <QVariant>
#include <QVariantMap>

#include <functional>

namespace brulion::notify {

class Notifier;

class ReminderSounds : public QObject {
    Q_OBJECT

    /// {mode, uri, title} of ⏰ reminders / ⏰! alarms (uri/title: the custom sound, also when
    /// another mode is selected).
    Q_PROPERTY(QVariantMap reminder READ reminder NOTIFY changed FINAL)
    Q_PROPERTY(QVariantMap alarm READ alarm NOTIFY changed FINAL)
    /// The modes this platform offers, in display order (keys).
    Q_PROPERTY(QStringList modes READ modes NOTIFY capabilitiesChanged FINAL)
    /// "Choose ringtone…" opens the system's picker (Android); else the page offers
    /// themeSounds and (soundFiles) a sound file.
    Q_PROPERTY(bool systemPicker READ systemPicker NOTIFY capabilitiesChanged FINAL)
    /// The system's notification settings of each kind can be opened (Android).
    Q_PROPERTY(bool systemSettings READ systemSettings NOTIFY capabilitiesChanged FINAL)
    Q_PROPERTY(bool soundFiles READ soundFiles NOTIFY capabilitiesChanged FINAL)
    /// Freedesktop sound-theme names (Linux).
    Q_PROPERTY(QStringList themeSounds READ themeSounds NOTIFY capabilitiesChanged FINAL)

public:
    /// Reads a stored value ("" when absent) / stores one.
    using Load = std::function<QVariant(const QString &key)>;
    using Store = std::function<void(const QString &key, const QVariant &value)>;

    explicit ReminderSounds(QObject *parent = nullptr);
    ~ReminderSounds() override;

    /// The instance created last and still alive (the app's, for the QML singleton); nullptr
    /// before.
    static ReminderSounds *instance();

    /// Settings key of `field` ("mode", "uri", "title") of `kind`.
    static QString settingsKey(SoundKind kind, const QString &field);

    /// Where the choices are kept; reads them at once (and hands them to the notifier).
    void setStorage(Load reader, Store writer);
    /// The notifier that plays the sounds: gets the choices now and after every change; its
    /// picker's answers (soundPicked) are stored here. Not owned.
    void setNotifier(Notifier *notifier);

    SoundChoice choice(SoundKind kind) const;
    /// Sets and stores `choice` (normalised); false when it is not offered here.
    bool setChoice(SoundKind kind, const SoundChoice &choice);

    QVariantMap reminder() const { return soundToVariant(choice(SoundKind::Reminder)); }
    QVariantMap alarm() const { return soundToVariant(choice(SoundKind::Alarm)); }
    QStringList modes() const;
    bool systemPicker() const;
    bool systemSettings() const;
    bool soundFiles() const;
    QStringList themeSounds() const;

    /// Selects a mode. "custom" brings back the custom sound chosen before; with none yet it
    /// changes nothing and returns false (choose one first: pick() / setCustom()). False also
    /// for an unknown kind or a mode this platform does not offer.
    Q_INVOKABLE bool setMode(const QString &kind, const QString &mode);
    /// Makes `uri` the custom sound and selects it: Android a ringtone URI, Linux a theme sound
    /// name or a sound file (file:///…). `title` "" = the file's name (theme sounds: none).
    Q_INVOKABLE bool setCustom(const QString &kind, const QString &uri, const QString &title = QString());
    /// Opens the system's ringtone picker (Android); its answer updates the choice. False: none.
    Q_INVOKABLE bool pick(const QString &kind);
    /// Plays the current sound of `kind` for a few seconds (desktop: a sample notification).
    Q_INVOKABLE bool preview(const QString &kind);
    /// The system's notification settings of `kind` (Android: its channel — sound, vibration,
    /// Do Not Disturb). False: none here.
    Q_INVOKABLE bool openSystemSettings(const QString &kind);

Q_SIGNALS:
    void changed();
    void capabilitiesChanged();
    /// preview() asked the notifier to play `kind` (tests, the settings self-test).
    void previewRequested(const QString &kind);

private:
    void loadChoices();
    void saveChoice(SoundKind kind);
    void apply();
    bool offers(SoundChoice::Mode mode) const;

    Load m_load;
    Store m_store;
    QPointer<Notifier> m_notifier;
    QMetaObject::Connection m_pickedConnection;
    SoundChoice m_reminder;
    SoundChoice m_alarm;
};

} // namespace brulion::notify
