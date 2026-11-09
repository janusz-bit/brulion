// Calendar entries of a vault (see calendar.h).
#include "reminders/calendar.h"

#include "index/vaultindex.h"
#include "reminders/reminders_p.h"

#include <algorithm>

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
    static QList<CalendarItem> sorted(QList<const CalendarItem *> items)
    {
        std::ranges::stable_sort(items, [](const CalendarItem *a, const CalendarItem *b) {
            if (a->date != b->date)
                return a->date < b->date;
            if (a->time.has_value() != b->time.has_value())
                return a->time.has_value(); // timed items before all-day ones
            return a->time && *a->time < *b->time;
        });
        QList<CalendarItem> out;
        out.reserve(items.size());
        for (const CalendarItem *item : std::as_const(items))
            out.append(*item);
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
        QString title;
        const auto add = [&](CalendarItem::Kind kind, DateSpec spec, DateSpec written) {
            if (title.isNull())
                title = titleFor(task.text, path);
            m_items.append({.date = spec.date,
                            .time = spec.time,
                            .kind = kind,
                            .title = title,
                            .path = path,
                            .line = task.line,
                            .status = task.status,
                            .recurring = rule.has_value(),
                            .written = written,
                            .rule = task.recurrence,
                            .priority = task.priority});
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
            m_blockSource = sourceOf(m_note ? m_note : m_index.note(ref.path), ref);
            m_blockTitle = QString();
        }
        const ReminderSource &src = m_blockSource;
        const QList<DateSpec> specs = reminderDates(ref, !isClosed(src.task), m_from, m_to);
        if (specs.isEmpty())
            return;
        const bool recurring = ruleOf(ref.reminder.recurrence).has_value();
        if (m_blockTitle.isNull())
            m_blockTitle = titleFor(src.text, ref.path);
        for (const DateSpec &spec : specs) {
            m_items.append({.date = spec.date,
                            .time = spec.time,
                            .kind = CalendarItem::Reminder,
                            .title = m_blockTitle,
                            .path = ref.path,
                            .line = ref.reminder.line,
                            .status = src.task ? std::optional(src.task->status) : std::nullopt,
                            .recurring = recurring,
                            .alarm = ref.reminder.alarm,
                            .written = ref.reminder.at,
                            .rule = ref.reminder.recurrence,
                            .priority = src.task ? src.task->priority : Priority::None});
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
    const auto collect = [&all](const QList<CalendarItem> &list) {
        for (const CalendarItem &item : list)
            all.append(&item);
    };
    collect(journals);
    for (const auto &[path, items] : d->notes.values())
        collect(items.tasks);
    for (const auto &[path, items] : d->notes.values())
        collect(items.reminders);
    return CalendarBuilder::sorted(std::move(all));
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
