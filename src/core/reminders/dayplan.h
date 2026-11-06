// The day plan of the side panel's journal sections (docs/UI.md "Side panel"): one day's
// calendar entries as one row per block — a task due today with a reminder at 17:30 is one row at
// 17:30, not two —, timed rows first by time, then the day's other rows. Part of the reminders
// module — see reminders/reminders.h.
#pragma once

#include "reminders/calendar.h"

#include <QDate>
#include <QList>
#include <QTime>

#include <optional>

namespace brulion {

/// One row of the day plan: a block with a due date, a scheduled date or reminders on the day.
struct DayPlanEntry {
    std::optional<QTime> time; ///< the earliest time of the block's entries on the day (none: all day)
    QString title;             ///< the block's text (calendarItems(): the task text)
    VaultPath path;
    int line = 0;
    std::optional<TaskStatus> status; ///< the task's status (a reminder on a task too)
    bool due = false;                 ///< a due date (📅) on the day
    bool scheduled = false;           ///< a scheduled date (⏳) on the day
    bool reminder = false;            ///< a reminder (⏰) on the day
    bool alarm = false;               ///< one of them is an alarm (⏰!)
    bool recurring = false;           ///< one of them belongs to a series (🔁)

    friend bool operator==(const DayPlanEntry &, const DayPlanEntry &) = default;
};

/// The plan of `day` from calendar entries (calendarItems() of a range holding it): the Due,
/// Scheduled and Reminder entries of that day, merged per block (path and line) — the earliest
/// time, the flags of all of them, the first entry's title and status —; journals, start dates
/// and completion dates are left out. Timed rows first by time, then all-day ones; ties keep the
/// order of `items`. A done or cancelled task stays (the panel strikes it through).
QList<DayPlanEntry> dayPlan(const QList<CalendarItem> &items, QDate day);

} // namespace brulion
