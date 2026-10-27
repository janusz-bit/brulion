#include "androidnotifier.h"

#include "androidpayload.h"

#include "android/androidbridge.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QJsonObject>
#include <QLoggingCategory>

namespace brulion::notify {

Q_LOGGING_CATEGORY(lcAndroidNotify, "brulion.notify.android")

namespace {

int kindCode(SoundKind kind)
{
    return kind == SoundKind::Alarm ? 1 : 0;
}

} // namespace

AndroidNotifier::AndroidNotifier(QObject *parent) : Notifier(parent)
{
    auto *bridge = platform::AndroidBridge::instance();
    connect(bridge, &platform::AndroidBridge::activated, this, &AndroidNotifier::activated);
    connect(bridge, &platform::AndroidBridge::soundPicked, this, [this](int kind, const QJsonObject &answer) {
        bool ok = false;
        const SoundChoice picked = androidPickedSound(answer, &ok);
        if (!ok) {
            qCWarning(lcAndroidNotify) << "Ringtone picker:" << answer.value(QStringLiteral("error")).toString();
            Q_EMIT errorOccurred(soundText(SoundText::NoPicker));
            return;
        }
        Q_EMIT soundPicked(kind == 1 ? SoundKind::Alarm : SoundKind::Reminder, picked);
    });
    connect(bridge, &platform::AndroidBridge::doneRequestsQueued, this, &AndroidNotifier::takeDoneRequests);
    // Taps queued while Brulion was closed: from the event loop, once the shell listens.
    QMetaObject::invokeMethod(this, &AndroidNotifier::takeDoneRequests, Qt::QueuedConnection);
}

void AndroidNotifier::takeDoneRequests()
{
    const QList<AndroidDoneRequest> requests =
            androidDoneRequests(platform::AndroidBridge::instance()->takeDoneRequests());
    for (const AndroidDoneRequest &request : requests) {
        qCInfo(lcAndroidNotify) << "Done tapped for" << request.notification.path << "on" << request.day;
        Q_EMIT doneRequested(request.notification, request.day);
    }
}

AndroidNotifier::~AndroidNotifier() = default;

QString AndroidNotifier::backend() const
{
    return QStringLiteral("android");
}

bool AndroidNotifier::canShow() const
{
    return platform::AndroidBridge::instance()->notificationPermission();
}

void AndroidNotifier::schedule(const QList<ScheduledNotification> &notifications)
{
    Notifier::schedule(notifications);
    arm(notifications);
}

void AndroidNotifier::showNow(const ScheduledNotification &notification)
{
    ScheduledNotification now = notification;
    const QDateTime soon = QDateTime::currentDateTime().addSecs(2);
    if (!now.at.isValid() || now.at < soon)
        now.at = soon;
    if (now.nativeId <= 0)
        now.nativeId = nativeIdFor(now.id);
    QList<ScheduledNotification> all = m_scheduled;
    all.removeIf([&](const ScheduledNotification &n) { return n.nativeId == now.nativeId; });
    all.append(now);
    arm(all);
}

QList<SoundChoice::Mode> AndroidNotifier::soundModes() const
{
    return {SoundChoice::Mode::Default, SoundChoice::Mode::Custom, SoundChoice::Mode::VibrateOnly,
            SoundChoice::Mode::Silent};
}

bool AndroidNotifier::pickSound(SoundKind kind)
{
    return platform::AndroidBridge::instance()->pickSound(kindCode(kind), androidSoundItem(sound(kind)));
}

bool AndroidNotifier::previewSound(SoundKind kind, const SoundChoice &choice)
{
    if (platform::AndroidBridge::instance()->previewSound(androidSoundPreview(kind, choice)))
        return true;
    if (choice.mode != SoundChoice::Mode::Silent)
        Q_EMIT errorOccurred(soundText(SoundText::CannotPlay));
    return false;
}

bool AndroidNotifier::openSystemSoundSettings(SoundKind kind)
{
    platform::AndroidBridge::instance()->openSoundSettings(kindCode(kind));
    return true;
}

void AndroidNotifier::soundsChanged()
{
    const QJsonObject answer =
            platform::AndroidBridge::instance()->setReminderSounds(androidSoundSettings(m_reminderSound, m_alarmSound));
    if (answer.isEmpty() || answer.contains(QStringLiteral("error"))) {
        qCWarning(lcAndroidNotify) << "Could not apply the reminder sounds:" << answer;
        Q_EMIT errorOccurred(soundText(SoundText::CannotApply));
        return;
    }
    qCInfo(lcAndroidNotify) << "Notification channels:" << answer;
}

void AndroidNotifier::arm(const QList<ScheduledNotification> &notifications)
{
    const platform::ScheduleResult result =
            platform::AndroidBridge::instance()->scheduleReminders(androidReminderPayload(notifications));
    if (!result.ok) {
        qCWarning(lcAndroidNotify) << "Could not arm reminders:" << result.error;
        Q_EMIT errorOccurred(QCoreApplication::translate("brulion::notify::AndroidNotifier",
                                                         "Could not schedule reminders: %1")
                                     .arg(result.error));
    }
}

} // namespace brulion::notify
