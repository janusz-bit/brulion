// Weekly and monthly notes ("periodic notes", docs/SYNTAX.md §1 "Weekly and monthly notes"): pages
// next to the daily ones, one per ISO week and one per month, with their own folder, file name
// format and template in `.brulion/config.json`. This module is GUI-free and knows nothing of the
// vault: the calendar arithmetic (ISO weeks: Monday first, week 1 holds the year's first Thursday,
// so 2027-01-01 is in 2026-W53 and 2024-12-30 in 2025-W01), the file names both ways and the
// phrases a user types ("ten tydzień", "next month", "2026-W45", "listopad 2026"). Creating a note
// from its template is notebook/periodicnotes.h.
//
// File name formats (relative to the period's folder, without ".md"; a "/" makes subfolders):
// * weekly — its own small format, as Qt has no week tokens: `yyyy` the ISO week-year (4 digits),
//   `yy` its last two digits, `ww` the ISO week with a leading zero, `w` without; text in single
//   quotes is literal (`''` is a quote), other characters but letters are literal. Default
//   `yyyy-'W'ww` → "2026-W45" (Obsidian Periodic Notes' ISO `GGGG-[W]WW`).
// * monthly — a Qt QDate format applied to the month's 1st, which must name the month alone (no
//   day): default `yyyy-MM` → "2026-11" (Obsidian's `YYYY-MM`); `MMMM yyyy` → "November 2026".
// Names are written with English month names (QLocale::c(), like the daily notes) and read back
// with English or Polish ones; a name that does not format back to itself is no periodic note.
#pragma once

#include "dates/names.h"
#include "types.h"

#include <QDate>
#include <QDateTime>
#include <QLocale>
#include <QString>
#include <QStringView>

#include <optional>

namespace brulion::periodic {

enum class Period { Week, Month };

/// "week" / "month" (routes, the command line's JSON).
QString periodName(Period period);
/// "week", "weekly", "tydzień", "tygodniowa" … / "month", "monthly", "miesiąc" …
/// (case- and diacritic-insensitive); nullopt otherwise.
std::optional<Period> parsePeriodName(QStringView name);

struct IsoWeek {
    int year = 0; ///< the ISO week-year (not always the calendar year of the day)
    int week = 0; ///< 1–53
    friend bool operator==(const IsoWeek &, const IsoWeek &) = default;
};

/// The ISO week of `date`; {0, 0} for an invalid date.
IsoWeek isoWeek(QDate date);
/// 52 or 53 (53 when the year starts or, in a leap year, ends on a Thursday); 0 outside 1–9999.
int isoWeeksInYear(int year);
/// The Monday of ISO week `week` of week-year `year`; invalid when there is no such week.
QDate isoWeekStart(int year, int week);

/// The first day of the period holding `date`: its ISO week's Monday, its month's 1st.
QDate periodStart(Period period, QDate date);
/// The last day: the Sunday, the month's last day.
QDate periodEnd(Period period, QDate date);
/// The start of the period `count` periods after the one holding `date` (negative: before).
QDate shiftPeriod(Period period, QDate date, int count);

/// The defaults of VaultConfig: "yyyy-'W'ww", "yyyy-MM".
QString defaultFileFormat(Period period);
/// A file name format that names every period apart and reads back (see the file comment).
bool isUsableFormat(Period period, const QString &format);

/// The vault's settings for a period, trimmed and normalised; an unusable format gives the default.
QString fileFormat(Period period, const VaultConfig &config);
QString folder(Period period, const VaultConfig &config);
QString templatePath(Period period, const VaultConfig &config);

/// The note's name of the period holding `date` (relative to its folder, without ".md"); "" for an
/// invalid date or a format that does not read back.
QString nameFor(Period period, QDate date, const QString &format, const QLocale &locale = QLocale::c());
/// The start of the period a name names (strictly: it must format back to itself, letter case
/// aside), else nullopt. Month names may be English or Polish.
std::optional<QDate> dateFromName(Period period, const QString &name, const QString &format);

/// Vault path of the period's note: "<folder>/<name>.md"; "" for an invalid date.
VaultPath pathFor(Period period, QDate date, const VaultConfig &config);
/// The start of the period of a note path (a Markdown file inside the period's folder — letter
/// case aside — whose name reads back with the period's format), else nullopt.
std::optional<QDate> dateFromPath(Period period, const VaultPath &path, const VaultConfig &config);

struct PeriodicNote {
    Period period = Period::Week;
    QDate start;
    friend bool operator==(const PeriodicNote &, const PeriodicNote &) = default;
};
/// The weekly or monthly note `path` is (the week first), else nullopt.
std::optional<PeriodicNote> periodicNoteFromPath(const VaultPath &path, const VaultConfig &config);

/// What a period is called in the UI: "Tydzień 45 (2–8 listopada 2026)", "Tydzień 1 (29 grudnia
/// 2025 – 4 stycznia 2026)" / "Week 45 (Nov 2–8, 2026)"; "listopad 2026" → "Listopad 2026" /
/// "November 2026". "" for an invalid date.
QString periodLabel(Period period, QDate date, dates::UiLocale locale);

/// The period a typed phrase names, as its start, relative to `now`'s date:
/// * this / next / last: "ten tydzień", "bieżący miesiąc", "przyszły tydzień", "następny miesiąc",
///   "zeszły tydzień", "poprzedni miesiąc", "this week", "next month", "last week"; "" or the
///   period's own word alone ("tydzień", "week") is the current one;
/// * weeks: "2026-W45", "2026W45", "W45", "w45 2026", "tydzień 45", "tydz. 45 2026", "week 45" (the
///   year: the current ISO week-year); a week the year does not have is no week;
/// * months: "2026-11", "11.2026", "11/2026", "listopad", "listopad 2026", "listopada 2026",
///   "Nov 2026", "November" (the year: the current one);
/// * the vault's own file name (`config`, may be null);
/// * any date dates::parseNaturalDate() understands, whole: its period ("jutro", "15 października",
///   "w przyszłym miesiącu", "za 2 tygodnie", "2026-11-03").
/// Case- and diacritic-insensitive; nullopt when nothing fits.
std::optional<QDate> parsePeriodPhrase(Period period, QStringView text, const QDateTime &now,
                                       Qt::DayOfWeek weekStart = Qt::Monday,
                                       dates::UiLocale locale = dates::UiLocale::Polish,
                                       const VaultConfig *config = nullptr);

} // namespace brulion::periodic
