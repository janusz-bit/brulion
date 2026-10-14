// Date expressions of the query language (docs/SYNTAX.md §10): `due:today`, `due:<=+7d`,
// `done:this-week`, `due:overdue`, … The comparison operator is handled by the query module;
// this resolves the expression itself. Part of the dates module — see dates/dates.h.
#pragma once

#include <QDate>
#include <QStringView>

#include <optional>
#include <variant>

namespace brulion::dates {

/// Inclusive range of local dates (`from == to` for a single day).
struct DateRange {
    QDate from;
    QDate to;

    bool contains(QDate date) const { return date >= from && date <= to; }
    friend bool operator==(const DateRange &, const DateRange &) = default;
};

/// Keywords that are not ranges: `overdue` (open tasks due before today), `none` (no date),
/// `any` (any date).
enum class SpecialDate { Overdue, None, Any };

using DateExpr = std::variant<DateRange, SpecialDate>;

/// Resolve a query date expression relative to `today`:
/// * `today`, `tomorrow`, `yesterday` (also `dziś`, `dzisiaj`, `jutro`, `wczoraj`, `pojutrze`,
///   `przedwczoraj`) -> that day;
/// * `YYYY-MM-DD` (or a date-time, whose date is used) -> that day;
/// * `+7d`, `-2w`, `+1m`, `+1y`, `7d`, `+7` (days), also Polish units (`+2t`, `+1r`, `+3 dni`)
///   -> the single day at that offset;
/// * `this-week`, `next-week`, `last-week`, `this-month`, `next-month`, `last-month`,
///   `this-year`, `next-year`, `last-year`, `week`/`month`/`year` (= this) and Polish
///   `ten-tydzień`, `następny-tydzień`, `przyszły-tydzień`, `zeszły-tydzień`, `ten-miesiąc`, …
///   -> the whole period (weeks per `weekStart`);
/// * `A..B` -> from the start of A to the end of B (`today..+7d`);
/// * `overdue`/`zaległe`, `none`/`brak`, `any`/`dowolna` -> the keyword.
/// Case- and diacritic-insensitive; `_` or spaces may replace `-`. Returns nullopt when not
/// understood, when `today` is invalid, or when the range would leave years 1–9999.
std::optional<DateExpr> resolveDateExpr(QStringView expr, QDate today, Qt::DayOfWeek weekStart = Qt::Monday);

} // namespace brulion::dates
