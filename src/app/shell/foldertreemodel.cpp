#include "foldertreemodel.h"

#include "workspace.h"

#include "util/paths.h"
#include "vault/vault.h"

#include <QJsonArray>

#include <algorithm>

using namespace Qt::StringLiterals;

namespace brulion::shell {

namespace {

constexpr auto kExpandedKey = "ui/expandedFolders";

} // namespace

FolderTreeModel::FolderTreeModel(QObject *parent) : QAbstractListModel(parent)
{
    // Coalesce bursts of vault changes (Syncthing delivering many files) into one refresh.
    m_refreshTimer.setSingleShot(true);
    m_refreshTimer.setInterval(80);
    connect(&m_refreshTimer, &QTimer::timeout, this, &FolderTreeModel::refresh);
}

FolderTreeModel::~FolderTreeModel() = default;

void FolderTreeModel::setWorkspace(Workspace *workspace)
{
    if (m_workspace == workspace)
        return;
    if (m_workspace)
        disconnect(m_workspace, nullptr, this, nullptr);
    m_workspace = workspace;
    if (m_workspace) {
        connect(m_workspace, &Workspace::openChanged, this, [this] {
            loadExpanded();
            refresh();
        });
        connect(m_workspace, &Workspace::filesChanged, this, [this] { m_refreshTimer.start(); });
    }
    loadExpanded();
    refresh();
    Q_EMIT workspaceChanged();
}

void FolderTreeModel::setShowAttachments(bool show)
{
    if (m_showAttachments == show)
        return;
    m_showAttachments = show;
    refresh();
    Q_EMIT showAttachmentsChanged();
}

int FolderTreeModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : int(m_rows.size());
}

QVariant FolderTreeModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_rows.size())
        return {};
    const Row &row = m_rows.at(index.row());
    const Entry &e = row.entry;
    switch (role) {
    case NameRole:
        return e.name;
    case TitleRole:
    case Qt::DisplayRole:
        return e.isDir || !paths::isMarkdown(e.path) ? e.name : paths::pageTitle(e.path);
    case PathRole:
        return e.path;
    case IsDirRole:
        return e.isDir;
    case DepthRole:
        return row.depth;
    case ExpandedRole:
        return e.isDir && m_expanded.contains(e.path);
    case HasChildrenRole:
        return e.isDir && !m_children.value(e.path).isEmpty();
    case IconRole:
        if (e.isDir)
            return m_expanded.contains(e.path) ? u"folder-open"_s : u"folder"_s;
        if (paths::isMarkdown(e.path))
            return u"file-text"_s;
        return paths::isPicturePath(e.path) ? u"image"_s : u"file"_s;
    default:
        return {};
    }
}

QHash<int, QByteArray> FolderTreeModel::roleNames() const
{
    return {
        { NameRole, "name" },       { TitleRole, "title" },       { PathRole, "path" },
        { IsDirRole, "isDir" },     { DepthRole, "depth" },       { ExpandedRole, "expanded" },
        { HasChildrenRole, "hasChildren" }, { IconRole, "fileIcon" },
    };
}

void FolderTreeModel::rebuildChildren()
{
    m_children.clear();
    const Vault *vault = m_workspace ? m_workspace->vault() : nullptr;
    if (!vault)
        return;
    // Folders that hold only attachments ("assets") are noise in a page tree; empty folders
    // stay (the user just created them).
    QSet<QString> withPages;
    QSet<QString> withAttachments;
    if (!m_showAttachments) {
        for (const FileStat &f : vault->files()) {
            QSet<QString> &set = paths::isMarkdown(f.path) ? withPages : withAttachments;
            for (QString dir = paths::dirname(f.path); !dir.isEmpty(); dir = paths::dirname(dir))
                set.insert(dir);
        }
    }
    for (const FileStat &f : vault->folders()) {
        if (!m_showAttachments && withAttachments.contains(f.path) && !withPages.contains(f.path))
            continue;
        m_children[paths::dirname(f.path)].append({ paths::basename(f.path), f.path, true });
    }
    for (const FileStat &f : vault->files()) {
        if (!m_showAttachments && !paths::isMarkdown(f.path))
            continue;
        if (paths::isConflictCopy(f.path))
            continue; // surfaced in the sync center, not as pages
        m_children[paths::dirname(f.path)].append({ paths::basename(f.path), f.path, false });
    }
    for (auto it = m_children.begin(); it != m_children.end(); ++it) {
        std::sort(it->begin(), it->end(), [](const Entry &a, const Entry &b) {
            if (a.isDir != b.isDir)
                return a.isDir;
            const int c = QString::compare(a.name, b.name, Qt::CaseInsensitive);
            return c != 0 ? c < 0 : a.name < b.name;
        });
    }
}

QList<FolderTreeModel::Row> FolderTreeModel::rowsUnder(const QString &folder, int depth) const
{
    QList<Row> out;
    for (const Entry &e : m_children.value(folder)) {
        out.append({ e, depth });
        if (e.isDir && m_expanded.contains(e.path))
            out.append(rowsUnder(e.path, depth + 1));
    }
    return out;
}

void FolderTreeModel::refresh()
{
    m_refreshTimer.stop();
    rebuildChildren();
    const QList<Row> rows = rowsUnder(QString(), 0);
    // Same shape (a file's content changed, or nothing visible changed): no reset needed.
    const bool same = rows.size() == m_rows.size()
                      && std::equal(rows.cbegin(), rows.cend(), m_rows.cbegin(), [](const Row &a, const Row &b) {
                             return a.depth == b.depth && a.entry.path == b.entry.path && a.entry.isDir == b.entry.isDir;
                         });
    if (same) {
        if (!m_rows.isEmpty())
            Q_EMIT dataChanged(index(0), index(int(m_rows.size()) - 1), { HasChildrenRole });
        return;
    }
    beginResetModel();
    m_rows = rows;
    endResetModel();
    Q_EMIT countChanged();
}

void FolderTreeModel::toggle(int row)
{
    if (row < 0 || row >= m_rows.size() || !m_rows.at(row).entry.isDir)
        return;
    const QString path = m_rows.at(row).entry.path;
    setExpanded(path, !m_expanded.contains(path));
}

void FolderTreeModel::setExpanded(const QString &folder, bool expanded)
{
    if (m_expanded.contains(folder) == expanded)
        return;
    if (expanded)
        m_expanded.insert(folder);
    else
        m_expanded.remove(folder);
    saveExpanded();

    const int row = indexOf(folder);
    if (row < 0)
        return; // not visible: only remembered
    if (expanded) {
        const QList<Row> children = rowsUnder(folder, m_rows.at(row).depth + 1);
        if (!children.isEmpty()) {
            beginInsertRows({}, row + 1, row + int(children.size()));
            for (qsizetype i = 0; i < children.size(); ++i)
                m_rows.insert(row + 1 + i, children.at(i));
            endInsertRows();
        }
    } else {
        int last = row;
        while (last + 1 < m_rows.size() && m_rows.at(last + 1).depth > m_rows.at(row).depth)
            ++last;
        if (last > row) {
            beginRemoveRows({}, row + 1, last);
            m_rows.remove(row + 1, last - row);
            endRemoveRows();
        }
    }
    Q_EMIT dataChanged(index(row), index(row), { ExpandedRole, IconRole });
    Q_EMIT countChanged();
}

int FolderTreeModel::reveal(const QString &path)
{
    QString folder = paths::dirname(path);
    QStringList chain;
    while (!folder.isEmpty()) {
        chain.prepend(folder);
        folder = paths::dirname(folder);
    }
    for (const QString &f : std::as_const(chain))
        setExpanded(f, true);
    return indexOf(path);
}

void FolderTreeModel::collapseAll()
{
    if (m_expanded.isEmpty())
        return;
    m_expanded.clear();
    saveExpanded();
    beginResetModel();
    m_rows = rowsUnder(QString(), 0);
    endResetModel();
    Q_EMIT countChanged();
}

int FolderTreeModel::indexOf(const QString &path) const
{
    for (qsizetype i = 0; i < m_rows.size(); ++i) {
        if (m_rows.at(i).entry.path == path)
            return int(i);
    }
    return -1;
}

void FolderTreeModel::loadExpanded()
{
    m_expanded.clear();
    const Vault *vault = m_workspace ? m_workspace->vault() : nullptr;
    if (!vault)
        return;
    const QJsonArray list = vault->store()->get(QString::fromLatin1(kExpandedKey)).toArray();
    for (const QJsonValue &v : list) {
        if (v.isString())
            m_expanded.insert(v.toString());
    }
}

void FolderTreeModel::saveExpanded()
{
    Vault *vault = m_workspace ? m_workspace->vault() : nullptr;
    if (!vault)
        return;
    QStringList list(m_expanded.cbegin(), m_expanded.cend());
    list.sort();
    vault->store()->set(QString::fromLatin1(kExpandedKey), QJsonArray::fromStringList(list));
}

} // namespace brulion::shell
