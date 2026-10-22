// QueryHost — an optional extension of EditorHost (editorhost.h) for live ```query blocks
// (docs/SYNTAX.md §10) and tasks checked in their results. A host that can run index queries
// implements it next to EditorHost; the editor finds it with
//
//   if (auto *queries = qobject_cast<brulion::QueryHost *>(host)) …        (C++)
//   if (host && typeof host.runQuery === "function") …                      (QML)
//
// and otherwise keeps showing the query's filters only. The shell's Workspace implements it
// (also as Q_INVOKABLEs, so QML reaches it through the EditorHost pointer); EditorHost itself
// stays unchanged. Header only, no QObject: nothing to moc.
//
// runQuery(source, currentPath) returns plain QVariant data:
//   { ok: bool, error: "" | translated message, type: "tasks" | "blocks" | "pages" | "reminders",
//     total: int (matches before `limit`), complete: bool (false while the index is still
//     being built — results grow; re-run on EditorHost::indexChanged()),
//     items: [item], groups: [{ key, label, items: [item] }] (only for `group:` queries) }
//   item: { path, title (page title), folder, line (0-based; 0 for pages), blockId?, text (plain
//     block text, tokens stripped), raw (the block's own Markdown lines), depth,
//     task?: { status: "todo" | "doing" | "done" | "cancelled" | "deferred", marker, text,
//              due?, scheduled?, start? ("YYYY-MM-DD[ HH:mm]"), done?, priority ("" | "lowest" …
//              "highest"), recurrence? (canonical English rule) },
//     reminder?: { at: "YYYY-MM-DD HH:mm", alarm: bool, recurrence? } }
// setTaskStatus(path, line, status) writes the task's new status ("todo", "doing", "done",
// "cancelled", "deferred"; completing a recurring task writes its next instance; "" = no status:
// markdown::removeTaskMarker) into the file and returns whether it changed.
#pragma once

#include <QObject>
#include <QString>
#include <QVariantMap>

namespace brulion {

class QueryHost {
public:
    virtual ~QueryHost() = default;

    /// Run the query `source` (the body of a ```query block) written in the page `currentPath`
    /// (`page:this`, relative `ref:`); see the file comment for the result.
    virtual QVariantMap runQuery(const QString &source, const QString &currentPath) = 0;
    /// Set the status of the task on `line` (0-based) of `path` ("" removes the task marker);
    /// false when nothing changed.
    virtual bool setTaskStatus(const QString &path, int line, const QString &status) = 0;
};

} // namespace brulion

Q_DECLARE_INTERFACE(brulion::QueryHost, "io.github.januszbit.brulion.QueryHost/1.0")
