#include "notebook/json.h"

#include "dataview/dataview.h"

#include "index/vaultindex.h"
#include "markdown/journal.h"
#include "markdown/notemode.h"
#include "markdown/taskdeps.h"
#include "markdown/tokenedit.h"
#include "text/text.h"
#include "util/paths.h"

using namespace Qt::StringLiterals;

namespace brulion::notebook {

namespace {

/// Folded, with spaces and underscores as dashes: "W toku" -> "w-toku".
QString wordKey(QStringView name)
{
    QString key = text::foldKey(name.trimmed());
    key.replace(u' ', u'-').replace(u'_', u'-');
    return key;
}

void insertIf(QJsonObject &o, const QString &key, const QString &value)
{
    if (!value.isEmpty())
        o.insert(key, value);
}

} // namespace

QString taskStatusName(TaskStatus status)
{
    switch (status) {
    case TaskStatus::Todo: return u"todo"_s;
    case TaskStatus::Doing: return u"doing"_s;
    case TaskStatus::Done: return u"done"_s;
    case TaskStatus::Cancelled: return u"cancelled"_s;
    case TaskStatus::Deferred: return u"deferred"_s;
    }
    return u"todo"_s;
}

std::optional<TaskStatus> taskStatusFromName(QStringView name)
{
    using S = TaskStatus;
    static const QHash<QString, S> table = {
            {u"todo"_s, S::Todo},           {u"open"_s, S::Todo},          {u"do-zrobienia"_s, S::Todo},
            {u"dozrobienia"_s, S::Todo},    {u"otwarte"_s, S::Todo},       {u"doing"_s, S::Doing},
            {u"in-progress"_s, S::Doing},   {u"w-toku"_s, S::Doing},       {u"wtoku"_s, S::Doing},
            {u"done"_s, S::Done},           {u"zrobione"_s, S::Done},      {u"wykonane"_s, S::Done},
            {u"cancelled"_s, S::Cancelled}, {u"canceled"_s, S::Cancelled}, {u"cancel"_s, S::Cancelled},
            {u"anulowane"_s, S::Cancelled}, {u"anuluj"_s, S::Cancelled},   {u"deferred"_s, S::Deferred},
            {u"defer"_s, S::Deferred},      {u"waiting"_s, S::Deferred},   {u"odlozone"_s, S::Deferred},
            {u"czeka"_s, S::Deferred},
    };
    const auto it = table.constFind(wordKey(name));
    return it == table.cend() ? std::nullopt : std::optional<TaskStatus>(*it);
}

QString priorityName(Priority priority)
{
    return markdown::priorityName(priority); // the token menus' names
}

std::optional<Priority> priorityFromName(QStringView name)
{
    static const QHash<QString, Priority> table = {
            {u"highest"_s, Priority::Highest},  {u"najwyzszy"_s, Priority::Highest}, {u"high"_s, Priority::High},
            {u"wysoki"_s, Priority::High},      {u"medium"_s, Priority::Medium},     {u"sredni"_s, Priority::Medium},
            {u"low"_s, Priority::Low},          {u"niski"_s, Priority::Low},         {u"lowest"_s, Priority::Lowest},
            {u"najnizszy"_s, Priority::Lowest}, {u"none"_s, Priority::None},         {u"normal"_s, Priority::None},
            {u"brak"_s, Priority::None},        {u"normalny"_s, Priority::None},
    };
    const auto it = table.constFind(wordKey(name));
    return it == table.cend() ? std::nullopt : std::optional<Priority>(*it);
}

QString blockKindName(BlockKind kind)
{
    switch (kind) {
    case BlockKind::ListItem: return u"listItem"_s;
    case BlockKind::Paragraph: return u"paragraph"_s;
    case BlockKind::Heading: return u"heading"_s;
    case BlockKind::Code: return u"code"_s;
    case BlockKind::Table: return u"table"_s;
    case BlockKind::Quote: return u"quote"_s;
    case BlockKind::Callout: return u"callout"_s;
    case BlockKind::Rule: return u"rule"_s;
    case BlockKind::Html: return u"html"_s;
    case BlockKind::Frontmatter: return u"frontmatter"_s;
    }
    return {};
}

QString linkKindName(LinkKind kind)
{
    switch (kind) {
    case LinkKind::Wiki: return u"wiki"_s;
    case LinkKind::Embed: return u"embed"_s;
    case LinkKind::BlockRef: return u"blockRef"_s;
    case LinkKind::BlockEmbed: return u"blockEmbed"_s;
    case LinkKind::Markdown: return u"markdown"_s;
    case LinkKind::Url: return u"url"_s;
    }
    return {};
}

QString queryTypeName(QueryType type)
{
    switch (type) {
    case QueryType::Tasks: return u"tasks"_s;
    case QueryType::Blocks: return u"blocks"_s;
    case QueryType::Pages: return u"pages"_s;
    case QueryType::Reminders: return u"reminders"_s;
    }
    return {};
}

QString calendarKindName(CalendarItem::Kind kind)
{
    switch (kind) {
    case CalendarItem::Journal: return u"journal"_s;
    case CalendarItem::Due: return u"due"_s;
    case CalendarItem::Scheduled: return u"scheduled"_s;
    case CalendarItem::Start: return u"start"_s;
    case CalendarItem::Reminder: return u"reminder"_s;
    case CalendarItem::Done: return u"done"_s;
    }
    return {};
}

QJsonObject taskJson(const Task &task)
{
    QJsonObject o{{u"status"_s, taskStatusName(task.status)},
                  {u"marker"_s, task.marker},
                  {u"text"_s, task.text},
                  {u"priority"_s, priorityName(task.priority)}};
    if (task.due)
        o.insert(u"due"_s, task.due->toString());
    if (task.scheduled)
        o.insert(u"scheduled"_s, task.scheduled->toString());
    if (task.start)
        o.insert(u"start"_s, task.start->toString());
    if (task.created)
        o.insert(u"created"_s, task.created->toString(Qt::ISODate));
    if (task.doneDate)
        o.insert(u"done"_s, task.doneDate->toString(Qt::ISODate));
    if (task.cancelledDate)
        o.insert(u"cancelled"_s, task.cancelledDate->toString(Qt::ISODate));
    insertIf(o, u"recurrence"_s, task.recurrence);
    insertIf(o, u"id"_s, task.id);
    if (!task.dependsOn.isEmpty())
        o.insert(u"dependsOn"_s, QJsonArray::fromStringList(task.dependsOn));
    if (task.clockedSeconds > 0)
        o.insert(u"clockedSeconds"_s, task.clockedSeconds);
    if (task.clockStart.isValid()) // local wall-clock time, no zone
        o.insert(u"clockStart"_s, task.clockStart.toString(u"yyyy-MM-ddTHH:mm:ss"_s));
    return o;
}

namespace {

/// A task item's dependencies (docs/SYNTAX.md §5): blocked?, blocking?, cycle?, blockedBy [{id,
/// path, line, text, status}] (the open tasks it waits for), missingIds, subtasks {done, total}.
void insertTaskRelations(QJsonObject &o, const VaultIndex &index, const VaultPath &path, int blockIndex)
{
    const ParsedNote *note = index.note(path);
    if (!note || blockIndex < 0 || blockIndex >= note->blocks.size() || !note->blocks[blockIndex].task)
        return;
    const Task &task = *note->blocks[blockIndex].task;
    if (!task.dependsOn.isEmpty() || !task.id.isEmpty()) {
        const TaskDependencies deps = index.taskDependencies(path, blockIndex);
        if (deps.blocked)
            o.insert(u"blocked"_s, true);
        if (index.isTaskBlocking(path, blockIndex))
            o.insert(u"blocking"_s, true);
        if (deps.cycle)
            o.insert(u"cycle"_s, true);
        QJsonArray blockedBy;
        QJsonArray missing;
        for (const TaskDependency &d : deps.dependsOn) {
            if (d.tasks.isEmpty())
                missing.append(d.id);
            for (const TaskRef &t : d.tasks) {
                if (t.task.status == TaskStatus::Done || t.task.status == TaskStatus::Cancelled)
                    continue;
                const ParsedNote *other = index.note(t.path);
                const int line =
                        other && t.blockIndex < other->blocks.size() ? other->blocks[t.blockIndex].line : t.task.line;
                blockedBy.append(QJsonObject{{u"id"_s, d.id},
                                             {u"path"_s, t.path},
                                             {u"line"_s, line},
                                             {u"text"_s, t.task.text},
                                             {u"status"_s, taskStatusName(t.task.status)}});
            }
        }
        if (!blockedBy.isEmpty())
            o.insert(u"blockedBy"_s, blockedBy);
        if (!missing.isEmpty())
            o.insert(u"missingIds"_s, missing);
    }
    const markdown::SubtaskProgress progress = markdown::subtaskProgress(*note, blockIndex);
    if (progress.total > 0)
        o.insert(u"subtasks"_s, QJsonObject{{u"done"_s, progress.done}, {u"total"_s, progress.total}});
}

} // namespace

QJsonObject reminderJson(const Reminder &reminder)
{
    QJsonObject o{{u"at"_s, reminder.at.toString()}, {u"alarm"_s, reminder.alarm}};
    insertIf(o, u"recurrence"_s, reminder.recurrence);
    return o;
}

QJsonObject blockJson(const Block &block)
{
    QJsonObject o{{u"line"_s, block.line},   {u"endLine"_s, block.endLine},
                  {u"depth"_s, block.depth}, {u"kind"_s, blockKindName(block.kind)},
                  {u"text"_s, block.text},   {u"raw"_s, block.raw.toString()}};
    insertIf(o, u"blockId"_s, block.id);
    if (block.headingLevel > 0)
        o.insert(u"headingLevel"_s, block.headingLevel);
    if (block.task)
        o.insert(u"task"_s, taskJson(*block.task));
    if (!block.reminders.isEmpty()) {
        QJsonArray reminders;
        for (const Reminder &r : block.reminders)
            reminders.append(reminderJson(r));
        o.insert(u"reminders"_s, reminders);
    }
    if (!block.tags.isEmpty())
        o.insert(u"tags"_s, QJsonArray::fromStringList(block.tags));
    return o;
}

QJsonObject queryItemJson(const VaultIndex &index, const QueryItem &item)
{
    const ParsedNote *note = index.note(item.path);
    QJsonObject o{{u"path"_s, item.path},
                  {u"title"_s, note ? note->title : paths::pageTitle(item.path)},
                  {u"folder"_s, paths::dirname(item.path)},
                  {u"line"_s, 0},
                  {u"depth"_s, 0}};
    if (note && item.blockIndex >= 0 && item.blockIndex < note->blocks.size()) {
        const Block &block = note->blocks.at(item.blockIndex);
        o.insert(u"line"_s, block.line);
        o.insert(u"text"_s, block.text);
        o.insert(u"raw"_s, block.raw.toString());
        o.insert(u"depth"_s, block.depth);
        insertIf(o, u"blockId"_s, block.id);
    } else {
        o.insert(u"text"_s, note ? note->title : paths::pageTitle(item.path));
        o.insert(u"raw"_s, QString());
    }
    if (item.task) {
        o.insert(u"task"_s, taskJson(*item.task));
        if (item.blockIndex < 0)
            o.insert(u"line"_s, item.task->line);
        else
            insertTaskRelations(o, index, item.path, item.blockIndex);
    }
    if (item.reminder) {
        o.insert(u"reminder"_s, reminderJson(*item.reminder));
        o.insert(u"line"_s, item.reminder->line);
    }
    return o;
}

QJsonObject queryResultJson(const VaultIndex &index, const QueryResult &result)
{
    QJsonArray items;
    for (const QueryItem &item : result.items)
        items.append(queryItemJson(index, item));
    QJsonArray groups;
    for (const QueryGroup &group : result.groups) {
        QJsonArray groupItems;
        for (const QueryItem &item : group.items)
            groupItems.append(queryItemJson(index, item));
        groups.append(QJsonObject{{u"key"_s, group.key}, {u"label"_s, group.label}, {u"items"_s, groupItems}});
    }
    return {{u"type"_s, queryTypeName(result.type)},
            {u"total"_s, result.total},
            {u"items"_s, items},
            {u"groups"_s, groups}};
}

QString dataviewTypeName(dataview::QueryType type)
{
    switch (type) {
    case dataview::QueryType::List: return u"list"_s;
    case dataview::QueryType::Table: return u"table"_s;
    case dataview::QueryType::Task: return u"tasks"_s;
    }
    return u"list"_s;
}

namespace {

QJsonObject dataviewRowJson(const VaultIndex &index, const dataview::Row &row)
{
    if (row.blockIndex >= 0)
        return queryItemJson(index, QueryItem{row.path, row.blockIndex, row.task, std::nullopt});
    const ParsedNote *note = row.path.isEmpty() ? nullptr : index.note(row.path);
    QJsonObject o{{u"path"_s, row.path},
                  {u"title"_s, note ? note->title : row.path.isEmpty() ? QString() : paths::pageTitle(row.path)},
                  {u"folder"_s, paths::dirname(row.path)},
                  {u"line"_s, 0},
                  {u"cells"_s, QJsonArray::fromStringList(row.cells)}};
    return o;
}

} // namespace

QJsonObject dataviewResultJson(const VaultIndex &index, const dataview::Result &result)
{
    QJsonArray items;
    for (const dataview::Row &row : result.rows)
        items.append(dataviewRowJson(index, row));
    QJsonArray groups;
    for (const dataview::Group &group : result.groups) {
        QJsonArray groupItems;
        for (const dataview::Row &row : group.rows) {
            groupItems.append(dataviewRowJson(index, row));
            items.append(groupItems.last()); // every item, as runQuery gives them with its groups
        }
        groups.append(QJsonObject{{u"key"_s, group.key}, {u"label"_s, group.label}, {u"items"_s, groupItems}});
    }
    return {{u"type"_s, dataviewTypeName(result.type)},
            {u"total"_s, result.total},
            {u"columns"_s, QJsonArray::fromStringList(result.headers)},
            {u"items"_s, items},
            {u"groups"_s, groups}};
}

QJsonObject searchHitJson(const SearchHit &hit, const VaultConfig &config)
{
    const auto journal = markdown::journalDateFromPath(hit.path, config);
    return {{u"path"_s, hit.path},
            {u"title"_s, hit.title},
            {u"folder"_s, paths::dirname(hit.path)},
            {u"line"_s, hit.line},
            {u"excerpt"_s, hit.excerpt},
            {u"score"_s, hit.score},
            {u"journal"_s, journal ? journal->toString(Qt::ISODate) : QString()}};
}

QJsonObject calendarItemJson(const CalendarItem &item)
{
    return {{u"date"_s, item.date.toString(Qt::ISODate)},
            {u"time"_s, item.time ? item.time->toString(u"HH:mm"_s) : QString()},
            {u"kind"_s, calendarKindName(item.kind)},
            {u"title"_s, item.title},
            {u"path"_s, item.path},
            {u"line"_s, item.line},
            {u"status"_s, item.status ? taskStatusName(*item.status) : QString()},
            {u"recurring"_s, item.recurring},
            {u"alarm"_s, item.alarm}};
}

QJsonObject notificationJson(const ScheduledNotification &n)
{
    const QDateTime local = n.at.toLocalTime();
    return {{u"id"_s, n.id},       {u"at"_s, local.toString(u"yyyy-MM-dd HH:mm"_s)},
            {u"title"_s, n.title}, {u"body"_s, n.body},
            {u"alarm"_s, n.alarm}, {u"path"_s, n.path},
            {u"line"_s, n.line}};
}

QJsonObject vaultConfigJson(const VaultConfig &c)
{
    return {{u"journalsFolder"_s, c.journalsFolder},
            {u"journalFileFormat"_s, c.journalFileFormat},
            {u"newPagesFolder"_s, c.newPagesFolder},
            {u"attachmentsFolder"_s, c.attachmentsFolder},
            {u"templatesFolder"_s, c.templatesFolder},
            {u"journalTemplate"_s, c.journalTemplate},
            {u"defaultReminderTime"_s, c.defaultReminderTime.toString(u"HH:mm"_s)},
            {u"weekStartsOn"_s, int(c.weekStartsOn)},
            {u"journalMode"_s, markdown::noteModeName(c.journalMode)},
            {u"pageMode"_s, markdown::noteModeName(c.pageMode)},
            {u"weeklyFolder"_s, c.weeklyFolder},
            {u"weeklyFileFormat"_s, c.weeklyFileFormat},
            {u"weeklyTemplate"_s, c.weeklyTemplate},
            {u"monthlyFolder"_s, c.monthlyFolder},
            {u"monthlyFileFormat"_s, c.monthlyFileFormat},
            {u"monthlyTemplate"_s, c.monthlyTemplate}};
}

} // namespace brulion::notebook
