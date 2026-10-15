// JournalListModel — the days of JournalsView (docs/UI.md "Navigation": the home view): today
// first — always, also before its file exists (the editor creates it with the first content) —
// then every earlier day that has a daily note, newest first.
//
//   JournalListModel { workspace: Workspace; today: DatesHelper.today }
//
// Days whose note is empty (e.g. created by an old "open today" and never written) are left out
// when they first appear; once listed a day stays while its file exists, so a day being edited
// never disappears under the caret. Days after today (planned in the calendar) are not listed.
// Several notes for one day (Logseq's "2026_10_14.md" next to "2026-10-14.md") show once: the
// configured file name, else the first path.
//
// Roles: date ("YYYY-MM-DD"), path (the note, or for today without a note the path it will
// get), exists, isToday, daysAgo (0 = today).
#pragma once

#include "workspacemodel.h"

#include <QDate>
#include <QSet>
#include <QString>

namespace brulion::content {

class JournalListModel : public WorkspaceListModel {
    Q_OBJECT
    /// Today ("YYYY-MM-DD"); "" = the workspace clock.
    Q_PROPERTY(QString today READ today WRITE setToday NOTIFY todayChanged FINAL)
    /// List empty notes too (default false).
    Q_PROPERTY(bool includeEmpty READ includeEmpty WRITE setIncludeEmpty NOTIFY includeEmptyChanged FINAL)
    /// Path of today's note (existing, or the one the editor will create).
    Q_PROPERTY(QString todayPath READ todayPath NOTIFY todayPathChanged FINAL)
    /// Today's note exists.
    Q_PROPERTY(bool todayExists READ todayExists NOTIFY todayPathChanged FINAL)

public:
    enum Role { DateRole = Qt::UserRole + 1, PathRole, ExistsRole, IsTodayRole, DaysAgoRole };
    Q_ENUM(Role)

    explicit JournalListModel(QObject *parent = nullptr);

    QString today() const { return m_todayText; }
    void setToday(const QString &today);
    bool includeEmpty() const { return m_includeEmpty; }
    void setIncludeEmpty(bool on);
    QString todayPath() const;
    bool todayExists() const;

    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    /// Row of `date`, else of the nearest earlier listed day; 0 (today) for dates after the first
    /// listed day; -1 when nothing is listed.
    Q_INVOKABLE int rowOfDate(const QString &date) const;
    /// Exact row of `date` (-1 when it is not listed).
    Q_INVOKABLE int exactRowOfDate(const QString &date) const;
    /// { date, path, exists, isToday } of a row (empty for a bad row).
    Q_INVOKABLE QVariantMap get(int row) const;

Q_SIGNALS:
    void todayChanged();
    void includeEmptyChanged();
    void todayPathChanged();

protected:
    void refresh() override;
    int rows() const override { return int(m_rows.size()); }
    void workspaceReset() override;

private:
    struct Day {
        QDate date;
        QString path;
        bool exists = false;
    };
    QDate effectiveToday() const;

    QList<Day> m_rows;
    QString m_todayText;
    bool m_includeEmpty = false;
    /// Days already listed (they stay while their file exists).
    QSet<QDate> m_shown;
    /// Today at the last refresh (the rows' isToday / daysAgo follow a new day).
    QDate m_lastToday;
    QString m_lastTodayPath;
    bool m_lastTodayExists = false;
};

} // namespace brulion::content
