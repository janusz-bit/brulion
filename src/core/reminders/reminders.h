// brulion reminders module — notifications for `⏰` reminders and `⏰!` alarms, the in-app
// reminder scheduler, calendar entries and snoozing (docs/SYNTAX.md §5-§7). Include this header
// to get the whole module (namespace brulion; nothing reads the system clock except the
// scheduler's default clock — `now` is always injectable).
//
// API overview:
//
//   notifications.h  struct ScheduledNotification { id (stable: hash(path|at|hash(text))), nativeId
//                      (positive 31-bit, unique within a batch), title (block text, plain,
//                      <= 120 chars, else page title), body (page title + due info), at (local),
//                      alarm, ringMinutes = 10, path, line }
//                    class NotificationSink (abstract; implemented by the platform layer):
//                      firesWhenClosed(), schedule(list) (replaces the whole set), showNow(item)
//                    upcomingNotifications(index, now, horizonDays = 14, max = 64,
//                      int *dropped = nullptr, locale = Polish) — done/cancelled tasks skipped,
//                      one-off reminders within [now - 1 min, now + horizon] (local time), 🔁
//                      series expanded with dates::occurrencesBetween (time kept; a recurring
//                      task's reminder keeps its distance to the task's due/scheduled/start
//                      date), sorted, nativeIds assigned
//                    notificationsBetween(index, from, to, locale) — same, explicit window, no limit
//                    notificationId(path, at, text), nativeIdFor(id), assignNativeIds(list)
//                    class NotificationCache — the occurrences of a window of days kept per note
//                      across index changes: setDays(first, last), between(), upcoming() (= the
//                      functions above; requests outside the days computed directly)
//
//   scheduler.h      class ReminderScheduler : QObject — (VaultIndex*, NotificationSink*,
//                      LocalStore*, Clock = QDateTime::currentDateTime)
//                      start() / stop() / refresh() (debounced on index changes) / checkNow()
//                      settings: horizonDays, maxScheduled, alarmRingMinutes, locale
//                      one NotificationCache of [today − 8, today + horizon + 1] for every pass
//                      (a refresh after a save computes the saved note only)
//                      in-app sinks (!firesWhenClosed): QTimer for the next reminder within
//                      24 h (re-planned hourly), 30 s heartbeat for sleep / clock jumps, fire
//                      once (handled ids persisted, pruned after 7 days), > 6 h late -> missed
//                      missed() since the app last ran (lastSeen persisted), dismissMissed(id),
//                      clearMissed(); upcoming(), nextFireTime(), droppedCount()
//                      signals fired(notification), missedChanged(), upcomingChanged()
//
//   calendar.h       struct CalendarItem { date, optional time, kind (Journal, Due, Scheduled,
//                      Start, Reminder, Done), title, path, line, optional status, recurring,
//                      alarm }
//                    calendarItems(index, from, to) — journals, task dates (series of tasks
//                      that are not done/cancelled expanded; dates keep their distance to the
//                      due/scheduled/start reference), done dates, reminders (expanded);
//                      sorted by date, timed items by time, then all-day; stable
//                    class CalendarCache — items(index, from, to) kept across index changes
//                      (= calendarItems(); only the changed notes computed again)
//
//   snooze.h         snoozeReminder(content, line, newAt) -> content — replaces the first ⏰
//                      token of the line (keeping '!'); a recurring reminder instead gets a
//                      one-off child bullet `- ⏰ newAt` (the series stays untouched)
#pragma once

#include "reminders/calendar.h"
#include "reminders/notifications.h"
#include "reminders/scheduler.h"
#include "reminders/snooze.h"
