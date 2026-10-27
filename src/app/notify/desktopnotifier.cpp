#include "desktopnotifier.h"

#include <QCoreApplication>
#include <QList>

#include <algorithm>
#include <utility>

using namespace Qt::StringLiterals;

namespace brulion::notify {

QString desktopAppId()
{
    return u"io.github.januszbit.brulion"_s;
}

DesktopNotification buildDesktopNotification(const ScheduledNotification &notification, const QStringList &capabilities,
                                             int snoozeMinutes, uint replacesId, const SoundChoice &sound)
{
    DesktopNotification d;
    d.appName = u"Brulion"_s;
    d.replacesId = replacesId;
    d.appIcon = desktopAppId();

    QString title = notification.title.simplified();
    if (title.isEmpty())
        title = notification.body.simplified();
    if (title.isEmpty())
        title = notification.alarm ? QCoreApplication::translate("brulion::notify::DesktopNotifier", "Alarm")
                                   : QCoreApplication::translate("brulion::notify::DesktopNotifier", "Reminder");
    d.summary = notification.alarm ? u"⏰ "_s + title : title;

    // The body may be interpreted as (a small subset of) HTML: note text must never become
    // markup there ("<b>" in a note is text, "&" must not start an entity).
    d.body = capabilities.contains(u"body-markup"_s) ? notification.body.toHtmlEscaped() : notification.body;

    if (capabilities.contains(u"actions"_s)) {
        d.actions = {
            u"default"_s, QCoreApplication::translate("brulion::notify::DesktopNotifier", "Open note"),
            u"snooze"_s, QCoreApplication::translate("brulion::notify::DesktopNotifier", "Snooze %n min", nullptr,
                                                         std::clamp(snoozeMinutes, 1, 120)),
            u"ok"_s, QCoreApplication::translate("brulion::notify::DesktopNotifier", "OK"),
        };
    }

    d.hints.insert(u"urgency"_s, QVariant::fromValue(uchar(notification.alarm ? 2 : 1)));
    d.hints.insert(u"desktop-entry"_s, desktopAppId());
    applyDesktopSoundHints(d.hints, normalizedSound(sound),
                           notification.alarm ? SoundKind::Alarm : SoundKind::Reminder);
    if (notification.alarm) {
        d.hints.insert(u"resident"_s, true);
        d.expireTimeout = 0; // never expires: an alarm waits for the user
    } else {
        d.expireTimeout = -1; // the server's default
    }
    return d;
}

DesktopNotification buildSoundPreview(SoundKind kind, const SoundChoice &choice, uint replacesId)
{
    DesktopNotification d;
    d.appName = u"Brulion"_s;
    d.replacesId = replacesId;
    d.appIcon = desktopAppId();
    d.summary = kind == SoundKind::Alarm ? u"⏰ "_s + soundText(SoundText::AlarmSample)
                                         : soundText(SoundText::ReminderSample);
    d.body = soundText(kind == SoundKind::Alarm ? SoundText::AlarmSampleBody : SoundText::ReminderSampleBody);
    // A normal, passing notification: a sample must not stay on screen like a real alarm.
    d.hints.insert(u"urgency"_s, QVariant::fromValue(uchar(1)));
    d.hints.insert(u"desktop-entry"_s, desktopAppId());
    d.hints.insert(u"transient"_s, true);
    applyDesktopSoundHints(d.hints, normalizedSound(choice), kind);
    d.expireTimeout = 5000;
    return d;
}

NotificationBus::~NotificationBus() = default;

QString NotificationBus::name() const
{
    return u"dbus"_s;
}

QStringList NotificationBus::themeSounds() const
{
    return desktopThemeSounds();
}

// ---- DesktopNotifier --------------------------------------------------------------------------

DesktopNotifier::DesktopNotifier(NotificationBus *bus, QObject *parent) : Notifier(parent), m_bus(bus)
{
    if (bus) {
        bus->setParent(this);
        connect(bus, &NotificationBus::actionInvoked, this, &DesktopNotifier::onActionInvoked);
        connect(bus, &NotificationBus::notificationClosed, this, &DesktopNotifier::onClosed);
        // The server broadcasts the token of every program's notification: keep only ours.
        connect(bus, &NotificationBus::activationTokenReceived, this, [this](uint id, const QString &token) {
            if (m_shown.contains(id))
                m_activationToken = token.toUtf8();
        });
    }
    m_ringTimer.setInterval(DefaultRingIntervalMs);
    connect(&m_ringTimer, &QTimer::timeout, this, &DesktopNotifier::ring);
}

DesktopNotifier::~DesktopNotifier() = default;

QString DesktopNotifier::backend() const
{
    if (m_bus && m_bus->isAvailable())
        return m_bus->name();
    return m_trayAvailable ? u"tray"_s : u"none"_s;
}

bool DesktopNotifier::canShow() const
{
    return (m_bus && m_bus->isAvailable()) || m_trayAvailable;
}

void DesktopNotifier::setTrayAvailable(bool available)
{
    m_trayAvailable = available;
}

void DesktopNotifier::setRingInterval(int msec)
{
    m_ringTimer.setInterval(std::max(10, msec));
}

void DesktopNotifier::setClock(std::function<QDateTime()> clock)
{
    m_clock = std::move(clock);
}

QDateTime DesktopNotifier::now() const
{
    return m_clock ? m_clock() : QDateTime::currentDateTime();
}

int DesktopNotifier::ringingCount() const
{
    return int(std::count_if(m_shown.cbegin(), m_shown.cend(), [](const Shown &s) { return s.ringUntil.isValid(); }));
}

void DesktopNotifier::showNow(const ScheduledNotification &notification)
{
    if (!m_bus) {
        fallback(notification);
        return;
    }
    // The same reminder again (shown late after a re-plan): replace its notification.
    uint replaces = 0;
    for (auto it = m_shown.cbegin(); it != m_shown.cend(); ++it) {
        if (it->notification.id == notification.id)
            replaces = it.key();
    }
    const DesktopNotification message =
            buildDesktopNotification(notification, m_bus->capabilities(), m_snoozeMinutes, replaces,
                                     sound(notification.alarm ? SoundKind::Alarm : SoundKind::Reminder));
    const QPointer<DesktopNotifier> self(this);
    m_bus->notify(message, [self, notification](uint id) {
        if (!self)
            return;
        if (id == 0) {
            self->fallback(notification);
            return;
        }
        Shown shown{ notification, {} };
        if (notification.alarm && notification.ringMinutes > 0)
            shown.ringUntil = self->now().addSecs(qint64(notification.ringMinutes) * 60);
        self->m_shown.insert(id, shown);
        self->updateRingTimer();
    });
}

QList<SoundChoice::Mode> DesktopNotifier::soundModes() const
{
    return {SoundChoice::Mode::Default, SoundChoice::Mode::Custom, SoundChoice::Mode::Silent};
}

bool DesktopNotifier::supportsSoundFiles() const
{
    return !m_bus || m_bus->supportsSoundFiles();
}

QStringList DesktopNotifier::themeSounds() const
{
    return m_bus ? m_bus->themeSounds() : desktopThemeSounds();
}

bool DesktopNotifier::previewSound(SoundKind kind, const SoundChoice &choice)
{
    const QString unavailable = soundText(SoundText::NoService);
    if (!m_bus || !m_bus->isAvailable()) {
        Q_EMIT errorOccurred(unavailable);
        return false;
    }
    const QPointer<DesktopNotifier> self(this);
    m_bus->notify(buildSoundPreview(kind, choice, m_previewId), [self, unavailable](uint id) {
        if (!self)
            return;
        self->m_previewId = id;
        if (id == 0)
            Q_EMIT self->errorOccurred(unavailable);
    });
    return true;
}

void DesktopNotifier::fallback(const ScheduledNotification &notification)
{
    // No notification service (or it refused): the tray shows it, as plain text.
    const DesktopNotification message = buildDesktopNotification(notification, {}, m_snoozeMinutes);
    Q_EMIT trayMessageRequested(message.summary, message.body);
}

void DesktopNotifier::onActionInvoked(uint id, const QString &action)
{
    // Every program's notifications share the bus: only ours are in m_shown.
    const auto it = m_shown.constFind(id);
    if (it == m_shown.constEnd())
        return;
    const ScheduledNotification notification = it->notification;
    m_shown.remove(id);
    updateRingTimer();
    // Wayland: the window raised for this click must present the server's activation token.
    if (!m_activationToken.isEmpty())
        qputenv("XDG_ACTIVATION_TOKEN", std::exchange(m_activationToken, QByteArray()));
    // Resident alarms stay on screen after an action: close them (no-op for the others).
    if (m_bus)
        m_bus->close(id);
    if (action == u"snooze")
        Q_EMIT snoozeRequested(notification, m_snoozeMinutes);
    else if (action == u"default" || action == u"open")
        Q_EMIT activated(notification.path, notification.line);
}

void DesktopNotifier::onClosed(uint id, uint reason)
{
    Q_UNUSED(reason);
    if (m_shown.remove(id))
        updateRingTimer();
}

void DesktopNotifier::ring()
{
    if (!m_bus)
        return;
    const QDateTime current = now();
    const bool ringsItself = m_bus->ringsItself();
    QList<QPair<uint, ScheduledNotification>> again;
    for (auto it = m_shown.begin(); it != m_shown.end(); ++it) {
        if (!it->ringUntil.isValid())
            continue;
        if (current >= it->ringUntil) {
            it->ringUntil = QDateTime(); // rang long enough: it stays on screen, silently
            // A notification looping its own sound is replaced by a silent one.
            if (ringsItself)
                again.append({it.key(), it->notification});
        } else if (!ringsItself) {
            again.append({ it.key(), it->notification });
        }
    }
    const QStringList capabilities = m_bus->capabilities();
    const SoundChoice alarmSound =
            ringsItself ? SoundChoice{SoundChoice::Mode::Silent, {}, {}} : sound(SoundKind::Alarm);
    for (const auto &[id, notification] : std::as_const(again)) {
        const QPointer<DesktopNotifier> self(this);
        m_bus->notify(buildDesktopNotification(notification, capabilities, m_snoozeMinutes, id, alarmSound),
                      [self, id](uint newId) {
                          if (!self || newId == id)
                              return;
                          // Refused, or the server made a new one: stop ringing the old entry.
                          const auto it = self->m_shown.find(id);
                          if (it == self->m_shown.end())
                              return;
                          Shown shown = *it;
                          self->m_shown.erase(it);
                          if (newId != 0)
                              self->m_shown.insert(newId, shown);
                          self->updateRingTimer();
                      });
    }
    updateRingTimer();
}

void DesktopNotifier::updateRingTimer()
{
    if (ringingCount() > 0) {
        if (!m_ringTimer.isActive())
            m_ringTimer.start();
    } else {
        m_ringTimer.stop();
    }
}

} // namespace brulion::notify
