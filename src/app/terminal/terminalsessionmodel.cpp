#include "terminalsessionmodel.h"

#include "terminalsession.h"

namespace brulion::terminal {

TerminalSessionModel::TerminalSessionModel(QObject *parent) : QAbstractListModel(parent) { }

TerminalSessionModel::~TerminalSessionModel()
{
    // The sessions are children: their Pty destructors hang up on the programs.
}

bool TerminalSessionModel::available()
{
    return TerminalSession::available();
}

int TerminalSessionModel::add(const QString &folder, const QString &command, const QString &label)
{
    auto *session = new TerminalSession(this);
    session->setFolder(folder);
    session->setCommand(command);
    session->setLabel(label);
    connect(session, &TerminalSession::titleChanged, this, [this, session] { sessionChanged(session); });
    connect(session, &TerminalSession::runningChanged, this, [this, session] { sessionChanged(session); });
    const int row = int(m_sessions.size());
    beginInsertRows(QModelIndex(), row, row);
    m_sessions.append(session);
    endInsertRows();
    Q_EMIT countChanged();
    return row;
}

void TerminalSessionModel::remove(int row)
{
    if (row < 0 || row >= m_sessions.size())
        return;
    beginRemoveRows(QModelIndex(), row, row);
    TerminalSession *session = m_sessions.takeAt(row);
    endRemoveRows();
    disconnect(session, nullptr, this, nullptr);
    session->stop();
    // Later: QML may still hold it for the rest of this event (the view, the tab being clicked).
    session->deleteLater();
    Q_EMIT countChanged();
}

TerminalSession *TerminalSessionModel::at(int row) const
{
    return row >= 0 && row < m_sessions.size() ? m_sessions.at(row) : nullptr;
}

int TerminalSessionModel::indexOf(TerminalSession *session) const
{
    return int(m_sessions.indexOf(session));
}

int TerminalSessionModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : int(m_sessions.size());
}

QVariant TerminalSessionModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() >= m_sessions.size())
        return {};
    TerminalSession *session = m_sessions.at(index.row());
    switch (role) {
    case SessionRole: return QVariant::fromValue(session);
    case Qt::DisplayRole:
    case TitleRole: return session->title();
    case RunningRole: return session->isRunning();
    case HasExitedRole: return session->hasExited();
    case ExitCodeRole: return session->exitCode();
    default: return {};
    }
}

QHash<int, QByteArray> TerminalSessionModel::roleNames() const
{
    return {{SessionRole, "session"},
            {TitleRole, "title"},
            {RunningRole, "running"},
            {HasExitedRole, "hasExited"},
            {ExitCodeRole, "exitCode"}};
}

void TerminalSessionModel::sessionChanged(TerminalSession *session)
{
    const qsizetype row = m_sessions.indexOf(session);
    if (row < 0)
        return;
    const QModelIndex at = index(int(row));
    Q_EMIT dataChanged(at, at, {TitleRole, RunningRole, HasExitedRole, ExitCodeRole});
}

} // namespace brulion::terminal
