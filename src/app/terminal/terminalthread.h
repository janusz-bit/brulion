// TerminalThread — the terminals of one Code mode thread (docs/CODE-TERMINAL.md "Threads, tabs and
// splits"): its tabs (TerminalGroup, each up to four terminals side by side or stacked), which one
// is shown, the folder new terminals start in. TerminalRegistry keeps one per thread, keyed by the
// thread's page path (a new thread's by its folder until its first message makes the page), so a
// thread's shells, their scrollback and the programs in them stay while another thread is shown.
//
//   TerminalThread *t = TerminalRegistry::instance()->thread(u"AI/2026-11-26 Fix.md"_s, folder);
//   t->openTab(u"npm run dev"_s, tr("Dev server"));   // a new tab running it, shown
//   t->split(Qt::Horizontal);                          // a shell beside the focused terminal
//   t->focusNext(1);                                   // the next terminal: pane, then tab
//
// Every new session asks the registry for room first (TerminalRegistry::MaxSessions, other threads'
// idle terminals let go): openTab() and split() give null when there is none, `error` says why.
// Roles of the tabs: group, title, activity, bellRung, busy, running, hasExited, exitCode, count.
#pragma once

#include "terminalgroup.h"
#include "terminalsession.h"

#include <QAbstractListModel>
#include <QList>
#include <QString>

namespace brulion::terminal {

class TerminalRegistry;

class TerminalThread : public QAbstractListModel {
    Q_OBJECT
    /// The thread's page path, or a new thread's folder (TerminalRegistry's key).
    Q_PROPERTY(QString key READ key NOTIFY keyChanged FINAL)
    /// Where new terminals start (the thread's folder: its worktree, else its project's).
    Q_PROPERTY(QString folder READ folder WRITE setFolder NOTIFY folderChanged FINAL)
    /// Tabs.
    Q_PROPERTY(int count READ count NOTIFY countChanged FINAL)
    /// Terminals in all tabs.
    Q_PROPERTY(int sessionCount READ sessionCount NOTIFY countChanged FINAL)
    /// The tab shown (-1: none).
    Q_PROPERTY(int currentIndex READ currentIndex WRITE setCurrentIndex NOTIFY currentChanged FINAL)
    Q_PROPERTY(brulion::terminal::TerminalGroup *current READ current NOTIFY currentChanged FINAL)
    /// The shown tab's focused terminal.
    Q_PROPERTY(brulion::terminal::TerminalSession *currentSession READ currentSession NOTIFY currentSessionChanged FINAL)
    /// A program runs in one of its terminals.
    Q_PROPERTY(bool busy READ isBusy NOTIFY busyChanged FINAL)
    /// A shell may open by itself when the panel shows the thread without terminals: false once
    /// the user closed its last one (the panel then offers "New terminal").
    Q_PROPERTY(bool autoOpen READ autoOpen NOTIFY autoOpenChanged FINAL)
    /// Why the last openTab() / split() made nothing ("" when it did).
    Q_PROPERTY(QString error READ error NOTIFY errorChanged FINAL)

public:
    enum Role {
        GroupRole = Qt::UserRole + 1,
        TitleRole,
        ActivityRole,
        BellRole,
        BusyRole,
        RunningRole,
        HasExitedRole,
        ExitCodeRole,
        CountRole,
    };

    explicit TerminalThread(const QString &key, TerminalRegistry *registry);
    ~TerminalThread() override;

    QString key() const { return m_key; }
    QString folder() const { return m_folder; }
    void setFolder(const QString &folder);
    int count() const { return int(m_groups.size()); }
    int sessionCount() const;
    int currentIndex() const { return m_current; }
    void setCurrentIndex(int index);
    TerminalGroup *current() const { return group(m_current); }
    TerminalSession *currentSession() const;
    bool isBusy() const;
    bool autoOpen() const { return m_autoOpen; }
    QString error() const { return m_error; }

    Q_INVOKABLE brulion::terminal::TerminalGroup *group(int index) const;
    Q_INVOKABLE int indexOf(brulion::terminal::TerminalGroup *group) const;
    /// All its sessions, tab by tab, pane by pane.
    QList<TerminalSession *> sessions() const;

    /// A new tab after the shown one with a session running `command` ("": the user's shell; a
    /// command through it), named `label` until the program sets a title; shown and started. Null
    /// when there is no room (error()).
    Q_INVOKABLE brulion::terminal::TerminalSession *openTab(const QString &command, const QString &label);
    /// A shell beside (Qt.Horizontal) or under (Qt.Vertical) the shown tab's focused terminal,
    /// focused and started — in the folder that terminal is in, as T3 Code does. Null when the tab
    /// is full (TerminalGroup::MaxSessions) or there is no room; a thread without tabs gets one.
    Q_INVOKABLE brulion::terminal::TerminalSession *split(Qt::Orientation orientation);
    /// Ends `session` (SIGHUP to its program) and takes it out; a tab left empty goes. `byUser`:
    /// the user closed it (the last one of the thread: autoOpen goes off).
    Q_INVOKABLE void closeSession(brulion::terminal::TerminalSession *session, bool byUser);
    /// Ends every terminal of tab `index`.
    Q_INVOKABLE void closeTab(int index, bool byUser);
    /// The focus `delta` terminals on, through the panes of the shown tab, then the next tab's
    /// (wrapping around).
    Q_INVOKABLE void focusNext(int delta);
    /// A terminal of a tab brought back after a restart (TerminalRegistry's layouts).
    struct Restored {
        QString folder;
        QString label;
        QString command;
    };
    /// A tab at the end with these terminals (shells in their folders — a missing one: the
    /// thread's —, a command typed into its shell, not run), side by side or stacked, `focus`
    /// focused, each its part of `shares` (none, or not one a terminal: equal parts). False when none
    /// could be made.
    bool restoreTab(const QList<Restored> &terminals, Qt::Orientation orientation, int focus,
                    const QList<qreal> &shares = {});
    /// Ends the terminals in which no program runs (busy ones stay); how many ended.
    int closeIdle();
    /// Ends all of them now.
    void closeAll();

    void setKey(const QString &key);

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

Q_SIGNALS:
    void keyChanged();
    void folderChanged();
    void countChanged();
    void currentChanged();
    void currentSessionChanged();
    void busyChanged();
    void autoOpenChanged();
    void errorChanged();
    /// A session was made or ended (TerminalRegistry counts them).
    void sessionsChanged();

private:
    TerminalSession *makeSession(const QString &folder, const QString &command, const QString &label, int pending = 0);
    void insertGroup(int index, TerminalGroup *group);
    void removeGroup(int index);
    void groupChanged(TerminalGroup *group);
    void endSession(TerminalSession *session);
    void setError(const QString &error);
    void setAutoOpen(bool on);
    void updateBusy();

    QString m_key;
    QString m_folder;
    TerminalRegistry *m_registry;
    QList<TerminalGroup *> m_groups;
    int m_current = -1;
    bool m_busy = false;
    bool m_autoOpen = true;
    QString m_error;
};

} // namespace brulion::terminal
