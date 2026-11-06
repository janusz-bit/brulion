#include "reminders/dayplan.h"

#include <QHash>

#include <algorithm>

namespace brulion {

QList<DayPlanEntry> dayPlan(const QList<CalendarItem> &items, QDate day)
{
    QList<DayPlanEntry> out;
    QHash<QString, qsizetype> rowOfBlock;
    for (const CalendarItem &item : items) {
        if (item.date != day
            || (item.kind != CalendarItem::Due && item.kind != CalendarItem::Scheduled
                && item.kind != CalendarItem::Reminder))
            continue;
        const QString key = item.path + u'\n' + QString::number(item.line);
        auto found = rowOfBlock.constFind(key);
        if (found == rowOfBlock.constEnd()) {
            found = rowOfBlock.insert(key, out.size());
            DayPlanEntry entry;
            entry.title = item.title;
            entry.path = item.path;
            entry.line = item.line;
            entry.status = item.status;
            out.append(entry);
        }
        DayPlanEntry &entry = out[*found];
        if (item.time && (!entry.time || *item.time < *entry.time))
            entry.time = item.time;
        if (!entry.status)
            entry.status = item.status;
        entry.due = entry.due || item.kind == CalendarItem::Due;
        entry.scheduled = entry.scheduled || item.kind == CalendarItem::Scheduled;
        entry.reminder = entry.reminder || item.kind == CalendarItem::Reminder;
        entry.alarm = entry.alarm || item.alarm;
        entry.recurring = entry.recurring || item.recurring;
    }
    std::stable_sort(out.begin(), out.end(), [](const DayPlanEntry &a, const DayPlanEntry &b) {
        if (a.time.has_value() != b.time.has_value())
            return a.time.has_value();
        return a.time && *a.time < *b.time;
    });
    return out;
}

} // namespace brulion
