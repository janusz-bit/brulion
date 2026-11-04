// Time spent on tasks (docs/SYNTAX.md §5 "Time tracking and status history"): the `CLOCK:` entries
// of their `:LOGBOOK:` drawers summed per day, page, tag or task — the Tasks view's "Czas pracy"
// and `brulion-cli task time`. Part of the index module — see index/index.h.
#pragma once

#include "index/vaultindex.h"

#include <QDate>
#include <QDateTime>
#include <QList>
#include <QString>

namespace brulion {

enum class TimeGroup { Day, Page, Tag, Task };

struct TimeSummaryRow {
    /// Day: "YYYY-MM-DD"; Page: the note's path; Tag: the tag key ("" for tasks without tags);
    /// Task: "path:line".
    QString key;
    QString label; ///< Day: the ISO date; Page: the note's title; Tag: the tag; Task: its text
    qint64 seconds = 0;
    int tasks = 0;  ///< tasks with time in the row
    VaultPath path; ///< Page and Task rows
    int line = -1;  ///< Task rows: the task's first line
};

struct TimeSummary {
    QList<TimeSummaryRow> rows; ///< Day: by date; the others: most time first (ties by label)
    qint64 total = 0;           ///< every task's time once (a task with two tags counts once)
    int tasks = 0;              ///< tasks with time
};

/// The time of `tasks` (as the index lists them) clocked on the local days [from, to] (an invalid
/// date: unbounded) grouped by `group`. Entries crossing midnight are split; overlapping entries
/// of one task count once; a running clock counts up to `now` (invalid: not at all). Tags are the
/// task's own, its ancestors' and its page's; a task counts in every tag it has.
TimeSummary summarizeTime(const VaultIndex &index, const QList<TaskRef> &tasks, TimeGroup group, QDate from = {},
                          QDate to = {}, const QDateTime &now = {});

/// Seconds a task (as the index lists it) has: its closed entries and a running clock up to `now`.
qint64 taskSpentSeconds(const Task &task, const QDateTime &now);

} // namespace brulion
