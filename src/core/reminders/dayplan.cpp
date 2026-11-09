#include "reminders/dayplan.h"

#include <QHash>

#include <algorithm>

namespace brulion {

QList<DayPlanEntry> dayPlan(const QList<CalendarItem> &items, QDate day)
{
    QList<DayPlanEntry> out;
    QHash<std::pair<VaultPath, int>, qsizetype> rowOfBlock;
    for (const CalendarItem &item : items) {
        if (item.date != day
            || (item.kind != CalendarItem::Due && item.kind != CalendarItem::Scheduled
                && item.kind != CalendarItem::Reminder))
            continue;
        const auto [row, added] = rowOfBlock.tryEmplace({item.path, item.line}, out.size());
        if (added) {
            out.append({.time = item.time,
                        .title = item.title,
                        .path = item.path,
                        .line = item.line,
                        .status = item.status});
        }
        DayPlanEntry &entry = out[*row];
        if (item.time && (!entry.time || *item.time < *entry.time))
            entry.time = item.time;
        if (!entry.status)
            entry.status = item.status;
        entry.due |= item.kind == CalendarItem::Due;
        entry.scheduled |= item.kind == CalendarItem::Scheduled;
        entry.reminder |= item.kind == CalendarItem::Reminder;
        entry.alarm |= item.alarm;
        entry.recurring |= item.recurring;
    }
    std::ranges::stable_sort(out, [](const DayPlanEntry &a, const DayPlanEntry &b) {
        if (a.time.has_value() != b.time.has_value())
            return a.time.has_value();
        return a.time && *a.time < *b.time;
    });
    return out;
}

} // namespace brulion
