// ReminderScheduler (see scheduler.h).
#include "reminders/scheduler.h"

#include "index/vaultindex.h"
#include "reminders/reminders_p.h"
#include "vault/localstore.h"

#include <QHash>
#include <QJsonArray>
#include <QJsonObject>

#include <algorithm>
#include <chrono>
#include <cstdlib>

namespace brulion {

using namespace Qt::StringLiterals;
using namespace reminders_detail;

namespace {

// LocalStore keys (device-local, per vault).
QString keyLastSeen()
{
    return u"reminders/lastSeen"_s;
}
QString keyHandled()
{
    return u"reminders/handled"_s;
}
QString keyMissed()
{
    return u"reminders/missed"_s;
}

/// lastSeen is written at most this often while running (and always by stop()); a stale value
/// only widens the next catch-up window, which the handled ids make harmless.
constexpr qint64 LastSeenSaveSecs = 5 * 60;
constexpr int MaxHorizonDays = 3660;
constexpr int MaxScheduledLimit = 1000;

QJsonObject toJson(const ScheduledNotification &n)
{
    QJsonObject o{
            {u"id"_s, n.id},       {u"title"_s, n.title}, {u"body"_s, n.body}, {u"at"_s, formatStamp(n.at)},
            {u"alarm"_s, n.alarm}, {u"path"_s, n.path},   {u"line"_s, n.line},
    };
    if (n.taskLine >= 0)
        o.insert(u"taskLine"_s, n.taskLine);
    return o;
}

std::optional<ScheduledNotification> fromJson(const QJsonValue &value)
{
    const QJsonObject o = value.toObject();
    ScheduledNotification n;
    n.id = o.value(u"id"_s).toString();
    n.at = parseStamp(o.value(u"at"_s).toString());
    if (n.id.isEmpty() || !n.at.isValid())
        return std::nullopt;
    n.title = o.value(u"title"_s).toString();
    n.body = o.value(u"body"_s).toString();
    n.alarm = o.value(u"alarm"_s).toBool();
    n.path = o.value(u"path"_s).toString();
    n.line = std::max(0, o.value(u"line"_s).toInt());
    // Absent (not on a task, or stored by an older version): no "Done" in the missed banner.
    n.taskLine = std::max(-1, o.value(u"taskLine"_s).toInt(-1));
    return n;
}

bool byTime(const ScheduledNotification &a, const ScheduledNotification &b)
{
    return a.at < b.at;
}

} // namespace

ReminderScheduler::ReminderScheduler(VaultIndex *index, NotificationSink *sink, LocalStore *store, Clock clock,
                                     QObject *parent)
    : QObject(parent)
    , m_index(index)
    , m_sink(sink)
    , m_store(store)
    , m_clock(clock ? std::move(clock) : Clock([] { return QDateTime::currentDateTime(); }))
{
    Q_ASSERT(index);
    Q_ASSERT(sink);

    m_debounce.setSingleShot(true);
    m_debounce.setInterval(DefaultDebounceMs);
    connect(&m_debounce, &QTimer::timeout, this, &ReminderScheduler::refresh);

    m_fireTimer.setSingleShot(true);
    m_fireTimer.setTimerType(Qt::PreciseTimer); // coarse timers may be off by 5 %: minutes over hours
    connect(&m_fireTimer, &QTimer::timeout, this, &ReminderScheduler::onFireTimer);

    m_replanTimer.setInterval(ReplanIntervalMs);
    connect(&m_replanTimer, &QTimer::timeout, this, &ReminderScheduler::refresh);

    m_heartbeat.setInterval(DefaultHeartbeatMs);
    connect(&m_heartbeat, &QTimer::timeout, this, &ReminderScheduler::onHeartbeat);
}

ReminderScheduler::~ReminderScheduler()
{
    stop();
}

// ---- settings ---------------------------------------------------------------------------------

void ReminderScheduler::setHorizonDays(int days)
{
    days = std::clamp(days, 0, MaxHorizonDays);
    if (days == m_horizonDays)
        return;
    m_horizonDays = days;
    refresh();
}

int ReminderScheduler::horizonDays() const
{
    return m_horizonDays;
}

void ReminderScheduler::setMaxScheduled(int max)
{
    max = std::clamp(max, 1, MaxScheduledLimit);
    if (max == m_maxScheduled)
        return;
    m_maxScheduled = max;
    refresh();
}

int ReminderScheduler::maxScheduled() const
{
    return m_maxScheduled;
}

void ReminderScheduler::setAlarmRingMinutes(int minutes)
{
    minutes = std::clamp(minutes, 1, 60);
    if (minutes == m_ringMinutes)
        return;
    m_ringMinutes = minutes;
    refresh();
}

int ReminderScheduler::alarmRingMinutes() const
{
    return m_ringMinutes;
}

void ReminderScheduler::setLocale(dates::UiLocale locale)
{
    if (locale == m_locale)
        return;
    m_locale = locale;
    refresh();
}

dates::UiLocale ReminderScheduler::locale() const
{
    return m_locale;
}

void ReminderScheduler::setDebounceInterval(int msec)
{
    m_debounce.setInterval(std::max(0, msec));
}

void ReminderScheduler::setHeartbeatInterval(int msec)
{
    m_heartbeat.setInterval(std::max(1, msec));
}

// ---- state ------------------------------------------------------------------------------------

bool ReminderScheduler::isRunning() const
{
    return m_running;
}

QList<ScheduledNotification> ReminderScheduler::upcoming() const
{
    return m_upcoming;
}

int ReminderScheduler::droppedCount() const
{
    return m_dropped;
}

QDateTime ReminderScheduler::nextFireTime() const
{
    return m_nextFire;
}

QList<ScheduledNotification> ReminderScheduler::missed() const
{
    return m_missed;
}

void ReminderScheduler::dismissMissed(const QString &id)
{
    if (m_missed.removeIf([&id](const ScheduledNotification &n) { return n.id == id; }) == 0)
        return;
    saveMissed();
    Q_EMIT missedChanged();
}

void ReminderScheduler::clearMissed()
{
    if (m_missed.isEmpty())
        return;
    m_missed.clear();
    saveMissed();
    Q_EMIT missedChanged();
}

// ---- lifecycle --------------------------------------------------------------------------------

void ReminderScheduler::start()
{
    if (m_running || !m_index || !m_sink)
        return;
    m_running = true;
    m_inApp = !m_sink->firesWhenClosed();
    const QDateTime now = m_clock();
    loadState(now);
    m_indexConnection = connect(m_index, &VaultIndex::changed, this, &ReminderScheduler::scheduleRefresh);
    m_replanTimer.start();
    if (m_inApp) {
        resetHeartbeat(now);
        m_heartbeat.start();
    }
    m_forceSchedule = true; // the sink may still hold a set from the previous run
    refreshAt(now);
}

void ReminderScheduler::stop()
{
    if (!m_running)
        return;
    m_running = false;
    QObject::disconnect(m_indexConnection);
    m_debounce.stop();
    m_fireTimer.stop();
    m_replanTimer.stop();
    m_heartbeat.stop();
    m_nextFire = QDateTime();
    m_occurrences.clear();
    // The time of the last check (not "now"): reminders due after it are caught up next time.
    if (m_inApp && m_lastSeen.isValid())
        setLastSeen(m_lastSeen, true);
}

void ReminderScheduler::refresh()
{
    if (m_running)
        refreshAt(m_clock());
}

void ReminderScheduler::checkNow()
{
    if (!m_running || !m_inApp)
        return;
    const QDateTime now = m_clock();
    processDue(now);
    armTimer(now);
}

void ReminderScheduler::scheduleRefresh()
{
    if (m_running)
        m_debounce.start();
}

void ReminderScheduler::followDays(const QDateTime &now)
{
    // The look-back of processDue() and pruneMissed() (HistoryDays), the horizon, a day of slack
    // either side; local days.
    const QDate today = now.toLocalTime().date();
    m_occurrences.setDays(today.addDays(-HistoryDays - 1), today.addDays(m_horizonDays + 1));
}

void ReminderScheduler::refreshAt(const QDateTime &now)
{
    m_debounce.stop();
    if (!m_running || !m_index || !m_sink)
        return;
    followDays(now);
    int dropped = 0;
    QList<ScheduledNotification> next =
            m_occurrences.upcoming(*m_index, now, m_horizonDays, m_maxScheduled, &dropped, m_locale);
    applyRingMinutes(next);
    m_dropped = dropped;
    if (m_forceSchedule || next != m_upcoming) {
        m_forceSchedule = false;
        m_upcoming = std::move(next);
        m_sink->schedule(m_upcoming);
        Q_EMIT upcomingChanged();
    }
    if (!m_running || !m_inApp)
        return;
    pruneHandled(now);
    pruneMissed(now);
    processDue(now);
    armTimer(now);
}

// ---- in-app firing ----------------------------------------------------------------------------

bool ReminderScheduler::hasNotes() const
{
    return m_index && !m_index->paths().isEmpty();
}

bool ReminderScheduler::isTooLate(const ScheduledNotification &n, const QDateTime &now) const
{
    const qint64 late = n.at.secsTo(now);
    const qint64 limit = n.alarm ? qint64(m_ringMinutes) * 60 : qint64(LateLimitHours) * 3600;
    return late > limit;
}

void ReminderScheduler::applyRingMinutes(QList<ScheduledNotification> &list) const
{
    for (ScheduledNotification &n : list)
        n.ringMinutes = m_ringMinutes;
}

void ReminderScheduler::processDue(const QDateTime &now)
{
    // An empty index is a vault still loading: do not consume the catch-up window yet.
    if (!m_running || !m_inApp || !hasNotes())
        return;
    // Since the last check, but at least the last minute (a reminder just set for "now") and at
    // most HistoryDays (the handled ids do not reach further back).
    QDateTime from = now.addSecs(-60);
    if (m_lastSeen.isValid() && m_lastSeen < from)
        from = std::max(m_lastSeen, now.addDays(-HistoryDays));
    followDays(now);
    QList<ScheduledNotification> due = m_occurrences.between(*m_index, from, now, m_locale);
    applyRingMinutes(due);

    bool handledChanged = false;
    bool missedAdded = false;
    for (const ScheduledNotification &n : std::as_const(due)) {
        if (!m_running) // a slot connected to fired() stopped us
            break;
        if (m_handled.contains(n.id))
            continue;
        m_handled.insert(n.id, now);
        handledChanged = true;
        if (isTooLate(n, now)) {
            m_missed.append(n);
            missedAdded = true;
            continue;
        }
        m_sink->showNow(n);
        Q_EMIT fired(n);
    }
    if (handledChanged)
        saveHandled();
    if (missedAdded) {
        std::stable_sort(m_missed.begin(), m_missed.end(), byTime);
        if (m_missed.size() > MaxMissed)
            m_missed.remove(0, m_missed.size() - MaxMissed);
        assignNativeIds(m_missed);
        saveMissed();
        Q_EMIT missedChanged();
    }
    // Stopped half-way: keep the old lastSeen so the rest is caught up by the next start().
    if (m_running)
        setLastSeen(now, false);
}

void ReminderScheduler::armTimer(const QDateTime &now)
{
    m_fireTimer.stop();
    m_nextFire = QDateTime();
    if (!m_running || !m_inApp || !m_index)
        return;
    followDays(now);
    const QList<ScheduledNotification> soon =
            m_occurrences.between(*m_index, now, now.addSecs(qint64(TimerWindowHours) * 3600), m_locale);
    for (const ScheduledNotification &n : soon) {
        if (n.at > now && !m_handled.contains(n.id)) {
            m_nextFire = n.at;
            break;
        }
    }
    if (!m_nextFire.isValid())
        return;
    const qint64 msec = std::clamp<qint64>(now.msecsTo(m_nextFire), 0, qint64(TimerWindowHours) * 3600 * 1000);
    m_fireTimer.start(std::chrono::milliseconds(msec));
}

void ReminderScheduler::onFireTimer()
{
    // The timer may fire a little early or late against the wall clock: processDue() looks at
    // the clock, and armTimer() re-arms for whatever is still ahead.
    checkNow();
}

void ReminderScheduler::resetHeartbeat(const QDateTime &now)
{
    m_beatWall = now;
    m_beatClock.start();
}

void ReminderScheduler::onHeartbeat()
{
    if (!m_running)
        return;
    const QDateTime now = m_clock();
    const qint64 wall = m_beatWall.isValid() ? m_beatWall.msecsTo(now) : 0;
    const qint64 monotonic = m_beatClock.isValid() ? m_beatClock.elapsed() : 0;
    resetHeartbeat(now);
    // Suspend (the monotonic clock stops, timers are late) or a clock / time-zone change.
    if (std::abs(wall - monotonic) > ClockJumpToleranceMs) {
        refreshAt(now);
        return;
    }
    processDue(now);
    if (m_nextFire.isValid() && m_nextFire <= now)
        armTimer(now);
}

// ---- persistence ------------------------------------------------------------------------------

void ReminderScheduler::loadState(const QDateTime &now)
{
    // Without a store the state of the previous start() (in memory) is kept.
    if (m_store) {
        m_lastSeen = parseStamp(m_store->get(keyLastSeen()).toString());
        m_lastSeenSaved = m_lastSeen;

        m_handled.clear();
        const QJsonObject handled = m_store->get(keyHandled()).toObject();
        for (auto it = handled.constBegin(); it != handled.constEnd(); ++it) {
            const QDateTime when = parseStamp(it.value().toString());
            if (when.isValid())
                m_handled.insert(it.key(), when);
        }

        const QList<ScheduledNotification> before = m_missed;
        m_missed.clear();
        const QJsonArray missed = m_store->get(keyMissed()).toArray();
        for (const QJsonValue &v : missed) {
            if (auto n = fromJson(v))
                m_missed.append(std::move(*n));
        }
        std::stable_sort(m_missed.begin(), m_missed.end(), byTime);
        applyRingMinutes(m_missed);
        assignNativeIds(m_missed);
        if (m_missed != before)
            Q_EMIT missedChanged();
    }
    pruneHandled(now);
}

void ReminderScheduler::pruneHandled(const QDateTime &now)
{
    const QDateTime limit = now.addDays(-HistoryDays);
    using Iterator = QHash<QString, QDateTime>::iterator;
    qsizetype removed = m_handled.removeIf([&limit](const Iterator &it) { return it.value() < limit; });
    if (m_handled.size() > MaxHandled) {
        QList<QDateTime> times = m_handled.values();
        std::sort(times.begin(), times.end());
        const QDateTime cutoff = times.at(times.size() - MaxHandled);
        removed += m_handled.removeIf([&cutoff](const Iterator &it) { return it.value() < cutoff; });
    }
    if (removed > 0)
        saveHandled();
}

void ReminderScheduler::pruneMissed(const QDateTime &now)
{
    if (m_missed.isEmpty())
        return;
    const QDateTime limit = now.addDays(-HistoryDays);
    // Entries whose reminder is gone from the notes (deleted, edited, task done) — checked only
    // when the index holds notes (not while the vault is loading).
    // The task line of the ones still there follows the note (the banner's "Done").
    QHash<QString, int> alive; // id → taskLine
    const bool check = hasNotes();
    if (check) {
        const QDateTime from = m_missed.constFirst().at;
        const QDateTime to = std::max(m_missed.constLast().at, from);
        followDays(now);
        for (const ScheduledNotification &n : m_occurrences.between(*m_index, from, to, m_locale))
            alive.insert(n.id, n.taskLine);
    }
    const qsizetype removed = m_missed.removeIf(
            [&](const ScheduledNotification &n) { return n.at < limit || (check && !alive.contains(n.id)); });
    bool changed = removed > 0;
    if (check) {
        for (ScheduledNotification &n : m_missed) {
            const int taskLine = alive.value(n.id, n.taskLine);
            changed = changed || taskLine != n.taskLine;
            n.taskLine = taskLine;
        }
    }
    if (!changed)
        return;
    saveMissed();
    Q_EMIT missedChanged();
}

void ReminderScheduler::setLastSeen(const QDateTime &now, bool force)
{
    m_lastSeen = now;
    if (!m_store)
        return;
    const bool due =
            !m_lastSeenSaved.isValid() || now < m_lastSeenSaved || m_lastSeenSaved.secsTo(now) >= LastSeenSaveSecs;
    if (!force && !due)
        return;
    m_store->set(keyLastSeen(), formatStamp(now));
    m_lastSeenSaved = now;
}

void ReminderScheduler::saveHandled()
{
    if (!m_store)
        return;
    QJsonObject object;
    for (auto it = m_handled.constBegin(); it != m_handled.constEnd(); ++it)
        object.insert(it.key(), formatStamp(it.value()));
    m_store->set(keyHandled(), object);
}

void ReminderScheduler::saveMissed()
{
    if (!m_store)
        return;
    QJsonArray array;
    for (const ScheduledNotification &n : std::as_const(m_missed))
        array.append(toJson(n));
    m_store->set(keyMissed(), array);
}

} // namespace brulion
