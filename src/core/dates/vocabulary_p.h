// Internal to the dates module: folded word tables shared by the natural-language, recurrence
// and query-expression parsers. Keys are folded (lower-case, no diacritics — text::foldKey), so
// "środę", "Srode" and "ŚRODĘ" all hit "srode". Not part of the public API.
#pragma once

#include <QHash>
#include <QString>
#include <QStringList>
#include <QStringView>

#include <optional>

namespace brulion::dates::detail {

/// A weekday word recognised by the parsers.
struct WeekdayWord {
    int day = 1;        ///< Qt::DayOfWeek: 1 = Monday … 7 = Sunday
    bool abbr = false;  ///< "pt", "fri": ambiguous in prose, only accepted in a clear context
    bool plural = false; ///< "poniedziałki", "mondays": recurrence only, not single dates
    friend bool operator==(const WeekdayWord &, const WeekdayWord &) = default;
};

/// A month word recognised by the parsers.
struct MonthWord {
    int month = 1;        ///< 1–12
    bool english = false; ///< English words may precede the day ("Oct 15")
    bool abbr = false;
    friend bool operator==(const MonthWord &, const MonthWord &) = default;
};

/// Folded weekday forms (all cases, plurals, abbreviations), Polish + English.
const QHash<QString, WeekdayWord> &weekdayWords();
/// Folded month forms, Polish + English.
const QHash<QString, MonthWord> &monthWords();
/// Folded cardinal number words: "dwa", "dwie", "trzy", "two", … -> value.
const QHash<QString, int> &numberWords();
/// Folded ordinal words used for intervals: "drugi", "other", "second", … -> value.
const QHash<QString, int> &ordinalWords();

std::optional<WeekdayWord> lookupWeekday(QStringView folded);
std::optional<MonthWord> lookupMonth(QStringView folded);

/// Regex alternation of folded phrases, longest first; a space inside a phrase matches any
/// run of whitespace (`\s+`).
QString phraseAlternation(const QStringList &phrases);

/// Collapse every run of whitespace into one space (no trimming). Used to turn a matched
/// phrase into a table key ("w  ciagu" -> "w ciagu").
QString collapseSpaces(QStringView s);

} // namespace brulion::dates::detail
