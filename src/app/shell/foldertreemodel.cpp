#include "foldertreemodel.h"

#include "workspace.h"

#include "markdown/journal.h"
#include "text/text.h"
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
        connect(m_workspace, &Workspace::configChanged, this, [this] { m_refreshTimer.start(); });
        connect(m_workspace, &Workspace::folderRenamed, this, &FolderTreeModel::onFolderRenamed);
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
        if (e.isDir && e.path == m_journalsFolder)
            return u"calendar-days"_s;
        if (e.isDir)
            return m_expanded.contains(e.path) ? u"folder-open"_s : u"folder"_s;
        if (!e.journalDate.isEmpty())
            return u"calendar"_s;
        if (paths::isMarkdown(e.path))
            return u"file-text"_s;
        return paths::isPicturePath(e.path) ? u"image"_s : u"file"_s;
    case SpecialRole: return e.isDir && e.path == m_journalsFolder ? u"journals"_s : QString();
    case JournalDateRole: return e.journalDate;
    default:
        return {};
    }
}

QHash<int, QByteArray> FolderTreeModel::roleNames() const
{
    return {
            {NameRole, "name"},
            {TitleRole, "title"},
            {PathRole, "path"},
            {IsDirRole, "isDir"},
            {DepthRole, "depth"},
            {ExpandedRole, "expanded"},
            {HasChildrenRole, "hasChildren"},
            {IconRole, "fileIcon"},
            {SpecialRole, "special"},
            {JournalDateRole, "journalDate"},
    };
}

void FolderTreeModel::rebuildChildren()
{
    m_children.clear();
    const Vault *vault = m_workspace ? m_workspace->vault() : nullptr;
    if (!vault)
        return;
    const VaultConfig config = vault->config();
    m_journalsFolder = paths::normalize(config.journalsFolder);
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
        m_children[paths::dirname(f.path)].append({paths::basename(f.path), f.path, true, {}});
    }
    for (const FileStat &f : vault->files()) {
        if (!m_showAttachments && !paths::isMarkdown(f.path))
            continue;
        if (paths::isConflictCopy(f.path))
            continue; // surfaced in the sync center, not as pages
        Entry e{paths::basename(f.path), f.path, false, {}};
        if (paths::isMarkdown(f.path)) {
            if (const auto date = markdown::journalDateFromPath(f.path, config))
                e.journalDate = date->toString(Qt::ISODate);
        }
        m_children[paths::dirname(f.path)].append(e);
    }
    const QString journals = m_journalsFolder;
    for (auto it = m_children.begin(); it != m_children.end(); ++it) {
        // In the journals folder: the newest first (years and months as folders too).
        const bool inJournals = !journals.isEmpty() && (it.key() == journals || it.key().startsWith(journals + u'/'));
        std::sort(it->begin(), it->end(), [&](const Entry &a, const Entry &b) {
            if (a.isDir != b.isDir)
                return a.isDir;
            // The journals folder first among its siblings.
            const bool ja = a.isDir && a.path == journals;
            const bool jb = b.isDir && b.path == journals;
            if (ja != jb)
                return ja;
            if (!a.journalDate.isEmpty() || !b.journalDate.isEmpty()) {
                if (a.journalDate.isEmpty() != b.journalDate.isEmpty())
                    return !a.journalDate.isEmpty();
                if (a.journalDate != b.journalDate)
                    return a.journalDate > b.journalDate;
            }
            const int c = text::compareNames(a.name, b.name);
            return inJournals && a.isDir ? c > 0 : c < 0;
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
    pruneExpanded();
    applyRows(rowsUnder(QString(), 0));
}

void FolderTreeModel::applyRows(QList<Row> next)
{
    const auto key = [](const Row &r) { return (r.entry.isDir ? u"d:"_s : u"f:"_s) + r.entry.path; };
    const qsizetype before = m_rows.size();
    if (m_rows.isEmpty() || next.isEmpty()) {
        if (m_rows.isEmpty() && next.isEmpty())
            return;
        beginResetModel();
        m_rows = std::move(next);
        endResetModel();
        Q_EMIT countChanged();
        return;
    }
    QHash<QString, qsizetype> wanted;
    wanted.reserve(next.size());
    for (qsizetype i = 0; i < next.size(); ++i)
        wanted.insert(key(next.at(i)), i);
    // The rows kept must keep their order (they do: one sort order); otherwise start anew.
    qsizetype lastPos = -1;
    for (const Row &r : std::as_const(m_rows)) {
        const auto it = wanted.constFind(key(r));
        if (it == wanted.cend())
            continue;
        if (*it <= lastPos) {
            beginResetModel();
            m_rows = std::move(next);
            endResetModel();
            Q_EMIT countChanged();
            return;
        }
        lastPos = *it;
    }
    // Removals, in contiguous runs from the end.
    for (qsizetype i = m_rows.size() - 1; i >= 0; --i) {
        if (wanted.contains(key(m_rows.at(i))))
            continue;
        qsizetype first = i;
        while (first > 0 && !wanted.contains(key(m_rows.at(first - 1))))
            --first;
        beginRemoveRows({}, int(first), int(i));
        m_rows.remove(first, i - first + 1);
        endRemoveRows();
        i = first;
    }
    // The rows left are in `next`'s order: insert the runs of new ones between them.
    QSet<QString> kept;
    kept.reserve(m_rows.size());
    for (const Row &r : std::as_const(m_rows))
        kept.insert(key(r));
    for (qsizetype i = 0; i < next.size(); ++i) {
        if (i < m_rows.size() && key(m_rows.at(i)) == key(next.at(i))) {
            m_rows[i] = next.at(i); // same path: the entry's data may have changed
            continue;
        }
        qsizetype last = i;
        while (last + 1 < next.size() && !kept.contains(key(next.at(last + 1))))
            ++last;
        beginInsertRows({}, int(i), int(last));
        for (qsizetype n = i; n <= last; ++n)
            m_rows.insert(n, next.at(n));
        endInsertRows();
        i = last;
    }
    if (!m_rows.isEmpty())
        Q_EMIT dataChanged(index(0), index(int(m_rows.size()) - 1),
                           {HasChildrenRole, ExpandedRole, IconRole, SpecialRole, JournalDateRole, TitleRole});
    if (m_rows.size() != before)
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
    if (m_refreshTimer.isActive())
        refresh();
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
    applyRows(rowsUnder(QString(), 0));
}

int FolderTreeModel::indexOf(const QString &path) const
{
    for (qsizetype i = 0; i < m_rows.size(); ++i) {
        if (m_rows.at(i).entry.path == path)
            return int(i);
    }
    return -1;
}

QVariantMap FolderTreeModel::get(int row) const
{
    if (row < 0 || row >= m_rows.size())
        return {};
    const Row &r = m_rows.at(row);
    return {{u"path"_s, r.entry.path},
            {u"isDir"_s, r.entry.isDir},
            {u"expanded"_s, r.entry.isDir && m_expanded.contains(r.entry.path)},
            {u"depth"_s, r.depth}};
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

void FolderTreeModel::pruneExpanded()
{
    const Vault *vault = m_workspace ? m_workspace->vault() : nullptr;
    if (!vault || m_expanded.isEmpty())
        return;
    QSet<QString> folders;
    for (const FileStat &f : vault->folders())
        folders.insert(f.path);
    const qsizetype before = m_expanded.size();
    m_expanded.removeIf([&folders](const QString &f) { return !folders.contains(f); });
    if (m_expanded.size() != before)
        saveExpanded();
}

void FolderTreeModel::onFolderRenamed(const QString &oldPath, const QString &newPath)
{
    QSet<QString> rebased;
    bool changed = false;
    for (const QString &f : std::as_const(m_expanded)) {
        if (f == oldPath || f.startsWith(oldPath + u'/')) {
            rebased.insert(newPath + f.mid(oldPath.size()));
            changed = true;
        } else {
            rebased.insert(f);
        }
    }
    if (!changed)
        return;
    m_expanded = rebased;
    saveExpanded();
    m_refreshTimer.start();
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
