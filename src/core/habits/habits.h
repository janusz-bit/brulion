// brulion habits module — recurring daily items with a streak and a month view, recorded in the
// journal (docs/SYNTAX.md §5 "Nawyki / Habits"). GUI-free, never reads the clock: callers pass
// `today`. Dates are local calendar days (QDate) — no times, no time zones, no DST.
//
// API overview (namespace brulion::habits):
//
//   schedule   parseSchedule(text) -> optional<Schedule> ("codziennie", "pn, śr, pt", "co 2 dni",
//              "3 razy w tygodniu", "every week on Monday, Friday", "every month on the 15th"; the
//              recurrence parser of dates/recurrence.h where it fits), formatSchedule(schedule,
//              locale), isScheduled(schedule, start, day, weekStart)
//   names      habitName(text) (a block's text as the habit's name: tags dropped, links as their
//              labels), habitKey(name) (what names are compared by: NFC, case folded, spaces
//              collapsed), isCheckTag(tag) ("nawyk" | "habit"), checkTag(locale)
//   per note   definitionsOf(path, note) (blocks with `habit::` / `nawyk::`, a page with the page
//              property), checksOf(note) (a daily note's tasks tagged #nawyk / #habit)
//   stats      statsOf(habit, days, today, weekStart) -> Stats{current / longest streak and its
//              unit, done, met / expected (the completion rate), today's state, the period's
//              progress}; daysOf(habit, days, from, to, today, weekStart) -> the month grid's
//              DayState per day
//   cache      HabitCache (habitcache.h): every definition and check of an index, kept per note
//              and computed again only for the notes a change names (index/notecache.h)
#pragma once

#include "dates/names.h"
#include "types.h"

#include <QDate>
#include <QList>
#include <QMap>
#include <QString>
#include <QStringView>

#include <optional>

namespace brulion::habits {

// ---- Schedules ---------------------------------------------------------------------------------

/// When a habit is due.
///
/// * Days      — every `interval`-th day from the habit's start ("codziennie", "co 2 dni").
/// * Weekdays  — the days `weekdays` (1 = Monday … 7 = Sunday) of every `interval`-th week, weeks
///               counted from the start's week ("pn, śr, pt", "w dni robocze", "co 2 tygodnie w
///               poniedziałek").
/// * MonthDay  — day `monthDay` of every `interval`-th month (0 = the last day; a day past the
///               month's end is its last day: 31 → 30 in April).
/// * Times     — at least `times` days in every period (`period`; `interval` periods long, counted
///               from the start's period): "3 razy w tygodniu", "raz w miesiącu", "every week".
struct Schedule {
    enum class Kind { Days, Weekdays, MonthDay, Times };
    enum class Period { Week, Month, Year };

    Kind kind = Kind::Days;
    int interval = 1;
    QList<int> weekdays;
    int monthDay = 0;
    int times = 1;
    Period period = Period::Week;

    friend bool operator==(const Schedule &, const Schedule &) = default;
};

/// Largest accepted interval (in days, weeks, months or periods).
inline constexpr int MaxInterval = 999;

/// Parse a habit's schedule, the value of its `habit::` property (case-insensitive, diacritics
/// optional, Polish or English):
///   * empty, "tak", "yes", "true" — every day;
///   * "N razy w tygodniu / miesiącu / roku", "Nx w tygodniu", "N/tydz", "raz w tygodniu", "dwa razy
///     w tygodniu", "N times a week / month / year", "once a week", "twice a month", "Nx/week",
///     "N per week" — Times (1 ≤ N ≤ 7 a week, 31 a month, 366 a year);
///   * a list of weekdays ("pn, śr, pt", "poniedziałek i czwartek", "mon, fri") — Weekdays;
///   * a recurrence rule (dates::parseRecurrence: "codziennie", "co 3 dni", "w dni robocze",
///     "co tydzień w poniedziałek, piątek", "co miesiąc 15.", "co miesiąc ostatniego", "every 2
///     weeks", "every year"): days → Days, business days → Weekdays 1–5, a week with days →
///     Weekdays, a month with a day → MonthDay, a week, month or year without days → Times 1 in
///     that period ("co tydzień" = once a week, any day); "when done" is ignored.
/// nullopt for anything else ("co drugi wtorek", "8 razy w tygodniu", "0 razy w miesiącu").
std::optional<Schedule> parseSchedule(QStringView text);

/// The schedule as words: English "every day", "every 2 days", "on Monday, Friday", "on weekdays",
/// "every month on the 15th", "3 times a week", "once a month"; Polish "codziennie", "co 2 dni",
/// "pn, pt" style day names ("w poniedziałek, piątek"), "w dni robocze", "co miesiąc 15.",
/// "3 razy w tygodniu", "raz w miesiącu". parseSchedule() reads both back.
QString formatSchedule(const Schedule &schedule, dates::UiLocale locale = dates::UiLocale::English);

/// The first day of the period of `day` (Times schedules, interval 1): the week's first day by
/// `weekStart`, the month's 1st, the year's 1 January.
QDate periodStart(Schedule::Period period, QDate day, Qt::DayOfWeek weekStart);

/// Is `day` a due day of a Days / Weekdays / MonthDay schedule that starts on `start`? Always false
/// before `start` and for Times schedules (they have no due days, only periods).
bool isScheduled(const Schedule &schedule, QDate start, QDate day, Qt::DayOfWeek weekStart);

// ---- Names and tags ------------------------------------------------------------------------------

/// A block's text as a habit's name: its first line, `[[links]]` as their labels (the alias, else
/// the target's last part without `#…`), tags (`#nawyk`, `#zdrowie`) dropped, white space collapsed.
QString habitName(QStringView text);

/// What habit names are compared by: NFC, case folded, white space collapsed and trimmed — not
/// diacritic-insensitive ("Łódź" is not "Lodz").
QString habitKey(QStringView name);

/// Does a tag (a lower-case key without '#') mark a task of a daily note as a habit's check?
/// "nawyk" and "habit".
bool isCheckTag(QStringView tag);

/// The tag Brulion writes on a new check: "nawyk" in Polish, "habit" in English.
QString checkTag(dates::UiLocale locale);

// ---- Definitions and checks -------------------------------------------------------------------------

/// A habit as a note defines it.
struct Habit {
    QString name;                     ///< as written (habitName() of the block, the page's title)
    QString key;                      ///< habitKey(name)
    QString scheduleText;             ///< the property's value as written
    std::optional<Schedule> schedule; ///< nullopt: not understood (shown as a problem)
    QDate start;                      ///< `start::` / `od::` (invalid when not given or not a date)
    VaultPath path;                   ///< the note defining it
    int blockIndex = -1;              ///< the block (index into ParsedNote::blocks); -1: the page itself
    int line = 0;                     ///< the block's first line (0 for a page)
    bool duplicate = false;           ///< another definition of the same name came first (HabitCache)

    bool isValid() const { return schedule.has_value() && !key.isEmpty() && !duplicate; }
    friend bool operator==(const Habit &, const Habit &) = default;
};

/// What a day holds for a habit.
enum class Mark {
    Open,    ///< a check that is not done ([ ], [/], [>])
    Skipped, ///< a cancelled check ([-]): excused, neither done nor missed
    Done,    ///< a done check ([x])
};

/// One check: a task of a daily note tagged #nawyk / #habit.
struct Check {
    QString key;  ///< habitKey() of the task's name
    QString name; ///< habitName() of the task's text
    QDate date;   ///< the daily note's day
    Mark mark = Mark::Open;
    int blockIndex = -1;
    int line = 0;

    friend bool operator==(const Check &, const Check &) = default;
};

/// The habits a note defines: list items, paragraphs and headings with a `habit::` or `nawyk::`
/// property (a block's name: habitName() of its text), and the page itself when its properties
/// (frontmatter or leading `key:: value` lines) have one (named by the page's title). A block or
/// page without a name defines nothing. In document order, the page first.
QList<Habit> definitionsOf(const VaultPath &path, const ParsedNote &note);

/// The checks of a daily note (ParsedNote::journalDate): its tasks tagged #nawyk or #habit (the
/// block's own tags) that define no habit, with a name. Empty for other notes.
QList<Check> checksOf(const ParsedNote &note);

/// The mark of a day with several checks: Done over Skipped over Open.
Mark strongerMark(Mark a, Mark b);

/// Each day's mark of one habit (several checks a day merged by strongerMark()).
using DayMarks = QMap<QDate, Mark>;

// ---- Statistics ------------------------------------------------------------------------------------

/// What a day of a habit's month grid shows.
enum class DayState {
    Outside, ///< before the habit's start
    Future,  ///< after today
    Rest,    ///< nothing due and nothing recorded (a day off of the schedule; any day of a Times habit)
    Due,     ///< today, due (a due day, or a Times period not yet met) and not done
    Done,    ///< recorded as done (also on a day off: an extra)
    Skipped, ///< recorded as skipped
    Missed,  ///< a due day before today without a done or skipped check (also a day without a note)
};

/// What a streak counts.
enum class StreakUnit {
    Days,  ///< daily habits: days in a row
    Times, ///< other due days: due days in a row
    Weeks, ///< Times habits: periods met in a row
    Months,
    Years,
};

struct Stats {
    QDate start;           ///< the habit's start: `start::`, else its first recorded day, else today
    int currentStreak = 0; ///< due days (periods) done in a row up to today; a due today not done
                           ///< yet, or a period still open, does not break it
    int longestStreak = 0;
    StreakUnit unit = StreakUnit::Days;
    int done = 0;                ///< days done since the start (extras included)
    int met = 0;                 ///< due days done / periods met since the start
    int expected = 0;            ///< met + missed (skipped days and periods, a pending today left out)
    bool dueToday = false;       ///< today is a due day, or the period still needs a day
    Mark todayMark = Mark::Open; ///< today's mark (Open without a check)
    bool todayRecorded = false;  ///< a check of today exists
    int periodDone = 0;          ///< Times: days done in today's period
    int periodTarget = 0;        ///< Times: days needed in it (fewer by its skipped days); 0 otherwise
    QDate lastDone;              ///< the last day done (invalid: never)

    /// met / expected, or -1 when nothing was expected yet.
    double rate() const { return expected > 0 ? double(met) / expected : -1.0; }
};

/// The days counted at most: older days of a habit are left out of its statistics.
inline constexpr int MaxCountedDays = 3660;

/// The statistics of `habit` (a valid schedule) with its days' marks `days`, on `today`.
/// Days kinds: each due day from the start to today is done, skipped, missed or (today) pending.
/// Times: each period is met (enough days done; each skipped day needs one fewer), excused (all
/// skipped), missed, or still open (today's); the start's period, when the habit starts inside
/// it, counts only once met. A habit whose start is after today has nothing counted yet.
Stats statsOf(const Habit &habit, const DayMarks &days, QDate today, Qt::DayOfWeek weekStart = Qt::Monday);

/// The state of each day of [from, to] (at most 400 days) for the month grid.
QList<DayState> daysOf(const Habit &habit, const DayMarks &days, QDate from, QDate to, QDate today,
                       Qt::DayOfWeek weekStart = Qt::Monday);

/// The habit's start as statsOf() takes it: `start::`, else the first day of `days`, else today.
QDate effectiveStart(const Habit &habit, const DayMarks &days, QDate today);

/// Stable English names for JSON and QML: "open" | "skipped" | "done"; "outside" | "future" |
/// "rest" | "due" | "done" | "skipped" | "missed"; "days" | "times" | "weeks" | "months" | "years".
QString markName(Mark mark);
std::optional<Mark> markFromName(QStringView name);
QString dayStateName(DayState state);
QString streakUnitName(StreakUnit unit);

} // namespace brulion::habits
