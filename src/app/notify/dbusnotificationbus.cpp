#include "dbusnotificationbus.h"

#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
#include <QLoggingCategory>
#include <QTimer>

using namespace Qt::StringLiterals;

namespace brulion::notify {

Q_LOGGING_CATEGORY(lcDBusNotify, "brulion.notify.dbus")

namespace {

const QString kService = u"org.freedesktop.Notifications"_s;
const QString kPath = u"/org/freedesktop/Notifications"_s;
const QString kInterface = u"org.freedesktop.Notifications"_s;
constexpr int kCallTimeoutMs = 10 * 1000;

} // namespace

DBusNotificationBus::DBusNotificationBus(QObject *parent) : NotificationBus(parent)
{
    QDBusConnection bus = QDBusConnection::sessionBus();
    m_connected = bus.isConnected();
    if (!m_connected) {
        qCInfo(lcDBusNotify) << "No session bus: reminders go to the tray";
        return;
    }
    bus.connect(kService, kPath, kInterface, u"ActionInvoked"_s, this, SLOT(onActionInvoked(uint,QString)));
    bus.connect(kService, kPath, kInterface, u"NotificationClosed"_s, this, SLOT(onNotificationClosed(uint,uint)));
    bus.connect(kService, kPath, kInterface, u"ActivationToken"_s, this, SLOT(onActivationToken(uint,QString)));
    queryCapabilities();
}

DBusNotificationBus::~DBusNotificationBus() = default;

void DBusNotificationBus::queryCapabilities()
{
    const QDBusMessage call = QDBusMessage::createMethodCall(kService, kPath, kInterface, u"GetCapabilities"_s);
    auto *watcher = new QDBusPendingCallWatcher(QDBusConnection::sessionBus().asyncCall(call, kCallTimeoutMs), this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this, [this](QDBusPendingCallWatcher *w) {
        const QDBusPendingReply<QStringList> reply = *w;
        m_answered = true;
        m_available = !reply.isError();
        if (m_available)
            m_capabilities = reply.value();
        else
            qCInfo(lcDBusNotify) << "No notification service:" << reply.error().message();
        w->deleteLater();
    });
}

bool DBusNotificationBus::isAvailable() const
{
    return m_connected && (!m_answered || m_available);
}

QStringList DBusNotificationBus::capabilities() const
{
    return m_capabilities;
}

void DBusNotificationBus::notify(const DesktopNotification &n, std::function<void(uint id)> done)
{
    if (!m_connected) {
        QTimer::singleShot(0, this, [done = std::move(done)] {
            if (done)
                done(0);
        });
        return;
    }
    QDBusMessage call = QDBusMessage::createMethodCall(kService, kPath, kInterface, u"Notify"_s);
    call << n.appName << n.replacesId << n.appIcon << n.summary << n.body << n.actions << n.hints << n.expireTimeout;
    auto *watcher = new QDBusPendingCallWatcher(QDBusConnection::sessionBus().asyncCall(call, kCallTimeoutMs), this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this, [this, done = std::move(done)](QDBusPendingCallWatcher *w) {
        const QDBusPendingReply<uint> reply = *w;
        w->deleteLater();
        if (reply.isError()) {
            qCWarning(lcDBusNotify) << "Notify failed:" << reply.error().message();
            // A daemon may appear later (a panel started after Brulion): ask again next time.
            m_answered = true;
            m_available = false;
            if (done)
                done(0);
            return;
        }
        if (!m_available) {
            m_available = true;
            queryCapabilities();
        }
        if (done)
            done(reply.value());
    });
}

void DBusNotificationBus::close(uint id)
{
    if (!m_connected || id == 0)
        return;
    QDBusMessage call = QDBusMessage::createMethodCall(kService, kPath, kInterface, u"CloseNotification"_s);
    call << id;
    QDBusConnection::sessionBus().asyncCall(call, kCallTimeoutMs);
}

void DBusNotificationBus::onActionInvoked(uint id, const QString &actionKey)
{
    Q_EMIT actionInvoked(id, actionKey);
}

void DBusNotificationBus::onNotificationClosed(uint id, uint reason)
{
    Q_EMIT notificationClosed(id, reason);
}

void DBusNotificationBus::onActivationToken(uint id, const QString &token)
{
    Q_EMIT activationTokenReceived(id, token);
}

} // namespace brulion::notify
