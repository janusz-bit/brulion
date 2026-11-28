#include "workspacecalendars.h"

#include "caldav/caldav.h"
#include "reminders/scheduler.h"
#include "dates/format.h"
#include "dates/recurrence.h"
#include "index/vaultindex.h"
#include "log/categories.h"
#include "sync/http.h"
#include "sync/scheduler.h"
#include "util/paths.h"
#include "vault/vault.h"
#include "workspacegithub.h"

#include <QDir>
#include <QFileInfo>
#include <QFileSystemWatcher>
#include <QUrl>

#include <algorithm>

namespace brulion::shell {

using namespace Qt::StringLiterals;
using namespace std::chrono_literals;

namespace {

/// [from, to] is a range of valid times.
bool isRange(const QDateTime &from, const QDateTime &to)
{
    return from.isValid() && to.isValid() && from <= to;
}

/// The calendar `c` holds `component` ("VEVENT", "VTODO"; a calendar that names none holds all).
bool holds(const caldav::CalendarEntry &c, const QString &component)
{
    return c.components.isEmpty() || c.components.contains(component);
}

/// The addresses invitations name the account of calendar `id` by (none for a subscription).
QStringList addressesOf(const caldav::CalendarSettings &s, const QString &id)
{
    const caldav::CalendarEntry *c = s.calendar(id);
    const caldav::Account *a = c ? s.account(c->account) : nullptr;
    return a ? a->addresses : QStringList();
}

const QString &answerName(caldav::Participation p)
{
    static const QString names[] = {u"needs-action"_s, u"accepted"_s, u"declined"_s, u"tentative"_s, u"delegated"_s};
    return names[int(p)];
}

/// "accepted", "declined", "tentative" as an answer (nullopt otherwise).
std::optional<caldav::Participation> answerOf(const QString &name)
{
    for (const caldav::Participation p :
         {caldav::Participation::Accepted, caldav::Participation::Declined, caldav::Participation::Tentative}) {
        if (answerName(p) == name)
            return p;
    }
    return std::nullopt;
}

/// The invitation of `e` as the event dialog shows it: organizer (its name, else its address),
/// attendees [{name, address, answer, role, me}], myAnswer ("" when the account is not invited)
/// and canAnswer (invited, the event writable).
QVariantMap invitationDetails(const caldav::Event &e, const QStringList &mine, bool writable)
{
    QVariantList people;
    QString myAnswer;
    for (const caldav::Participant &a : e.attendees) {
        const bool me = mine.contains(a.address);
        if (me)
            myAnswer = answerName(a.status);
        people.append(QVariantMap{{u"name"_s, a.name.isEmpty() ? a.address : a.name},
                                  {u"address"_s, a.address},
                                  {u"answer"_s, answerName(a.status)},
                                  {u"role"_s, a.role},
                                  {u"me"_s, me}});
    }
    const caldav::Participant &o = e.organizer;
    return QVariantMap{{u"organizer"_s, o.name.isEmpty() ? o.address : o.name},
                       {u"attendees"_s, people},
                       {u"myAnswer"_s, myAnswer},
                       {u"canAnswer"_s, writable && !myAnswer.isEmpty()}};
}

/// The plain fields of `values` (QML's names) written into `sub`: name, color, refreshMinutes,
/// alarms — not its address.
void applySubscriptionValues(caldav::Subscription &sub, const QVariantMap &values)
{
    if (values.contains(u"name"_s))
        sub.name = values.value(u"name"_s).toString().simplified();
    if (const QString c = caldav::normalizedColor(values.value(u"color"_s).toString()); !c.isEmpty())
        sub.color = c;
    if (values.contains(u"refreshMinutes"_s))
        sub.refreshMinutes = std::clamp(values.value(u"refreshMinutes"_s).toInt(), 15, 10'080);
    if (values.contains(u"alarms"_s))
        sub.alarms = values.value(u"alarms"_s).toBool();
}

constexpr QChar KeySep = QChar(0x1f);

QString isoDate(QDate d)
{
    return d.isValid() ? d.toString(u"yyyy-MM-dd"_s) : QString();
}

QDate parseDate(const QString &s)
{
    return QDate::fromString(s.left(10), u"yyyy-MM-dd"_s);
}

std::optional<QTime> parseTime(const QString &s)
{
    const QTime t = QTime::fromString(s.trimmed(), u"HH:mm"_s);
    if (!t.isValid())
        return std::nullopt;
    return t;
}

dates::UiLocale localeOf(const QString &language)
{
    return language == u"pl" ? dates::UiLocale::Polish : dates::UiLocale::English;
}

QString instanceText(const std::optional<io::ical::DateTimeValue> &v)
{
    return v ? caldav::instanceText(*v) : QString();
}

/// The calendar an event is in, as the event dialog names it.
struct Owner {
    QString name;
    QString color;
    bool readOnly = true; ///< a subscription, a calendar the server does not let us write, none
};

Owner ownerOf(const caldav::CalendarSettings &s, const QString &id)
{
    if (const caldav::Subscription *sub = s.subscription(id))
        return {sub->name, sub->color, true};
    if (const caldav::CalendarEntry *cal = s.calendar(id))
        return {cal->name, cal->color, cal->readOnly};
    return {};
}

/// The times the event dialog shows, as an edit gives them: its days and (timed) its times.
caldav::EventTimes dialogTimes(const QVariantMap &values)
{
    caldav::EventTimes t;
    const bool allDay = values.value(u"allDay"_s).toBool();
    t.allDay = allDay;
    DateSpec start{parseDate(values.value(u"startDate"_s).toString()), std::nullopt};
    DateSpec end{parseDate(values.value(u"endDate"_s).toString()), std::nullopt};
    if (!allDay) {
        start.time = parseTime(values.value(u"startTime"_s).toString()).value_or(QTime(9, 0));
        end.time = parseTime(values.value(u"endTime"_s).toString());
    }
    if (start.isValid())
        t.start = start;
    if (end.isValid() && end.date >= start.date) // (else its length: an hour, its one day)
        t.end = end;
    return t;
}

/// Whether the dialog's times differ from what it showed (a timed event's times too).
bool timesChanged(const QVariantMap &values, const QVariantMap &before)
{
    const bool allDay = values.value(u"allDay"_s).toBool();
    QStringList keys{u"startDate"_s, u"endDate"_s};
    if (!allDay)
        keys << u"startTime"_s << u"endTime"_s;
    const bool moved = std::ranges::any_of(
            keys, [&](const QString &k) { return values.value(k).toString() != before.value(k).toString(); });
    return moved || allDay != before.value(u"allDay"_s).toBool();
}

/// What the event dialog changed: each field that differs from what it showed (`before`; empty for
/// a new event: all of them). One occurrence has no rule of its own.
bool fieldChanged(const QVariantMap &values, const QVariantMap &before, const QString &key)
{
    return before.isEmpty() || values.value(key).toString() != before.value(key).toString();
}

caldav::EventPatch dialogPatch(const QVariantMap &values, const QVariantMap &before, bool oneOccurrence)
{
    caldav::EventPatch p;
    const auto changed = [&values, &before](const QString &key) { return fieldChanged(values, before, key); };
    for (const auto &[key, field] : {std::pair{u"location"_s, &caldav::EventPatch::location},
                                     std::pair{u"description"_s, &caldav::EventPatch::description}}) {
        if (changed(key))
            p.*field = values.value(key).toString();
    }
    if (changed(u"title"_s))
        p.title = values.value(u"title"_s).toString().simplified();
    if (before.isEmpty() || timesChanged(values, before))
        p.times = dialogTimes(values);
    if (changed(u"rule"_s) && !oneOccurrence)
        p.rule = dates::canonicalRecurrence(values.value(u"rule"_s).toString());
    if (changed(u"reminderMinutes"_s))
        p.reminderMinutes = values.value(u"reminderMinutes"_s, -1).toInt();
    return p;
}

} // namespace

QStringList WorkspaceCalendars::palette()
{
    return {u"#1e88e5"_s, u"#43a047"_s, u"#e53935"_s, u"#fb8c00"_s, u"#8e24aa"_s,
            u"#00897b"_s, u"#6d4c41"_s, u"#3949ab"_s, u"#d81b60"_s, u"#546e7a"_s};
}

WorkspaceCalendars::WorkspaceCalendars(QObject *parent) : planning::EventSource(parent)
{
#if QT_CONFIG(timezone)
    m_zone = QTimeZone::systemTimeZone();
#else
    m_zone = QTimeZone(QTimeZone::LocalTime); // (no database: the browser's own local time)
#endif
    m_eventsTimer.setSingleShot(true);
    m_eventsTimer.setInterval(0);
    connect(&m_eventsTimer, &QTimer::timeout, this, [this] {
        ++m_revision;
        Q_EMIT eventsChanged();
    });
    m_inboxTimer.setSingleShot(true);
    m_inboxTimer.setInterval(100);
    connect(&m_inboxTimer, &QTimer::timeout, this, &WorkspaceCalendars::takeInbox);
    m_startTimer.setSingleShot(true);
    m_startTimer.setInterval(1500);
    connect(&m_startTimer, &QTimer::timeout, this, [this] {
        if (m_scheduler && m_automatic)
            m_scheduler->start();
    });
}

WorkspaceCalendars::~WorkspaceCalendars()
{
    teardown();
}

void WorkspaceCalendars::setTransports(sync::HttpTransport *caldav, sync::HttpTransport *feeds)
{
    m_caldavTransport = caldav;
    m_feedTransport = feeds;
    if (m_sync)
        m_sync->setTransports(caldav, feeds);
}

void WorkspaceCalendars::setSecretsFolder(const QString &folder)
{
    m_secrets = folder;
}

void WorkspaceCalendars::setClock(std::function<QDateTime()> clock)
{
    m_clock = std::move(clock);
    if (m_sync)
        m_sync->setClock(m_clock);
}

void WorkspaceCalendars::setZone(const QTimeZone &zone)
{
    m_zone = zone;
    if (m_sync)
        m_sync->setZone(zone);
    m_data.clear();
    scheduleEventsChanged();
}

void WorkspaceCalendars::setLanguage(const QString &language)
{
    if (m_language == language)
        return;
    m_language = language;
    Q_EMIT configurationChanged();
    Q_EMIT statusChanged();
    Q_EMIT logChanged();
    scheduleEventsChanged(); // "(no title)" in the new language
}

void WorkspaceCalendars::setBrowser(bool browser)
{
    m_browser = browser;
    if (m_sync)
        m_sync->setBrowser(browser);
}

void WorkspaceCalendars::setAutomatic(bool on)
{
    m_automatic = on;
    if (!on && m_scheduler)
        m_scheduler->stop();
}

void WorkspaceCalendars::setOnlineCheck(std::function<bool()> isOnline)
{
    m_onlineCheck = std::move(isOnline);
}

void WorkspaceCalendars::setTaskFolder(const QString &folder)
{
    m_taskFolder = folder;
}

QDateTime WorkspaceCalendars::now() const
{
    return m_clock ? m_clock() : QDateTime::currentDateTime();
}

QString WorkspaceCalendars::passwordFile(const QString &accountId) const
{
    QString safe;
    for (const QChar c : accountId) {
        if (c.isLetterOrNumber() || c == u'-')
            safe += c;
    }
    return m_secrets.isEmpty() ? QString() : m_secrets + u"/caldav-"_s + safe;
}

QString WorkspaceCalendars::password(const QString &accountId) const
{
    const QString file = passwordFile(accountId);
    return file.isEmpty() ? QString() : readSecretFile(file);
}

// ---- attach / detach -------------------------------------------------------------------------------

void WorkspaceCalendars::attach(Vault *vault, VaultIndex *index)
{
    teardown();
    m_vault = vault;
    m_index = index;
    rebuild();
}

void WorkspaceCalendars::detach()
{
    teardown();
    m_vault = nullptr;
    m_index = nullptr;
    Q_EMIT configurationChanged();
    Q_EMIT statusChanged();
    Q_EMIT logChanged();
    scheduleEventsChanged();
}

void WorkspaceCalendars::teardown()
{
    m_startTimer.stop();
    m_inboxTimer.stop();
    m_inboxWatcher.reset();
    m_scheduler.reset();
    m_sync.reset();
    m_store.reset();
    m_data.clear();
    m_trackedBlocks.clear();
    m_taskPages.clear();
    m_needsConfirmation = false;
    m_pending.clear();
}

void WorkspaceCalendars::rebuild()
{
    if (!m_vault)
        return;
    const QString state = m_vault->deviceStatePath();
    m_store = std::make_unique<caldav::CalendarStore>(state.isEmpty() ? QString() : state + u"/calendars"_s);
    m_sync = std::make_unique<caldav::CalendarSync>(m_store.get());
    m_sync->setVault(m_vault.data(), m_index.data());
    if (!m_caldavTransport && !m_ownCaldav)
        m_ownCaldav = std::make_unique<sync::NetworkHttpTransport>();
    if (!m_feedTransport && !m_ownFeeds)
        m_ownFeeds = std::make_unique<caldav::FeedHttpTransport>();
    m_sync->setTransports(m_caldavTransport ? m_caldavTransport : m_ownCaldav.get(),
                          m_feedTransport ? m_feedTransport : m_ownFeeds.get());
    m_sync->setPasswords([this](const QString &id) { return password(id); });
    m_sync->setZone(m_zone);
    m_sync->setBrowser(m_browser);
    if (m_clock)
        m_sync->setClock(m_clock);
    connect(m_sync.get(), &caldav::CalendarSync::calendarChanged, this, &WorkspaceCalendars::invalidate);
    connect(m_sync.get(), &caldav::CalendarSync::logChanged, this, &WorkspaceCalendars::logChanged);
    connect(m_sync.get(), &caldav::CalendarSync::runningChanged, this, [this] {
        if (!m_sync->isRunning())
            onRunFinished();
        Q_EMIT statusChanged();
    });
    m_scheduler = std::make_unique<sync::SyncScheduler>(m_sync.get());
    if (m_onlineCheck)
        m_scheduler->setOnlineCheck(m_onlineCheck);
    else
        m_scheduler->useSystemNetworkInformation();
    // Feeds are slow to change and big: a failed run waits longer before it is tried again.
    m_scheduler->setRetryBase(60'000);
    m_scheduler->setActiveSyncGap(5 * 60'000);
    connect(m_scheduler.get(), &sync::SyncScheduler::statusChanged, this, &WorkspaceCalendars::statusChanged);
    connect(m_sync.get(), &caldav::CalendarSync::localChange, m_scheduler.get(), &sync::SyncScheduler::notifyLocalChange);
    configure();
    refreshTracked();
    Q_EMIT configurationChanged();
    Q_EMIT statusChanged();
    Q_EMIT logChanged();
    scheduleEventsChanged();
}

void WorkspaceCalendars::configure()
{
    if (!m_scheduler)
        return;
    const caldav::CalendarSettings s = m_store->settings();
    int minutes = 60;
    for (const caldav::Account &a : s.accounts)
        minutes = std::min(minutes, a.intervalMinutes);
    for (const caldav::Subscription &sub : s.subscriptions)
        minutes = std::min(minutes, sub.refreshMinutes);
    m_scheduler->setInterval(qint64(std::max(5, minutes)) * 60'000);
    refreshTaskPages(s);
    const bool any = !s.subscriptions.isEmpty() || !s.calendars.isEmpty();
    if (!any) {
        m_scheduler->stop();
    } else if (m_automatic && !m_scheduler->isActive()) {
        m_startTimer.start();
    }
    watchInbox();
}

void WorkspaceCalendars::refreshTaskPages(const caldav::CalendarSettings &s)
{
    m_taskPages.clear();
    for (const caldav::CalendarEntry &c : s.calendars) {
        if (c.syncTasks && !c.taskPage.isEmpty())
            m_taskPages.insert(c.taskPage);
    }
}

void WorkspaceCalendars::watchInbox()
{
    const caldav::CalendarSettings s = m_store ? m_store->settings() : caldav::CalendarSettings();
    if (!m_store || m_store->folder().isEmpty() || m_browser || s.calendars.isEmpty()) {
        m_inboxWatcher.reset();
        return;
    }
    if (m_inboxWatcher)
        return;
    const QString inbox = caldav::inboxFolder(m_store->folder());
    QDir().mkpath(inbox); // (if it cannot be made, nothing is watched: a run still takes the inbox)
    QFile::setPermissions(inbox, QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner);
    m_inboxWatcher = std::make_unique<QFileSystemWatcher>(QStringList{inbox});
    connect(m_inboxWatcher.get(), &QFileSystemWatcher::directoryChanged, &m_inboxTimer, qOverload<>(&QTimer::start));
    m_inboxTimer.start(); // what was left while the app was not watching
}

void WorkspaceCalendars::takeInbox()
{
    if (m_sync && m_sync->takeInbox() > 0 && m_scheduler)
        m_scheduler->syncNow();
}

void WorkspaceCalendars::setAppActive(bool active)
{
    m_appActive = active;
    if (m_scheduler && m_scheduler->isActive())
        m_scheduler->setAppActive(active);
}

void WorkspaceCalendars::refreshTracked()
{
    m_trackedBlocks.clear();
    if (!m_store)
        return;
    for (const caldav::CalendarEntry &c : m_store->settings().calendars) {
        if (!c.syncTasks)
            continue;
        for (const caldav::TrackedTask &t : m_store->state(c.id).tasks)
            m_trackedBlocks.insert(t.blockId);
    }
}

void WorkspaceCalendars::notifyChanges(const QList<VaultChange> &changes)
{
    if (!m_scheduler || !m_scheduler->isActive() || (m_taskPages.isEmpty() && m_trackedBlocks.isEmpty()))
        return;
    for (const VaultChange &c : changes) {
        if (c.source == ChangeSource::Sync)
            continue;
        bool relevant = m_taskPages.contains(c.path) || m_taskPages.contains(c.oldPath);
        if (!relevant && m_index) {
            if (const ParsedNote *note = m_index->note(c.path)) {
                for (auto it = note->blockIds.constBegin(); it != note->blockIds.constEnd() && !relevant; ++it)
                    relevant = m_trackedBlocks.contains(it.key());
            } else if (c.type == ChangeType::Deleted) {
                relevant = !m_trackedBlocks.isEmpty(); // a note with synced tasks may be gone
            }
        }
        if (relevant) {
            m_scheduler->notifyLocalChange();
            return;
        }
    }
}

void WorkspaceCalendars::onRunFinished()
{
    const caldav::CalendarRunReport report = m_sync->lastReport();
    m_lastRun = report.finished;
    m_lastError = report.error.isError() ? sync::errorCodeName(report.error.code) : QString();
    m_needsConfirmation = report.needsConfirmation;
    m_pending = report.pendingDeletions;
    // The store was read again at the run's start (another process may have synced): what is
    // shown follows it.
    m_data.clear();
    scheduleEventsChanged();
    refreshTracked();
    if (!report.conflicts.isEmpty())
        Q_EMIT conflictsFound(report.conflicts);
    if (report.needsConfirmation)
        Q_EMIT deletionsHeld(int(report.pendingDeletions.size()));
    Q_EMIT configurationChanged(); // statuses of the rows
    Q_EMIT statusChanged();
}

void WorkspaceCalendars::invalidate(const QString &id)
{
    m_data.remove(id);
    scheduleEventsChanged();
    Q_EMIT configurationChanged();
}

void WorkspaceCalendars::scheduleEventsChanged()
{
    m_alarmCacheDay = QDate();
    m_alarmCache.clear();
    m_cacheFrom = QDate();
    m_cache.clear();
    m_eventsTimer.start();
}

// ---- events -------------------------------------------------------------------------------------

std::shared_ptr<const caldav::CalendarData> WorkspaceCalendars::dataOf(const QString &id) const
{
    if (!m_store)
        return {};
    if (const auto it = m_data.constFind(id); it != m_data.constEnd())
        return *it;
    auto data = std::make_shared<caldav::CalendarData>();
    const caldav::CalendarSettings s = m_store->settings();
    if (s.subscription(id)) {
        caldav::appendCalendarData(*data, m_store->feedData(id));
    } else if (s.calendar(id)) {
        *data = caldav::eventsOf(m_store->state(id));
    }
    m_data.insert(id, data);
    return data;
}

QList<ScheduledNotification> WorkspaceCalendars::alarmsWithin(const QDateTime &from, const QDateTime &to) const
{
    QList<ScheduledNotification> out;
    const dates::UiLocale locale = m_language == u"pl" ? dates::UiLocale::Polish : dates::UiLocale::English;
    const caldav::CalendarSettings s = m_store->settings();
    // (dataOf() always has data while there is a store.)
    const auto add = [&](const QString &id, const QString &name) {
        out += caldav::eventNotifications(*dataOf(id), id, name, from, to, m_zone, locale);
    };
    for (const caldav::CalendarEntry &c : s.calendars) {
        if (c.showEvents && c.alarms)
            add(c.id, c.name);
    }
    for (const caldav::Subscription &sub : s.subscriptions) {
        if (sub.alarms)
            add(sub.id, sub.name);
    }
    std::ranges::stable_sort(out, [](const ScheduledNotification &x, const ScheduledNotification &y) {
        return x.at != y.at ? x.at < y.at : x.id < y.id;
    });
    return out;
}

QList<ScheduledNotification> WorkspaceCalendars::eventAlarmsBetween(const QDateTime &from, const QDateTime &to) const
{
    if (!m_store || !isRange(from, to))
        return {};
    // The window the reminder scheduler asks about (a day's look-back, its horizon, a day of
    // slack) is computed once a day and after every change of the events; other windows directly.
    const QDateTime now = m_clock ? m_clock() : QDateTime::currentDateTime();
    const QDate today = now.date();
    const QDateTime windowFrom(today.addDays(-9), QTime(0, 0));
    const QDateTime windowTo(today.addDays(ReminderScheduler::DefaultHorizonDays + 2), QTime(23, 59));
    if (from < windowFrom || to > windowTo)
        return alarmsWithin(from, to);
    if (m_alarmCacheDay != today) {
        m_alarmCache = alarmsWithin(windowFrom, windowTo);
        m_alarmCacheDay = today;
    }
    QList<ScheduledNotification> out = m_alarmCache;
    out.removeIf([&](const ScheduledNotification &n) { return n.at < from || n.at > to; });
    return out;
}

QList<planning::ExternalEvent> WorkspaceCalendars::eventsBetween(QDate from, QDate to) const
{
    QList<planning::ExternalEvent> out;
    if (!m_store || !from.isValid() || !to.isValid() || from > to || from.daysTo(to) > 800)
        return out;
    if (m_cacheFrom.isValid() && from >= m_cacheFrom && to <= m_cacheTo) {
        for (const planning::ExternalEvent &e : std::as_const(m_cache)) {
            if (e.date >= from && e.date <= to)
                out.append(e);
        }
        return out;
    }
    const caldav::CalendarSettings s = m_store->settings();
    struct Source {
        QString id;
        QString name;
        QString color;
        bool readOnly;
    };
    QList<Source> sources;
    for (const caldav::Subscription &sub : s.subscriptions) {
        if (sub.visible)
            sources.append({sub.id, sub.name, sub.color, true});
    }
    for (const caldav::CalendarEntry &c : s.calendars) {
        if (c.visible && c.showEvents)
            sources.append({c.id, c.name, c.color, c.readOnly});
    }
    for (const Source &src : std::as_const(sources)) {
        const auto data = dataOf(src.id);
        if (!data || data->events.isEmpty())
            continue;
        const QList<caldav::Occurrence> list = caldav::occurrences(*data, from, to, m_zone);
        for (const caldav::Occurrence &o : list) {
            const caldav::Event &e = data->events.at(o.event);
            planning::ExternalEvent x;
            x.key = src.id + KeySep + e.uid + KeySep + instanceText(o.instance);
            x.calendarId = src.id;
            x.calendarName = src.name;
            x.color = !e.color.isEmpty() ? e.color : !src.color.isEmpty() ? src.color : palette().first();
            x.title = e.summary.isEmpty() ? tr("(no title)") : e.summary;
            x.location = e.location;
            x.firstDay = o.start.date;
            x.lastDay = o.lastDay();
            x.allDay = o.allDay;
            x.start = o.allDay ? QTime() : o.start.time;
            x.end = o.allDay || o.end.time == QTime(0, 0) ? QTime() : o.end.time;
            x.recurring = o.recurring;
            x.readOnly = src.readOnly;
            const QDate first = std::max(from, x.firstDay);
            const QDate last = std::min(to, x.lastDay);
            for (QDate d = first; d.isValid() && d <= last; d = d.addDays(1)) {
                x.date = d;
                out.append(x);
            }
        }
    }
    std::ranges::stable_sort(out, [](const planning::ExternalEvent &a, const planning::ExternalEvent &b) {
        if (a.date != b.date)
            return a.date < b.date;
        const bool at = !a.allDay && a.date == a.firstDay;
        const bool bt = !b.allDay && b.date == b.firstDay;
        if (at != bt)
            return !at; // all-day (and continued) first
        return at && a.start < b.start;
    });
    m_cacheFrom = from;
    m_cacheTo = to;
    m_cache = out;
    return out;
}

QVariantMap WorkspaceCalendars::eventDetails(const QString &key) const
{
    const QStringList parts = key.split(KeySep);
    if (parts.size() != 3 || !m_store)
        return {};
    const QString id = parts.at(0);
    const auto data = dataOf(id);
    if (!data)
        return {};
    // The occurrence on the device's clock: the override of that instance, else the series'.
    const auto fields = caldav::eventFields(*data, parts.at(1), caldav::instanceFromText(parts.at(2)), m_zone);
    if (!fields)
        return {};
    const caldav::Event &e = data->events.at(fields->event);
    const caldav::EventEdit &f = fields->edit;
    const Owner owner = ownerOf(m_store->settings(), id);
    QVariantMap details{
            {u"key"_s, key},
            {u"calendarId"_s, id},
            {u"calendarName"_s, owner.name},
            {u"color"_s, !e.color.isEmpty() ? e.color : owner.color},
            {u"title"_s, f.title},
            {u"location"_s, f.location},
            {u"description"_s, f.description},
            {u"url"_s, e.url},
            {u"startDate"_s, isoDate(f.startDate)},
            {u"startTime"_s, f.allDay() ? QString() : f.startTime->toString(u"HH:mm"_s)},
            {u"endDate"_s, isoDate(f.endDate)},
            {u"endTime"_s, f.allDay() ? QString() : f.endTime.value_or(*f.startTime).toString(u"HH:mm"_s)},
            {u"allDay"_s, f.allDay()},
            {u"recurring"_s, fields->recurring},
            {u"rule"_s, f.rule},
            {u"ruleText"_s, fields->ruleText},
            {u"readOnly"_s, owner.readOnly},
            {u"canEdit"_s, !owner.readOnly && !e.href.isEmpty()},
            {u"whenText"_s, whenText(f)},
            {u"reminderMinutes"_s, f.reminderMinutes},
            {u"instance"_s, parts.at(2)},
    };
    details.insert(invitationDetails(e, addressesOf(m_store->settings(), id), details.value(u"canEdit"_s).toBool()));
    return details;
}

bool WorkspaceCalendars::answerEvent(const QString &key, const QString &answer)
{
    const QStringList parts = key.split(KeySep);
    const std::optional<caldav::Participation> value = answerOf(answer);
    if (parts.size() != 3 || !m_sync || !value || !eventDetails(key).value(u"canAnswer"_s).toBool())
        return false;
    caldav::EventIntent intent;
    intent.kind = caldav::EventIntent::Kind::Answer;
    intent.calendarId = parts.at(0);
    intent.uid = parts.at(1);
    intent.answer = value;
    intent.addresses = addressesOf(m_store->settings(), intent.calendarId);
    return queueIntent(intent, false);
}

QString WorkspaceCalendars::whenText(const caldav::EventEdit &f) const
{
    const dates::UiLocale loc = localeOf(m_language);
    const int year = now().date().year();
    QString when = dates::formatDateShort(f.startDate, loc, year);
    if (!f.allDay()) {
        when += u", "_s + f.startTime->toString(u"HH:mm"_s) + u"–"_s;
        if (f.endDate != f.startDate)
            when += dates::formatDateShort(f.endDate, loc, year) + u' ';
        when += f.endTime.value_or(*f.startTime).toString(u"HH:mm"_s);
    } else if (f.endDate != f.startDate) {
        when += u" – "_s + dates::formatDateShort(f.endDate, loc, year);
    }
    return when;
}

bool WorkspaceCalendars::saveEvent(const QVariantMap &values)
{
    if (!m_sync || !m_store)
        return false;
    const QString key = values.value(u"key"_s).toString();
    caldav::EventIntent intent;
    if (key.isEmpty()) {
        intent.kind = caldav::EventIntent::Kind::Create;
        intent.calendarId = values.value(u"calendarId"_s).toString();
        intent.uid = caldav::newEventUid();
        intent.patch = dialogPatch(values, {}, false);
        return queueIntent(intent, true);
    }
    const QStringList parts = key.split(KeySep);
    const QVariantMap before = eventDetails(key);
    if (before.isEmpty() || !before.value(u"canEdit"_s).toBool())
        return false;
    intent.kind = caldav::EventIntent::Kind::Change;
    intent.calendarId = parts.at(0);
    intent.uid = parts.at(1);
    if (values.value(u"scope"_s).toString() == u"this")
        intent.instance = caldav::instanceFromText(parts.at(2));
    intent.patch = dialogPatch(values, before, intent.instance.has_value());
    return queueIntent(intent, true);
}

bool WorkspaceCalendars::deleteEvent(const QString &key, bool thisOccurrenceOnly)
{
    const QStringList parts = key.split(KeySep);
    if (parts.size() != 3 || !m_sync)
        return false;
    const QVariantMap before = eventDetails(key);
    if (before.isEmpty() || !before.value(u"canEdit"_s).toBool())
        return false;
    caldav::EventIntent intent;
    intent.kind = caldav::EventIntent::Kind::Delete;
    intent.calendarId = parts.at(0);
    intent.uid = parts.at(1);
    if (thisOccurrenceOnly)
        intent.instance = caldav::instanceFromText(parts.at(2));
    return queueIntent(intent, false);
}

bool WorkspaceCalendars::queueIntent(caldav::EventIntent intent, bool sayWhy)
{
    intent.at = now().toUTC();
    caldav::TodoContext context;
    context.now = now();
    context.zone = m_zone;
    const caldav::CalendarSettings s = m_store->settings();
    const caldav::CalendarEntry *entry = s.calendar(intent.calendarId);
    caldav::EventProblem why = caldav::EventProblem::NoCalendar;
    std::optional<caldav::EventChange> change;
    if (entry)
        change = caldav::plannedChange(*entry, m_store->state(entry->id), intent, context, &why);
    if (!change || !m_sync->queueEvent(entry->id, *change)) {
        if (sayWhy)
            Q_EMIT errorOccurred(why == caldav::EventProblem::NoTitle || why == caldav::EventProblem::NoStart
                                         ? tr("An event needs a title and a date.")
                                         : tr("The event could not be saved."));
        return false;
    }
    if (m_scheduler)
        m_scheduler->syncNow();
    return true;
}

QString WorkspaceCalendars::journalLine(const QString &key) const
{
    const QVariantMap e = eventDetails(key);
    if (e.isEmpty())
        return {};
    QString line = u"- "_s;
    const QString start = e.value(u"startTime"_s).toString();
    const QString end = e.value(u"endTime"_s).toString();
    if (!start.isEmpty()) {
        line += start;
        if (!end.isEmpty() && end != start && e.value(u"endDate"_s) == e.value(u"startDate"_s))
            line += u"–"_s + end;
        line += u' ';
    }
    line += caldav::lineSafeText(e.value(u"title"_s).toString());
    const QString calendar = e.value(u"calendarName"_s).toString();
    if (!calendar.isEmpty())
        line += u" ("_s + calendar + u')';
    if (const QString where = e.value(u"location"_s).toString().simplified(); !where.isEmpty())
        line += u"\n  location:: "_s + where;
    return line;
}

// ---- configuration --------------------------------------------------------------------------------

QString WorkspaceCalendars::nextColor() const
{
    QSet<QString> taken;
    if (m_store) {
        const caldav::CalendarSettings s = m_store->settings();
        for (const caldav::Subscription &sub : s.subscriptions)
            taken.insert(sub.color);
        for (const caldav::CalendarEntry &c : s.calendars)
            taken.insert(c.color);
    }
    const QStringList colors = palette();
    for (const QString &c : colors) {
        if (!taken.contains(c))
            return c;
    }
    return colors.at(int(taken.size()) % colors.size());
}

QString WorkspaceCalendars::addSubscription(const QString &url, const QString &name, const QString &color)
{
    if (!m_store) {
        Q_EMIT errorOccurred(tr("Open a notebook first."));
        return {};
    }
    QString problem;
    const QUrl feed = caldav::feedUrl(url, &problem);
    if (!feed.isValid()) {
        Q_EMIT errorOccurred(problem);
        return {};
    }
    caldav::CalendarSettings s = m_store->settings();
    for (const caldav::Subscription &sub : s.subscriptions) {
        if (QUrl(sub.url) == feed) {
            Q_EMIT errorOccurred(tr("This calendar is subscribed already."));
            return {};
        }
    }
    caldav::Subscription sub;
    sub.id = caldav::CalendarStore::newId(u's');
    sub.url = feed.toString(QUrl::FullyEncoded);
    sub.name = name.simplified();
    sub.color = caldav::normalizedColor(color);
    if (sub.color.isEmpty())
        sub.color = nextColor();
    s.subscriptions.append(sub);
    if (!m_store->saveSettings(s)) {
        Q_EMIT errorOccurred(tr("The calendars could not be saved."));
        return {};
    }
    configure();
    Q_EMIT configurationChanged();
    refresh(sub.id);
    return sub.id;
}

bool WorkspaceCalendars::updateSubscription(const QString &id, const QVariantMap &values)
{
    if (!m_store)
        return false;
    caldav::CalendarSettings s = m_store->settings();
    for (caldav::Subscription &sub : s.subscriptions) {
        if (sub.id != id)
            continue;
        applySubscriptionValues(sub, values);
        bool refetch = false;
        if (values.contains(u"url"_s)) {
            QString problem;
            const QUrl feed = caldav::feedUrl(values.value(u"url"_s).toString(), &problem);
            if (!feed.isValid()) {
                Q_EMIT errorOccurred(problem);
                return false;
            }
            refetch = feed.toString(QUrl::FullyEncoded) != sub.url;
            sub.url = feed.toString(QUrl::FullyEncoded);
        }
        m_store->saveSettings(s);
        if (refetch) {
            m_store->forget(id);
            refresh(id);
        }
        configure();
        invalidate(id);
        return true;
    }
    return false;
}

QStringList WorkspaceCalendars::defaultChoice(const QVariantList &found, bool tasks) const
{
    QStringList out;
    for (const QVariant &v : found) {
        const QVariantMap m = v.toMap();
        const bool events = m.value(u"events"_s).toBool();
        const bool todo = m.value(u"tasks"_s).toBool();
        if (tasks ? (todo && !events) : events)
            out.append(m.value(u"url"_s).toString());
    }
    return out;
}

QString WorkspaceCalendars::defaultTaskPage(const QString &calendarName) const
{
    const QString folder = !m_taskFolder.isEmpty() ? m_taskFolder : m_language == u"pl" ? u"Kalendarze"_s : u"Calendars"_s;
    QString name = paths::sanitizeFileName(calendarName.simplified());
    if (name.isEmpty())
        name = m_language == u"pl" ? u"Zadania"_s : u"Tasks"_s;
    return folder + u'/' + name + u".md"_s;
}

void WorkspaceCalendars::discover(const QString &server, const QString &user, const QString &pass)
{
    if (!m_sync) {
        Q_EMIT discovered(false, tr("Open a notebook first."), {});
        return;
    }
    // An empty password: the one saved for this server and user, if any.
    QString usedPassword = pass;
    if (usedPassword.isEmpty()) {
        for (const caldav::Account &a : m_store->settings().accounts) {
            if (a.server == server.trimmed() && a.user == user.trimmed())
                usedPassword = password(a.id);
        }
    }
    ++m_discovering;
    Q_EMIT discoveringChanged();
    const QPointer<WorkspaceCalendars> self(this);
    const QString srv = server.trimmed();
    const QString usr = user.trimmed();
    m_sync->discover(srv, usr, usedPassword, [self, srv, usr](const caldav::Discovery &d) {
        if (!self)
            return;
        --self->m_discovering;
        Q_EMIT self->discoveringChanged();
        if (d.error.isError()) {
            Q_EMIT self->discovered(false, self->errorText(sync::errorCodeName(d.error.code)), {});
            return;
        }
        self->m_lastDiscovery = QVariantMap{
                {u"server"_s, srv}, {u"user"_s, usr}, {u"principal"_s, d.principalUrl}, {u"home"_s, d.homeUrl},
                {u"addresses"_s, d.addresses}};
        QVariantList calendars;
        QSet<QString> used;
        if (self->m_store) {
            for (const caldav::Subscription &sub : self->m_store->settings().subscriptions)
                used.insert(sub.color);
            for (const caldav::CalendarEntry &c : self->m_store->settings().calendars)
                used.insert(c.color);
        }
        const QStringList colors = palette();
        int next = 0;
        for (const caldav::CalendarInfo &c : d.calendars) {
            QString color = c.color;
            while (color.isEmpty() && next < colors.size() * 2) {
                const QString candidate = colors.at(next % colors.size());
                ++next;
                if (!used.contains(candidate) || next > colors.size())
                    color = candidate;
            }
            used.insert(color);
            const QString name = c.name.isEmpty() ? QUrl(c.url).path().section(u'/', -2, -2) : c.name;
            calendars.append(QVariantMap{{u"url"_s, c.url},
                                         {u"name"_s, name},
                                         {u"color"_s, color},
                                         {u"events"_s, c.holdsEvents()},
                                         {u"tasks"_s, c.holdsTasks()},
                                         {u"readOnly"_s, c.readOnly},
                                         {u"components"_s, c.components},
                                         {u"taskPage"_s, self->defaultTaskPage(name)}});
        }
        if (calendars.isEmpty()) {
            Q_EMIT self->discovered(false, tr("The account has no calendars."), {});
            return;
        }
        self->m_lastFound = calendars;
        Q_EMIT self->discovered(true, QString(), calendars);
    });
}

QString WorkspaceCalendars::addAccount(const QString &server, const QString &user, const QString &pass,
                                       const QStringList &eventCalendars, const QStringList &taskCalendars)
{
    QVariantList chosen;
    for (const QVariant &v : std::as_const(m_lastFound)) {
        QVariantMap m = v.toMap();
        const QString url = m.value(u"url"_s).toString();
        const bool events = eventCalendars.contains(url);
        const bool tasks = taskCalendars.contains(url);
        if (!events && !tasks)
            continue;
        m.insert(u"showEvents"_s, events);
        m.insert(u"syncTasks"_s, tasks);
        chosen.append(m);
    }
    if (!m_store)
        return {};
    caldav::CalendarSettings s = m_store->settings();
    caldav::Account account;
    for (const caldav::Account &a : s.accounts) {
        if (a.server == server.trimmed() && a.user == user.trimmed())
            account = a; // the same account again: its calendars are added to it
    }
    const bool known = !account.id.isEmpty();
    if (!known) {
        account.id = caldav::CalendarStore::newId(u'a');
        account.server = server.trimmed();
        account.user = user.trimmed();
        account.name = account.user + u" @ "_s + QUrl::fromUserInput(account.server).host();
    }
    if (m_lastDiscovery.value(u"server"_s) == account.server && m_lastDiscovery.value(u"user"_s) == account.user) {
        account.principalUrl = m_lastDiscovery.value(u"principal"_s).toString();
        account.homeUrl = m_lastDiscovery.value(u"home"_s).toString();
        account.addresses = m_lastDiscovery.value(u"addresses"_s).toStringList();
    }
    if (!pass.isEmpty() && !writeSecretFile(passwordFile(account.id), pass)) {
        Q_EMIT errorOccurred(tr("The password could not be saved."));
        return {};
    }
    if (!known)
        s.accounts.append(account);
    else {
        for (caldav::Account &a : s.accounts) {
            if (a.id == account.id)
                a = account;
        }
    }
    for (const QVariant &v : chosen) {
        const QVariantMap m = v.toMap();
        const QString url = m.value(u"url"_s).toString();
        if (url.isEmpty() || std::ranges::any_of(s.calendars, [&url](const caldav::CalendarEntry &c) { return c.url == url; }))
            continue;
        caldav::CalendarEntry c;
        c.id = caldav::CalendarStore::newId(u'c');
        c.account = account.id;
        c.url = url;
        c.name = m.value(u"name"_s).toString().simplified();
        c.color = caldav::normalizedColor(m.value(u"color"_s).toString());
        c.components = m.value(u"components"_s).toStringList();
        c.readOnly = m.value(u"readOnly"_s).toBool();
        c.showEvents = m.value(u"showEvents"_s, true).toBool();
        c.syncTasks = m.value(u"syncTasks"_s).toBool();
        c.taskPage = m.value(u"taskPage"_s).toString().trimmed();
        if (c.taskPage.isEmpty())
            c.taskPage = defaultTaskPage(c.name);
        if (!c.taskPage.endsWith(u".md"_s, Qt::CaseInsensitive))
            c.taskPage += u".md"_s;
        s.calendars.append(c);
    }
    if (!m_store->saveSettings(s)) {
        Q_EMIT errorOccurred(tr("The calendars could not be saved."));
        return {};
    }
    configure();
    Q_EMIT configurationChanged();
    refresh(QString());
    return account.id;
}

bool WorkspaceCalendars::updateAccount(const QString &id, const QVariantMap &values)
{
    if (!m_store)
        return false;
    caldav::CalendarSettings s = m_store->settings();
    for (caldav::Account &a : s.accounts) {
        if (a.id != id)
            continue;
        if (values.contains(u"conflicts"_s))
            a.conflicts = values.value(u"conflicts"_s).toString() == u"both" ? caldav::ConflictPolicy::KeepBoth
                                                                            : caldav::ConflictPolicy::Newer;
        if (values.contains(u"intervalMinutes"_s))
            a.intervalMinutes = std::clamp(values.value(u"intervalMinutes"_s).toInt(), 5, 1440);
        if (values.contains(u"password"_s)) {
            const QString p = values.value(u"password"_s).toString();
            if (!p.isEmpty() && !writeSecretFile(passwordFile(id), p)) {
                Q_EMIT errorOccurred(tr("The password could not be saved."));
                return false;
            }
        }
        m_store->saveSettings(s);
        m_sync->setPasswords([this](const QString &acc) { return password(acc); }); // the clients again
        configure();
        Q_EMIT configurationChanged();
        return true;
    }
    return false;
}

bool WorkspaceCalendars::removeAccount(const QString &id)
{
    if (!m_store)
        return false;
    caldav::CalendarSettings s = m_store->settings();
    if (!s.account(id))
        return false;
    for (const caldav::CalendarEntry &c : s.calendars) {
        if (c.account == id) {
            m_store->forget(c.id);
            m_data.remove(c.id);
        }
    }
    s.calendars.removeIf([&id](const caldav::CalendarEntry &c) { return c.account == id; });
    s.accounts.removeIf([&id](const caldav::Account &a) { return a.id == id; });
    m_store->saveSettings(s);
    writeSecretFile(passwordFile(id), QString());
    configure();
    refreshTracked();
    Q_EMIT configurationChanged();
    scheduleEventsChanged();
    return true;
}

bool WorkspaceCalendars::updateCalendar(const QString &id, const QVariantMap &values)
{
    if (!m_store)
        return false;
    caldav::CalendarSettings s = m_store->settings();
    for (caldav::CalendarEntry &c : s.calendars) {
        if (c.id != id)
            continue;
        if (values.contains(u"name"_s))
            c.name = values.value(u"name"_s).toString().simplified();
        if (values.contains(u"color"_s)) {
            if (const QString color = caldav::normalizedColor(values.value(u"color"_s).toString()); !color.isEmpty())
                c.color = color;
        }
        if (values.contains(u"showEvents"_s))
            c.showEvents = values.value(u"showEvents"_s).toBool();
        if (values.contains(u"alarms"_s))
            c.alarms = values.value(u"alarms"_s).toBool();
        if (values.contains(u"syncTasks"_s))
            c.syncTasks = values.value(u"syncTasks"_s).toBool();
        if (values.contains(u"visible"_s))
            c.visible = values.value(u"visible"_s).toBool();
        if (values.contains(u"taskPage"_s)) {
            QString page = values.value(u"taskPage"_s).toString().trimmed();
            if (!page.isEmpty() && !page.endsWith(u".md"_s, Qt::CaseInsensitive))
                page += u".md"_s;
            if (!page.isEmpty())
                c.taskPage = page;
        }
        m_store->saveSettings(s);
        configure();
        invalidate(id);
        if (values.contains(u"syncTasks"_s) && c.syncTasks && m_scheduler)
            m_scheduler->syncNow();
        return true;
    }
    return updateSubscription(id, values);
}

bool WorkspaceCalendars::removeCalendar(const QString &id)
{
    if (!m_store)
        return false;
    caldav::CalendarSettings s = m_store->settings();
    const qsizetype before = s.subscriptions.size() + s.calendars.size();
    s.subscriptions.removeIf([&id](const caldav::Subscription &sub) { return sub.id == id; });
    s.calendars.removeIf([&id](const caldav::CalendarEntry &c) { return c.id == id; });
    if (s.subscriptions.size() + s.calendars.size() == before)
        return false;
    m_store->saveSettings(s);
    m_store->forget(id);
    m_data.remove(id);
    configure();
    refreshTracked();
    Q_EMIT configurationChanged();
    scheduleEventsChanged();
    return true;
}

void WorkspaceCalendars::setCalendarVisible(const QString &id, bool visible)
{
    if (!m_store)
        return;
    caldav::CalendarSettings s = m_store->settings();
    bool changed = false;
    for (caldav::Subscription &sub : s.subscriptions) {
        if (sub.id == id && sub.visible != visible) {
            sub.visible = visible;
            changed = true;
        }
    }
    for (caldav::CalendarEntry &c : s.calendars) {
        if (c.id == id && c.visible != visible) {
            c.visible = visible;
            changed = true;
        }
    }
    if (!changed)
        return;
    m_store->saveSettings(s);
    Q_EMIT configurationChanged();
    scheduleEventsChanged();
}

void WorkspaceCalendars::refresh(const QString &id)
{
    if (!m_sync)
        return;
    if (id.isEmpty()) {
        syncNow();
        return;
    }
    m_sync->runOne(id, {});
    Q_EMIT statusChanged();
}

void WorkspaceCalendars::syncNow()
{
    if (m_scheduler)
        m_scheduler->syncNow();
}

void WorkspaceCalendars::confirmDeletions()
{
    if (m_scheduler)
        m_scheduler->syncNowConfirmingDeletions();
}

void WorkspaceCalendars::clearLog()
{
    if (m_store)
        m_store->clearLog();
    Q_EMIT logChanged();
}

// ---- QML values ------------------------------------------------------------------------------------

bool WorkspaceCalendars::hasCalendars() const
{
    if (!m_store)
        return false;
    const caldav::CalendarSettings s = m_store->settings();
    return !s.subscriptions.isEmpty() || !s.calendars.isEmpty();
}

QString WorkspaceCalendars::human(const QDateTime &time) const
{
    if (!time.isValid())
        return {};
    const QDateTime local = time.toTimeZone(m_zone.isValid() ? m_zone : QTimeZone(QTimeZone::LocalTime));
    return dates::formatHuman(DateSpec{local.date(), QTime(local.time().hour(), local.time().minute())},
                              localeOf(m_language), now().date());
}

QString WorkspaceCalendars::errorText(const QString &code) const
{
    static const QHash<QString, const char *> texts{
            {u"auth"_s, QT_TR_NOOP("The server refused the user name or password.")},
            {u"not-found"_s, QT_TR_NOOP("Nothing was found at this address.")},
            {u"network"_s, QT_TR_NOOP("The server cannot be reached. Check the address and the connection.")},
            {u"timeout"_s, QT_TR_NOOP("The server did not answer in time.")},
            {u"parse"_s, QT_TR_NOOP("The answer is not a calendar (perhaps a login page).")},
            {u"oversized"_s, QT_TR_NOOP("The calendar is too large.")},
            {u"forbidden"_s, QT_TR_NOOP("The server does not allow this.")},
            {u"server"_s, QT_TR_NOOP("The server had a problem. It is tried again later.")},
            {u"cors"_s, QT_TR_NOOP("The server does not let other websites read the calendar (CORS). The apps for "
                                   "Linux, Windows and Android can.")},
            {u"not-ready"_s, QT_TR_NOOP("Calendars are not ready.")},
            {u"cli-change"_s,
             QT_TR_NOOP("A change of an event asked for from the command line or by an AI agent could not be made.")},
    };
    if (code.isEmpty())
        return {};
    if (const char *text = texts.value(code))
        return tr(text);
    return tr("The calendar could not be updated (%1).").arg(code);
}

QVariantMap WorkspaceCalendars::row(const QString &id) const
{
    const caldav::CalendarSettings s = m_store->settings();
    if (const caldav::Subscription *sub = s.subscription(id))
        return subscriptionRow(*sub);
    if (const caldav::CalendarEntry *c = s.calendar(id))
        return calendarRow(s, *c);
    return {};
}

QVariantMap WorkspaceCalendars::subscriptionRow(const caldav::Subscription &sub) const
{
    const QString &id = sub.id;
    const caldav::FeedMeta meta = m_store->feedMeta(id);
    const QString code = meta.cors ? u"cors"_s : meta.error;
    QString status;
    if (meta.fetchedAt.isValid())
        status = tr("Updated %1").arg(human(meta.fetchedAt));
    else if (meta.error.isEmpty())
        status = tr("Not fetched yet");
    return QVariantMap{{u"id"_s, id},
                    {u"kind"_s, u"subscription"_s},
                    {u"name"_s, sub.name.isEmpty() ? QUrl(sub.url).host() : sub.name},
                    {u"color"_s, sub.color},
                    {u"visible"_s, sub.visible},
                    {u"readOnly"_s, true},
                    {u"events"_s, true},
                    {u"tasks"_s, false},
                    {u"taskPage"_s, QString()},
                    {u"account"_s, QString()},
                    {u"url"_s, sub.url},
                    {u"error"_s, code},
                    {u"errorText"_s, errorText(code)},
                    {u"statusText"_s, status},
                    {u"cors"_s, meta.cors},
                    {u"refreshMinutes"_s, sub.refreshMinutes},
                    {u"alarms"_s, sub.alarms},
                    {u"eventCount"_s, meta.events}};
}

QVariantMap WorkspaceCalendars::calendarRow(const caldav::CalendarSettings &settings, const caldav::CalendarEntry &c) const
{
    const QString &id = c.id;
    const caldav::Account *a = settings.account(c.account);
    const caldav::CalendarState st = m_store->state(id);
    QString status;
    if (st.syncedAt.isValid() && st.error.isEmpty())
        status = tr("Synced %1").arg(human(st.syncedAt));
    else if (!st.syncedAt.isValid())
        status = tr("Not synced yet");
    int tasks = 0;
    for (const caldav::Resource &res : st.resources)
        tasks += res.kind == u"VTODO" ? 1 : 0;
    return QVariantMap{{u"id"_s, id},
                    {u"kind"_s, u"caldav"_s},
                    {u"name"_s, c.name},
                    {u"color"_s, c.color},
                    {u"visible"_s, c.visible},
                    {u"readOnly"_s, c.readOnly},
                    {u"events"_s, c.showEvents},
                    {u"alarms"_s, c.alarms},
                    {u"tasks"_s, c.syncTasks},
                    {u"holdsEvents"_s, holds(c, u"VEVENT"_s)},
                    {u"holdsTasks"_s, holds(c, u"VTODO"_s)},
                    {u"taskPage"_s, c.taskPage},
                    {u"account"_s, a ? a->name : QString()},
                    {u"url"_s, c.url},
                    {u"error"_s, st.error},
                    {u"errorText"_s, errorText(st.error)},
                    {u"statusText"_s, status},
                    {u"cors"_s, false},
                    {u"refreshMinutes"_s, a ? a->intervalMinutes : 15},
                    {u"eventCount"_s, int(st.resources.size()) - tasks},
                    {u"taskCount"_s, tasks},
                    {u"outbox"_s, int(st.outbox.size())}};
}

QVariantList WorkspaceCalendars::calendars() const
{
    QVariantList out;
    if (!m_store)
        return out;
    const caldav::CalendarSettings s = m_store->settings();
    for (const caldav::Subscription &sub : s.subscriptions)
        out.append(row(sub.id));
    for (const caldav::CalendarEntry &c : s.calendars) {
        if (c.showEvents)
            out.append(row(c.id));
    }
    return out;
}

QVariantList WorkspaceCalendars::subscriptions() const
{
    QVariantList out;
    if (!m_store)
        return out;
    for (const caldav::Subscription &sub : m_store->settings().subscriptions)
        out.append(row(sub.id));
    return out;
}

QVariantList WorkspaceCalendars::accounts() const
{
    QVariantList out;
    if (!m_store)
        return out;
    const caldav::CalendarSettings s = m_store->settings();
    for (const caldav::Account &a : s.accounts) {
        QVariantList cals;
        for (const caldav::CalendarEntry &c : s.calendars) {
            if (c.account == a.id)
                cals.append(row(c.id));
        }
        out.append(QVariantMap{{u"id"_s, a.id},
                               {u"name"_s, a.name},
                               {u"server"_s, a.server},
                               {u"user"_s, a.user},
                               {u"conflicts"_s, a.conflicts == caldav::ConflictPolicy::KeepBoth ? u"both"_s : u"newer"_s},
                               {u"intervalMinutes"_s, a.intervalMinutes},
                               {u"hasPassword"_s, !password(a.id).isEmpty()},
                               {u"calendars"_s, cals}});
    }
    return out;
}

QVariantList WorkspaceCalendars::writableCalendars() const
{
    QVariantList out;
    if (!m_store)
        return out;
    const caldav::CalendarSettings s = m_store->settings();
    for (const caldav::CalendarEntry &c : s.calendars) {
        if (caldav::writableProblem(s, c.id) == caldav::EventProblem::None)
            out.append(QVariantMap{{u"id"_s, c.id}, {u"name"_s, c.name}, {u"color"_s, c.color}});
    }
    return out;
}

QVariantList WorkspaceCalendars::log() const
{
    QVariantList out;
    if (!m_store)
        return out;
    const QList<caldav::LogEntry> entries = m_store->log();
    for (qsizetype i = entries.size() - 1; i >= 0; --i) {
        const caldav::LogEntry &e = entries.at(i);
        QString kind;
        QString tone;
        QString text;
        switch (e.kind) {
        case caldav::LogEntry::Kind::Refreshed:
            kind = u"refreshed"_s;
            tone = u"success"_s;
            text = tr("Updated: %n event(s)", "", e.downloaded);
            break;
        case caldav::LogEntry::Kind::Synced:
            kind = u"synced"_s;
            tone = u"success"_s;
            text = tr("%1 received, %2 sent, %3 deleted").arg(e.downloaded).arg(e.uploaded).arg(e.deleted);
            break;
        case caldav::LogEntry::Kind::Conflict:
            kind = u"conflict"_s;
            tone = u"warning"_s;
            text = e.code == u"both"       ? tr("Changed on both sides: “%1” — both versions kept").arg(e.text)
                   : e.code == u"restored" ? tr("Changed on one side and deleted on the other: “%1” kept").arg(e.text)
                                           : tr("Changed on both sides: “%1” — the newer version kept").arg(e.text);
            break;
        case caldav::LogEntry::Kind::Deleted:
            kind = u"deleted"_s;
            tone = u"warning"_s;
            text = tr("%n deletion(s) wait for a confirmation: %1", "", e.deleted).arg(e.text);
            break;
        case caldav::LogEntry::Kind::Error:
            kind = u"error"_s;
            tone = u"danger"_s;
            text = errorText(e.code);
            break;
        }
        out.append(QVariantMap{{u"when"_s, e.at.toString(Qt::ISODate)},
                               {u"whenText"_s, human(e.at)},
                               {u"kind"_s, kind},
                               {u"tone"_s, tone},
                               {u"name"_s, e.name},
                               {u"text"_s, text},
                               {u"detail"_s, e.kind == caldav::LogEntry::Kind::Error ? e.text : QString()}});
    }
    return out;
}

QString WorkspaceCalendars::state() const
{
    if (!m_scheduler || !hasCalendars())
        return u"disabled"_s;
    if (m_sync && m_sync->isRunning())
        return u"syncing"_s;
    if (!m_scheduler->isOnline())
        return u"offline"_s;
    return m_lastError.isEmpty() ? u"idle"_s : u"error"_s;
}

QString WorkspaceCalendars::statusText() const
{
    const QString s = state();
    if (s == u"disabled")
        return tr("No calendars");
    if (s == u"syncing")
        return tr("Updating calendars…");
    if (s == u"offline")
        return tr("Offline — calendars wait for the network");
    if (s == u"error")
        return errorText(m_lastError);
    if (m_lastRun.isValid())
        return tr("Calendars updated %1").arg(human(m_lastRun));
    return tr("Calendars not updated yet");
}

bool WorkspaceCalendars::running() const
{
    return m_sync && m_sync->isRunning();
}

bool WorkspaceCalendars::needsConfirmation() const
{
    return m_needsConfirmation;
}

QStringList WorkspaceCalendars::pendingDeletions() const
{
    return m_pending;
}

} // namespace brulion::shell
