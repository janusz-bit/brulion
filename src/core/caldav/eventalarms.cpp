#include "caldav/eventalarms.h"

#include "caldav/eventedit.h"
#include "dates/dates.h"

#include <QSet>

#include <algorithm>

namespace brulion::caldav {

using namespace Qt::StringLiterals;

namespace {

/// A wall-clock time of the occurrence's clock (the device's) as a QDateTime in `zone`.
QDateTime inZone(const NaiveDateTime &t, const QTimeZone &zone)
{
    return QDateTime(t.date, t.time, zone);
}

/// The local wall-clock time (Qt::LocalTime, seconds 0) of an instant seen in `zone`: what the
/// reminders module calls local time.
QDateTime asLocal(const QDateTime &t, const QTimeZone &zone)
{
    const QDateTime z = t.toTimeZone(zone);
    return QDateTime(z.date(), QTime(z.time().hour(), z.time().minute()));
}

QString instanceKeyText(const std::optional<io::ical::DateTimeValue> &v)
{
    return v ? formatDateTimeValue(*v) : QString();
}

} // namespace

QList<ScheduledNotification> eventNotifications(const CalendarData &data, const QString &calendarId,
                                                const QString &calendarName, const QDateTime &from,
                                                const QDateTime &to, const QTimeZone &zoneGiven, dates::UiLocale locale,
                                                const EventAlarmLimits &limits)
{
    QList<ScheduledNotification> out;
    const QTimeZone zone = zoneGiven.isValid() ? zoneGiven : QTimeZone::systemTimeZone();
    if (!from.isValid() || !to.isValid() || from > to || data.events.isEmpty())
        return out;
    const bool anyAlarm = std::ranges::any_of(data.events, [](const Event &e) { return !e.alarms.isEmpty(); });
    if (!anyAlarm)
        return out;
    const QDateTime fromLocal = from.toLocalTime();
    const QDateTime toLocal = to.toLocalTime();
    const int lead = std::clamp(limits.maxLeadDays, 0, 366);
    const QDate firstDay = fromLocal.date().addDays(-lead - 1);
    const QDate lastDay = toLocal.date().addDays(lead + 1);
    if (firstDay.daysTo(lastDay) > 800)
        return out;
    const QList<Occurrence> list = occurrences(data, firstDay, lastDay, zone);
    const QString path = EventNotificationPrefix + calendarId;
    const QString noTitle = locale == dates::UiLocale::Polish ? u"(bez tytułu)"_s : u"(no title)"_s;
    QSet<QString> seen;
    for (const Occurrence &o : list) {
        const Event &e = data.events.at(o.event);
        int taken = 0;
        for (qsizetype i = 0; i < e.alarms.size() && taken < limits.maxPerEvent; ++i) {
            const EventAlarm &a = e.alarms.at(i);
            if (!a.action.isEmpty() && a.action != u"DISPLAY" && a.action != u"AUDIO")
                continue;
            ++taken;
            QDateTime ring;
            if (a.relative) {
                if (qAbs(a.relative->days) > lead || qAbs(a.relative->seconds) > qint64(lead + 1) * 86400)
                    continue;
                const NaiveDateTime base = a.fromEnd ? o.end : o.start;
                ring = inZone(base, zone).addDays(a.relative->days).addSecs(a.relative->seconds);
            } else if (a.absolute && !o.recurring) {
                const auto ms = data.zones.instant(*a.absolute);
                if (ms)
                    ring = QDateTime::fromMSecsSinceEpoch(*ms, zone);
                else if (a.absolute->time)
                    ring = QDateTime(a.absolute->date, *a.absolute->time, zone);
                else
                    ring = QDateTime(a.absolute->date, QTime(0, 0), zone);
            } else {
                continue;
            }
            if (!ring.isValid())
                continue;
            const QDateTime at = asLocal(ring, zone);
            if (at < fromLocal || at > toLocal)
                continue;
            ScheduledNotification n;
            n.at = at;
            n.alarm = a.audio;
            n.title = e.summary.isEmpty() ? noTitle : e.summary;
            if (n.title.size() > ScheduledNotification::MaxTitleLength)
                n.title = n.title.left(ScheduledNotification::MaxTitleLength - 1) + u'…';
            const DateSpec when{o.start.date, o.allDay ? std::nullopt : std::optional<QTime>(o.start.time)};
            QString body = calendarName.isEmpty() ? QString() : calendarName + u" · "_s;
            body += dates::formatHuman(when, locale, at.date());
            if (!e.location.isEmpty())
                body += u" · "_s + e.location;
            n.body = body;
            n.path = path;
            n.line = 0;
            const QString text = QString(e.uid + u'|' + instanceKeyText(o.instance) + u'|' + o.start.date.toString(Qt::ISODate)
                                         + u'T' + o.start.time.toString(u"HH:mm"_s) + u'|' + QString::number(i));
            n.id = notificationId(path, at, text);
            if (seen.contains(n.id))
                continue;
            seen.insert(n.id);
            out.append(n);
            if (out.size() >= limits.maxTotal)
                break;
        }
        if (out.size() >= limits.maxTotal)
            break;
    }
    std::ranges::stable_sort(out, [](const ScheduledNotification &a, const ScheduledNotification &b) {
        return a.at != b.at ? a.at < b.at : a.id < b.id;
    });
    return out;
}

QList<ScheduledNotification> eventNotifications(const CalendarStore &store, const QDateTime &from, const QDateTime &to,
                                                const QTimeZone &zone, dates::UiLocale locale,
                                                const EventAlarmLimits &limits)
{
    QList<ScheduledNotification> out;
    const CalendarSettings settings = store.settings();
    for (const CalendarEntry &c : settings.calendars) {
        if (!c.showEvents || !c.alarms)
            continue;
        const CalendarState state = store.state(c.id);
        CalendarData data;
        for (auto it = state.resources.constBegin(); it != state.resources.constEnd(); ++it) {
            if (it->kind == u"VEVENT")
                appendCalendarData(data, it->data, it.key());
        }
        out += eventNotifications(data, c.id, c.name, from, to, zone, locale, limits);
    }
    std::ranges::stable_sort(out, [](const ScheduledNotification &a, const ScheduledNotification &b) {
        return a.at != b.at ? a.at < b.at : a.id < b.id;
    });
    return out;
}

} // namespace brulion::caldav
