// Weekday / month names and small language helpers (Polish + English) for formatting.
// Part of the dates module — see dates/dates.h for the module overview.
#pragma once

#include <QString>
#include <QStringView>

namespace brulion::dates {

/// Language of user-visible date texts (the UI language). Files always use canonical English
/// tokens; Polish is the default UI language.
enum class UiLocale { Polish, English };

/// Grammatical form of a Polish name. English names have no cases: every form except Short
/// gives the full name.
enum class NameForm {
    Nominative, ///< "środa", "październik" (also the English full name)
    Genitive,   ///< "środy", "października" ("15 października")
    Accusative, ///< "środę" ("w środę"); months: same as nominative
    Short,      ///< "śr", "paź" / "Wed", "Oct"
};

/// Polish weekday name, lower-case. `dayOfWeek` follows Qt::DayOfWeek (1 = Monday … 7 = Sunday);
/// out-of-range values give an empty string.
QString polishWeekdayName(int dayOfWeek, NameForm form = NameForm::Nominative);
/// Polish month name, lower-case (`month` 1–12, else empty).
QString polishMonthName(int month, NameForm form = NameForm::Nominative);

/// Localised weekday name: Polish lower-case ("piątek", "pt"), English capitalised ("Friday", "Fri").
QString weekdayName(int dayOfWeek, UiLocale locale, NameForm form = NameForm::Nominative);
/// Localised month name: "października" (Genitive) / "October", "paź" / "Oct" (Short).
QString monthName(int month, UiLocale locale, NameForm form = NameForm::Nominative);

/// English ordinal: 1st 2nd 3rd 4th 11th 12th 13th 21st 22nd 101st 111th.
QString englishOrdinal(int n);

/// Polish plural form of a count: `one` for 1, `few` for 2–4, 22–24, … (not 12–14), else `many`.
/// polishPlural(5, u"tydzień", u"tygodnie", u"tygodni") == "tygodni".
QString polishPlural(qint64 n, QStringView one, QStringView few, QStringView many);

} // namespace brulion::dates
