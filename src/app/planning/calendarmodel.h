// CalendarModel — the days of the calendar view (QML: CalendarView.qml) with their entries from
// reminders::calendarItems(): daily notes (journal dot), task due / scheduled / start / done
// dates and reminders / alarms with their times; recurring tasks and reminders are expanded.
//
//   CalendarModel { vaultIndex: PlanningContext.vaultIndex; mode: "month"; anchorDate: "2026-10-15"
//                   today: DatesHelper.today; weekStart: DatesHelper.weekStart; locale: DatesHelper.locale }
//
// Modes (rows = days):
//   "month"   42 days (6 weeks) starting on `weekStart` at the week of the anchor's 1st
//   "week"    the 7 days of the anchor's week (QML lays out hour rows from `minutes`)
//   "agenda"  the days of [start of the anchor's week, + agendaDays - 1] that have entries,
//             plus today
// next() / previous() move the anchor by a month, a week or agendaDays; goToday(). The anchor
// is the selected day (isSelected); moving it inside the range only updates that role.
//
// Day roles: date ("YYYY-MM-DD"), day, month, year, dayOfWeek (1 = Monday), inMonth, isToday,
// isPast, isWeekend, isSelected, weekNumber (ISO), journalPath ("" = none), hasJournal, items
// (list of entry maps, journals excluded, see below), itemCount, timedCount, kinds (distinct
// mark kinds for dots: due, overdue, scheduled, reminder, alarm, done), label ("czw, 15 paź"),
// longLabel ("czwartek, 15 października").
//
// Entry maps: { key, kind ("due" | "scheduled" | "start" | "reminder" | "done"), variant (chip:
// "due" | "overdue" | "scheduled" | "reminder" | "alarm" | "done" | "neutral"), icon, time
// ("HH:mm" or ""), minutes (since midnight, -1 = all-day), title (plain text), path, line,
// pageTitle, isTask, status ("todo" … or "" for reminders outside tasks), closed, overdue,
// recurring, alarm }. Within a day timed entries come first by time, then all-day ones.
//
// The model watches the index (debounced by refreshDelay ms; refresh() rebuilds at once).
// `today` "" follows the system clock (re-evaluated at midnight). `weekStart` 0 takes the
// vault's weekStartsOn. Everything is local wall-clock dates (QDate arithmetic, never UTC), so
// DST changes cannot shift a day. Created from QML, the model builds its days once, when the
// component is complete (not once per property set on creation).
#pragma once

#include "reminders/calendar.h"
#include "types.h"

#include <QAbstractListModel>
#include <QDate>
#include <QHash>
#include <QPointer>
#include <QQmlParserStatus>
#include <QTimer>
#include <QVariantList>
#include <QVariantMap>

#include <functional>

namespace brulion {
class VaultIndex;
}

namespace brulion::planning {

class CalendarModel : public QAbstractListModel, public QQmlParserStatus {
    Q_OBJECT
    Q_INTERFACES(QQmlParserStatus)

    /// The VaultIndex to read (QObject* so QML can pass PlanningContext.vaultIndex).
    Q_PROPERTY(QObject *vaultIndex READ vaultIndexObject WRITE setVaultIndexObject NOTIFY vaultIndexChanged FINAL)
    /// "month" | "week" | "agenda".
    Q_PROPERTY(QString mode READ mode WRITE setMode NOTIFY modeChanged FINAL)
    /// Selected day ("YYYY-MM-DD"); defines the range. Default: today.
    Q_PROPERTY(QString anchorDate READ anchorDate WRITE setAnchorDate NOTIFY anchorDateChanged FINAL)
    /// Today ("YYYY-MM-DD"); "" = the system clock.
    Q_PROPERTY(QString today READ today WRITE setToday NOTIFY todayChanged FINAL)
    /// First day of the week (1 = Monday … 7 = Sunday); 0 = the vault's setting.
    Q_PROPERTY(int weekStart READ weekStart WRITE setWeekStart NOTIFY weekStartChanged FINAL)
    /// "pl" (default) | "en": labels and titles.
    Q_PROPERTY(QString locale READ locale WRITE setLocale NOTIFY localeChanged FINAL)
    /// Length of the agenda window in days (1–366, default 28).
    Q_PROPERTY(int agendaDays READ agendaDays WRITE setAgendaDays NOTIFY agendaDaysChanged FINAL)
    /// Debounce of rebuilds after index changes, ms (default 120).
    Q_PROPERTY(int refreshDelay READ refreshDelay WRITE setRefreshDelay NOTIFY refreshDelayChanged FINAL)

    Q_PROPERTY(QString rangeStart READ rangeStart NOTIFY rangeChanged FINAL)
    Q_PROPERTY(QString rangeEnd READ rangeEnd NOTIFY rangeChanged FINAL)
    /// Header: "Październik 2026" / "12–18 października 2026" / "15 października – 11 listopada 2026".
    Q_PROPERTY(QString title READ title NOTIFY rangeChanged FINAL)
    /// The same for narrow headers: "Październik 2026" / "12–18 paź" / "28 wrz – 4 paź" (the year
    /// only when the range is not in today's year).
    Q_PROPERTY(QString shortTitle READ shortTitle NOTIFY rangeChanged FINAL)
    /// Month shown by the month grid (also the anchor's month in other modes).
    Q_PROPERTY(int displayYear READ displayYear NOTIFY rangeChanged FINAL)
    Q_PROPERTY(int displayMonth READ displayMonth NOTIFY rangeChanged FINAL)
    /// The effective first weekday (1–7).
    Q_PROPERTY(int effectiveWeekStart READ effectiveWeekStart NOTIFY rangeChanged FINAL)
    /// The anchor is today (the "Today" button has nothing to do).
    Q_PROPERTY(bool showingToday READ showingToday NOTIFY rangeChanged FINAL)

    /// Entries (journals excluded) within the range.
    Q_PROPERTY(int itemCount READ itemCount NOTIFY itemsChanged FINAL)
    /// Open tasks due before today: [{ key, title, path, line, pageTitle, due, dueLabel, status,
    /// recurring }], oldest first (at most MaxOverdue; overdueCount has the full number).
    Q_PROPERTY(QVariantList overdue READ overdue NOTIFY itemsChanged FINAL)
    Q_PROPERTY(int overdueCount READ overdueCount NOTIFY itemsChanged FINAL)
    /// Week mode: earliest / latest hour with a timed entry in the range (defaults 8 / 18).
    Q_PROPERTY(int firstHour READ firstHour NOTIFY itemsChanged FINAL)
    Q_PROPERTY(int lastHour READ lastHour NOTIFY itemsChanged FINAL)
    /// Grows with every rebuild (bind day panels to it).
    Q_PROPERTY(int revision READ revision NOTIFY itemsChanged FINAL)

public:
    static constexpr int MaxOverdue = 200;

    enum Role {
        DateRole = Qt::UserRole + 1,
        DayRole,
        MonthRole,
        YearRole,
        DayOfWeekRole,
        InMonthRole,
        IsTodayRole,
        IsPastRole,
        IsWeekendRole,
        IsSelectedRole,
        WeekNumberRole,
        JournalPathRole,
        HasJournalRole,
        ItemsRole,
        ItemCountRole,
        TimedCountRole,
        KindsRole,
        LabelRole,
        LongLabelRole,
    };
    Q_ENUM(Role)

    explicit CalendarModel(QObject *parent = nullptr);
    ~CalendarModel() override;

    // QAbstractListModel
    int rowCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
    QHash<int, QByteArray> roleNames() const override;

    // QQmlParserStatus: properties set while QML creates the model rebuild once, at the end.
    void classBegin() override;
    void componentComplete() override;

    VaultIndex *vaultIndex() const;
    void setVaultIndex(VaultIndex *index);
    QObject *vaultIndexObject() const;
    void setVaultIndexObject(QObject *index);

    QString mode() const { return m_mode; }
    void setMode(const QString &mode);
    QString anchorDate() const;
    void setAnchorDate(const QString &date);
    QString today() const { return m_todayText; }
    void setToday(const QString &today);
    int weekStart() const { return m_weekStart; }
    void setWeekStart(int weekStart);
    QString locale() const { return m_locale; }
    void setLocale(const QString &locale);
    int agendaDays() const { return m_agendaDays; }
    void setAgendaDays(int days);
    int refreshDelay() const { return m_refreshTimer.interval(); }
    void setRefreshDelay(int msec);

    QString rangeStart() const;
    QString rangeEnd() const;
    QString title() const;
    QString shortTitle() const;
    int displayYear() const { return anchor().year(); }
    int displayMonth() const { return anchor().month(); }
    int effectiveWeekStart() const { return int(weekStartDay()); }
    bool showingToday() const { return anchor() == currentDay(); }
    int itemCount() const { return m_itemCount; }
    QVariantList overdue() const { return m_overdue; }
    int overdueCount() const { return m_overdueCount; }
    int firstHour() const { return m_firstHour; }
    int lastHour() const { return m_lastHour; }
    int revision() const { return m_revision; }

    /// Move the anchor by one period of the mode (month / week / agenda window).
    Q_INVOKABLE void next();
    Q_INVOKABLE void previous();
    /// Anchor = today.
    Q_INVOKABLE void goToday();
    /// Move the anchor by `days` (keyboard navigation); the range follows when it leaves it.
    Q_INVOKABLE void moveAnchor(int days);
    /// Entries of any day (not only the range): the journal first ({ kind: "journal", path,
    /// title }), then the entries as in `items`.
    Q_INVOKABLE QVariantList dayItems(const QString &date) const;
    /// { "YYYY-MM-DD": [kinds] } for BDatePicker / BReminderDialog marks within [from, to]
    /// (at most 400 days).
    Q_INVOKABLE QVariantMap marks(const QString &from, const QString &to) const;
    /// Row of `date` in the model, -1 when not shown.
    Q_INVOKABLE int rowForDate(const QString &date) const;
    /// Rebuild now.
    Q_INVOKABLE void refresh();

    /// Test hook: the clock used when `today` is "".
    void setClock(std::function<QDate()> clock);

Q_SIGNALS:
    void vaultIndexChanged();
    void modeChanged();
    void anchorDateChanged();
    void todayChanged();
    void weekStartChanged();
    void localeChanged();
    void agendaDaysChanged();
    void refreshDelayChanged();
    void rangeChanged();
    void itemsChanged();

private:
    struct Day {
        QDate date;
        VaultPath journal;
        QVariantList items;
        int timed = 0;
        QStringList kinds;
    };

    QDate currentDay() const;
    QDate anchor() const;
    Qt::DayOfWeek weekStartDay() const;
    /// [first, last] of the range for `anchor` in the current mode.
    std::pair<QDate, QDate> rangeFor(QDate anchor) const;
    void scheduleRefresh();
    /// rebuild(forceReset), or once when the QML component is complete.
    void requestRebuild(bool forceReset);
    void rebuild(bool forceReset);
    QVariantMap itemMap(const CalendarItem &item, QDate today) const;
    QVariant dayData(const Day &day, int role) const;
    void scheduleMidnight();

    QPointer<VaultIndex> m_index;
    QMetaObject::Connection m_indexConnection;
    QString m_mode = QStringLiteral("month");
    QDate m_anchor; ///< invalid = today
    QString m_todayText;
    QDate m_today;  ///< parsed m_todayText
    int m_weekStart = 0;
    QString m_locale = QStringLiteral("pl");
    int m_agendaDays = 28;
    std::function<QDate()> m_clock;

    QDate m_rangeStart;
    QDate m_rangeEnd;
    QList<Day> m_days;
    QHash<QDate, int> m_rowOf;
    int m_itemCount = 0;
    QVariantList m_overdue;
    int m_overdueCount = 0;
    int m_firstHour = 8;
    int m_lastHour = 18;
    int m_revision = 0;

    QTimer m_refreshTimer;
    QTimer m_midnightTimer;
    bool m_completing = false;     ///< between classBegin() and componentComplete()
    bool m_rebuildPending = false; ///< a rebuild was requested meanwhile
    bool m_resetPending = false;   ///< … with forceReset
};

} // namespace brulion::planning
