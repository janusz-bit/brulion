// Sync center helpers for compiled QML (docs/UI.md "Compiled QML"): WorkspaceSync hands out its
// lists and one run's details as QVariantMap / QVariantList values, whose members compiled QML
// can read only through FINAL properties or typed C++ results.
//
//   SyncRunInfo { sync: Workspace.sync; number: 42 }   one run of the history for SyncRunDetails:
//                      `details` (WorkspaceSync::runDetails(number); {} when unknown), `known`, and
//                      its nested values as properties of their own (stats, counts, phases,
//                      errors, conflicts, slowest)
//   IoLists            QML singleton: counting and picking from WorkspaceSync's lists, the rows of
//                      IoPathPickerDialog's folders
#pragma once

#include <QObject>
#include <QPointer>
#include <QStringList>
#include <QVariantList>
#include <QVariantMap>

namespace brulion::shell {
class WorkspaceSync;
}

namespace brulion::ioviews {

class SyncRunInfo : public QObject {
    Q_OBJECT
    Q_PROPERTY(brulion::shell::WorkspaceSync *sync READ sync WRITE setSync NOTIFY syncChanged FINAL)
    Q_PROPERTY(int number READ number WRITE setNumber NOTIFY numberChanged FINAL)
    /// The run is in the history (details has its fields).
    Q_PROPERTY(bool known READ known NOTIFY detailsChanged FINAL)
    Q_PROPERTY(QVariantMap details READ details NOTIFY detailsChanged FINAL)
    Q_PROPERTY(QVariantMap stats READ stats NOTIFY detailsChanged FINAL)
    Q_PROPERTY(QVariantMap counts READ counts NOTIFY detailsChanged FINAL)
    Q_PROPERTY(QVariantList phases READ phases NOTIFY detailsChanged FINAL)
    Q_PROPERTY(QVariantList errors READ errors NOTIFY detailsChanged FINAL)
    Q_PROPERTY(QVariantList conflicts READ conflicts NOTIFY detailsChanged FINAL)
    /// stats.slowest
    Q_PROPERTY(QVariantList slowest READ slowest NOTIFY detailsChanged FINAL)

public:
    explicit SyncRunInfo(QObject *parent = nullptr);

    shell::WorkspaceSync *sync() const { return m_sync; }
    void setSync(shell::WorkspaceSync *sync);
    int number() const { return m_number; }
    void setNumber(int number);

    bool known() const { return m_details.contains(QStringLiteral("number")); }
    QVariantMap details() const { return m_details; }
    QVariantMap stats() const;
    QVariantMap counts() const;
    QVariantList phases() const;
    QVariantList errors() const;
    QVariantList conflicts() const;
    QVariantList slowest() const;

Q_SIGNALS:
    void syncChanged();
    void numberChanged();
    void detailsChanged();

private:
    void refresh();

    QPointer<shell::WorkspaceSync> m_sync;
    int m_number = 0;
    QVariantMap m_details;
};

class IoLists : public QObject {
    Q_OBJECT

public:
    explicit IoLists(QObject *parent = nullptr);

    /// How many rows of `rows` (maps) have `key` equal to `value` (pendingDeletions' "action").
    Q_INVOKABLE int countWhere(const QVariantList &rows, const QString &key, const QString &value) const;
    /// The text of `key` of every row of `rows` (maps), in order ("" where a row has none).
    Q_INVOKABLE QStringList column(const QVariantList &rows, const QString &key) const;
    /// The folder picker's rows for `query`: `folders` ranked by TextHelper::fuzzyRank (at most
    /// `limit`, 0 = all) as [{ title (the last name), subtitle (the folders above it, "" at the
    /// top), icon: "folder", path }].
    Q_INVOKABLE QVariantList folderItems(const QString &query, const QStringList &folders, int limit) const;
};

} // namespace brulion::ioviews
