// FolderTreeModel — the vault's folders and pages as a flat list for the sidebar tree (QML type
// `FolderTreeModel { workspace: Workspace }`). Lazy: only the children of expanded folders are
// rows; expanding/collapsing inserts/removes rows (no reset, the list keeps its position). The
// expanded folders are remembered per vault (device-local). Folders come first, then pages,
// each sorted case-insensitively; attachments (and folders holding nothing else) are hidden
// unless `showAttachments`.
//
// Roles: name (file name), title (page title / folder name), path, isDir, depth, expanded,
// hasChildren, fileIcon (Lucide name).
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
    /// Expand the folders above `path` (to show it); returns its row or -1.
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
    };
    struct Row {
        Entry entry;
        int depth = 0;
    };

    void rebuildChildren();
    QList<Row> rowsUnder(const QString &folder, int depth) const;
    void loadExpanded();
    void saveExpanded();

    QPointer<Workspace> m_workspace;
    bool m_showAttachments = false;
    QHash<QString, QList<Entry>> m_children; ///< folder ("" = root) -> sorted entries
    QList<Row> m_rows;
    QSet<QString> m_expanded;
    QTimer m_refreshTimer;
};

} // namespace brulion::shell
