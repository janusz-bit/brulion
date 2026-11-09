// VaultIndex: task dependencies (Obsidian Tasks `🆔` / `⛔`, docs/SYNTAX.md §5 "Dependencies") and
// running clocks. The id maps are kept by attachNote() / detachNote(); noteChanged() logs the notes
// whose tasks a change blocks or unblocks.
#include "index/vaultindex.h"
#include "index/vaultindex_p.h"

#include <QSet>

#include <algorithm>

namespace brulion {

using namespace index_detail;

namespace {

bool isClosed(TaskStatus s)
{
    return s == TaskStatus::Done || s == TaskStatus::Cancelled;
}

/// A task's place, for visited sets.
QString placeKey(const VaultPath &path, int block)
{
    return path + u'#' + QString::number(block);
}

/// Most tasks a cycle search visits (a pathological vault stays cheap).
constexpr int MaxCycleSearch = 10000;

} // namespace

void VaultIndex::Private::forTasksWithId(const QString &id,
                                         const std::function<void(const Entry &, int, const Task &)> &f) const
{
    const auto owners = taskIdOwners.constFind(id);
    if (owners == taskIdOwners.constEnd())
        return;
    QList<VaultPath> paths;
    owners->forEach([&](const VaultPath &p) { paths.append(p); });
    std::ranges::sort(paths);
    for (const VaultPath &p : std::as_const(paths)) {
        const Entry *e = entry(p);
        if (!e)
            continue;
        for (const int i : e->taskBlocks()) {
            const Task &t = *e->note.blocks[i].task;
            if (t.id == id)
                f(*e, i, t);
        }
    }
}

bool VaultIndex::Private::taskBlocked(const Entry &e, int block) const
{
    if (block < 0 || block >= e.note.blocks.size() || !e.note.blocks[block].task)
        return false;
    const Task &task = *e.note.blocks[block].task;
    if (isClosed(task.status) || task.dependsOn.isEmpty())
        return false;
    bool blocked = false;
    for (const QString &dep : task.dependsOn) {
        forTasksWithId(dep, [&](const Entry &other, int i, const Task &t) {
            if (!(&other == &e && i == block) && !isClosed(t.status))
                blocked = true;
        });
        if (blocked)
            return true;
    }
    return false;
}

bool VaultIndex::Private::taskBlocking(const Entry &e, int block) const
{
    if (block < 0 || block >= e.note.blocks.size() || !e.note.blocks[block].task)
        return false;
    const Task &task = *e.note.blocks[block].task;
    if (task.id.isEmpty() || isClosed(task.status))
        return false;
    const auto notes = taskDependents.constFind(task.id);
    if (notes == taskDependents.constEnd())
        return false;
    bool blocking = false;
    notes->forEach([&](const VaultPath &p) {
        const Entry *other = blocking ? nullptr : entry(p);
        if (!other)
            return;
        for (const int i : other->taskBlocks()) {
            const Task &t = *other->note.blocks[i].task;
            if (!(other == &e && i == block) && !isClosed(t.status) && t.dependsOn.contains(task.id)) {
                blocking = true;
                return;
            }
        }
    });
    return blocking;
}

QList<TaskRef> VaultIndex::tasksWithId(const QString &id) const
{
    QList<TaskRef> out;
    d->forTasksWithId(id, [&](const Entry &e, int i, const Task &t) { out.append(TaskRef{e.path, i, t}); });
    return out;
}

bool VaultIndex::hasTaskId(const QString &id) const
{
    return d->taskIdOwners.contains(id);
}

bool VaultIndex::isTaskBlocked(const VaultPath &path, int blockIndex) const
{
    const Private::Entry *e = d->entry(path);
    return e && d->taskBlocked(*e, blockIndex);
}

bool VaultIndex::isTaskBlocking(const VaultPath &path, int blockIndex) const
{
    const Private::Entry *e = d->entry(path);
    return e && d->taskBlocking(*e, blockIndex);
}

TaskDependencies VaultIndex::taskDependencies(const VaultPath &path, int blockIndex) const
{
    TaskDependencies out;
    const Private::Entry *e = d->entry(path);
    if (!e || blockIndex < 0 || blockIndex >= e->note.blocks.size() || !e->note.blocks[blockIndex].task)
        return out;
    const Task &task = *e->note.blocks[blockIndex].task;
    for (const QString &dep : task.dependsOn) {
        TaskDependency one;
        one.id = dep;
        d->forTasksWithId(dep, [&](const Entry &other, int i, const Task &t) {
            if (&other == e && i == blockIndex) {
                out.cycle = true; // it waits for itself
                return;
            }
            one.tasks.append(TaskRef{other.path, i, t});
            if (!isClosed(t.status))
                one.open = true;
        });
        if (one.tasks.isEmpty() && !d->taskIdOwners.contains(dep))
            out.missing = true;
        out.dependsOn.append(one);
    }
    out.blocked = d->taskBlocked(*e, blockIndex);

    if (!task.id.isEmpty()) {
        const auto notes = d->taskDependents.constFind(task.id);
        if (notes != d->taskDependents.constEnd()) {
            QList<VaultPath> paths;
            notes->forEach([&](const VaultPath &p) { paths.append(p); });
            std::ranges::sort(paths);
            for (const VaultPath &p : std::as_const(paths)) {
                const Private::Entry *other = d->entry(p);
                if (!other)
                    continue;
                for (const int i : other->taskBlocks()) {
                    const Task &t = *other->note.blocks[i].task;
                    if (!(other == e && i == blockIndex) && !isClosed(t.status) && t.dependsOn.contains(task.id))
                        out.dependents.append(TaskRef{other->path, i, t});
                }
            }
        }
    }

    // A cycle: the open tasks this one waits for (and theirs, …) lead back to it.
    if (!out.cycle && !task.dependsOn.isEmpty()) {
        QSet<QString> visited{placeKey(e->path, blockIndex)};
        QList<const Task *> stack{&task};
        while (!stack.isEmpty() && !out.cycle && visited.size() < MaxCycleSearch) {
            const Task *current = stack.takeLast();
            for (const QString &dep : current->dependsOn) {
                d->forTasksWithId(dep, [&](const Entry &other, int i, const Task &t) {
                    if (&other == e && i == blockIndex) {
                        out.cycle = true;
                        return;
                    }
                    if (isClosed(t.status) || out.cycle)
                        return;
                    const QString key = placeKey(other.path, i);
                    if (visited.contains(key))
                        return;
                    visited.insert(key);
                    stack.append(&t);
                });
                if (out.cycle)
                    break;
            }
        }
    }
    return out;
}

QList<TaskRef> VaultIndex::runningClocks() const
{
    QList<TaskRef> out;
    for (const TaskRef &t : tasks()) {
        if (t.task.clockStart.isValid())
            out.append(t);
    }
    return out;
}

} // namespace brulion
