// Events of calendars Brulion shows but does not keep in notes (docs/UI.md "Calendars"): the
// VEVENTs of an ICS subscription or of a CalDAV calendar, parsed within limits (a hostile or huge
// feed cannot exhaust the device) and expanded into the occurrences of a range of days in the
// device's wall clock — recurrence (RRULE, RDATE, EXDATE), exceptions (RECURRENCE-ID overrides,
// cancelled instances), time zones (VTIMEZONE, IANA, Windows names), all-day and multi-day
// events. Part of the caldav module — see caldav/caldav.h.
#pragma once

#include "caldav/rrule.h"
#include "caldav/zones.h"
#include "io/ical.h"

#include <QByteArrayView>
#include <QDate>
#include <QList>
#include <QString>
#include <QStringList>
#include <QTimeZone>

#include <optional>

namespace brulion::caldav {

/// A VALARM of an event (what a reminder of it would ring at).
struct EventAlarm {
    std::optional<io::ical::Duration> relative; ///< TRIGGER as a duration
    bool fromEnd = false;                        ///< RELATED=END
    std::optional<io::ical::DateTimeValue> absolute;
    bool audio = false; ///< ACTION:AUDIO
    QString action;     ///< ACTION in upper case ("" when missing)
};

/// An attendee's answer to an invitation (PARTSTAT, RFC 5545 §3.2.12).
enum class Participation : quint8 { NeedsAction, Accepted, Declined, Tentative, Delegated };

/// ORGANIZER or ATTENDEE of an event (RFC 5545 §3.8.4.1, §3.8.4.3).
struct Participant {
    /// Its calendar address: an e-mail ("mailto:" dropped, in lower case), else the URI as written.
    QString address;
    QString name;                                       ///< CN ("" none)
    Participation status = Participation::NeedsAction; ///< PARTSTAT (an attendee's)
    QString role = QStringLiteral("REQ-PARTICIPANT");   ///< ROLE in upper case
    QString kind = QStringLiteral("INDIVIDUAL");        ///< CUTYPE in upper case (ROOM, GROUP …)
    friend bool operator==(const Participant &, const Participant &) = default;
};

/// The attendees of one event kept at most (a mailing list invited: the rest left out).
inline constexpr int MaxAttendees = 200;
/// A participant's address is cut here (an e-mail address has at most 254 characters).
inline constexpr int MaxAddressLength = 320;

/// A calendar address as Brulion compares it: "mailto:" dropped and in lower case for an e-mail,
/// else as written (trimmed).
QString participantAddress(const QString &value);
/// PARTSTAT's value (any case; unknown and "" are NeedsAction).
Participation participationOf(const QString &partstat);

/// One VEVENT: a single event, a recurring series (its master) or an exception of a series (an
/// override with a RECURRENCE-ID).
struct Event {
    QString uid;
    QString href; ///< the CalDAV resource holding it ("" for a subscription's)
    QString summary;
    QString description;
    QString location;
    QString url;
    QString status; ///< TENTATIVE, CONFIRMED, CANCELLED (upper case) or ""
    QStringList categories;
    QString color; ///< COLOR (RFC 7986), "" when none
    io::ical::DateTimeValue start;
    std::optional<io::ical::DateTimeValue> end;
    std::optional<io::ical::Duration> duration;
    std::optional<RRule> rrule;
    QString rruleText; ///< RRULE as written ("" none; set but `rrule` empty: not understood)
    QList<io::ical::DateTimeValue> rdates;
    QList<io::ical::DateTimeValue> exdates;
    std::optional<io::ical::DateTimeValue> recurrenceId;
    bool thisAndFuture = false; ///< RECURRENCE-ID;RANGE=THISANDFUTURE
    int sequence = 0;
    bool transparent = false; ///< TRANSP:TRANSPARENT (free time)
    QList<EventAlarm> alarms;
    Participant organizer;          ///< ORGANIZER (address "" when the event is no invitation)
    QList<Participant> attendees;   ///< ATTENDEEs with an address, in their order

    bool allDay() const { return !start.time; }
    bool cancelled() const { return status == u"CANCELLED"; }
};

/// What a calendar holds that Brulion shows.
struct CalendarData {
    QString name;        ///< X-WR-CALNAME
    QString description; ///< X-WR-CALDESC
    QString color;       ///< COLOR / X-APPLE-CALENDAR-COLOR as "#rrggbb", "" none
    QList<Event> events;
    ZoneTable zones;
    int skipped = 0;        ///< VEVENTs without UID or start, or beyond the limits
    bool truncated = false; ///< the limits stopped the parse
    bool malformed = false; ///< lines or components were ignored
};

/// Bounds of one parse.
struct ParseLimits {
    qsizetype maxBytes = 32LL * 1024 * 1024;
    int maxEvents = 50'000;
    int maxTextLength = 20'000; ///< summary, description, location are cut here
};

/// Parse one iCalendar object (a feed, a CalDAV resource) and add its events to `into` (its
/// VTIMEZONEs into `into.zones`; the name and colour only when `into` has none). `href`: the
/// CalDAV resource, stamped on the events. Data larger than `limits.maxBytes` is refused
/// (`truncated`); data that is no calendar adds nothing.
void appendCalendarData(CalendarData &into, QByteArrayView ics, const QString &href = {}, const ParseLimits &limits = {});
CalendarData parseCalendarData(QByteArrayView ics, const ParseLimits &limits = {});
/// One VEVENT component -> an Event (nullopt without UID or DTSTART). Texts cut at
/// `maxTextLength`.
std::optional<Event> eventFromComponent(const io::ical::Component &vevent, int maxTextLength = 20'000);
/// "#rrggbb" of a colour value ("#RRGGBB", "#RRGGBBAA" as Apple writes it, CSS3 names of RFC
/// 7986); "" when not one.
QString normalizedColor(QStringView value);

/// One occurrence of an event within a range, in the device's wall clock.
struct Occurrence {
    qsizetype event = -1; ///< index into CalendarData::events (the master, or the override)
    QString uid;
    /// The occurrence's original start in the series' own form (the RECURRENCE-ID an exception of
    /// it would carry); unset for events that do not recur.
    std::optional<io::ical::DateTimeValue> instance;
    NaiveDateTime start;
    /// Exclusive: an all-day event of one day ends at 00:00 of the next day; an event without
    /// DTEND / DURATION ends where it starts (all-day ones after one day).
    NaiveDateTime end;
    bool allDay = false;
    bool recurring = false; ///< part of a series (also an override)

    /// The days this occurrence shows on: start's to the last day it covers.
    QDate lastDay() const;
};

/// The occurrences of `data`'s events that overlap the days [from, to] (inclusive) in the
/// wall clock of `target`, sorted by start (all-day first within a day), then title. Cancelled
/// events and instances are left out; a series contributes at most `limits.maxOccurrences`
/// (`truncated` is set when a limit cut something).
QList<Occurrence> occurrences(const CalendarData &data, QDate from, QDate to, const QTimeZone &target,
                              const ExpandLimits &limits = {}, bool *truncated = nullptr);

} // namespace brulion::caldav
