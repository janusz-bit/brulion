// Task dependencies and clocks as the app's views use them (docs/SYNTAX.md §5, docs/UI.md "Tasks:
// dependencies, subtasks and time"): the dependency picker's candidates, a task's relations for
// its menu and the running clocks — maps for QML, over a VaultIndex. Part of the notebook module.
#pragma once

#include "index/vaultindex.h"
#include "types.h"

#include <QDateTime>
#include <QList>
#include <QString>
#include <QVariantList>
#include <QVariantMap>

#include <optional>

namespace brulion::notebook {

/// A task's place as the index has it.
struct TaskPlace {
    VaultPath path;
    int line = 0; ///< the task block's first line
    int blockIndex = -1;
    Task task;
};

/// The first task carrying `🆔 id` (tasksWithId(): notes by path), nullopt when none.
std::optional<TaskPlace> firstTaskWithId(const VaultIndex &index, const QString &id);
/// The tasks with a running clock (VaultIndex::runningClocks()).
QList<TaskPlace> runningClockPlaces(const VaultIndex &index);
/// The task block at `line` of `path` (any line of it), nullopt when it holds no task.
std::optional<TaskPlace> taskAt(const VaultIndex &index, const VaultPath &path, int line);

/// The dependency picker: tasks that are not done or cancelled whose text contains every word of
/// `text` (case- and diacritic-insensitive; "" = all), those starting with it first, then by page
/// title and line; the task at `excludePath`:`excludeLine` and the ones it already waits for are
/// left out. [{path, line, text, title, id, status}], at most `limit`.
QVariantList dependencyCandidates(const VaultIndex &index, const QString &text, const VaultPath &excludePath,
                                  int excludeLine, int limit = 30);

/// What a task's menu and hint show: {isTask, id, blocked, blocking, cycle, missing, dependsOn:
/// [{id, path, line, text, title, open, missing}], dependents: [{path, line, text, title}],
/// clockRunning, clockStart ("HH:mm"), clockStartIso, clockedSeconds, progressDone, progressTotal};
/// {isTask: false} for a line that holds no task.
QVariantMap taskRelations(const VaultIndex &index, const VaultPath &path, int line);

} // namespace brulion::notebook
