// Calendar entries of a vault (see calendar.h).
#include "reminders/calendar.h"

#include "index/vaultindex.h"
#include "reminders/reminders_p.h"

#include <algorithm>
#include <numeric>

namespace brulion {

using namespace reminders_detail;

namespace {

/// Collects the items of one date range into `out`.
class CalendarBuilder {
public:
    CalendarBuilder(const VaultIndex &index, QDate from, QDate to, QList<CalendarItem> &out)
        : m_index(index), m_from(from), m_to(to), m_items(out)
    { }

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

    /// The task dates of one note (document order: ParsedNote::tasks lists its blocks' tasks, as
    /// index.tasks() does).
    void addTasks(const VaultPath &path, const ParsedNote &note)
    {
        for (const Task &task : note.tasks)
            addTask(path, task);
    }

    /// The reminders of one note (document order).
    void addReminders(const VaultPath &path, const ParsedNote &note)
    {
        m_note = &note;
        const QList<ReminderRef> reminders = reminders_detail::remindersOf(path, note, m_from, m_to);
        for (const ReminderRef &ref : reminders)
            addReminder(ref);
        m_note = nullptr;
    }

    /// The final order of `items`: by date; within a day timed items first (by time), then
    /// all-day ones; ties in the given order (a stable sort). Sorted through pointers, so every
    /// item is copied once.
    static QList<CalendarItem> sorted(const QList<const CalendarItem *> &items)
    {
        QList<qsizetype> order(items.size());
        std::iota(order.begin(), order.end(), 0);
        std::sort(order.begin(), order.end(), [&items](qsizetype x, qsizetype y) {
            const CalendarItem &a = *items[x];
            const CalendarItem &b = *items[y];
            if (a.date != b.date)
                return a.date < b.date;
            if (a.time.has_value() != b.time.has_value())
                return a.time.has_value(); // timed items before all-day ones
            if (a.time && *a.time != *b.time)
                return *a.time < *b.time;
            return x < y;
        });
        QList<CalendarItem> out;
        out.reserve(items.size());
        for (const qsizetype i : std::as_const(order))
            out.append(*items[i]);
        return out;
    }

    /// The dates of one task (see calendarItems()).
    void addTask(const VaultPath &path, const Task &task)
    {
        const std::optional<dates::RecurrenceRule> rule = ruleOf(task.recurrence);
        // Done / cancelled series stop: only their written dates are shown.
        const std::optional<dates::RecurrenceRule> expand = isClosed(task) ? std::nullopt : rule;
        // The dates of a series move together: each keeps its distance to the reference date.
        const std::optional<DateSpec> reference = seriesReference(task);
        const bool recurring = rule.has_value();
        QString title;
        const auto add = [&](CalendarItem::Kind kind, const DateSpec &spec, const DateSpec &written) {
            if (title.isNull())
                title = titleFor(task.text, path);
            CalendarItem item;
            item.date = spec.date;
            item.time = spec.time;
            item.kind = kind;
            item.title = title;
            item.path = path;
            item.line = task.line;
            item.status = task.status;
            item.recurring = recurring;
            item.written = written;
            item.rule = task.recurrence;
            item.priority = task.priority;
            m_items.append(std::move(item));
        };
        const auto addSeries = [&](CalendarItem::Kind kind, const std::optional<DateSpec> &anchor) {
            if (!anchor)
                return;
            for (const DateSpec &spec : datesWithin(*anchor, expand, m_from, m_to, reference))
                add(kind, spec, *anchor);
        };
        addSeries(CalendarItem::Due, task.due);
        addSeries(CalendarItem::Scheduled, task.scheduled);
        addSeries(CalendarItem::Start, task.start);
        if (task.doneDate && *task.doneDate >= m_from && *task.doneDate <= m_to)
            add(CalendarItem::Done, DateSpec{*task.doneDate, std::nullopt}, DateSpec{*task.doneDate, std::nullopt});
    }

    /// The dates of one reminder; the reminders of a block one after another.
    void addReminder(const ReminderRef &ref)
    {
        // index.reminders() lists the reminders of a block together: its source and title are
        // computed once per block (a huge block with many reminders costs its size once).
        if (ref.path != m_blockPath || ref.blockIndex != m_blockIndex) {
            m_blockPath = ref.path;
            m_blockIndex = ref.blockIndex;
            m_blockSource = m_note ? sourceOf(m_note, ref) : sourceOf(m_index, ref);
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
            item.written = ref.reminder.at;
            item.rule = ref.reminder.recurrence;
            if (src.task)
                item.priority = src.task->priority;
            m_items.append(std::move(item));
        }
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

    const VaultIndex &m_index;
    const QDate m_from;
    const QDate m_to;
    QList<CalendarItem> &m_items;
    const ParsedNote *m_note = nullptr; ///< the note of addReminders()
    // The block of the previous reminder (see addReminder()).
    VaultPath m_blockPath;
    int m_blockIndex = -1;
    ReminderSource m_blockSource;
    QString m_blockTitle;
};

/// The items of one note within the range.
struct NoteItems {
    QList<CalendarItem> tasks;
    QList<CalendarItem> reminders;
};

} // namespace

struct CalendarCache::Private {
    NoteCache<NoteItems> notes; ///< notes with items in the range
    NoteCacheSync last;
};

CalendarCache::CalendarCache() : d(std::make_unique<Private>()) { }
CalendarCache::~CalendarCache() = default;
CalendarCache::CalendarCache(CalendarCache &&) noexcept = default;
CalendarCache &CalendarCache::operator=(CalendarCache &&) noexcept = default;

QList<CalendarItem> CalendarCache::items(const VaultIndex &index, QDate from, QDate to)
{
    if (!from.isValid() || !to.isValid() || from > to)
        return {};
    const QString key = from.toString(Qt::ISODate) + u'|' + to.toString(Qt::ISODate);
    d->last = d->notes.sync(
            index, key, false,
            [&](const VaultPath &path, const ParsedNote &note) {
                NoteItems items;
                if (note.tasks.isEmpty() && note.reminders.isEmpty())
                    return items;
                CalendarBuilder(index, from, to, items.tasks).addTasks(path, note);
                CalendarBuilder(index, from, to, items.reminders).addReminders(path, note);
                return items;
            },
            [](const NoteItems &items) { return !items.tasks.isEmpty() || !items.reminders.isEmpty(); },
            [&](std::map<VaultPath, NoteItems> &values) {
                // Everything: the same items from the index's task and reminder lists (notes by
                // path, document order) — contiguous, so cheaper to walk than every note.
                const QList<TaskRef> tasks = index.tasks();
                for (qsizetype i = 0; i < tasks.size();) {
                    const VaultPath &path = tasks.at(i).path;
                    NoteItems items;
                    CalendarBuilder builder(index, from, to, items.tasks);
                    for (; i < tasks.size() && tasks.at(i).path == path; ++i)
                        builder.addTask(path, tasks.at(i).task);
                    if (!items.tasks.isEmpty())
                        values.emplace_hint(values.end(), path, std::move(items));
                }
                forEachNoteReminders(index, from, to, [&](const VaultPath &path, const QList<ReminderRef> &refs) {
                    QList<CalendarItem> list;
                    CalendarBuilder builder(index, from, to, list);
                    for (const ReminderRef &ref : refs)
                        builder.addReminder(ref);
                    if (!list.isEmpty())
                        values[path].reminders = std::move(list);
                });
            });
    // Journals, then every note's task dates (notes by path), then every note's reminders —
    // the order ties keep.
    QList<CalendarItem> journals;
    CalendarBuilder(index, from, to, journals).addJournals();
    QList<const CalendarItem *> all;
    for (const CalendarItem &item : std::as_const(journals))
        all.append(&item);
    for (const auto &[path, items] : d->notes.values()) {
        for (const CalendarItem &item : items.tasks)
            all.append(&item);
    }
    for (const auto &[path, items] : d->notes.values()) {
        for (const CalendarItem &item : items.reminders)
            all.append(&item);
    }
    return CalendarBuilder::sorted(all);
}

const NoteCacheSync &CalendarCache::lastSync() const
{
    return d->last;
}

void CalendarCache::clear()
{
    d->notes.clear();
    d->last = NoteCacheSync();
}

QList<CalendarItem> calendarItems(const VaultIndex &index, QDate from, QDate to)
{
    return CalendarCache().items(index, from, to);
}

} // namespace brulion
