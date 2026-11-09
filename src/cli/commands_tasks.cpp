// Tasks and reminders: task list/add/set (+ done, doing, todo, cancel, defer), task start/stop
// (the :LOGBOOK: clock), task time (summaries), task depend (🆔 / ⛔), remind add/list/snooze.
#include "commands_p.h"

#include "dates/dates.h"
#include "index/query.h"
#include "index/tasktime.h"
#include "index/vaultindex.h"
#include "markdown/edit.h"
#include "markdown/logbook.h"
#include "markdown/parse.h"
#include "markdown/taskdeps.h"
#include "markdown/tokens.h"
#include "notebook/journals.h"
#include "notebook/json.h"
#include "reminders/notifications.h"
#include "reminders/snooze.h"
#include "text/text.h"
#include "util/paths.h"
#include "vault/vault.h"

#include <QFileInfo>
#include <QRegularExpression>

using namespace Qt::StringLiterals;

namespace brulion::cli {

namespace {

/// Where a new task or reminder goes: --page, else the daily note of --date, else today's.
std::optional<VaultPath> placeFor(Call &call)
{
    Vault *v = call.vault();
    if (!v)
        return std::nullopt;
    std::optional<QDate> day;
    if (call.has(u"page"_s)) {
        const auto ref = call.page(call.str(u"page"_s), false);
        if (!ref)
            return std::nullopt;
        if (!ref->journal && !ref->exists) {
            call.setError(ExitNotFound, u"page_not_found"_s, tr("No page %1 (page create makes one)").arg(ref->path),
                          {{u"path"_s, ref->path}});
            return std::nullopt;
        }
        if (ref->exists)
            return ref->path;
        day = ref->journal;
    } else {
        day = call.date(call.str(u"date"_s));
        if (!day)
            return std::nullopt;
    }
    return notebook::journalFileFor(*v, *day);
}

/// Append one line to `path` (a missing daily note is created from its template): the write JSON
/// with {line, ref, written}.
Result appendLine(Call &call, const VaultPath &path, const QString &line)
{
    int at = -1;
    const auto written = call.edit(
            path,
            [&](const QString &current, bool, QString *next) {
                *next = notebook::appendBlock(current, line);
                at = appendedLine(*next, line);
                return true;
            },
            creatingJournal(call, path));
    if (!written)
        return call.failed();
    QJsonObject o = call.writeJson(*written);
    o.insert(u"line"_s, at);
    o.insert(u"ref"_s, Call::lineRef(path, written->after, at));
    o.insert(u"written"_s, line);
    return call.ok(o, line + u"  →  "_s + path);
}

void warnIfPast(Call &call, const DateSpec &spec, const QString &arg)
{
    const QDateTime now = call.ctx.now();
    const bool past = spec.hasTime() ? spec.toLocalDateTime() < now : spec.date < now.date();
    if (past)
        call.warn(u"date_in_past"_s, tr("%1 is in the past: %2").arg(optionName(arg), spec.toString()),
                  {{u"argument"_s, arg}, {u"date"_s, spec.toString()}});
}

/// A recurrence phrase in its canonical English form ("co tydzień" -> "every week").
std::optional<QString> recurrence(Call &call)
{
    if (!call.has(u"every"_s))
        return QString();
    const QString rule = dates::canonicalRecurrence(call.str(u"every"_s));
    if (rule.isEmpty()) {
        call.setError(ExitParse, u"recurrence_not_understood"_s, tr("Not a recurrence: %1").arg(call.str(u"every"_s)),
                      {{u"examples"_s, QJsonArray{u"co tydzień"_s, u"codziennie"_s, u"co miesiąc 15."_s,
                                                  u"every 2 weeks"_s, u"every weekday"_s}}});
        return std::nullopt;
    }
    return rule;
}

Result taskList(Call &call)
{
    VaultIndex *index = call.index();
    if (!index)
        return call.failed();
    QString source;
    QList<QueryFilter> extra;
    if (call.has(u"query"_s)) {
        source = call.str(u"query"_s);
        static const QRegularExpression typed(u"(^|\\s)-?(type|typ):"_s, QRegularExpression::CaseInsensitiveOption);
        if (!typed.match(source).hasMatch())
            source.prepend(u"type:tasks "_s);
    } else {
        source = u"type:tasks"_s;
        const QString status = call.has(u"status"_s) ? call.str(u"status"_s).trimmed() : u"open"_s;
        source += u" status:"_s + QString(status).replace(u' ', u'-');
        if (call.has(u"due"_s)) {
            const QString due = call.str(u"due"_s).trimmed();
            if (due.contains(QRegularExpression(u"\\s"_s)))
                return call.fail(ExitUsage, u"bad_argument"_s,
                                 tr("--due takes one expression (today, +7d, overdue, "
                                    "this-week, 2026-10-01..2026-10-31)"));
            source += u" due:"_s + due;
        }
        if (call.has(u"tag"_s)) {
            QString tag = call.str(u"tag"_s).trimmed();
            if (tag.startsWith(u'#'))
                tag.remove(0, 1);
            QueryFilter f;
            f.kind = QueryFilterKind::Tag;
            f.values = {tag.normalized(QString::NormalizationForm_C).toLower()};
            extra.append(f);
        }
        if (call.flag(u"blocked"_s))
            source += u" is:blocked"_s;
        if (call.has(u"limit"_s))
            source += u" limit:"_s + QString::number(std::clamp(call.integer(u"limit"_s, 500), 1, MaxQueryLimit));
    }
    if (call.has(u"page"_s)) {
        const auto ref = call.page(call.str(u"page"_s), true);
        if (!ref)
            return call.failed();
        QueryFilter f;
        f.kind = QueryFilterKind::Path;
        f.values = {ref->path};
        extra.append(f);
    }
    const std::variant<Query, QString> parsed = parseQuery(source);
    if (const QString *message = std::get_if<QString>(&parsed))
        return call.fail(ExitParse, u"query_error"_s, *message, {{u"query"_s, source}});
    Query query = std::get<Query>(parsed);
    query.filters += extra;
    QueryOptions options;
    options.now = call.ctx.now().time();
    QJsonObject result = notebook::queryResultJson(
            *index, runQuery(query, *index, call.ctx.today(), call.vault()->config().weekStartsOn, options));
    addRefs(call, result);
    result.insert(u"query"_s, source);
    QString text;
    for (const QJsonValue &v : result.value(u"items"_s).toArray()) {
        const QJsonObject o = v.toObject();
        QString marks;
        if (o.value(u"blocked"_s).toBool())
            marks += u"⛔ "_s;
        if (const QJsonObject sub = o.value(u"subtasks"_s).toObject(); !sub.isEmpty())
            marks += u"%1/%2 "_s.arg(sub.value(u"done"_s).toInt()).arg(sub.value(u"total"_s).toInt());
        text += u'[' + o.value(u"task"_s).toObject().value(u"status"_s).toString() + u"] "_s + marks
                + o.value(u"text"_s).toString().simplified() + u"  "_s + o.value(u"ref"_s).toString() + u'\n';
    }
    return call.ok(result, text);
}

Result taskAdd(Call &call)
{
    const QString text = cleanItemText(call.text(u"text"_s));
    if (text.isEmpty())
        return call.fail(ExitUsage, u"text_required"_s, tr("The task needs a text"));
    QString line = u"- [ ] "_s + text;
    if (call.has(u"priority"_s)) {
        const auto p = notebook::priorityFromName(call.str(u"priority"_s));
        if (!p)
            return call.fail(ExitUsage, u"bad_argument"_s,
                             tr("--priority is highest, high, medium, low, lowest (or najwyższy … najniższy)"));
        if (*p != Priority::None)
            line += u' ' + markdown::tokens::priorityEmoji(*p).toString();
    }
    const struct {
        const char *arg = nullptr;
        QStringView token;
    } dated[] = {{"start", markdown::tokens::Start},
                 {"scheduled", markdown::tokens::Scheduled},
                 {"due", markdown::tokens::Due}};
    for (const auto &d : dated) {
        const QString arg = QString::fromLatin1(d.arg);
        if (!call.has(arg))
            continue;
        const auto spec = call.dateSpec(call.str(arg), arg);
        if (!spec)
            return call.failed();
        warnIfPast(call, *spec, arg);
        line += u' ' + d.token.toString() + u' ' + spec->toString();
    }
    if (call.has(u"remind"_s) && call.has(u"alarm"_s))
        return call.fail(ExitUsage, u"bad_argument"_s, tr("Give --remind or --alarm, not both"));
    for (const QString &arg : {u"remind"_s, u"alarm"_s}) {
        if (!call.has(arg))
            continue;
        const auto spec = call.dateSpec(call.str(arg), arg);
        if (!spec)
            return call.failed();
        warnIfPast(call, *spec, arg);
        line += u' ' + markdown::formatReminderToken(*spec, arg == u"alarm");
    }
    const auto rule = recurrence(call);
    if (!rule)
        return call.failed();
    if (!rule->isEmpty())
        line += u' ' + markdown::tokens::Recurrence.toString() + u' ' + *rule;
    const auto path = placeFor(call);
    if (!path)
        return call.failed();
    return appendLine(call, *path, line);
}

/// The status a word names: a TaskStatus, or nullopt for "no status".
struct StatusWord {
    bool valid = false;
    std::optional<TaskStatus> status;
};
StatusWord statusWord(const QString &word)
{
    const QString key = text::foldKey(word.trimmed());
    if (key == u"none" || key == u"brak" || key == u"no status" || key == u"bez statusu")
        return {true, std::nullopt};
    if (const auto s = notebook::taskStatusFromName(word))
        return {true, s};
    return {};
}

/// The line an item ref points at in `content` now, or an error (not found, stale).
std::optional<int> refLine(Call &call, const QString &content, const notebook::ItemRef &item)
{
    const notebook::ResolvedLine line = notebook::resolve(content, item, call.vault()->config());
    switch (line.status) {
    case notebook::ResolvedLine::Ok: return line.line;
    case notebook::ResolvedLine::Moved:
        call.warn(u"moved"_s, tr("The line moved to %1 (lines were added or removed above it)").arg(line.line + 1),
                  {{u"from"_s, item.line}, {u"to"_s, line.line}});
        return line.line;
    case notebook::ResolvedLine::Stale:
        call.setError(ExitConflict, u"stale_ref"_s, tr("The line changed since the ref was taken: read the page again"),
                      {{u"ref"_s, notebook::formatRef(item)},
                       {u"line"_s, line.line},
                       {u"current"_s, line.line >= 0 ? lineText(content, line.line) : QString()}});
        return std::nullopt;
    case notebook::ResolvedLine::NotFound: break;
    }
    call.setError(ExitNotFound, u"line_not_found"_s, tr("No such line or block: %1").arg(notebook::formatRef(item)),
                  {{u"ref"_s, notebook::formatRef(item)}});
    return std::nullopt;
}

/// A page ref with a line or block part: the page and the item.
std::optional<std::pair<VaultPath, notebook::ItemRef>> itemOf(Call &call, const QString &raw)
{
    const auto ref = call.page(raw, true);
    if (!ref)
        return std::nullopt;
    if (!ref->item) {
        call.setError(ExitUsage, u"item_ref_required"_s,
                      tr("Point at a line or block: Dom.md#L12@hash (from task list) or ^id"), {{u"ref"_s, raw}});
        return std::nullopt;
    }
    notebook::ItemRef item = *ref->item;
    if (item.page.isEmpty())
        item.page = ref->path;
    return std::pair{ref->path, item};
}

Result taskSet(Call &call)
{
    const StatusWord wanted = statusWord(call.str(u"status"_s));
    if (!wanted.valid)
        return call.fail(ExitUsage, u"bad_argument"_s,
                         tr("Status is todo, doing, done, cancelled, deferred or none (also: zrobione, w toku, "
                            "anulowane, odłożone, brak)"),
                         {{u"status"_s, call.str(u"status"_s)}});
    const auto target = itemOf(call, call.str(u"ref"_s));
    if (!target)
        return call.failed();
    const VaultPath path = target->first;
    const notebook::ItemRef item = target->second;
    int line = -1;
    int inserted = 0;
    bool wasTask = false;
    const auto written = call.edit(path, [&](const QString &content, bool, QString *next) {
        const auto at = refLine(call, content, item);
        if (!at)
            return false;
        line = *at;
        const ParsedNote note = markdown::parseNote(path, content, call.vault()->config());
        const int block = markdown::blockAtLine(note, line);
        wasTask = block >= 0 && note.blocks.at(block).task.has_value();
        markdown::StatusLogOptions log;
        log.history = call.flag(u"history"_s);
        log.now = call.ctx.now();
        *next = wanted.status ? markdown::setTaskStatusLogged(content, line, *wanted.status, log)
                              : markdown::removeTaskMarker(content, line);
        inserted = int(text::splitLines(*next).size() - text::splitLines(content).size());
        return true;
    });
    if (!written)
        return call.failed();
    if (!wasTask && wanted.status)
        call.warn(u"became_a_task"_s, tr("The line was no task: it is one now"));
    // Completing a recurring task inserts its next occurrence above it.
    const int completed = inserted > 0 ? line + inserted : line;
    QJsonObject o = call.writeJson(*written);
    o.insert(u"status"_s, wanted.status ? notebook::taskStatusName(*wanted.status) : QString());
    o.insert(u"line"_s, completed);
    o.insert(u"ref"_s, Call::lineRef(path, written->after, completed));
    if (inserted > 0)
        o.insert(u"next"_s, QJsonObject{{u"line"_s, line},
                                        {u"ref"_s, Call::lineRef(path, written->after, line)},
                                        {u"text"_s, lineText(written->after, line)}});
    return call.ok(o, lineText(written->after, completed));
}

/// The first line of the task block `ref` points at now, or an error.
std::optional<int> taskLineOf(Call &call, const QString &content, const notebook::ItemRef &item)
{
    const auto at = refLine(call, content, item);
    if (!at)
        return std::nullopt;
    const int line = markdown::taskBlockLine(content, *at);
    if (line < 0) {
        call.setError(ExitUsage, u"not_a_task"_s, tr("The line is no task: %1").arg(lineText(content, *at)),
                      {{u"ref"_s, notebook::formatRef(item)}});
        return std::nullopt;
    }
    return line;
}

/// Stop the running clocks (all, or all but the one at `keepPath`:`keepLine`): what was stopped
/// [{ref, text, seconds}]; nullopt after an error.
std::optional<QJsonArray> stopClocks(Call &call, const VaultPath &keepPath = {}, int keepLine = -1)
{
    VaultIndex *index = call.index();
    if (!index)
        return std::nullopt;
    QJsonArray stopped;
    const QDateTime now = call.ctx.now();
    for (const TaskRef &t : index->runningClocks()) {
        const ParsedNote *note = index->note(t.path);
        if (!note || t.blockIndex >= note->blocks.size())
            continue;
        const int line = note->blocks[t.blockIndex].line;
        if (t.path == keepPath && line == keepLine)
            continue;
        const QDateTime start = t.task.clockStart;
        const auto written = call.edit(t.path, [&](const QString &content, bool, QString *next) {
            *next = markdown::stopTaskClock(content, line, now);
            return true;
        });
        if (!written)
            return std::nullopt;
        stopped.append(QJsonObject{{u"ref"_s, Call::lineRef(t.path, written->after, line)},
                                   {u"text"_s, t.task.text},
                                   {u"seconds"_s, std::max<qint64>(0, start.secsTo(now))}});
    }
    return stopped;
}

Result taskStart(Call &call)
{
    const auto target = itemOf(call, call.str(u"ref"_s));
    if (!target)
        return call.failed();
    const VaultPath path = target->first;
    VaultIndex *index = call.index();
    if (!index)
        return call.failed();
    // The task's line as the index has it, to keep its clock when it already runs.
    int line = -1;
    const QString current = index->content(path);
    if (!current.isNull()) {
        const notebook::ResolvedLine r = notebook::resolve(current, target->second, call.vault()->config());
        if (r.line >= 0)
            line = markdown::taskBlockLine(current, r.line);
    }
    // One clock at a time: the others stop first.
    const auto stopped = stopClocks(call, path, line);
    if (!stopped)
        return call.failed();
    const QDateTime now = call.ctx.now();
    bool running = false;
    const auto written = call.edit(path, [&](const QString &content, bool, QString *next) {
        const auto at = taskLineOf(call, content, target->second);
        if (!at)
            return false;
        line = *at;
        running = markdown::taskLogbook(content, line).running() != nullptr;
        *next = markdown::startTaskClock(content, line, now);
        return true;
    });
    if (!written)
        return call.failed();
    if (running)
        call.warn(u"already_running"_s, tr("The clock of this task already runs"));
    QJsonObject o = call.writeJson(*written);
    o.insert(u"line"_s, line);
    o.insert(u"ref"_s, Call::lineRef(path, written->after, line));
    o.insert(u"started"_s, now.toString(u"yyyy-MM-ddTHH:mm:ss"_s));
    o.insert(u"stopped"_s, *stopped);
    return call.ok(o, tr("Clock started: %1").arg(lineText(written->after, line).trimmed()));
}

Result taskStop(Call &call)
{
    if (!call.has(u"ref"_s)) {
        const auto stopped = stopClocks(call);
        if (!stopped)
            return call.failed();
        QString text;
        for (const QJsonValue &v : *stopped) {
            const QJsonObject o = v.toObject();
            text += markdown::formatClockDuration(o.value(u"seconds"_s).toInteger()) + u"  "_s
                    + o.value(u"text"_s).toString() + u"  "_s + o.value(u"ref"_s).toString() + u'\n';
        }
        if (stopped->isEmpty())
            text = tr("No clock runs");
        return call.ok(QJsonObject{{u"stopped"_s, *stopped}}, text);
    }
    const auto target = itemOf(call, call.str(u"ref"_s));
    if (!target)
        return call.failed();
    const VaultPath path = target->first;
    const QDateTime now = call.ctx.now();
    int line = -1;
    qint64 seconds = -1;
    const auto written = call.edit(path, [&](const QString &content, bool, QString *next) {
        const auto at = taskLineOf(call, content, target->second);
        if (!at)
            return false;
        line = *at;
        if (const markdown::ClockEntry *running = markdown::taskLogbook(content, line).running())
            seconds = std::max<qint64>(0, running->start.secsTo(now));
        *next = markdown::stopTaskClock(content, line, now);
        return true;
    });
    if (!written)
        return call.failed();
    if (seconds < 0)
        call.warn(u"not_running"_s, tr("No clock runs on this task"));
    QJsonObject o = call.writeJson(*written);
    o.insert(u"line"_s, line);
    o.insert(u"ref"_s, Call::lineRef(path, written->after, line));
    o.insert(u"seconds"_s, std::max<qint64>(0, seconds));
    return call.ok(o, markdown::formatClockDuration(std::max<qint64>(0, seconds)) + u"  "_s
                              + lineText(written->after, line).trimmed());
}

Result taskTime(Call &call)
{
    VaultIndex *index = call.index();
    if (!index)
        return call.failed();
    const QString by = call.has(u"by"_s) ? text::foldKey(call.str(u"by"_s).trimmed()) : u"day"_s;
    TimeGroup group = TimeGroup::Day;
    if (by == u"day" || by == u"dzien")
        group = TimeGroup::Day;
    else if (by == u"page" || by == u"strona")
        group = TimeGroup::Page;
    else if (by == u"tag")
        group = TimeGroup::Tag;
    else if (by == u"task" || by == u"zadanie")
        group = TimeGroup::Task;
    else
        return call.fail(ExitUsage, u"bad_argument"_s, tr("--by is day, page, tag or task"), {{u"by"_s, by}});
    QDate from;
    QDate to;
    if (call.has(u"from"_s)) {
        const auto d = call.date(call.str(u"from"_s));
        if (!d)
            return call.failed();
        from = *d;
    }
    if (call.has(u"to"_s)) {
        const auto d = call.date(call.str(u"to"_s));
        if (!d)
            return call.failed();
        to = *d;
    }
    QList<TaskRef> tasks;
    if (call.has(u"query"_s)) {
        QString source = call.str(u"query"_s);
        static const QRegularExpression typed(u"(^|\\s)-?(type|typ):"_s, QRegularExpression::CaseInsensitiveOption);
        if (!typed.match(source).hasMatch())
            source.prepend(u"type:tasks "_s);
        source += u" is:tracked limit:"_s + QString::number(MaxQueryLimit);
        const std::variant<Query, QString> parsed = parseQuery(source);
        if (const QString *message = std::get_if<QString>(&parsed))
            return call.fail(ExitParse, u"query_error"_s, *message, {{u"query"_s, source}});
        const QueryResult r =
                runQuery(std::get<Query>(parsed), *index, call.ctx.today(), call.vault()->config().weekStartsOn);
        for (const QueryItem &i : r.items) {
            if (i.task)
                tasks.append(TaskRef{i.path, i.blockIndex, *i.task});
        }
    } else {
        tasks = index->tasks();
    }
    const TimeSummary summary = summarizeTime(*index, tasks, group, from, to, call.ctx.now());
    QJsonArray rows;
    QString text;
    for (const TimeSummaryRow &r : summary.rows) {
        QJsonObject o{{u"key"_s, r.key},
                      {u"label"_s, r.label},
                      {u"seconds"_s, r.seconds},
                      {u"duration"_s, markdown::formatClockDuration(r.seconds)},
                      {u"tasks"_s, r.tasks}};
        if (!r.path.isEmpty())
            o.insert(u"path"_s, r.path);
        if (r.line >= 0)
            o.insert(u"ref"_s, Call::lineRef(r.path, index->content(r.path), r.line));
        rows.append(o);
        text += markdown::formatClockDuration(r.seconds) + u"  "_s
                + (r.label.isEmpty() && group == TimeGroup::Tag ? tr("(no tag)") : r.label) + u'\n';
    }
    text += tr("Total: %1 (tasks: %2)").arg(markdown::formatClockDuration(summary.total)).arg(summary.tasks);
    QJsonObject o{{u"by"_s, by},
                  {u"rows"_s, rows},
                  {u"total"_s, summary.total},
                  {u"duration"_s, markdown::formatClockDuration(summary.total)},
                  {u"tasks"_s, summary.tasks}};
    if (from.isValid())
        o.insert(u"from"_s, from.toString(Qt::ISODate));
    if (to.isValid())
        o.insert(u"to"_s, to.toString(Qt::ISODate));
    return call.ok(o, text);
}

Result taskDepend(Call &call)
{
    const auto target = itemOf(call, call.str(u"ref"_s));
    if (!target)
        return call.failed();
    const auto on = itemOf(call, call.str(u"on"_s));
    if (!on)
        return call.failed();
    VaultIndex *index = call.index();
    if (!index)
        return call.failed();
    const bool remove = call.flag(u"remove"_s);
    // The id of the task depended on: its 🆔, else a new one written to it.
    QString id;
    const auto idWritten = call.edit(on->first, [&](const QString &content, bool, QString *next) {
        const auto line = taskLineOf(call, content, on->second);
        if (!line)
            return false;
        const ParsedNote note = markdown::parseNote(on->first, content, call.vault()->config());
        const int b = markdown::blockAtLine(note, *line);
        id = b >= 0 && note.blocks[b].task ? note.blocks[b].task->id : QString();
        if (!id.isEmpty() || remove) {
            *next = content;
            return true;
        }
        id = markdown::generateTaskId([&](const QString &c) { return index->hasTaskId(c) || content.contains(c); });
        *next = markdown::setTaskId(content, *line, id);
        return true;
    });
    if (!idWritten)
        return call.failed();
    if (id.isEmpty())
        return call.fail(ExitUsage, u"no_task_id"_s, tr("That task has no id (🆔): it has no dependents"));
    int line = -1;
    const auto written = call.edit(target->first, [&](const QString &content, bool, QString *next) {
        const auto at = taskLineOf(call, content, target->second);
        if (!at)
            return false;
        line = *at;
        *next = remove ? markdown::removeTaskDependency(content, line, id)
                       : markdown::addTaskDependency(content, line, id);
        return true;
    });
    if (!written)
        return call.failed();
    QJsonObject o = call.writeJson(*written);
    o.insert(u"id"_s, id);
    o.insert(u"line"_s, line);
    o.insert(u"ref"_s, Call::lineRef(target->first, written->after, line));
    if (idWritten->status != notebook::Notebook::WriteResult::Unchanged)
        o.insert(u"idWritten"_s, call.writeJson(*idWritten));
    return call.ok(o, lineText(written->after, line).trimmed());
}

Result taskStatusAlias(Call &call, const QString &status)
{
    call.args.insert(u"status"_s, status);
    return taskSet(call);
}

Result remindAdd(Call &call)
{
    const QString text = cleanItemText(call.text(u"text"_s));
    if (text.isEmpty())
        return call.fail(ExitUsage, u"text_required"_s, tr("The reminder needs a text"));
    const auto at = call.dateSpec(call.str(u"at"_s), u"at"_s);
    if (!at)
        return call.failed();
    warnIfPast(call, *at, u"at"_s);
    QString line = (call.flag(u"task"_s) ? u"- [ ] "_s : u"- "_s) + text + u' '
                   + markdown::formatReminderToken(*at, call.flag(u"alarm"_s));
    const auto rule = recurrence(call);
    if (!rule)
        return call.failed();
    if (!rule->isEmpty())
        line += u' ' + markdown::tokens::Recurrence.toString() + u' ' + *rule;
    const auto path = placeFor(call);
    if (!path)
        return call.failed();
    return appendLine(call, *path, line);
}

Result remindList(Call &call)
{
    VaultIndex *index = call.index();
    if (!index)
        return call.failed();
    QList<ScheduledNotification> list;
    QJsonObject o;
    int dropped = 0;
    if (call.has(u"from"_s) || call.has(u"to"_s)) {
        const auto from = call.date(call.str(u"from"_s));
        if (!from)
            return call.failed();
        const auto to = call.has(u"to"_s) ? call.date(call.str(u"to"_s)) : std::optional<QDate>(from->addDays(6));
        if (!to)
            return call.failed();
        if (*to < *from || from->daysTo(*to) > 400)
            return call.fail(ExitUsage, u"bad_range"_s, tr("--to must follow --from, at most 400 days later"));
        list = notificationsBetween(*index, QDateTime(*from, QTime(0, 0)), QDateTime(*to, QTime(23, 59, 59)),
                                    call.ctx.locale());
        o.insert(u"from"_s, from->toString(Qt::ISODate));
        o.insert(u"to"_s, to->toString(Qt::ISODate));
    } else {
        const int days = std::clamp(call.integer(u"upcoming"_s, 7), 0, 3660);
        list = upcomingNotifications(*index, call.ctx.now(), days, std::clamp(call.integer(u"limit"_s, 200), 1, 10000),
                                     &dropped, call.ctx.locale());
        o.insert(u"from"_s, call.ctx.now().toString(u"yyyy-MM-dd HH:mm"_s));
        o.insert(u"days"_s, days);
    }
    QJsonArray items;
    QString text;
    for (const ScheduledNotification &n : std::as_const(list)) {
        QJsonObject item = notebook::notificationJson(n);
        item.insert(u"ref"_s, Call::lineRef(n.path, noteText(call, n.path), n.line));
        items.append(item);
        text += item.value(u"at"_s).toString() + (n.alarm ? u" ⏰! "_s : u" ⏰ "_s) + n.title + u'\n';
    }
    o.insert(u"items"_s, items);
    o.insert(u"dropped"_s, dropped);
    return call.ok(o, text);
}

Result remindSnooze(Call &call)
{
    QDateTime newAt;
    if (call.has(u"minutes"_s) == call.has(u"to"_s))
        return call.fail(ExitUsage, u"bad_argument"_s, tr("Give --minutes or --to"));
    if (call.has(u"minutes"_s)) {
        const int minutes = call.integer(u"minutes"_s, 0);
        if (minutes <= 0)
            return call.fail(ExitUsage, u"bad_argument"_s, tr("--minutes must be positive"));
        newAt = call.ctx.now().addSecs(qint64(minutes) * 60);
    } else {
        const auto spec = call.dateSpec(call.str(u"to"_s), u"to"_s);
        if (!spec)
            return call.failed();
        DateSpec at = *spec;
        if (!at.time && call.vault())
            at.time = call.vault()->config().defaultReminderTime;
        newAt = at.toLocalDateTime();
        warnIfPast(call, at, u"to"_s);
    }
    newAt.setTime(QTime(newAt.time().hour(), newAt.time().minute()));
    const auto target = itemOf(call, call.str(u"ref"_s));
    if (!target)
        return call.failed();
    const VaultPath path = target->first;
    const notebook::ItemRef item = target->second;
    int line = -1;
    const auto written = call.edit(path, [&](const QString &content, bool, QString *next) {
        const auto at = refLine(call, content, item);
        if (!at)
            return false;
        line = *at;
        *next = snoozeReminder(content, line, newAt);
        if (*next == content) {
            call.setError(ExitConflict, u"no_reminder"_s, tr("There is no reminder on that line"),
                          {{u"line"_s, line}, {u"text"_s, lineText(content, line)}});
            return false;
        }
        return true;
    });
    if (!written)
        return call.failed();
    QJsonObject o = call.writeJson(*written);
    o.insert(u"at"_s, newAt.toString(u"yyyy-MM-dd HH:mm"_s));
    o.insert(u"line"_s, line);
    o.insert(u"ref"_s, Call::lineRef(path, written->after, line));
    return call.ok(o);
}

} // namespace

void addTaskCommands(QList<Command> &out)
{
    const Arg ref = positional(
            u"ref"_s, ArgType::Ref,
            QT_TRANSLATE_NOOP("brulion::cli",
                              "The task's line: the `ref` task list gives (Dom.md#L12@hash), or a block (^id)"));
    const auto placeArgs = [] {
        return QList<Arg>{
                option(u"page"_s, ArgType::Ref,
                       QT_TRANSLATE_NOOP("brulion::cli", "Into this page (default: today's daily note)")),
                option(u"date"_s, ArgType::Date, QT_TRANSLATE_NOOP("brulion::cli", "Into the daily note of this day"))};
    };
    Command c;
    c.id = u"task.list"_s;
    c.summary = cliText(QT_TRANSLATE_NOOP("brulion::cli",
                                          "List tasks (default: open ones), or run a task query (docs/SYNTAX.md §10)"));
    c.args = {option(u"status"_s, ArgType::String,
                     QT_TRANSLATE_NOOP(
                             "brulion::cli",
                             "open (default), todo, doing, done, cancelled, deferred, closed, all — or Polish words")),
              option(u"due"_s, ArgType::String,
                     QT_TRANSLATE_NOOP("brulion::cli",
                                       "A due date expression: today, +7d, <=+7d, overdue, this-week, none")),
              option(u"tag"_s, ArgType::String, QT_TRANSLATE_NOOP("brulion::cli", "With this tag (also its sub-tags)")),
              option(u"page"_s, ArgType::Ref, QT_TRANSLATE_NOOP("brulion::cli", "In this page")),
              option(u"limit"_s, ArgType::Int,
                     QT_TRANSLATE_NOOP("brulion::cli", "At most this many (the total is still counted)")),
              flag(u"blocked"_s,
                   QT_TRANSLATE_NOOP("brulion::cli", "Only blocked tasks: waiting for an open task (⛔ / 🆔)")),
              option(u"query"_s, ArgType::String,
                     QT_TRANSLATE_NOOP(
                             "brulion::cli",
                             "A whole query instead (\"status:open due:<=+7d sort:due\"; type:tasks unless given)"))};
    c.run = taskList;
    out.append(c);
    c = Command();
    c.id = u"task.add"_s;
    c.summary =
            cliText(QT_TRANSLATE_NOOP("brulion::cli", "Add a task with canonical tokens (📅 ⏳ 🛫 ⏰ 🔁, priority)"));
    c.help = cliText(QT_TRANSLATE_NOOP(
            "brulion::cli",
            "Dates are phrases (jutro 9:00, w piątek, 2026-10-15) written as YYYY-MM-DD [HH:mm]; a date in the past "
            "gives a warning. The task goes to today's daily note unless --page or --date."));
    c.args = QList<Arg>{stdinable(positional(u"text"_s, ArgType::Text, QT_TRANSLATE_NOOP("brulion::cli", "The task"),
                                             true, true)),
                        option(u"due"_s, ArgType::Date, QT_TRANSLATE_NOOP("brulion::cli", "Due date 📅")),
                        option(u"scheduled"_s, ArgType::Date, QT_TRANSLATE_NOOP("brulion::cli", "Scheduled ⏳")),
                        option(u"start"_s, ArgType::Date, QT_TRANSLATE_NOOP("brulion::cli", "Start 🛫")),
                        option(u"remind"_s, ArgType::Date,
                               QT_TRANSLATE_NOOP("brulion::cli", "Reminder ⏰ (a date and time)")),
                        option(u"alarm"_s, ArgType::Date,
                               QT_TRANSLATE_NOOP("brulion::cli", "Alarm ⏰! (loud, stays until dismissed)")),
                        option(u"every"_s, ArgType::String,
                               QT_TRANSLATE_NOOP("brulion::cli", "Recurrence 🔁 (co tydzień, every weekday, …)")),
                        option(u"priority"_s, ArgType::String,
                               QT_TRANSLATE_NOOP("brulion::cli", "highest, high, medium, low, lowest"))}
             + placeArgs();
    c.run = taskAdd;
    c.writes = true;
    out.append(c);
    c = Command();
    c.id = u"task.set"_s;
    c.summary = cliText(QT_TRANSLATE_NOOP(
            "brulion::cli", "Set a task's status (completing a recurring task adds its next occurrence)"));
    c.args = {ref,
              positional(u"status"_s, ArgType::String,
                         QT_TRANSLATE_NOOP("brulion::cli", "todo, doing, done, cancelled, deferred, none — or "
                                                           "zrobione, w toku, anulowane, odłożone, brak")),
              flag(u"history"_s,
                   QT_TRANSLATE_NOOP("brulion::cli", "Log the change in the task's :LOGBOOK: (doing starts its clock, "
                                                     "leaving doing stops it), as the app's status history setting"))};
    c.run = taskSet;
    c.writes = true;
    out.append(c);
    c = Command();
    c.id = u"task.start"_s;
    c.summary = cliText(QT_TRANSLATE_NOOP(
            "brulion::cli", "Start a task's clock (a CLOCK line in its :LOGBOOK:); a running one elsewhere stops"));
    c.args = {ref};
    c.run = taskStart;
    c.writes = true;
    out.append(c);
    c = Command();
    c.id = u"task.stop"_s;
    c.summary = cliText(QT_TRANSLATE_NOOP("brulion::cli", "Stop a task's clock (without a ref: every running one)"));
    c.args = {positional(
            u"ref"_s, ArgType::Ref,
            QT_TRANSLATE_NOOP("brulion::cli", "The task's line (Dom.md#L12@hash); default: the running clocks"),
            false)};
    c.run = taskStop;
    c.writes = true;
    out.append(c);
    c = Command();
    c.id = u"task.time"_s;
    c.summary = cliText(QT_TRANSLATE_NOOP("brulion::cli", "Time clocked on tasks per day, page, tag or task"));
    c.args = {option(u"by"_s, ArgType::String, QT_TRANSLATE_NOOP("brulion::cli", "day (default), page, tag, task")),
              option(u"from"_s, ArgType::Date, QT_TRANSLATE_NOOP("brulion::cli", "From this day")),
              option(u"to"_s, ArgType::Date, QT_TRANSLATE_NOOP("brulion::cli", "To this day")),
              option(u"query"_s, ArgType::String,
                     QT_TRANSLATE_NOOP("brulion::cli", "Only the tasks of this query (\"tag:praca\")"))};
    c.run = taskTime;
    out.append(c);
    c = Command();
    c.id = u"task.depend"_s;
    c.summary = cliText(QT_TRANSLATE_NOOP(
            "brulion::cli", "Make a task wait for another (⛔ id; the other gets a 🆔 when it has none)"));
    c.args = {ref, option(u"on"_s, ArgType::Ref, QT_TRANSLATE_NOOP("brulion::cli", "The task it waits for"), true),
              flag(u"remove"_s, QT_TRANSLATE_NOOP("brulion::cli", "Remove the dependency instead"))};
    c.run = taskDepend;
    c.writes = true;
    out.append(c);
    const struct {
        const char *word;
        const char *status;
        const char *summary;
    } aliases[] = {{"done", "done", QT_TRANSLATE_NOOP("brulion::cli", "Mark a task done (task set <ref> done)")},
                   {"doing", "doing", QT_TRANSLATE_NOOP("brulion::cli", "Mark a task in progress")},
                   {"todo", "todo", QT_TRANSLATE_NOOP("brulion::cli", "Mark a task to do (open again)")},
                   {"cancel", "cancelled", QT_TRANSLATE_NOOP("brulion::cli", "Cancel a task")},
                   {"defer", "deferred", QT_TRANSLATE_NOOP("brulion::cli", "Defer a task (waiting)")}};
    for (const auto &a : aliases) {
        c = Command();
        c.id = u"task."_s + QString::fromLatin1(a.word);
        c.summary = cliText(a.summary);
        c.args = {ref};
        const QString status = QString::fromLatin1(a.status);
        c.run = [status](Call &call) { return taskStatusAlias(call, status); };
        c.writes = true;
        c.mcp = false; // brulion_task_set covers them
        c.aliasOf = u"task.set"_s;
        out.append(c);
    }

    c = Command();
    c.id = u"remind.add"_s;
    c.summary = cliText(QT_TRANSLATE_NOOP("brulion::cli", "Add a reminder ⏰ (or an alarm ⏰!) as a new block"));
    c.help = cliText(QT_TRANSLATE_NOOP("brulion::cli", "Reminders fire where a Brulion app runs: the desktop app (in "
                                                       "the tray) or the phone once the app has seen the change."));
    c.args =
            QList<Arg>{stdinable(positional(u"text"_s, ArgType::Text,
                                            QT_TRANSLATE_NOOP("brulion::cli", "What to be reminded of"), true, true)),
                       option(u"at"_s, ArgType::Date,
                              QT_TRANSLATE_NOOP("brulion::cli", "When: jutro 9:00, w piątek o 18, 2026-10-15 09:00"),
                              true),
                       flag(u"alarm"_s, QT_TRANSLATE_NOOP("brulion::cli", "An alarm ⏰!: loud, stays until dismissed")),
                       option(u"every"_s, ArgType::String, QT_TRANSLATE_NOOP("brulion::cli", "Recurrence 🔁")),
                       flag(u"task"_s, QT_TRANSLATE_NOOP("brulion::cli", "Make it a task (- [ ] …)"))}
            + placeArgs();
    c.run = remindAdd;
    c.writes = true;
    out.append(c);
    c = Command();
    c.id = u"remind.list"_s;
    c.summary = cliText(QT_TRANSLATE_NOOP(
            "brulion::cli", "Reminders due soon (or in a range), recurring ones expanded — read only, no state kept"));
    c.args = {option(u"upcoming"_s, ArgType::Int,
                     QT_TRANSLATE_NOOP("brulion::cli", "The next this many days (default 7)")),
              option(u"from"_s, ArgType::Date, QT_TRANSLATE_NOOP("brulion::cli", "From this day")),
              option(u"to"_s, ArgType::Date, QT_TRANSLATE_NOOP("brulion::cli", "To this day")),
              option(u"limit"_s, ArgType::Int, QT_TRANSLATE_NOOP("brulion::cli", "At most this many (default 200)"))};
    c.run = remindList;
    out.append(c);
    c = Command();
    c.id = u"remind.snooze"_s;
    c.summary = cliText(
            QT_TRANSLATE_NOOP("brulion::cli", "Move a reminder (a recurring one gets a one-off child reminder)"));
    c.args = {ref,
              option(u"minutes"_s, ArgType::Int, QT_TRANSLATE_NOOP("brulion::cli", "By this many minutes from now")),
              option(u"to"_s, ArgType::Date, QT_TRANSLATE_NOOP("brulion::cli", "To this time"))};
    c.run = remindSnooze;
    c.writes = true;
    out.append(c);
}

} // namespace brulion::cli
