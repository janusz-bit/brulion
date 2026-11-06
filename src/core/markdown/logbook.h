// Time tracking and status history of tasks in Logseq's `:LOGBOOK:` drawer (docs/SYNTAX.md §5
// "Time tracking and status history"). Part of the markdown module — see markdown/markdown.h.
//
//   - DOING Napisać raport
//     :LOGBOOK:
//     CLOCK: [2026-11-03 Tue 10:00:00]--[2026-11-03 Tue 10:25:00] =>  00:25:00
//     CLOCK: [2026-11-03 Tue 11:00:00]
//     State "DOING"      from "TODO"       [2026-11-03 Tue 11:00]
//     :END:
//
// A `CLOCK:` line with one timestamp is a running clock. Timestamps are local wall-clock time
// (never converted through UTC); the day name is written in English and ignored when read (org
// mode writes it in the user's language). Elapsed time is measured between the two local times,
// so a clock across a daylight-saving change counts the hours that really passed; the written
// `=> HH:MM:SS` is read only when the timestamps give none (the end before the start: an hour
// repeated when the clocks go back, a hand edit). Overlapping entries of one task count once.
//
// Edits are "content in -> content out" on a whole file and the task block at `line` (any line of
// it): only the drawer's lines change (or the drawer is added after the block's first line and its
// planning / property lines), line endings and every other byte stay as they were.
#pragma once

#include "types.h"

#include <QDate>
#include <QDateTime>
#include <QList>
#include <QMap>
#include <QString>
#include <QStringView>

namespace brulion::markdown {

/// One `CLOCK:` line.
struct ClockEntry {
    QDateTime start;    ///< local time
    QDateTime end;      ///< local time; invalid while the clock runs
    qint64 seconds = 0; ///< elapsed seconds of a closed entry
    int line = 0;       ///< line of the scanned text (0-based)
    bool running() const { return !end.isValid(); }
    friend bool operator==(const ClockEntry &, const ClockEntry &) = default;
};

/// One status change line: org mode's `State "DONE" from "TODO" [2026-11-03 Tue 10:25]` (written
/// without org's leading "- ", which Markdown would read as a child list item; read with or
/// without it).
struct StatusChange {
    QString to;   ///< the keyword as written ("DONE")
    QString from; ///< "" when the line names none
    QDateTime at; ///< local time
    int line = 0;
    friend bool operator==(const StatusChange &, const StatusChange &) = default;
};

/// What a block's `:LOGBOOK:` drawers hold.
struct Logbook {
    bool present = false; ///< a `:LOGBOOK:` drawer was found
    QList<ClockEntry> clocks;
    QList<StatusChange> changes;
    /// `CLOCK:` / `State` lines that do not read: bad timestamps, an end before the start without a
    /// `=>` duration, a running clock before the last one (only the last of them runs).
    int malformed = 0;

    /// The running entry (the last one without an end), nullptr when none runs.
    const ClockEntry *running() const;
};

/// Read the `:LOGBOOK:` drawers (`:LOGBOOK:` … `:END:`, upper case as the parser's drawers) of `text` — a block's
/// lines ("\n", "\r\n"); other drawers and lines outside drawers are ignored.
Logbook readLogbook(QStringView text);

/// Seconds clocked by the closed entries, overlapping ones counted once.
qint64 clockedSeconds(const QList<ClockEntry> &clocks);
/// The same per local day (entries split at midnight); a running entry counts up to `now` when
/// `now` is valid and after its start. Days without time are left out.
QMap<QDate, qint64> clockedSecondsByDay(const QList<ClockEntry> &clocks, const QDateTime &now = {});

/// `[2026-11-03 Tue 10:00:00]` (with `seconds` false `[2026-11-03 Tue 10:00]`, org mode's form).
QString formatClockTimestamp(const QDateTime &local, bool seconds = true);
/// `00:25:00`; hours grow past two digits (`123:05:00`); negative counts as 0.
QString formatClockDuration(qint64 seconds);
/// `CLOCK: [start]--[end] =>  HH:MM:SS`, or `CLOCK: [start]` without an end (a running clock).
QString formatClockLine(const QDateTime &start, const QDateTime &end = {});
/// `State "DONE"       from "TODO"       [2026-11-03 Tue 10:25]` (org mode's padding).
QString formatStatusChange(QStringView to, QStringView from, const QDateTime &at);

/// "1 h 05 min", "25 min", "45 s" (short, for chips and lists; no translation needed).
QString formatSpentTime(qint64 seconds);

// ---- edits ------------------------------------------------------------------------------------

/// The logbook of the task block at `line` as the file has it now (empty for a line that holds no
/// task).
Logbook taskLogbook(const QString &content, int line);
/// The first line of the task block containing `line`, -1 when there is none (a list item that
/// is a task; code, quotes and other blocks are not).
int taskBlockLine(const QString &content, int line);

/// Start a clock on the task at `line`: `CLOCK: [now]` at the end of its `:LOGBOOK:` drawer (the
/// drawer added when missing). A clock already running there, or no task: unchanged.
QString startTaskClock(const QString &content, int line, const QDateTime &now);
/// Stop the running clock of the task at `line` at `now` (not before its start): its line gets
/// `--[now] =>  HH:MM:SS`. No running clock: unchanged.
QString stopTaskClock(const QString &content, int line, const QDateTime &now);
/// Append a status change line (Logseq keywords: keywordFor()) to the task's drawer.
QString logStatusChange(const QString &content, int line, TaskStatus from, TaskStatus to, const QDateTime &now);

/// How status changes are written (the app's "Log task status changes" setting).
struct StatusLogOptions {
    bool history = false; ///< write the history and Logseq's clock (below)
    QDateTime now;        ///< local time of the change; its date is setTaskStatus()'s today
};
/// setTaskStatus() and, with `options.history` on a block that was a task before: a status change
/// line, and as Logseq does — becoming doing starts the clock, leaving doing stops it.
QString setTaskStatusLogged(const QString &content, int line, TaskStatus status, const StatusLogOptions &options);
/// toggleTask() with the same logging.
QString toggleTaskLogged(const QString &content, int line, const StatusLogOptions &options);

} // namespace brulion::markdown
