// Events written by Brulion (docs/UI.md "Calendars" — the event dialog): a new VEVENT, the
// server's one changed (only what the dialog shows; everything else of it kept), one occurrence
// of a series changed (an override with its RECURRENCE-ID) or left out (EXDATE). Wall-clock times
// are written with the device's zone (TZID and a VTIMEZONE). Part of the caldav module — see
// caldav/caldav.h.
#pragma once

#include "caldav/events.h"
#include "caldav/taskmap.h"
#include "io/ical.h"

#include <QByteArray>
#include <QDate>
#include <QString>
#include <QTime>

#include <functional>
#include <optional>

namespace brulion::caldav {

/// What the event dialog edits.
struct EventEdit {
    QString title;
    QString location;
    QString description;
    QDate startDate;
    std::optional<QTime> startTime; ///< none: an all-day event
    QDate endDate;                  ///< the last day (all-day: inclusive)
    std::optional<QTime> endTime;
    QString rule;             ///< Brulion's rule ("every week"), "" none
    bool ruleChanged = true;  ///< false: the RRULE stays as the server has it
    int reminderMinutes = -1; ///< a DISPLAY alarm this long before the start; -1 none
    bool reminderChanged = true;

    bool allDay() const { return !startTime.has_value(); }
};

/// A VCALENDAR with one new VEVENT.
QByteArray newEvent(const QString &uid, const EventEdit &edit, const TodoContext &context);
/// `ics` with the series (or single event) `uid` changed as `edit` says; empty when it has none.
QByteArray patchEvent(const QByteArray &ics, const QString &uid, const EventEdit &edit, const TodoContext &context);
/// `ics` with one occurrence of the series `uid` (its original start `instance`) changed: its
/// override written (or changed); empty when the series is not there.
QByteArray patchOccurrence(const QByteArray &ics, const QString &uid, const io::ical::DateTimeValue &instance,
                           const EventEdit &edit, const TodoContext &context);
/// `ics` without one occurrence of the series `uid`: an EXDATE on the series, its override (if
/// any) removed; empty when the series is not there.
QByteArray excludeOccurrence(const QByteArray &ics, const QString &uid, const io::ical::DateTimeValue &instance,
                             const TodoContext &context);
/// `ics` with the left-out occurrences (EXDATE) and the exceptions (RECURRENCE-ID) of the series
/// `uid` named anew: `rename` gives each one's new start on the device's clock, written the way
/// DTSTART is. A series whose start moved keeps them naming its occurrences. `ics` as it was when
/// the series is not there.
QByteArray renameInstances(const QByteArray &ics, const QString &uid,
                           const std::function<DateSpec(const io::ical::DateTimeValue &)> &rename,
                           const TodoContext &context);
/// A DATE / DATE-TIME value as RECURRENCE-ID and EXDATE write it ("20261120T090000" with its TZID).
/// `ics` with the answer `answer` (Accepted, Declined or Tentative) of the attendee whose address
/// is one of `addresses` (participantAddress() form) to the event `uid`: its ATTENDEE's PARTSTAT
/// set in the series and in each override naming it, RSVP dropped, DTSTAMP now, SEQUENCE kept (an
/// attendee's change: RFC 5546 §2.1.4) — on a CalDAV server with scheduling (RFC 6638) that is the
/// reply the organizer gets. Empty when there is no such event or attendee, or `answer` is none.
QByteArray answerInvitation(const QByteArray &ics, const QString &uid, const QStringList &addresses,
                            Participation answer, const TodoContext &context);

QString formatDateTimeValue(const io::ical::DateTimeValue &value);

} // namespace brulion::caldav
