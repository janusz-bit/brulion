// Obsidian Tasks dependencies (docs/SYNTAX.md §5 "Dependencies"): a task's id `🆔 abc123` and the
// ids it waits for `⛔ abc123,def456`, and the subtask progress of a parent task ("3/5"). Part of
// the markdown module — see markdown/markdown.h.
//
// The edits are "content in -> content out" on a whole file and the task at `line` (any line of
// it); only the task's first line changes, written in Obsidian Tasks' order (description, 🆔, ⛔,
// priority, 🔁, dates…): a new 🆔 goes before the first metadata token of the line (at its end when
// it has none, before a trailing block id), a new ⛔ right after the 🆔.
#pragma once

#include "types.h"

#include <QString>
#include <QStringList>
#include <QStringView>

#include <functional>

namespace brulion::markdown {

/// An Obsidian Tasks id: one or more of [A-Za-z0-9_-].
bool isValidTaskId(QStringView id);
/// A new id, 6 characters [a-z0-9] as Obsidian Tasks makes them, not one `isTaken` refuses.
QString generateTaskId(const std::function<bool(const QString &)> &isTaken = {});

/// The task at `line` gets id `id` (an existing 🆔 is replaced; "" removes it). Not a task, an
/// invalid id or the same id: unchanged.
QString setTaskId(const QString &content, int line, const QString &id);
/// The task at `line` waits for `id` too: ",id" added to its ⛔ token, else ` ⛔ id` written after
/// its 🆔 (where a 🆔 would go when it has none). Already listed, not a task or an invalid id:
/// unchanged.
QString addTaskDependency(const QString &content, int line, const QString &id);
/// `id` taken out of the task's ⛔ tokens; a token left without ids goes (with one adjacent
/// space). Not listed: unchanged.
QString removeTaskDependency(const QString &content, int line, const QString &id);

/// Subtask progress of a task block (SYNTAX §5 "Subtasks"): the tasks among its descendants (at
/// any depth); `done` counts the done ones, `total` all but the cancelled ones (a cancelled
/// subtask is neither done nor waiting). total 0: nothing to show.
struct SubtaskProgress {
    int done = 0;
    int total = 0;
    friend bool operator==(const SubtaskProgress &, const SubtaskProgress &) = default;
};
SubtaskProgress subtaskProgress(const ParsedNote &note, int blockIndex);

} // namespace brulion::markdown
