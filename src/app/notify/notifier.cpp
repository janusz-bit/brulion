#include "notifier.h"

#include "desktopnotifier.h"

#if defined(Q_OS_ANDROID)
#include "androidnotifier.h"
#elif defined(Q_OS_WASM)
#include "wasmnotifiersink.h"
#elif defined(BRULION_NOTIFY_HAVE_WINTOAST)
#include "appidentity.h" // src/platform
#include "windowstoastbus.h"
#elif defined(BRULION_NOTIFY_HAVE_DBUS)
#include "dbusnotificationbus.h"
#endif

#include <algorithm>

namespace brulion::notify {

Notifier::Notifier(QObject *parent) : QObject(parent) { }

Notifier::~Notifier() = default;

void Notifier::setSnoozeMinutes(int minutes)
{
    m_snoozeMinutes = std::clamp(minutes, 1, 120);
}

void Notifier::schedule(const QList<ScheduledNotification> &notifications)
{
    m_scheduled = notifications;
}

void Notifier::setSounds(const SoundChoice &reminder, const SoundChoice &alarm)
{
    m_reminderSound = normalizedSound(reminder);
    m_alarmSound = normalizedSound(alarm);
    soundsChanged();
}

SoundChoice Notifier::sound(SoundKind kind) const
{
    return kind == SoundKind::Alarm ? m_alarmSound : m_reminderSound;
}

QList<SoundChoice::Mode> Notifier::soundModes() const
{
    return {SoundChoice::Mode::Default, SoundChoice::Mode::Silent};
}

bool Notifier::pickSound(SoundKind kind)
{
    Q_UNUSED(kind);
    return false;
}

bool Notifier::previewSound(SoundKind kind, const SoundChoice &choice)
{
    Q_UNUSED(kind);
    Q_UNUSED(choice);
    return false;
}

bool Notifier::openSystemSoundSettings(SoundKind kind)
{
    Q_UNUSED(kind);
    return false;
}

QString soundText(SoundText text)
{
    switch (text) {
    case SoundText::ReminderSample: return Notifier::tr("Reminder sound");
    case SoundText::AlarmSample: return Notifier::tr("Alarm sound");
    case SoundText::ReminderSampleBody: return Notifier::tr("This is how reminders will sound.");
    case SoundText::AlarmSampleBody: return Notifier::tr("This is how alarms will sound.");
    case SoundText::NoService: return Notifier::tr("No notification service on this desktop: sounds cannot be played.");
    case SoundText::NoPicker: return Notifier::tr("This phone has no ringtone picker.");
    case SoundText::CannotPlay: return Notifier::tr("This sound cannot be played.");
    case SoundText::CannotApply: return Notifier::tr("Could not change the sound of reminders.");
    case SoundText::BrowserRefused: return Notifier::tr("The browser does not allow notifications for Brulion.");
    }
    return {};
}

Notifier *createPlatformNotifier(QObject *parent)
{
#if defined(Q_OS_ANDROID)
    return new AndroidNotifier(parent);
#elif defined(Q_OS_WASM)
    return new WasmNotifierSink(parent);
#elif defined(BRULION_NOTIFY_HAVE_WINTOAST)
    return new DesktopNotifier(new WindowsToastBus(platform::appUserModelId()), parent);
#elif defined(BRULION_NOTIFY_HAVE_DBUS)
    return new DesktopNotifier(new DBusNotificationBus, parent);
#else
    return new DesktopNotifier(nullptr, parent);
#endif
}

} // namespace brulion::notify
