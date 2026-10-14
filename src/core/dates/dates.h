// brulion::dates — local dates, natural-language dates, recurrence rules, query date
// expressions and human formatting. Include this header to get the whole module.
//
// API overview (namespace brulion::dates; all dates are local wall-clock values — DateSpec
// from types.h, never converted through UTC; supported years 1–9999):
//
//   names.h       enum class UiLocale { Polish, English }; enum class NameForm
//                 polishWeekdayName(1..7, form), polishMonthName(1..12, form),
//                 weekdayName/monthName(n, locale, form), englishOrdinal(n), polishPlural(n, …)
//   local.h       parseDateSpec("2026-10-15[ 09:00]" / "2026-10-15T09:00") -> optional<DateSpec>
//                 parseIsoDate, parseClockTime, isSupportedDate, compareDateSpecs,
//                 startOfWeek/endOfWeek(date, weekStart), startOfMonth/endOfMonth,
//                 sanitizeWeekStart(int from config)
//   natural.h     parseNaturalDate(text, now, weekStart, locale) -> optional<NaturalDate{spec,
//                 start, length}> ("jutro o 18", "w przyszły piątek", "za 3 godziny",
//                 "15 października", "next friday 6pm" — first phrase anywhere in the text),
//                 findNaturalDates (all phrases), stripNaturalDate(text, found) (quick capture)
//   recurrence.h  struct RecurrenceRule { Freq freq; interval; weekdays (1=Mon..7=Sun);
//                 monthDay; lastDayOfMonth; weekdaysOnly; whenDone }
//                 parseRecurrence (English + Polish, optional 🔁), formatRecurrence(rule, locale),
//                 canonicalRecurrence(text) ("co tydzień w pn" -> "every week on Monday"),
//                 repeaterToRule("+1w" / ".+1d" / "++2m"), ruleToRepeater, normalizeRule,
//                 nextOccurrence(rule, from, completedOn), occurrencesBetween(rule, anchor, from, to)
//   expr.h        resolveDateExpr("this-week" / "+7d" / "today..+7d" / "overdue", today,
//                 weekStart) -> optional<variant<DateRange, SpecialDate>>
//   format.h      formatHuman(spec, locale, today) ("dziś 09:00", "pt 17 paź", "Fri, Oct 17"),
//                 formatDateShort(date, locale, currentYear)
//
// Determinism: nothing here reads the system clock — callers pass `now`/`today` (tests inject
// fixed values). Natural-language and recurrence parsing are case- and diacritic-insensitive
// (text::foldKey), so input typed without Polish letters ("srode", "co tydzien") works.
#pragma once

#include "dates/expr.h"
#include "dates/format.h"
#include "dates/local.h"
#include "dates/names.h"
#include "dates/natural.h"
#include "dates/recurrence.h"
