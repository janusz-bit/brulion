#include "workspacemodel.h"

#include "workspace.h"

namespace brulion::content {

WorkspaceListModel::WorkspaceListModel(QObject *parent) : QAbstractListModel(parent)
{
    m_timer.setSingleShot(true);
    connect(&m_timer, &QTimer::timeout, this, &WorkspaceListModel::refreshNow);
    // Row counts change through every kind of model signal: one place tells QML.
    const auto counted = [this] {
        const int n = rows();
        if (n != m_lastCount) {
            m_lastCount = n;
            Q_EMIT countChanged();
        }
    };
    connect(this, &QAbstractItemModel::rowsInserted, this, counted);
    connect(this, &QAbstractItemModel::rowsRemoved, this, counted);
    connect(this, &QAbstractItemModel::modelReset, this, counted);
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
    connect(m_workspace, &shell::Workspace::revisionChanged, this, [this] { scheduleRefresh(m_refreshDelay); });
    connect(m_workspace, &EditorHost::indexChanged, this, [this] { scheduleRefresh(m_refreshDelay); });
    // The index is complete (the vault was opened): show it on the next turn, not after the
    // debounce, which coalesces edits and index chunks. The shell's lists exist before the index
    // is done (the vault opens after the window's first frame), so a small vault's journal
    // would otherwise appear only 120 ms after its last chunk.
    connect(m_workspace, &shell::Workspace::readyChanged, this, [this] {
        if (m_workspace && m_workspace->ready())
            scheduleRefresh(0);
    });
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
    refresh();
    const int n = rows();
    if (n != m_lastCount) {
        m_lastCount = n;
        Q_EMIT countChanged();
    }
    Q_EMIT refreshed();
}

} // namespace brulion::content
