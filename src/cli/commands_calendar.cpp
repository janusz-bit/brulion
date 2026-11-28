// Calendars of other programs (docs/UI.md "Calendars", docs/CLI.md "Calendar events"): calendar
// list and calendar events read what the app's last sync kept on this device (`<device
// state>/calendars`, caldav/store.h) — the events of ICS subscriptions and CalDAV calendars
// expanded into the days asked for —; calendar add, edit and delete ask for a change of a CalDAV
// event. Nothing is fetched here and the calendars' state is never written: the app is its one
// writer. A change is left for it in the inbox (caldav/eventchange.h: one file of its own, written
// once), checked first against the calendar as it will be once the app took the changes left
// before; the app's run applies it and sends it to the server.
#include "commands_p.h"

#include "caldav/eventchange.h"
#include "caldav/events.h"
#include "caldav/store.h"
#include "dates/recurrence.h"
#include "singleinstance.h"
#include "vault/vault.h"

#include <QDir>
#include <QFileInfo>
#include <QRegularExpression>
#include <QTimeZone>

#include <algorithm>

using namespace Qt::StringLiterals;

namespace brulion::cli {

namespace {

using caldav::EventIntent;
using caldav::EventProblem;
using caldav::Participation;

/// The calendars the app set up on this device, or nothing (no state folder, never set up). Read
/// only: the app is the one writer of their state.
std::optional<const caldav::CalendarStore> storeOf(Call &call)
{
    const QString state = call.ctx.stateDir();
    if (state.isEmpty() || !QFileInfo::exists(state + u"/calendars/calendars.json"_s))
        return std::nullopt;
    return caldav::CalendarStore(state + u"/calendars"_s);
}

QTimeZone deviceZone()
{
#if QT_CONFIG(timezone)
    return QTimeZone::systemTimeZone();
#else
    return QTimeZone(QTimeZone::LocalTime);
#endif
}

caldav::TodoContext contextOf(const Call &call)
{
    caldav::TodoContext c;
    c.now = call.ctx.now();
    c.zone = deviceZone();
    return c;
}

/// The changes left for the app that it has not taken yet, oldest first.
QList<EventIntent> pendingOf(const caldav::CalendarStore &store)
{
    QList<EventIntent> out;
    for (const caldav::PendingIntent &p : caldav::pendingIntents(store.folder())) {
        if (p.intent)
            out.append(*p.intent);
    }
    return out;
}

/// A CalDAV calendar's state as it will be once the app took the changes left for it.
caldav::CalendarState viewOf(const caldav::CalendarStore &store, const caldav::CalendarEntry &entry,
                             const QList<EventIntent> &pending, const caldav::TodoContext &context)
{
    return caldav::withIntents(store.state(entry.id), entry, pending, context);
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

QString noCalendarsText()
{
    return tr("No calendars: they are set up in the app (Sync center, Calendars)");
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

/// The events of one source: a subscription's feed, a CalDAV calendar's members with the changes
/// left for the app; `pendingUids` receives the UIDs those changes touch.
caldav::CalendarData dataOf(const caldav::CalendarStore &store, const Source &x, const QList<EventIntent> &pending,
                            const caldav::TodoContext &context, QSet<QString> *pendingUids)
{
    caldav::CalendarData data;
    if (x.kind == u"subscription"_s) {
        caldav::appendCalendarData(data, store.feedData(x.id));
        return data;
    }
    const caldav::CalendarSettings settings = store.settings();
    const caldav::CalendarEntry *entry = settings.calendar(x.id);
    for (const EventIntent &i : pending) {
        if (i.calendarId == x.id)
            pendingUids->insert(i.uid);
    }
    return entry ? caldav::eventsOf(viewOf(store, *entry, pending, context)) : data;
}

/// One occurrence as calendar events lists it.
struct EventRow {
    QDateTime sortKey;
    QJsonObject json;
    QString line;
};

/// "2026-10-14 08:00–08:15 Title (Calendar) — place".
QString lineOf(const Source &x, const caldav::Event &e, const caldav::Occurrence &o)
{
    QString line = o.allDay ? o.start.date.toString(Qt::ISODate)
                            : o.start.date.toString(Qt::ISODate) + u' ' + o.start.time.toString(u"HH:mm"_s) + u'–'
                                      + o.end.time.toString(u"HH:mm"_s);
    line += u' ' + (e.summary.isEmpty() ? tr("(no title)") : e.summary) + u" ("_s + x.name + u')';
    if (!e.location.isEmpty())
        line += u" — "_s + e.location;
    return line;
}

EventRow rowOf(const Source &x, const caldav::Event &e, const caldav::Occurrence &o, bool pending)
{
    QJsonObject j{{u"calendar"_s, x.name},
                  {u"calendarId"_s, x.id},
                  {u"uid"_s, e.uid},
                  {u"title"_s, e.summary},
                  {u"start"_s, timeText(o.start, o.allDay)},
                  {u"end"_s, o.allDay ? o.lastDay().toString(Qt::ISODate) : timeText(o.end, false)},
                  {u"allDay"_s, o.allDay},
                  {u"recurring"_s, o.recurring}};
    if (o.instance)
        j.insert(u"instance"_s, caldav::instanceText(*o.instance));
    if (pending)
        j.insert(u"pending"_s, true);
    for (const auto &[key, value] : {std::pair{u"location"_s, e.location}, std::pair{u"description"_s, e.description},
                                     std::pair{u"status"_s, e.status}}) {
        if (!value.isEmpty())
            j.insert(key, value);
    }
    return {QDateTime(o.start.date, o.allDay ? QTime(0, 0) : o.start.time, QTimeZone::UTC), j, lineOf(x, e, o)};
}

/// The occurrences of the calendars `filter` names (all: "") in [from, last], sorted.
QList<EventRow> rowsOf(const caldav::CalendarStore &store, const QString &filter, QDate from, QDate last,
                       const caldav::TodoContext &context, bool *truncated)
{
    QList<EventRow> rows;
    const QList<EventIntent> pending = pendingOf(store);
    for (const Source &x : sourcesOf(store)) {
        const bool named = filter.isEmpty() || x.id == filter || x.name.compare(filter, Qt::CaseInsensitive) == 0;
        if (!named || !x.events)
            continue;
        QSet<QString> pendingUids;
        const caldav::CalendarData data = dataOf(store, x, pending, context, &pendingUids);
        bool cut = false;
        for (const caldav::Occurrence &o : caldav::occurrences(data, from, last, context.zone, {}, &cut)) {
            const caldav::Event &e = data.events.at(o.event);
            rows.append(rowOf(x, e, o, pendingUids.contains(e.uid)));
        }
        *truncated = *truncated || cut;
    }
    std::ranges::stable_sort(rows, [](const EventRow &a, const EventRow &b) { return a.sortKey < b.sortKey; });
    return rows;
}

/// --from and --to: the days asked for; nullopt with the call's error.
std::optional<std::pair<QDate, QDate>> rangeOf(Call &call)
{
    const auto from = call.date(call.str(u"from"_s));
    const auto to = call.has(u"to"_s) ? call.date(call.str(u"to"_s)) : std::optional<QDate>();
    if (!from || (call.has(u"to"_s) && !to))
        return std::nullopt;
    const QDate last = to ? *to : from->addDays(6);
    if (last < *from)
        call.setError(ExitUsage, u"bad_range"_s, tr("--to is before --from"));
    else if (from->daysTo(last) > 800)
        call.setError(ExitUsage, u"bad_range"_s, tr("At most 800 days at a time"));
    else
        return std::pair{*from, last};
    return std::nullopt;
}

Result calendarEvents(Call &call)
{
    const auto range = rangeOf(call);
    if (!range)
        return call.failed();
    const auto [from, last] = *range;
    const int limit = std::clamp(call.integer(u"limit"_s, 200), 1, 5000);
    const auto store = storeOf(call);
    bool truncated = false;
    QList<EventRow> rows;
    if (store)
        rows = rowsOf(*store, call.str(u"calendar"_s).trimmed(), from, last, contextOf(call), &truncated);
    if (rows.size() > limit) {
        rows.resize(limit);
        truncated = true;
    }
    QJsonArray events;
    QString text;
    for (const EventRow &r : std::as_const(rows)) {
        events.append(r.json);
        text += r.line + u'\n';
    }
    if (events.isEmpty())
        text = store ? tr("No events in these days") + u'\n' : noCalendarsText() + u'\n';
    return call.ok(QJsonObject{{u"from"_s, from.toString(Qt::ISODate)},
                               {u"to"_s, last.toString(Qt::ISODate)},
                               {u"events"_s, events},
                               {u"truncated"_s, truncated}},
                   text);
}

// ---- changes ----------------------------------------------------------------------------------------

/// How a problem reaches the caller: its exit code, error code and words (%1: the calendar's name).
struct Refusal {
    int exit;
    const char16_t *code;
    const char *message;
};

/// One row per EventProblem but None, in its order.
constexpr Refusal Refusals[] = {
        {ExitNotFound, u"calendar_not_found",
         QT_TRANSLATE_NOOP("brulion::cli", "No calendar “%1” (calendar list shows them)")},
        {ExitRefused, u"calendar_read_only",
         QT_TRANSLATE_NOOP("brulion::cli",
                           "“%1” is an ICS subscription: Brulion only reads the copy it fetches from the calendar's "
                           "address, so its events are changed where the calendar is published. Add the event to a "
                           "CalDAV calendar instead (calendar list: kind caldav)")},
        {ExitRefused, u"calendar_read_only",
         QT_TRANSLATE_NOOP("brulion::cli", "“%1” is read-only on its server: this account may not change it")},
        {ExitRefused, u"calendar_read_only",
         QT_TRANSLATE_NOOP("brulion::cli", "“%1” holds tasks only (they are task lines of the notes)")},
        {ExitRefused, u"calendar_read_only",
         QT_TRANSLATE_NOOP("brulion::cli",
                           "The app does not show the events of “%1” (its “Events in the calendar” is off in the Sync "
                           "center)")},
        {ExitNotFound, u"event_not_found",
         QT_TRANSLATE_NOOP("brulion::cli", "No such event in “%1” (calendar events shows each one's uid)")},
        {ExitNotFound, u"occurrence_not_found",
         QT_TRANSLATE_NOOP("brulion::cli",
                           "The event has no occurrence there in “%1” (calendar events shows each one's instance)")},
        {ExitUsage, u"no_title", QT_TRANSLATE_NOOP("brulion::cli", "An event needs a title")},
        {ExitUsage, u"no_start",
         QT_TRANSLATE_NOOP("brulion::cli", "A new event needs its day: --start (jutro 10:00, 2026-12-01)")},
        {ExitUsage, u"end_before_start", QT_TRANSLATE_NOOP("brulion::cli", "The end is before the start")},
        {ExitUsage, u"all_day_with_time",
         QT_TRANSLATE_NOOP("brulion::cli",
                           "An all-day event has no times: give its days alone, or leave out --all-day")},
        {ExitParse, u"recurrence_not_understood",
         QT_TRANSLATE_NOOP("brulion::cli", "The recurrence is not understood")},
        {ExitUsage, u"occurrence_rule",
         QT_TRANSLATE_NOOP("brulion::cli",
                           "One occurrence has no recurrence of its own: leave out --occurrence to change the series")},
        {ExitRefused, u"not_invited",
         QT_TRANSLATE_NOOP("brulion::cli",
                           "The event does not invite this account (or “%1” does not know the account's addresses: "
                           "add the account again in the app)")},
        {ExitUsage, u"no_answer", QT_TRANSLATE_NOOP("brulion::cli", "An answer is accept, decline or tentative")},
};

static_assert(std::size(Refusals) == std::size_t(EventProblem::NoAnswer), "one row per EventProblem but None");

/// The error of a change that cannot be made (`problem` is never None).
Result refusal(Call &call, EventProblem problem, const QString &calendar, const QString &uid)
{
    Q_ASSERT(problem != EventProblem::None);
    const Refusal *it = &Refusals[std::size_t(problem) - 1];
    QString message = tr(it->message);
    if (message.contains(u"%1"_s))
        message = message.arg(calendar);
    QJsonObject details{{u"reason"_s, caldav::problemCode(problem)}, {u"calendar"_s, calendar}};
    if (!uid.isEmpty())
        details.insert(u"uid"_s, uid);
    return call.fail(it->exit, QString::fromUtf16(it->code), message, details);
}

/// A time alone ("10:30", "9.15"): it keeps the event's day.
std::optional<QTime> bareTime(const QString &text)
{
    static const QRegularExpression re(u"^([01]?\\d|2[0-3])[:.]([0-5]\\d)$"_s);
    const auto m = re.match(text);
    return m.hasMatch() ? std::optional<QTime>(QTime(m.captured(1).toInt(), m.captured(2).toInt())) : std::nullopt;
}

/// --start / --end into `out`: a time alone (unless `dayNeeded`), else a date phrase ("jutro
/// 10:00", "w piątek", "2026-12-01 18:30"). False with the call's error.
bool momentArg(Call &call, const QString &name, bool dayNeeded, std::optional<caldav::EventMoment> *out)
{
    if (!call.has(name))
        return true;
    const QString text = call.str(name).trimmed();
    if (const auto t = bareTime(text); t && !dayNeeded) {
        *out = caldav::EventMoment(*t);
        return true;
    }
    const auto spec = call.dateSpec(text, name);
    if (spec)
        *out = caldav::EventMoment(*spec);
    return spec.has_value();
}

bool noneWord(const QString &text)
{
    static const QStringList words{u"none"_s, u"brak"_s, u"off"_s, u"nie"_s, u"no"_s};
    return text.isEmpty() || words.contains(text, Qt::CaseInsensitive);
}

/// --reminder: minutes before the start ("15", "15m", "1h", "2 d", "1w"; Polish units too), none.
std::optional<int> reminderOf(const QString &raw)
{
    const QString text = raw.trimmed().toLower();
    if (noneWord(text))
        return -1;
    static const QRegularExpression re(
            u"^(\\d{1,6})\\s*(m|min|minut|minuty|h|godz|godzina|godziny|d|dni|dzień|w|tyg|tydzień|tygodnie)?$"_s);
    const auto m = re.match(text);
    if (!m.hasMatch())
        return std::nullopt;
    static const QHash<QChar, int> unit{{u'm', 1}, {u'h', 60}, {u'g', 60}, {u'd', 1440}, {u'w', 10080}, {u't', 10080}};
    const QString u = m.captured(2);
    const qint64 minutes = m.captured(1).toLongLong() * (u.isEmpty() ? 1 : unit.value(u.at(0), 1));
    return minutes > 366 * 1440 ? std::nullopt : std::optional<int>(int(minutes));
}

/// The text fields of an edit: given ones only ("" removes a location or description).
void textArgs(Call &call, EventIntent::Kind kind, caldav::EventPatch *patch)
{
    if (call.args.contains(u"title"_s))
        patch->title = call.str(u"title"_s).simplified();
    if (call.args.contains(u"location"_s))
        patch->location = call.str(u"location"_s).simplified();
    if (call.args.contains(u"description"_s) || (call.ctx.globals.stdinText && kind != EventIntent::Kind::Delete))
        patch->description = call.text(u"description"_s);
}

/// --every and --reminder into `patch`; false with the call's error.
bool ruleArgs(Call &call, caldav::EventPatch *patch)
{
    if (call.args.contains(u"every"_s)) {
        const QString every = call.str(u"every"_s).trimmed();
        const QString rule = noneWord(every) ? QString() : dates::canonicalRecurrence(every);
        if (rule.isEmpty() && !noneWord(every)) {
            call.setError(
                    ExitParse, u"recurrence_not_understood"_s, tr("Not a recurrence: %1").arg(every),
                    {{u"examples"_s, QJsonArray{u"co tydzień"_s, u"every 2 weeks"_s, u"every weekday"_s, u"none"_s}}});
            return false;
        }
        patch->rule = rule;
    }
    if (call.args.contains(u"reminder"_s)) {
        const auto minutes = reminderOf(call.str(u"reminder"_s));
        if (!minutes) {
            call.setError(ExitParse, u"reminder_not_understood"_s,
                          tr("Not a reminder: %1").arg(call.str(u"reminder"_s)),
                          {{u"examples"_s, QJsonArray{u"15"_s, u"1h"_s, u"2d"_s, u"none"_s}}});
            return false;
        }
        patch->reminderMinutes = *minutes;
    }
    return true;
}

/// The edit the arguments ask for, parsed once here; false with the call's error.
bool patchArgs(Call &call, EventIntent::Kind kind, caldav::EventPatch *patch)
{
    if (call.flag(u"allDay"_s) && call.flag(u"timed"_s)) {
        call.setError(ExitUsage, u"bad_arguments"_s, tr("--all-day and --timed exclude each other"));
        return false;
    }
    if (call.flag(u"allDay"_s) || call.flag(u"timed"_s))
        patch->times.allDay = call.flag(u"allDay"_s);
    textArgs(call, kind, patch);
    const bool creating = kind == EventIntent::Kind::Create;
    return momentArg(call, u"start"_s, creating, &patch->times.start)
           && momentArg(call, u"end"_s, false, &patch->times.end) && ruleArgs(call, patch);
}

/// The ids of the calendars and subscriptions `name` names (an id, or a name in any letter case).
QStringList calendarsNamed(const caldav::CalendarSettings &s, const QString &name)
{
    QStringList out;
    for (const caldav::Subscription &sub : s.subscriptions) {
        if (sub.id == name || sub.name.compare(name, Qt::CaseInsensitive) == 0)
            out.append(sub.id);
    }
    for (const caldav::CalendarEntry &c : s.calendars) {
        if (c.id == name || c.name.compare(name, Qt::CaseInsensitive) == 0)
            out.append(c.id);
    }
    return out;
}

/// The ids among `candidates` holding the event `uid` (subscriptions too).
QStringList holding(const caldav::CalendarStore &store, const QList<EventIntent> &pending,
                    const caldav::TodoContext &context, const QString &uid)
{
    QStringList out;
    const caldav::CalendarSettings s = store.settings();
    for (const caldav::Subscription &sub : s.subscriptions) {
        const caldav::CalendarData data = caldav::parseCalendarData(store.feedData(sub.id));
        if (std::ranges::any_of(data.events, [&uid](const caldav::Event &e) { return e.uid == uid; }))
            out.append(sub.id);
    }
    for (const caldav::CalendarEntry &c : s.calendars) {
        const caldav::CalendarState view = viewOf(store, c, pending, context);
        if (std::ranges::any_of(view.resources, [&uid](const caldav::Resource &r) { return r.uid == uid; }))
            out.append(c.id);
    }
    return out;
}

QString nameOf(const caldav::CalendarSettings &s, const QString &id)
{
    if (const caldav::CalendarEntry *c = s.calendar(id))
        return c->name;
    const caldav::Subscription *sub = s.subscription(id);
    return sub ? sub->name : id;
}

/// The calendars that could hold a change: --calendar's, else (a new event) the ones events can
/// be written in, or (an existing one) the ones holding it.
QStringList candidatesFor(const Call &call, const caldav::CalendarStore &store, const QList<EventIntent> &pending,
                          const caldav::TodoContext &context, const EventIntent &intent)
{
    const caldav::CalendarSettings s = store.settings();
    const QString given = call.str(u"calendar"_s).trimmed();
    if (!given.isEmpty())
        return calendarsNamed(s, given);
    if (intent.kind != EventIntent::Kind::Create)
        return holding(store, pending, context, intent.uid);
    QStringList ids;
    for (const caldav::CalendarEntry &c : s.calendars) {
        if (caldav::writableProblem(s, c.id) == EventProblem::None)
            ids.append(c.id);
    }
    return ids;
}

/// Why there is not one calendar for the change, as the call's error.
void noSingleCalendar(Call &call, const caldav::CalendarSettings &s, const QStringList &ids, const EventIntent &intent)
{
    const QString given = call.str(u"calendar"_s).trimmed();
    if (!ids.isEmpty()) {
        QStringList names;
        for (const QString &id : ids)
            names.append(nameOf(s, id));
        call.setError(ExitConflict, u"calendar_required"_s,
                      tr("Several calendars could take it: name one with --calendar"),
                      {{u"calendars"_s, QJsonArray::fromStringList(names)}});
    } else if (!given.isEmpty()) {
        call.setError(ExitNotFound, u"calendar_not_found"_s,
                      tr("No calendar “%1” (calendar list shows them)").arg(given));
    } else if (intent.kind == EventIntent::Kind::Create) {
        call.setError(ExitRefused, u"calendar_read_only"_s,
                      tr("No calendar takes new events: add a CalDAV account in the app (Sync center, Calendars)"));
    } else {
        call.setError(ExitNotFound, u"event_not_found"_s,
                      tr("No calendar holds the event %1 (calendar events shows each one's uid)").arg(intent.uid));
    }
}

/// The calendar of a change; "" with the call's error.
QString calendarOf(Call &call, const caldav::CalendarStore &store, const QList<EventIntent> &pending,
                   const caldav::TodoContext &context, const EventIntent &intent)
{
    const QStringList ids = candidatesFor(call, store, pending, context, intent);
    if (ids.size() == 1)
        return ids.first();
    noSingleCalendar(call, store.settings(), ids, intent);
    return {};
}

/// The instances of the occurrences of `uid` that start on `day` (at its time, when it has one).
QList<io::ical::DateTimeValue> occurrencesOn(const caldav::CalendarData &data, const QString &uid, const DateSpec &day,
                                             const QTimeZone &zone)
{
    QList<io::ical::DateTimeValue> found;
    for (const caldav::Occurrence &o : caldav::occurrences(data, day.date, day.date, zone)) {
        const bool atTime = !day.time || o.start.time == *day.time;
        if (o.uid == uid && o.instance && o.start.date == day.date && atTime)
            found.append(*o.instance);
    }
    return found;
}

/// --occurrence: an instance as calendar events gives it, or the day (and time) the occurrence
/// starts on. Nullopt with the call's error.
std::optional<io::ical::DateTimeValue> occurrenceOf(Call &call, const caldav::CalendarData &data, const QString &uid,
                                                    const QTimeZone &zone)
{
    const QString text = call.str(u"occurrence"_s).trimmed();
    if (const auto v = caldav::instanceFromText(text))
        return v;
    const auto day = call.dateSpec(text, u"occurrence"_s);
    if (!day)
        return std::nullopt;
    const QList<io::ical::DateTimeValue> found = occurrencesOn(data, uid, *day, zone);
    if (found.size() == 1)
        return found.first();
    QJsonArray instances;
    for (const io::ical::DateTimeValue &v : found)
        instances.append(caldav::instanceText(v));
    if (found.isEmpty())
        call.setError(ExitNotFound, u"occurrence_not_found"_s,
                      tr("The event has no occurrence on %1 (calendar events shows each one's instance)").arg(text));
    else
        call.setError(ExitConflict, u"ambiguous_occurrence"_s,
                      tr("Several occurrences start on %1: give its time or its instance").arg(text),
                      {{u"instances"_s, instances}});
    return std::nullopt;
}

QString momentJson(QDate date, const std::optional<QTime> &time)
{
    return time ? date.toString(Qt::ISODate) + u'T' + time->toString(u"HH:mm"_s) : date.toString(Qt::ISODate);
}

QJsonObject eventJson(const caldav::EventEdit &e)
{
    QJsonObject o{{u"title"_s, e.title},
                  {u"start"_s, momentJson(e.startDate, e.startTime)},
                  {u"end"_s, momentJson(e.endDate, e.endTime)},
                  {u"allDay"_s, e.allDay()}};
    if (!e.location.isEmpty())
        o.insert(u"location"_s, e.location);
    if (!e.description.isEmpty())
        o.insert(u"description"_s, e.description);
    if (!e.rule.isEmpty())
        o.insert(u"rule"_s, e.rule);
    if (e.reminderMinutes >= 0)
        o.insert(u"reminderMinutes"_s, e.reminderMinutes);
    return o;
}

/// What a person reads: "Zebranie — 2026-12-01 10:00–11:00".
QString eventLine(const caldav::EventEdit &e)
{
    QString when = e.startDate.toString(Qt::ISODate);
    if (!e.allDay())
        when += u' ' + e.startTime->toString(u"HH:mm"_s) + u'–'
                + (e.endDate != e.startDate ? e.endDate.toString(Qt::ISODate) + u' ' : QString())
                + e.endTime.value_or(*e.startTime).toString(u"HH:mm"_s);
    else if (e.endDate != e.startDate)
        when += u" – "_s + e.endDate.toString(Qt::ISODate);
    return u"“"_s + e.title.simplified() + u"” "_s + when;
}

/// Whether the app runs in this session, and has this notebook open.
QJsonObject appJson(Call &call)
{
    const ipc::SingleInstance app(ipc::SingleInstance::keyFor(call.ctx.appDataDir()));
    const QString current = call.ctx.settingsValue(u"vaults/current"_s);
    const bool same = !current.isEmpty() && Vault::idForPath(current) == Vault::idForPath(call.ctx.vaultRoot());
    return {{u"running"_s, app.isPrimaryRunning()}, {u"sameNotebook"_s, same}};
}

QString queuedText(const QJsonObject &app)
{
    if (app.value(u"running"_s).toBool() && app.value(u"sameNotebook"_s).toBool())
        return tr("The app takes it now, shows it and sends it to the server.");
    return tr("Brulion takes it when it has this notebook open (at once while it runs), shows it and sends it to the "
              "server.");
}

/// The intent's target filled in: its calendar, its event, its occurrence. The calendar's entry,
/// or nullptr with the call's error.
const caldav::CalendarEntry *targetOf(Call &call, const caldav::CalendarSettings &settings,
                                      const caldav::CalendarStore &store, const QList<EventIntent> &pending,
                                      const caldav::TodoContext &context, EventIntent *intent)
{
    intent->uid = intent->kind == EventIntent::Kind::Create ? caldav::newEventUid() : call.str(u"uid"_s).trimmed();
    intent->calendarId = calendarOf(call, store, pending, context, *intent);
    if (intent->calendarId.isEmpty())
        return nullptr;
    if (const EventProblem p = caldav::writableProblem(settings, intent->calendarId); p != EventProblem::None) {
        call.error = refusal(call, p, nameOf(settings, intent->calendarId), intent->uid).error;
        return nullptr;
    }
    const caldav::CalendarEntry *entry = settings.calendar(intent->calendarId);
    if (call.has(u"occurrence"_s)) {
        const caldav::CalendarData data = caldav::eventsOf(viewOf(store, *entry, pending, context));
        intent->instance = occurrenceOf(call, data, intent->uid, context.zone);
        if (!intent->instance)
            return nullptr;
    }
    return entry;
}

/// When a change is asked: now on the real clock (never --now's), and after every change left
/// before it — the app takes them in that order.
QDateTime askedAt(const QList<EventIntent> &pending)
{
    QDateTime at = QDateTime::currentDateTimeUtc();
    for (const EventIntent &i : pending)
        at = std::max(at, i.at.toUTC().addMSecs(1));
    return at;
}

/// The answer argument ("accept", "decline", "tentative"; Polish too) into `intent`; false (the
/// call's error set) for anything else.
bool answerArg(Call &call, EventIntent *intent)
{
    static const QHash<QString, Participation> words = {
            {u"accept"_s, Participation::Accepted},   {u"accepted"_s, Participation::Accepted},
            {u"yes"_s, Participation::Accepted},      {u"tak"_s, Participation::Accepted},
            {u"decline"_s, Participation::Declined},  {u"declined"_s, Participation::Declined},
            {u"no"_s, Participation::Declined},       {u"nie"_s, Participation::Declined},
            {u"tentative"_s, Participation::Tentative}, {u"maybe"_s, Participation::Tentative},
            {u"może"_s, Participation::Tentative},
    };
    const auto it = words.constFind(call.text(u"answer"_s).trimmed().toLower());
    if (it == words.constEnd()) {
        
        return false;
    }
    intent->answer = *it;
    return true;
}

/// "accepted the invitation", … (changeAnswer()'s text).
QString answerText(const EventIntent &intent)
{
    switch (intent.answer.value_or(Participation::NeedsAction)) {
    case Participation::Accepted: return tr("invitation accepted");
    case Participation::Declined: return tr("invitation declined");
    case Participation::Tentative: return tr("invitation answered: maybe");
    case Participation::NeedsAction:
    case Participation::Delegated: break;
    }
    return {};
}

/// The answer to a change left for the app (`result`: the event after it; none for a deletion).
Result changeAnswer(Call &call, const caldav::CalendarEntry &entry, const EventIntent &intent,
                    const std::optional<caldav::EventEdit> &result)
{
    static const QString kinds[] = {u"create"_s, u"change"_s, u"delete"_s, u"answer"_s};
    const QJsonObject app = appJson(call);
    QJsonObject o{{u"queued"_s, !call.dryRun()},
                  {u"id"_s, intent.id},
                  {u"kind"_s, kinds[int(intent.kind)]},
                  {u"calendar"_s, entry.name},
                  {u"calendarId"_s, entry.id},
                  {u"uid"_s, intent.uid},
                  {u"app"_s, app}};
    if (intent.instance)
        o.insert(u"instance"_s, caldav::instanceText(*intent.instance));
    QString text = entry.name + u": "_s;
    if (intent.kind == EventIntent::Kind::Answer) {
        o.insert(u"answer"_s, intent.toJson().value(u"answer"_s));
        text += answerText(intent);
    } else if (result) {
        o.insert(u"event"_s, eventJson(*result));
        text += eventLine(*result);
    } else {
        text += intent.instance ? tr("one occurrence left out") : tr("deleted");
    }
    if (call.dryRun())
        o.insert(u"dryRun"_s, true);
    text += u'\n' + (call.dryRun() ? tr("Dry run: nothing was left for the app.") : queuedText(app)) + u'\n';
    return call.ok(o, text);
}

Result change(Call &call, EventIntent::Kind kind)
{
    const auto store = storeOf(call);
    if (!store)
        return call.fail(ExitUnavailable, u"no_calendars"_s, noCalendarsText());
    const caldav::TodoContext context = contextOf(call);
    const QList<EventIntent> pending = pendingOf(*store);
    EventIntent intent;
    intent.kind = kind;
    intent.at = askedAt(pending);
    intent.id = caldav::newIntentId(intent.at);
    if (kind == EventIntent::Kind::Answer ? !answerArg(call, &intent) : !patchArgs(call, kind, &intent.patch))
        return call.failed();
    const caldav::CalendarSettings settings = store->settings(); // (entry points into it)
    const caldav::CalendarEntry *entry = targetOf(call, settings, *store, pending, context, &intent);
    if (!entry)
        return call.failed();
    if (const caldav::Account *account = settings.account(entry->account); account && kind == EventIntent::Kind::Answer)
        intent.addresses = account->addresses;
    EventProblem why = EventProblem::None;
    caldav::EventEdit result;
    if (!caldav::plannedChange(*entry, viewOf(*store, *entry, pending, context), intent, context, &why, &result))
        return refusal(call, why, entry->name, intent.uid);
    if (!call.dryRun() && !caldav::postIntent(store->folder(), intent))
        return call.fail(
                ExitInternal, u"io_error"_s,
                tr("The change could not be left for the app in %1").arg(caldav::inboxFolder(store->folder())));
    const bool fields = kind == EventIntent::Kind::Create || kind == EventIntent::Kind::Change;
    return changeAnswer(call, *entry, intent, fields ? std::optional<caldav::EventEdit>(result) : std::nullopt);
}

Result calendarAdd(Call &call)
{
    return change(call, EventIntent::Kind::Create);
}

Result calendarEdit(Call &call)
{
    return change(call, EventIntent::Kind::Change);
}

Result calendarDelete(Call &call)
{
    return change(call, EventIntent::Kind::Delete);
}

Result calendarAnswer(Call &call)
{
    return change(call, EventIntent::Kind::Answer);
}

QList<Arg> eventArgs(bool creating)
{
    QList<Arg> args;
    if (creating) {
        args.append(positional(u"title"_s, ArgType::Text, QT_TRANSLATE_NOOP("brulion::cli", "The event's title"), true,
                               true));
        args.append(option(
                u"start"_s, ArgType::Date,
                QT_TRANSLATE_NOOP("brulion::cli", "Its day, with a time unless all-day (jutro 10:00, 2026-12-01)"),
                true));
        args.append(option(u"end"_s, ArgType::Date,
                           QT_TRANSLATE_NOOP("brulion::cli",
                                             "Its end: a time (11:30) or a day and time; all-day: its last day "
                                             "(default: an hour, or its one day)")));
    } else {
        args.append(positional(u"uid"_s, ArgType::String,
                               QT_TRANSLATE_NOOP("brulion::cli", "The event's uid (calendar events)")));
        args.append(
                option(u"occurrence"_s, ArgType::String,
                       QT_TRANSLATE_NOOP("brulion::cli",
                                         "Only this occurrence of a series: its instance (calendar events) or the day "
                                         "(and time) it starts")));
        args.append(option(u"title"_s, ArgType::String, QT_TRANSLATE_NOOP("brulion::cli", "A new title")));
        args.append(option(u"start"_s, ArgType::Date,
                           QT_TRANSLATE_NOOP("brulion::cli",
                                             "Move it: a day (its time and length kept), a time alone (10:30: its day "
                                             "kept) or both")));
        args.append(option(
                u"end"_s, ArgType::Date,
                QT_TRANSLATE_NOOP("brulion::cli", "A new end: a time, a day, or both (default: its length kept)")));
    }
    args.append(
            option(u"calendar"_s, ArgType::String,
                   QT_TRANSLATE_NOOP("brulion::cli",
                                     "The CalDAV calendar (its name or id; default: the only one that can take it)")));
    args.append(flag(u"allDay"_s, QT_TRANSLATE_NOOP("brulion::cli", "All-day: its days without times")));
    args.append(flag(u"timed"_s,
                     QT_TRANSLATE_NOOP("brulion::cli", "At a time (an all-day event: 09:00 unless --start has one)")));
    args.append(option(u"location"_s, ArgType::String, QT_TRANSLATE_NOOP("brulion::cli", "Where (\"\" removes it)")));
    args.append(stdinable(option(u"description"_s, ArgType::Text,
                                 QT_TRANSLATE_NOOP("brulion::cli", "Its description (\"\" removes it)"))));
    args.append(
            option(u"every"_s, ArgType::String,
                   QT_TRANSLATE_NOOP("brulion::cli", "Recurrence (co tydzień, every weekday, …; none removes it)")));
    args.append(option(
            u"reminder"_s, ArgType::String,
            QT_TRANSLATE_NOOP("brulion::cli", "A reminder this long before the start (15, 1h, 1d; none removes it)")));
    return args;
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
            "instance?, pending?, location?, description?, status?}; an all-day event's start and end are its first "
            "and last day, a series' occurrence names its instance (calendar edit --occurrence), pending marks an "
            "event with a change the app has not taken yet. Read from what the app's last sync kept on this device, "
            "with the changes left for the app; calendars whose events the app does not show are left out."));
    c.args = {option(u"from"_s, ArgType::Date, QT_TRANSLATE_NOOP("brulion::cli", "The first day (default: today)")),
              option(u"to"_s, ArgType::Date, QT_TRANSLATE_NOOP("brulion::cli", "The last day (default: 6 days after --from)")),
              option(u"calendar"_s, ArgType::String,
                     QT_TRANSLATE_NOOP("brulion::cli", "Only this calendar (its name or id)")),
              option(u"limit"_s, ArgType::Int, QT_TRANSLATE_NOOP("brulion::cli", "At most this many (default 200)"))};
    c.run = calendarEvents;
    out.append(c);
    c = Command();
    c.id = u"calendar.add"_s;
    c.summary = cliText(QT_TRANSLATE_NOOP("brulion::cli", "A new event in a CalDAV calendar"));
    c.help = cliText(QT_TRANSLATE_NOOP(
            "brulion::cli",
            "Left for the app, which shows it and sends it to the server (at once while it runs with this notebook, "
            "else when it opens it): {queued, id, kind, calendar, calendarId, uid, event {title, start, end, allDay, "
            "location?, description?, rule?, reminderMinutes?}, app {running, sameNotebook}}. Times are the "
            "device's wall clock. An ICS subscription takes no events (exit 7 calendar_read_only): it is a "
            "read-only copy of another program's feed."));
    c.args = eventArgs(true);
    c.writes = true;
    c.run = calendarAdd;
    out.append(c);
    c = Command();
    c.id = u"calendar.edit"_s;
    c.summary = cliText(QT_TRANSLATE_NOOP("brulion::cli", "Change or move an event of a CalDAV calendar"));
    c.help = cliText(QT_TRANSLATE_NOOP(
            "brulion::cli",
            "Only what is given changes (moved: its time and length kept); the whole series, or one occurrence with "
            "--occurrence. Left for the app like calendar add, which applies it to the event as the app then has "
            "it; the same answer."));
    c.args = eventArgs(false);
    c.writes = true;
    c.run = calendarEdit;
    out.append(c);
    c = Command();
    c.id = u"calendar.delete"_s;
    c.summary = cliText(QT_TRANSLATE_NOOP("brulion::cli", "Delete an event of a CalDAV calendar, or one occurrence"));
    c.help = cliText(QT_TRANSLATE_NOOP(
            "brulion::cli",
            "The whole event (the series), or with --occurrence one occurrence left out of its series. Left for the "
            "app like calendar add: {queued, id, kind, calendar, calendarId, uid, instance?, app}."));
    c.args = {
            positional(u"uid"_s, ArgType::String,
                       QT_TRANSLATE_NOOP("brulion::cli", "The event's uid (calendar events)")),
            option(u"occurrence"_s, ArgType::String,
                   QT_TRANSLATE_NOOP("brulion::cli",
                                     "Only this occurrence of a series: its instance (calendar events) or the day (and "
                                     "time) it starts")),
            option(u"calendar"_s, ArgType::String,
                   QT_TRANSLATE_NOOP("brulion::cli", "The calendar holding it (its name or id)"))};
    c.writes = true;
    c.destructive = true;
    c.run = calendarDelete;
    out.append(c);
    c = Command();
    c.id = u"calendar.answer"_s;
    c.summary = cliText(QT_TRANSLATE_NOOP("brulion::cli", "Answer an invitation: accept, decline or tentative"));
    c.help = cliText(QT_TRANSLATE_NOOP(
            "brulion::cli",
            "The account's answer to an event it is invited to (the whole event or series); its server sends the "
            "organizer the reply. Left for the app like calendar add: {queued, id, kind: answer, answer, calendar, "
            "calendarId, uid, app}."));
    c.args = {positional(u"uid"_s, ArgType::String,
                         QT_TRANSLATE_NOOP("brulion::cli", "The event's uid (calendar events)")),
              positional(u"answer"_s, ArgType::String,
                         QT_TRANSLATE_NOOP("brulion::cli", "accept, decline or tentative (tak, nie, może)")),
              option(u"calendar"_s, ArgType::String,
                     QT_TRANSLATE_NOOP("brulion::cli", "The calendar holding it (its name or id)"))};
    c.writes = true;
    c.run = calendarAnswer;
    out.append(c);
}

} // namespace brulion::cli
