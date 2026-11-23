// Calendars of other programs (docs/UI.md "Calendars"): calendar list, calendar events — read only,
// over what the app's last sync kept on this device (`<device state>/calendars`, caldav/store.h):
// the events of ICS subscriptions and CalDAV calendars expanded into the days asked for. Nothing
// is fetched here (the app syncs) and nothing is written.
#include "commands_p.h"

#include "caldav/events.h"
#include "caldav/store.h"

#include <QDir>
#include <QFileInfo>
#include <QTimeZone>

#include <algorithm>

using namespace Qt::StringLiterals;

namespace brulion::cli {

namespace {

/// The calendars the app set up on this device, or nothing (no state folder, never set up).
std::optional<caldav::CalendarStore> storeOf(Call &call)
{
    const QString state = call.ctx.stateDir();
    if (state.isEmpty() || !QFileInfo::exists(state + u"/calendars/calendars.json"_s))
        return std::nullopt;
    return caldav::CalendarStore(state + u"/calendars"_s);
}

/// One source of events: a subscription or a CalDAV calendar.
struct Source {
    QString id;
    QString kind; ///< "subscription" | "caldav"
    QString name;
    QString color;
    QString account;
    bool visible = true;
    bool readOnly = false;
    bool events = true; ///< its events are shown (CalDAV: the calendar's switch)
    bool tasks = false;
    bool alarms = false;
    QDateTime syncedAt;
    QString error;
};

QList<Source> sourcesOf(const caldav::CalendarStore &store)
{
    QList<Source> out;
    const caldav::CalendarSettings s = store.settings();
    for (const caldav::Subscription &sub : s.subscriptions) {
        Source x;
        x.id = sub.id;
        x.kind = u"subscription"_s;
        x.name = sub.name;
        x.color = sub.color;
        x.visible = sub.visible;
        x.readOnly = true;
        const caldav::FeedMeta meta = store.feedMeta(sub.id);
        x.syncedAt = meta.fetchedAt;
        x.error = meta.error;
        out.append(x);
    }
    for (const caldav::CalendarEntry &c : s.calendars) {
        Source x;
        x.id = c.id;
        x.kind = u"caldav"_s;
        x.name = c.name;
        x.color = c.color;
        if (const caldav::Account *a = s.account(c.account))
            x.account = a->name.isEmpty() ? a->user : a->name;
        x.visible = c.visible;
        x.readOnly = c.readOnly;
        x.events = c.showEvents;
        x.tasks = c.syncTasks;
        x.alarms = c.alarms;
        const caldav::CalendarState st = store.state(c.id);
        x.syncedAt = st.syncedAt;
        x.error = st.error;
        out.append(x);
    }
    return out;
}

QString timeText(const caldav::NaiveDateTime &t, bool allDay)
{
    return allDay ? t.date.toString(Qt::ISODate) : t.date.toString(Qt::ISODate) + u'T' + t.time.toString(u"HH:mm"_s);
}

Result calendarList(Call &call)
{
    const auto store = storeOf(call);
    QJsonArray list;
    QString text;
    if (store) {
        for (const Source &x : sourcesOf(*store)) {
            QJsonObject o{{u"id"_s, x.id},
                          {u"kind"_s, x.kind},
                          {u"name"_s, x.name},
                          {u"color"_s, x.color},
                          {u"visible"_s, x.visible},
                          {u"readOnly"_s, x.readOnly},
                          {u"events"_s, x.events}};
            if (x.kind == u"caldav"_s) {
                o.insert(u"account"_s, x.account);
                o.insert(u"tasks"_s, x.tasks);
                o.insert(u"alarms"_s, x.alarms);
            }
            o.insert(u"syncedAt"_s, x.syncedAt.isValid() ? QJsonValue(x.syncedAt.toString(Qt::ISODate)) : QJsonValue());
            if (!x.error.isEmpty())
                o.insert(u"error"_s, x.error);
            list.append(o);
            text += (x.visible ? u"● "_s : u"○ "_s) + x.name + u" — "_s
                    + (x.kind == u"caldav"_s ? tr("CalDAV") + (x.account.isEmpty() ? QString() : u" "_s + x.account)
                                             : tr("subscription"))
                    + (x.error.isEmpty() ? QString() : u" — "_s + tr("error: %1").arg(x.error)) + u'\n';
        }
    }
    if (list.isEmpty())
        text = tr("No calendars: they are set up in the app (Sync center, Calendars)") + u'\n';
    return call.ok(QJsonObject{{u"calendars"_s, list}}, text);
}

Result calendarEvents(Call &call)
{
    const auto from = call.date(call.str(u"from"_s));
    const auto to = call.has(u"to"_s) ? call.date(call.str(u"to"_s)) : std::optional<QDate>();
    if (!from || (call.has(u"to"_s) && !to))
        return call.failed();
    const QDate last = to ? *to : from->addDays(6);
    if (last < *from)
        return call.fail(ExitUsage, u"bad_range"_s, tr("--to is before --from"));
    if (from->daysTo(last) > 800)
        return call.fail(ExitUsage, u"bad_range"_s, tr("At most 800 days at a time"));
    const int limit = std::clamp(call.integer(u"limit"_s, 200), 1, 5000);
    const QString filter = call.str(u"calendar"_s).trimmed();
    const auto store = storeOf(call);
    struct Row {
        QDateTime sortKey;
        QJsonObject json;
        QString line;
    };
    QList<Row> rows;
    bool truncated = false;
    if (store) {
#if QT_CONFIG(timezone)
        const QTimeZone zone = QTimeZone::systemTimeZone();
#else
        const QTimeZone zone(QTimeZone::LocalTime);
#endif
        for (const Source &x : sourcesOf(*store)) {
            if (!filter.isEmpty() && x.id != filter && x.name.compare(filter, Qt::CaseInsensitive) != 0)
                continue;
            if (!x.events)
                continue;
            caldav::CalendarData data;
            if (x.kind == u"subscription"_s) {
                caldav::appendCalendarData(data, store->feedData(x.id));
            } else {
                const caldav::CalendarState st = store->state(x.id);
                for (auto it = st.resources.constBegin(); it != st.resources.constEnd(); ++it) {
                    if (it->kind == u"VEVENT"_s)
                        caldav::appendCalendarData(data, it->data, it.key());
                }
            }
            bool cut = false;
            for (const caldav::Occurrence &o : caldav::occurrences(data, *from, last, zone, {}, &cut)) {
                const caldav::Event &e = data.events.at(o.event);
                QJsonObject j{{u"calendar"_s, x.name},
                              {u"calendarId"_s, x.id},
                              {u"uid"_s, e.uid},
                              {u"title"_s, e.summary},
                              {u"start"_s, timeText(o.start, o.allDay)},
                              {u"end"_s, o.allDay ? o.lastDay().toString(Qt::ISODate) : timeText(o.end, false)},
                              {u"allDay"_s, o.allDay},
                              {u"recurring"_s, o.recurring}};
                if (!e.location.isEmpty())
                    j.insert(u"location"_s, e.location);
                if (!e.description.isEmpty())
                    j.insert(u"description"_s, e.description);
                if (!e.status.isEmpty())
                    j.insert(u"status"_s, e.status);
                QString line = o.allDay ? o.start.date.toString(Qt::ISODate)
                                        : o.start.date.toString(Qt::ISODate) + u' ' + o.start.time.toString(u"HH:mm"_s)
                                                  + u'–' + o.end.time.toString(u"HH:mm"_s);
                line += u' ' + (e.summary.isEmpty() ? tr("(no title)") : e.summary) + u" ("_s + x.name + u')';
                if (!e.location.isEmpty())
                    line += u" — "_s + e.location;
                rows.append({QDateTime(o.start.date, o.allDay ? QTime(0, 0) : o.start.time, QTimeZone::UTC), j, line});
            }
            truncated = truncated || cut;
        }
    }
    std::ranges::stable_sort(rows, [](const Row &a, const Row &b) { return a.sortKey < b.sortKey; });
    if (rows.size() > limit) {
        rows.resize(limit);
        truncated = true;
    }
    QJsonArray events;
    QString text;
    for (const Row &r : std::as_const(rows)) {
        events.append(r.json);
        text += r.line + u'\n';
    }
    if (events.isEmpty())
        text = store ? tr("No events in these days") + u'\n'
                     : tr("No calendars: they are set up in the app (Sync center, Calendars)") + u'\n';
    return call.ok(QJsonObject{{u"from"_s, from->toString(Qt::ISODate)},
                               {u"to"_s, last.toString(Qt::ISODate)},
                               {u"events"_s, events},
                               {u"truncated"_s, truncated}},
                   text);
}

} // namespace

void addCalendarCommands(QList<Command> &out)
{
    Command c;
    c.id = u"calendar.list"_s;
    c.summary = cliText(QT_TRANSLATE_NOOP("brulion::cli", "The calendars of other programs the app shows"));
    c.help = cliText(QT_TRANSLATE_NOOP(
            "brulion::cli",
            "ICS subscriptions and CalDAV calendars as the app's last sync left them on this device: "
            "{id, kind (subscription, caldav), name, color, visible, readOnly, events, syncedAt, error?} and, "
            "for CalDAV, account, tasks (synced with task lines) and alarms (their events' reminders ring). "
            "Nothing is fetched here: the app syncs them. Addresses are never listed."));
    c.run = calendarList;
    out.append(c);
    c = Command();
    c.id = u"calendar.events"_s;
    c.summary = cliText(QT_TRANSLATE_NOOP("brulion::cli", "The events of the calendars in a range of days"));
    c.help = cliText(QT_TRANSLATE_NOOP(
            "brulion::cli",
            "Every occurrence of every event (series expanded, cancelled and left-out ones not shown) from --from to "
            "--to in the device's time zone: {calendar, calendarId, uid, title, start, end, allDay, recurring, "
            "location?, description?, status?}; an all-day event's start and end are its first and last day. Read "
            "from what the app's last sync kept on this device; calendars whose events the app does not show are "
            "left out."));
    c.args = {option(u"from"_s, ArgType::Date, QT_TRANSLATE_NOOP("brulion::cli", "The first day (default: today)")),
              option(u"to"_s, ArgType::Date, QT_TRANSLATE_NOOP("brulion::cli", "The last day (default: 6 days after --from)")),
              option(u"calendar"_s, ArgType::String,
                     QT_TRANSLATE_NOOP("brulion::cli", "Only this calendar (its name or id)")),
              option(u"limit"_s, ArgType::Int, QT_TRANSLATE_NOOP("brulion::cli", "At most this many (default 200)"))};
    c.run = calendarEvents;
    out.append(c);
}

} // namespace brulion::cli
