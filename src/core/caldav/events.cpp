#include "caldav/events.h"

#include <QDateTime>
#include <QHash>
#include <QRegularExpression>
#include <QSet>
#include <QStringTokenizer>

#include <algorithm>

namespace brulion::caldav {

using namespace Qt::StringLiterals;

namespace {

/// The values of a property holding a comma-separated list (RDATE, EXDATE), each as a property
/// of its own with the same parameters; a PERIOD's start only.
QList<io::ical::DateTimeValue> dateList(const io::ical::Property &p)
{
    QList<io::ical::DateTimeValue> out;
    for (const QStringView part : qTokenize(QStringView(p.value), u',', Qt::SkipEmptyParts)) {
        io::ical::Property single;
        single.name = p.name;
        single.params = p.params;
        const qsizetype slash = part.indexOf(u'/');
        single.value = (slash >= 0 ? part.first(slash) : part).trimmed().toString();
        if (single.params.value(u"VALUE"_s).compare(u"PERIOD"_s, Qt::CaseInsensitive) == 0)
            single.params.remove(u"VALUE"_s);
        if (const auto v = io::ical::parseDateTime(single))
            out.append(*v);
    }
    return out;
}

QString cut(QString text, int max)
{
    if (text.size() > max)
        text.truncate(max);
    return text;
}

/// A stable key of a start: the instant of a timed value, the date of an all-day one, the
/// written time of a floating one.
QString instanceKey(const io::ical::DateTimeValue &v, const ZoneTable &zones)
{
    if (!v.time)
        return u"d"_s + QString::number(v.date.toJulianDay());
    if (const auto ms = zones.instant(v))
        return u"i"_s + QString::number(*ms);
    return u"f"_s + v.date.toString(u"yyyyMMdd"_s) + v.time->toString(u"HHmmss"_s);
}

/// The duration of an event: nominal days and exact seconds (RFC 5545 §3.3.6).
struct Span {
    qint64 days = 0;
    qint64 secs = 0;
};

Span spanOf(const Event &e, const ZoneTable &zones)
{
    Span s;
    if (e.end) {
        if (e.allDay()) {
            s.days = std::max<qint64>(1, e.start.date.daysTo(e.end->date));
        } else if (e.end->time) {
            const auto a = zones.instant(e.start);
            const auto b = zones.instant(*e.end);
            if (a && b)
                s.secs = std::max<qint64>(0, (*b - *a) / 1000);
            else
                s.secs = std::max<qint64>(0, NaiveDateTime{e.start.date, *e.start.time}.secsTo({e.end->date, *e.end->time}));
        } else {
            s.days = std::max<qint64>(0, e.start.date.daysTo(e.end->date));
        }
    } else if (e.duration) {
        s.days = std::max<qint64>(0, e.duration->days);
        s.secs = std::max<qint64>(0, e.duration->seconds);
        if (e.allDay()) {
            s.days += s.secs / 86400;
            s.secs = 0;
            s.days = std::max<qint64>(1, s.days);
        }
    } else if (e.allDay()) {
        s.days = 1;
    }
    // A span of decades is a broken feed: a year at most (one occurrence per day of it).
    s.days = std::min<qint64>(s.days, 366);
    s.secs = std::min<qint64>(s.secs, 366LL * 86400);
    return s;
}

io::ical::DateTimeValue withNaive(const io::ical::DateTimeValue &like, const NaiveDateTime &t)
{
    io::ical::DateTimeValue v = like;
    v.date = t.date;
    if (like.time)
        v.time = t.time;
    return v;
}

/// Where an instance stands in time, for comparing it with a RECURRENCE-ID: the instant of a timed
/// value (a floating one is read as UTC), the day of an all-day one.
qint64 orderKey(const io::ical::DateTimeValue &v, const ZoneTable &zones)
{
    if (!v.time)
        return QDateTime(v.date, QTime(0, 0), QTimeZone::UTC).toMSecsSinceEpoch();
    if (const auto ms = zones.instant(v))
        return *ms;
    return QDateTime(v.date, *v.time, QTimeZone::UTC).toMSecsSinceEpoch();
}

/// A RECURRENCE-ID;RANGE=THISANDFUTURE override (RFC 5545 §3.8.4.4): from the instance it names on,
/// every instance of the series is replaced by this one's properties, its start and end moved by
/// as much as the override moved the named instance.
struct FutureOverride {
    qsizetype index = -1;
    qint64 from = 0; ///< order key of the instance it names
    qint64 shiftSecs = 0; ///< wall-clock shift of the start on the series' clock
};

struct Builder {
    const CalendarData &data;
    const QTimeZone &target;
    NaiveDateTime windowFrom;
    NaiveDateTime windowTo;
    ExpandLimits limits;
    bool truncated = false;
    QList<Occurrence> out;
    QSet<qsizetype> usedFuture; ///< THISANDFUTURE overrides that produced the instance they name

    qint64 targetMs(const NaiveDateTime &t) const
    {
        return QDateTime(t.date, t.time, target).toMSecsSinceEpoch();
    }

    /// An occurrence of event `index` starting at `startValue` (the series' form), if it
    /// overlaps the window.
    void add(qsizetype index, const io::ical::DateTimeValue &startValue, const Span &span,
             const std::optional<io::ical::DateTimeValue> &instance, bool recurring)
    {
        const Event &e = data.events.at(index);
        Occurrence o;
        o.event = index;
        o.uid = e.uid;
        o.instance = instance;
        o.recurring = recurring;
        o.allDay = !startValue.time;
        if (o.allDay) {
            o.start = {startValue.date, QTime(0, 0)};
            o.end = {startValue.date.addDays(std::max<qint64>(1, span.days)), QTime(0, 0)};
        } else {
            o.start = data.zones.toTarget(startValue, target);
            // Nominal days on the series' clock, then exact seconds.
            io::ical::DateTimeValue endValue = startValue;
            endValue.date = startValue.date.addDays(span.days);
            if (const auto ms = data.zones.instant(endValue)) {
                const QDateTime t = QDateTime::fromMSecsSinceEpoch(*ms + span.secs * 1000, target);
                o.end = {t.date(), QTime(t.time().hour(), t.time().minute())};
            } else {
                o.end = data.zones.toTarget(endValue, target).addSecs(span.secs);
            }
            if (o.end < o.start)
                o.end = o.start;
        }
        const bool overlaps = o.start < windowTo && (o.end > windowFrom || (o.end == o.start && o.start >= windowFrom));
        if (overlaps)
            out.append(o);
    }

    void series(const QString &uid, qsizetype master, const QList<qsizetype> &overrides)
    {
        const Event &m = data.events.at(master);
        if (m.cancelled())
            return;
        const Span span = spanOf(m, data.zones);
        // The window on the series' own clock, wide enough for events that started before it.
        NaiveDateTime sFrom;
        NaiveDateTime sTo;
        if (m.allDay()) {
            sFrom = {windowFrom.date.addDays(-span.days - 1), QTime(0, 0)};
            sTo = {windowTo.date.addDays(1), QTime(0, 0)};
        } else {
            sFrom = data.zones.inZoneOf(targetMs(windowFrom), m.start, target)
                            .addSecs(-span.secs - (span.days + 1) * 86400);
            sTo = data.zones.inZoneOf(targetMs(windowTo), m.start, target).addSecs(86400);
        }
        // THISANDFUTURE overrides, by the instance they name; a plain override of one instance
        // beats the range it falls in.
        QList<FutureOverride> future;
        QSet<QString> overridden;
        for (qsizetype i : overrides) {
            const Event &o = data.events.at(i);
            if (!o.recurrenceId)
                continue;
            if (!o.thisAndFuture) {
                overridden.insert(instanceKey(*o.recurrenceId, data.zones));
                continue;
            }
            FutureOverride f;
            f.index = i;
            f.from = orderKey(*o.recurrenceId, data.zones);
            if (o.allDay() == m.allDay() && !m.allDay()) {
                // On the series' clock: a move of the start by an hour stays an hour in summer.
                const auto ms = data.zones.instant(o.start);
                const auto rms = data.zones.instant(*o.recurrenceId);
                if (ms && rms) {
                    const NaiveDateTime a = data.zones.inZoneOf(*ms, m.start, target);
                    const NaiveDateTime r = data.zones.inZoneOf(*rms, m.start, target);
                    f.shiftSecs = r.secsTo(a);
                } else {
                    f.shiftSecs = NaiveDateTime{o.recurrenceId->date, o.recurrenceId->time.value_or(QTime(0, 0))}.secsTo(
                            {o.start.date, o.start.time.value_or(QTime(0, 0))});
                }
            } else if (o.allDay() && m.allDay()) {
                f.shiftSecs = o.recurrenceId->date.daysTo(o.start.date) * 86400;
            }
            future.append(f);
        }
        std::ranges::sort(future, {}, &FutureOverride::from);
        qint64 widen = 0; // a shifted instance may come into the window from outside it
        for (const FutureOverride &f : std::as_const(future))
            widen = std::max(widen, std::min<qint64>(qAbs(f.shiftSecs), 400LL * 86400));
        if (widen > 0) {
            sFrom = sFrom.addSecs(-widen);
            sTo = sTo.addSecs(widen);
        }
        QSet<QString> excluded;
        QSet<qint64> excludedDays; // EXDATE;VALUE=DATE on a timed series: the whole day
        for (const io::ical::DateTimeValue &ex : m.exdates) {
            if (!ex.time && m.start.time)
                excludedDays.insert(ex.date.toJulianDay());
            else
                excluded.insert(instanceKey(ex, data.zones));
        }
        const bool recurring = m.rrule.has_value() || !m.rdates.isEmpty() || !overrides.isEmpty();
        QList<io::ical::DateTimeValue> starts;
        const NaiveDateTime dtstart{m.start.date, m.start.time.value_or(QTime(0, 0))};
        if (m.rrule) {
            std::optional<NaiveDateTime> until;
            if (const auto &u = m.rrule->until) {
                if (!u->time) {
                    until = NaiveDateTime{u->date, QTime(23, 59, 59)};
                } else if (u->utc) {
                    const qint64 ms = QDateTime(u->date, *u->time, QTimeZone::UTC).toMSecsSinceEpoch();
                    until = m.allDay() ? NaiveDateTime{QDateTime::fromMSecsSinceEpoch(ms, target).date(), QTime(23, 59, 59)}
                                       : data.zones.inZoneOf(ms, m.start, target);
                } else {
                    until = NaiveDateTime{u->date, *u->time};
                }
            }
            bool cutShort = false;
            const QList<NaiveDateTime> list = expandRRule(*m.rrule, dtstart, m.allDay(), sFrom, sTo, until, limits, &cutShort);
            truncated = truncated || cutShort;
            for (const NaiveDateTime &t : list)
                starts.append(withNaive(m.start, t));
        } else if (dtstart >= sFrom && dtstart < sTo) {
            starts.append(m.start);
        }
        for (const io::ical::DateTimeValue &r : m.rdates) {
            io::ical::DateTimeValue v = r;
            if (m.allDay()) {
                v.time.reset();
                v.utc = false;
                v.tzid.clear();
            } else if (!r.time) {
                v = withNaive(m.start, {r.date, *m.start.time});
            } else if (const auto ms = data.zones.instant(r); ms && (r.utc || r.tzid != m.start.tzid)) {
                v = withNaive(m.start, data.zones.inZoneOf(*ms, m.start, target));
            }
            const NaiveDateTime n{v.date, v.time.value_or(QTime(0, 0))};
            if (n >= sFrom && n < sTo)
                starts.append(v);
        }
        QSet<QString> seen;
        for (const io::ical::DateTimeValue &s : std::as_const(starts)) {
            const QString key = instanceKey(s, data.zones);
            if (seen.contains(key) || overridden.contains(key) || excluded.contains(key)
                || excludedDays.contains(s.date.toJulianDay()))
                continue;
            seen.insert(key);
            const FutureOverride *range = nullptr;
            const qint64 order = orderKey(s, data.zones);
            for (const FutureOverride &f : std::as_const(future)) {
                if (f.from <= order)
                    range = &f; // the last one that began at or before this instance
            }
            if (range) {
                const Event &o = data.events.at(range->index);
                if (order == range->from)
                    usedFuture.insert(range->index);
                if (o.cancelled())
                    continue;
                io::ical::DateTimeValue moved = s;
                if (range->shiftSecs != 0) {
                    const NaiveDateTime n = NaiveDateTime{s.date, s.time.value_or(QTime(0, 0))}.addSecs(range->shiftSecs);
                    moved = withNaive(s, n);
                }
                // The span of the override when it is of the series' kind (a timed one in an
                // all-day series would be a different event).
                add(range->index, moved, o.allDay() == m.allDay() ? spanOf(o, data.zones) : span, s, true);
                continue;
            }
            add(master, s, span, recurring ? std::optional(s) : std::nullopt, recurring);
        }
        Q_UNUSED(uid);
    }
};

} // namespace

QString normalizedColor(QStringView value)
{
    const QString v = value.trimmed().toString().toLower();
    if (v.isEmpty())
        return {};
    static const QRegularExpression hex(u"^#([0-9a-f]{3}|[0-9a-f]{6}|[0-9a-f]{8})$"_s);
    if (hex.match(v).hasMatch()) {
        if (v.size() == 4) // #rgb
            return QString(u'#') + v.at(1) + v.at(1) + v.at(2) + v.at(2) + v.at(3) + v.at(3);
        return v.left(7); // Apple's #RRGGBBAA: the alpha dropped
    }
    // CSS3 colour names (RFC 7986 COLOR): the ones calendars use.
    static const QHash<QString, QString> names = {
            {u"black"_s, u"#000000"_s},   {u"white"_s, u"#ffffff"_s},     {u"red"_s, u"#ff0000"_s},
            {u"green"_s, u"#008000"_s},   {u"blue"_s, u"#0000ff"_s},      {u"yellow"_s, u"#ffff00"_s},
            {u"orange"_s, u"#ffa500"_s},  {u"purple"_s, u"#800080"_s},    {u"pink"_s, u"#ffc0cb"_s},
            {u"brown"_s, u"#a52a2a"_s},   {u"gray"_s, u"#808080"_s},      {u"grey"_s, u"#808080"_s},
            {u"navy"_s, u"#000080"_s},    {u"teal"_s, u"#008080"_s},      {u"olive"_s, u"#808000"_s},
            {u"maroon"_s, u"#800000"_s},  {u"lime"_s, u"#00ff00"_s},      {u"aqua"_s, u"#00ffff"_s},
            {u"cyan"_s, u"#00ffff"_s},    {u"fuchsia"_s, u"#ff00ff"_s},   {u"magenta"_s, u"#ff00ff"_s},
            {u"silver"_s, u"#c0c0c0"_s},  {u"gold"_s, u"#ffd700"_s},      {u"indigo"_s, u"#4b0082"_s},
            {u"violet"_s, u"#ee82ee"_s},  {u"coral"_s, u"#ff7f50"_s},     {u"salmon"_s, u"#fa8072"_s},
            {u"tomato"_s, u"#ff6347"_s},  {u"crimson"_s, u"#dc143c"_s},   {u"turquoise"_s, u"#40e0d0"_s},
            {u"skyblue"_s, u"#87ceeb"_s}, {u"steelblue"_s, u"#4682b4"_s}, {u"darkgreen"_s, u"#006400"_s},
            {u"darkblue"_s, u"#00008b"_s}, {u"darkred"_s, u"#8b0000"_s},  {u"darkorange"_s, u"#ff8c00"_s},
    };
    return names.value(v);
}

std::optional<Event> eventFromComponent(const io::ical::Component &c, int maxTextLength)
{
    Event e;
    e.uid = c.text(u"UID").trimmed();
    const io::ical::Property *start = c.property(u"DTSTART");
    if (e.uid.isEmpty() || !start)
        return std::nullopt;
    const auto startValue = io::ical::parseDateTime(*start);
    if (!startValue)
        return std::nullopt;
    e.start = *startValue;
    e.summary = cut(c.text(u"SUMMARY").simplified(), maxTextLength);
    e.description = cut(c.text(u"DESCRIPTION"), maxTextLength);
    e.location = cut(c.text(u"LOCATION").simplified(), maxTextLength);
    e.url = cut(c.text(u"URL").trimmed(), 2000);
    e.status = c.text(u"STATUS").trimmed().toUpper();
    e.color = normalizedColor(c.text(u"COLOR"));
    e.sequence = c.text(u"SEQUENCE").trimmed().toInt();
    e.transparent = c.text(u"TRANSP").trimmed().compare(u"TRANSPARENT"_s, Qt::CaseInsensitive) == 0;
    if (const io::ical::Property *end = c.property(u"DTEND")) {
        if (const auto v = io::ical::parseDateTime(*end))
            e.end = *v;
    }
    if (!e.end) {
        if (const io::ical::Property *d = c.property(u"DURATION"))
            e.duration = io::ical::parseDurationParts(d->value);
    }
    if (const io::ical::Property *r = c.property(u"RRULE")) {
        e.rruleText = r->value.trimmed();
        e.rrule = parseRRule(e.rruleText);
    }
    for (const io::ical::Property &p : c.properties) {
        if (p.name == u"RDATE")
            e.rdates += dateList(p);
        else if (p.name == u"EXDATE")
            e.exdates += dateList(p);
        else if (p.name == u"CATEGORIES") {
            for (const QStringView cat : qTokenize(QStringView(p.value), u',', Qt::SkipEmptyParts)) {
                const QString t = io::ical::unescapeText(cat).trimmed();
                if (!t.isEmpty() && !e.categories.contains(t))
                    e.categories.append(t);
            }
        }
    }
    if (const io::ical::Property *rid = c.property(u"RECURRENCE-ID")) {
        e.recurrenceId = io::ical::parseDateTime(*rid);
        e.thisAndFuture = rid->param(u"RANGE"_s).compare(u"THISANDFUTURE"_s, Qt::CaseInsensitive) == 0;
    }
    for (const io::ical::Component *a : c.childrenNamed(u"VALARM")) {
        const io::ical::Property *trigger = a->property(u"TRIGGER");
        if (!trigger)
            continue;
        EventAlarm alarm;
        alarm.audio = a->text(u"ACTION").trimmed().compare(u"AUDIO"_s, Qt::CaseInsensitive) == 0;
        if (trigger->param(u"VALUE"_s).compare(u"DATE-TIME"_s, Qt::CaseInsensitive) == 0
            || !trigger->value.contains(u'P')) {
            alarm.absolute = io::ical::parseDateTime(*trigger);
        } else {
            alarm.relative = io::ical::parseDurationParts(trigger->value);
            alarm.fromEnd = trigger->param(u"RELATED"_s).compare(u"END"_s, Qt::CaseInsensitive) == 0;
        }
        if (alarm.absolute || alarm.relative)
            e.alarms.append(alarm);
    }
    return e;
}

void appendCalendarData(CalendarData &into, QByteArrayView ics, const QString &href, const ParseLimits &limits)
{
    if (ics.size() > limits.maxBytes) {
        into.truncated = true;
        return;
    }
    QStringList warnings;
    const QList<io::ical::Component> roots = io::ical::parse(ics, &warnings);
    if (!warnings.isEmpty())
        into.malformed = true;
    for (const io::ical::Component &root : roots) {
        if (root.name != u"VCALENDAR") {
            into.malformed = true;
            continue;
        }
        into.zones.addCalendar(root);
        if (into.name.isEmpty()) {
            into.name = root.text(u"X-WR-CALNAME").simplified();
            if (into.name.isEmpty())
                into.name = root.text(u"NAME").simplified();
            into.name = cut(into.name, 200);
        }
        if (into.description.isEmpty())
            into.description = cut(root.text(u"X-WR-CALDESC").trimmed(), 2000);
        if (into.color.isEmpty()) {
            into.color = normalizedColor(root.text(u"COLOR"));
            if (into.color.isEmpty())
                into.color = normalizedColor(root.text(u"X-APPLE-CALENDAR-COLOR"));
        }
        for (const io::ical::Component &c : root.children) {
            if (c.name != u"VEVENT")
                continue;
            if (into.events.size() >= limits.maxEvents) {
                into.truncated = true;
                ++into.skipped;
                continue;
            }
            if (auto e = eventFromComponent(c, limits.maxTextLength)) {
                e->href = href;
                into.events.append(std::move(*e));
            } else {
                ++into.skipped;
            }
        }
    }
}

CalendarData parseCalendarData(QByteArrayView ics, const ParseLimits &limits)
{
    CalendarData data;
    appendCalendarData(data, ics, {}, limits);
    return data;
}

QDate Occurrence::lastDay() const
{
    if (allDay)
        return std::max(start.date, end.date.addDays(-1));
    if (end.date > start.date && end.time == QTime(0, 0))
        return end.date.addDays(-1);
    return std::max(start.date, end.date);
}

QList<Occurrence> occurrences(const CalendarData &data, QDate from, QDate to, const QTimeZone &target,
                              const ExpandLimits &limits, bool *truncated)
{
    if (truncated)
        *truncated = false;
    if (!from.isValid() || !to.isValid() || from > to)
        return {};
    Builder b{data, target, {from, QTime(0, 0)}, {to.addDays(1), QTime(0, 0)}, limits, false, {}, {}};
    // Masters and overrides of each UID, in file order.
    QHash<QString, std::pair<qsizetype, QList<qsizetype>>> byUid;
    QStringList order;
    for (qsizetype i = 0; i < data.events.size(); ++i) {
        const Event &e = data.events.at(i);
        auto it = byUid.find(e.uid);
        if (it == byUid.end()) {
            it = byUid.insert(e.uid, {-1, {}});
            order.append(e.uid);
        }
        if (e.recurrenceId)
            it->second.append(i);
        else if (it->first < 0)
            it->first = i;
    }
    for (const QString &uid : std::as_const(order)) {
        const auto &[master, overrides] = byUid.value(uid);
        const bool masterCancelled = master >= 0 && data.events.at(master).cancelled();
        if (master >= 0)
            b.series(uid, master, overrides);
        if (masterCancelled)
            continue;
        for (qsizetype i : overrides) {
            const Event &e = data.events.at(i);
            if (e.cancelled() || b.usedFuture.contains(i))
                continue;
            b.add(i, e.start, spanOf(e, data.zones), e.recurrenceId, master >= 0);
        }
        if (b.out.size() > 20'000) {
            b.truncated = true;
            break;
        }
    }
    std::ranges::stable_sort(b.out, [&data](const Occurrence &a, const Occurrence &c) {
        if (a.start.date != c.start.date)
            return a.start.date < c.start.date;
        if (a.allDay != c.allDay)
            return a.allDay;
        if (a.start.time != c.start.time)
            return a.start.time < c.start.time;
        return data.events.at(a.event).summary.compare(data.events.at(c.event).summary, Qt::CaseInsensitive) < 0;
    });
    if (truncated)
        *truncated = b.truncated;
    return b.out;
}

} // namespace brulion::caldav
