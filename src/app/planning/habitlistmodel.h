// HabitListModel — the habits of the vault for HabitsView.qml (docs/UI.md "Habits"), one row per
// definition (docs/SYNTAX.md §5 "Habits"): its schedule, today's check, streaks, the
// completion rate, the period's progress and the month grid of `month`.
//
//   HabitListModel { vaultIndex: PlanningContext.vaultIndex; today: DatesHelper.today; month: "2026-11" }
//
// Follows the index through a habits::HabitCache: after a change only the notes it names are read
// again, and only the rows whose habit's revision moved are computed again (dataChanged); rows are
// reset when habits are added, removed or reordered. Ticks are written by Workspace.setHabitCheck()
// (the daily note's check), not here.
//
// Roles: key, name, schedule (as written), scheduleLabel (in the UI language), valid, problem (""
// | "duplicate" | "schedule"), path, line, isPage (the page itself is the habit), todayMark ("open"
// | "done" | "skipped"), todayRecorded, dueToday, currentStreak, longestStreak, streakUnit ("days"
// | "times" | "weeks" | "months" | "years"), streakLabel ("5 dni"), longestLabel, rate (0…1, -1 when
// nothing was due yet), rateLabel ("80%"), periodDone, periodTarget, periodLabel ("2 z 3 w tym
// tygodniu"; "" for other schedules), lastDone, start, days (the month grid: [{date, day, state
// ("outside" | "future" | "rest" | "due" | "done" | "skipped" | "missed"), today}]).
#pragma once

#include "habits/habitcache.h"
#include "index/vaultindex.h"

#include <QAbstractListModel>
#include <QDate>
#include <QPointer>
#include <QQmlParserStatus>
#include <QStringList>
#include <QTimer>
#include <QVariantList>

#include <functional>

namespace brulion::planning {

class HabitListModel : public QAbstractListModel, public QQmlParserStatus {
    Q_OBJECT
    Q_INTERFACES(QQmlParserStatus)

    Q_PROPERTY(QObject *vaultIndex READ vaultIndexObject WRITE setVaultIndexObject NOTIFY vaultIndexChanged FINAL)
    /// Today ("YYYY-MM-DD"); "" = the system clock.
    Q_PROPERTY(QString today READ today WRITE setToday NOTIFY todayChanged FINAL)
    /// First weekday of the grid and of "N times a week" (1 = Monday … 7 = Sunday); 0 = the vault's.
    Q_PROPERTY(int weekStart READ weekStart WRITE setWeekStart NOTIFY weekStartChanged FINAL)
    Q_PROPERTY(QString locale READ locale WRITE setLocale NOTIFY localeChanged FINAL)
    /// The month of the grids ("YYYY-MM"); "" = today's.
    Q_PROPERTY(QString month READ month WRITE setMonth NOTIFY monthChanged FINAL)
    /// "listopad 2026" / "November 2026".
    Q_PROPERTY(QString monthLabel READ monthLabel NOTIFY monthChanged FINAL)
    /// The grid's column heads from weekStart: ["pn", "wt", …].
    Q_PROPERTY(QStringList weekdayNames READ weekdayNames NOTIFY monthChanged FINAL)
    /// Empty cells before the month's 1st.
    Q_PROPERTY(int monthOffset READ monthOffset NOTIFY monthChanged FINAL)
    /// The month shown is today's.
    Q_PROPERTY(bool currentMonth READ currentMonth NOTIFY monthChanged FINAL)
    Q_PROPERTY(int count READ count NOTIFY countsChanged FINAL)
    /// Valid habits due today and not done / done today / defined with a problem.
    Q_PROPERTY(int dueCount READ dueCount NOTIFY countsChanged FINAL)
    Q_PROPERTY(int doneTodayCount READ doneTodayCount NOTIFY countsChanged FINAL)
    Q_PROPERTY(int problemCount READ problemCount NOTIFY countsChanged FINAL)
    /// The note holding the first definition ("" when none): where "Add habit" appends.
    Q_PROPERTY(QString habitsPage READ habitsPage NOTIFY countsChanged FINAL)
    /// Names of checks no definition has (their days are kept; a definition picks them up).
    Q_PROPERTY(QStringList undefinedNames READ undefinedNames NOTIFY countsChanged FINAL)
    /// Debounce of rebuilds after index changes, ms (default 150).
    Q_PROPERTY(int refreshDelay READ refreshDelay WRITE setRefreshDelay NOTIFY refreshDelayChanged FINAL)

public:
    enum Role {
        KeyRole = Qt::UserRole + 1,
        NameRole,
        ScheduleRole,
        ScheduleLabelRole,
        ValidRole,
        ProblemRole,
        PathRole,
        LineRole,
        IsPageRole,
        TodayMarkRole,
        TodayRecordedRole,
        DueTodayRole,
        CurrentStreakRole,
        LongestStreakRole,
        StreakUnitRole,
        StreakLabelRole,
        LongestLabelRole,
        RateRole,
        RateLabelRole,
        PeriodDoneRole,
        PeriodTargetRole,
        PeriodLabelRole,
        LastDoneRole,
        StartRole,
        DaysRole,
    };
    Q_ENUM(Role)

    explicit HabitListModel(QObject *parent = nullptr);
    ~HabitListModel() override;

    int rowCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
    QHash<int, QByteArray> roleNames() const override;

    void classBegin() override;
    void componentComplete() override;

    VaultIndex *vaultIndex() const { return m_index.data(); }
    void setVaultIndex(VaultIndex *index);
    QObject *vaultIndexObject() const { return m_index.data(); }
    void setVaultIndexObject(QObject *index);
    QString today() const { return m_todayText; }
    void setToday(const QString &today);
    int weekStart() const { return m_weekStart; }
    void setWeekStart(int weekStart);
    QString locale() const { return m_locale; }
    void setLocale(const QString &locale);
    QString month() const { return m_monthText; }
    void setMonth(const QString &month);
    QString monthLabel() const;
    QStringList weekdayNames() const;
    int monthOffset() const;
    bool currentMonth() const;
    int count() const { return int(m_rows.size()); }
    int dueCount() const { return m_due; }
    int doneTodayCount() const { return m_doneToday; }
    int problemCount() const { return m_problems; }
    QString habitsPage() const;
    QStringList undefinedNames() const;
    int refreshDelay() const { return m_refreshTimer.interval(); }
    void setRefreshDelay(int msec);

    /// The month before / after the one shown, today's.
    Q_INVOKABLE void previousMonth();
    Q_INVOKABLE void nextMonth();
    Q_INVOKABLE void thisMonth();
    /// A schedule as the view's "add" dialog previews it: its words in the UI language, "" when it
    /// is not understood (empty text: every day).
    Q_INVOKABLE QString describeSchedule(const QString &text) const;
    /// Is a habit of this name (any letter case) defined?
    Q_INVOKABLE bool hasHabit(const QString &name) const;
    /// The row's data as a map (QML helpers, tests); {} out of range.
    Q_INVOKABLE QVariantMap get(int row) const;
    /// Row of the habit `key` (habits::habitKey()), -1 when none.
    Q_INVOKABLE int rowOfKey(const QString &key) const;
    /// Rebuild now.
    Q_INVOKABLE void refresh();

    /// Test hook: the clock used when `today` is "".
    void setClock(std::function<QDate()> clock) { m_clock = std::move(clock); }
    /// Rows computed again by the last rebuild (tests: an edit recomputes only its habit's row).
    int lastRecomputed() const { return m_lastRecomputed; }

Q_SIGNALS:
    void vaultIndexChanged();
    void todayChanged();
    void weekStartChanged();
    void localeChanged();
    void monthChanged();
    void countsChanged();
    void refreshDelayChanged();

private:
    struct Row {
        habits::Habit habit;
        habits::Stats stats;
        QVariantList days;
        int revision = -1;
    };
    QDate currentDay() const;
    QDate shownMonth() const;
    Qt::DayOfWeek weekStartDay() const;
    void requestRebuild(bool full);
    void scheduleRefresh();
    void rebuild();
    void compute(Row &row) const;
    QVariant rowData(const Row &row, int role) const;
    void updateCounts();

    QPointer<VaultIndex> m_index;
    QMetaObject::Connection m_indexConnection;
    habits::HabitCache m_cache;
    QList<Row> m_rows;
    QString m_todayText;
    QDate m_today;
    int m_weekStart = 0;
    QString m_locale = QStringLiteral("pl");
    QString m_monthText;
    QDate m_month; ///< the 1st of the month shown (invalid: today's)
    std::function<QDate()> m_clock;
    QTimer m_refreshTimer;
    bool m_completing = false;
    bool m_rebuildPending = false;
    bool m_fullPending = true; ///< every row computed again (another day, month, week start, language)
    QString m_computedFor;     ///< what the rows were computed for (today, month, week start, language)
    int m_due = 0;
    int m_doneToday = 0;
    int m_problems = 0;
    int m_lastRecomputed = 0;
};

} // namespace brulion::planning
