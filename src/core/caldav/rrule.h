// Recurrence rules of iCalendar (RFC 5545 §3.3.10) in full: every FREQ, INTERVAL, COUNT, UNTIL,
// BYSECOND … BYSETPOS and WKST, expanded in the series' own wall-clock time — what calendars of
// other programs send (subscriptions, CalDAV). Brulion's own rules (docs/SYNTAX.md §7) are the
// subset dates::RecurrenceRule holds; io::ical::parseRRule() maps between the two. Part of the
// caldav module — see caldav/caldav.h.
#pragma once

#include <QDate>
#include <QList>
#include <QString>
#include <QStringView>
#include <QTime>

#include <compare>
#include <optional>

namespace brulion::caldav {

/// A date and a wall-clock time without a zone (the series' own clock). All-day values have
/// 00:00. Ordered by date, then time.
struct NaiveDateTime {
    QDate date;
    QTime time = QTime(0, 0);

    bool isValid() const { return date.isValid() && time.isValid(); }
    friend bool operator==(const NaiveDateTime &, const NaiveDateTime &) = default;
    friend std::strong_ordering operator<=>(const NaiveDateTime &a, const NaiveDateTime &b)
    {
        if (a.date != b.date)
            return a.date.toJulianDay() <=> b.date.toJulianDay();
        return a.time.msecsSinceStartOfDay() <=> b.time.msecsSinceStartOfDay();
    }
    /// `seconds` later (or earlier), across midnights.
    NaiveDateTime addSecs(qint64 seconds) const;
    /// Seconds from `this` to `other`.
    qint64 secsTo(const NaiveDateTime &other) const;
};

struct RRule {
    enum class Freq { Secondly, Minutely, Hourly, Daily, Weekly, Monthly, Yearly };
    /// A BYDAY entry: weekday 1 (Monday) … 7 (Sunday), `ordinal` 0 = every one, else the n-th
    /// (negative: from the end) of the month or the year.
    struct WeekdayNum {
        int ordinal = 0;
        int weekday = 1;
        friend bool operator==(const WeekdayNum &, const WeekdayNum &) = default;
    };
    /// UNTIL as written: a date (all-day series), a UTC time or a local one.
    struct Until {
        QDate date;
        std::optional<QTime> time;
        bool utc = false;
        friend bool operator==(const Until &, const Until &) = default;
    };

    Freq freq = Freq::Daily;
    int interval = 1;
    std::optional<int> count;
    std::optional<Until> until;
    QList<int> bySecond, byMinute, byHour;
    QList<WeekdayNum> byDay;
    QList<int> byMonthDay, byYearDay, byWeekNo, byMonth, bySetPos;
    int weekStart = 1; ///< WKST, 1 = Monday

    friend bool operator==(const RRule &, const RRule &) = default;
};

/// Parse an RRULE value ("FREQ=MONTHLY;BYDAY=-1FR;COUNT=10"). Names are case-insensitive, unknown
/// parts are ignored (X-…), values out of their ranges or a rule without FREQ give nullopt
/// (`error`: the reason in English, for logs).
std::optional<RRule> parseRRule(QStringView value, QString *error = nullptr);
/// The rule as an RRULE value, parts in RFC 5545's order (FREQ first).
QString formatRRule(const RRule &rule);

/// Bounds of one expansion: a hostile rule ("FREQ=SECONDLY" over a century, a rule no date ever
/// matches) ends within them.
struct ExpandLimits {
    int maxOccurrences = 2000;  ///< results per call
    int maxPeriods = 100'000;   ///< periods (years, months, … of the FREQ) looked at
    int maxCandidates = 400'000; ///< dates and times tried
};

/// The occurrences of a series starting at `dtstart` (its first occurrence, whether or not it
/// matches the rule — RFC 5545: DTSTART always counts) whose start lies in [from, to), in the
/// series' wall clock, sorted. `until` is UNTIL in the same wall clock (the caller converts a
/// UTC one); `dateOnly`: an all-day series (times are ignored, BYHOUR… too). COUNT counts from
/// `dtstart` whatever the window. Rules without COUNT skip the periods before the window, so
/// the cost follows the window, not the series' age. `truncated` is set when a limit ended the
/// expansion before the window did.
QList<NaiveDateTime> expandRRule(const RRule &rule, const NaiveDateTime &dtstart, bool dateOnly,
                                 const NaiveDateTime &from, const NaiveDateTime &to,
                                 const std::optional<NaiveDateTime> &until, const ExpandLimits &limits = {},
                                 bool *truncated = nullptr);

} // namespace brulion::caldav
