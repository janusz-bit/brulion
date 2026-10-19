#include "workspacereminders.h"

#include "dates/dates.h"
#include "index/vaultindex.h"
#include "log/categories.h"
#include "reminders/reminders.h"
#include "vault/vault.h"

#include <QLoggingCategory>

#include <algorithm>
#include <optional>

using namespace Qt::StringLiterals;

namespace brulion::shell {

using logging::lcReminders;

namespace {

/// Id of the settings page's test notification (never an id of a real reminder: those are
/// 16 hex digits).
const QString kTestId = u"brulion-test"_s;
constexpr int kTestNativeId = 2'000'000'001;

} // namespace

/// Forwards to the notifier while it exists: the scheduler keeps one sink for its whole life,
/// the notifier may be set later or go away first.
class WorkspaceReminders::Proxy : public NotificationSink {
public:
    NotificationSink *target = nullptr;
    QPointer<QObject> lifetime;
    bool guarded = false;

    NotificationSink *live() const
    {
        if (guarded && !lifetime)
            return nullptr;
        return target;
    }
    bool firesWhenClosed() const override
    {
        const NotificationSink *t = live();
        return t && t->firesWhenClosed();
    }
    void schedule(const QList<ScheduledNotification> &notifications) override
    {
        if (NotificationSink *t = live())
            t->schedule(notifications);
    }
    void showNow(const ScheduledNotification &notification) override
    {
        if (NotificationSink *t = live())
            t->showNow(notification);
    }
};

WorkspaceReminders::WorkspaceReminders(Clock clock, QObject *parent)
    : QObject(parent), m_clock(std::move(clock)), m_proxy(std::make_unique<Proxy>())
{
}

WorkspaceReminders::~WorkspaceReminders()
{
    m_scheduler.reset(); // before the proxy it uses
}

QDateTime WorkspaceReminders::now() const
{
    return m_clock ? m_clock() : QDateTime::currentDateTime();
}

// ---- wiring -----------------------------------------------------------------------------------

void WorkspaceReminders::setSink(NotificationSink *sink, QObject *lifetime)
{
    m_proxy->target = sink;
    m_proxy->lifetime = lifetime;
    m_proxy->guarded = lifetime != nullptr;
    Q_EMIT sinkChanged();
    restart(); // firesWhenClosed() is read when the scheduler starts
}

void WorkspaceReminders::setSink(NotificationSink *sink)
{
    setSink(sink, nullptr);
}

NotificationSink *WorkspaceReminders::sink() const
{
    return m_proxy->live();
}

void WorkspaceReminders::setBackendInfo(std::function<QString()> backend, std::function<bool()> canShow)
{
    m_backend = std::move(backend);
    m_canShow = std::move(canShow);
    Q_EMIT sinkChanged();
}

void WorkspaceReminders::attach(Vault *vault, VaultIndex *index)
{
    detach();
    m_vault = vault;
    m_index = index;
}

void WorkspaceReminders::detach()
{
    m_wantRunning = false;
    stopScheduler();
    m_vault = nullptr;
    m_index = nullptr;
}

void WorkspaceReminders::start()
{
    m_wantRunning = true;
    if (!m_enabled || !m_index || m_scheduler)
        return;
    m_scheduler = std::make_unique<ReminderScheduler>(m_index.data(), m_proxy.get(), m_vault ? m_vault->store() : nullptr,
                                                      [this] { return now(); });
    m_scheduler->setAlarmRingMinutes(m_ringMinutes);
    m_scheduler->setLocale(m_language == u"en" ? dates::UiLocale::English : dates::UiLocale::Polish);
    if (m_testHeartbeatMs > 0)
        m_scheduler->setHeartbeatInterval(m_testHeartbeatMs);
    if (m_testDebounceMs > 0)
        m_scheduler->setDebounceInterval(m_testDebounceMs);
    connect(m_scheduler.get(), &ReminderScheduler::fired, this,
            [this](const ScheduledNotification &n) { Q_EMIT fired(toMap(n)); });
    connect(m_scheduler.get(), &ReminderScheduler::upcomingChanged, this, &WorkspaceReminders::upcomingChanged);
    connect(m_scheduler.get(), &ReminderScheduler::missedChanged, this, [this] {
        // "Show missed reminders" is off: forget them as soon as they are recorded.
        if (!m_notifyMissed && m_scheduler && !m_scheduler->missed().isEmpty()) {
            m_scheduler->clearMissed();
            return;
        }
        Q_EMIT missedChanged();
    });
    m_scheduler->start();
    if (!m_notifyMissed)
        m_scheduler->clearMissed();
    Q_EMIT runningChanged();
    Q_EMIT upcomingChanged();
    Q_EMIT missedChanged();
}

void WorkspaceReminders::stopScheduler()
{
    if (!m_scheduler)
        return;
    m_scheduler.reset(); // stops and persists; the platform keeps what it was given
    Q_EMIT runningChanged();
    Q_EMIT upcomingChanged();
    Q_EMIT missedChanged();
}

void WorkspaceReminders::restart()
{
    if (!m_scheduler)
        return;
    stopScheduler();
    if (m_wantRunning)
        start();
}

bool WorkspaceReminders::isRunning() const
{
    return m_scheduler && m_scheduler->isRunning();
}

void WorkspaceReminders::cancelAll()
{
    m_proxy->schedule({});
}

// ---- settings ---------------------------------------------------------------------------------

void WorkspaceReminders::setEnabled(bool on)
{
    if (m_enabled == on)
        return;
    m_enabled = on;
    if (!on) {
        stopScheduler();
        cancelAll(); // nothing may ring any more, also not Android alarms armed earlier
    } else if (m_wantRunning) {
        start();
    }
    Q_EMIT settingsChanged();
}

void WorkspaceReminders::setAlarmRingMinutes(int minutes)
{
    m_ringMinutes = std::clamp(minutes, 1, 60);
    if (m_scheduler)
        m_scheduler->setAlarmRingMinutes(m_ringMinutes);
}

void WorkspaceReminders::setSnoozeMinutes(int minutes)
{
    const int m = std::clamp(minutes, 1, 120);
    if (m == m_snoozeMinutes)
        return;
    m_snoozeMinutes = m;
    Q_EMIT settingsChanged();
}

void WorkspaceReminders::setNotifyMissed(bool on)
{
    if (m_notifyMissed == on)
        return;
    m_notifyMissed = on;
    if (!on && m_scheduler)
        m_scheduler->clearMissed();
    Q_EMIT missedChanged();
}

void WorkspaceReminders::setLanguage(const QString &language)
{
    const QString l = language == u"en" ? u"en"_s : u"pl"_s;
    if (l == m_language)
        return;
    m_language = l;
    if (m_scheduler)
        m_scheduler->setLocale(l == u"en" ? dates::UiLocale::English : dates::UiLocale::Polish);
    Q_EMIT upcomingChanged();
    Q_EMIT missedChanged();
}

void WorkspaceReminders::setTestIntervals(int heartbeatMs, int debounceMs)
{
    m_testHeartbeatMs = heartbeatMs;
    m_testDebounceMs = debounceMs;
    if (m_scheduler) {
        if (heartbeatMs > 0)
            m_scheduler->setHeartbeatInterval(heartbeatMs);
        if (debounceMs > 0)
            m_scheduler->setDebounceInterval(debounceMs);
    }
}

// ---- QML ----------------------------------------------------------------------------------------

QVariantMap WorkspaceReminders::toMap(const ScheduledNotification &n) const
{
    const dates::UiLocale locale = m_language == u"en" ? dates::UiLocale::English : dates::UiLocale::Polish;
    const QDateTime at = n.at.toLocalTime();
    DateSpec spec{ at.date(), at.time() };
    return {
        { u"id"_s, n.id },
        { u"title"_s, n.title },
        { u"body"_s, n.body },
        { u"at"_s, at.toString(u"yyyy-MM-dd HH:mm"_s) },
        { u"when"_s, at.isValid() ? dates::formatHuman(spec, locale, now().date()) : QString() },
        { u"alarm"_s, n.alarm },
        { u"path"_s, n.path },
        { u"line"_s, n.line },
    };
}

QVariantList WorkspaceReminders::missed() const
{
    QVariantList out;
    if (!m_scheduler || !m_notifyMissed)
        return out;
    for (const ScheduledNotification &n : m_scheduler->missed())
        out.append(toMap(n));
    return out;
}

int WorkspaceReminders::missedCount() const
{
    return m_scheduler && m_notifyMissed ? int(m_scheduler->missed().size()) : 0;
}

QVariantList WorkspaceReminders::upcoming() const
{
    QVariantList out;
    if (!m_scheduler)
        return out;
    const QDateTime current = now();
    for (const ScheduledNotification &n : m_scheduler->upcoming()) {
        if (n.at < current.addSecs(-60))
            continue;
        out.append(toMap(n));
        if (out.size() >= MaxUpcoming)
            break;
    }
    return out;
}

QVariantMap WorkspaceReminders::next() const
{
    const QVariantList list = upcoming();
    return list.isEmpty() ? QVariantMap() : list.first().toMap();
}

QString WorkspaceReminders::backend() const
{
    if (m_backend)
        return m_backend();
    return sink() ? u"custom"_s : QString();
}

bool WorkspaceReminders::firesWhenClosed() const
{
    return m_proxy->firesWhenClosed();
}

bool WorkspaceReminders::canShow() const
{
    if (!sink())
        return false;
    return m_canShow ? m_canShow() : true;
}

void WorkspaceReminders::dismissMissed(const QString &id)
{
    if (m_scheduler)
        m_scheduler->dismissMissed(id);
}

void WorkspaceReminders::clearMissed()
{
    if (m_scheduler)
        m_scheduler->clearMissed();
}

void WorkspaceReminders::openMissed(const QString &id)
{
    const std::optional<ScheduledNotification> n = find(id);
    if (!n)
        return;
    dismissMissed(id);
    Q_EMIT navigationRequested({ { u"view"_s, u"page"_s }, { u"path"_s, n->path }, { u"line"_s, n->line } });
}

std::optional<ScheduledNotification> WorkspaceReminders::find(const QString &id) const
{
    if (!m_scheduler || id.isEmpty())
        return std::nullopt;
    const QList<ScheduledNotification> missed = m_scheduler->missed();
    for (const ScheduledNotification &n : missed) {
        if (n.id == id)
            return n;
    }
    const QList<ScheduledNotification> upcoming = m_scheduler->upcoming();
    for (const ScheduledNotification &n : upcoming) {
        if (n.id == id)
            return n;
    }
    return std::nullopt;
}

bool WorkspaceReminders::snooze(const QString &id, int minutes)
{
    const std::optional<ScheduledNotification> n = find(id);
    return n && snoozeNotification(*n, minutes);
}

bool WorkspaceReminders::snoozeNotification(const ScheduledNotification &notification, int minutes)
{
    if (!m_vault || !m_index || notification.path.isEmpty())
        return false;
    const int length = minutes > 0 ? std::min(minutes, 24 * 60) : m_snoozeMinutes;
    // The note may have changed since the notification was made: find the occurrence again
    // (same id = same text and time) for its current line. Not found: the reminder was edited,
    // moved to another time, completed or deleted meanwhile — its old line may now hold another
    // reminder, which must not be moved instead.
    ScheduledNotification target = notification;
    if (notification.at.isValid()) {
        bool found = false;
        for (const ScheduledNotification &n : notificationsBetween(*m_index, notification.at, notification.at)) {
            if (n.id == notification.id) {
                target = n;
                found = true;
                break;
            }
        }
        if (!found) {
            qCInfo(lcReminders) << "Not snoozed: the reminder changed in" << notification.path;
            Q_EMIT errorOccurred(tr("“%1” has changed in the note meanwhile and was not snoozed.").arg(notification.title));
            return false;
        }
    }
    bool ok = false;
    const QString content = m_vault->readText(target.path, &ok);
    if (!ok)
        return false;
    QDateTime when = now().addSecs(qint64(length) * 60);
    when.setTime(QTime(when.time().hour(), when.time().minute()));
    const QString edited = snoozeReminder(content, target.line, when);
    if (edited == content) {
        qCInfo(lcReminders) << "Nothing to snooze at" << target.path << target.line;
        return false;
    }
    if (!m_vault->writeText(target.path, edited, ChangeSource::User)) {
        Q_EMIT errorOccurred(tr("Could not snooze the reminder: %1").arg(m_vault->errorString()));
        return false;
    }
    if (m_scheduler)
        m_scheduler->dismissMissed(notification.id);
    return true;
}

bool WorkspaceReminders::sendTestNotification(const QString &title, const QString &body)
{
    NotificationSink *target = sink();
    if (!target)
        return false;
    ScheduledNotification n;
    n.id = kTestId;
    n.nativeId = kTestNativeId;
    n.title = title;
    n.body = body;
    n.at = now();
    target->showNow(n);
    return true;
}

} // namespace brulion::shell
