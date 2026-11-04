// Task helpers the editor, the planning views and the shell share (docs/SYNTAX.md §5): one task
// clock at a time ("Time tracking and status history") — what they call before a clock starts —
// and the renderer's view of `⛔ id` ("Dependencies"). Part of the EditorHost library
// (brulion_editorhost).
#pragma once

#include "markdown/inlinerenderer.h"

#include <QDateTime>
#include <QString>

#include <functional>
#include <optional>

namespace brulion {

class EditorHost;
class VaultIndex;

/// markdown::RenderContext::taskDependency from `index` (the first task carrying `🆔 id`); an
/// empty function for a null index. The index must outlive the rendering (it is used at once).
std::function<std::optional<markdown::TaskDependencyView>(const QString &id)>
taskDependencyResolver(const VaultIndex *index);

/// Stop every running clock of the notebook (EditorHost::taskIndex()) except those in
/// `exceptPath` (the caller edits that file itself) — each file read and written through `host`
/// (markdown::stopTaskClock() at `now`). Returns how many were stopped.
int stopTaskClocksElsewhere(EditorHost &host, const QDateTime &now, const QString &exceptPath = QString());

/// `content` (the file `path`) with its running clocks stopped at `now`, except the task block at
/// `keepLine` (-1: none kept).
QString stopTaskClocksIn(const QString &content, const QDateTime &now, int keepLine = -1);

} // namespace brulion
