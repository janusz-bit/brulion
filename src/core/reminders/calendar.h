// Calendar entries of a vault: daily notes, task dates and reminders within a date range.
// Part of the reminders module — see reminders/reminders.h.
#pragma once

#include "types.h"

#include <QDate>
#include <QList>
#include <QMetaType>
#include <QString>
#include <QTime>

#include <optional>

namespace brulion {

class VaultIndex;

/// One entry of the calendar.
struct CalendarItem {
    enum Kind {
        Journal,   ///< a daily note exists for the date
        Due,       ///< task due date (📅, DEADLINE)
        Scheduled, ///< task scheduled date (⏳, SCHEDULED)
        Start,     ///< task start date (🛫)
        Reminder,  ///< reminder or alarm (⏰, ⏰!, (@…))
        Done,      ///< task completion date (✅)
    };

    QDate date;
    std::optional<QTime> time; ///< reminders always have one; task dates when written ("📅 2026-10-15 18:00")
    Kind kind = Journal;
    /// Plain text: the page title for journals, the task text for task dates, the block text
    /// for reminders (page title when empty). Not elided.
    QString title;
    VaultPath path;
    int line = 0;                     ///< 0-based line to open (0 for journals)
    std::optional<TaskStatus> status; ///< the task's status (task items, reminders on tasks)
    bool recurring = false;           ///< the item belongs to a series (🔁 rule)
    bool alarm = false;               ///< reminders: `⏰!`
    /// The date token as written in the note (📅 ⏳ 🛫 ✅, the reminder; a reminder without a time
    /// with the default reminder time): an expanded series' item (`recurring`) has another
    /// date than this one — its token menu changes the written date (the whole series).
    /// Unset for journals.
    std::optional<DateSpec> written;

    friend bool operator==(const CalendarItem &, const CalendarItem &) = default;
};

/// Calendar entries with a date within [from, to] (inclusive):
///
/// * Journal: every daily note of the range;
/// * Due / Scheduled / Start: the dates of every task; a recurring task (valid 🔁 rule or Logseq
///   repeater) that is not done or cancelled is expanded: its reference date (due, else
///   scheduled, else start) with dates::occurrencesBetween() — the written date is the first
///   occurrence — and its other dates keep their distance in days to it, like the next
///   instance written on completion (SYNTAX §5); `recurring` is set on all of them, `written` is
///   the date as written;
/// * Done: completion dates (✅) of tasks;
/// * Reminder: every reminder, expanded like in upcomingNotifications() unless its task is done
///   or cancelled (those keep only the written date); a bare `- ⏰ …` bullet takes its parent's
///   text and task status.
///
/// Sorted by date; within a day timed items first (by time), then all-day items. The sort is
/// stable: ties keep the order journals, tasks (notes by path, document order: due, scheduled,
/// start, done), reminders. A recurring series contributes at most 500 dates. `from` > `to` or
/// invalid dates give an empty list.
QList<CalendarItem> calendarItems(const VaultIndex &index, QDate from, QDate to);

} // namespace brulion

Q_DECLARE_METATYPE(brulion::CalendarItem)
