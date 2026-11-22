// Scheduled conversations of the AI chat (docs/UI.md "Scheduled conversations"): when one runs —
// a rule written in words, Polish or English, through Brulion's own date parsing (dates/natural.h,
// dates/recurrence.h) —, the rule shown back in words, its next run, and the schedules as the
// device keeps them (JSON). GUI-free; the app's runner is src/app/shell/chatschedules.h.
//
//   "codziennie o 7:30", "w poniedziałki o 9:00", "w dni robocze o 8"   Repeat (a rule and a time)
//   "every day at 7:30", "on mondays at 9am", "weekdays at 8"          Repeat
//   "co 3 godziny", "co pół godziny", "every 2 hours", "hourly"         Interval (at least 15 min)
//   "jutro o 8", "w piątek o 18", "2026-12-01 18:00", "tomorrow 8am"   Once (in the future)
//
// Every time is the local wall-clock time (as reminders: never through UTC). A run is due once
// the wall clock reaches it: a time a spring-forward skips (02:30) runs at the first minute after
// the gap, a time an autumn hour repeats runs once. An interval counts elapsed time from the last
// run. A run missed while the app was closed is due once at its next start — not once per missed
// occurrence: the next run is always computed from the last one (or from when the schedule was
// set up), so after a week away a daily schedule runs once, then tomorrow at its time.
#pragma once

#include "dates/names.h"
#include "dates/recurrence.h"
#include "types.h"

#include <QByteArray>
#include <QDateTime>
#include <QJsonObject>
#include <QList>
#include <QString>
#include <QStringView>
#include <QTime>

#include <optional>

namespace brulion::schedule {

/// The shortest interval accepted ("co 5 minut" is refused: TooOften).
inline constexpr int MinIntervalMinutes = 15;
/// The longest one (31 days).
inline constexpr int MaxIntervalMinutes = 31 * 24 * 60;
/// The time of a rule or a day named without one ("codziennie", "jutro").
inline constexpr int DefaultHour = 9;
/// The schedules a device keeps for one notebook (more are left out when read).
inline constexpr int MaxSchedules = 50;
/// The longest title and prompt kept.
inline constexpr int MaxTitleChars = 200;
inline constexpr int MaxPromptChars = 20000;

/// When a scheduled conversation runs.
struct When {
    enum class Kind { Once, Repeat, Interval };
    Kind kind = Kind::Once;
    /// Once: the day (with `time`). Repeat: unused.
    QDate date;
    /// Once and Repeat: the time of day (local wall-clock, minutes).
    QTime time;
    /// Repeat: the days (a rule without "when done").
    dates::RecurrenceRule rule;
    /// Interval: the minutes between runs (MinIntervalMinutes … MaxIntervalMinutes).
    int minutes = 0;

    bool isValid() const;
    friend bool operator==(const When &, const When &) = default;
};

/// Why parseWhen() understood nothing.
enum class WhenError {
    None,
    Empty,         ///< nothing written
    NotUnderstood, ///< no rule, interval or date in it
    TooOften,      ///< an interval under MinIntervalMinutes (or over the longest)
    Past,          ///< a single time that has passed
    WhenDone,      ///< a rule "when done" ("po wykonaniu"): a conversation is never done
};

/// The rule written in `text` (any letter case, diacritics optional), relative to `now` (local
/// wall-clock: "jutro", "w piątek" and a bare time count from it). nullopt and `*error` when not
/// understood.
std::optional<When> parseWhen(QStringView text, const QDateTime &now, dates::UiLocale locale = dates::UiLocale::Polish,
                              WhenError *error = nullptr);

/// The rule in words: "codziennie o 7:30", "co tydzień w poniedziałek o 9:00", "co 3 godziny",
/// "raz: jutro 08:00" ("every day at 7:30", "every 3 hours", "once: tomorrow 08:00"); `today`
/// for the words of a single run's day ("jutro"; invalid: always its date).
QString describeWhen(const When &when, dates::UiLocale locale, QDate today = {});

/// The first run strictly after `after` (a wall-clock time with a time of day: the last run, or
/// when the schedule was set up); nullopt when there is none (a single run at or before `after`,
/// an invalid rule).
std::optional<DateSpec> nextRun(const When &when, const DateSpec &after);

/// The wall-clock minute of `moment` (its local date and time, seconds dropped).
DateSpec wallClock(const QDateTime &moment);

/// The rule as kept: {kind: "once", at: "2026-11-23 08:00"} | {kind: "repeat", rule: "every day",
/// time: "07:30"} | {kind: "interval", minutes: 180}.
QJsonObject toJson(const When &when);
std::optional<When> whenFromJson(const QJsonObject &object);

/// A scheduled conversation as the device keeps it (AppSettings, per notebook — never in the
/// notebook, which syncs from anywhere: a synced file must not start agents).
struct Schedule {
    QString id;      ///< "s" and random hex digits, made by the app
    QString title;   ///< its name (and the new conversations' first words come from `prompt`)
    QString prompt;  ///< the message sent each run
    QString partner; ///< who answers: "" the built-in assistant, "acp:<id>", an extension's
    QString whenText; ///< the rule as written ("codziennie o 7:30")
    When when;
    /// Each run continues one conversation (`conversation`; the first run makes it) instead of
    /// starting a new one.
    bool continues = false;
    QString conversation{}; ///< the conversation it continues ("" none yet)
    bool enabled = true;
    DateSpec created{};          ///< when it was set up (or its rule last changed)
    DateSpec lastRun{};          ///< invalid: never ran
    QString lastConversation{};  ///< the conversation of its last run
    QString lastError{};         ///< why its last run did not start ("" it did)
    QString by{};                ///< who set it up: "" the user, else an agent's (a client's) name

    /// The next run: after the last one, else after `created`; nullopt none (a single run done,
    /// switched off is not considered here).
    std::optional<DateSpec> next() const;
    /// It is on and its next run has come by `now` (wall-clock).
    bool isDue(const QDateTime &now) const;
    friend bool operator==(const Schedule &, const Schedule &) = default;
};

QJsonObject toJson(const Schedule &schedule);
/// A schedule read back: nullopt for a malformed one (no id, prompt or valid rule).
std::optional<Schedule> scheduleFromJson(const QJsonObject &object);

/// The schedules kept as JSON (an array of toJson()): malformed entries left out, ids kept once,
/// at most MaxSchedules; nothing for anything that is no array.
QList<Schedule> parseSchedules(const QByteArray &json);
QByteArray serializeSchedules(const QList<Schedule> &schedules);

} // namespace brulion::schedule
