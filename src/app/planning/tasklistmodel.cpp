#include "tasklistmodel.h"

#include "dates/local.h"
#include "dates/recurrence.h"
#include "index/query.h"
#include "index/querycache.h"
#include "index/tasktime.h"
#include "index/vaultindex.h"
#include "markdown/inlinerenderer.h"
#include "markdown/logbook.h"
#include "markdown/taskdeps.h"
#include "planningutil.h"

#include <QCoreApplication>
#include <QSet>

#include <algorithm>
#include <limits>
#include <numeric>
#include <vector>

namespace brulion::planning {

using namespace Qt::StringLiterals;
using namespace std::chrono_literals;

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

/// The done section's date: the cancelled date of a cancelled task, else the done date, else the
/// cancelled one.
std::optional<QDate> finishedDate(const Task &t)
{
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

/// `folder` without leading and trailing slashes.
QString withoutSlashes(QString folder)
{
    while (folder.endsWith(u'/'))
        folder.chop(1);
    while (folder.startsWith(u'/'))
        folder.remove(0, 1);
    return folder;
}

using ChoiceCount = std::pair<QString, int>; ///< a filter choice (tag, folder, page) and its tasks

bool mostUsedFirst(const ChoiceCount &a, const ChoiceCount &b)
{
    return a.second != b.second ? a.second > b.second : compareNames(a.first, b.first) < 0;
}

bool byName(const ChoiceCount &a, const ChoiceCount &b)
{
    return compareNames(a.first, b.first) < 0;
}

/// The entries of `counts` in `less` order, at most `limit` of them (0: all).
QList<ChoiceCount> sortedCounts(const QHash<QString, int> &counts,
                                bool (*less)(const ChoiceCount &, const ChoiceCount &), int limit)
{
    QList<ChoiceCount> list;
    list.reserve(counts.size());
    for (auto it = counts.cbegin(); it != counts.cend(); ++it)
        list.append({it.key(), it.value()});
    std::ranges::sort(list, less);
    if (limit > 0 && list.size() > limit)
        list.resize(limit);
    return list;
}

} // namespace

TaskListModel::TaskListModel(QObject *parent) : QAbstractListModel(parent)
{
    m_refreshTimer.setSingleShot(true);
    m_refreshTimer.setInterval(150ms);
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

std::optional<QDate> TaskListModel::filingDate(const Task &task, QDate today, std::optional<QDate> reminder)
{
    if (const auto dated = sectionDate(task))
        return dated->date;
    if (reminder)
        return reminder;
    if (task.start && task.start->date > today)
        return task.start->date;
    return std::nullopt;
}

std::optional<Reminder> TaskListModel::nextReminder(const QList<Reminder> &reminders, QDate today)
{
    std::optional<Reminder> next;
    for (const Reminder &r : reminders) {
        if (!next || (next->at.date < today && r.at.date >= today)
            || (r.at.date >= today && dates::compareDateSpecs(r.at, next->at) < 0))
            next = r;
    }
    return next;
}

QString TaskListModel::sectionOf(const Task &task, QDate today, Qt::DayOfWeek weekStart, std::optional<QDate> reminder)
{
    if (isClosed(task.status))
        return u"done"_s;
    if (task.due && task.due->date < today)
        return u"overdue"_s;
    const std::optional<QDate> date = filingDate(task, today, reminder);
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
    switch (role) {
    case SectionRole: return row.section;
    case SectionLabelRole: return sectionLabel(row.section);
    case SectionCountRole: return m_sectionCounts.value(row.section);
    case DepthRole: return row.depth;
    case SubtaskCountRole: return row.subtasks;
    case FoldedRole: return row.subtasks > 0 && m_folded.contains(rowKey(row));
    default: return taskRowData(m_items.at(row.item), role);
    }
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
    case DepthRole:
    case SubtaskCountRole:
    case PriorityRole:
    case BlockingCountRole: return 0;
    case OverdueRole:
    case IsJournalRole:
    case ReminderAlarmRole:
    case BlockedRole:
    case ProgressCompleteRole:
    case ClockRunningRole:
    case FoldedRole: return false;
    case TagsRole: return QStringList();
    case ReminderSpecRole:
    case BlockedByRole:
    case BlockedByIdRole:
    case DependencyProblemRole:
    case ProgressRole:
    case TimeLabelRole:
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
    case CollapsedRole: return false;
    case KeyRole: return taskKey(item.ref.path, t.line);
    case Qt::DisplayRole:
    case TextRole: return plainText(t.text);
    case HtmlRole:
        if (!item.rendered) {
            item.html = renderInline(m_index.data(), t.text, item.ref.path, today, loc, m_palette);
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
    case BlockedRole: return item.blocked;
    case BlockedByRole: return item.blockedBy;
    case BlockedByIdRole: return item.blockedById;
    case BlockingCountRole: return item.blockingCount;
    case DependencyProblemRole: return item.dependencyProblem;
    case ProgressRole:
        return item.progressTotal > 0 ? u"%1/%2"_s.arg(item.progressDone).arg(item.progressTotal) : QString();
    case ProgressCompleteRole: return item.progressTotal > 0 && item.progressDone >= item.progressTotal;
    case TimeLabelRole: return markdown::taskTimeLabel(t.clockedSeconds, t.clockStart, loc);
    case ClockRunningRole: return t.clockStart.isValid();
    default: return {};
    }
}

QVariantMap TaskListModel::timeSummary(const QString &group, const QString &range) const
{
    QVariantMap out;
    if (!m_index)
        return out;
    const TimeGroup g = group == u"page"   ? TimeGroup::Page
                        : group == u"tag"  ? TimeGroup::Tag
                        : group == u"task" ? TimeGroup::Task
                                           : TimeGroup::Day;
    const QDate today = currentDay();
    QDate from;
    QDate to;
    if (range == u"today") {
        from = to = today;
    } else if (range == u"week") {
        from = dates::startOfWeek(today, weekStartDay());
        to = from.addDays(6);
    } else if (range == u"month") {
        from = dates::startOfMonth(today);
        to = dates::endOfMonth(today);
    }
    QList<TaskRef> tasks;
    tasks.reserve(m_items.size());
    for (const Item &item : m_items)
        tasks.append(item.ref);
    // A running clock counts up to now (on a fixed `today`, a test's, up to its end).
    const QDateTime now = m_todayText.isEmpty() ? QDateTime::currentDateTime() : QDateTime(today, QTime(23, 59, 59));
    const TimeSummary summary = summarizeTime(*m_index, tasks, g, from, to, now);
    const dates::UiLocale loc = uiLocale(m_locale);
    QVariantList rows;
    for (const TimeSummaryRow &r : summary.rows) {
        QString label = r.label;
        if (g == TimeGroup::Day)
            label = humanDate(DateSpec{QDate::fromString(r.key, Qt::ISODate), std::nullopt}, loc, today);
        else if (g == TimeGroup::Task)
            label = plainText(r.label);
        rows.append(QVariantMap{{u"key"_s, r.key},
                                {u"label"_s, label},
                                {u"seconds"_s, r.seconds},
                                {u"duration"_s, markdown::formatSpentTime(r.seconds)},
                                {u"tasks"_s, r.tasks},
                                {u"path"_s, r.path},
                                {u"line"_s, r.line}});
    }
    out.insert(u"rows"_s, rows);
    out.insert(u"total"_s, summary.total);
    out.insert(u"totalLabel"_s, markdown::formatSpentTime(summary.total));
    out.insert(u"tasks"_s, summary.tasks);
    return out;
}

QVariantList TaskListModel::paletteChoices(const QString &kind) const
{
    const bool page = kind == u"page";
    const QVariantList choices = kind == u"tag"      ? tagChoices(0)
                                 : page              ? pageChoices(0)
                                 : kind == u"folder" ? folderChoices(0)
                                                     : QVariantList();
    const QString icon = kind == u"tag" ? u"hash"_s : page ? u"file-text"_s : u"folder"_s;
    QVariantList out;
    out.reserve(choices.size());
    for (const QVariant &choice : choices) {
        const QVariantMap c = choice.toMap();
        const QString count =
                QCoreApplication::translate("TasksFilterBar", "%n tasks", nullptr, c.value(u"count"_s).toInt());
        const QString folder = page ? c.value(u"folder"_s).toString() : QString();
        out.append(QVariantMap{{u"title"_s, c.value(u"label"_s).toString()},
                               {u"subtitle"_s, folder.isEmpty() ? count : folder + u" · "_s + count},
                               {u"icon"_s, icon},
                               {u"value"_s, c.value(u"value"_s).toString()}});
    }
    return out;
}

QHash<int, QByteArray> TaskListModel::roleNames() const
{
    static const QHash<int, QByteArray> names = {
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
            {BlockedRole, "blocked"},
            {BlockedByRole, "blockedBy"},
            {BlockedByIdRole, "blockedById"},
            {BlockingCountRole, "blockingCount"},
            {DependencyProblemRole, "dependencyProblem"},
            {ProgressRole, "progress"},
            {ProgressCompleteRole, "progressComplete"},
            {DepthRole, "depth"},
            {SubtaskCountRole, "subtaskCount"},
            {FoldedRole, "folded"},
            {TimeLabelRole, "timeLabel"},
            {ClockRunningRole, "clockRunning"},
            {ReminderLabelRole, "reminderLabel"},
            {ReminderAlarmRole, "reminderAlarm"},
            {ReminderSpecRole, "reminderSpec"},
            {DoneDateRole, "doneDate"},
            {CreatedRole, "created"},
    };
    return names;
}

QVariantMap TaskListModel::get(int row) const
{
    QVariantMap out;
    if (row < 0 || row >= m_rows.size())
        return out;
    const QHash<int, QByteArray> names = roleNames();
    const QModelIndex idx = createIndex(row, 0);
    for (auto [role, name] : names.asKeyValueRange())
        out.insert(QString::fromLatin1(name), data(idx, role));
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
    m_palette = paletteFromMap(palette);
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

void TaskListModel::setFilter(QString &filter, const QString &value)
{
    if (filter == value)
        return;
    filter = value;
    filtersEdited();
}

void TaskListModel::setTagFilter(const QString &tag)
{
    QString t = tag.trimmed();
    if (t.startsWith(u'#'))
        t.remove(0, 1);
    if (t.startsWith(u"[[") && t.endsWith(u"]]"))
        t = t.mid(2, t.size() - 4).trimmed();
    setFilter(m_tag, t.toLower());
}

void TaskListModel::setPageFilter(const QString &path)
{
    setFilter(m_page, path.trimmed());
}

void TaskListModel::setFolderFilter(const QString &folder)
{
    setFilter(m_folder, withoutSlashes(folder.trimmed()));
}

void TaskListModel::setPriorityFilter(const QString &priority)
{
    static const QStringList known{u"highest"_s, u"high"_s, u"medium"_s, u"low"_s, u"lowest"_s, u"none"_s};
    setFilter(m_priority, known.contains(priority) ? priority : QString());
}

void TaskListModel::setStatusFilter(const QString &status)
{
    static const QStringList known{u"all"_s, u"open"_s, u"todo"_s, u"doing"_s, u"deferred"_s, u"done"_s, u"cancelled"_s};
    setFilter(m_status, known.contains(status) ? status : u"all"_s);
}

void TaskListModel::setTextFilter(const QString &text)
{
    setFilter(m_text, text.simplified());
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

void TaskListModel::setNestSubtasks(bool nest)
{
    if (m_nestSubtasks == nest)
        return;
    m_nestSubtasks = nest;
    Q_EMIT nestSubtasksChanged();
    if (m_completing) {
        m_rebuildPending = true;
        return;
    }
    layoutTasks();
    rebuildRows();
    Q_EMIT countsChanged();
}

void TaskListModel::toggleSubtasks(const QString &key)
{
    setSubtasksFolded(key, !m_folded.contains(key));
}

void TaskListModel::setSubtasksFolded(const QString &key, bool folded)
{
    if (m_folded.contains(key) == folded)
        return;
    if (!folded) {
        m_folded.remove(key);
    } else {
        const auto task = std::find_if(m_layout.cbegin(), m_layout.cend(),
                                       [&](const Row &row) { return rowKey(row) == key; });
        if (task == m_layout.cend() || task->subtasks == 0)
            return;
        m_folded.insert(key, plainText(m_items.at(task->item).ref.task.text));
    }
    rebuildRows(); // (the rows that stay get dataChanged: the task's folded role)
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

void TaskListModel::setQueryCache(std::shared_ptr<QueryCache> cache)
{
    m_query = std::move(cache);
    m_follower.reset();
}

QString TaskListModel::pageQueryValue(const QString &path) const
{
    // The title reads best; use it when it resolves back to the same page, else the path.
    QString p = path;
    if (p.endsWith(u".md", Qt::CaseInsensitive))
        p.chop(3);
    const QString title = p.section(u'/', -1);
    return queryValue(m_index && m_index->resolveLink(title) == path ? title : p);
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
    for (const QStringList words = m_text.split(u' ', Qt::SkipEmptyParts); const QString &w : words)
        parts << textWord(w);
    // m_sort is one of setSortKey()'s keys; created sorts the newest first.
    parts << u"sort:"_s + (m_sort == u"created" ? u"-created"_s : m_sort);
    return parts.join(u' ');
}

bool TaskListModel::applyQuery(const QString &query)
{
    const auto parsed = parseQuery(query);
    const auto *q = std::get_if<Query>(&parsed);
    if (!q)
        return false;
    QString tag, page, folder, priority, status = u"all"_s;
    QStringList words;
    for (const QueryFilter &f : q->filters) {
        if (f.negate)
            continue;
        switch (f.kind) {
        case QueryFilterKind::Tag:
            if (!f.values.isEmpty())
                tag = f.values.first();
            break;
        case QueryFilterKind::Page:
            if (!f.values.isEmpty()) {
                const QString &v = f.values.first();
                page = m_index ? m_index->resolveLink(v).value_or(v) : v;
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
            std::ranges::sort(s);
            if (s == QList<TaskStatus>{TaskStatus::Todo, TaskStatus::Doing})
                status = u"open"_s;
            else if (s.size() == 1)
                status = statusName(s.first());
            break;
        }
        case QueryFilterKind::Text: words << f.values; break;
        case QueryFilterKind::Ref:
        case QueryFilterKind::Date:
        case QueryFilterKind::Is: break;
        }
    }
    QString sort = m_sort;
    if (!q->sort.isEmpty()) {
        switch (q->sort.first().key) {
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
    page = page.trimmed();
    folder = withoutSlashes(folder).trimmed();
    const QString text = words.join(u' ').simplified();
    // The same filters again (TasksView applies its route when created and when complete).
    if (tag == m_tag && page == m_page && folder == m_folder && priority == m_priority && status == m_status
        && text == m_text && sort == m_sort)
        return true;
    const bool sortChanged = sort != m_sort;
    m_tag = tag;
    m_page = page;
    m_folder = folder;
    m_priority = priority;
    m_status = status;
    m_text = text;
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
    for (const QList<TaskRef> tasks = m_index->tasks(); const TaskRef &ref : tasks) {
        const ParsedNote *note = m_index->note(ref.path);
        if (!note || ref.blockIndex < 0 || ref.blockIndex >= note->blocks.size())
            continue;
        for (const QString &tag : note->blocks.at(ref.blockIndex).tags)
            ++counts[tag];
    }
    QHash<QString, QString> display;
    for (const QList<TagCount> tags = m_index->tags(); const TagCount &t : tags)
        display.insert(t.tag, t.display.isEmpty() ? t.tag : t.display);
    for (const QList<ChoiceCount> list = sortedCounts(counts, mostUsedFirst, limit); const auto &[tag, count] : list)
        out.append(QVariantMap{
                {u"value"_s, tag}, {u"label"_s, QString(u'#' + display.value(tag, tag))}, {u"count"_s, count}});
    return out;
}

QVariantList TaskListModel::folderChoices(int limit) const
{
    QVariantList out;
    if (!m_index)
        return out;
    QHash<QString, int> counts;
    for (const QList<TaskRef> tasks = m_index->tasks(); const TaskRef &ref : tasks) {
        // Every ancestor folder counts ("Projekty/Dom/x.md" -> "Projekty", "Projekty/Dom").
        for (QString folder = folderOf(ref.path); !folder.isEmpty(); folder = folderOf(folder))
            ++counts[folder];
    }
    for (const QList<ChoiceCount> list = sortedCounts(counts, byName, limit); const auto &[folder, count] : list)
        out.append(QVariantMap{{u"value"_s, folder}, {u"label"_s, folder}, {u"count"_s, count}});
    return out;
}

QVariantList TaskListModel::pageChoices(int limit) const
{
    QVariantList out;
    if (!m_index)
        return out;
    QHash<QString, int> counts;
    for (const QList<TaskRef> tasks = m_index->tasks(); const TaskRef &ref : tasks)
        ++counts[ref.path];
    for (const QList<ChoiceCount> list = sortedCounts(counts, mostUsedFirst, limit); const auto &[path, count] : list) {
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

TaskListModel::Item TaskListModel::makeItem(const QueryItem &qi, QDate today, Qt::DayOfWeek weekStart)
{
    static const QStringList sections = sectionKeys();
    Item item;
    item.ref = TaskRef{qi.path, qi.blockIndex, *qi.task};
    item.folder = folderOf(qi.path);
    if (const ParsedNote *note = m_index ? m_index->note(qi.path) : nullptr) {
        item.pageTitle = note->title;
        item.isJournal = note->journalDate.has_value();
        if (qi.blockIndex >= 0 && qi.blockIndex < note->blocks.size()) {
            const Block &block = note->blocks.at(qi.blockIndex);
            item.tags = block.tags;
            item.reminder = nextReminder(block.reminders, today);
            const markdown::SubtaskProgress progress = markdown::subtaskProgress(*note, qi.blockIndex);
            item.progressDone = progress.done;
            item.progressTotal = progress.total;
            for (int p = block.parent; p >= 0 && p < note->blocks.size(); p = note->blocks.at(p).parent) {
                if (note->blocks.at(p).task)
                    item.taskAncestors.append(p);
                if (note->blocks.at(p).parent >= p)
                    break; // (pre-order: a parent comes first; never loops)
            }
            if (!qi.task->dependsOn.isEmpty() || !qi.task->id.isEmpty()) {
                const TaskDependencies deps = m_index->taskDependencies(qi.path, qi.blockIndex);
                item.blocked = deps.blocked;
                item.blockingCount = int(deps.dependents.size());
                item.dependencyProblem = deps.cycle ? u"cycle"_s : deps.missing ? u"missing"_s : QString();
                for (const TaskDependency &dep : deps.dependsOn) {
                    const auto open = std::find_if(dep.tasks.cbegin(), dep.tasks.cend(),
                                                   [](const TaskRef &r) { return !isClosed(r.task.status); });
                    if (open != dep.tasks.cend()) {
                        item.blockedBy = plainText(open->task.text);
                        item.blockedById = dep.id;
                        break;
                    }
                }
            }
        }
    }
    const Task &t = item.ref.task;
    item.section =
            sectionOf(t, today, weekStart, item.reminder ? std::optional<QDate>(item.reminder->at.date) : std::nullopt);
    item.sectionRank = sections.indexOf(item.section);
    item.finished = finishedDate(t);
    item.priority = priorityRank(t.priority);
    item.date = sortDate(t);
    if (!item.date && item.reminder)
        item.date = item.reminder->at;
    return item;
}

void TaskListModel::rankTitles(QList<Item> &items)
{
    // The distinct titles sorted once by their collation keys (kept per title: many tasks share a
    // page, and a rebuild sees the same pages again); the comparison then reads integers.
    m_titleRank.clear();
    for (const Item &item : std::as_const(items))
        m_titleRank.insert(item.pageTitle, 0);
    std::vector<std::pair<QString, QCollatorSortKey>> titles; // copies share the key's data
    titles.reserve(size_t(m_titleRank.size()));
    for (auto it = m_titleRank.cbegin(); it != m_titleRank.cend(); ++it) {
        auto key = m_titleKeys.find(it.key());
        if (key == m_titleKeys.end())
            key = m_titleKeys.insert(it.key(), nameSortKey(it.key()));
        titles.emplace_back(it.key(), *key);
    }
    std::ranges::sort(titles, [](const auto &a, const auto &b) {
        const int c = a.second.compare(b.second);
        return c != 0 ? c < 0 : QString::compare(a.first, b.first) < 0;
    });
    for (size_t rank = 0; rank < titles.size(); ++rank)
        m_titleRank[titles[rank].first] = int(rank);
    for (Item &item : items)
        item.title = m_titleRank.value(item.pageTitle);
}

bool TaskListModel::itemLess(const Item &a, const Item &b) const
{
    if (a.sectionRank != b.sectionRank)
        return a.sectionRank < b.sectionRank;
    const Task &ta = a.ref.task;
    const Task &tb = b.ref.task;
    static const qsizetype doneRank = sectionKeys().indexOf(u"done"_s);
    const bool done = a.sectionRank == doneRank;
    // Newest first, the undated last (std::optional orders nullopt first).
    if (done && a.finished != b.finished)
        return a.finished > b.finished;
    if (!done && m_sortMode == SortByPriority && a.priority != b.priority)
        return a.priority < b.priority;
    if (!done && m_sortMode == SortByCreated && ta.created != tb.created)
        return ta.created > tb.created;
    if (done || m_sortMode != SortByPage) {
        if (const int c = compareOptionalSpecs(a.date, b.date); c != 0)
            return c < 0;
        if (a.priority != b.priority)
            return a.priority < b.priority;
    }
    // compareNames() order of the page titles (ranks), then the path and the line.
    if (a.title != b.title)
        return a.title < b.title;
    if (a.ref.path != b.ref.path)
        return a.ref.path < b.ref.path;
    return ta.line < tb.line;
}

void TaskListModel::rebuild()
{
    m_refreshTimer.stop();
    m_rebuildPending = false;
    const QDate today = currentDay();
    const Qt::DayOfWeek ws = weekStartDay();
    m_sortMode = m_sort == u"priority"  ? SortByPriority
                 : m_sort == u"created" ? SortByCreated
                 : m_sort == u"page"    ? SortByPage
                                        : SortByDue;
    std::optional<Query> query;
    if (m_index) {
        auto parsed = parseQuery(queryText());
        if (auto *q = std::get_if<Query>(&parsed))
            query = std::move(*q);
    }
    if (!query) {
        m_items.clear();
        m_follower.reset();
    } else {
        QueryOptions options;
        options.defaultLimit = std::numeric_limits<int>::max();
        if (!m_query)
            m_query = std::make_shared<QueryCache>();
        m_query->update(*query, *m_index, today, ws, options);
        // The items follow the index on their own (the cache may be shared): what changed since
        // they were built. Their fields depend on the query (filters and sort), today and the
        // week start; filters resolving names follow the cache's rule (queryResolvesNames()).
        const QString key = query->source + u'\x1f' + today.toString(Qt::ISODate) + u'\x1f' + QString::number(int(ws));
        const NoteCacheSync sync = m_follower.follow(*m_index, key, queryResolvesNames(*query));
        // Only the changed notes: their items again (one pass over the cache's matches, however
        // many notes changed), merged into the others' sorted ones — unless a title is new (its
        // rank among the others is not known: everything again).
        QList<Item> fresh;
        bool full = sync.full;
        QSet<VaultPath> changed;
        if (!full && !sync.changed.isEmpty()) {
            changed = QSet<VaultPath>(sync.changed.cbegin(), sync.changed.cend());
            for (const QList<QueryItem> matches = m_query->itemsOf(changed, *m_index); const QueryItem &qi : matches) {
                if (!qi.task)
                    continue;
                fresh.append(makeItem(qi, today, ws));
                const auto rank = m_titleRank.constFind(fresh.last().pageTitle);
                if (rank == m_titleRank.cend()) {
                    full = true;
                    break;
                }
                fresh.last().title = *rank;
            }
        }
        if (full) {
            const QueryResult result = m_query->result(*m_index);
            QList<Item> items;
            items.reserve(result.items.size());
            for (const QueryItem &qi : result.items) {
                if (qi.task)
                    items.append(makeItem(qi, today, ws));
            }
            rankTitles(items);
            // Through a permutation: an item is too big to move around n log n times.
            QList<qsizetype> order(items.size());
            std::iota(order.begin(), order.end(), 0);
            std::ranges::sort(order, [&](qsizetype x, qsizetype y) { return itemLess(items.at(x), items.at(y)); });
            QList<Item> sorted;
            sorted.reserve(items.size());
            for (const qsizetype i : std::as_const(order))
                sorted.append(std::move(items[i]));
            m_items = std::move(sorted);
        } else if (!changed.isEmpty()) {
            m_items.removeIf([&](const Item &item) { return changed.contains(item.ref.path); });
            if (!fresh.isEmpty()) {
                const auto less = [this](const Item &a, const Item &b) { return itemLess(a, b); };
                std::ranges::sort(fresh, less);
                QList<Item> merged;
                merged.reserve(m_items.size() + fresh.size());
                std::merge(std::make_move_iterator(m_items.begin()), std::make_move_iterator(m_items.end()),
                           std::make_move_iterator(fresh.begin()), std::make_move_iterator(fresh.end()),
                           std::back_inserter(merged), less);
                m_items = std::move(merged);
            }
        }
    }
    m_total = int(m_items.size());
    m_open = 0;
    m_overdue = 0;
    m_todayCount = 0;
    for (Item &item : m_items) {
        item.rendered = false; // the rich text shows links into other notes: rendered again when shown
        if (!isClosed(item.ref.task.status))
            ++m_open;
        if (item.section == u"overdue")
            ++m_overdue;
        else if (item.section == u"today")
            ++m_todayCount;
    }
    m_vaultTasks = m_index ? int(m_index->tasks().size()) : 0;
    layoutTasks();
    rebuildRows();
    Q_EMIT countsChanged();
}

void TaskListModel::layoutTasks()
{
    const qsizetype n = m_items.size();
    // The listed task each item follows (the nearest listed ancestor task), -1 for none.
    QList<int> parentOf(n, -1);
    const auto hasAncestors = [](const Item &item) { return !item.taskAncestors.isEmpty(); };
    if (m_nestSubtasks && std::ranges::any_of(std::as_const(m_items), hasAncestors)) {
        QHash<std::pair<QString, int>, int> itemOfBlock; // (path, block index) -> item
        itemOfBlock.reserve(n);
        for (int i = 0; i < n; ++i)
            itemOfBlock.insert({m_items.at(i).ref.path, m_items.at(i).ref.blockIndex}, i);
        for (int i = 0; i < n; ++i) {
            const Item &item = m_items.at(i);
            for (const int ancestor : item.taskAncestors) {
                if (const int p = itemOfBlock.value({item.ref.path, ancestor}, -1); p >= 0) {
                    parentOf[i] = p;
                    break;
                }
            }
        }
        // The section a task is shown in: its own, or the one of the task it follows unless
        // it is more urgent than that. Ancestors first (a parent's block index is smaller).
        QList<int> order(n);
        std::iota(order.begin(), order.end(), 0);
        std::ranges::sort(order, [&](int a, int b) {
            const TaskRef &x = m_items.at(a).ref;
            const TaskRef &y = m_items.at(b).ref;
            return x.path != y.path ? x.path < y.path : x.blockIndex < y.blockIndex;
        });
        QList<qsizetype> shownRank(n, -1);
        for (const int i : std::as_const(order)) {
            const qsizetype own = m_items.at(i).sectionRank;
            const int p = parentOf[i];
            if (p >= 0 && shownRank[p] >= 0 && own >= shownRank[p]) {
                shownRank[i] = shownRank[p];
            } else {
                parentOf[i] = -1;
                shownRank[i] = own;
            }
        }
    }
    // Each task's subtasks in the note's order.
    QHash<int, QList<int>> children;
    for (int i = 0; i < n; ++i) {
        if (parentOf[i] >= 0)
            children[parentOf[i]].append(i);
    }
    for (QList<int> &list : children) {
        std::ranges::sort(list,
                          [&](int a, int b) { return m_items.at(a).ref.blockIndex < m_items.at(b).ref.blockIndex; });
    }
    m_layout.clear();
    m_layout.reserve(n);
    m_sectionCounts.clear();
    QList<std::pair<int, int>> stack; // (item, depth)
    for (int i = 0; i < n; ++i) {
        if (parentOf[i] >= 0)
            continue;
        const QString &section = m_items.at(i).section;
        stack.append({i, 0});
        while (!stack.isEmpty()) {
            const auto [item, depth] = stack.takeLast();
            m_layout.append(Row{false, section, item, depth});
            ++m_sectionCounts[section];
            const auto kids = children.constFind(item);
            if (kids == children.cend())
                continue;
            for (qsizetype k = kids->size() - 1; k >= 0; --k)
                stack.append({kids->at(k), depth + 1});
        }
    }
    // Each row's subtasks: the rows after it that are deeper, up to the next one that is not (a
    // tree's first row, at depth 0, closes all rows before it).
    QList<qsizetype> open; // rows whose subtasks are still being counted
    for (qsizetype r = 0; r < m_layout.size(); ++r) {
        while (!open.isEmpty() && m_layout.at(open.last()).depth >= m_layout.at(r).depth)
            open.removeLast();
        for (const qsizetype o : std::as_const(open))
            ++m_layout[o].subtasks;
        open.append(r);
    }
    followFoldedTasks();
}

void TaskListModel::followFoldedTasks()
{
    if (m_folded.isEmpty())
        return;
    // A folded task stays folded at its key while that line holds a task with the same text and
    // subtasks; else the nearest task of its note with that text and subtasks takes the fold (an
    // edit moved it); else the fold is gone.
    QHash<QString, QList<const Item *>> byPath; // a note's tasks with subtasks, in row order
    for (const Row &row : std::as_const(m_layout)) {
        if (row.subtasks > 0) {
            const Item &item = m_items.at(row.item);
            byPath[item.ref.path].append(&item);
        }
    }
    QHash<QString, QString> folded;
    for (auto [key, text] : std::as_const(m_folded).asKeyValueRange()) {
        const qsizetype colon = key.lastIndexOf(u':');
        const QString path = key.left(colon);
        const int line = QStringView(key).sliced(colon + 1).toInt();
        std::optional<int> best; // the line of the nearest task with that text
        for (const QList<const Item *> tasks = byPath.value(path); const Item *item : tasks) {
            const Task &task = item->ref.task;
            if ((!best || std::abs(task.line - line) < std::abs(*best - line)) && plainText(task.text) == text)
                best = task.line;
        }
        if (best)
            folded.insert(taskKey(path, *best), text);
    }
    m_folded = std::move(folded);
}

QString TaskListModel::rowKey(const Row &row) const
{
    const Item &item = m_items.at(row.item);
    return taskKey(item.ref.path, item.ref.task.line);
}

void TaskListModel::rebuildRows()
{
    QList<Row> rows;
    QStringList keys;
    QString current;
    int foldedDepth = -1; // the depth of the folded task whose subtasks are skipped
    for (const Row &task : std::as_const(m_layout)) {
        if (task.section != current) {
            current = task.section;
            rows.append(Row{true, current, -1});
            keys.append(u"header:"_s + current);
            foldedDepth = -1;
        }
        if (m_collapsed.contains(task.section) || (foldedDepth >= 0 && task.depth > foldedDepth))
            continue;
        const QString key = rowKey(task);
        foldedDepth = task.subtasks > 0 && m_folded.contains(key) ? task.depth : -1;
        rows.append(task);
        keys.append(key);
    }
    QHash<QString, int> newRow; // key -> its row in `rows`
    newRow.reserve(keys.size());
    for (int i = 0; i < keys.size(); ++i)
        newRow.insert(keys.at(i), i);

    // Keep the view steady (scroll position, delegates) with row moves/inserts/removes instead of a
    // reset: ticking off a task moves one row. Large reshuffles reset. Each step below changes
    // m_rows and m_rowKeys together, between its begin and end signals.
    const auto gone = [&newRow](const QString &key) { return !newRow.contains(key); };
    const qsizetype changes =
            std::ranges::count_if(std::as_const(m_rowKeys), gone) + std::abs(keys.size() - m_rowKeys.size());
    const bool incremental = !m_rows.isEmpty() && changes < kMaxIncrementalOps && newRow.size() == keys.size()
                             && m_rowOfKey.size() == m_rowKeys.size(); // the keys are unique, new and old
    if (incremental) {
        // 0. Point the surviving rows at the new items (m_items was replaced already).
        for (int i = 0; i < m_rows.size(); ++i) {
            if (const int row = newRow.value(m_rowKeys.at(i), -1); row >= 0)
                m_rows[i] = rows.at(row);
            else
                m_rows[i].item = -1;
        }
        // 1. Remove rows that are gone (bottom-up, contiguous ranges).
        for (int last = int(m_rowKeys.size()) - 1; last >= 0; --last) {
            if (!gone(m_rowKeys.at(last)))
                continue;
            int first = last;
            while (first > 0 && gone(m_rowKeys.at(first - 1)))
                --first;
            beginRemoveRows({}, first, last);
            m_rows.remove(first, last - first + 1);
            m_rowKeys.remove(first, last - first + 1);
            endRemoveRows();
            last = first;
        }
        // 2. Walk the new order: move rows into place or insert new ones.
        int ops = 0;
        for (int i = 0; i < keys.size() && ops < kMaxIncrementalOps; ++i) {
            if (i < m_rowKeys.size() && m_rowKeys.at(i) == keys.at(i))
                continue;
            if (const int j = int(m_rowKeys.indexOf(keys.at(i), i + 1)); j > i) {
                beginMoveRows({}, j, j, {}, i);
                m_rowKeys.move(j, i);
                m_rows.move(j, i);
                endMoveRows();
            } else {
                beginInsertRows({}, i, i);
                m_rowKeys.insert(i, keys.at(i));
                m_rows.insert(i, rows.at(i));
                endInsertRows();
            }
            ++ops;
        }
    }
    if (!incremental || m_rowKeys != keys) {
        beginResetModel();
        m_rows = std::move(rows);
        m_rowKeys = std::move(keys);
        m_rowOfKey = std::move(newRow);
        endResetModel();
    } else {
        // The rows are the new ones now (moved, inserted, pointed at the new items): any data changed.
        m_rowOfKey = std::move(newRow);
        if (!m_rows.isEmpty())
            Q_EMIT dataChanged(createIndex(0, 0), createIndex(int(m_rows.size()) - 1, 0));
    }
}

QVariantList TaskListModel::sections() const
{
    QVariantList out;
    for (const QStringList keys = sectionKeys(); const QString &key : keys) {
        const int count = m_sectionCounts.value(key);
        if (count == 0)
            continue;
        out.append(QVariantMap{
                {u"key"_s, key}, {u"label"_s, sectionLabel(key)}, {u"count"_s, count}, {u"collapsed"_s, m_collapsed.contains(key)}});
    }
    return out;
}

} // namespace brulion::planning
