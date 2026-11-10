#include "calendarmodel.h"

#include "dates/format.h"
#include "dates/local.h"
#include "dates/names.h"
#include "index/vaultindex.h"
#include "planningutil.h"

#include <QDateTime>

#include <algorithm>
#include <iterator>

namespace brulion::planning {

using namespace Qt::StringLiterals;
using namespace std::chrono_literals;

namespace {

QString capitalized(QString s)
{
    if (!s.isEmpty())
        s[0] = s[0].toUpper();
    return s;
}

QString kindName(CalendarItem::Kind kind)
{
    switch (kind) {
    case CalendarItem::Journal: return u"journal"_s;
    case CalendarItem::Due: return u"due"_s;
    case CalendarItem::Scheduled: return u"scheduled"_s;
    case CalendarItem::Start: return u"start"_s;
    case CalendarItem::Reminder: return u"reminder"_s;
    case CalendarItem::Done: return u"done"_s;
    }
    return u"due"_s;
}

QString iconFor(const QString &variant, CalendarItem::Kind kind)
{
    if (kind == CalendarItem::Start)
        return u"calendar-clock"_s;
    if (variant == u"overdue" || variant == u"due")
        return u"calendar"_s;
    if (variant == u"scheduled")
        return u"hourglass"_s;
    if (variant == u"alarm")
        return u"alarm-clock"_s;
    if (variant == u"reminder")
        return u"bell"_s;
    if (variant == u"done")
        return u"check"_s;
    return u"calendar"_s;
}

/// A day with its month's name in `form`: "October 15" / "15 października" (Genitive: the full name
/// after a day number, in English the plain full name).
QString dayMonth(QDate d, dates::UiLocale locale, dates::NameForm form)
{
    const QString month = dates::monthName(d.month(), locale, form);
    const QString day = QString::number(d.day());
    return locale == dates::UiLocale::English ? u"%1 %2"_s.arg(month, day) : u"%1 %2"_s.arg(day, month);
}

/// The days [a, b] without their year: "October 12–18" / "12–18 października" within a month, else
/// "September 28 – October 4" / "28 września – 4 października" (month names in `form`).
QString dayMonthRange(QDate a, QDate b, dates::UiLocale locale, dates::NameForm form)
{
    if (a.month() != b.month() || a.year() != b.year())
        return dayMonth(a, locale, form) + u" – "_s + dayMonth(b, locale, form);
    const QString month = dates::monthName(a.month(), locale, form);
    const QString first = QString::number(a.day());
    const QString last = QString::number(b.day());
    return locale == dates::UiLocale::English ? u"%1 %2–%3"_s.arg(month, first, last)
                                              : u"%1–%2 %3"_s.arg(first, last, month);
}

/// `text` with the `year` after it: "October 15, 2026" / "15 października 2026".
QString withYear(const QString &text, int year, dates::UiLocale locale)
{
    return text + (locale == dates::UiLocale::English ? u", "_s : u" "_s) + QString::number(year);
}

/// The title of the days [a, b]: "October 12–18, 2026", "September 28 – October 4, 2026",
/// "December 28, 2026 – January 3, 2027" (Polish: "12–18 października 2026" …).
QString rangeTitle(QDate a, QDate b, dates::UiLocale locale)
{
    constexpr auto form = dates::NameForm::Genitive;
    if (a.year() != b.year())
        return withYear(dayMonth(a, locale, form), a.year(), locale) + u" – "_s
               + withYear(dayMonth(b, locale, form), b.year(), locale);
    return withYear(a == b ? dayMonth(a, locale, form) : dayMonthRange(a, b, locale, form), a.year(), locale);
}

/// "Thursday, October 15" / "czwartek, 15 października".
QString longLabel(QDate d, dates::UiLocale locale)
{
    return dates::weekdayName(d.dayOfWeek(), locale) + u", "_s + dayMonth(d, locale, dates::NameForm::Genitive);
}

/// Is `item` (a Due entry of `note`) the written date of its task, not a later occurrence of its
/// series?
bool isWrittenDue(const ParsedNote *note, const CalendarItem &item)
{
    if (!item.recurring || !note)
        return true;
    const auto it = std::ranges::find(note->tasks, item.line, &Task::line);
    return it == note->tasks.cend() || (it->due && it->due->date == item.date);
}

/// An open task (todo, doing) whose due date passed.
bool isOverdue(const Task &t, QDate today)
{
    return (t.status == TaskStatus::Todo || t.status == TaskStatus::Doing) && t.due && t.due->date < today;
}

/// An entry map without a time (minutes < 0).
bool isAllDay(const QVariant &entry)
{
    return entry.toMap().value(u"minutes"_s).toInt() < 0;
}

/// The kinds of a day's open entries for the month grid's marks on phones, the most urgent first:
/// overdue, reminder (an alarm too: one bell, four marks do not fit a phone's cell), due,
/// scheduled. No journal and no done entries: a mark only for what still asks for attention.
QStringList openKinds(const QStringList &kinds)
{
    QStringList out;
    if (kinds.contains(u"overdue"_s))
        out.append(u"overdue"_s);
    if (kinds.contains(u"reminder"_s) || kinds.contains(u"alarm"_s))
        out.append(u"reminder"_s);
    if (kinds.contains(u"due"_s))
        out.append(u"due"_s);
    if (kinds.contains(u"scheduled"_s))
        out.append(u"scheduled"_s);
    return out;
}

} // namespace

CalendarModel::CalendarModel(QObject *parent) : QAbstractListModel(parent)
{
    m_refreshTimer.setSingleShot(true);
    m_refreshTimer.setInterval(120ms);
    connect(&m_refreshTimer, &QTimer::timeout, this, [this] { rebuild(false); });
    m_midnightTimer.setSingleShot(true);
    connect(&m_midnightTimer, &QTimer::timeout, this, [this] {
        if (!m_today.isValid()) { // following the system clock, as currentDay() does
            Q_EMIT todayChanged();
            if (!m_anchor.isValid())
                Q_EMIT anchorDateChanged();
            rebuild(true);
        }
        scheduleMidnight();
    });
    scheduleMidnight();
    rebuild(true);
}

CalendarModel::~CalendarModel() = default;

void CalendarModel::setCaches(std::shared_ptr<CalendarCache> calendar, std::shared_ptr<OverdueCache> overdue)
{
    m_calendar = std::move(calendar);
    m_overdueTasks = std::move(overdue);
}

OverdueCache::Found OverdueCache::tasks(const VaultIndex &index, QDate today, int max)
{
    m_last = m_notes.sync(
            index, today.toString(Qt::ISODate), false,
            [today](const VaultPath &path, const ParsedNote &note) {
                QList<Overdue> out;
                for (const Task &t : note.tasks) { // the blocks' tasks, in document order
                    if (isOverdue(t, today))
                        out.append(Overdue{*t.due, path, t});
                }
                return out;
            },
            [](const QList<Overdue> &list) { return !list.isEmpty(); },
            [&](std::map<VaultPath, QList<Overdue>> &values) {
                // Everything: from the index's task list (notes by path, document order).
                const QList<TaskRef> all = index.tasks();
                for (qsizetype i = 0; i < all.size();) {
                    const VaultPath &path = all.at(i).path;
                    QList<Overdue> out;
                    for (; i < all.size() && all.at(i).path == path; ++i) {
                        const Task &t = all.at(i).task;
                        if (isOverdue(t, today))
                            out.append(Overdue{*t.due, path, t});
                    }
                    if (!out.isEmpty())
                        values.emplace_hint(values.end(), path, std::move(out));
                }
            });
    // Notes by path, tasks in document order, then stably by the due date (time included).
    QList<const Overdue *> all;
    for (const auto &[path, list] : m_notes.values()) {
        for (const Overdue &o : list)
            all.append(&o);
    }
    std::ranges::stable_sort(
            all, [](const Overdue *a, const Overdue *b) { return dates::compareDateSpecs(a->due, b->due) < 0; });
    Found found;
    found.total = int(all.size());
    for (qsizetype i = 0; i < all.size() && i < max; ++i)
        found.tasks.append(*all.at(i));
    return found;
}

void CalendarModel::classBegin()
{
    m_completing = true;
    // Nothing is bound to the model yet: no rows until the component is complete, so views
    // created with it do not make delegates for the days of the constructor first.
    beginResetModel();
    m_days.clear();
    m_rowOf.clear();
    endResetModel();
    m_rebuildPending = true;
    m_resetPending = true;
}

void CalendarModel::componentComplete()
{
    m_completing = false;
    if (m_rebuildPending)
        rebuild(m_resetPending);
}

void CalendarModel::requestRebuild(bool forceReset)
{
    if (m_completing) {
        m_rebuildPending = true;
        m_resetPending = m_resetPending || forceReset;
        return;
    }
    rebuild(forceReset);
}

QVariantList openEntriesFirst(const QVariantList &items)
{
    QVariantList open;
    QVariantList closed;
    for (const QVariant &entry : items)
        (entry.toMap().value(u"closed"_s).toBool() ? closed : open).append(entry);
    return open + closed;
}

QVariantList allDayEntries(const QVariantList &items)
{
    QVariantList out;
    std::ranges::copy_if(items, std::back_inserter(out), isAllDay);
    return out;
}

QVariantList timedLanes(const QVariantList &items)
{
    QVariantList out;
    QVariantList group;
    int groupStart = 0;
    const auto flush = [&] {
        for (qsizetype k = 0; k < group.size(); ++k)
            out.append(QVariantMap{{u"entry"_s, group.at(k)}, {u"lane"_s, int(k)}, {u"lanes"_s, int(group.size())}});
        group.clear();
    };
    for (const QVariant &entry : items) {
        const int minutes = entry.toMap().value(u"minutes"_s).toInt();
        if (minutes < 0)
            continue;
        if (!group.isEmpty() && minutes - groupStart >= 45)
            flush();
        if (group.isEmpty())
            groupStart = minutes;
        group.append(entry);
    }
    flush();
    return out;
}

int CalendarModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : int(m_days.size());
}

QVariant CalendarModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_days.size())
        return {};
    const Day &day = m_days.at(index.row());
    const QDate date = day.date;
    switch (role) {
    case Qt::DisplayRole:
    case DateRole: return isoDate(date);
    case DayRole: return date.day();
    case MonthRole: return date.month();
    case YearRole: return date.year();
    case DayOfWeekRole: return date.dayOfWeek();
    case InMonthRole: return m_mode != u"month" || date.month() == anchor().month();
    case IsTodayRole: return date == currentDay();
    case IsPastRole: return date < currentDay();
    case IsWeekendRole: return date.dayOfWeek() >= 6;
    case IsSelectedRole: return date == anchor();
    case WeekNumberRole: return date.weekNumber();
    case JournalPathRole: return day.journal;
    case HasJournalRole: return !day.journal.isEmpty();
    case ItemsRole: return day.items;
    case ItemCountRole: return int(day.items.size());
    case TimedCountRole: return day.timed;
    case KindsRole: return day.kinds;
    case OpenKindsRole: return openKinds(day.kinds);
    case LabelRole: return dates::formatDateShort(date, uiLocale(m_locale));
    case LongLabelRole: return longLabel(date, uiLocale(m_locale));
    case OrderedItemsRole: return openEntriesFirst(day.items);
    case AllDayRole: return allDayEntries(day.items);
    case TimedRole: return timedLanes(day.items);
    default: return {};
    }
}

QHash<int, QByteArray> CalendarModel::roleNames() const
{
    static const QHash<int, QByteArray> names{
            {DateRole, "date"},
            {DayRole, "day"},
            {MonthRole, "month"},
            {YearRole, "year"},
            {DayOfWeekRole, "dayOfWeek"},
            {InMonthRole, "inMonth"},
            {IsTodayRole, "isToday"},
            {IsPastRole, "isPast"},
            {IsWeekendRole, "isWeekend"},
            {IsSelectedRole, "isSelected"},
            {WeekNumberRole, "weekNumber"},
            {JournalPathRole, "journalPath"},
            {HasJournalRole, "hasJournal"},
            {ItemsRole, "items"},
            {ItemCountRole, "itemCount"},
            {TimedCountRole, "timedCount"},
            {KindsRole, "kinds"},
            {OpenKindsRole, "openKinds"},
            {LabelRole, "label"},
            {LongLabelRole, "longLabel"},
            {OrderedItemsRole, "orderedItems"},
            {AllDayRole, "allDay"},
            {TimedRole, "timed"},
    };
    return names;
}

// ---- properties ----------------------------------------------------------------------------

VaultIndex *CalendarModel::vaultIndex() const
{
    return m_index.data();
}

void CalendarModel::setVaultIndex(VaultIndex *index)
{
    if (m_index == index)
        return;
    disconnect(m_indexConnection);
    m_index = index;
    if (index)
        m_indexConnection = connect(index, &VaultIndex::changed, this, &CalendarModel::scheduleRefresh);
    Q_EMIT vaultIndexChanged();
    requestRebuild(true);
}

QObject *CalendarModel::vaultIndexObject() const
{
    return m_index.data();
}

void CalendarModel::setVaultIndexObject(QObject *index)
{
    setVaultIndex(qobject_cast<VaultIndex *>(index));
}

void CalendarModel::setMode(const QString &mode)
{
    const QString m = (mode == u"week" || mode == u"agenda") ? mode : u"month"_s;
    if (m_mode == m)
        return;
    m_mode = m;
    Q_EMIT modeChanged();
    requestRebuild(true);
}

QString CalendarModel::anchorDate() const
{
    return isoDate(anchor());
}

void CalendarModel::setAnchorDate(const QString &date)
{
    const QDate picked = parseIsoDate(date);
    if (!picked.isValid() || picked == anchor()) {
        if (picked.isValid() && !m_anchor.isValid())
            m_anchor = picked; // pin it (today might change later)
        return;
    }
    const QDate old = anchor();
    m_anchor = picked;
    Q_EMIT anchorDateChanged();
    const auto [from, to] = rangeFor(picked);
    if (from == m_rangeStart && to == m_rangeEnd) {
        // Same range: only the selection moves (and inMonth stays — same month in month mode).
        for (const QDate &x : {old, picked}) {
            const int row = m_rowOf.value(x, -1);
            if (row >= 0)
                Q_EMIT dataChanged(createIndex(row, 0), createIndex(row, 0), {IsSelectedRole});
        }
        Q_EMIT rangeChanged(); // showingToday, title stay correct
        return;
    }
    requestRebuild(true);
}

void CalendarModel::setToday(const QString &today)
{
    if (m_todayText == today)
        return;
    const QDate oldAnchor = anchor();
    m_todayText = today;
    m_today = parseIsoDate(today);
    Q_EMIT todayChanged();
    if (anchor() != oldAnchor)
        Q_EMIT anchorDateChanged(); // an unpinned anchor follows today
    requestRebuild(true);
}

void CalendarModel::setWeekStart(int weekStart)
{
    const int w = weekStart == 0 ? 0 : int(dates::sanitizeWeekStart(weekStart));
    if (m_weekStart == w)
        return;
    m_weekStart = w;
    Q_EMIT weekStartChanged();
    requestRebuild(true);
}

void CalendarModel::setLocale(const QString &locale)
{
    if (m_locale == locale)
        return;
    m_locale = locale;
    Q_EMIT localeChanged();
    requestRebuild(true);
}

void CalendarModel::setAgendaDays(int days)
{
    days = std::clamp(days, 1, 366);
    if (m_agendaDays == days)
        return;
    m_agendaDays = days;
    Q_EMIT agendaDaysChanged();
    if (m_mode == u"agenda")
        requestRebuild(true);
}

void CalendarModel::setRefreshDelay(int msec)
{
    msec = std::max(0, msec);
    if (m_refreshTimer.interval() == msec)
        return;
    m_refreshTimer.setInterval(msec);
    Q_EMIT refreshDelayChanged();
}

void CalendarModel::setClock(std::function<QDate()> clock)
{
    m_clock = std::move(clock);
    if (!m_anchor.isValid())
        Q_EMIT anchorDateChanged();
    rebuild(true);
}

QString CalendarModel::rangeStart() const
{
    return isoDate(m_rangeStart);
}

QString CalendarModel::rangeEnd() const
{
    return isoDate(m_rangeEnd);
}

QString CalendarModel::title() const
{
    const dates::UiLocale loc = uiLocale(m_locale);
    if (m_mode == u"month") {
        const QDate a = anchor();
        return capitalized(dates::monthName(a.month(), loc, dates::NameForm::Nominative)) + u' '
               + QString::number(a.year());
    }
    return rangeTitle(m_rangeStart, m_rangeEnd, loc);
}

QString CalendarModel::shortTitle() const
{
    if (m_mode == u"month")
        return title();
    const dates::UiLocale loc = uiLocale(m_locale);
    const QString days = dayMonthRange(m_rangeStart, m_rangeEnd, loc, dates::NameForm::Short);
    const int year = currentDay().year();
    if (m_rangeStart.year() == year && m_rangeEnd.year() == year)
        return days;
    return withYear(days, m_rangeEnd.year(), loc);
}

// ---- navigation ----------------------------------------------------------------------------

void CalendarModel::next()
{
    setAnchorDate(isoDate(stepped(1)));
}

void CalendarModel::previous()
{
    setAnchorDate(isoDate(stepped(-1)));
}

QDate CalendarModel::stepped(int periods) const
{
    const QDate a = anchor();
    if (m_mode == u"month")
        return a.addMonths(periods);
    if (m_mode == u"week")
        return a.addDays(7 * periods);
    // The agenda: from the range's first week, not from the anchor: the anchor may be in any of its
    // weeks (a day picked in the list), and the next range must start right after this one.
    const QDate start = m_rangeStart.isValid() && a >= m_rangeStart && a <= m_rangeEnd
                                ? m_rangeStart
                                : dates::startOfWeek(a, weekStartDay());
    return start.addDays(periods * m_agendaDays + int(start.daysTo(a) % 7));
}

void CalendarModel::goToday()
{
    setAnchorDate(isoDate(currentDay()));
}

void CalendarModel::moveAnchor(int days)
{
    const QDate target = anchor().addDays(days);
    if (dates::isSupportedDate(target))
        setAnchorDate(isoDate(target));
}

// ---- queries -------------------------------------------------------------------------------

QVariantList CalendarModel::dayItems(const QString &date) const
{
    const QDate on = parseIsoDate(date);
    QVariantList out;
    if (!on.isValid() || !m_index)
        return out;
    const QDate today = currentDay();
    const QList<CalendarItem> items = calendarItems(*m_index, on, on);
    for (const CalendarItem &item : items) {
        if (item.kind == CalendarItem::Journal) {
            out.prepend(QVariantMap{
                    {u"key"_s, QString(u"journal:"_s + item.path)},
                    {u"kind"_s, u"journal"_s},
                    {u"variant"_s, u"neutral"_s},
                    {u"icon"_s, u"book-open"_s},
                    {u"title"_s, item.title},
                    {u"path"_s, item.path},
                    {u"line"_s, 0},
                    {u"time"_s, QString()},
                    {u"minutes"_s, -1},
                    {u"isTask"_s, false},
            });
            continue;
        }
        out.append(itemMap(item, today));
    }
    return out;
}

QVariantList CalendarModel::dayEntries(const QString &date) const
{
    QVariantList entries = dayItems(date);
    entries.removeIf([](const QVariant &entry) { return entry.toMap().value(u"kind"_s).toString() == u"journal"; });
    return openEntriesFirst(entries);
}

bool CalendarModel::dayHasJournal(const QString &date) const
{
    const QDate on = parseIsoDate(date);
    if (!on.isValid() || !m_index)
        return false;
    const QList<CalendarItem> items = calendarItems(*m_index, on, on);
    return std::ranges::any_of(items, [](const CalendarItem &item) { return item.kind == CalendarItem::Journal; });
}

QVariantMap CalendarModel::marks(const QString &from, const QString &to) const
{
    QVariantMap out;
    const QDate a = parseIsoDate(from);
    QDate b = parseIsoDate(to);
    if (!a.isValid() || !b.isValid() || a > b || !m_index)
        return out;
    if (a.daysTo(b) > 400)
        b = a.addDays(400);
    const QDate today = currentDay();
    QHash<QDate, QStringList> kinds;
    const QList<CalendarItem> items = calendarItems(*m_index, a, b);
    for (const CalendarItem &item : items) {
        QString k;
        switch (item.kind) {
        case CalendarItem::Journal: k = u"journal"_s; break;
        case CalendarItem::Due:
            k = (item.date < today && item.status && !isClosed(*item.status)) ? u"overdue"_s : u"due"_s;
            break;
        case CalendarItem::Scheduled:
        case CalendarItem::Start: k = u"scheduled"_s; break;
        case CalendarItem::Reminder: k = item.alarm ? u"alarm"_s : u"reminder"_s; break;
        case CalendarItem::Done: k = u"done"_s; break;
        }
        QStringList &list = kinds[item.date];
        if (!list.contains(k))
            list.append(k);
    }
    for (const auto [date, list] : std::as_const(kinds).asKeyValueRange())
        out.insert(isoDate(date), list);
    return out;
}

int CalendarModel::rowForDate(const QString &date) const
{
    return m_rowOf.value(parseIsoDate(date), -1);
}

void CalendarModel::refresh()
{
    m_refreshTimer.stop();
    rebuild(false);
}

// ---- internals -----------------------------------------------------------------------------

QDate CalendarModel::currentDay() const
{
    if (m_today.isValid())
        return m_today;
    return m_clock ? m_clock() : QDate::currentDate();
}

QDate CalendarModel::anchor() const
{
    return m_anchor.isValid() ? m_anchor : currentDay();
}

Qt::DayOfWeek CalendarModel::weekStartDay() const
{
    if (m_weekStart > 0)
        return Qt::DayOfWeek(m_weekStart);
    return m_index ? m_index->config().weekStartsOn : Qt::Monday;
}

std::pair<QDate, QDate> CalendarModel::rangeFor(QDate a) const
{
    const Qt::DayOfWeek ws = weekStartDay();
    if (m_mode == u"week") {
        const QDate s = dates::startOfWeek(a, ws);
        return {s, s.addDays(6)};
    }
    if (m_mode == u"agenda") {
        // The days shown stay while a chosen anchor is among them (a day picked in the list),
        // unless the weeks now start on another day. An anchor that follows today still starts
        // them at today's week (a new week after midnight).
        if (m_anchor.isValid() && m_agendaStart.isValid() && a >= m_agendaStart
            && a < m_agendaStart.addDays(m_agendaDays) && dates::startOfWeek(m_agendaStart, ws) == m_agendaStart)
            return {m_agendaStart, m_agendaStart.addDays(m_agendaDays - 1)};
        const QDate s = dates::startOfWeek(a, ws);
        return {s, s.addDays(m_agendaDays - 1)};
    }
    const QDate s = dates::startOfWeek(dates::startOfMonth(a), ws);
    return {s, s.addDays(41)};
}

void CalendarModel::scheduleRefresh()
{
    if (m_refreshTimer.interval() == 0)
        rebuild(false);
    else
        m_refreshTimer.start();
}

QVariantMap CalendarModel::itemMap(const CalendarItem &item, QDate today) const
{
    const bool isTask = item.kind != CalendarItem::Reminder || item.status.has_value();
    const bool closed = item.status && isClosed(*item.status);
    const ParsedNote *note = m_index ? m_index->note(item.path) : nullptr;
    const bool overdue =
            item.kind == CalendarItem::Due && !closed && item.status && item.date < today && isWrittenDue(note, item);
    QString variant;
    switch (item.kind) {
    case CalendarItem::Journal: variant = u"neutral"_s; break;
    case CalendarItem::Due: variant = closed ? u"done"_s : overdue ? u"overdue"_s : u"due"_s; break;
    case CalendarItem::Scheduled: variant = closed ? u"done"_s : u"scheduled"_s; break;
    case CalendarItem::Start: variant = closed ? u"done"_s : u"neutral"_s; break;
    case CalendarItem::Reminder: variant = closed ? u"done"_s : item.alarm ? u"alarm"_s : u"reminder"_s; break;
    case CalendarItem::Done: variant = u"done"_s; break;
    }
    const QString time = clockTime(item.time);
    return QVariantMap{
            {u"key"_s, u"%1:%2:%3:%4"_s.arg(kindName(item.kind), item.path, QString::number(item.line), time)},
            {u"kind"_s, kindName(item.kind)},
            {u"variant"_s, variant},
            {u"icon"_s, iconFor(variant, item.kind)},
            {u"time"_s, time},
            {u"minutes"_s, item.time ? item.time->hour() * 60 + item.time->minute() : -1},
            {u"title"_s, plainText(item.title)},
            {u"path"_s, item.path},
            {u"line"_s, item.line},
            {u"pageTitle"_s, note ? note->title : QString()},
            {u"isJournalPage"_s, note && note->journalDate.has_value()},
            {u"isTask"_s, isTask},
            {u"status"_s, item.status ? statusName(*item.status) : QString()},
            {u"closed"_s, closed},
            {u"overdue"_s, overdue},
            {u"recurring"_s, item.recurring},
            {u"alarm"_s, item.alarm},
            {u"date"_s, isoDate(item.date)},
            // The token as written (its menu), and whether this is another date of its series.
            {u"spec"_s, item.written ? item.written->toString() : QString()},
            {u"occurrence"_s, item.written && (item.written->date != item.date || item.written->time != item.time)},
            {u"rule"_s, item.rule},
            {u"priority"_s, priorityName(item.priority)},
    };
}

void CalendarModel::rebuild(bool forceReset)
{
    m_rebuildPending = false;
    m_resetPending = false;
    const QDate today = currentDay();
    const auto [from, to] = rangeFor(anchor());
    const bool rangeMoved = from != m_rangeStart || to != m_rangeEnd;

    QHash<QDate, Day> byDate;
    int itemCount = 0;
    int firstHour = 24;
    int lastHour = -1;
    if (m_index) {
        if (!m_calendar)
            m_calendar = std::make_shared<CalendarCache>();
        const QList<CalendarItem> items = m_calendar->items(*m_index, from, to);
        for (const CalendarItem &item : items) {
            Day &day = byDate[item.date];
            day.date = item.date;
            if (item.kind == CalendarItem::Journal) {
                if (day.journal.isEmpty())
                    day.journal = item.path;
                if (!day.kinds.contains(u"journal"_s))
                    day.kinds.append(u"journal"_s);
                continue;
            }
            const QVariantMap map = itemMap(item, today);
            day.items.append(map);
            ++itemCount;
            if (item.time) {
                ++day.timed;
                firstHour = std::min(firstHour, item.time->hour());
                lastHour = std::max(lastHour, item.time->hour());
            }
            QString mark = map.value(u"variant"_s).toString();
            if (mark == u"neutral")
                mark = u"scheduled"_s;
            if (!day.kinds.contains(mark))
                day.kinds.append(mark);
        }
    }

    QList<Day> days;
    for (QDate date = from; date.isValid() && date <= to; date = date.addDays(1)) {
        Day day = byDate.take(date);
        day.date = date;
        if (m_mode == u"agenda" && day.items.isEmpty() && day.journal.isEmpty() && date != today)
            continue;
        days.append(std::move(day));
    }

    // Overdue: open tasks whose (written) due date passed, oldest first (then by page and line) —
    // kept per note across index changes.
    QVariantList overdue;
    int overdueCount = 0;
    if (m_index) {
        if (!m_overdueTasks)
            m_overdueTasks = std::make_shared<OverdueCache>();
        const auto [found, total] = m_overdueTasks->tasks(*m_index, today, MaxOverdue);
        overdueCount = total;
        const dates::UiLocale loc = uiLocale(m_locale);
        for (const OverdueCache::Overdue &item : found) {
            const Task &task = item.task;
            const DateSpec &due = item.due;
            const ParsedNote *note = m_index->note(item.path);
            overdue.append(QVariantMap{
                    {u"key"_s, u"overdue:%1:%2"_s.arg(item.path, QString::number(task.line))},
                    {u"kind"_s, u"due"_s},
                    {u"variant"_s, u"overdue"_s},
                    {u"icon"_s, u"calendar"_s},
                    {u"title"_s, plainText(task.text)},
                    {u"path"_s, item.path},
                    {u"line"_s, task.line},
                    {u"pageTitle"_s, note ? note->title : QString()},
                    {u"isJournalPage"_s, note && note->journalDate.has_value()},
                    {u"due"_s, due.toString()},
                    {u"dueLabel"_s, humanDate(due, loc, today)},
                    {u"time"_s, clockTime(due.time)},
                    {u"minutes"_s, -1},
                    {u"isTask"_s, true},
                    {u"status"_s, statusName(task.status)},
                    {u"closed"_s, false},
                    {u"overdue"_s, true},
                    {u"recurring"_s, !task.recurrence.isEmpty()},
                    {u"alarm"_s, false},
                    {u"date"_s, isoDate(due.date)},
                    {u"spec"_s, due.toString()},
                    {u"occurrence"_s, false},
                    {u"rule"_s, task.recurrence},
                    {u"priority"_s, priorityName(task.priority)},
            });
        }
    }

    const bool sameRows = !forceReset && !rangeMoved
                          && std::ranges::equal(std::as_const(days), std::as_const(m_days), {}, &Day::date, &Day::date);
    if (sameRows) {
        m_days = std::move(days);
        if (!m_days.isEmpty())
            Q_EMIT dataChanged(createIndex(0, 0), createIndex(int(m_days.size()) - 1, 0));
    } else {
        beginResetModel();
        m_days = std::move(days);
        endResetModel();
    }
    m_rowOf.clear();
    for (qsizetype i = 0; i < m_days.size(); ++i)
        m_rowOf.insert(m_days.at(i).date, int(i));
    m_rangeStart = from;
    m_rangeEnd = to;
    m_agendaStart = m_mode == u"agenda" ? from : QDate();
    m_itemCount = itemCount;
    m_overdue = std::move(overdue);
    m_overdueCount = overdueCount;
    m_firstHour = firstHour <= lastHour ? firstHour : 8;
    m_lastHour = firstHour <= lastHour ? lastHour : 18;
    m_maxAllDay = 0;
    for (const Day &day : std::as_const(m_days))
        m_maxAllDay = std::max(m_maxAllDay, int(std::ranges::count_if(day.items, isAllDay)));
    ++m_revision;
    Q_EMIT rangeChanged();
    Q_EMIT itemsChanged();
}

void CalendarModel::scheduleMidnight()
{
    const QDateTime now = QDateTime::currentDateTime();
    const QDateTime midnight(now.date().addDays(1), QTime(0, 0, 5));
    const qint64 ms = std::clamp<qint64>(now.msecsTo(midnight), 1000, 24LL * 3600 * 1000);
    m_midnightTimer.start(int(ms));
}

} // namespace brulion::planning
