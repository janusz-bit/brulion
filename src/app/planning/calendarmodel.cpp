#include "calendarmodel.h"

#include "dates/format.h"
#include "dates/local.h"
#include "dates/names.h"
#include "index/vaultindex.h"
#include "planningutil.h"

#include <QDateTime>
#include <QSet>

#include <algorithm>

namespace brulion::planning {

using namespace Qt::StringLiterals;

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

/// "15 października 2026" / "October 15, 2026" parts of a range title.
QString rangeTitle(QDate a, QDate b, dates::UiLocale locale)
{
    using dates::NameForm;
    const bool en = locale == dates::UiLocale::English;
    const auto month = [&](QDate d) {
        return dates::monthName(d.month(), locale, en ? NameForm::Nominative : NameForm::Genitive);
    };
    const QString dash = u"–"_s;
    if (a.year() == b.year() && a.month() == b.month()) {
        if (a == b)
            return en ? u"%1 %2, %3"_s.arg(month(a)).arg(a.day()).arg(a.year())
                      : u"%1 %2 %3"_s.arg(a.day()).arg(month(a)).arg(a.year());
        return en ? u"%1 %2%3%4, %5"_s.arg(month(a)).arg(a.day()).arg(dash).arg(b.day()).arg(a.year())
                  : u"%1%2%3 %4 %5"_s.arg(a.day()).arg(dash).arg(b.day()).arg(month(a)).arg(a.year());
    }
    if (a.year() == b.year()) {
        return en ? u"%1 %2 %3 %4 %5, %6"_s.arg(month(a)).arg(a.day()).arg(dash, month(b)).arg(b.day()).arg(a.year())
                  : u"%1 %2 %3 %4 %5 %6"_s.arg(a.day()).arg(month(a), dash).arg(b.day()).arg(month(b)).arg(a.year());
    }
    return en ? u"%1 %2, %3 %4 %5 %6, %7"_s.arg(month(a)).arg(a.day()).arg(a.year()).arg(dash, month(b)).arg(b.day()).arg(b.year())
              : u"%1 %2 %3 %4 %5 %6 %7"_s.arg(a.day()).arg(month(a)).arg(a.year()).arg(dash).arg(b.day()).arg(month(b)).arg(b.year());
}

QString longLabel(QDate d, dates::UiLocale locale)
{
    using dates::NameForm;
    if (locale == dates::UiLocale::English)
        return u"%1, %2 %3"_s.arg(dates::weekdayName(d.dayOfWeek(), locale), dates::monthName(d.month(), locale)).arg(d.day());
    return u"%1, %2 %3"_s.arg(dates::weekdayName(d.dayOfWeek(), locale)).arg(d.day()).arg(
            dates::monthName(d.month(), locale, NameForm::Genitive));
}

/// Is `item` (a Due entry) the written date of its task, not a later occurrence of its series?
bool isWrittenDue(const VaultIndex *index, const CalendarItem &item)
{
    if (!item.recurring || !index)
        return true;
    const ParsedNote *note = index->note(item.path);
    if (!note)
        return true;
    for (const Task &t : note->tasks) {
        if (t.line == item.line)
            return t.due && t.due->date == item.date;
    }
    return true;
}

} // namespace

CalendarModel::CalendarModel(QObject *parent) : QAbstractListModel(parent)
{
    m_refreshTimer.setSingleShot(true);
    m_refreshTimer.setInterval(120);
    connect(&m_refreshTimer, &QTimer::timeout, this, [this] { rebuild(false); });
    m_midnightTimer.setSingleShot(true);
    connect(&m_midnightTimer, &QTimer::timeout, this, [this] {
        if (m_todayText.isEmpty()) {
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

int CalendarModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : int(m_days.size());
}

QVariant CalendarModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_days.size())
        return {};
    return dayData(m_days.at(index.row()), role);
}

QVariant CalendarModel::dayData(const Day &day, int role) const
{
    const QDate d = day.date;
    const QDate today = currentDay();
    const dates::UiLocale loc = uiLocale(m_locale);
    switch (role) {
    case Qt::DisplayRole:
    case DateRole: return isoDate(d);
    case DayRole: return d.day();
    case MonthRole: return d.month();
    case YearRole: return d.year();
    case DayOfWeekRole: return d.dayOfWeek();
    case InMonthRole: return m_mode != u"month" || d.month() == anchor().month();
    case IsTodayRole: return d == today;
    case IsPastRole: return d < today;
    case IsWeekendRole: return d.dayOfWeek() >= 6;
    case IsSelectedRole: return d == anchor();
    case WeekNumberRole: return d.weekNumber();
    case JournalPathRole: return day.journal;
    case HasJournalRole: return !day.journal.isEmpty();
    case ItemsRole: return day.items;
    case ItemCountRole: return int(day.items.size());
    case TimedCountRole: return day.timed;
    case KindsRole: return day.kinds;
    case LabelRole: return dates::formatDateShort(d, loc);
    case LongLabelRole: return longLabel(d, loc);
    default: return {};
    }
}

QHash<int, QByteArray> CalendarModel::roleNames() const
{
    return {
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
            {LabelRole, "label"},
            {LongLabelRole, "longLabel"},
    };
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
    if (m_indexConnection)
        disconnect(m_indexConnection);
    m_index = index;
    if (index)
        m_indexConnection = connect(index, &VaultIndex::changed, this, &CalendarModel::scheduleRefresh);
    Q_EMIT vaultIndexChanged();
    rebuild(true);
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
    rebuild(true);
}

QString CalendarModel::anchorDate() const
{
    return isoDate(anchor());
}

void CalendarModel::setAnchorDate(const QString &date)
{
    const QDate d = parseIsoDate(date);
    if (!d.isValid() || d == anchor()) {
        if (d.isValid() && !m_anchor.isValid())
            m_anchor = d; // pin it (today might change later)
        return;
    }
    const QDate old = anchor();
    m_anchor = d;
    Q_EMIT anchorDateChanged();
    const auto [from, to] = rangeFor(d);
    if (from == m_rangeStart && to == m_rangeEnd) {
        // Same range: only the selection moves (and inMonth stays — same month in month mode).
        for (const QDate &x : {old, d}) {
            const int row = m_rowOf.value(x, -1);
            if (row >= 0)
                Q_EMIT dataChanged(createIndex(row, 0), createIndex(row, 0), {IsSelectedRole});
        }
        Q_EMIT rangeChanged(); // showingToday, title stay correct
        return;
    }
    rebuild(true);
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
    rebuild(true);
}

void CalendarModel::setWeekStart(int weekStart)
{
    const int w = weekStart == 0 ? 0 : int(dates::sanitizeWeekStart(weekStart));
    if (m_weekStart == w)
        return;
    m_weekStart = w;
    Q_EMIT weekStartChanged();
    rebuild(true);
}

void CalendarModel::setLocale(const QString &locale)
{
    if (m_locale == locale)
        return;
    m_locale = locale;
    Q_EMIT localeChanged();
    rebuild(true);
}

void CalendarModel::setAgendaDays(int days)
{
    days = std::clamp(days, 1, 366);
    if (m_agendaDays == days)
        return;
    m_agendaDays = days;
    Q_EMIT agendaDaysChanged();
    if (m_mode == u"agenda")
        rebuild(true);
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
    const bool en = loc == dates::UiLocale::English;
    const QDate a = m_rangeStart;
    const QDate b = m_rangeEnd;
    const auto month = [loc](QDate d) { return dates::monthName(d.month(), loc, dates::NameForm::Short); };
    const QString dash = u"–"_s;
    QString out;
    if (a.month() == b.month() && a.year() == b.year())
        out = en ? u"%1 %2%3%4"_s.arg(month(a)).arg(a.day()).arg(dash).arg(b.day())
                 : u"%1%2%3 %4"_s.arg(a.day()).arg(dash).arg(b.day()).arg(month(a));
    else
        out = en ? u"%1 %2 %3 %4 %5"_s.arg(month(a)).arg(a.day()).arg(dash, month(b)).arg(b.day())
                 : u"%1 %2 %3 %4 %5"_s.arg(a.day()).arg(month(a), dash).arg(b.day()).arg(month(b));
    if (a.year() != currentDay().year() || b.year() != currentDay().year())
        out += (en ? u", "_s : u" "_s) + QString::number(b.year());
    return out;
}

// ---- navigation ----------------------------------------------------------------------------

void CalendarModel::next()
{
    const QDate a = anchor();
    if (m_mode == u"month")
        setAnchorDate(isoDate(a.addMonths(1)));
    else if (m_mode == u"week")
        setAnchorDate(isoDate(a.addDays(7)));
    else
        setAnchorDate(isoDate(a.addDays(m_agendaDays)));
}

void CalendarModel::previous()
{
    const QDate a = anchor();
    if (m_mode == u"month")
        setAnchorDate(isoDate(a.addMonths(-1)));
    else if (m_mode == u"week")
        setAnchorDate(isoDate(a.addDays(-7)));
    else
        setAnchorDate(isoDate(a.addDays(-m_agendaDays)));
}

void CalendarModel::goToday()
{
    setAnchorDate(isoDate(currentDay()));
}

void CalendarModel::moveAnchor(int days)
{
    const QDate d = anchor().addDays(days);
    if (dates::isSupportedDate(d))
        setAnchorDate(isoDate(d));
}

// ---- queries -------------------------------------------------------------------------------

QVariantList CalendarModel::dayItems(const QString &date) const
{
    const QDate d = parseIsoDate(date);
    QVariantList out;
    if (!d.isValid() || !m_index)
        return out;
    const QDate today = currentDay();
    const QList<CalendarItem> items = calendarItems(*m_index, d, d);
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

QVariantMap CalendarModel::marks(const QString &from, const QString &to) const
{
    QVariantMap out;
    QDate a = parseIsoDate(from);
    QDate b = parseIsoDate(to);
    if (!a.isValid() || !b.isValid() || a > b || !m_index)
        return out;
    if (a.daysTo(b) > 400)
        b = a.addDays(400);
    const QDate today = currentDay();
    QHash<QDate, QStringList> kinds;
    for (const CalendarItem &item : calendarItems(*m_index, a, b)) {
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
    for (auto it = kinds.cbegin(); it != kinds.cend(); ++it)
        out.insert(isoDate(it.key()), it.value());
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
    const bool overdue = item.kind == CalendarItem::Due && !closed && item.status && item.date < today
                         && isWrittenDue(m_index.data(), item);
    QString variant;
    switch (item.kind) {
    case CalendarItem::Journal: variant = u"neutral"_s; break;
    case CalendarItem::Due: variant = closed ? u"done"_s : overdue ? u"overdue"_s : u"due"_s; break;
    case CalendarItem::Scheduled: variant = closed ? u"done"_s : u"scheduled"_s; break;
    case CalendarItem::Start: variant = closed ? u"done"_s : u"neutral"_s; break;
    case CalendarItem::Reminder: variant = closed ? u"done"_s : item.alarm ? u"alarm"_s : u"reminder"_s; break;
    case CalendarItem::Done: variant = u"done"_s; break;
    }
    const ParsedNote *note = m_index ? m_index->note(item.path) : nullptr;
    const QString time = clockTime(item.time);
    return QVariantMap{
            {u"key"_s, u"%1:%2:%3:%4"_s.arg(kindName(item.kind), item.path).arg(item.line).arg(time)},
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
    };
}

void CalendarModel::rebuild(bool forceReset)
{
    const QDate today = currentDay();
    const auto [from, to] = rangeFor(anchor());
    const bool rangeMoved = from != m_rangeStart || to != m_rangeEnd;

    QHash<QDate, Day> byDate;
    int itemCount = 0;
    int firstHour = 24;
    int lastHour = -1;
    if (m_index) {
        const QList<CalendarItem> items = calendarItems(*m_index, from, to);
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
    for (QDate d = from; d.isValid() && d <= to; d = d.addDays(1)) {
        Day day = byDate.value(d);
        day.date = d;
        if (m_mode == u"agenda" && day.items.isEmpty() && day.journal.isEmpty() && d != today)
            continue;
        days.append(day);
    }

    // Overdue: open tasks whose (written) due date passed.
    QVariantList overdue;
    int overdueCount = 0;
    if (m_index) {
        struct Entry {
            DateSpec due;
            TaskRef ref;
        };
        QList<Entry> list;
        for (const TaskRef &ref : m_index->tasks()) {
            const Task &t = ref.task;
            if ((t.status == TaskStatus::Todo || t.status == TaskStatus::Doing) && t.due && t.due->date < today)
                list.append({*t.due, ref});
        }
        std::stable_sort(list.begin(), list.end(), [](const Entry &a, const Entry &b) {
            return dates::compareDateSpecs(a.due, b.due) < 0;
        });
        overdueCount = int(list.size());
        const dates::UiLocale loc = uiLocale(m_locale);
        for (qsizetype i = 0; i < list.size() && i < MaxOverdue; ++i) {
            const Entry &e = list.at(i);
            const ParsedNote *note = m_index->note(e.ref.path);
            overdue.append(QVariantMap{
                    {u"key"_s, u"overdue:%1:%2"_s.arg(e.ref.path).arg(e.ref.task.line)},
                    {u"kind"_s, u"due"_s},
                    {u"variant"_s, u"overdue"_s},
                    {u"icon"_s, u"calendar"_s},
                    {u"title"_s, plainText(e.ref.task.text)},
                    {u"path"_s, e.ref.path},
                    {u"line"_s, e.ref.task.line},
                    {u"pageTitle"_s, note ? note->title : QString()},
                    {u"isJournalPage"_s, note && note->journalDate.has_value()},
                    {u"due"_s, e.due.toString()},
                    {u"dueLabel"_s, humanDate(e.due, loc, today)},
                    {u"time"_s, clockTime(e.due.time)},
                    {u"minutes"_s, -1},
                    {u"isTask"_s, true},
                    {u"status"_s, statusName(e.ref.task.status)},
                    {u"closed"_s, false},
                    {u"overdue"_s, true},
                    {u"recurring"_s, !e.ref.task.recurrence.isEmpty()},
                    {u"alarm"_s, false},
                    {u"date"_s, isoDate(e.due.date)},
            });
        }
    }

    bool sameRows = !forceReset && !rangeMoved && days.size() == m_days.size();
    if (sameRows) {
        for (qsizetype i = 0; i < days.size(); ++i) {
            if (days.at(i).date != m_days.at(i).date) {
                sameRows = false;
                break;
            }
        }
    }
    if (sameRows) {
        m_days = days;
        if (!m_days.isEmpty())
            Q_EMIT dataChanged(createIndex(0, 0), createIndex(int(m_days.size()) - 1, 0));
    } else {
        beginResetModel();
        m_days = days;
        endResetModel();
    }
    m_rowOf.clear();
    for (qsizetype i = 0; i < m_days.size(); ++i)
        m_rowOf.insert(m_days.at(i).date, int(i));
    m_rangeStart = from;
    m_rangeEnd = to;
    m_itemCount = itemCount;
    m_overdue = overdue;
    m_overdueCount = overdueCount;
    m_firstHour = firstHour <= lastHour ? firstHour : 8;
    m_lastHour = firstHour <= lastHour ? lastHour : 18;
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
