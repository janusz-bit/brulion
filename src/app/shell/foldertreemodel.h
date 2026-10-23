// FolderTreeModel — the vault's folders and pages as a flat list for the sidebar tree (QML type
// `FolderTreeModel { workspace: Workspace }`). Lazy: only the children of expanded folders are
// rows; expanding/collapsing inserts/removes rows. Changes in the vault are applied as the rows
// that differ (inserted / removed runs, never a reset while rows stay: the list keeps its
// position and delegates). The expanded folders are remembered per vault (device-local), follow
// folders renamed or moved (Workspace::folderRenamed) and forget folders that are gone.
// Folders come first, then pages, each in Polish alphabetical order with numbers by value
// (text::compareNames()); the journals folder comes first among its siblings, and its daily notes
// newest first (`journalDate`). Attachments (and folders holding nothing else) are hidden unless
// `showAttachments`.
//
// Roles: name (file name), title (page title / folder name), path, isDir, depth, expanded,
// hasChildren, fileIcon (Lucide name), special ("journals" for the journals folder, else ""),
// journalDate ("YYYY-MM-DD" of a daily note, else "").
#pragma once

#include "workspace.h"

#include <QAbstractListModel>
#include <QHash>
#include <QPointer>
#include <QSet>
#include <QTimer>

namespace brulion::shell {

class FolderTreeModel : public QAbstractListModel {
    Q_OBJECT
    Q_PROPERTY(brulion::shell::Workspace *workspace READ workspace WRITE setWorkspace NOTIFY workspaceChanged FINAL)
    Q_PROPERTY(bool showAttachments READ showAttachments WRITE setShowAttachments NOTIFY showAttachmentsChanged FINAL)
    Q_PROPERTY(int count READ count NOTIFY countChanged FINAL)

public:
    enum Role {
        NameRole = Qt::UserRole + 1,
        TitleRole,
        PathRole,
        IsDirRole,
        DepthRole,
        ExpandedRole,
        HasChildrenRole,
        IconRole,
        SpecialRole,
        JournalDateRole,
    };
    Q_ENUM(Role)

    explicit FolderTreeModel(QObject *parent = nullptr);
    ~FolderTreeModel() override;

    Workspace *workspace() const { return m_workspace; }
    void setWorkspace(Workspace *workspace);
    bool showAttachments() const { return m_showAttachments; }
    void setShowAttachments(bool show);
    int count() const { return int(m_rows.size()); }

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    /// Expand or collapse the folder at `row`.
    Q_INVOKABLE void toggle(int row);
    Q_INVOKABLE void setExpanded(const QString &folder, bool expanded);
    Q_INVOKABLE bool isExpanded(const QString &folder) const { return m_expanded.contains(folder); }
    /// Expand the folders above `path` (to show it); returns its row or -1. A refresh still
    /// pending (a page created a moment ago) is done first.
    Q_INVOKABLE int reveal(const QString &path);
    Q_INVOKABLE void collapseAll();
    /// Row of `path`, -1 when not visible.
    Q_INVOKABLE int indexOf(const QString &path) const;
    /// Re-read the vault now (normally done shortly after every change).
    Q_INVOKABLE void refresh();

Q_SIGNALS:
    void workspaceChanged();
    void showAttachmentsChanged();
    void countChanged();

private:
    struct Entry {
        QString name;
        QString path;
        bool isDir = false;
        QString journalDate; ///< "YYYY-MM-DD" of a daily note
    };
    struct Row {
        Entry entry;
        int depth = 0;
    };

    void rebuildChildren();
    QList<Row> rowsUnder(const QString &folder, int depth) const;
    /// Replace the rows by `next`, signalling only removed and inserted runs (a reset only when
    /// rows kept change their order, or the list was or becomes empty).
    void applyRows(QList<Row> next);
    void loadExpanded();
    void saveExpanded();
    /// Drop expanded folders that no longer exist.
    void pruneExpanded();
    /// A folder moved: its expanded state (and its subfolders') moves along.
    void onFolderRenamed(const QString &oldPath, const QString &newPath);

    QPointer<Workspace> m_workspace;
    bool m_showAttachments = false;
    QHash<QString, QList<Entry>> m_children; ///< folder ("" = root) -> sorted entries
    QList<Row> m_rows;
    QSet<QString> m_expanded;
    QString m_journalsFolder;
    QTimer m_refreshTimer;
};

} // namespace brulion::shell
