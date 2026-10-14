// Recurrence rules (docs/SYNTAX.md §7): parsing (canonical English / Obsidian Tasks and Polish),
// formatting, Logseq repeaters and occurrence computation. Part of the dates module — see
// dates/dates.h for the module overview.
#pragma once

#include "dates/names.h"
#include "types.h"

#include <QList>
#include <QString>
#include <QStringView>

#include <optional>

namespace brulion::dates {

/// Largest accepted interval. Bigger values are rejected by the parsers and clamped by
/// normalizeRule(); they could only produce dates beyond year 9999.
inline constexpr int MaxRecurrenceInterval = 99'999;

/// A recurrence rule ("every 2 weeks on Monday, Friday when done").
///
/// * `weekdays` — Week only: days of the week (1 = Monday … 7 = Sunday), unique, ascending.
/// * `monthDay` — Month only: day of month (clamped to the month's length); without it (and
///   without `lastDayOfMonth`) the series' first date's day is used.
/// * `lastDayOfMonth` — Month only: "every month on the last" (takes precedence over monthDay).
/// * Yearly rules repeat on the base date's month and day (Feb 29 -> Feb 28 in common years).
/// * `weekdaysOnly` — business days: with Day every `interval`-th Monday–Friday ("every
///   weekday"); with Week and no `weekdays`, Monday–Friday of every `interval`-th week.
/// * `whenDone` — the next occurrence is computed from the completion date.
///
/// Multi-week intervals count Monday-based (ISO) weeks, like Obsidian Tasks / rrule.
struct RecurrenceRule {
    enum class Freq { Day, Week, Month, Year };

    Freq freq = Freq::Day;
    int interval = 1;
    QList<int> weekdays;
    std::optional<int> monthDay;
    bool lastDayOfMonth = false;
    bool weekdaysOnly = false;
    bool whenDone = false;

    friend bool operator==(const RecurrenceRule &, const RecurrenceRule &) = default;
};

/// Clean a rule (e.g. one built by the UI or read from JSON): interval clamped to
/// 1…MaxRecurrenceInterval, weekdays filtered to 1–7, deduplicated and sorted (Week only),
/// monthDay kept only for Month and 1–31, lastDayOfMonth only for Month, weekdaysOnly only for
/// Day or for Week without weekdays.
RecurrenceRule normalizeRule(const RecurrenceRule &rule);

/// Parse a rule written in canonical English (Obsidian Tasks: "every 2 weeks on Monday, Friday",
/// "every month on the last", "every weekday when done") or Polish ("co tydzień w poniedziałek,
/// piątek", "co miesiąc 15.", "w dni robocze po wykonaniu"), plus common variants ("daily",
/// "every other week", "co drugi dzień", "w każdy piątek", "co kwartał", "every mon, fri").
/// Case-insensitive, diacritics optional, an optional leading 🔁 is ignored. Returns nullopt
/// for unsupported or malformed text (e.g. "every month on the 2nd Tuesday").
std::optional<RecurrenceRule> parseRecurrence(QStringView text);

/// Format a rule. English gives the canonical Obsidian-Tasks-compatible text written into
/// files ("every week on Monday, Friday when done"); Polish gives the UI form ("co tydzień w
/// poniedziałek, piątek po wykonaniu"). Both round-trip through parseRecurrence(). The rule is
/// normalised first; a rule with an invalid freq formats as an empty string.
QString formatRecurrence(const RecurrenceRule &rule, UiLocale locale = UiLocale::English);

/// Parse any supported phrasing and return the canonical English text; empty if invalid.
QString canonicalRecurrence(QStringView text);

/// Weekday list ("poniedziałek i piątek", "w środę, sobotę", "pn, pt", "mondays and fridays")
/// as unique days 1–7 in ascending order; empty when any item is not a weekday.
QList<int> parseWeekdayList(QStringView text);

/// Convert a Logseq/org-mode repeater: "+1w" (shift from the original date), "++2d" (catch-up,
/// treated like "+"), ".+1m" (from the completion date -> whenDone). Hour repeaters ("+3h") and
/// malformed input give nullopt.
std::optional<RecurrenceRule> repeaterToRule(QStringView repeater);

/// Inverse of repeaterToRule() for rules Logseq can express (no weekdays, month day or business
/// days): {Week, 1} -> "+1w", with whenDone -> ".+1w". Empty when not expressible.
QString ruleToRepeater(const RecurrenceRule &rule);

/// Next occurrence strictly after `from` — or, for whenDone rules with a `completedOn` date,
/// strictly after the completion date. The time of `from` is kept. Monthly rules clamp to the
/// month's length (Jan 31 + 1 month -> Feb 28/29); without an explicit month day the base date's
/// day is used. Returns nullopt for an invalid `from` or rule, or when the next date would lie
/// beyond year 9999.
std::optional<DateSpec> nextOccurrence(const RecurrenceRule &rule, const DateSpec &from,
                                       std::optional<QDate> completedOn = std::nullopt);

/// All occurrences of a series starting at `anchor` that fall within [from, to] (inclusive,
/// compared by date), each with the anchor's time. The anchor itself is the first occurrence
/// (even when it does not match the rule's weekdays, like a task's original due date). Later
/// occurrences remember the anchor's day of month (Jan 31 monthly -> Feb 28, Mar 31, …).
/// whenDone is ignored here (future completion dates are unknown). At most `max` results;
/// old anchors are fast-forwarded, so the cost depends on the range, not on the anchor's age.
QList<DateSpec> occurrencesBetween(const RecurrenceRule &rule, const DateSpec &anchor, QDate from, QDate to,
                                   int max = 500);

} // namespace brulion::dates
