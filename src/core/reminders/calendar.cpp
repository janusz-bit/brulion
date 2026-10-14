// Calendar entries of a vault (see calendar.h).
#include "reminders/calendar.h"

#include "index/vaultindex.h"
#include "reminders/reminders_p.h"

#include <algorithm>

namespace brulion {

using namespace reminders_detail;

namespace {

/// Collects the items of one date range.
class CalendarBuilder {
public:
    CalendarBuilder(const VaultIndex &index, QDate from, QDate to) : m_index(index), m_from(from), m_to(to) { }

    void addJournals()
    {
        // Newest first, equal dates by path; the final sort orders the dates.
        const QList<JournalRef> journals = m_index.journals();
        for (const JournalRef &journal : journals) {
            if (journal.date < m_from || journal.date > m_to)
                continue;
            CalendarItem item;
            item.date = journal.date;
            item.kind = CalendarItem::Journal;
            item.title = pageTitle(journal.path);
            item.path = journal.path;
            m_items.append(std::move(item));
        }
    }

    void addTasks()
    {
        const QList<TaskRef> tasks = m_index.tasks();
        for (const TaskRef &ref : tasks)
            addTask(ref);
    }

    void addReminders()
    {
        const QList<ReminderRef> reminders = m_index.reminders();
        for (const ReminderRef &ref : reminders)
            addReminder(ref);
    }

    QList<CalendarItem> take()
    {
        std::stable_sort(m_items.begin(), m_items.end(), [](const CalendarItem &a, const CalendarItem &b) {
            if (a.date != b.date)
                return a.date < b.date;
            if (a.time.has_value() != b.time.has_value())
                return a.time.has_value(); // timed items before all-day ones
            return a.time && *a.time < *b.time;
        });
        return std::move(m_items);
    }

private:
    QString pageTitle(const VaultPath &path) const
    {
        const ParsedNote *note = m_index.note(path);
        return note ? note->title : QString();
    }

    QString titleFor(const QString &markdown, const VaultPath &path) const
    {
        const QString plain = plainText(markdown, &m_index);
        return plain.isEmpty() ? pageTitle(path) : plain;
    }

    void addTask(const TaskRef &ref)
    {
        const Task &task = ref.task;
        const std::optional<dates::RecurrenceRule> rule = ruleOf(task.recurrence);
        // Done / cancelled series stop: only their written dates are shown.
        const std::optional<dates::RecurrenceRule> expand = isClosed(task) ? std::nullopt : rule;
        // The dates of a series move together: each keeps its distance to the reference date.
        const std::optional<DateSpec> reference = seriesReference(task);
        const bool recurring = rule.has_value();
        QString title;
        const auto add = [&](CalendarItem::Kind kind, const DateSpec &spec) {
            if (title.isNull())
                title = titleFor(task.text, ref.path);
            CalendarItem item;
            item.date = spec.date;
            item.time = spec.time;
            item.kind = kind;
            item.title = title;
            item.path = ref.path;
            item.line = task.line;
            item.status = task.status;
            item.recurring = recurring;
            m_items.append(std::move(item));
        };
        const auto addSeries = [&](CalendarItem::Kind kind, const std::optional<DateSpec> &anchor) {
            if (!anchor)
                return;
            for (const DateSpec &spec : datesWithin(*anchor, expand, m_from, m_to, reference))
                add(kind, spec);
        };
        addSeries(CalendarItem::Due, task.due);
        addSeries(CalendarItem::Scheduled, task.scheduled);
        addSeries(CalendarItem::Start, task.start);
        if (task.doneDate && *task.doneDate >= m_from && *task.doneDate <= m_to)
            add(CalendarItem::Done, DateSpec{*task.doneDate, std::nullopt});
    }

    void addReminder(const ReminderRef &ref)
    {
        // index.reminders() lists the reminders of a block together: its source and title are
        // computed once per block (a huge block with many reminders costs its size once).
        if (ref.path != m_blockPath || ref.blockIndex != m_blockIndex) {
            m_blockPath = ref.path;
            m_blockIndex = ref.blockIndex;
            m_blockSource = sourceOf(m_index, ref);
            m_blockTitle = QString();
        }
        const ReminderSource &src = m_blockSource;
        const QList<DateSpec> specs = reminderDates(ref, !isClosed(src.task), m_from, m_to);
        if (specs.isEmpty())
            return;
        const bool recurring = ruleOf(ref.reminder.recurrence).has_value();
        if (m_blockTitle.isNull())
            m_blockTitle = titleFor(src.text, ref.path);
        const QString &title = m_blockTitle;
        for (const DateSpec &spec : specs) {
            CalendarItem item;
            item.date = spec.date;
            item.time = spec.time;
            item.kind = CalendarItem::Reminder;
            item.title = title;
            item.path = ref.path;
            item.line = ref.reminder.line;
            if (src.task)
                item.status = src.task->status;
            item.recurring = recurring;
            item.alarm = ref.reminder.alarm;
            m_items.append(std::move(item));
        }
    }

    const VaultIndex &m_index;
    const QDate m_from;
    const QDate m_to;
    QList<CalendarItem> m_items;
    // The block of the previous reminder (see addReminder()).
    VaultPath m_blockPath;
    int m_blockIndex = -1;
    ReminderSource m_blockSource;
    QString m_blockTitle;
};

} // namespace

QList<CalendarItem> calendarItems(const VaultIndex &index, QDate from, QDate to)
{
    if (!from.isValid() || !to.isValid() || from > to)
        return {};
    CalendarBuilder builder(index, from, to);
    builder.addJournals();
    builder.addTasks();
    builder.addReminders();
    return builder.take();
}

} // namespace brulion
