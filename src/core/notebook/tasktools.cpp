#include "notebook/tasktools.h"

#include "markdown/parse.h"
#include "markdown/taskdeps.h"
#include "text/text.h"

#include <algorithm>

namespace brulion::notebook {

using namespace Qt::StringLiterals;

namespace {

bool isClosed(TaskStatus s)
{
    return s == TaskStatus::Done || s == TaskStatus::Cancelled;
}

QString statusName(TaskStatus s)
{
    switch (s) {
    case TaskStatus::Todo: return u"todo"_s;
    case TaskStatus::Doing: return u"doing"_s;
    case TaskStatus::Done: return u"done"_s;
    case TaskStatus::Cancelled: return u"cancelled"_s;
    case TaskStatus::Deferred: return u"deferred"_s;
    }
    return {};
}

std::optional<TaskPlace> placeOf(const VaultIndex &index, const TaskRef &ref)
{
    const ParsedNote *note = index.note(ref.path);
    if (!note || ref.blockIndex < 0 || ref.blockIndex >= note->blocks.size())
        return std::nullopt;
    return TaskPlace{ref.path, note->blocks[ref.blockIndex].line, ref.blockIndex, ref.task};
}

QString titleOf(const VaultIndex &index, const VaultPath &path)
{
    const ParsedNote *note = index.note(path);
    return note ? note->title : path;
}

QVariantMap placeMap(const VaultIndex &index, const TaskPlace &p)
{
    return {{u"path"_s, p.path},
            {u"line"_s, p.line},
            {u"text"_s, p.task.text},
            {u"title"_s, titleOf(index, p.path)},
            {u"id"_s, p.task.id},
            {u"status"_s, statusName(p.task.status)},
            {u"open"_s, !isClosed(p.task.status)}};
}

} // namespace

std::optional<TaskPlace> firstTaskWithId(const VaultIndex &index, const QString &id)
{
    for (const TaskRef &t : index.tasksWithId(id)) {
        if (const auto p = placeOf(index, t))
            return p;
    }
    return std::nullopt;
}

QList<TaskPlace> runningClockPlaces(const VaultIndex &index)
{
    QList<TaskPlace> out;
    for (const TaskRef &t : index.runningClocks()) {
        if (const auto p = placeOf(index, t))
            out.append(*p);
    }
    return out;
}

std::optional<TaskPlace> taskAt(const VaultIndex &index, const VaultPath &path, int line)
{
    const ParsedNote *note = index.note(path);
    if (!note)
        return std::nullopt;
    const int b = markdown::blockAtLine(*note, line);
    if (b < 0 || !note->blocks[b].task)
        return std::nullopt;
    return TaskPlace{path, note->blocks[b].line, b, *note->blocks[b].task};
}

QVariantList dependencyCandidates(const VaultIndex &index, const QString &text, const VaultPath &excludePath,
                                  int excludeLine, int limit)
{
    const QString query = text::foldKey(text.simplified());
    const QStringList words = query.split(u' ', Qt::SkipEmptyParts);
    QStringList waitsFor;
    if (const auto self = taskAt(index, excludePath, excludeLine))
        waitsFor = self->task.dependsOn;
    struct Candidate {
        TaskPlace place;
        bool starts = false;
        QString titleKey;
    };
    QList<Candidate> found;
    for (const TaskRef &t : index.tasks()) {
        if (isClosed(t.task.status))
            continue;
        const auto place = placeOf(index, t);
        if (!place || (place->path == excludePath && place->line == excludeLine))
            continue;
        if (!t.task.id.isEmpty() && waitsFor.contains(t.task.id))
            continue;
        const QString folded = text::foldKey(t.task.text);
        if (!std::all_of(words.cbegin(), words.cend(), [&](const QString &w) { return folded.contains(w); }))
            continue;
        found.append({*place, !query.isEmpty() && folded.startsWith(query), text::nameSortKey(titleOf(index, t.path))});
    }
    std::ranges::stable_sort(found, [](const Candidate &a, const Candidate &b) {
        if (a.starts != b.starts)
            return a.starts;
        if (a.titleKey != b.titleKey)
            return a.titleKey < b.titleKey;
        if (a.place.path != b.place.path)
            return a.place.path < b.place.path;
        return a.place.line < b.place.line;
    });
    QVariantList out;
    for (const Candidate &c : std::as_const(found)) {
        if (out.size() >= limit)
            break;
        out.append(placeMap(index, c.place));
    }
    return out;
}

QVariantMap taskRelations(const VaultIndex &index, const VaultPath &path, int line)
{
    const auto self = taskAt(index, path, line);
    if (!self)
        return {{u"isTask"_s, false}};
    const TaskDependencies deps = index.taskDependencies(path, self->blockIndex);
    QVariantList dependsOn;
    for (const TaskDependency &d : deps.dependsOn) {
        if (d.tasks.isEmpty()) {
            dependsOn.append(QVariantMap{{u"id"_s, d.id}, {u"missing"_s, true}, {u"open"_s, false}});
            continue;
        }
        for (const TaskRef &t : d.tasks) {
            if (const auto p = placeOf(index, t)) {
                QVariantMap m = placeMap(index, *p);
                m.insert(u"id"_s, d.id);
                m.insert(u"missing"_s, false);
                dependsOn.append(m);
            }
        }
    }
    QVariantList dependents;
    for (const TaskRef &t : deps.dependents) {
        if (const auto p = placeOf(index, t))
            dependents.append(placeMap(index, *p));
    }
    const ParsedNote *note = index.note(path);
    const markdown::SubtaskProgress progress = markdown::subtaskProgress(*note, self->blockIndex);
    const Task &task = self->task;
    return {{u"isTask"_s, true},
            {u"path"_s, path},
            {u"line"_s, self->line},
            {u"text"_s, task.text},
            {u"id"_s, task.id},
            {u"blocked"_s, deps.blocked},
            {u"blocking"_s, index.isTaskBlocking(path, self->blockIndex)},
            {u"cycle"_s, deps.cycle},
            {u"missing"_s, deps.missing},
            {u"dependsOn"_s, dependsOn},
            {u"dependents"_s, dependents},
            {u"clockRunning"_s, task.clockStart.isValid()},
            {u"clockStart"_s, task.clockStart.isValid() ? task.clockStart.toString(u"HH:mm"_s) : QString()},
            {u"clockStartIso"_s,
             task.clockStart.isValid() ? task.clockStart.toString(u"yyyy-MM-ddTHH:mm:ss"_s) : QString()},
            {u"clockedSeconds"_s, task.clockedSeconds},
            {u"progressDone"_s, progress.done},
            {u"progressTotal"_s, progress.total}};
}

} // namespace brulion::notebook
