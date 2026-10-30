// TaskListModel — the vault's tasks for TasksView.qml, in sections:
//
//   overdue   Zaległe       open (not done/cancelled) with a due date before today
//   today     Dziś          due or scheduled today, or scheduled in the past (rolled over)
//   tomorrow  Jutro         due / scheduled tomorrow
//   thisWeek  Ten tydzień   until the end of today's week (per weekStart)
//   later     Później       after this week (a start date 🛫 in the future counts too)
//   noDate    Bez terminu   no due / scheduled date
//   done      Zrobione      done and cancelled tasks, most recently finished first (collapsed
//                           by default)
//
// The section date of an open task is the earlier of its due and scheduled dates; a task with
// neither is filed by its next reminder (⏰ in the block, as quick capture writes "… jutro o 9").
//
//   TaskListModel { vaultIndex: PlanningContext.vaultIndex; today: DatesHelper.today; tagFilter: "praca" }
//
// Filters: tagFilter (also sub-tags, block / ancestor / page tags), pageFilter (a page path),
// folderFilter (path prefix), priorityFilter ("" | "highest" … "lowest" | "none"),
// statusFilter ("all" | "open" | "todo" | "doing" | "deferred" | "done" | "cancelled"),
// textFilter (words, case- and diacritic-insensitive). They are evaluated by the query engine
// (index/query.h) from the text in `queryText` — so "Pokaż jako zapytanie" shows exactly what
// the list shows (docs/SYNTAX.md §10). sortKey: "due" (default) | "priority" | "page" |
// "created" orders tasks inside each section.
//
// Rows are section headers and tasks (rowType "header" | "task"); the tasks of collapsed
// sections are left out, their header stays (with the count). Empty sections have no header.
// Created from QML, the model builds its rows once, when the component is complete (not once
// per property set on creation); afterwards every change of a property rebuilds at once.
// After an index change only the changed notes are evaluated again (a QueryCache) and their
// tasks merged into the sorted list; rows then move, appear or disappear as before.
//
// Header roles: rowType, section, sectionLabel, sectionCount, collapsed.
// Task roles (also on headers, empty): key ("path:line"), text (plain), html (escaped rich
// text: links and tags clickable — page:/tag: URLs), status, path, line, pageTitle, folder,
// isJournal, due ("YYYY-MM-DD[ HH:mm]"), dueLabel ("jutro", "pt 17 paź"), overdue, scheduled,
// scheduledLabel, start, startLabel, recurrence (canonical), recurrenceLabel (UI language),
// priority (0 = none … 5 = highest), priorityName, tags, reminderLabel (next reminder in the
// block, "jutro 09:00"), reminderAlarm, reminderSpec (that reminder as the index reads it, "YYYY-MM-DD
// HH:mm": its chip's menu names it), doneDate, created.
#pragma once

#include "index/notecache.h"
#include "index/vaultindex.h"
#include "types.h"

#include <QAbstractListModel>
#include <QCollatorSortKey>
#include <QDate>
#include <QPointer>
#include <QQmlParserStatus>
#include <QStringList>
#include <QTimer>
#include <QVariantMap>

#include <functional>
#include <memory>
#include <optional>

namespace brulion {
class QueryCache;
struct QueryItem;
} // namespace brulion

namespace brulion::planning {

class TaskListModel : public QAbstractListModel, public QQmlParserStatus {
    Q_OBJECT
    Q_INTERFACES(QQmlParserStatus)

    Q_PROPERTY(QObject *vaultIndex READ vaultIndexObject WRITE setVaultIndexObject NOTIFY vaultIndexChanged FINAL)
    /// Today ("YYYY-MM-DD"); "" = the system clock.
    Q_PROPERTY(QString today READ today WRITE setToday NOTIFY todayChanged FINAL)
    /// First weekday for "Ten tydzień" (1 = Monday … 7 = Sunday); 0 = the vault's setting.
    Q_PROPERTY(int weekStart READ weekStart WRITE setWeekStart NOTIFY weekStartChanged FINAL)
    Q_PROPERTY(QString locale READ locale WRITE setLocale NOTIFY localeChanged FINAL)
    /// Renderer colours (see BlockEditor.rendererPalette).
    Q_PROPERTY(QVariantMap palette READ palette WRITE setPalette NOTIFY paletteChanged FINAL)

    Q_PROPERTY(QString tagFilter READ tagFilter WRITE setTagFilter NOTIFY filtersChanged FINAL)
    Q_PROPERTY(QString pageFilter READ pageFilter WRITE setPageFilter NOTIFY filtersChanged FINAL)
    Q_PROPERTY(QString folderFilter READ folderFilter WRITE setFolderFilter NOTIFY filtersChanged FINAL)
    Q_PROPERTY(QString priorityFilter READ priorityFilter WRITE setPriorityFilter NOTIFY filtersChanged FINAL)
    Q_PROPERTY(QString statusFilter READ statusFilter WRITE setStatusFilter NOTIFY filtersChanged FINAL)
    Q_PROPERTY(QString textFilter READ textFilter WRITE setTextFilter NOTIFY filtersChanged FINAL)
    Q_PROPERTY(QString sortKey READ sortKey WRITE setSortKey NOTIFY sortKeyChanged FINAL)
    /// Any filter set (the "clear filters" button).
    Q_PROPERTY(bool filtered READ filtered NOTIFY filtersChanged FINAL)
    /// Keys of collapsed sections (default ["done"]).
    Q_PROPERTY(QStringList collapsedSections READ collapsedSections WRITE setCollapsedSections NOTIFY collapsedSectionsChanged FINAL)

    /// The equivalent ```query (SYNTAX §10) of the filters and the sort.
    Q_PROPERTY(QString queryText READ queryText NOTIFY filtersChanged FINAL)
    /// [{ key, label, count, collapsed }] of the non-empty sections, in order.
    Q_PROPERTY(QVariantList sections READ sections NOTIFY countsChanged FINAL)
    /// Tasks matching the filters / open ones among them / overdue / due today.
    Q_PROPERTY(int totalCount READ totalCount NOTIFY countsChanged FINAL)
    Q_PROPERTY(int openCount READ openCount NOTIFY countsChanged FINAL)
    Q_PROPERTY(int overdueCount READ overdueCount NOTIFY countsChanged FINAL)
    Q_PROPERTY(int todayCount READ todayCount NOTIFY countsChanged FINAL)
    /// Tasks in the whole vault (no filters) — distinguishes "no tasks" from "none match".
    Q_PROPERTY(int vaultTaskCount READ vaultTaskCount NOTIFY countsChanged FINAL)
    /// Debounce of rebuilds after index changes, ms (default 150).
    Q_PROPERTY(int refreshDelay READ refreshDelay WRITE setRefreshDelay NOTIFY refreshDelayChanged FINAL)

public:
    enum Role {
        RowTypeRole = Qt::UserRole + 1,
        SectionRole,
        SectionLabelRole,
        SectionCountRole,
        CollapsedRole,
        KeyRole,
        TextRole,
        HtmlRole,
        StatusRole,
        PathRole,
        LineRole,
        PageTitleRole,
        FolderRole,
        IsJournalRole,
        DueRole,
        DueLabelRole,
        OverdueRole,
        ScheduledRole,
        ScheduledLabelRole,
        StartRole,
        StartLabelRole,
        RecurrenceRole,
        RecurrenceLabelRole,
        PriorityRole,
        PriorityNameRole,
        TagsRole,
        ReminderLabelRole,
        ReminderAlarmRole,
        ReminderSpecRole,
        DoneDateRole,
        CreatedRole,
    };
    Q_ENUM(Role)

    explicit TaskListModel(QObject *parent = nullptr);
    ~TaskListModel() override;

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

    QString today() const { return m_todayText; }
    void setToday(const QString &today);
    int weekStart() const { return m_weekStart; }
    void setWeekStart(int weekStart);
    QString locale() const { return m_locale; }
    void setLocale(const QString &locale);
    QVariantMap palette() const { return m_paletteMap; }
    void setPalette(const QVariantMap &palette);

    QString tagFilter() const { return m_tag; }
    void setTagFilter(const QString &tag);
    QString pageFilter() const { return m_page; }
    void setPageFilter(const QString &path);
    QString folderFilter() const { return m_folder; }
    void setFolderFilter(const QString &folder);
    QString priorityFilter() const { return m_priority; }
    void setPriorityFilter(const QString &priority);
    QString statusFilter() const { return m_status; }
    void setStatusFilter(const QString &status);
    QString textFilter() const { return m_text; }
    void setTextFilter(const QString &text);
    QString sortKey() const { return m_sort; }
    void setSortKey(const QString &key);
    bool filtered() const;
    QStringList collapsedSections() const { return m_collapsed; }
    void setCollapsedSections(const QStringList &keys);

    QString queryText() const;
    QVariantList sections() const;
    int totalCount() const { return m_total; }
    int openCount() const { return m_open; }
    int overdueCount() const { return m_overdue; }
    int todayCount() const { return m_todayCount; }
    int vaultTaskCount() const { return m_vaultTasks; }
    int refreshDelay() const { return m_refreshTimer.interval(); }
    void setRefreshDelay(int msec);

    /// Collapse / expand a section.
    Q_INVOKABLE void toggleSection(const QString &key);
    /// Remove every filter (the sort stays).
    Q_INVOKABLE void clearFilters();
    /// Take the filters from a query text (route {view: "tasks", query}): tag, page, path,
    /// priority, status and text filters are applied (one value each; other filters ignored).
    /// Returns false (and changes nothing) when the query does not parse.
    Q_INVOKABLE bool applyQuery(const QString &query);
    /// Filter choices for the menus: tags [{ value, label, count }] (most used first), folders
    /// [{ value, label, count }], pages [{ value, label, count }] — only places with tasks.
    Q_INVOKABLE QVariantList tagChoices(int limit = 50) const;
    Q_INVOKABLE QVariantList folderChoices(int limit = 50) const;
    Q_INVOKABLE QVariantList pageChoices(int limit = 50) const;
    /// Row of the task "path:line", -1 when not listed.
    Q_INVOKABLE int rowOfKey(const QString &key) const;
    /// The row's data as a map (QML helpers, tests).
    Q_INVOKABLE QVariantMap get(int row) const;
    /// Rebuild now.
    Q_INVOKABLE void refresh();

    /// Test hook: the clock used when `today` is "".
    void setClock(std::function<QDate()> clock);
    /// The QueryCache the model evaluates its query with (C++ only; default: its own). Widgets
    /// keep one across the models they create per publish. Set it before the model builds.
    void setQueryCache(std::shared_ptr<QueryCache> cache);

    /// The section of a task for `today` (keys as above); `reminder` is the date of the block's
    /// next reminder (used when the task has no due / scheduled date).
    static QString sectionOf(const Task &task, QDate today, Qt::DayOfWeek weekStart,
                             std::optional<QDate> reminder = std::nullopt);
    /// The day an open task is filed under (see sectionOf()): the earlier of due and scheduled,
    /// else `reminder`, else a start date after `today`; nullopt when it has none (no date).
    static std::optional<QDate> filingDate(const Task &task, QDate today,
                                           std::optional<QDate> reminder = std::nullopt);
    /// The reminder of a block that counts on `today`: the earliest on or after `today`, else
    /// the first of them (all are in the past); nullopt without reminders.
    static std::optional<Reminder> nextReminder(const QList<Reminder> &reminders, QDate today);
    /// Section keys in display order.
    static QStringList sectionKeys();
    /// Translated section label.
    static QString sectionLabel(const QString &key);

Q_SIGNALS:
    void vaultIndexChanged();
    void todayChanged();
    void weekStartChanged();
    void localeChanged();
    void paletteChanged();
    void filtersChanged();
    void sortKeyChanged();
    void collapsedSectionsChanged();
    void countsChanged();
    void refreshDelayChanged();

private:
    struct Item {
        TaskRef ref;
        QString section;
        QString pageTitle;
        QString folder;
        bool isJournal = false;
        QStringList tags;
        std::optional<Reminder> reminder;
        // What the list is sorted by (itemLess()).
        qsizetype sectionRank = 0;
        std::optional<QDate> finished;            ///< the done section's date
        int priority = 0;                         ///< priorityRank()
        std::optional<DateSpec> date;             ///< the sort date, else the reminder's
        int title = 0;                            ///< m_titleRank of pageTitle
        mutable QString html; ///< rendered lazily
        mutable bool rendered = false;
    };
    struct Row {
        bool header = false;
        QString section;
        int item = -1; ///< index into m_items
    };

    QDate currentDay() const;
    Qt::DayOfWeek weekStartDay() const;
    void scheduleRefresh();
    /// rebuild(), or once when the QML component is complete.
    void requestRebuild();
    void rebuild();
    /// The model's item for a task of the query's result (Item::title not set).
    Item makeItem(const QueryItem &qi, QDate today, Qt::DayOfWeek weekStart);
    /// Rank the page titles of `items` in compareNames() order (m_titleRank) and set Item::title.
    void rankTitles(QList<Item> &items);
    /// The list order: section, then the sort key's fields, then title, path and line.
    bool itemLess(const Item &a, const Item &b) const;
    void rebuildRows();
    void filtersEdited();
    QVariant taskRowData(const Item &item, int role) const;
    QVariant sectionRowData(const QString &section, int role) const;
    QString pageQueryValue(const QString &path) const;

    QPointer<VaultIndex> m_index;
    QMetaObject::Connection m_indexConnection;
    QString m_todayText;
    QDate m_today;
    int m_weekStart = 0;
    QString m_locale = QStringLiteral("pl");
    QVariantMap m_paletteMap;
    std::function<QDate()> m_clock;

    QString m_tag;
    QString m_page;
    QString m_folder;
    QString m_priority;
    QString m_status = QStringLiteral("all");
    QString m_text;
    QString m_sort = QStringLiteral("due");
    QStringList m_collapsed{QStringLiteral("done")};

    std::shared_ptr<QueryCache> m_query;          ///< the query's matches, kept across index changes
    IndexFollower m_follower;                     ///< which notes changed since m_items was built
    QHash<QString, QCollatorSortKey> m_titleKeys; ///< nameSortKey() per page title
    QHash<QString, int> m_titleRank;              ///< the titles of m_items in compareNames() order
    enum SortMode { SortByDue, SortByPriority, SortByCreated, SortByPage };
    SortMode m_sortMode = SortByDue; ///< m_sort as itemLess() reads it
    QList<Item> m_items;            ///< all matching tasks, sorted by section then sort key
    QHash<QString, int> m_sectionCounts;
    QList<Row> m_rows;
    QStringList m_rowKeys;          ///< key of every row ("header:<section>", "path:line")
    QHash<QString, int> m_rowOfKey;
    int m_total = 0;
    int m_open = 0;
    int m_overdue = 0;
    int m_todayCount = 0;
    int m_vaultTasks = 0;

    QTimer m_refreshTimer;
    bool m_completing = false;     ///< between classBegin() and componentComplete()
    bool m_rebuildPending = false; ///< a rebuild was requested meanwhile
};

} // namespace brulion::planning
