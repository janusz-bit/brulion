#include "terminalgroup.h"

#include "terminalsession.h"

#include <algorithm>

namespace brulion::terminal {

TerminalGroup::TerminalGroup(QObject *parent) : QAbstractListModel(parent) { }

TerminalGroup::~TerminalGroup()
{
    // The sessions still in it are children: their Pty destructors hang up on the programs.
    for (TerminalSession *session : std::as_const(m_sessions))
        disconnect(session, nullptr, this, nullptr);
}

void TerminalGroup::setOrientation(Qt::Orientation orientation)
{
    if (orientation == m_orientation)
        return;
    m_orientation = orientation;
    Q_EMIT orientationChanged();
}

void TerminalGroup::setFocusIndex(int index)
{
    index = m_sessions.isEmpty() ? -1 : std::clamp(index, 0, int(m_sessions.size()) - 1);
    if (index == m_focus)
        return;
    m_focus = index;
    Q_EMIT focusChanged();
    Q_EMIT stateChanged();
}

TerminalSession *TerminalGroup::at(int index) const
{
    return index >= 0 && index < m_sessions.size() ? m_sessions.at(index) : nullptr;
}

int TerminalGroup::indexOf(TerminalSession *session) const
{
    return int(m_sessions.indexOf(session));
}

QString TerminalGroup::title() const
{
    const TerminalSession *session = focusSession();
    return session ? session->title() : QString();
}

bool TerminalGroup::activity() const
{
    return std::ranges::any_of(m_sessions, &TerminalSession::hasActivity);
}

bool TerminalGroup::bellRung() const
{
    return std::ranges::any_of(m_sessions, &TerminalSession::bellRung);
}

bool TerminalGroup::isBusy() const
{
    return std::ranges::any_of(m_sessions, &TerminalSession::isBusy);
}

bool TerminalGroup::isRunning() const
{
    return std::ranges::any_of(m_sessions, &TerminalSession::isRunning);
}

bool TerminalGroup::hasExited() const
{
    const TerminalSession *session = focusSession();
    return session && session->hasExited();
}

int TerminalGroup::exitCode() const
{
    const TerminalSession *session = focusSession();
    return session ? session->exitCode() : -1;
}

void TerminalGroup::insert(int index, TerminalSession *session)
{
    if (!session || m_sessions.contains(session))
        return;
    index = std::clamp(index, 0, int(m_sessions.size()));
    session->setParent(this);
    for (auto signal : {&TerminalSession::titleChanged, &TerminalSession::runningChanged,
                        &TerminalSession::busyChanged, &TerminalSession::marksChanged})
        connect(session, signal, this, [this, session] { sessionChanged(session); });
    beginInsertRows(QModelIndex(), index, index);
    m_sessions.insert(index, session);
    endInsertRows();
    Q_EMIT countChanged();
    // The new one has the keyboard (the index shifts even when the focused session stays).
    m_focus = -1;
    setFocusIndex(index);
}

void TerminalGroup::take(TerminalSession *session)
{
    const int row = indexOf(session);
    if (row < 0)
        return;
    disconnect(session, nullptr, this, nullptr);
    const int focus = m_focus;
    beginRemoveRows(QModelIndex(), row, row);
    m_sessions.removeAt(row);
    endRemoveRows();
    Q_EMIT countChanged();
    // The one beside it (the one before it when it was the last), or the same one moved up.
    int next = focus;
    if (row < focus)
        next = focus - 1;
    else if (row == focus)
        next = std::min(row, int(m_sessions.size()) - 1);
    m_focus = -2; // (always announced: the session at the index changed; -1 when none is left)
    setFocusIndex(next);
}

int TerminalGroup::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : int(m_sessions.size());
}

QVariant TerminalGroup::data(const QModelIndex &index, int role) const
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
    case BusyRole: return session->isBusy();
    case ActivityRole: return session->hasActivity();
    case BellRole: return session->bellRung();
    default: return {};
    }
}

QHash<int, QByteArray> TerminalGroup::roleNames() const
{
    return {{SessionRole, "session"},   {TitleRole, "title"},   {RunningRole, "running"},
            {HasExitedRole, "hasExited"}, {ExitCodeRole, "exitCode"}, {BusyRole, "busy"},
            {ActivityRole, "activity"}, {BellRole, "bellRung"}};
}

void TerminalGroup::sessionChanged(TerminalSession *session)
{
    const qsizetype row = m_sessions.indexOf(session);
    if (row < 0)
        return;
    const QModelIndex at = index(int(row));
    Q_EMIT dataChanged(at, at);
    Q_EMIT stateChanged();
}

} // namespace brulion::terminal
