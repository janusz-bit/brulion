// brulion caldav module — calendars of other programs: ICS subscriptions shown in the calendar and
// CalDAV calendars synced both ways (docs/UI.md "Calendars", docs/SYNTAX.md §5 "Tasks synced with
// CalDAV"). Include this header to get the whole module (namespace brulion::caldav; HTTP only through
// sync::HttpTransport, the clock and the device's zone injectable).
//
// API overview:
//
//   rrule.h        RRule (RFC 5545 §3.3.10 in full: every FREQ, COUNT, UNTIL, BYxxx, BYSETPOS, WKST),
//                  parseRRule / formatRRule, expandRRule(rule, dtstart, dateOnly, from, to, until,
//                  ExpandLimits) — the series' own wall clock, bounded (hostile rules end), windows
//                  far from DTSTART skip the periods before them; NaiveDateTime
//   zones.h        ZoneDefinition (a VTIMEZONE: observances with DTSTART, RRULE, RDATE, offsets;
//                  offsetAtUtc, toUtc — gaps and repeated hours), parseVTimezone, parseUtcOffset,
//                  systemZoneFor(tzid) (IANA, /mozilla.org/… prefixes, Windows names), ZoneTable
//                  (a calendar's TZIDs: instant(), toTarget(), inZoneOf())
//   events.h       Event (a VEVENT: times, RRULE, RDATE, EXDATE, RECURRENCE-ID, status, colour,
//                  alarms), CalendarData (a feed's or a calendar's events, name, colour, zones,
//                  limits hit), appendCalendarData / parseCalendarData (ParseLimits),
//                  occurrences(data, from, to, targetZone) -> Occurrence (start, exclusive end,
//                  all-day, the instance an exception would name), normalizedColor
//   feed.h         feedUrl (webcal:// -> https://), looksLikeCalendar, FeedFetcher (GET with
//                  If-None-Match / If-Modified-Since, redirects by hand, size limit, an HTML page is
//                  no calendar, CORS told apart in the browser), FeedHttpTransport (redirects to
//                  other servers: no credentials travel)
//   client.h       CalDavClient: discover (the address, /.well-known/caldav, current-user-principal,
//                  calendar-home-set, the calendars: CalendarInfo), listCalendars, calendarState,
//                  changes (sync-collection; else every member's ETag: ChangeSet::complete), fetch
//                  (calendar-multiget in batches, else GET), put (If-Match / If-None-Match: *),
//                  remove; Basic and Digest authentication; mayCarryCredentials (the password stays
//                  on the account's server or its site over https)
//   taskmap.h      TaskFields (summary, status, due, start, priority, completed, recurrence,
//                  reminders, trailing #tags as categories, description, parent), changedFields /
//                  withFields, fieldsFromBlock / fieldsFromTodo, newTaskBody / applyFieldsToBody (a
//                  task line edited token by token), newTodo / patchTodo (the server's VTODO
//                  patched: what Brulion does not know stays), blockIdForUid / uidForBlockId /
//                  newTaskBlockId (how a line keeps its UID), serialize, vtimezoneFor
//   eventedit.h    EventEdit (the event dialog), newEvent / patchEvent (the series) / patchOccurrence (an
//                  override) / excludeOccurrence (EXDATE): what the server has and Brulion does not
//                  show stays
//   eventalarms.h  the VALARMs of events as notifications the reminder scheduler rings (relative to
//                  the start or end of every occurrence, or absolute; per calendar on / off)
//   store.h        CalendarStore (<device state>/calendars/: settings — accounts, calendars,
//                  subscriptions —, feed caches with validators, per-calendar state — sync token,
//                  members, tracked tasks, the outbox of events —, the log)
//   calendarsync.h CalendarSync (a sync::SyncRunner): subscriptions that are due, every calendar
//                  (outbox, changes, tasks reconciled with the notes, conflicts by the account's
//                  policy, deletions behind a guard), queueEvent(), discover(); CalendarRunReport
#pragma once

#include "caldav/calendarsync.h"
#include "caldav/client.h"
#include "caldav/eventalarms.h"
#include "caldav/eventedit.h"
#include "caldav/events.h"
#include "caldav/feed.h"
#include "caldav/rrule.h"
#include "caldav/store.h"
#include "caldav/taskmap.h"
#include "caldav/zones.h"
