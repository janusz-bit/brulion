// Calendar helpers for local, time-zone-less dates (DateSpec from types.h).
// Part of the dates module — see dates/dates.h for the module overview.
#pragma once

#include "types.h"

#include <QStringView>

#include <optional>

namespace brulion::dates {

/// First and last year a note can contain: tokens are written as four-digit "YYYY-MM-DD".
/// (QDate has no year 0, so 0001 is the first.) Results outside this range are rejected.
inline constexpr int MinSupportedYear = 1;
inline constexpr int MaxSupportedYear = 9999;

/// A valid QDate within MinSupportedYear…MaxSupportedYear.
bool isSupportedDate(QDate date);

/// Parse a date as written in notes: "YYYY-MM-DD", "YYYY-MM-DD HH:mm" or "YYYY-MM-DDTHH:mm".
/// Lenient about details that occur in imported files: surrounding whitespace, several spaces
/// before the time, a one-digit hour ("9:05") and seconds ("09:00:59", dropped). Returns
/// nullopt for anything else, including impossible dates ("2026-02-30") and times ("24:00").
std::optional<DateSpec> parseDateSpec(QStringView text);

/// Strict "YYYY-MM-DD" (no whitespace, existing calendar day), else nullopt.
std::optional<QDate> parseIsoDate(QStringView text);

/// Wall-clock time "H:mm" or "HH:mm" (00:00–23:59, surrounding whitespace allowed), else nullopt.
std::optional<QTime> parseClockTime(QStringView text);

/// Chronological order for sorting: negative when `a` is earlier. A bare date sorts before any
/// date-time on the same day; invalid specs sort after all valid ones (and equal each other).
int compareDateSpecs(const DateSpec &a, const DateSpec &b);

/// First day of the week containing `date` for the given first weekday.
QDate startOfWeek(QDate date, Qt::DayOfWeek weekStart = Qt::Monday);
/// Last day of the week containing `date`.
QDate endOfWeek(QDate date, Qt::DayOfWeek weekStart = Qt::Monday);
/// First day of the month containing `date`.
QDate startOfMonth(QDate date);
/// Last day of the month containing `date`.
QDate endOfMonth(QDate date);

/// A week-start setting from untrusted input (synced config, QML): 1–6 are Monday–Saturday,
/// 0 and 7 are Sunday (both the JavaScript and the Qt convention); anything else is Monday.
Qt::DayOfWeek sanitizeWeekStart(int value);

} // namespace brulion::dates
