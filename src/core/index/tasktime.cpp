#include "index/tasktime.h"

#include "index/vaultindex_p.h"
#include "markdown/logbook.h"

#include <QHash>
#include <QSet>

#include <algorithm>

namespace brulion {

using namespace Qt::StringLiterals;

namespace {

/// The tags of a block as queries see them: its own, its ancestors' and the page's.
QStringList effectiveTags(const ParsedNote &note, int block)
{
    QStringList out = index_detail::pageLevelTags(note);
    for (int b = block; b >= 0 && b < note.blocks.size(); b = note.blocks[b].parent) {
        for (const QString &t : note.blocks[b].tags) {
            if (!out.contains(t))
                out.append(t);
        }
    }
    return out;
}

} // namespace

qint64 taskSpentSeconds(const Task &task, const QDateTime &now)
{
    // A running clock as the summaries count it (markdown::clockedSecondsByDay()).
    const qint64 running = task.clockStart.isValid() && now.isValid() ? task.clockStart.secsTo(now) : 0;
    return task.clockedSeconds + (running > 0 && running <= markdown::MaxClockSeconds ? running : 0);
}

TimeSummary summarizeTime(const VaultIndex &index, const QList<TaskRef> &tasks, TimeGroup group, QDate from, QDate to,
                          const QDateTime &now)
{
    TimeSummary out;
    QHash<QString, int> rowOf;
    QList<QSet<QString>> counted; // per row: the tasks counted in it
    const auto add = [&](const QString &key, const QString &label, qint64 seconds, const QString &task,
                         const VaultPath &path, int line) {
        auto it = rowOf.constFind(key);
        if (it == rowOf.constEnd()) {
            it = rowOf.insert(key, int(out.rows.size()));
            out.rows.append(TimeSummaryRow{.key = key, .label = label, .path = path, .line = line});
            counted.emplaceBack();
        }
        TimeSummaryRow &row = out.rows[*it];
        row.seconds += seconds;
        if (!counted[*it].contains(task)) {
            counted[*it].insert(task);
            ++row.tasks;
        }
    };
    for (const TaskRef &ref : tasks) {
        if (ref.task.clockedSeconds <= 0 && !ref.task.clockStart.isValid())
            continue;
        const ParsedNote *note = index.note(ref.path);
        if (!note || ref.blockIndex < 0 || ref.blockIndex >= note->blocks.size())
            continue;
        const Block &block = note->blocks[ref.blockIndex];
        const markdown::Logbook book = markdown::readLogbook(block.raw);
        const QMap<QDate, qint64> days = markdown::clockedSecondsByDay(book.clocks, now);
        qint64 seconds = 0;
        const QString taskKey = ref.path + u':' + QString::number(block.line);
        for (auto [day, daySeconds] : days.asKeyValueRange()) {
            if ((from.isValid() && day < from) || (to.isValid() && day > to))
                continue;
            seconds += daySeconds;
            if (group == TimeGroup::Day) {
                const QString iso = day.toString(Qt::ISODate);
                add(iso, iso, daySeconds, taskKey, {}, -1);
            }
        }
        if (seconds <= 0)
            continue;
        out.total += seconds;
        ++out.tasks;
        switch (group) {
        case TimeGroup::Day: break;
        case TimeGroup::Page: add(ref.path, note->title, seconds, taskKey, ref.path, -1); break;
        case TimeGroup::Task: add(taskKey, ref.task.text, seconds, taskKey, ref.path, block.line); break;
        case TimeGroup::Tag: {
            const QStringList tags = effectiveTags(*note, ref.blockIndex);
            if (tags.isEmpty())
                add(QString(), QString(), seconds, taskKey, {}, -1);
            for (const QString &t : tags)
                add(t, t, seconds, taskKey, {}, -1);
            break;
        }
        }
    }
    if (group == TimeGroup::Day) {
        std::ranges::sort(out.rows, {}, &TimeSummaryRow::key);
    } else {
        std::ranges::sort(out.rows, [](const TimeSummaryRow &a, const TimeSummaryRow &b) {
            if (a.seconds != b.seconds)
                return a.seconds > b.seconds;
            if (a.label != b.label)
                return a.label < b.label;
            return a.key < b.key;
        });
    }
    return out;
}

} // namespace brulion
