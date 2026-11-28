#include "terminalthread.h"

#include "terminalgroup.h"
#include "terminalregistry.h"
#include "terminalsession.h"

#include <QFileInfo>

#include <algorithm>

namespace brulion::terminal {

TerminalThread::TerminalThread(const QString &key, TerminalRegistry *registry)
    : QAbstractListModel(registry), m_key(key), m_registry(registry)
{
}

TerminalThread::~TerminalThread()
{
    // The groups and their sessions are children: the sessions' Pty destructors hang up.
    for (TerminalGroup *group : std::as_const(m_groups))
        disconnect(group, nullptr, this, nullptr);
}

void TerminalThread::setKey(const QString &key)
{
    if (key == m_key)
        return;
    m_key = key;
    Q_EMIT keyChanged();
}

void TerminalThread::setFolder(const QString &folder)
{
    if (folder == m_folder)
        return;
    m_folder = folder;
    // (Terminals that run keep their folder: only new ones start in this one.)
    for (TerminalSession *session : sessions()) {
        if (!session->isRunning() && !session->hasExited())
            session->setFolder(folder);
    }
    Q_EMIT folderChanged();
}

int TerminalThread::sessionCount() const
{
    int n = 0;
    for (const TerminalGroup *group : m_groups)
        n += group->count();
    return n;
}

void TerminalThread::setCurrentIndex(int index)
{
    index = m_groups.isEmpty() ? -1 : std::clamp(index, 0, int(m_groups.size()) - 1);
    if (index == m_current)
        return;
    m_current = index;
    Q_EMIT currentChanged();
    Q_EMIT currentSessionChanged();
}

TerminalSession *TerminalThread::currentSession() const
{
    const TerminalGroup *g = current();
    return g ? g->focusSession() : nullptr;
}

bool TerminalThread::isBusy() const
{
    return std::ranges::any_of(m_groups, &TerminalGroup::isBusy);
}

TerminalGroup *TerminalThread::group(int index) const
{
    return index >= 0 && index < m_groups.size() ? m_groups.at(index) : nullptr;
}

int TerminalThread::indexOf(TerminalGroup *group) const
{
    return int(m_groups.indexOf(group));
}

QList<TerminalSession *> TerminalThread::sessions() const
{
    QList<TerminalSession *> out;
    for (const TerminalGroup *group : m_groups)
        out += group->sessions();
    return out;
}

void TerminalThread::setError(const QString &error)
{
    if (error == m_error)
        return;
    m_error = error;
    Q_EMIT errorChanged();
}

void TerminalThread::setAutoOpen(bool on)
{
    if (on == m_autoOpen)
        return;
    m_autoOpen = on;
    Q_EMIT autoOpenChanged();
}

TerminalSession *TerminalThread::makeSession(const QString &folder, const QString &command, const QString &label,
                                             int pending)
{
    if (!TerminalSession::available()) {
        setError(tr("There is no terminal on this device."));
        return nullptr;
    }
    if (m_registry && !m_registry->makeRoom(this, pending)) {
        setError(tr("%n terminal(s) are open already: close some of them first.", "", m_registry->maxSessions()));
        return nullptr;
    }
    setError(QString());
    auto *session = new TerminalSession;
    session->setFolder(folder);
    session->setCommand(command);
    session->setLabel(label);
    connect(session, &TerminalSession::busyChanged, this, &TerminalThread::updateBusy);
    return session;
}

void TerminalThread::insertGroup(int index, TerminalGroup *group)
{
    index = std::clamp(index, 0, int(m_groups.size()));
    group->setParent(this);
    connect(group, &TerminalGroup::stateChanged, this, [this, group] { groupChanged(group); });
    connect(group, &TerminalGroup::countChanged, this, [this, group] {
        groupChanged(group);
        Q_EMIT countChanged();
    });
    connect(group, &TerminalGroup::focusChanged, this, [this, group] {
        if (group == current())
            Q_EMIT currentSessionChanged();
    });
    beginInsertRows(QModelIndex(), index, index);
    m_groups.insert(index, group);
    endInsertRows();
    if (m_current >= index)
        ++m_current; // (the same tab, one further)
    Q_EMIT countChanged();
}

void TerminalThread::removeGroup(int index)
{
    if (index < 0 || index >= m_groups.size())
        return;
    const int current = m_current;
    beginRemoveRows(QModelIndex(), index, index);
    TerminalGroup *group = m_groups.takeAt(index);
    endRemoveRows();
    disconnect(group, nullptr, this, nullptr);
    group->deleteLater();
    // The tab beside it is shown: the next one, the one before it when it was the last.
    int next = current;
    if (index < current)
        next = current - 1;
    else if (index == current)
        next = std::min(index, int(m_groups.size()) - 1);
    m_current = -2; // (announced: the tab at the index is another one)
    setCurrentIndex(next);
    Q_EMIT countChanged();
}

void TerminalThread::groupChanged(TerminalGroup *group)
{
    const qsizetype row = m_groups.indexOf(group);
    if (row < 0)
        return;
    const QModelIndex at = index(int(row));
    Q_EMIT dataChanged(at, at);
}

TerminalSession *TerminalThread::openTab(const QString &command, const QString &label)
{
    TerminalSession *session = makeSession(m_folder, command, label);
    if (!session)
        return nullptr;
    auto *group = new TerminalGroup(this);
    group->insert(0, session);
    insertGroup(m_current + 1, group);
    setCurrentIndex(indexOf(group));
    session->start();
    updateBusy();
    Q_EMIT sessionsChanged();
    return session;
}

TerminalSession *TerminalThread::split(Qt::Orientation orientation)
{
    TerminalGroup *group = current();
    if (!group)
        return openTab(QString(), QString());
    if (!group->canSplit()) {
        setError(tr("A tab holds %n terminal(s) at most.", "", TerminalGroup::MaxSessions));
        return nullptr;
    }
    const TerminalSession *beside = group->focusSession();
    // Where the terminal beside it is now (its shell may have gone elsewhere since).
    const QString folder = beside && beside->isRunning() ? beside->currentDirectory() : m_folder;
    TerminalSession *session = makeSession(folder, QString(), QString());
    if (!session)
        return nullptr;
    // (One direction a tab: the last split decides it.)
    group->setOrientation(orientation);
    group->insert(group->focusIndex() + 1, session);
    session->start();
    updateBusy();
    Q_EMIT sessionsChanged();
    return session;
}

bool TerminalThread::restoreTab(const QList<Restored> &terminals, Qt::Orientation orientation, int focus,
                                const QList<qreal> &shares)
{
    auto *group = new TerminalGroup(this);
    QList<std::pair<TerminalSession *, QString>> started;
    for (const Restored &r : terminals) {
        if (group->count() >= TerminalGroup::MaxSessions)
            break;
        const QString folder = !r.folder.isEmpty() && QFileInfo(r.folder).isDir() ? r.folder : m_folder;
        // (A command's tab: its shell, the command typed and waiting for Enter — never run by itself.)
        TerminalSession *session = makeSession(folder, QString(), r.label.isEmpty() ? r.command.simplified().left(80) : r.label,
                                               group->count());
        if (!session)
            break;
        group->insert(group->count(), session);
        started.append({session, r.command});
    }
    if (group->count() == 0) {
        delete group;
        return false;
    }
    group->setOrientation(orientation);
    group->setFocusIndex(focus);
    group->setShares(shares);
    insertGroup(count(), group);
    if (m_current < 0)
        setCurrentIndex(0);
    for (const auto &[session, command] : std::as_const(started)) {
        if (!command.isEmpty())
            session->typeWhenReady(command);
        session->start();
    }
    updateBusy();
    Q_EMIT sessionsChanged();
    return true;
}

void TerminalThread::endSession(TerminalSession *session)
{
    disconnect(session, nullptr, this, nullptr);
    session->setParent(nullptr);
    session->stop();
    // Later: QML may still hold it for the rest of this event (the view, the tab being clicked).
    session->deleteLater();
}

void TerminalThread::closeSession(TerminalSession *session, bool byUser)
{
    if (!session)
        return;
    for (int i = 0; i < m_groups.size(); ++i) {
        TerminalGroup *group = m_groups.at(i);
        if (group->indexOf(session) < 0)
            continue;
        group->take(session);
        endSession(session);
        if (group->count() == 0)
            removeGroup(i);
        if (byUser && m_groups.isEmpty())
            setAutoOpen(false);
        updateBusy();
        Q_EMIT sessionsChanged();
        return;
    }
}

void TerminalThread::closeTab(int index, bool byUser)
{
    TerminalGroup *group = this->group(index);
    if (!group)
        return;
    const QList<TerminalSession *> sessions = group->sessions();
    for (TerminalSession *session : sessions)
        closeSession(session, byUser);
}

void TerminalThread::focusNext(int delta)
{
    // Every terminal in order: the tabs' panes one after the other.
    QList<std::pair<int, int>> order;
    int at = -1;
    for (int g = 0; g < m_groups.size(); ++g) {
        for (int p = 0; p < m_groups.at(g)->count(); ++p) {
            if (g == m_current && p == m_groups.at(g)->focusIndex())
                at = int(order.size());
            order.append({g, p});
        }
    }
    if (order.isEmpty())
        return;
    const int n = int(order.size());
    const int next = at < 0 ? 0 : ((at + delta) % n + n) % n;
    const auto [g, p] = order.at(next);
    m_groups.at(g)->setFocusIndex(p);
    setCurrentIndex(g);
}

int TerminalThread::closeIdle()
{
    int ended = 0;
    for (TerminalSession *session : sessions()) {
        if (session->isBusy())
            continue;
        closeSession(session, false);
        ++ended;
    }
    return ended;
}

void TerminalThread::closeAll()
{
    const QList<TerminalSession *> all = sessions();
    // Every program hung up on first, so that the waits for their ends overlap.
    for (TerminalSession *session : all)
        session->stop();
    for (TerminalSession *session : all)
        closeSession(session, false);
}

void TerminalThread::updateBusy()
{
    const bool busy = isBusy();
    if (busy == m_busy)
        return;
    m_busy = busy;
    Q_EMIT busyChanged();
}

int TerminalThread::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : int(m_groups.size());
}

QVariant TerminalThread::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() >= m_groups.size())
        return {};
    TerminalGroup *group = m_groups.at(index.row());
    switch (role) {
    case GroupRole: return QVariant::fromValue(group);
    case Qt::DisplayRole:
    case TitleRole: return group->title();
    case ActivityRole: return group->activity();
    case BellRole: return group->bellRung();
    case BusyRole: return group->isBusy();
    case RunningRole: return group->isRunning();
    case HasExitedRole: return group->hasExited();
    case ExitCodeRole: return group->exitCode();
    case CountRole: return group->count();
    default: return {};
    }
}

QHash<int, QByteArray> TerminalThread::roleNames() const
{
    return {{GroupRole, "group"},       {TitleRole, "title"},       {ActivityRole, "activity"},
            {BellRole, "bellRung"},     {BusyRole, "busy"},         {RunningRole, "running"},
            {HasExitedRole, "hasExited"}, {ExitCodeRole, "exitCode"}, {CountRole, "paneCount"}};
}

} // namespace brulion::terminal
