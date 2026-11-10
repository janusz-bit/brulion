// Workspace: index queries for the views and the editor's QueryHost (see workspace.h).
#include "workspace.h"

#include "dataview/dataview.h"
#include "dates/dates.h"
#include "index/index.h"
#include "index/querycache.h"
#include "markdown/markdown.h"
#include "notebook/json.h"
#include "reminders/calendar.h"
#include "reminders/dayplan.h"
#include "taskhelpers.h"
#include "util/paths.h"
#include "vault/vault.h"

#include <QCoreApplication>

#include <algorithm>

using namespace Qt::StringLiterals;

namespace brulion::shell {

namespace {

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

/// The caches of the most recently used keys (the oldest dropped beyond `max`).
template<typename Cache>
class RecentCaches {
public:
    explicit RecentCaches(int max) : m_max(max) { }

    Cache &get(const QString &key)
    {
        for (qsizetype i = 0; i < m_entries.size(); ++i) {
            if (m_entries[i].first == key) {
                m_entries.move(i, 0);
                return *m_entries.first().second;
            }
        }
        m_entries.prepend({key, std::make_shared<Cache>()});
        while (m_entries.size() > m_max)
            m_entries.removeLast();
        return *m_entries.first().second;
    }
    void remove(const QString &key)
    {
        m_entries.removeIf([&key](const auto &entry) { return entry.first == key; });
    }
    QStringList keys() const
    {
        QStringList out;
        for (const auto &entry : m_entries)
            out.append(entry.first);
        return out;
    }

private:
    int m_max;
    QList<std::pair<QString, std::shared_ptr<Cache>>> m_entries; ///< most recent first
};

/// One line of plain text (lists show the start of long blocks).
QString shortText(const QString &text)
{
    QString out = text.simplified();
    if (out.size() > 300)
        out = out.left(299) + u'…';
    return out;
}

/// A ```dataview block's last result: valid for its index version and minute (`date(now)`).
struct DataviewEntry {
    int version = -1;
    QDateTime minute;
    QVariantMap result;
};

} // namespace

// ---- Caches of the lists asked for again and again ------------------------------------------------

struct Workspace::ListCaches {
    RecentCaches<CalendarCache> ranges{MaxCachedRanges}; ///< the side panel's month, a picked day, …
    RecentCaches<QueryCache> queries{MaxCachedQueries};  ///< the ```query blocks of the open pages
    RecentCaches<DataviewEntry> dataview{MaxCachedQueries}; ///< the ```dataview blocks of the open pages
};

Workspace::ListCaches &Workspace::listCaches() const
{
    if (!m_listCaches)
        m_listCaches = std::make_shared<ListCaches>();
    return *m_listCaches;
}

QStringList Workspace::cachedLists() const
{
    if (!m_listCaches)
        return {};
    return m_listCaches->ranges.keys() + m_listCaches->queries.keys();
}

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
    // (A const copy: the index's cached list, which a non-const loop would copy deeply.)
    for (const QList<JournalRef> all = m_index->journals(); const JournalRef &j : all) {
        if (j.date >= a && j.date <= b)
            out.append(j.date.toString(Qt::ISODate));
    }
    out.removeDuplicates();
    std::ranges::sort(out);
    return out;
}

QVariantMap Workspace::journalMarks(const QVariantList &cells) const
{
    QVariantMap marks;
    if (cells.isEmpty())
        return marks;
    const QString from = cells.first().toMap().value(u"date"_s).toString();
    const QString to = cells.last().toMap().value(u"date"_s).toString();
    for (const QString &date : journalDates(from, to))
        marks.insert(date, QStringList{u"journal"_s});
    return marks;
}

// ---- Search, links, tags, calendar ----------------------------------------------------------------

QVariantList Workspace::search(const QString &query, int limit) const
{
    QVariantList out;
    if (!m_index || query.trimmed().isEmpty() || limit <= 0)
        return out;
    // The command line's JSON of a hit (notebook::searchHitJson): one serializer for both.
    const VaultConfig config = m_vault ? m_vault->config() : VaultConfig();
    for (const SearchHit &hit : m_index->search(query, limit))
        out.append(notebook::searchHitJson(hit, config).toVariantMap());
    return out;
}

QVariantList Workspace::backlinks(const QString &path) const
{
    QVariantList out;
    if (!m_index)
        return out;
    for (const Backlink &b : m_index->backlinks(paths::normalize(path))) {
        const ParsedNote *note = m_index->note(b.path);
        QVariantMap m{{u"path"_s, b.path},
                      {u"title"_s, note ? note->title : paths::pageTitle(b.path)},
                      {u"line"_s, b.link.line},
                      {u"journal"_s, journalDate(b.path)},
                      {u"text"_s, QString()}};
        if (note && b.blockIndex >= 0 && b.blockIndex < note->blocks.size()) {
            const Block &block = note->blocks.at(b.blockIndex);
            m.insert(u"text"_s, shortText(block.text));
            if (!block.id.isEmpty())
                m.insert(u"blockId"_s, block.id);
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
    for (const QList<TagCount> all = m_index->tags(); const TagCount &t : all) {
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
    CalendarCache &cache = listCaches().ranges.get(a.toString(Qt::ISODate) + u'|' + b.toString(Qt::ISODate));
    for (const QList<CalendarItem> all = cache.items(*m_index, a, b); const CalendarItem &item : all)
        out.append(notebook::calendarItemJson(item).toVariantMap());
    return out;
}

QVariantList Workspace::dayPlan(const QString &date) const
{
    QVariantList out;
    const QDate day = QDate::fromString(date, Qt::ISODate);
    if (!m_index || !day.isValid())
        return out;
    CalendarCache &cache = listCaches().ranges.get(day.toString(Qt::ISODate) + u'|' + day.toString(Qt::ISODate));
    for (const DayPlanEntry &e : brulion::dayPlan(cache.items(*m_index, day, day), day)) {
        out.append(QVariantMap{{u"time"_s, e.time ? e.time->toString(u"HH:mm"_s) : QString()},
                               {u"title"_s, e.title},
                               {u"path"_s, e.path},
                               {u"line"_s, e.line},
                               {u"status"_s, e.status ? notebook::taskStatusName(*e.status) : QString()},
                               {u"due"_s, e.due},
                               {u"scheduled"_s, e.scheduled},
                               {u"reminder"_s, e.reminder},
                               {u"alarm"_s, e.alarm},
                               {u"recurring"_s, e.recurring}});
    }
    return out;
}

// ---- QueryHost ------------------------------------------------------------------------------------

QVariantMap Workspace::queryItem(const QueryItem &item) const
{
    // The command line's JSON of an item (notebook::queryItemJson): one serializer for both.
    return m_index ? notebook::queryItemJson(*m_index, item).toVariantMap() : QVariantMap();
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
    const QString key = source + u'\x1f' + options.currentPath;
    QueryCache &cache = listCaches().queries.get(key);
    const QueryResult result = cache.run(query, *m_index, current.date(), m_vault->config().weekStartsOn, options);
    // A query matching much of the vault: its matches would stay in memory for the session.
    if (cache.total() > MaxCachedQueryMatches)
        listCaches().queries.remove(key);
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
    out.insert(u"type"_s, notebook::queryTypeName(result.type));
    out.insert(u"total"_s, result.total);
    out.insert(u"items"_s, items);
    out.insert(u"groups"_s, groups);
    return out;
}

QVariantMap Workspace::runDataview(const QString &source, const QString &currentPath)
{
    QVariantMap out{{u"ok"_s, false},  {u"error"_s, QString()},      {u"type"_s, u"list"_s},
                    {u"total"_s, 0},   {u"items"_s, QVariantList()}, {u"groups"_s, QVariantList()},
                    {u"columns"_s, QStringList()}, {u"complete"_s, !indexing()}};
    if (!m_index || !m_vault) {
        out.insert(u"error"_s, tr("No notebook is open."));
        return out;
    }
    const VaultPath path = paths::normalize(currentPath);
    QDateTime minute = now();
    minute.setTime(QTime(minute.time().hour(), minute.time().minute()));
    // Every block re-runs after each change of the index: the same query on the same version and
    // minute gives the same result.
    DataviewEntry &cached = listCaches().dataview.get(source + u'\x1f' + path);
    if (cached.version == m_index->version() && cached.minute == minute && !cached.result.isEmpty()) {
        QVariantMap result = cached.result;
        result.insert(u"complete"_s, !indexing());
        return result;
    }
    const std::variant<dataview::Query, QString> parsed = dataview::parseQuery(source);
    if (const QString *error = std::get_if<QString>(&parsed)) {
        out.insert(u"error"_s, *error);
        return out;
    }
    dataview::Options options;
    options.currentPath = path;
    options.now = now();
    options.weekStart = m_vault->config().weekStartsOn;
    const Vault *vault = m_vault.get();
    options.stat = [vault](const VaultPath &p) { return vault->stat(p); };
    const auto result = dataview::runQuery(std::get<dataview::Query>(parsed), *m_index, options);
    if (const QString *error = std::get_if<QString>(&result)) {
        out.insert(u"error"_s, *error);
    } else {
        out.insert(notebook::dataviewResultJson(*m_index, std::get<dataview::Result>(result)).toVariantMap());
        out.insert(u"ok"_s, true);
    }
    cached.version = m_index->version();
    cached.minute = minute;
    cached.result = out;
    return out;
}

bool Workspace::setTaskStatus(const QString &rawPath, int line, const QString &status)
{
    // "" = no status: the task marker goes (status menus' "No status").
    const std::optional<TaskStatus> wanted = statusFromName(status);
    const QString path = paths::normalize(rawPath);
    if (!m_vault || (!wanted && !status.isEmpty()) || line < 0 || !paths::isMarkdown(path))
        return false;
    bool ok = false;
    const QString content = m_vault->readText(path, &ok);
    if (!ok)
        return false;
    markdown::StatusLogOptions log;
    log.history = m_taskStatusHistory;
    log.now = now();
    QString edited = wanted ? markdown::setTaskStatusLogged(content, line, *wanted, log)
                            : markdown::removeTaskMarker(content, line);
    if (edited == content)
        return false;
    // The status history starts the clock of a task that became doing: one clock at a time.
    if (log.history && wanted == TaskStatus::Doing) {
        stopTaskClocksElsewhere(*this, log.now, path);
        const int task = markdown::taskBlockLine(edited, line);
        edited = stopTaskClocksIn(edited, log.now, task);
    }
    return write(path, edited);
}

const VaultIndex *Workspace::taskIndex() const
{
    return m_index.get();
}

int Workspace::stopTaskClocks()
{
    return stopTaskClocksElsewhere(*this, now());
}

void Workspace::setTaskStatusHistory(bool on)
{
    if (m_taskStatusHistory == on)
        return;
    m_taskStatusHistory = on;
    Q_EMIT taskStatusHistoryChanged();
}

} // namespace brulion::shell
