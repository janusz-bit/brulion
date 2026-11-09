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
    qint64 total = task.clockedSeconds;
    if (task.clockStart.isValid() && now.isValid() && now > task.clockStart)
        total += task.clockStart.secsTo(now);
    return total;
}

TimeSummary summarizeTime(const VaultIndex &index, const QList<TaskRef> &tasks, TimeGroup group, QDate from, QDate to,
                          const QDateTime &now)
{
    TimeSummary out;
    QHash<QString, int> rowOf;
    QHash<QString, QSet<QString>> tasksOf; // row key -> tasks counted in it
    const auto add = [&](const QString &key, const QString &label, qint64 seconds, const QString &task,
                         const VaultPath &path, int line) {
        auto it = rowOf.constFind(key);
        int r = 0;
        if (it == rowOf.constEnd()) {
            r = int(out.rows.size());
            rowOf.insert(key, r);
            TimeSummaryRow row;
            row.key = key;
            row.label = label;
            row.path = path;
            row.line = line;
            out.rows.append(row);
        } else {
            r = *it;
        }
        out.rows[r].seconds += seconds;
        QSet<QString> &counted = tasksOf[key];
        if (!counted.contains(task)) {
            counted.insert(task);
            ++out.rows[r].tasks;
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
        for (auto d = days.cbegin(); d != days.cend(); ++d) {
            if ((from.isValid() && d.key() < from) || (to.isValid() && d.key() > to))
                continue;
            seconds += d.value();
            if (group == TimeGroup::Day) {
                const QString iso = d.key().toString(Qt::ISODate);
                add(iso, iso, d.value(), taskKey, {}, -1);
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
        std::ranges::sort(out.rows, [](const TimeSummaryRow &a, const TimeSummaryRow &b) { return a.key < b.key; });
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
