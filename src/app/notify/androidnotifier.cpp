#include "androidnotifier.h"

#include "androidpayload.h"

#include "android/androidbridge.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QLoggingCategory>

namespace brulion::notify {

Q_LOGGING_CATEGORY(lcAndroidNotify, "brulion.notify.android")

AndroidNotifier::AndroidNotifier(QObject *parent) : Notifier(parent)
{
    connect(platform::AndroidBridge::instance(), &platform::AndroidBridge::activated, this,
            &AndroidNotifier::activated);
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
