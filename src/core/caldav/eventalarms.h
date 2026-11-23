// The VALARMs of events as notifications (docs/UI.md "Calendars"): a reminder of an event of a
// CalDAV calendar rings in Brulion like a `⏰` of a note does — the ReminderScheduler takes these
// as an extra source and hands them to the platform's notification sink (desktop, Android alarms,
// the browser). Part of the caldav module — see caldav/caldav.h.
#pragma once

#include "caldav/events.h"
#include "caldav/store.h"
#include "dates/names.h"
#include "reminders/notifications.h"

#include <QDateTime>
#include <QList>
#include <QString>
#include <QTimeZone>

namespace brulion::caldav {

/// The prefix of the path of an event's notification ("calendar:2026-11-20": the day of the event;
/// tapping it opens the calendar there).
inline const QString EventNotificationPrefix = QStringLiteral("calendar:");

/// Limits of one computation: a hostile or broken calendar cannot make the scheduler's work grow.
struct EventAlarmLimits {
    int maxPerEvent = 5;          ///< the first alarms of an event (the ones after are not rung)
    int maxTotal = 2000;          ///< notifications of one computation
    int maxLeadDays = 31;         ///< a trigger further than this from the start / end is ignored
};

/// The notifications of the alarms of `data`'s events (the calendar `calendarId`, shown as
/// `calendarName`) that ring within [from, to] (inclusive, local wall-clock time of `zone` — the
/// device's), sorted by time then id, nativeIds not assigned.
///
/// * every occurrence of a series has its own alarms (an override's VALARMs replace the series');
///   cancelled events and instances ring nothing;
/// * a TRIGGER relative to the start (or, RELATED=END, the end) rings that long before / after it
///   — measured in exact time, a trigger in days on the wall clock; an absolute TRIGGER rings once
///   and only for an event that does not recur;
/// * ACTION DISPLAY (or none) is a reminder, AUDIO a loud alarm (`⏰!`); EMAIL and the like ring
///   nothing;
/// * title: the event's title; body: the calendar, when the event is ("today 17:00") and where;
///   path: EventNotificationPrefix + the event's first day; id: stable for one alarm of one
///   occurrence.
QList<ScheduledNotification> eventNotifications(const CalendarData &data, const QString &calendarId,
                                                const QString &calendarName, const QDateTime &from,
                                                const QDateTime &to, const QTimeZone &zone,
                                                dates::UiLocale locale = dates::UiLocale::Polish,
                                                const EventAlarmLimits &limits = {});

/// The same for every CalDAV calendar of `store` that shows events and has its alarms on, read
/// from the members the last sync kept (the Android background check, which has no calendar view
/// loaded). Subscriptions ring nothing.
QList<ScheduledNotification> eventNotifications(const CalendarStore &store, const QDateTime &from, const QDateTime &to,
                                                const QTimeZone &zone, dates::UiLocale locale = dates::UiLocale::Polish,
                                                const EventAlarmLimits &limits = {});

} // namespace brulion::caldav
