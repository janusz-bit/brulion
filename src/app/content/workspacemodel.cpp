#include "workspacemodel.h"

#include "workspace.h"

#include "index/vaultindex.h"

namespace brulion::content {

WorkspaceListModel::WorkspaceListModel(QObject *parent) : QAbstractListModel(parent)
{
    m_timer.setSingleShot(true);
    connect(&m_timer, &QTimer::timeout, this, &WorkspaceListModel::refreshNow);
    // Row counts change through every kind of model signal: one place tells QML.
    connect(this, &QAbstractItemModel::rowsInserted, this, &WorkspaceListModel::publishCount);
    connect(this, &QAbstractItemModel::rowsRemoved, this, &WorkspaceListModel::publishCount);
    connect(this, &QAbstractItemModel::modelReset, this, &WorkspaceListModel::publishCount);
}

WorkspaceListModel::~WorkspaceListModel() = default;

QObject *WorkspaceListModel::workspaceObject() const
{
    return m_workspace.data();
}

shell::Workspace *WorkspaceListModel::workspace() const
{
    return m_workspace.data();
}

void WorkspaceListModel::setWorkspaceObject(QObject *workspace)
{
    setWorkspace(qobject_cast<shell::Workspace *>(workspace));
}

void WorkspaceListModel::setWorkspace(shell::Workspace *workspace)
{
    if (m_workspace == workspace)
        return;
    if (m_workspace)
        disconnect(m_workspace, nullptr, this, nullptr);
    m_workspace = workspace;
    connectWorkspace();
    workspaceReset();
    Q_EMIT workspaceChanged();
    scheduleRefresh(0);
}

void WorkspaceListModel::connectWorkspace()
{
    if (!m_workspace)
        return;
    connect(m_workspace, &shell::Workspace::openChanged, this, [this] {
        workspaceReset();
        scheduleRefresh(0);
    });
    connect(m_workspace, &shell::Workspace::revisionChanged, this, &WorkspaceListModel::contentChanged);
    connect(m_workspace, &EditorHost::indexChanged, this, &WorkspaceListModel::contentChanged);
    // The index is complete (the vault was opened): show it on the next turn, not after the
    // debounce, which coalesces edits and index chunks. The shell's lists exist before the index
    // is done (the vault opens after the window's first frame), so a small vault's journal
    // would otherwise appear only 120 ms after its last chunk.
    connect(m_workspace, &shell::Workspace::readyChanged, this, [this] {
        if (m_workspace && m_workspace->ready())
            scheduleRefresh(0);
    });
}

void WorkspaceListModel::setActive(bool active)
{
    if (m_active == active)
        return;
    m_active = active;
    Q_EMIT activeChanged();
    // What changed while hidden, at once: the view shown again shows it in its first frame.
    if (m_active && m_stale)
        refreshNow();
}

void WorkspaceListModel::contentChanged()
{
    if (!m_active && m_stale)
        return; // computed when shown, whatever else changes meanwhile
    if (unaffectedByIndexChanges())
        return;
    if (m_active)
        scheduleRefresh(m_refreshDelay);
    else
        m_stale = true;
}

void WorkspaceListModel::markShownChanged()
{
    if (m_active)
        scheduleRefresh(0);
    else
        m_stale = true;
}

bool WorkspaceListModel::affectedBy(const IndexChanges &changes) const
{
    Q_UNUSED(changes);
    return true;
}

bool WorkspaceListModel::unaffectedByIndexChanges()
{
    const VaultIndex *idx = vaultIndex();
    if (!idx || m_indexSerial != idx->serial() || m_indexVersion < 0)
        return false;
    // (The same version: no changed notes — whether that is nothing is the subclass's call.)
    const IndexChanges changes = idx->changesSince(m_indexVersion);
    if (!changes.complete || (changes.names && affectedByNames()) || affectedBy(changes))
        return false;
    m_indexVersion = idx->version(); // nothing the rows show changed up to here
    return true;
}

VaultIndex *WorkspaceListModel::vaultIndex() const
{
    return m_workspace ? m_workspace->index() : nullptr;
}

int WorkspaceListModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : rows();
}

void WorkspaceListModel::scheduleRefresh(int ms)
{
    if (m_timer.isActive() && m_timer.remainingTime() <= ms)
        return;
    m_timer.start(ms);
}

void WorkspaceListModel::refreshNow()
{
    m_timer.stop();
    m_stale = false;
    // (Before refresh(): whatever changes while it runs is seen by the next change's check.)
    const VaultIndex *idx = vaultIndex();
    m_indexSerial = idx ? idx->serial() : 0;
    m_indexVersion = idx ? idx->version() : -1;
    refresh();
    publishCount();
    Q_EMIT refreshed();
}

void WorkspaceListModel::publishCount()
{
    const int n = rows();
    if (n == m_lastCount)
        return;
    m_lastCount = n;
    Q_EMIT countChanged();
}

QVariantMap WorkspaceListModel::rowMap(int row) const
{
    QVariantMap out;
    if (row < 0 || row >= rows())
        return out;
    const QHash<int, QByteArray> names = roleNames();
    for (const auto &[role, name] : names.asKeyValueRange())
        out.insert(QString::fromLatin1(name), data(index(row), role));
    return out;
}

} // namespace brulion::content
