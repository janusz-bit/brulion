#include "taskhelpers.h"

#include "editorhost.h"
#include "markdown/logbook.h"
#include "markdown/parse.h"
#include "notebook/tasktools.h"

#include <QSet>

#include <ranges>

namespace brulion {

std::function<std::optional<markdown::TaskDependencyView>(const QString &id)>
taskDependencyResolver(const VaultIndex *index)
{
    if (!index)
        return {};
    return [index](const QString &id) -> std::optional<markdown::TaskDependencyView> {
        const auto place = notebook::firstTaskWithId(*index, id);
        if (!place)
            return std::nullopt;
        const TaskStatus s = place->task.status;
        return markdown::TaskDependencyView{place->task.text, s != TaskStatus::Done && s != TaskStatus::Cancelled};
    };
}

int stopTaskClocksElsewhere(EditorHost &host, const QDateTime &now, const QString &exceptPath)
{
    const VaultIndex *index = host.taskIndex();
    if (!index)
        return 0;
    QSet<QString> files;
    for (const auto places = notebook::runningClockPlaces(*index); const notebook::TaskPlace &p : places) {
        if (p.path != exceptPath)
            files.insert(p.path);
    }
    int stopped = 0;
    for (const QString &path : std::as_const(files)) {
        bool ok = false;
        const QString before = host.read(path, &ok);
        if (!ok)
            continue;
        const QString after = stopTaskClocksIn(before, now);
        if (after != before && host.writeIfUnchanged(path, after, before) == EditorHost::WriteResult::Written)
            ++stopped;
    }
    return stopped;
}

QString stopTaskClocksIn(const QString &content, const QDateTime &now, int keepLine)
{
    const ParsedNote note = markdown::parseNote(VaultPath(), content);
    QString out = content;
    // Bottom up: stopping a clock never moves the lines above it (one line rewritten in place).
    for (const Block &block : std::views::reverse(note.blocks)) {
        if (block.task && block.task->clockStart.isValid() && block.line != keepLine)
            out = markdown::stopTaskClock(out, block.line, now);
    }
    return out;
}

} // namespace brulion
