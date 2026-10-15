#include "notifier.h"

#include "desktopnotifier.h"

#if defined(Q_OS_ANDROID)
#include "androidnotifier.h"
#elif defined(Q_OS_WASM)
#include "wasmnotifiersink.h"
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

Notifier *createPlatformNotifier(QObject *parent)
{
#if defined(Q_OS_ANDROID)
    return new AndroidNotifier(parent);
#elif defined(Q_OS_WASM)
    return new WasmNotifierSink(parent);
#elif defined(BRULION_NOTIFY_HAVE_DBUS)
    return new DesktopNotifier(new DBusNotificationBus, parent);
#else
    return new DesktopNotifier(nullptr, parent);
#endif
}

} // namespace brulion::notify
