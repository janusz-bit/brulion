// The calendar grids' keys (CalendarMonthGrid's and CalendarWeekGrid's, docs/UI.md "Calendar"): ← → ↑ ↓
// move the selected day by 1 and 7, Home / End to its week's first and last day, PageUp / PageDown
// another month or week, Enter picks the selected day, Ctrl+Enter opens its daily note. What a key
// asks is an action with its arguments (the views' viewAction); nothing: the key is not the grid's.
#pragma once

#include "ui/atrament.h"

#include <QString>
#include <QVariantMap>

#include <optional>

namespace brulion::views::atr {

struct GridKey {
    QString action; // "move" {days}, "previous", "next", "day" {date}, "open" {date}
    QVariantMap args;
};

// `key` as Atrament names it ("left", "pageup", "enter"…); `inWeek` the selected day's place in its
// week (0: the first), `selected` its date ("": none).
std::optional<GridKey> gridKey(const QString &key, bool ctrl, bool otherModifiers, int inWeek, const QString &selected);
// The same from Atrament's keystroke.
std::optional<GridKey> gridKey(const atrament::Keystroke &k, int inWeek, const QString &selected);

// A day's number on its disc in the grids (CalendarMonthGrid's dayBadge, CalendarWeekGrid's): today
// on a red one, the selected day on a dark one, another month's day faint.
struct DayDisc {
    atrament::Color today, onToday, selected, onSelected, text, faint;
    float size = 24;
    float textSize = 13;
};
atrament::Div dayDisc(const DayDisc &look, int day, bool isToday, bool isSelected, bool inMonth = true);

} // namespace brulion::views::atr
