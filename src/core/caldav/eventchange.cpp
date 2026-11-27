#include "caldav/eventchange.h"

#include "caldav/taskmap.h"
#include "dates/recurrence.h"

#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QRandomGenerator>
#include <QRegularExpression>
#include <QSaveFile>
#include <QUrl>

#include <algorithm>

namespace brulion::caldav {

using namespace Qt::StringLiterals;

namespace {

constexpr int IntentVersion = 1;
constexpr qsizetype MaxTitle = 2000;
constexpr qsizetype MaxLocation = 2000;
constexpr qsizetype MaxDescription = 20'000;
constexpr qsizetype MaxUid = 1000;
constexpr int MaxReminderMinutes = 366 * 1440;
constexpr qint64 MaxIntentBytes = 1 << 20;

QString randomText(int length)
{
    static constexpr char Alphabet[] = "abcdefghijklmnopqrstuvwxyz0123456789";
    QString out;
    for (int i = 0; i < length; ++i)
        out += QChar(QLatin1Char(Alphabet[QRandomGenerator::global()->bounded(36)]));
    return out;
}

/// Wall-clock arithmetic: a date and time carried in UTC, which has no gaps — never a zone's.
QDateTime naive(QDate date, QTime time)
{
    return QDateTime(date, time, QTimeZone::UTC);
}

bool sameInstance(const io::ical::DateTimeValue &a, const io::ical::DateTimeValue &b)
{
    return a.date == b.date && a.time == b.time;
}

EventProblem entryProblem(const CalendarEntry &calendar)
{
    if (calendar.readOnly)
        return EventProblem::ReadOnly;
    if (!calendar.components.isEmpty() && !calendar.components.contains(u"VEVENT"_s))
        return EventProblem::NoEvents;
    if (!calendar.showEvents)
        return EventProblem::EventsHidden;
    return EventProblem::None;
}

// ---- an event's fields ---------------------------------------------------------------------------

/// The one occurrence event `index` has on its own (its recurrence left aside): its own start and
/// end on the clock of `zone`; nullopt when it shows none (cancelled).
std::optional<Occurrence> ownOccurrence(const CalendarData &data, qsizetype index, const QTimeZone &zone)
{
    CalendarData one;
    one.zones = data.zones;
    Event e = data.events.at(index);
    e.rrule.reset();
    e.rruleText.clear();
    e.rdates.clear();
    e.exdates.clear();
    e.recurrenceId.reset();
    e.thisAndFuture = false;
    one.events.append(e);
    const QList<Occurrence> list = occurrences(one, e.start.date.addDays(-2), e.start.date.addDays(2), zone);
    if (list.isEmpty())
        return std::nullopt;
    return list.first();
}

/// The occurrence of series `master` that started at `instance`, when the series has it.
std::optional<Occurrence> seriesOccurrence(const CalendarData &data, qsizetype master,
                                           const io::ical::DateTimeValue &instance, const QTimeZone &zone)
{
    for (const Occurrence &o : occurrences(data, instance.date.addDays(-2), instance.date.addDays(2), zone)) {
        if (o.event == master && o.instance && sameInstance(*o.instance, instance))
            return o;
    }
    return std::nullopt;
}

/// Brulion's form of a series' rule; "" none or not representable.
QString ruleOf(const Event &e)
{
    if (!e.rrule || e.rruleText.isEmpty())
        return {};
    QStringList dropped;
    const auto rule = io::ical::parseRRule(e.rruleText, &dropped);
    return rule && dropped.isEmpty() ? dates::formatRecurrence(*rule, dates::UiLocale::English) : QString();
}

/// The first reminder the dialog can show: a display alarm before the start; -1 none.
int reminderOf(const Event &e)
{
    for (const EventAlarm &a : e.alarms) {
        if (a.relative && !a.fromEnd && !a.audio)
            return int(-(a.relative->days * 1440 + a.relative->seconds / 60));
    }
    return -1;
}

EventFields fieldsFrom(const CalendarData &data, qsizetype index, const Occurrence &o)
{
    const Event &e = data.events.at(index);
    EventFields f;
    f.event = index;
    f.path = e.href;
    f.ruleText = e.rruleText;
    f.recurring = e.rrule.has_value() || !e.rdates.isEmpty() || e.recurrenceId.has_value();
    f.edit.title = e.summary;
    f.edit.location = e.location;
    f.edit.description = e.description;
    f.edit.startDate = o.start.date;
    if (o.allDay) {
        f.edit.endDate = std::max(o.start.date, o.end.date.addDays(-1));
    } else {
        f.edit.startTime = o.start.time;
        f.edit.endDate = o.end.date;
        f.edit.endTime = o.end.time;
    }
    f.edit.rule = ruleOf(e);
    f.edit.ruleChanged = false;
    f.edit.reminderMinutes = reminderOf(e);
    f.edit.reminderChanged = false;
    return f;
}

/// The series of `uid` and the override of `instance` in `data` (-1 when none).
struct Members {
    qsizetype master = -1;
    qsizetype override = -1;
    qsizetype any = -1;
};

Members membersOf(const CalendarData &data, const QString &uid, const std::optional<io::ical::DateTimeValue> &instance)
{
    Members m;
    for (qsizetype i = 0; i < data.events.size(); ++i) {
        const Event &e = data.events.at(i);
        if (e.uid != uid)
            continue;
        if (m.any < 0)
            m.any = i;
        if (!e.recurrenceId) {
            if (m.master < 0)
                m.master = i;
        } else if (instance && sameInstance(*e.recurrenceId, *instance)) {
            m.override = i;
        }
    }
    return m;
}

// ---- times -----------------------------------------------------------------------------------------

struct Times {
    QDate startDate;
    std::optional<QTime> startTime;
    QDate endDate;
    std::optional<QTime> endTime;
};

/// The start an edit gives: its day (none when only a time) and its time (none when only a day).
struct GivenStart {
    std::optional<QDate> date;
    std::optional<QTime> time;
};

GivenStart givenStart(const std::optional<EventMoment> &start)
{
    if (!start)
        return {};
    if (const QTime *t = std::get_if<QTime>(&*start))
        return {std::nullopt, *t};
    const DateSpec &d = std::get<DateSpec>(*start);
    return {d.date, d.time};
}

/// The end of a timed event: as given, else the event's length kept (a new one: an hour).
std::optional<Times> timedEnd(const EventEdit *cur, const std::optional<EventMoment> &end, Times t, EventProblem *problem)
{
    const QDateTime start = naive(t.startDate, *t.startTime);
    if (end) {
        if (const QTime *alone = std::get_if<QTime>(&*end)) {
            t.endDate = *alone < *t.startTime ? t.startDate.addDays(1) : t.startDate;
            t.endTime = *alone;
        } else {
            const DateSpec &d = std::get<DateSpec>(*end);
            t.endDate = d.date;
            t.endTime = d.time ? *d.time : cur && !cur->allDay() && cur->endTime ? *cur->endTime : t.startTime->addSecs(3600);
        }
    } else {
        qint64 secs = 3600;
        if (cur && !cur->allDay())
            secs = std::max<qint64>(0, naive(cur->startDate, *cur->startTime)
                                               .secsTo(naive(cur->endDate, cur->endTime.value_or(*cur->startTime))));
        const QDateTime e = start.addSecs(secs);
        t.endDate = e.date();
        t.endTime = e.time();
    }
    if (!t.endDate.isValid() || naive(t.endDate, *t.endTime) < start) {
        *problem = EventProblem::EndBeforeStart;
        return std::nullopt;
    }
    return t;
}

/// The last day of an all-day event: as given, else as many days as it had.
std::optional<Times> allDayEnd(const EventEdit *cur, const std::optional<EventMoment> &end, Times t, EventProblem *problem)
{
    if (end) {
        const DateSpec *d = std::get_if<DateSpec>(&*end);
        if (!d || d->time) {
            *problem = EventProblem::AllDayWithTime;
            return std::nullopt;
        }
        t.endDate = d->date;
    } else {
        t.endDate = t.startDate.addDays(cur ? std::max<qint64>(0, cur->startDate.daysTo(cur->endDate)) : 0);
    }
    if (!t.endDate.isValid() || t.endDate < t.startDate) {
        *problem = EventProblem::EndBeforeStart;
        return std::nullopt;
    }
    return t;
}

std::optional<Times> resolveTimes(const EventEdit *cur, const EventTimes &given, EventProblem *problem)
{
    const GivenStart start = givenStart(given.start);
    Times t;
    t.startDate = start.date ? *start.date : cur ? cur->startDate : QDate();
    if (!t.startDate.isValid()) {
        *problem = EventProblem::NoStart;
        return std::nullopt;
    }
    const bool allDay = given.allDay ? *given.allDay : start.time ? false : cur ? cur->allDay() : true;
    if (!allDay) {
        t.startTime = start.time ? *start.time : cur && !cur->allDay() ? *cur->startTime : QTime(9, 0);
        return timedEnd(cur, given.end, t, problem);
    }
    if (start.time) {
        *problem = EventProblem::AllDayWithTime;
        return std::nullopt;
    }
    return allDayEnd(cur, given.end, t, problem);
}

std::optional<EventChange> failed(EventProblem *problem, EventProblem why)
{
    if (problem)
        *problem = why;
    return std::nullopt;
}

/// Where an occurrence of a series that started at `old` (the device's clock; `wasDay`: a date)
/// starts once the series moved from `from` to `to`: as much later on the wall clock, at the new
/// time of day when the kind changed.
DateSpec shifted(const NaiveDateTime &old, bool wasDay, const EventEdit &from, const EventEdit &to)
{
    const qint64 days = from.startDate.daysTo(to.startDate);
    if (to.allDay())
        return {old.date.addDays(days), std::nullopt};
    if (wasDay || from.allDay())
        return {old.date.addDays(days), to.startTime};
    const QDateTime t = naive(old.date, old.time).addSecs(naive(from.startDate, *from.startTime).secsTo(naive(to.startDate, *to.startTime)));
    return {t.date(), t.time()};
}

/// The series' new ICS with its EXDATEs and exceptions moved along with its start.
QByteArray movedAlong(const QByteArray &ics, const QString &uid, const CalendarData &data, const EventEdit &from,
                      const EventEdit &to, const TodoContext &context)
{
    if (from.startDate == to.startDate && from.startTime == to.startTime)
        return ics;
    return renameInstances(
            ics, uid,
            [&](const io::ical::DateTimeValue &old) {
                return shifted(data.zones.toTarget(old, context.zone), !old.time, from, to);
            },
            context);
}

QString pathOf(const CalendarData &data, const QString &uid)
{
    for (const Event &e : data.events) {
        if (e.uid == uid)
            return e.href;
    }
    return {};
}

// ---- JSON ------------------------------------------------------------------------------------------

QString momentText(const EventMoment &m)
{
    if (const QTime *t = std::get_if<QTime>(&m))
        return t->toString(u"HH:mm"_s);
    const DateSpec &d = std::get<DateSpec>(m);
    return d.time ? d.date.toString(Qt::ISODate) + u'T' + d.time->toString(u"HH:mm"_s) : d.date.toString(Qt::ISODate);
}

std::optional<EventMoment> momentFrom(const QString &text)
{
    static const QRegularExpression timeOnly(u"^(\\d{2}):(\\d{2})$"_s);
    static const QRegularExpression day(u"^(\\d{4})-(\\d{2})-(\\d{2})(?:T(\\d{2}):(\\d{2}))?$"_s);
    if (const auto m = timeOnly.match(text); m.hasMatch()) {
        const QTime t(m.captured(1).toInt(), m.captured(2).toInt());
        return t.isValid() ? std::optional<EventMoment>(t) : std::nullopt;
    }
    const auto m = day.match(text);
    if (!m.hasMatch())
        return std::nullopt;
    DateSpec d{QDate(m.captured(1).toInt(), m.captured(2).toInt(), m.captured(3).toInt()), std::nullopt};
    if (!d.date.isValid())
        return std::nullopt;
    if (m.hasCaptured(4)) {
        const QTime t(m.captured(4).toInt(), m.captured(5).toInt());
        if (!t.isValid())
            return std::nullopt;
        d.time = t;
    }
    return EventMoment(d);
}

const QString &kindName(EventIntent::Kind kind)
{
    static const QString names[] = {u"create"_s, u"change"_s, u"delete"_s};
    return names[int(kind)];
}

/// A text field of `json` into `out`: absent is fine; false when it is no string or too long.
bool readText(const QJsonObject &json, const QString &key, qsizetype max, std::optional<QString> *out)
{
    const QJsonValue v = json.value(key);
    if (v.isUndefined())
        return true;
    if (!v.isString() || v.toString().size() > max)
        return false;
    *out = v.toString();
    return true;
}

bool readMoment(const QJsonObject &json, const QString &key, std::optional<EventMoment> *out)
{
    const QJsonValue v = json.value(key);
    if (v.isUndefined())
        return true;
    *out = v.isString() ? momentFrom(v.toString()) : std::nullopt;
    return out->has_value();
}

bool readPatch(const QJsonObject &json, EventPatch *patch)
{
    if (!readText(json, u"title"_s, MaxTitle, &patch->title) || !readText(json, u"location"_s, MaxLocation, &patch->location)
        || !readText(json, u"description"_s, MaxDescription, &patch->description)
        || !readText(json, u"rule"_s, MaxTitle, &patch->rule) || !readMoment(json, u"start"_s, &patch->times.start)
        || !readMoment(json, u"end"_s, &patch->times.end))
        return false;
    if (const QJsonValue v = json.value(u"allDay"_s); !v.isUndefined()) {
        if (!v.isBool())
            return false;
        patch->times.allDay = v.toBool();
    }
    if (const QJsonValue v = json.value(u"reminderMinutes"_s); !v.isUndefined()) {
        const double minutes = v.toDouble(-2);
        if (!v.isDouble() || minutes != double(qint64(minutes)) || minutes < -1 || minutes > MaxReminderMinutes)
            return false;
        patch->reminderMinutes = int(minutes);
    }
    return true;
}

bool safeId(const QString &id)
{
    static const QRegularExpression safe(u"^[0-9A-Za-z-]{1,80}$"_s);
    return safe.match(id).hasMatch();
}

} // namespace

// ---- EventIntent -------------------------------------------------------------------------------------

QJsonObject EventIntent::toJson() const
{
    QJsonObject o{{u"version"_s, IntentVersion},
                  {u"id"_s, id},
                  {u"kind"_s, kindName(kind)},
                  {u"calendar"_s, calendarId},
                  {u"uid"_s, uid},
                  {u"at"_s, at.toUTC().toString(Qt::ISODateWithMs)}};
    if (instance)
        o.insert(u"instance"_s, instanceText(*instance));
    const auto text = [&o](const QString &key, const std::optional<QString> &value) {
        if (value)
            o.insert(key, *value);
    };
    text(u"title"_s, patch.title);
    text(u"location"_s, patch.location);
    text(u"description"_s, patch.description);
    text(u"rule"_s, patch.rule);
    if (patch.times.start)
        o.insert(u"start"_s, momentText(*patch.times.start));
    if (patch.times.end)
        o.insert(u"end"_s, momentText(*patch.times.end));
    if (patch.times.allDay)
        o.insert(u"allDay"_s, *patch.times.allDay);
    if (patch.reminderMinutes)
        o.insert(u"reminderMinutes"_s, *patch.reminderMinutes);
    return o;
}

std::optional<EventIntent> EventIntent::fromJson(const QJsonObject &json)
{
    if (json.value(u"version"_s).toInt(-1) != IntentVersion)
        return std::nullopt;
    EventIntent i;
    const QString kind = json.value(u"kind"_s).toString();
    if (kind == kindName(Kind::Create))
        i.kind = Kind::Create;
    else if (kind == kindName(Kind::Change))
        i.kind = Kind::Change;
    else if (kind == kindName(Kind::Delete))
        i.kind = Kind::Delete;
    else
        return std::nullopt;
    i.id = json.value(u"id"_s).toString();
    i.calendarId = json.value(u"calendar"_s).toString();
    i.uid = json.value(u"uid"_s).toString();
    i.at = QDateTime::fromString(json.value(u"at"_s).toString(), Qt::ISODateWithMs);
    if (!safeId(i.id) || i.calendarId.isEmpty() || i.calendarId.size() > MaxUid || i.uid.trimmed().isEmpty()
        || i.uid.size() > MaxUid || !i.at.isValid())
        return std::nullopt;
    if (const QJsonValue v = json.value(u"instance"_s); !v.isUndefined()) {
        i.instance = instanceFromText(v.toString());
        if (!i.instance)
            return std::nullopt;
    }
    if (!readPatch(json, &i.patch))
        return std::nullopt;
    return i;
}

// ---- problems ------------------------------------------------------------------------------------------

QString problemCode(EventProblem problem)
{
    switch (problem) {
    case EventProblem::None:
        return {};
    case EventProblem::NoCalendar:
        return u"no_calendar"_s;
    case EventProblem::Subscription:
        return u"subscription"_s;
    case EventProblem::ReadOnly:
        return u"read_only"_s;
    case EventProblem::NoEvents:
        return u"no_events"_s;
    case EventProblem::EventsHidden:
        return u"events_hidden"_s;
    case EventProblem::NotFound:
        return u"not_found"_s;
    case EventProblem::NoOccurrence:
        return u"no_occurrence"_s;
    case EventProblem::NoTitle:
        return u"no_title"_s;
    case EventProblem::NoStart:
        return u"no_start"_s;
    case EventProblem::EndBeforeStart:
        return u"end_before_start"_s;
    case EventProblem::AllDayWithTime:
        return u"all_day_with_time"_s;
    case EventProblem::UnknownRule:
        return u"unknown_rule"_s;
    case EventProblem::OccurrenceRule:
        return u"occurrence_rule"_s;
    }
    return {};
}

QString problemText(EventProblem problem)
{
    switch (problem) {
    case EventProblem::None:
        return {};
    case EventProblem::NoCalendar:
        return u"No calendar of that id on this device"_s;
    case EventProblem::Subscription:
        return u"An ICS subscription is a read-only copy of a feed another program publishes"_s;
    case EventProblem::ReadOnly:
        return u"The server does not let this account change the calendar"_s;
    case EventProblem::NoEvents:
        return u"The calendar holds tasks only"_s;
    case EventProblem::EventsHidden:
        return u"The calendar's events are not shown"_s;
    case EventProblem::NotFound:
        return u"No event of that UID in the calendar"_s;
    case EventProblem::NoOccurrence:
        return u"The series has no occurrence that started then"_s;
    case EventProblem::NoTitle:
        return u"An event needs a title"_s;
    case EventProblem::NoStart:
        return u"A new event needs its day"_s;
    case EventProblem::EndBeforeStart:
        return u"The end is before the start"_s;
    case EventProblem::AllDayWithTime:
        return u"An all-day event has no times"_s;
    case EventProblem::UnknownRule:
        return u"The recurrence is not understood"_s;
    case EventProblem::OccurrenceRule:
        return u"One occurrence has no recurrence of its own"_s;
    }
    return {};
}

EventProblem writableProblem(const CalendarSettings &settings, const QString &id)
{
    if (const CalendarEntry *c = settings.calendar(id))
        return entryProblem(*c);
    return settings.subscription(id) ? EventProblem::Subscription : EventProblem::NoCalendar;
}

// ---- fields and changes ------------------------------------------------------------------------------

std::optional<EventFields> eventFields(const CalendarData &data, const QString &uid,
                                       const std::optional<io::ical::DateTimeValue> &instance, const QTimeZone &zone,
                                       EventProblem *problem)
{
    if (problem)
        *problem = EventProblem::None;
    const Members m = membersOf(data, uid, instance);
    if (m.any < 0) {
        if (problem)
            *problem = EventProblem::NotFound;
        return std::nullopt;
    }
    qsizetype index = -1;
    std::optional<Occurrence> o;
    if (m.override >= 0) {
        index = m.override;
        o = ownOccurrence(data, index, zone);
    } else if (instance && m.master >= 0) {
        index = m.master;
        o = seriesOccurrence(data, index, *instance, zone);
    } else if (!instance) {
        index = m.master >= 0 ? m.master : m.any; // a lone override: an invitation to one meeting
        o = ownOccurrence(data, index, zone);
    }
    if (!o) {
        if (problem)
            *problem = EventProblem::NoOccurrence;
        return std::nullopt;
    }
    return fieldsFrom(data, index, *o);
}

std::optional<EventEdit> editedFields(const std::optional<EventFields> &current, const EventPatch &patch,
                                      EventProblem *problem)
{
    EventProblem why = EventProblem::None;
    if (problem)
        *problem = why;
    EventEdit e;
    if (current) {
        e = current->edit;
        e.ruleChanged = false;
        e.reminderChanged = false;
    }
    if (patch.title)
        e.title = *patch.title;
    if (patch.location)
        e.location = *patch.location;
    if (patch.description)
        e.description = *patch.description;
    if (patch.rule) {
        e.rule = patch.rule->trimmed().isEmpty() ? QString() : dates::canonicalRecurrence(*patch.rule);
        e.ruleChanged = true;
        if (e.rule.isEmpty() && !patch.rule->trimmed().isEmpty())
            why = EventProblem::UnknownRule;
    }
    if (patch.reminderMinutes) {
        e.reminderMinutes = std::max(-1, *patch.reminderMinutes);
        e.reminderChanged = true;
    }
    if (e.title.trimmed().isEmpty())
        why = EventProblem::NoTitle;
    std::optional<Times> t;
    if (why == EventProblem::None)
        t = resolveTimes(current ? &current->edit : nullptr, patch.times, &why);
    if (!t) {
        if (problem)
            *problem = why;
        return std::nullopt;
    }
    e.startDate = t->startDate;
    e.startTime = t->startTime;
    e.endDate = t->endDate;
    e.endTime = t->endTime;
    return e;
}

std::optional<EventChange> plannedChange(const CalendarEntry &calendar, const CalendarState &state,
                                         const EventIntent &intent, const TodoContext &context, EventProblem *problem,
                                         EventEdit *result)
{
    if (problem)
        *problem = EventProblem::None;
    if (const EventProblem p = entryProblem(calendar); p != EventProblem::None)
        return failed(problem, p);
    if (intent.instance && intent.patch.rule)
        return failed(problem, EventProblem::OccurrenceRule);
    const CalendarData data = eventsOf(state);
    EventChange change;
    change.uid = intent.uid;
    change.at = (intent.at.isValid() ? intent.at : context.now).toUTC();
    change.path = pathOf(data, intent.uid);
    EventProblem why = EventProblem::None;
    if (change.path.isEmpty()) {
        if (intent.kind != EventIntent::Kind::Create)
            return failed(problem, EventProblem::NotFound);
        const auto edit = editedFields(std::nullopt, intent.patch, &why);
        if (!edit)
            return failed(problem, why);
        change.kind = EventChange::Kind::Create;
        change.path = newEventPath(calendar, intent.uid);
        change.data = newEvent(intent.uid, *edit, context);
        if (result)
            *result = *edit;
        return change;
    }
    // (A UID the calendar has — a creation taken twice too: that event, changed.)
    const auto current = eventFields(data, intent.uid, intent.instance, context.zone, &why);
    if (!current)
        return failed(problem, why);
    const QByteArray ics = state.resources.value(change.path).data;
    const bool series = membersOf(data, intent.uid, std::nullopt).master >= 0;
    const bool one = intent.instance && series;
    change.kind = EventChange::Kind::Update;
    if (intent.kind == EventIntent::Kind::Delete) {
        if (!one) {
            change.kind = EventChange::Kind::Delete;
            return change;
        }
        change.data = excludeOccurrence(ics, intent.uid, *intent.instance, context);
    } else {
        const auto edit = editedFields(current, intent.patch, &why);
        if (!edit)
            return failed(problem, why);
        change.data = one ? patchOccurrence(ics, intent.uid, *intent.instance, *edit, context)
                          : movedAlong(patchEvent(ics, intent.uid, *edit, context), intent.uid, data, current->edit, *edit,
                                       context);
        if (result)
            *result = *edit;
    }
    if (change.data.isEmpty())
        return failed(problem, EventProblem::NotFound);
    return change;
}

void addToOutbox(CalendarState &state, EventChange change)
{
    const auto known = state.resources.constFind(change.path);
    const QString etag = known != state.resources.constEnd() ? known->etag : QString();
    if (change.kind != EventChange::Kind::Create && change.ifMatch.isEmpty())
        change.ifMatch = etag;
    for (qsizetype i = 0; i < state.outbox.size(); ++i) {
        const EventChange &earlier = state.outbox.at(i);
        if (earlier.path != change.path)
            continue;
        if (earlier.kind == EventChange::Kind::Create && change.kind == EventChange::Kind::Delete) {
            state.outbox.removeAt(i);
            state.resources.remove(change.path);
            return;
        }
        if (earlier.kind == EventChange::Kind::Create)
            change.kind = EventChange::Kind::Create;
        change.ifMatch = earlier.ifMatch;
        state.outbox.removeAt(i);
        break;
    }
    if (change.kind == EventChange::Kind::Delete)
        state.resources.remove(change.path);
    else
        state.resources.insert(change.path, Resource{etag, change.uid, u"VEVENT"_s, change.data});
    state.outbox.append(change);
}

CalendarState withIntents(CalendarState state, const CalendarEntry &calendar, const QList<EventIntent> &intents,
                          const TodoContext &context)
{
    for (const EventIntent &i : intents) {
        if (i.calendarId != calendar.id)
            continue;
        if (auto change = plannedChange(calendar, state, i, context))
            addToOutbox(state, std::move(*change));
    }
    return state;
}

CalendarData eventsOf(const CalendarState &state)
{
    CalendarData data;
    for (auto it = state.resources.constBegin(); it != state.resources.constEnd(); ++it) {
        if (it->kind == u"VEVENT")
            appendCalendarData(data, it->data, it.key());
    }
    return data;
}

QString newEventPath(const CalendarEntry &calendar, const QString &uid)
{
    QString folder = QUrl(calendar.url).path(QUrl::FullyDecoded);
    if (!folder.endsWith(u'/'))
        folder += u'/';
    QString name;
    for (const QChar c : uid)
        name += c.isLetterOrNumber() || c == u'-' || c == u'_' || c == u'.' ? c : u'-';
    if (name.isEmpty())
        name = u"event"_s;
    return folder + name.left(120) + u".ics"_s;
}

QString newEventUid()
{
    return u"brulion-event-"_s + randomText(16);
}

QString instanceText(const io::ical::DateTimeValue &instance)
{
    QString text = formatDateTimeValue(instance);
    if (!instance.tzid.isEmpty())
        text += u'@' + instance.tzid;
    return text;
}

std::optional<io::ical::DateTimeValue> instanceFromText(const QString &text)
{
    static const QRegularExpression form(u"^\\d{8}(T\\d{6}Z?)?(@[^@\\s]+)?$"_s);
    if (!form.match(text).hasMatch())
        return std::nullopt;
    const qsizetype at = text.indexOf(u'@');
    io::ical::Property p;
    p.name = u"RECURRENCE-ID"_s;
    p.value = at < 0 ? text : text.first(at);
    if (at >= 0)
        p.params.insert(u"TZID"_s, text.sliced(at + 1));
    if (p.value.size() == 8)
        p.params.insert(u"VALUE"_s, u"DATE"_s);
    const auto v = io::ical::parseDateTime(p);
    if (!v || !v->date.isValid() || (v->time && !v->time->isValid()))
        return std::nullopt;
    return v;
}

// ---- the inbox ---------------------------------------------------------------------------------------

QString inboxFolder(const QString &calendarsFolder)
{
    return calendarsFolder + u"/inbox"_s;
}

QString newIntentId(const QDateTime &at)
{
    return at.toUTC().toString(u"yyyyMMdd'T'HHmmsszzz"_s) + u'-' + randomText(8);
}

bool postIntent(const QString &calendarsFolder, const EventIntent &intent)
{
    if (calendarsFolder.isEmpty() || !safeId(intent.id))
        return false;
    const QString folder = inboxFolder(calendarsFolder);
    if (!QDir().mkpath(folder))
        return false;
    constexpr auto Private = QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner;
    QFile::setPermissions(calendarsFolder, Private);
    QFile::setPermissions(folder, Private);
    const QString path = folder + u'/' + intent.id + u".json"_s;
    if (QFile::exists(path))
        return false; // written once
    QSaveFile f(path);
    if (!f.open(QIODevice::WriteOnly))
        return false;
    f.write(QJsonDocument(intent.toJson()).toJson(QJsonDocument::Compact));
    if (!f.commit())
        return false;
    QFile::setPermissions(path, QFileDevice::ReadOwner | QFileDevice::WriteOwner);
    return true;
}

QList<PendingIntent> pendingIntents(const QString &calendarsFolder, int max)
{
    QList<PendingIntent> out;
    if (calendarsFolder.isEmpty())
        return out;
    const QDir dir(inboxFolder(calendarsFolder));
    QStringList names = dir.entryList({u"*.json"_s}, QDir::Files | QDir::Hidden, QDir::Name);
    if (names.size() > max)
        names.resize(std::max(0, max));
    for (const QString &name : std::as_const(names)) {
        PendingIntent p;
        p.file = name;
        QFile f(dir.filePath(name));
        if (f.open(QIODevice::ReadOnly) && f.size() <= MaxIntentBytes) {
            const QJsonDocument doc = QJsonDocument::fromJson(f.read(MaxIntentBytes));
            if (doc.isObject())
                p.intent = EventIntent::fromJson(doc.object());
        }
        out.append(p);
    }
    return out;
}

} // namespace brulion::caldav
