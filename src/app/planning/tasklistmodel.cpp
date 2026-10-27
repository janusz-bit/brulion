#include "tasklistmodel.h"

#include "dates/local.h"
#include "dates/recurrence.h"
#include "index/query.h"
#include "index/vaultindex.h"
#include "planningutil.h"

#include <QCoreApplication>
#include <QSet>

#include <algorithm>
#include <limits>

namespace brulion::planning {

using namespace Qt::StringLiterals;

namespace {

/// Like the query engine: highest, high, medium, none, low, lowest.
int priorityRank(Priority p)
{
    switch (p) {
    case Priority::Highest: return 0;
    case Priority::High: return 1;
    case Priority::Medium: return 2;
    case Priority::None: return 3;
    case Priority::Low: return 4;
    case Priority::Lowest: return 5;
    }
    return 3;
}

int priorityLevel(Priority p)
{
    switch (p) {
    case Priority::None: return 0;
    case Priority::Lowest: return 1;
    case Priority::Low: return 2;
    case Priority::Medium: return 3;
    case Priority::High: return 4;
    case Priority::Highest: return 5;
    }
    return 0;
}

/// The date an open task is filed under: the earlier of due and scheduled.
std::optional<DateSpec> sectionDate(const Task &t)
{
    std::optional<DateSpec> d = t.due;
    if (t.scheduled && (!d || dates::compareDateSpecs(*t.scheduled, *d) < 0))
        d = t.scheduled;
    return d;
}

/// Sort date: section date, else start.
std::optional<DateSpec> sortDate(const Task &t)
{
    if (auto d = sectionDate(t))
        return d;
    return t.start;
}

int compareOptionalSpecs(const std::optional<DateSpec> &a, const std::optional<DateSpec> &b)
{
    if (a && b)
        return dates::compareDateSpecs(*a, *b);
    if (a)
        return -1; // dated before undated
    if (b)
        return 1;
    return 0;
}

std::optional<QDate> finishedDate(const Task &t)
{
    if (t.status == TaskStatus::Done && t.doneDate)
        return t.doneDate;
    if (t.status == TaskStatus::Cancelled && t.cancelledDate)
        return t.cancelledDate;
    return t.doneDate ? t.doneDate : t.cancelledDate;
}

QString folderOf(const VaultPath &path)
{
    const qsizetype slash = path.lastIndexOf(u'/');
    return slash < 0 ? QString() : path.left(slash);
}

/// Bare text words are safe in a query unless they look like syntax.
QString textWord(const QString &w)
{
    const bool syntax = w.contains(u':') || w.startsWith(u'-') || w.startsWith(u'#') || w.startsWith(u"[[")
                        || w.startsWith(u"//") || w.contains(u'"') || w.contains(u'„') || w.contains(u'“')
                        || w.contains(u'«');
    if (!syntax)
        return w;
    QString inner = w;
    inner.replace(u'"', u'\'');
    return u'"' + inner + u'"';
}

constexpr int kMaxIncrementalOps = 400;

/// The key of a task row, "path:line" — exactly what u"%1:%2"_s.arg(path).arg(line) builds,
/// without parsing the pattern twice for every row (only a path with '%' can differ).
QString taskKey(const QString &path, int line)
{
    if (path.contains(u'%'))
        return u"%1:%2"_s.arg(path).arg(line);
    return path + u':' + QString::number(line);
}

} // namespace

TaskListModel::TaskListModel(QObject *parent) : QAbstractListModel(parent)
{
    m_refreshTimer.setSingleShot(true);
    m_refreshTimer.setInterval(150);
    connect(&m_refreshTimer, &QTimer::timeout, this, &TaskListModel::rebuild);
}

TaskListModel::~TaskListModel() = default;

void TaskListModel::classBegin()
{
    m_completing = true;
}

void TaskListModel::componentComplete()
{
    m_completing = false;
    if (m_rebuildPending)
        rebuild();
}

QStringList TaskListModel::sectionKeys()
{
    return {u"overdue"_s, u"today"_s, u"tomorrow"_s, u"thisWeek"_s, u"later"_s, u"noDate"_s, u"done"_s};
}

QString TaskListModel::sectionLabel(const QString &key)
{
    if (key == u"overdue")
        return tr("Overdue");
    if (key == u"today")
        return tr("Today");
    if (key == u"tomorrow")
        return tr("Tomorrow");
    if (key == u"thisWeek")
        return tr("This week");
    if (key == u"later")
        return tr("Later");
    if (key == u"noDate")
        return tr("No date");
    if (key == u"done")
        return tr("Done");
    return key;
}

QString TaskListModel::sectionOf(const Task &task, QDate today, Qt::DayOfWeek weekStart, std::optional<QDate> reminder)
{
    if (isClosed(task.status))
        return u"done"_s;
    if (task.due && task.due->date < today)
        return u"overdue"_s;
    std::optional<QDate> date;
    if (const auto dated = sectionDate(task))
        date = dated->date;
    else if (reminder)
        date = reminder;
    else if (task.start && task.start->date > today)
        date = task.start->date;
    if (!date)
        return u"noDate"_s;
    if (*date <= today)
        return u"today"_s;
    if (*date == today.addDays(1))
        return u"tomorrow"_s;
    if (*date <= dates::endOfWeek(today, weekStart))
        return u"thisWeek"_s;
    return u"later"_s;
}

int TaskListModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : int(m_rows.size());
}

QVariant TaskListModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_rows.size())
        return {};
    const Row &row = m_rows.at(index.row());
    if (row.header)
        return sectionRowData(row.section, role);
    if (row.item < 0 || row.item >= m_items.size())
        return {};
    return taskRowData(m_items.at(row.item), role);
}

QVariant TaskListModel::sectionRowData(const QString &section, int role) const
{
    switch (role) {
    case RowTypeRole: return u"header"_s;
    case SectionRole: return section;
    case SectionLabelRole:
    case Qt::DisplayRole: return sectionLabel(section);
    case SectionCountRole: return m_sectionCounts.value(section);
    case CollapsedRole: return m_collapsed.contains(section);
    case KeyRole: return QString(u"header:"_s + section);
    case LineRole:
    case PriorityRole: return 0;
    case OverdueRole:
    case IsJournalRole:
    case ReminderAlarmRole: return false;
    case ReminderSpecRole: return QString();
    case TagsRole: return QStringList();
    case TextRole:
    case HtmlRole:
    case StatusRole:
    case PathRole:
    case PageTitleRole:
    case FolderRole:
    case DueRole:
    case DueLabelRole:
    case ScheduledRole:
    case ScheduledLabelRole:
    case StartRole:
    case StartLabelRole:
    case RecurrenceRole:
    case RecurrenceLabelRole:
    case PriorityNameRole:
    case ReminderLabelRole:
    case DoneDateRole:
    case CreatedRole: return QString();
    default: return {};
    }
}

QVariant TaskListModel::taskRowData(const Item &item, int role) const
{
    const Task &t = item.ref.task;
    const QDate today = currentDay();
    const dates::UiLocale loc = uiLocale(m_locale);
    switch (role) {
    case RowTypeRole: return u"task"_s;
    case SectionRole: return item.section;
    case SectionLabelRole: return sectionLabel(item.section);
    case SectionCountRole: return m_sectionCounts.value(item.section);
    case CollapsedRole: return false;
    case KeyRole: return taskKey(item.ref.path, t.line);
    case Qt::DisplayRole:
    case TextRole: return plainText(t.text);
    case HtmlRole:
        if (!item.rendered) {
            item.html = renderInline(m_index.data(), t.text, item.ref.path, today, loc, paletteFromMap(m_paletteMap));
            item.rendered = true;
        }
        return item.html;
    case StatusRole: return statusName(t.status);
    case PathRole: return item.ref.path;
    case LineRole: return t.line;
    case PageTitleRole: return item.pageTitle;
    case FolderRole: return item.folder;
    case IsJournalRole: return item.isJournal;
    case DueRole: return t.due ? t.due->toString() : QString();
    case DueLabelRole: return t.due ? humanDate(*t.due, loc, today) : QString();
    case OverdueRole: return !isClosed(t.status) && t.due && t.due->date < today;
    case ScheduledRole: return t.scheduled ? t.scheduled->toString() : QString();
    case ScheduledLabelRole: return t.scheduled ? humanDate(*t.scheduled, loc, today) : QString();
    case StartRole: return t.start ? t.start->toString() : QString();
    case StartLabelRole: return t.start ? humanDate(*t.start, loc, today) : QString();
    case RecurrenceRole: return t.recurrence;
    case RecurrenceLabelRole: {
        if (t.recurrence.isEmpty())
            return QString();
        if (const auto rule = dates::parseRecurrence(t.recurrence))
            return dates::formatRecurrence(*rule, loc);
        return t.recurrence;
    }
    case PriorityRole: return priorityLevel(t.priority);
    case PriorityNameRole: return priorityName(t.priority);
    case TagsRole: return item.tags;
    case ReminderLabelRole: return item.reminder ? humanDate(item.reminder->at, loc, today) : QString();
    case ReminderAlarmRole: return item.reminder && item.reminder->alarm;
    case ReminderSpecRole: return item.reminder ? item.reminder->at.toString() : QString();
    case DoneDateRole: {
        const auto finished = finishedDate(t);
        return finished ? isoDate(*finished) : QString();
    }
    case CreatedRole: return t.created ? isoDate(*t.created) : QString();
    default: return {};
    }
}

QHash<int, QByteArray> TaskListModel::roleNames() const
{
    return {
            {RowTypeRole, "rowType"},
            {SectionRole, "section"},
            {SectionLabelRole, "sectionLabel"},
            {SectionCountRole, "sectionCount"},
            {CollapsedRole, "collapsed"},
            {KeyRole, "key"},
            {TextRole, "text"},
            {HtmlRole, "html"},
            {StatusRole, "status"},
            {PathRole, "path"},
            {LineRole, "line"},
            {PageTitleRole, "pageTitle"},
            {FolderRole, "folder"},
            {IsJournalRole, "isJournal"},
            {DueRole, "due"},
            {DueLabelRole, "dueLabel"},
            {OverdueRole, "overdue"},
            {ScheduledRole, "scheduled"},
            {ScheduledLabelRole, "scheduledLabel"},
            {StartRole, "start"},
            {StartLabelRole, "startLabel"},
            {RecurrenceRole, "recurrence"},
            {RecurrenceLabelRole, "recurrenceLabel"},
            {PriorityRole, "priority"},
            {PriorityNameRole, "priorityName"},
            {TagsRole, "tags"},
            {ReminderLabelRole, "reminderLabel"},
            {ReminderAlarmRole, "reminderAlarm"},
            {ReminderSpecRole, "reminderSpec"},
            {DoneDateRole, "doneDate"},
            {CreatedRole, "created"},
    };
}

QVariantMap TaskListModel::get(int row) const
{
    QVariantMap out;
    if (row < 0 || row >= m_rows.size())
        return out;
    const QHash<int, QByteArray> names = roleNames();
    const QModelIndex idx = createIndex(row, 0);
    for (auto it = names.cbegin(); it != names.cend(); ++it)
        out.insert(QString::fromLatin1(it.value()), data(idx, it.key()));
    return out;
}

// ---- properties ----------------------------------------------------------------------------

VaultIndex *TaskListModel::vaultIndex() const
{
    return m_index.data();
}

void TaskListModel::setVaultIndex(VaultIndex *index)
{
    if (m_index == index)
        return;
    if (m_indexConnection)
        disconnect(m_indexConnection);
    m_index = index;
    if (index)
        m_indexConnection = connect(index, &VaultIndex::changed, this, &TaskListModel::scheduleRefresh);
    Q_EMIT vaultIndexChanged();
    requestRebuild();
}

QObject *TaskListModel::vaultIndexObject() const
{
    return m_index.data();
}

void TaskListModel::setVaultIndexObject(QObject *index)
{
    setVaultIndex(qobject_cast<VaultIndex *>(index));
}

void TaskListModel::setToday(const QString &today)
{
    if (m_todayText == today)
        return;
    m_todayText = today;
    m_today = parseIsoDate(today);
    Q_EMIT todayChanged();
    requestRebuild();
}

void TaskListModel::setWeekStart(int weekStart)
{
    const int w = weekStart == 0 ? 0 : int(dates::sanitizeWeekStart(weekStart));
    if (m_weekStart == w)
        return;
    m_weekStart = w;
    Q_EMIT weekStartChanged();
    requestRebuild();
}

void TaskListModel::setLocale(const QString &locale)
{
    if (m_locale == locale)
        return;
    m_locale = locale;
    Q_EMIT localeChanged();
    for (Item &item : m_items)
        item.rendered = false;
    if (!m_rows.isEmpty())
        Q_EMIT dataChanged(createIndex(0, 0), createIndex(int(m_rows.size()) - 1, 0));
}

void TaskListModel::setPalette(const QVariantMap &palette)
{
    if (m_paletteMap == palette)
        return;
    m_paletteMap = palette;
    Q_EMIT paletteChanged();
    for (Item &item : m_items)
        item.rendered = false;
    if (!m_rows.isEmpty())
        Q_EMIT dataChanged(createIndex(0, 0), createIndex(int(m_rows.size()) - 1, 0), {HtmlRole});
}

void TaskListModel::filtersEdited()
{
    Q_EMIT filtersChanged();
    requestRebuild();
}

void TaskListModel::setTagFilter(const QString &tag)
{
    QString t = tag.trimmed();
    if (t.startsWith(u'#'))
        t.remove(0, 1);
    if (t.startsWith(u"[[") && t.endsWith(u"]]"))
        t = t.mid(2, t.size() - 4).trimmed();
    t = t.toLower();
    if (m_tag == t)
        return;
    m_tag = t;
    filtersEdited();
}

void TaskListModel::setPageFilter(const QString &path)
{
    const QString p = path.trimmed();
    if (m_page == p)
        return;
    m_page = p;
    filtersEdited();
}

void TaskListModel::setFolderFilter(const QString &folder)
{
    QString f = folder.trimmed();
    while (f.endsWith(u'/'))
        f.chop(1);
    while (f.startsWith(u'/'))
        f.remove(0, 1);
    if (m_folder == f)
        return;
    m_folder = f;
    filtersEdited();
}

void TaskListModel::setPriorityFilter(const QString &priority)
{
    static const QStringList known{u"highest"_s, u"high"_s, u"medium"_s, u"low"_s, u"lowest"_s, u"none"_s};
    const QString p = known.contains(priority) ? priority : QString();
    if (m_priority == p)
        return;
    m_priority = p;
    filtersEdited();
}

void TaskListModel::setStatusFilter(const QString &status)
{
    static const QStringList known{u"all"_s, u"open"_s, u"todo"_s, u"doing"_s, u"deferred"_s, u"done"_s, u"cancelled"_s};
    const QString s = known.contains(status) ? status : u"all"_s;
    if (m_status == s)
        return;
    m_status = s;
    filtersEdited();
}

void TaskListModel::setTextFilter(const QString &text)
{
    const QString t = text.simplified();
    if (m_text == t)
        return;
    m_text = t;
    filtersEdited();
}

void TaskListModel::setSortKey(const QString &key)
{
    static const QStringList known{u"due"_s, u"priority"_s, u"page"_s, u"created"_s};
    const QString k = known.contains(key) ? key : u"due"_s;
    if (m_sort == k)
        return;
    m_sort = k;
    Q_EMIT sortKeyChanged();
    Q_EMIT filtersChanged(); // queryText has the sort
    requestRebuild();
}

bool TaskListModel::filtered() const
{
    return !m_tag.isEmpty() || !m_page.isEmpty() || !m_folder.isEmpty() || !m_priority.isEmpty()
           || m_status != u"all" || !m_text.isEmpty();
}

void TaskListModel::setCollapsedSections(const QStringList &keys)
{
    if (m_collapsed == keys)
        return;
    m_collapsed = keys;
    Q_EMIT collapsedSectionsChanged();
    rebuildRows();
    Q_EMIT countsChanged();
}

void TaskListModel::toggleSection(const QString &key)
{
    QStringList keys = m_collapsed;
    if (keys.contains(key))
        keys.removeAll(key);
    else
        keys.append(key);
    setCollapsedSections(keys);
}

void TaskListModel::clearFilters()
{
    if (!filtered())
        return;
    m_tag.clear();
    m_page.clear();
    m_folder.clear();
    m_priority.clear();
    m_status = u"all"_s;
    m_text.clear();
    filtersEdited();
}

void TaskListModel::setRefreshDelay(int msec)
{
    msec = std::max(0, msec);
    if (m_refreshTimer.interval() == msec)
        return;
    m_refreshTimer.setInterval(msec);
    Q_EMIT refreshDelayChanged();
}

void TaskListModel::setClock(std::function<QDate()> clock)
{
    m_clock = std::move(clock);
    rebuild();
}

QString TaskListModel::pageQueryValue(const QString &path) const
{
    // The title reads best; use it when it resolves back to the same page, else the path.
    QString title = path.section(u'/', -1);
    if (title.endsWith(u".md", Qt::CaseInsensitive))
        title.chop(3);
    if (m_index) {
        if (const auto resolved = m_index->resolveLink(title); resolved && *resolved == path)
            return queryValue(title);
    }
    QString p = path;
    if (p.endsWith(u".md", Qt::CaseInsensitive))
        p.chop(3);
    return queryValue(p);
}

QString TaskListModel::queryText() const
{
    QStringList parts{u"type:tasks"_s};
    if (m_status != u"all")
        parts << u"status:"_s + m_status;
    if (!m_tag.isEmpty())
        parts << u"tag:"_s + queryValue(m_tag);
    if (!m_page.isEmpty())
        parts << u"page:"_s + pageQueryValue(m_page);
    if (!m_folder.isEmpty())
        parts << u"path:"_s + queryValue(m_folder + u'/');
    if (!m_priority.isEmpty())
        parts << u"priority:"_s + m_priority;
    for (const QString &w : m_text.split(u' ', Qt::SkipEmptyParts))
        parts << textWord(w);
    if (m_sort == u"due")
        parts << u"sort:due"_s;
    else if (m_sort == u"priority")
        parts << u"sort:priority"_s;
    else if (m_sort == u"page")
        parts << u"sort:page"_s;
    else if (m_sort == u"created")
        parts << u"sort:-created"_s;
    return parts.join(u' ');
}

bool TaskListModel::applyQuery(const QString &query)
{
    const auto parsed = parseQuery(query);
    if (!std::holds_alternative<Query>(parsed))
        return false;
    const Query &q = std::get<Query>(parsed);
    QString tag, page, folder, priority, status = u"all"_s, text;
    QStringList words;
    for (const QueryFilter &f : q.filters) {
        if (f.negate)
            continue;
        switch (f.kind) {
        case QueryFilterKind::Tag:
            if (!f.values.isEmpty())
                tag = f.values.first();
            break;
        case QueryFilterKind::Page:
            if (!f.values.isEmpty()) {
                const QString v = f.values.first();
                const auto resolved = m_index ? m_index->resolveLink(v) : std::nullopt;
                page = resolved ? *resolved : v;
            }
            break;
        case QueryFilterKind::Path:
            if (!f.values.isEmpty())
                folder = f.values.first();
            break;
        case QueryFilterKind::Priority:
            if (!f.priorities.isEmpty())
                priority = f.priorities.first() == Priority::None ? u"none"_s : priorityName(f.priorities.first());
            break;
        case QueryFilterKind::Status: {
            QList<TaskStatus> s = f.statuses;
            std::sort(s.begin(), s.end());
            if (s == QList<TaskStatus>{TaskStatus::Todo, TaskStatus::Doing})
                status = u"open"_s;
            else if (s.size() == 1)
                status = statusName(s.first());
            break;
        }
        case QueryFilterKind::Text: words << f.values; break;
        case QueryFilterKind::Ref:
        case QueryFilterKind::Date: break;
        }
    }
    text = words.join(u' ');
    QString sort = m_sort;
    if (!q.sort.isEmpty()) {
        switch (q.sort.first().key) {
        case QuerySortKey::Priority: sort = u"priority"_s; break;
        case QuerySortKey::Page:
        case QuerySortKey::Path: sort = u"page"_s; break;
        case QuerySortKey::Created: sort = u"created"_s; break;
        default: sort = u"due"_s; break;
        }
    }
    // Apply everything, then announce and rebuild once.
    tag = tag.trimmed().toLower();
    if (tag.startsWith(u'#'))
        tag.remove(0, 1);
    while (folder.endsWith(u'/'))
        folder.chop(1);
    while (folder.startsWith(u'/'))
        folder.remove(0, 1);
    // The same filters again (TasksView applies its route when created and when complete).
    if (tag == m_tag && page.trimmed() == m_page && folder.trimmed() == m_folder && priority == m_priority
        && status == m_status && text.simplified() == m_text && sort == m_sort)
        return true;
    const bool sortChanged = sort != m_sort;
    m_tag = tag;
    m_page = page.trimmed();
    m_folder = folder.trimmed();
    m_priority = priority;
    m_status = status;
    m_text = text.simplified();
    m_sort = sort;
    Q_EMIT filtersChanged();
    if (sortChanged)
        Q_EMIT sortKeyChanged();
    requestRebuild();
    return true;
}

QVariantList TaskListModel::tagChoices(int limit) const
{
    QVariantList out;
    if (!m_index)
        return out;
    QHash<QString, int> counts;
    QHash<QString, QString> display;
    for (const TaskRef &ref : m_index->tasks()) {
        const ParsedNote *note = m_index->note(ref.path);
        if (!note || ref.blockIndex < 0 || ref.blockIndex >= note->blocks.size())
            continue;
        for (const QString &tag : note->blocks.at(ref.blockIndex).tags)
            ++counts[tag];
    }
    for (const TagCount &t : m_index->tags())
        display.insert(t.tag, t.display.isEmpty() ? t.tag : t.display);
    QList<std::pair<QString, int>> list;
    for (auto it = counts.cbegin(); it != counts.cend(); ++it)
        list.append({it.key(), it.value()});
    std::sort(list.begin(), list.end(), [](const auto &a, const auto &b) {
        return a.second != b.second ? a.second > b.second : compareNames(a.first, b.first) < 0;
    });
    for (const auto &[tag, count] : list) {
        if (limit > 0 && out.size() >= limit)
            break;
        out.append(QVariantMap{{u"value"_s, tag}, {u"label"_s, QString(u'#' + display.value(tag, tag))}, {u"count"_s, count}});
    }
    return out;
}

QVariantList TaskListModel::folderChoices(int limit) const
{
    QVariantList out;
    if (!m_index)
        return out;
    QHash<QString, int> counts;
    for (const TaskRef &ref : m_index->tasks()) {
        // Every ancestor folder counts ("Projekty/Dom/x.md" -> "Projekty", "Projekty/Dom").
        QString folder = folderOf(ref.path);
        while (!folder.isEmpty()) {
            ++counts[folder];
            folder = folderOf(folder);
        }
    }
    QList<std::pair<QString, int>> list;
    for (auto it = counts.cbegin(); it != counts.cend(); ++it)
        list.append({it.key(), it.value()});
    std::sort(list.begin(), list.end(),
              [](const auto &a, const auto &b) { return compareNames(a.first, b.first) < 0; });
    for (const auto &[folder, count] : list) {
        if (limit > 0 && out.size() >= limit)
            break;
        out.append(QVariantMap{{u"value"_s, folder}, {u"label"_s, folder}, {u"count"_s, count}});
    }
    return out;
}

QVariantList TaskListModel::pageChoices(int limit) const
{
    QVariantList out;
    if (!m_index)
        return out;
    QHash<QString, int> counts;
    for (const TaskRef &ref : m_index->tasks())
        ++counts[ref.path];
    QList<std::pair<QString, int>> list;
    for (auto it = counts.cbegin(); it != counts.cend(); ++it)
        list.append({it.key(), it.value()});
    std::sort(list.begin(), list.end(), [](const auto &a, const auto &b) {
        return a.second != b.second ? a.second > b.second : compareNames(a.first, b.first) < 0;
    });
    for (const auto &[path, count] : list) {
        if (limit > 0 && out.size() >= limit)
            break;
        const ParsedNote *note = m_index->note(path);
        out.append(QVariantMap{
                {u"value"_s, path}, {u"label"_s, note ? note->title : path}, {u"count"_s, count}, {u"folder"_s, folderOf(path)}});
    }
    return out;
}

int TaskListModel::rowOfKey(const QString &key) const
{
    return m_rowOfKey.value(key, -1);
}

void TaskListModel::refresh()
{
    m_refreshTimer.stop();
    rebuild();
}

// ---- internals -----------------------------------------------------------------------------

QDate TaskListModel::currentDay() const
{
    if (m_today.isValid())
        return m_today;
    return m_clock ? m_clock() : QDate::currentDate();
}

Qt::DayOfWeek TaskListModel::weekStartDay() const
{
    if (m_weekStart > 0)
        return Qt::DayOfWeek(m_weekStart);
    return m_index ? m_index->config().weekStartsOn : Qt::Monday;
}

void TaskListModel::requestRebuild()
{
    if (m_completing)
        m_rebuildPending = true;
    else
        rebuild();
}

void TaskListModel::scheduleRefresh()
{
    if (m_refreshTimer.interval() == 0)
        rebuild();
    else
        m_refreshTimer.start();
}

void TaskListModel::rebuild()
{
    m_refreshTimer.stop();
    m_rebuildPending = false;
    const QDate today = currentDay();
    const Qt::DayOfWeek ws = weekStartDay();
    QList<Item> items;
    int vaultTasks = 0;
    if (m_index) {
        vaultTasks = int(m_index->tasks().size());
        const auto parsed = parseQuery(queryText());
        if (std::holds_alternative<Query>(parsed)) {
            QueryOptions options;
            options.defaultLimit = std::numeric_limits<int>::max();
            const QueryResult result = runQuery(std::get<Query>(parsed), *m_index, today, ws, options);
            items.reserve(result.items.size());
            for (const QueryItem &qi : result.items) {
                if (!qi.task)
                    continue;
                Item item;
                item.ref = TaskRef{qi.path, qi.blockIndex, *qi.task};
                item.folder = folderOf(qi.path);
                if (const ParsedNote *note = m_index->note(qi.path)) {
                    item.pageTitle = note->title;
                    item.isJournal = note->journalDate.has_value();
                    if (qi.blockIndex >= 0 && qi.blockIndex < note->blocks.size()) {
                        const Block &block = note->blocks.at(qi.blockIndex);
                        item.tags = block.tags;
                        // The next reminder of the block (else its last one).
                        for (const Reminder &r : block.reminders) {
                            if (!item.reminder || (item.reminder->at.date < today && r.at.date >= today)
                                || (r.at.date >= today && dates::compareDateSpecs(r.at, item.reminder->at) < 0))
                                item.reminder = r;
                        }
                    }
                }
                item.section = sectionOf(*qi.task, today, ws,
                                         item.reminder ? std::optional<QDate>(item.reminder->at.date) : std::nullopt);
                items.append(std::move(item));
            }
        }
    }

    // Sort by keys computed once per task (the comparison runs n log n times): the section's
    // place, the dates, the priority rank and the page title's place in compareNames() order
    // (collation keys of the distinct titles, sorted once).
    const QStringList sections = sectionKeys();
    const qsizetype done = sections.indexOf(u"done"_s);
    enum class By { Due, Priority, Created, Page };
    const By by = m_sort == u"priority"  ? By::Priority
                  : m_sort == u"created" ? By::Created
                  : m_sort == u"page"    ? By::Page
                                         : By::Due;
    QHash<QString, int> titleRank;
    {
        QList<std::pair<QCollatorSortKey, QString>> titles;
        for (const Item &item : std::as_const(items)) {
            if (!titleRank.contains(item.pageTitle)) {
                titleRank.insert(item.pageTitle, 0);
                titles.append({nameSortKey(item.pageTitle), item.pageTitle});
            }
        }
        std::sort(titles.begin(), titles.end(), [](const auto &a, const auto &b) {
            const int c = a.first.compare(b.first);
            return c != 0 ? c < 0 : QString::compare(a.second, b.second) < 0;
        });
        for (int k = 0; k < titles.size(); ++k)
            titleRank[titles.at(k).second] = k;
    }
    struct SortKey {
        qsizetype section;
        std::optional<QDate> finished; ///< done section
        int priority;
        std::optional<QDate> created;
        std::optional<DateSpec> date; ///< sort date, else the reminder
        int title;
        int item;
    };
    QList<SortKey> order;
    order.reserve(items.size());
    for (int k = 0; k < items.size(); ++k) {
        const Item &item = items.at(k);
        const Task &t = item.ref.task;
        std::optional<DateSpec> date = sortDate(t);
        if (!date && item.reminder)
            date = item.reminder->at;
        order.append(SortKey{sections.indexOf(item.section), finishedDate(t), priorityRank(t.priority), t.created, date,
                             titleRank.value(item.pageTitle), k});
    }
    std::stable_sort(order.begin(), order.end(), [&](const SortKey &a, const SortKey &b) {
        if (a.section != b.section)
            return a.section < b.section;
        const Item &ia = items.at(a.item);
        const Item &ib = items.at(b.item);
        if (a.section == done) {
            if (a.finished != b.finished) {
                if (a.finished && b.finished)
                    return *a.finished > *b.finished;
                return a.finished.has_value();
            }
        } else if (by == By::Priority) {
            if (a.priority != b.priority)
                return a.priority < b.priority;
        } else if (by == By::Created) {
            if (a.created != b.created) {
                if (a.created && b.created)
                    return *a.created > *b.created;
                return a.created.has_value();
            }
        } else if (by == By::Page) {
            if (a.title != b.title)
                return a.title < b.title;
            if (ia.ref.path != ib.ref.path)
                return ia.ref.path < ib.ref.path;
            return ia.ref.task.line < ib.ref.task.line;
        }
        if (const int c = compareOptionalSpecs(a.date, b.date); c != 0)
            return c < 0;
        if (a.priority != b.priority)
            return a.priority < b.priority;
        if (a.title != b.title)
            return a.title < b.title;
        if (ia.ref.path != ib.ref.path)
            return ia.ref.path < ib.ref.path;
        return ia.ref.task.line < ib.ref.task.line;
    });
    QList<Item> sorted;
    sorted.reserve(items.size());
    for (const SortKey &k : std::as_const(order))
        sorted.append(std::move(items[k.item]));
    items = std::move(sorted);

    m_items = std::move(items);
    m_sectionCounts.clear();
    m_total = int(m_items.size());
    m_open = 0;
    for (const Item &item : std::as_const(m_items)) {
        ++m_sectionCounts[item.section];
        if (!isClosed(item.ref.task.status))
            ++m_open;
    }
    m_overdue = m_sectionCounts.value(u"overdue"_s);
    m_todayCount = m_sectionCounts.value(u"today"_s);
    m_vaultTasks = vaultTasks;
    rebuildRows();
    Q_EMIT countsChanged();
}

void TaskListModel::rebuildRows()
{
    QList<Row> rows;
    QStringList newKeys;
    {
        QString current;
        for (int i = 0; i < m_items.size(); ++i) {
            const Item &item = m_items.at(i);
            if (item.section != current) {
                current = item.section;
                rows.append(Row{true, current, -1});
                newKeys.append(u"header:"_s + current);
            }
            if (m_collapsed.contains(item.section))
                continue;
            rows.append(Row{false, item.section, i});
            newKeys.append(taskKey(item.ref.path, item.ref.task.line));
        }
    }

    // Keep the view steady (scroll position, delegates) with row moves/inserts/removes instead of a
    // reset: ticking off a task moves one row. Large reshuffles reset.
    const QStringList &oldKeys = m_rowKeys;
    QSet<QString> newSet; // only needed to update rows that are shown already
    bool incremental = false;
    if (!m_rows.isEmpty()) {
        newSet = QSet<QString>(newKeys.cbegin(), newKeys.cend());
        int removals = 0;
        for (const QString &k : oldKeys)
            removals += newSet.contains(k) ? 0 : 1;
        incremental = oldKeys.size() == m_rows.size()
                      && removals + std::abs(int(newKeys.size()) - int(oldKeys.size())) < kMaxIncrementalOps
                      && QSet<QString>(oldKeys.cbegin(), oldKeys.cend()).size() == oldKeys.size()
                      && newSet.size() == newKeys.size();
    }

    if (!incremental) {
        beginResetModel();
        m_rows = rows;
        endResetModel();
    } else {
        // 0. Point the surviving rows at the new items (m_items was replaced already).
        QHash<QString, Row> byKey;
        for (int i = 0; i < newKeys.size(); ++i)
            byKey.insert(newKeys.at(i), rows.at(i));
        QList<Row> cur = m_rows;
        QStringList curKeys = oldKeys;
        for (int i = 0; i < cur.size(); ++i) {
            const auto it = byKey.constFind(curKeys.at(i));
            if (it != byKey.cend())
                cur[i] = it.value();
            else
                cur[i].item = -1;
        }
        m_rows = cur;
        // 1. Remove rows that are gone (bottom-up, contiguous ranges).
        for (int i = int(curKeys.size()) - 1; i >= 0;) {
            if (newSet.contains(curKeys.at(i))) {
                --i;
                continue;
            }
            int first = i;
            while (first - 1 >= 0 && !newSet.contains(curKeys.at(first - 1)))
                --first;
            beginRemoveRows({}, first, i);
            cur.remove(first, i - first + 1);
            curKeys.remove(first, i - first + 1);
            m_rows = cur;
            endRemoveRows();
            i = first - 1;
        }
        // 2. Walk the new order: move rows into place or insert new ones.
        int ops = 0;
        for (int i = 0; i < newKeys.size() && ops < kMaxIncrementalOps; ++i) {
            if (i < curKeys.size() && curKeys.at(i) == newKeys.at(i))
                continue;
            const qsizetype j = curKeys.indexOf(newKeys.at(i), i + 1);
            if (j > i) {
                beginMoveRows({}, int(j), int(j), {}, i);
                curKeys.move(j, i);
                cur.move(j, i);
                m_rows = cur;
                endMoveRows();
            } else {
                beginInsertRows({}, i, i);
                curKeys.insert(i, newKeys.at(i));
                cur.insert(i, rows.at(i));
                m_rows = cur;
                endInsertRows();
            }
            ++ops;
        }
        if (curKeys != newKeys) {
            beginResetModel();
            m_rows = rows;
            endResetModel();
        } else {
            m_rows = rows;
            if (!m_rows.isEmpty())
                Q_EMIT dataChanged(createIndex(0, 0), createIndex(int(m_rows.size()) - 1, 0));
        }
    }
    m_rowKeys = newKeys;
    m_rowOfKey.clear();
    for (int i = 0; i < newKeys.size(); ++i)
        m_rowOfKey.insert(newKeys.at(i), i);
}

QVariantList TaskListModel::sections() const
{
    QVariantList out;
    for (const QString &key : sectionKeys()) {
        const int count = m_sectionCounts.value(key);
        if (count == 0)
            continue;
        out.append(QVariantMap{
                {u"key"_s, key}, {u"label"_s, sectionLabel(key)}, {u"count"_s, count}, {u"collapsed"_s, m_collapsed.contains(key)}});
    }
    return out;
}

} // namespace brulion::planning
