#include "habitlistmodel.h"

#include "dates/local.h"
#include "dates/names.h"
#include "planningutil.h"

#include <QCoreApplication>

#include <algorithm>

namespace brulion::planning {

using namespace Qt::StringLiterals;
using namespace std::chrono_literals;

namespace {

QString streakText(int n, habits::StreakUnit unit)
{
    switch (unit) {
    case habits::StreakUnit::Days: return HabitListModel::tr("%n day(s)", "habit streak", n);
    case habits::StreakUnit::Times: return HabitListModel::tr("%n time(s)", "habit streak", n);
    case habits::StreakUnit::Weeks: return HabitListModel::tr("%n week(s)", "habit streak", n);
    case habits::StreakUnit::Months: return HabitListModel::tr("%n month(s)", "habit streak", n);
    case habits::StreakUnit::Years: return HabitListModel::tr("%n year(s)", "habit streak", n);
    }
    return QString::number(n);
}

QString periodText(const habits::Schedule &s, int done, int target)
{
    if (s.interval != 1)
        return HabitListModel::tr("%1 of %2 this period").arg(done).arg(target);
    switch (s.period) {
    case habits::Schedule::Period::Week: return HabitListModel::tr("%1 of %2 this week").arg(done).arg(target);
    case habits::Schedule::Period::Month: return HabitListModel::tr("%1 of %2 this month").arg(done).arg(target);
    case habits::Schedule::Period::Year: return HabitListModel::tr("%1 of %2 this year").arg(done).arg(target);
    }
    return {};
}

} // namespace

HabitListModel::HabitListModel(QObject *parent) : QAbstractListModel(parent)
{
    m_refreshTimer.setSingleShot(true);
    m_refreshTimer.setInterval(150ms);
    connect(&m_refreshTimer, &QTimer::timeout, this, &HabitListModel::rebuild);
}

HabitListModel::~HabitListModel() = default;

void HabitListModel::classBegin()
{
    m_completing = true;
}

void HabitListModel::componentComplete()
{
    m_completing = false;
    if (m_rebuildPending)
        rebuild();
}

int HabitListModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : int(m_rows.size());
}

QVariant HabitListModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_rows.size())
        return {};
    return rowData(m_rows.at(index.row()), role);
}

QVariant HabitListModel::rowData(const Row &row, int role) const
{
    const habits::Habit &h = row.habit;
    const habits::Stats &st = row.stats;
    const bool valid = h.isValid();
    switch (role) {
    case KeyRole: return h.key;
    case Qt::DisplayRole:
    case NameRole: return h.name;
    case ScheduleRole: return h.scheduleText;
    case ScheduleLabelRole:
        return h.schedule ? habits::formatSchedule(*h.schedule, uiLocale(m_locale)) : h.scheduleText;
    case ValidRole: return valid;
    case ProblemRole: return h.duplicate ? u"duplicate"_s : !h.schedule ? u"schedule"_s : QString();
    case PathRole: return h.path;
    case LineRole: return h.line;
    case IsPageRole: return h.blockIndex < 0;
    case TodayMarkRole: return habits::markName(st.todayMark);
    case TodayRecordedRole: return st.todayRecorded;
    case DueTodayRole: return valid && st.dueToday;
    case CurrentStreakRole: return st.currentStreak;
    case LongestStreakRole: return st.longestStreak;
    case StreakUnitRole: return habits::streakUnitName(st.unit);
    case StreakLabelRole: return valid ? streakText(st.currentStreak, st.unit) : QString();
    case LongestLabelRole: return valid ? streakText(st.longestStreak, st.unit) : QString();
    case RateRole: return valid ? st.rate() : -1.0;
    case RateLabelRole: return valid && st.expected > 0 ? tr("%1%").arg(qRound(st.rate() * 100)) : QString();
    case PeriodDoneRole: return st.periodDone;
    case PeriodTargetRole: return st.periodTarget;
    case PeriodLabelRole:
        return valid && h.schedule->kind == habits::Schedule::Kind::Times
                       ? periodText(*h.schedule, st.periodDone, st.periodTarget)
                       : QString();
    case LastDoneRole: return isoDate(st.lastDone);
    case StartRole: return isoDate(st.start);
    case DaysRole: return row.days;
    default: return {};
    }
}

QHash<int, QByteArray> HabitListModel::roleNames() const
{
    static const QHash<int, QByteArray> names{
            {KeyRole, "key"},
            {NameRole, "name"},
            {ScheduleRole, "schedule"},
            {ScheduleLabelRole, "scheduleLabel"},
            {ValidRole, "valid"},
            {ProblemRole, "problem"},
            {PathRole, "path"},
            {LineRole, "line"},
            {IsPageRole, "isPage"},
            {TodayMarkRole, "todayMark"},
            {TodayRecordedRole, "todayRecorded"},
            {DueTodayRole, "dueToday"},
            {CurrentStreakRole, "currentStreak"},
            {LongestStreakRole, "longestStreak"},
            {StreakUnitRole, "streakUnit"},
            {StreakLabelRole, "streakLabel"},
            {LongestLabelRole, "longestLabel"},
            {RateRole, "rate"},
            {RateLabelRole, "rateLabel"},
            {PeriodDoneRole, "periodDone"},
            {PeriodTargetRole, "periodTarget"},
            {PeriodLabelRole, "periodLabel"},
            {LastDoneRole, "lastDone"},
            {StartRole, "start"},
            {DaysRole, "days"},
    };
    return names;
}

// ---- properties ----------------------------------------------------------------------------

void HabitListModel::setVaultIndex(VaultIndex *index)
{
    if (m_index == index)
        return;
    if (m_indexConnection)
        disconnect(m_indexConnection);
    m_index = index;
    if (index)
        m_indexConnection = connect(index, &VaultIndex::changed, this, &HabitListModel::scheduleRefresh);
    m_cache.clear();
    Q_EMIT vaultIndexChanged();
    requestRebuild(true);
}

void HabitListModel::setVaultIndexObject(QObject *index)
{
    setVaultIndex(qobject_cast<VaultIndex *>(index));
}

void HabitListModel::setToday(const QString &today)
{
    if (m_todayText == today)
        return;
    m_todayText = today;
    m_today = parseIsoDate(today);
    Q_EMIT todayChanged();
    Q_EMIT monthChanged();
    requestRebuild(true);
}

void HabitListModel::setWeekStart(int weekStart)
{
    const int w = weekStart == 0 ? 0 : int(dates::sanitizeWeekStart(weekStart));
    if (m_weekStart == w)
        return;
    m_weekStart = w;
    Q_EMIT weekStartChanged();
    Q_EMIT monthChanged();
    requestRebuild(true);
}

void HabitListModel::setLocale(const QString &locale)
{
    if (m_locale == locale)
        return;
    m_locale = locale;
    Q_EMIT localeChanged();
    Q_EMIT monthChanged();
    requestRebuild(true);
}

void HabitListModel::setMonth(const QString &month)
{
    if (m_monthText == month)
        return;
    m_monthText = month;
    m_month = QDate::fromString(month + u"-01"_s, Qt::ISODate); // invalid: today's month
    Q_EMIT monthChanged();
    requestRebuild(true);
}

QString HabitListModel::monthLabel() const
{
    const QDate m = shownMonth();
    return dates::monthName(m.month(), uiLocale(m_locale), dates::NameForm::Nominative) + u' '
           + QString::number(m.year());
}

QStringList HabitListModel::weekdayNames() const
{
    QStringList out;
    const int first = int(weekStartDay());
    for (int i = 0; i < 7; ++i)
        out.append(dates::weekdayName((first - 1 + i) % 7 + 1, uiLocale(m_locale), dates::NameForm::Short));
    return out;
}

int HabitListModel::monthOffset() const
{
    return (shownMonth().dayOfWeek() - int(weekStartDay()) + 7) % 7;
}

bool HabitListModel::currentMonth() const
{
    const QDate today = currentDay();
    return shownMonth() == QDate(today.year(), today.month(), 1);
}

QString HabitListModel::habitsPage() const
{
    const auto it = std::ranges::find_if(m_rows, [](const Row &row) { return row.habit.blockIndex >= 0; });
    return it == m_rows.cend() ? QString() : it->habit.path;
}

QStringList HabitListModel::undefinedNames() const
{
    return m_cache.undefinedKeys();
}

void HabitListModel::setRefreshDelay(int msec)
{
    msec = std::max(0, msec);
    if (m_refreshTimer.interval() == msec)
        return;
    m_refreshTimer.setInterval(msec);
    Q_EMIT refreshDelayChanged();
}

void HabitListModel::previousMonth()
{
    setMonth(shownMonth().addMonths(-1).toString(u"yyyy-MM"_s));
}

void HabitListModel::nextMonth()
{
    setMonth(shownMonth().addMonths(1).toString(u"yyyy-MM"_s));
}

void HabitListModel::thisMonth()
{
    setMonth(QString());
}

QString HabitListModel::describeSchedule(const QString &text) const
{
    const auto s = habits::parseSchedule(text);
    return s ? habits::formatSchedule(*s, uiLocale(m_locale)) : QString();
}

bool HabitListModel::hasHabit(const QString &name) const
{
    return rowOfKey(habits::habitKey(habits::habitName(name))) >= 0;
}

QVariantMap HabitListModel::get(int row) const
{
    QVariantMap out;
    if (row < 0 || row >= m_rows.size())
        return out;
    const QHash<int, QByteArray> names = roleNames();
    for (const auto &[role, name] : names.asKeyValueRange())
        out.insert(QString::fromLatin1(name), rowData(m_rows.at(row), role));
    return out;
}

int HabitListModel::rowOfKey(const QString &key) const
{
    const auto it =
            std::ranges::find_if(m_rows, [&](const Row &row) { return row.habit.key == key && !row.habit.duplicate; });
    return it == m_rows.cend() ? -1 : int(it - m_rows.cbegin());
}

void HabitListModel::refresh()
{
    m_refreshTimer.stop();
    rebuild();
}

// ---- internals -----------------------------------------------------------------------------

QDate HabitListModel::currentDay() const
{
    if (m_today.isValid())
        return m_today;
    return m_clock ? m_clock() : QDate::currentDate();
}

QDate HabitListModel::shownMonth() const
{
    if (m_month.isValid())
        return m_month;
    const QDate today = currentDay();
    return QDate(today.year(), today.month(), 1);
}

Qt::DayOfWeek HabitListModel::weekStartDay() const
{
    if (m_weekStart > 0)
        return Qt::DayOfWeek(m_weekStart);
    return m_index ? m_index->config().weekStartsOn : Qt::Monday;
}

void HabitListModel::requestRebuild(bool full)
{
    if (full)
        m_fullPending = true;
    if (m_completing)
        m_rebuildPending = true;
    else
        rebuild();
}

void HabitListModel::scheduleRefresh()
{
    if (m_refreshTimer.interval() == 0)
        rebuild();
    else
        m_refreshTimer.start();
}

void HabitListModel::compute(Row &row) const
{
    const QDate today = currentDay();
    const Qt::DayOfWeek weekStart = weekStartDay();
    const habits::DayMarks marks = m_cache.days(row.habit.key);
    row.revision = m_cache.revision(row.habit.key);
    row.stats = row.habit.isValid() ? habits::statsOf(row.habit, marks, today, weekStart) : habits::Stats();
    if (!row.habit.isValid()) {
        // Today's mark is shown for any habit with checks (its definition may come back).
        const auto it = marks.constFind(today);
        row.stats.todayRecorded = it != marks.constEnd();
        row.stats.todayMark = row.stats.todayRecorded ? *it : habits::Mark::Open;
    }
    const QDate first = shownMonth();
    const QDate last = first.addDays(first.daysInMonth() - 1);
    const QList<habits::DayState> states = row.habit.duplicate
                                                   ? QList<habits::DayState>()
                                                   : habits::daysOf(row.habit, marks, first, last, today, weekStart);
    row.days.clear();
    row.days.reserve(states.size());
    for (qsizetype i = 0; i < states.size(); ++i) {
        const QDate day = first.addDays(i);
        row.days.append(QVariantMap{{u"date"_s, isoDate(day)},
                                    {u"day"_s, day.day()},
                                    {u"state"_s, habits::dayStateName(states[i])},
                                    {u"today"_s, day == today}});
    }
}

void HabitListModel::rebuild()
{
    m_rebuildPending = false;
    m_lastRecomputed = 0;
    if (!m_index) {
        if (!m_rows.isEmpty()) {
            beginResetModel();
            m_rows.clear();
            endResetModel();
        }
        m_cache.clear();
        updateCounts();
        return;
    }
    const QString computedFor = isoDate(currentDay()) + u'|' + isoDate(shownMonth()) + u'|'
                                + QString::number(int(weekStartDay())) + u'|' + m_locale;
    const bool full = m_fullPending || computedFor != m_computedFor;
    m_fullPending = false;
    m_computedFor = computedFor;
    const bool changed = m_cache.sync(*m_index);
    if (!changed && !full)
        return;
    const QList<habits::Habit> &habits = m_cache.habits();
    const bool sameRows = std::ranges::equal(habits, std::as_const(m_rows), [](const habits::Habit &h, const Row &r) {
        return h.key == r.habit.key && h.path == r.habit.path;
    });
    if (!sameRows) {
        beginResetModel();
        m_rows.clear();
        m_rows.reserve(habits.size());
        for (const habits::Habit &h : habits) {
            Row &row = m_rows.emplaceBack();
            row.habit = h;
            compute(row);
        }
        m_lastRecomputed = int(m_rows.size());
        endResetModel();
    } else {
        for (qsizetype i = 0; i < habits.size(); ++i) {
            Row &row = m_rows[i];
            if (!full && row.habit == habits[i] && row.revision == m_cache.revision(row.habit.key))
                continue;
            row.habit = habits[i];
            compute(row);
            ++m_lastRecomputed;
            Q_EMIT dataChanged(index(int(i)), index(int(i)));
        }
    }
    updateCounts();
}

void HabitListModel::updateCounts()
{
    int due = 0;
    int done = 0;
    int problems = 0;
    for (const Row &row : std::as_const(m_rows)) {
        if (!row.habit.isValid()) {
            ++problems;
            continue;
        }
        if (row.stats.todayRecorded && row.stats.todayMark == habits::Mark::Done)
            ++done;
        else if (row.stats.dueToday)
            ++due;
    }
    m_due = due;
    m_doneToday = done;
    m_problems = problems;
    Q_EMIT countsChanged();
}

} // namespace brulion::planning
