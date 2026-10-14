// Human-friendly date formatting for the UI ("dziś 09:00", "jutro", "pt 17 paź").
// Part of the dates module — see dates/dates.h for the module overview.
#pragma once

#include "dates/names.h"
#include "types.h"

#include <QDate>
#include <QString>

#include <optional>

namespace brulion::dates {

/// A date relative to `today`, plus the 24-hour time when the spec has one:
/// * yesterday / today / tomorrow -> "wczoraj" / "dziś" / "jutro" ("yesterday", "today", "tomorrow");
/// * other days of today's year -> "pt 17 paź" ("Fri, Oct 17");
/// * other years -> "17 paź 2027" ("Oct 17, 2027");
/// * with a time: "dziś 09:00", "pt 17 paź 18:30".
/// An invalid `today` disables the relative words and always shows the year. An invalid spec
/// gives an empty string.
QString formatHuman(const DateSpec &spec, UiLocale locale, QDate today);

/// A date without relative words: "pt 17 paź" / "Fri, Oct 17"; with the year ("17 paź 2027",
/// "Oct 17, 2027") when `currentYear` is given and differs from the date's year. Invalid
/// dates give an empty string.
QString formatDateShort(QDate date, UiLocale locale, std::optional<int> currentYear = std::nullopt);

} // namespace brulion::dates
