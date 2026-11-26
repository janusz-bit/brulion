// TerminalGroup — one tab of the terminal panel (docs/CODE-TERMINAL.md "Threads, tabs and splits"):
// up to MaxSessions terminals shown together, side by side (Qt::Horizontal: "split right") or
// stacked (Qt::Vertical: "split down"), one of them focused. TerminalThread owns its groups and
// makes and ends the sessions (it asks TerminalRegistry for room first); the group owns its
// sessions while they are in it.
//
//   Repeater { model: thread.current; delegate: TerminalPane { required property TerminalSession session } }
//   group.focusIndex = 1                              // the pane the keyboard is in
//   group.title; group.activity; group.busy           // the tab's title and marks
//
// The tab's title is the focused session's; its marks — output or a bell while not shown, a
// program running — any session's; its end (hasExited, exitCode) the focused session's.
// Roles: session, title, running, hasExited, exitCode, busy, activity, bellRung.
#pragma once

#include "terminalsession.h"

#include <QAbstractListModel>
#include <QList>

namespace brulion::terminal {

class TerminalGroup : public QAbstractListModel {
    Q_OBJECT
    Q_PROPERTY(int count READ count NOTIFY countChanged FINAL)
    /// Side by side (Qt.Horizontal) or stacked (Qt.Vertical).
    Q_PROPERTY(Qt::Orientation orientation READ orientation WRITE setOrientation NOTIFY orientationChanged FINAL)
    /// The session the keyboard goes to (-1: none, an empty group).
    Q_PROPERTY(int focusIndex READ focusIndex WRITE setFocusIndex NOTIFY focusChanged FINAL)
    Q_PROPERTY(brulion::terminal::TerminalSession *focusSession READ focusSession NOTIFY focusChanged FINAL)
    /// Another session fits (fewer than MaxSessions).
    Q_PROPERTY(bool canSplit READ canSplit NOTIFY countChanged FINAL)
    Q_PROPERTY(QString title READ title NOTIFY stateChanged FINAL)
    Q_PROPERTY(bool activity READ activity NOTIFY stateChanged FINAL)
    Q_PROPERTY(bool bellRung READ bellRung NOTIFY stateChanged FINAL)
    Q_PROPERTY(bool busy READ isBusy NOTIFY stateChanged FINAL)
    Q_PROPERTY(bool running READ isRunning NOTIFY stateChanged FINAL)
    Q_PROPERTY(bool hasExited READ hasExited NOTIFY stateChanged FINAL)
    Q_PROPERTY(int exitCode READ exitCode NOTIFY stateChanged FINAL)

public:
    /// Terminals in one tab, at most (T3 Code's limit).
    static constexpr int MaxSessions = 4;

    enum Role {
        SessionRole = Qt::UserRole + 1,
        TitleRole,
        RunningRole,
        HasExitedRole,
        ExitCodeRole,
        BusyRole,
        ActivityRole,
        BellRole,
    };

    explicit TerminalGroup(QObject *parent = nullptr);
    ~TerminalGroup() override;

    int count() const { return int(m_sessions.size()); }
    bool canSplit() const { return count() < MaxSessions; }
    Qt::Orientation orientation() const { return m_orientation; }
    void setOrientation(Qt::Orientation orientation);
    int focusIndex() const { return m_focus; }
    void setFocusIndex(int index);
    TerminalSession *focusSession() const { return at(m_focus); }
    QString title() const;
    bool activity() const;
    bool bellRung() const;
    bool isBusy() const;
    bool isRunning() const;
    bool hasExited() const;
    int exitCode() const;

    Q_INVOKABLE brulion::terminal::TerminalSession *at(int index) const;
    Q_INVOKABLE int indexOf(brulion::terminal::TerminalSession *session) const;
    const QList<TerminalSession *> &sessions() const { return m_sessions; }

    /// `session` at `index` (clamped), its parent now this group; it gets the focus.
    void insert(int index, TerminalSession *session);
    /// `session` out of the group (not deleted, its parent unchanged); the focus moves to the one
    /// beside it.
    void take(TerminalSession *session);

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

Q_SIGNALS:
    void countChanged();
    void orientationChanged();
    void focusChanged();
    /// The title, a mark, the running state or the end of a session changed.
    void stateChanged();

private:
    void sessionChanged(TerminalSession *session);

    QList<TerminalSession *> m_sessions;
    Qt::Orientation m_orientation = Qt::Horizontal;
    int m_focus = -1;
};

} // namespace brulion::terminal
