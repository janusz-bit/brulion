// Workspace: index queries for the views and the editor's QueryHost (see workspace.h).
#include "workspace.h"

#include "dates/dates.h"
#include "index/index.h"
#include "markdown/markdown.h"
#include "reminders/calendar.h"
#include "util/paths.h"
#include "vault/vault.h"

#include <QCoreApplication>

#include <algorithm>

using namespace Qt::StringLiterals;

namespace brulion::shell {

namespace {

QString statusName(TaskStatus status)
{
    switch (status) {
    case TaskStatus::Todo: return u"todo"_s;
    case TaskStatus::Doing: return u"doing"_s;
    case TaskStatus::Done: return u"done"_s;
    case TaskStatus::Cancelled: return u"cancelled"_s;
    case TaskStatus::Deferred: return u"deferred"_s;
    }
    return {};
}

std::optional<TaskStatus> statusFromName(const QString &name)
{
    const QString n = name.trimmed().toLower();
    if (n == u"todo" || n == u"open")
        return TaskStatus::Todo;
    if (n == u"doing")
        return TaskStatus::Doing;
    if (n == u"done")
        return TaskStatus::Done;
    if (n == u"cancelled" || n == u"canceled")
        return TaskStatus::Cancelled;
    if (n == u"deferred")
        return TaskStatus::Deferred;
    return std::nullopt;
}

QString priorityName(Priority priority)
{
    switch (priority) {
    case Priority::None: return {};
    case Priority::Lowest: return u"lowest"_s;
    case Priority::Low: return u"low"_s;
    case Priority::Medium: return u"medium"_s;
    case Priority::High: return u"high"_s;
    case Priority::Highest: return u"highest"_s;
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

QVariantMap taskMap(const Task &task)
{
    QVariantMap m{
        { u"status"_s, statusName(task.status) },
        { u"marker"_s, task.marker },
        { u"text"_s, task.text },
        { u"priority"_s, priorityName(task.priority) },
    };
    if (task.due)
        m.insert(u"due"_s, task.due->toString());
    if (task.scheduled)
        m.insert(u"scheduled"_s, task.scheduled->toString());
    if (task.start)
        m.insert(u"start"_s, task.start->toString());
    if (task.doneDate)
        m.insert(u"done"_s, task.doneDate->toString(Qt::ISODate));
    if (!task.recurrence.isEmpty())
        m.insert(u"recurrence"_s, task.recurrence);
    return m;
}

/// One line of plain text (lists show the start of long blocks).
QString shortText(const QString &text)
{
    QString out = text.simplified();
    if (out.size() > 300)
        out = out.left(299) + u'…';
    return out;
}

} // namespace

// ---- Journals -----------------------------------------------------------------------------------

int Workspace::journalCount() const
{
    return m_index ? int(m_index->journals().size()) : 0;
}

QVariantList Workspace::journals(int offset, int limit) const
{
    QVariantList out;
    if (!m_index || limit <= 0)
        return out;
    const QList<JournalRef> all = m_index->journals();
    for (qsizetype i = std::max(0, offset); i < all.size() && out.size() < limit; ++i) {
        const JournalRef &j = all.at(i);
        out.append(QVariantMap{ { u"date"_s, j.date.toString(Qt::ISODate) },
                                { u"path"_s, j.path },
                                { u"title"_s, paths::pageTitle(j.path) } });
    }
    return out;
}

QStringList Workspace::journalDates(const QString &from, const QString &to) const
{
    QStringList out;
    const QDate a = QDate::fromString(from, Qt::ISODate);
    const QDate b = QDate::fromString(to, Qt::ISODate);
    if (!m_index || !a.isValid() || !b.isValid() || a > b)
        return out;
    for (const JournalRef &j : m_index->journals()) {
        if (j.date >= a && j.date <= b)
            out.append(j.date.toString(Qt::ISODate));
    }
    out.removeDuplicates();
    std::sort(out.begin(), out.end());
    return out;
}

// ---- Search, links, tags, calendar ----------------------------------------------------------------

QVariantList Workspace::search(const QString &query, int limit) const
{
    QVariantList out;
    if (!m_index || query.trimmed().isEmpty() || limit <= 0)
        return out;
    for (const SearchHit &hit : m_index->search(query, limit)) {
        out.append(QVariantMap{ { u"path"_s, hit.path },
                                { u"title"_s, hit.title },
                                { u"folder"_s, paths::dirname(hit.path) },
                                { u"line"_s, hit.line },
                                { u"excerpt"_s, hit.excerpt },
                                { u"score"_s, hit.score },
                                { u"journal"_s, journalDate(hit.path) } });
    }
    return out;
}

QVariantList Workspace::backlinks(const QString &path) const
{
    QVariantList out;
    if (!m_index)
        return out;
    for (const Backlink &b : m_index->backlinks(paths::normalize(path))) {
        const ParsedNote *note = m_index->note(b.path);
        QVariantMap m{ { u"path"_s, b.path },
                       { u"title"_s, note ? note->title : paths::pageTitle(b.path) },
                       { u"line"_s, b.link.line },
                       { u"journal"_s, journalDate(b.path) } };
        if (note && b.blockIndex >= 0 && b.blockIndex < note->blocks.size()) {
            const Block &block = note->blocks.at(b.blockIndex);
            m.insert(u"text"_s, shortText(block.text));
            if (!block.id.isEmpty())
                m.insert(u"blockId"_s, block.id);
        } else {
            m.insert(u"text"_s, QString());
        }
        out.append(m);
    }
    return out;
}

QVariantList Workspace::unlinkedReferences(const QString &path) const
{
    QVariantList out;
    if (!m_index)
        return out;
    for (const UnlinkedRef &r : m_index->unlinkedReferences(paths::normalize(path))) {
        out.append(QVariantMap{ { u"path"_s, r.path },
                                { u"title"_s, paths::pageTitle(r.path) },
                                { u"line"_s, r.line },
                                { u"excerpt"_s, r.excerpt } });
    }
    return out;
}

QVariantList Workspace::tagList() const
{
    QVariantList out;
    if (!m_index)
        return out;
    for (const TagCount &t : m_index->tags()) {
        out.append(QVariantMap{ { u"tag"_s, t.tag },
                                { u"display"_s, t.display.isEmpty() ? t.tag : t.display },
                                { u"count"_s, t.count } });
    }
    return out;
}

QVariantList Workspace::calendarItems(const QString &from, const QString &to) const
{
    QVariantList out;
    const QDate a = QDate::fromString(from, Qt::ISODate);
    const QDate b = QDate::fromString(to, Qt::ISODate);
    if (!m_index || !a.isValid() || !b.isValid() || a > b || a.daysTo(b) > 400)
        return out;
    for (const CalendarItem &item : brulion::calendarItems(*m_index, a, b)) {
        out.append(QVariantMap{ { u"date"_s, item.date.toString(Qt::ISODate) },
                                { u"time"_s, item.time ? item.time->toString(u"HH:mm"_s) : QString() },
                                { u"kind"_s, calendarKindName(item.kind) },
                                { u"title"_s, item.title },
                                { u"path"_s, item.path },
                                { u"line"_s, item.line },
                                { u"status"_s, item.status ? statusName(*item.status) : QString() },
                                { u"recurring"_s, item.recurring },
                                { u"alarm"_s, item.alarm } });
    }
    return out;
}

// ---- QueryHost ------------------------------------------------------------------------------------

QVariantMap Workspace::queryItem(const QueryItem &item) const
{
    const ParsedNote *note = m_index ? m_index->note(item.path) : nullptr;
    QVariantMap m{ { u"path"_s, item.path },
                   { u"title"_s, note ? note->title : paths::pageTitle(item.path) },
                   { u"folder"_s, paths::dirname(item.path) },
                   { u"line"_s, 0 },
                   { u"depth"_s, 0 } };
    if (note && item.blockIndex >= 0 && item.blockIndex < note->blocks.size()) {
        const Block &block = note->blocks.at(item.blockIndex);
        m.insert(u"line"_s, block.line);
        m.insert(u"text"_s, block.text);
        m.insert(u"raw"_s, block.raw);
        m.insert(u"depth"_s, block.depth);
        if (!block.id.isEmpty())
            m.insert(u"blockId"_s, block.id);
    } else {
        m.insert(u"text"_s, note ? note->title : paths::pageTitle(item.path));
        m.insert(u"raw"_s, QString());
    }
    if (item.task) {
        m.insert(u"task"_s, taskMap(*item.task));
        if (item.blockIndex < 0)
            m.insert(u"line"_s, item.task->line);
    }
    if (item.reminder) {
        QVariantMap r{ { u"at"_s, item.reminder->at.toString() }, { u"alarm"_s, item.reminder->alarm } };
        if (!item.reminder->recurrence.isEmpty())
            r.insert(u"recurrence"_s, item.reminder->recurrence);
        m.insert(u"reminder"_s, r);
        m.insert(u"line"_s, item.reminder->line);
    }
    return m;
}

QVariantMap Workspace::runQuery(const QString &source, const QString &currentPath)
{
    QVariantMap out{ { u"ok"_s, false },        { u"error"_s, QString() },   { u"type"_s, u"blocks"_s },
                     { u"total"_s, 0 },         { u"items"_s, QVariantList() }, { u"groups"_s, QVariantList() },
                     { u"complete"_s, !indexing() } };
    if (!m_index || !m_vault) {
        out.insert(u"error"_s, tr("No notebook is open."));
        return out;
    }
    const std::variant<Query, QString> parsed = parseQuery(source);
    if (const QString *error = std::get_if<QString>(&parsed)) {
        out.insert(u"error"_s, *error);
        return out;
    }
    const Query &query = std::get<Query>(parsed);
    QueryOptions options;
    options.currentPath = paths::normalize(currentPath);
    const QDateTime current = now();
    options.now = current.time();
    const QueryResult result =
            brulion::runQuery(query, *m_index, current.date(), m_vault->config().weekStartsOn, options);
    QVariantList items;
    items.reserve(result.items.size());
    for (const QueryItem &item : result.items)
        items.append(queryItem(item));
    QVariantList groups;
    for (const QueryGroup &group : result.groups) {
        QVariantList groupItems;
        for (const QueryItem &item : group.items)
            groupItems.append(queryItem(item));
        groups.append(QVariantMap{ { u"key"_s, group.key }, { u"label"_s, group.label }, { u"items"_s, groupItems } });
    }
    out.insert(u"ok"_s, true);
    out.insert(u"type"_s, queryTypeName(result.type));
    out.insert(u"total"_s, result.total);
    out.insert(u"items"_s, items);
    out.insert(u"groups"_s, groups);
    return out;
}

bool Workspace::setTaskStatus(const QString &rawPath, int line, const QString &status)
{
    const std::optional<TaskStatus> wanted = statusFromName(status);
    const QString path = paths::normalize(rawPath);
    if (!m_vault || !wanted || line < 0 || !paths::isMarkdown(path))
        return false;
    bool ok = false;
    const QString content = m_vault->readText(path, &ok);
    if (!ok)
        return false;
    const QString edited = markdown::setTaskStatus(content, line, *wanted, now().date());
    if (edited == content)
        return false;
    return write(path, edited);
}

} // namespace brulion::shell
