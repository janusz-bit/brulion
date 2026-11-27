#include "caldav/eventchange.h"

#include "caldav/ids_p.h"
#include "caldav/taskmap.h"
#include "dates/recurrence.h"

#include <QDir>
#include <QFile>
#include <QJsonDocument>
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

/// The occurrence an edit is about: the override of `instance`, else the series' occurrence there,
/// else (no instance) the event's own — a lone override's too (an invitation to one meeting).
std::pair<qsizetype, std::optional<Occurrence>> chosenOccurrence(const CalendarData &data, const Members &m,
                                                                 const std::optional<io::ical::DateTimeValue> &instance,
                                                                 const QTimeZone &zone)
{
    if (m.override >= 0)
        return {m.override, ownOccurrence(data, m.override, zone)};
    if (!instance) {
        const qsizetype index = m.master >= 0 ? m.master : m.any;
        return {index, ownOccurrence(data, index, zone)};
    }
    if (m.master < 0)
        return {-1, std::nullopt};
    return {m.master, seriesOccurrence(data, m.master, *instance, zone)};
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

bool timed(const EventEdit *cur)
{
    return cur && !cur->allDay();
}

/// A timed event's length: its own, else an hour.
qint64 lengthOf(const EventEdit *cur)
{
    if (!timed(cur))
        return 3600;
    const QTime end = cur->endTime.value_or(*cur->startTime);
    return std::max<qint64>(0, naive(cur->startDate, *cur->startTime).secsTo(naive(cur->endDate, end)));
}

/// The time given, else the event's own, else `fallback`.
QTime timeOr(const std::optional<QTime> &given, const std::optional<QTime> &own, QTime fallback)
{
    return given.value_or(own.value_or(fallback));
}

/// The end time of an end given as a day: its own, else the event's, else an hour after `start`.
QTime endTimeFor(const EventEdit *cur, const DateSpec &end, QTime start)
{
    return timeOr(end.time, timed(cur) ? cur->endTime : std::nullopt, start.addSecs(3600));
}

/// A timed event's end as `end` gives it — a time alone on the start's day (the next one when it
/// is earlier) —, else its length kept.
QDateTime timedEndOf(const EventEdit *cur, const std::optional<EventMoment> &end, const QDateTime &start)
{
    if (!end)
        return start.addSecs(lengthOf(cur));
    if (const QTime *alone = std::get_if<QTime>(&*end))
        return naive(*alone < start.time() ? start.date().addDays(1) : start.date(), *alone);
    const DateSpec &d = std::get<DateSpec>(*end);
    return naive(d.date, endTimeFor(cur, d, start.time()));
}

std::optional<Times> timedEnd(const EventEdit *cur, const std::optional<EventMoment> &end, Times t,
                              EventProblem *problem)
{
    const QDateTime start = naive(t.startDate, *t.startTime);
    const QDateTime e = timedEndOf(cur, end, start);
    if (!e.isValid() || e < start) {
        *problem = EventProblem::EndBeforeStart;
        return std::nullopt;
    }
    t.endDate = e.date();
    t.endTime = e.time();
    return t;
}

/// The last day of an all-day event: as given, else as many days as it had.
std::optional<Times> allDayEnd(const EventEdit *cur, const std::optional<EventMoment> &end, Times t,
                               EventProblem *problem)
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

/// Whether the edited event is all-day: as given, timed when a start time is given, else as it was
/// (a new one: all-day).
bool allDayOf(const EventEdit *cur, const EventTimes &given, const GivenStart &start)
{
    if (given.allDay)
        return *given.allDay;
    if (start.time)
        return false;
    return !cur || cur->allDay();
}

QTime startTimeOf(const EventEdit *cur, const GivenStart &start)
{
    return timeOr(start.time, timed(cur) ? cur->startTime : std::nullopt, QTime(9, 0));
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
    if (!allDayOf(cur, given, start)) {
        t.startTime = startTimeOf(cur, start);
        return timedEnd(cur, given.end, t, problem);
    }
    if (start.time) {
        *problem = EventProblem::AllDayWithTime;
        return std::nullopt;
    }
    return allDayEnd(cur, given.end, t, problem);
}

/// What an edit starts from: the event's fields (its rule and reminder not rewritten unless
/// given), or a new event's.
EventEdit baseOf(const std::optional<EventFields> &current)
{
    if (!current)
        return {};
    EventEdit e = current->edit;
    e.ruleChanged = false;
    e.reminderChanged = false;
    return e;
}

void applyTexts(const EventPatch &patch, EventEdit &e)
{
    if (patch.title)
        e.title = *patch.title;
    if (patch.location)
        e.location = *patch.location;
    if (patch.description)
        e.description = *patch.description;
}

/// The rule (in its canonical form) and the reminder given; UnknownRule when the rule is none.
EventProblem applyRuleAndReminder(const EventPatch &patch, EventEdit &e)
{
    if (patch.reminderMinutes) {
        e.reminderMinutes = std::max(-1, *patch.reminderMinutes);
        e.reminderChanged = true;
    }
    if (!patch.rule)
        return EventProblem::None;
    const bool none = patch.rule->trimmed().isEmpty();
    e.rule = none ? QString() : dates::canonicalRecurrence(*patch.rule);
    e.ruleChanged = true;
    return !none && e.rule.isEmpty() ? EventProblem::UnknownRule : EventProblem::None;
}

/// `why` into `out` when there is one.
void tell(EventProblem *out, EventProblem why)
{
    if (out)
        *out = why;
}

std::optional<EventChange> failed(EventProblem *problem, EventProblem why)
{
    tell(problem, why);
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
    const QDateTime t =
            naive(old.date, old.time)
                    .addSecs(naive(from.startDate, *from.startTime).secsTo(naive(to.startDate, *to.startTime)));
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

std::optional<EventChange> newChange(const CalendarEntry &calendar, const EventIntent &intent,
                                     const TodoContext &context, EventChange change, EventProblem *problem,
                                     EventEdit *result)
{
    EventProblem why = EventProblem::None;
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

/// The change of an event the calendar has: the whole event or one occurrence of its series.
std::optional<EventChange> existingChange(const CalendarData &data, const CalendarState &state,
                                          const EventIntent &intent, const TodoContext &context, EventChange change,
                                          EventProblem *problem, EventEdit *result)
{
    EventProblem why = EventProblem::None;
    const auto current = eventFields(data, intent.uid, intent.instance, context.zone, &why);
    if (!current)
        return failed(problem, why);
    const QByteArray ics = state.resources.value(change.path).data;
    const bool one = intent.instance && membersOf(data, intent.uid, std::nullopt).master >= 0;
    change.kind = EventChange::Kind::Update;
    if (intent.kind == EventIntent::Kind::Delete) {
        if (!one)
            change.kind = EventChange::Kind::Delete;
        else
            change.data = excludeOccurrence(ics, intent.uid, *intent.instance, context);
        return change;
    }
    const auto edit = editedFields(current, intent.patch, &why);
    if (!edit)
        return failed(problem, why);
    change.data = one ? patchOccurrence(ics, intent.uid, *intent.instance, *edit, context)
                      : movedAlong(patchEvent(ics, intent.uid, *edit, context), intent.uid, data, current->edit, *edit,
                                   context);
    if (result)
        *result = *edit;
    return change;
}

/// The change waiting for the same member taken out of `state`'s outbox, its kind (still new to
/// the server) and ETag carried over to `change`; false when both go (an event deleted before it
/// was ever sent).
bool replaceEarlier(CalendarState &state, EventChange &change)
{
    const auto it = std::ranges::find(state.outbox, change.path, &EventChange::path);
    if (it == state.outbox.end())
        return true;
    const bool neverSent = it->kind == EventChange::Kind::Create;
    change.ifMatch = it->ifMatch;
    state.outbox.erase(it);
    if (neverSent && change.kind == EventChange::Kind::Delete)
        return false;
    if (neverSent)
        change.kind = EventChange::Kind::Create;
    return true;
}

QString pathOf(const CalendarData &data, const QString &uid)
{
    const auto it = std::ranges::find(data.events, uid, &Event::uid);
    return it != data.events.end() ? it->href : QString();
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

struct TextField {
    const char16_t *key;
    qsizetype max;
    std::optional<QString> EventPatch::*member;
};

constexpr TextField TextFields[] = {{u"title", MaxTitle, &EventPatch::title},
                                    {u"location", MaxLocation, &EventPatch::location},
                                    {u"description", MaxDescription, &EventPatch::description},
                                    {u"rule", MaxTitle, &EventPatch::rule}};

bool readAllDay(const QJsonObject &json, EventPatch *patch)
{
    const QJsonValue v = json.value(u"allDay"_s);
    if (v.isUndefined())
        return true;
    if (v.isBool())
        patch->times.allDay = v.toBool();
    return v.isBool();
}

bool readReminder(const QJsonObject &json, EventPatch *patch)
{
    const QJsonValue v = json.value(u"reminderMinutes"_s);
    if (v.isUndefined())
        return true;
    if (!v.isDouble())
        return false;
    const double minutes = v.toDouble();
    if (minutes != double(qint64(minutes)) || minutes < -1 || minutes > MaxReminderMinutes)
        return false;
    patch->reminderMinutes = int(minutes);
    return true;
}

bool readPatch(const QJsonObject &json, EventPatch *patch)
{
    for (const TextField &f : TextFields) {
        if (!readText(json, QString::fromUtf16(f.key), f.max, &(patch->*f.member)))
            return false;
    }
    if (!readMoment(json, u"start"_s, &patch->times.start) || !readMoment(json, u"end"_s, &patch->times.end))
        return false;
    return readAllDay(json, patch) && readReminder(json, patch);
}

bool safeId(const QString &id)
{
    static const QRegularExpression safe(u"^[0-9A-Za-z-]{1,80}$"_s);
    return safe.match(id).hasMatch();
}

bool sizedId(const QString &id)
{
    return !id.trimmed().isEmpty() && id.size() <= MaxUid;
}

/// The kind, ids and time of an intent; false when one is missing or malformed.
bool readHeader(const QJsonObject &json, EventIntent *i)
{
    static constexpr EventIntent::Kind kinds[] = {EventIntent::Kind::Create, EventIntent::Kind::Change,
                                                  EventIntent::Kind::Delete};
    const QString kind = json.value(u"kind"_s).toString();
    const auto it = std::ranges::find(kinds, kind, kindName);
    if (it == std::end(kinds))
        return false;
    i->kind = *it;
    i->id = json.value(u"id"_s).toString();
    i->calendarId = json.value(u"calendar"_s).toString();
    i->uid = json.value(u"uid"_s).toString();
    i->at = QDateTime::fromString(json.value(u"at"_s).toString(), Qt::ISODateWithMs);
    return safeId(i->id) && sizedId(i->calendarId) && sizedId(i->uid) && i->at.isValid();
}

} // namespace

// ---- EventIntent -------------------------------------------------------------------------------------

QJsonObject EventIntent::toJson() const
{
    QJsonObject o{{u"version"_s, IntentVersion}, {u"id"_s, id},   {u"kind"_s, kindName(kind)},
                  {u"calendar"_s, calendarId},   {u"uid"_s, uid}, {u"at"_s, at.toUTC().toString(Qt::ISODateWithMs)}};
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
    EventIntent i;
    if (json.value(u"version"_s).toInt(-1) != IntentVersion || !readHeader(json, &i))
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

namespace {

struct ProblemWords {
    EventProblem problem;
    const char16_t *code;
    const char16_t *text;
};

constexpr ProblemWords Problems[] = {
        {EventProblem::None, u"", u""},
        {EventProblem::NoCalendar, u"no_calendar", u"No calendar of that id on this device"},
        {EventProblem::Subscription, u"subscription",
         u"An ICS subscription is a read-only copy of a feed another program publishes"},
        {EventProblem::ReadOnly, u"read_only", u"The server does not let this account change the calendar"},
        {EventProblem::NoEvents, u"no_events", u"The calendar holds tasks only"},
        {EventProblem::EventsHidden, u"events_hidden", u"The calendar's events are not shown"},
        {EventProblem::NotFound, u"not_found", u"No event of that UID in the calendar"},
        {EventProblem::NoOccurrence, u"no_occurrence", u"The series has no occurrence that started then"},
        {EventProblem::NoTitle, u"no_title", u"An event needs a title"},
        {EventProblem::NoStart, u"no_start", u"A new event needs its day"},
        {EventProblem::EndBeforeStart, u"end_before_start", u"The end is before the start"},
        {EventProblem::AllDayWithTime, u"all_day_with_time", u"An all-day event has no times"},
        {EventProblem::UnknownRule, u"unknown_rule", u"The recurrence is not understood"},
        {EventProblem::OccurrenceRule, u"occurrence_rule", u"One occurrence has no recurrence of its own"},
};

constexpr bool problemsInOrder()
{
    for (std::size_t i = 0; i < std::size(Problems); ++i) {
        if (std::size_t(Problems[i].problem) != i)
            return false;
    }
    return std::size(Problems) == std::size_t(EventProblem::OccurrenceRule) + 1;
}
static_assert(problemsInOrder(), "one row per EventProblem, in its order");

const ProblemWords &wordsOf(EventProblem problem)
{
    return Problems[std::size_t(problem)];
}

} // namespace

QString problemCode(EventProblem problem)
{
    return QString::fromUtf16(wordsOf(problem).code);
}

QString problemText(EventProblem problem)
{
    return QString::fromUtf16(wordsOf(problem).text);
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
    tell(problem, EventProblem::None);
    const Members m = membersOf(data, uid, instance);
    if (m.any < 0) {
        tell(problem, EventProblem::NotFound);
        return std::nullopt;
    }
    const auto [index, o] = chosenOccurrence(data, m, instance, zone);
    if (!o) {
        tell(problem, EventProblem::NoOccurrence);
        return std::nullopt;
    }
    return fieldsFrom(data, index, *o);
}

std::optional<EventEdit> editedFields(const std::optional<EventFields> &current, const EventPatch &patch,
                                      EventProblem *problem)
{
    EventEdit e = baseOf(current);
    applyTexts(patch, e);
    EventProblem why = applyRuleAndReminder(patch, e);
    if (why == EventProblem::None && e.title.trimmed().isEmpty())
        why = EventProblem::NoTitle;
    std::optional<Times> t;
    if (why == EventProblem::None)
        t = resolveTimes(current ? &current->edit : nullptr, patch.times, &why);
    tell(problem, why);
    if (!t)
        return std::nullopt;
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
    tell(problem, EventProblem::None);
    if (const EventProblem p = entryProblem(calendar); p != EventProblem::None)
        return failed(problem, p);
    if (intent.instance && intent.patch.rule)
        return failed(problem, EventProblem::OccurrenceRule);
    const CalendarData data = eventsOf(state);
    EventChange change;
    change.uid = intent.uid;
    change.at = (intent.at.isValid() ? intent.at : context.now).toUTC();
    change.path = pathOf(data, intent.uid);
    if (!change.path.isEmpty()) // (a UID the calendar has — a creation taken twice too: that event, changed)
        return existingChange(data, state, intent, context, std::move(change), problem, result);
    if (intent.kind != EventIntent::Kind::Create)
        return failed(problem, EventProblem::NotFound);
    return newChange(calendar, intent, context, std::move(change), problem, result);
}

void addToOutbox(CalendarState &state, EventChange change)
{
    const auto known = state.resources.constFind(change.path);
    const QString etag = known != state.resources.constEnd() ? known->etag : QString();
    if (change.kind != EventChange::Kind::Create && change.ifMatch.isEmpty())
        change.ifMatch = etag;
    if (!replaceEarlier(state, change)) {
        state.resources.remove(change.path);
        return;
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
    return u"brulion-event-"_s + detail::randomId(16);
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
    static const QRegularExpression form(u"^(\\d{8}(?:T\\d{6}Z?)?)(?:@([^@\\s]+))?$"_s);
    const auto m = form.match(text);
    if (!m.hasMatch())
        return std::nullopt;
    io::ical::Property p;
    p.name = u"RECURRENCE-ID"_s;
    p.value = m.captured(1);
    if (m.hasCaptured(2))
        p.params.insert(u"TZID"_s, m.captured(2));
    const auto v = io::ical::parseDateTime(p); // (eight digits: a date)
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
    return at.toUTC().toString(u"yyyyMMdd'T'HHmmsszzz"_s) + u'-' + detail::randomId(8);
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
    const bool written = f.open(QIODevice::WriteOnly)
                         && f.write(QJsonDocument(intent.toJson()).toJson(QJsonDocument::Compact)) >= 0 && f.commit();
    if (written)
        QFile::setPermissions(path, QFileDevice::ReadOwner | QFileDevice::WriteOwner);
    return written;
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
