#include "widgetsnapshot.h"

#include "calendarmodel.h"
#include "planningutil.h"
#include "tasklistmodel.h"

#include "dates/local.h"
#include "index/querycache.h"
#include "index/vaultindex.h"
#include "reminders/calendar.h"
#include "notebook/journals.h"
#include "vault/vault.h"

#include <QFileInfo>
#include <QJsonArray>

using namespace Qt::StringLiterals;

namespace brulion::widgets {

namespace {

/// Reminders of a task's block published for the widgets to pick the next one on a later day.
constexpr int MaxTaskReminders = 16;

/// The block of the task at `line` of `path` (nullptr when the index has none).
const Block *taskBlock(const VaultIndex &index, const QString &path, int line)
{
    const ParsedNote *note = index.note(path);
    if (!note)
        return nullptr;
    for (const Block &block : note->blocks) {
        if (block.task && block.task->line == line)
            return &block;
    }
    return nullptr;
}

QJsonArray pagesJson(const QList<PageRef> &pages, int max)
{
    QJsonArray out;
    for (const PageRef &page : pages) {
        if (out.size() >= max)
            break;
        if (page.path.isEmpty())
            continue;
        out.append(QJsonObject{{u"path"_s, page.path}, {u"title"_s, page.title}});
    }
    return out;
}

QJsonObject tasksJson(VaultIndex *index, const SnapshotOptions &options, QDate today, QJsonArray *tasks)
{
    using Model = planning::TaskListModel;
    Model model;
    model.classBegin();
    if (options.caches)
        model.setQueryCache(options.caches->tasks);
    model.setVaultIndex(index);
    model.setToday(planning::isoDate(today));
    model.setWeekStart(int(options.weekStart));
    model.setLocale(options.locale);
    model.setStatusFilter(u"open"_s);
    model.componentComplete();

    const QDate last = today.addDays(options.taskDays);
    QHash<QString, int> counts;
    for (int row = 0; row < model.rowCount(); ++row) {
        const QModelIndex idx = model.index(row);
        if (model.data(idx, Model::RowTypeRole).toString() != u"task")
            continue;
        const QString section = model.data(idx, Model::SectionRole).toString();
        if (section == u"noDate" || section == u"done")
            continue;
        ++counts[section];
        const QString path = model.data(idx, Model::PathRole).toString();
        const int line = model.data(idx, Model::LineRole).toInt();
        const Block *block = taskBlock(*index, path, line);
        if (!block)
            continue;
        // Filed by TaskListModel's own rule (the 14-day cut follows any change of it).
        const std::optional<Reminder> next = Model::nextReminder(block->reminders, today);
        const std::optional<QDate> filed =
                Model::filingDate(*block->task, today, next ? std::optional<QDate>(next->at.date) : std::nullopt);
        if (section != u"overdue" && (!filed || *filed > last))
            continue;
        if (tasks->size() >= options.maxTasks)
            continue;
        QJsonArray reminders;
        for (const Reminder &r : block->reminders) {
            if (reminders.size() >= MaxTaskReminders)
                break;
            reminders.append(QJsonObject{{u"at"_s, r.at.toString()}, {u"alarm"_s, r.alarm}});
        }
        tasks->append(QJsonObject{
                {u"key"_s, model.data(idx, Model::KeyRole).toString()},
                {u"text"_s, model.data(idx, Model::TextRole).toString()},
                {u"status"_s, model.data(idx, Model::StatusRole).toString()},
                {u"priority"_s, model.data(idx, Model::PriorityRole).toInt()},
                {u"path"_s, path},
                {u"line"_s, line},
                {u"page"_s, model.data(idx, Model::PageTitleRole).toString()},
                {u"due"_s, model.data(idx, Model::DueRole).toString()},
                {u"scheduled"_s, model.data(idx, Model::ScheduledRole).toString()},
                {u"start"_s, model.data(idx, Model::StartRole).toString()},
                {u"reminder"_s, model.data(idx, Model::ReminderSpecRole).toString()},
                {u"alarm"_s, model.data(idx, Model::ReminderAlarmRole).toBool()},
                {u"reminders"_s, reminders},
                {u"recurring"_s, !model.data(idx, Model::RecurrenceRole).toString().isEmpty()},
                {u"section"_s, section},
        });
    }
    return QJsonObject{{u"overdue"_s, counts.value(u"overdue"_s)},
                       {u"today"_s, counts.value(u"today"_s)},
                       {u"tomorrow"_s, counts.value(u"tomorrow"_s)}};
}

QJsonObject calendarJson(VaultIndex *index, const SnapshotOptions &options, QDate today)
{
    using Model = planning::CalendarModel;
    Model model;
    model.classBegin();
    if (options.caches)
        model.setCaches(options.caches->calendar, options.caches->overdue);
    model.setVaultIndex(index);
    model.setMode(u"agenda"_s);
    model.setToday(planning::isoDate(today));
    model.setWeekStart(int(options.weekStart));
    model.setLocale(options.locale);
    // Agenda days start at the anchor's week: the anchor a week back, the range to eight weeks on.
    const QDate anchor = today.addDays(-options.calendarDaysBefore);
    const QDate weekStart = dates::startOfWeek(anchor, options.weekStart);
    model.setAgendaDays(int(weekStart.daysTo(today.addDays(options.calendarDaysAfter))) + 1);
    model.setAnchorDate(planning::isoDate(anchor));
    model.componentComplete();

    QJsonObject days;
    for (int row = 0; row < model.rowCount(); ++row) {
        const QModelIndex idx = model.index(row);
        const QString journal = model.data(idx, Model::JournalPathRole).toString();
        const QVariantList items = model.data(idx, Model::ItemsRole).toList();
        if (journal.isEmpty() && items.isEmpty())
            continue; // today, listed by the agenda even when empty
        QJsonArray entries;
        for (const QVariant &value : items) {
            if (entries.size() >= options.maxDayItems)
                break;
            const QVariantMap item = value.toMap();
            entries.append(QJsonObject{
                    {u"time"_s, item.value(u"time"_s).toString()},
                    {u"minutes"_s, item.value(u"minutes"_s).toInt()},
                    {u"title"_s, item.value(u"title"_s).toString()},
                    {u"kind"_s, item.value(u"kind"_s).toString()},
                    {u"variant"_s, item.value(u"variant"_s).toString()},
                    {u"path"_s, item.value(u"path"_s).toString()},
                    {u"line"_s, item.value(u"line"_s).toInt()},
                    {u"isTask"_s, item.value(u"isTask"_s).toBool()},
                    {u"closed"_s, item.value(u"closed"_s).toBool()},
                    {u"alarm"_s, item.value(u"alarm"_s).toBool()},
                    {u"recurring"_s, item.value(u"recurring"_s).toBool()},
            });
        }
        days.insert(model.data(idx, Model::DateRole).toString(),
                    QJsonObject{{u"journal"_s, journal},
                                {u"kinds"_s, QJsonArray::fromStringList(model.data(idx, Model::KindsRole).toStringList())},
                                {u"items"_s, entries}});
    }
    return QJsonObject{{u"from"_s, model.rangeStart()}, {u"to"_s, model.rangeEnd()}, {u"days"_s, days}};
}

QString builtStamp(const QDateTime &now)
{
    return now.toString(u"yyyy-MM-dd'T'HH:mm"_s);
}

} // namespace

std::shared_ptr<SnapshotCaches> SnapshotCaches::create()
{
    auto caches = std::make_shared<SnapshotCaches>();
    caches->tasks = std::make_shared<QueryCache>();
    caches->calendar = std::make_shared<CalendarCache>();
    caches->overdue = std::make_shared<QueryCache>();
    return caches;
}

QJsonObject buildSnapshot(const VaultIndex &constIndex, const SnapshotOptions &options)
{
    // The models only read the index (they keep a QPointer to watch it while they live).
    auto *index = const_cast<VaultIndex *>(&constIndex);
    const QDate today = options.now.date();
    QJsonArray tasks;
    const QJsonObject counts = tasksJson(index, options, today, &tasks);

    QJsonArray capture;
    for (const CaptureDay &day : options.captureDays) {
        if (!day.date.isValid() || day.path.isEmpty())
            continue;
        capture.append(QJsonObject{{u"date"_s, planning::isoDate(day.date)},
                                   {u"path"_s, day.path},
                                   {u"exists"_s, day.exists},
                                   {u"newText"_s, day.newText}});
    }

    return QJsonObject{
            {u"version"_s, SnapshotVersion},
            {u"built"_s, builtStamp(options.now)},
            {u"today"_s, planning::isoDate(today)},
            {u"weekStart"_s, int(options.weekStart)},
            {u"locale"_s, options.locale == u"en" ? u"en"_s : u"pl"_s},
            {u"vault"_s, QJsonObject{{u"id"_s, options.vaultId},
                                     {u"name"_s, options.vaultName},
                                     {u"root"_s, options.vaultRoot}}},
            {u"tasks"_s, tasks},
            {u"taskCounts"_s, counts},
            {u"calendar"_s, calendarJson(index, options, today)},
            {u"notes"_s, QJsonObject{{u"favorites"_s, pagesJson(options.favorites, options.maxPages)},
                                     {u"recent"_s, pagesJson(options.recent, options.maxPages)}}},
            {u"capture"_s, QJsonObject{{u"days"_s, capture}}},
            {u"reminders"_s, options.reminders},
    };
}

QJsonObject emptySnapshot(const QDateTime &now, const QString &locale)
{
    return QJsonObject{
            {u"version"_s, SnapshotVersion},
            {u"built"_s, builtStamp(now)},
            {u"today"_s, planning::isoDate(now.date())},
            {u"weekStart"_s, int(Qt::Monday)},
            {u"locale"_s, locale == u"en" ? u"en"_s : u"pl"_s},
            {u"vault"_s, QJsonValue::Null},
            {u"tasks"_s, QJsonArray()},
            {u"taskCounts"_s, QJsonObject{{u"overdue"_s, 0}, {u"today"_s, 0}, {u"tomorrow"_s, 0}}},
            {u"calendar"_s, QJsonObject{{u"from"_s, QString()}, {u"to"_s, QString()}, {u"days"_s, QJsonObject()}}},
            {u"notes"_s, QJsonObject{{u"favorites"_s, QJsonArray()}, {u"recent"_s, QJsonArray()}}},
            {u"capture"_s, QJsonObject{{u"days"_s, QJsonArray()}}},
    };
}

QList<CaptureDay> captureDays(Vault &vault, QDate today, int days, const QDateTime &now, dates::UiLocale locale)
{
    QList<CaptureDay> out;
    for (int k = 0; k < days; ++k) {
        CaptureDay day;
        day.date = today.addDays(k);
        day.path = notebook::journalFileFor(vault, day.date);
        if (day.path.isEmpty())
            continue;
        day.exists = vault.exists(day.path) || QFileInfo::exists(vault.absolutePath(day.path));
        // Also for a note that exists now: it may be deleted (another device, a file manager)
        // before quick add writes, which then starts it as the app would.
        day.newText = notebook::newJournalText(vault, day.date, now, locale);
        out.append(day);
    }
    return out;
}

} // namespace brulion::widgets
