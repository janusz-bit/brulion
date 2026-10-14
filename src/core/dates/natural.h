// Natural-language date/time recognition (Polish + English) for editor input, quick capture and
// the date picker (docs/SYNTAX.md §8). Part of the dates module — see dates/dates.h.
#pragma once

#include "dates/names.h"
#include "types.h"

#include <QDateTime>
#include <QList>
#include <QStringView>

#include <optional>

namespace brulion::dates {

/// A recognised date phrase: the resolved date (and time, if the phrase had one) and the span
/// of the phrase in the input (UTF-16 offsets into the original, unfolded text).
struct NaturalDate {
    DateSpec spec;
    qsizetype start = 0;
    qsizetype length = 0;

    qsizetype end() const { return start + length; }
    friend bool operator==(const NaturalDate &, const NaturalDate &) = default;
};

/// Find the first date/time phrase anywhere in `text` and resolve it relative to `now`:
/// "Zadzwoń do mamy jutro o 18" -> {2026-10-15 18:00, start 16, length 10} ("jutro o 18").
///
/// `now` is read in its own time spec (pass QDateTime::currentDateTime() for local time); its
/// date is "today" and its time decides whether a bare time ("o 9") is today or tomorrow.
/// Relative times ("za 3 godziny", "in 30 minutes") use QDateTime::addSecs (elapsed time, so
/// DST-correct); everything else is calendar arithmetic on QDate. Seconds are dropped.
///
/// Understood (case- and diacritic-insensitive; "srode" = "środę"):
/// * day words: dziś/dzisiaj, jutro, pojutrze, wczoraj, przedwczoraj, "do jutra";
///   today, tomorrow, yesterday, the day after tomorrow, the day before yesterday;
/// * weekdays: "piątek", "w piątek", "friday", "on friday" = the next such day strictly after
///   today; "w przyszły/następny piątek", "next friday" = that day in next week (per
///   `weekStart`); "ten piątek", "this friday" = next on or after today; "zeszły/ubiegły
///   piątek", "last friday" = the most recent one before today; abbreviations ("pt", "sat")
///   only with a preposition/modifier, next to a time, or as the whole input;
/// * offsets: "za 2 dni", "za tydzień", "za 3 godziny", "za pół godziny", "za kwadrans",
///   "w ciągu 3 dni", "2 dni temu", "in 2 weeks", "in an hour", "3 weeks ago";
/// * periods: "w przyszłym tygodniu/miesiącu/roku", "next week/month/year" (first day),
///   "koniec miesiąca", "do końca przyszłego tygodnia", "end of next month" (last day),
///   "w weekend", "this weekend", "w przyszły weekend" (Saturday);
/// * explicit dates: "15 października [2027]", "15 paź", "15.10", "15.10.2026", "15.10.26r.",
///   "15/10", "15-10-2026", "2026-10-15", "2026-10-15T18:30", "Oct 15", "October 15th, 2027",
///   "1st of November", "the 15th", "15-go"; a date without a year that already passed this
///   year means next year; "10/11" is 10 November unless `locale` is English (October 11);
/// * times: "o 9", "o 9:30", "o 18.30", "o godz. 18", "o 18h", "o 9h30", "9:30", "6pm",
///   "6:30 p.m.", "at 9", "5 o'clock", "rano/przed południem/w południe/po południu/wieczorem/
///   w nocy/o północy", "noon", "midnight", "tonight", "this morning", "7 rano" (07:00),
///   "8 wieczorem" (20:00). A time without a date is today if still ahead, else tomorrow.
///   Midnight words and small hours at night after a named day mean the following night
///   ("dziś o północy" = tomorrow 00:00, "jutro o 2 w nocy" = the day after tomorrow 02:00);
/// * a date and a time may be combined, separated by whitespace and at most one ",", ":" or
///   dash: "jutro o 7:30", "w piątek wieczorem", "15.10.2026 18:30", "Oct 15 6pm", "Jutro: 18:00".
///
/// False positives are avoided: nothing inside words ("jutrzejszy"), URLs, e-mail addresses,
/// [[wiki links]], [markdown](links), `code`, #tags, canonical tokens ("📅 2026-10-15", Logseq
/// "SCHEDULED: <2026-10-15 Thu>"), recurrence rules ("🔁 co tydzień w sobotę" — text after the
/// rule is still searched) or property lines ("termin:: 2026-10-15"); a
/// bare "o 9"/"at 9" needs a time-ish context ("podwyżka o 5 zł" is no time); amounts
/// ("12.05 zł", "$15.10"), decimals ("12.5"), fractions ("1/2"), versions ("1.2.3") and
/// ordinals before a noun ("the 2nd bug", "na 2-go piętrze") are no dates; "15 się" / "5 mają"
/// are not August / May.
///
/// Returns nullopt when nothing is found, `now` is invalid, or the result would lie outside
/// years 1–9999.
std::optional<NaturalDate> parseNaturalDate(QStringView text, const QDateTime &now,
                                            Qt::DayOfWeek weekStart = Qt::Monday,
                                            UiLocale locale = UiLocale::Polish);

/// All date/time phrases in `text`, left to right (e.g. for highlighting while typing).
/// Same rules as parseNaturalDate().
QList<NaturalDate> findNaturalDates(QStringView text, const QDateTime &now, Qt::DayOfWeek weekStart = Qt::Monday,
                                    UiLocale locale = UiLocale::Polish);

/// Remove a recognised phrase from `text` and tidy up whitespace and dangling separators:
/// "Zadzwoń do mamy jutro o 18" -> "Zadzwoń do mamy", "Zadzwoń, jutro o 18, do mamy" ->
/// "Zadzwoń do mamy", "Zadzwoń jutro." -> "Zadzwoń.". Brackets or quotes that only wrapped the
/// phrase go too: "Kupić mleko (jutro)" -> "Kupić mleko".
QString stripNaturalDate(QStringView text, const NaturalDate &found);

} // namespace brulion::dates
