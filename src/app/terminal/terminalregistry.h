// TerminalRegistry — every Code mode thread's terminals, kept outside the view (docs/CODE-TERMINAL.md
// "Threads, tabs and splits"; QML singleton `TerminalRegistry`). The panel shows one thread's
// (thread()), and when another thread is shown its terminals — tabs, splits, scrollback, the
// programs in them — stay here until that thread is shown again. Also across leaving Code mode:
// CodeView is made anew, the registry is not.
//
//   const thread = TerminalRegistry.thread(path !== "" ? path : folder, folder)  // made when missing
//   TerminalRegistry.shown(key)              // the panel shows it: the most recently shown now
//   TerminalRegistry.rekey(folder, newPath)  // a new thread's first message made its page
//   TerminalRegistry.isBusy(path)            // a program runs in its terminals (the sidebar's mark)
//
// Keys: a thread's page path ("AI/2026-11-26 Napraw testy.md"), a new thread's folder until its first
// message makes the page (then rekey()). The app's wiring (terminalwiring.cpp) follows the notebook:
// a thread renamed or moved — rekey(); deleted — close(); archived — closeIdle(), as T3 Code's
// settling does; the app quitting — hangUpAll().
//
// Limits: MaxThreads threads with terminals and MaxSessions terminals in all. Beyond them the
// least recently shown other threads' idle terminals end (shown(), makeRoom()); a terminal in which
// a program runs (TerminalSession::busy: a dev server, an editor, a build) is never ended to make
// room — with only such ones left the limit waits, and a new terminal beyond MaxSessions is refused
// (TerminalThread::error says so).
#pragma once

#include "terminalthread.h"

#include <QHash>
#include <QObject>
#include <QPointer>
#include <QString>
#include <QStringList>

namespace brulion::terminal {

class TerminalRegistry : public QObject {
    Q_OBJECT
    /// Keys of the threads that have terminals, the most recently shown first.
    Q_PROPERTY(QStringList keys READ keys NOTIFY threadsChanged FINAL)
    /// Keys of the threads in whose terminals a program runs.
    Q_PROPERTY(QStringList busyKeys READ busyKeys NOTIFY busyKeysChanged FINAL)
    Q_PROPERTY(int sessionCount READ sessionCount NOTIFY threadsChanged FINAL)
    /// Whether this device can run a terminal (platform::Pty::available()).
    Q_PROPERTY(bool available READ available CONSTANT FINAL)

public:
    /// Threads whose terminals are kept while not shown, at most (idle ones beyond end).
    static constexpr int MaxThreads = 10;
    /// Terminals in all threads, at most.
    static constexpr int MaxSessions = 40;

    explicit TerminalRegistry(QObject *parent = nullptr);
    ~TerminalRegistry() override;

    /// The app's registry (made on first use; QML's singleton is this one).
    static TerminalRegistry *instance();

    QStringList keys() const;
    QStringList busyKeys() const { return m_busy; }
    int sessionCount() const;
    static bool available();

    /// The terminals of `key` — made (without terminals) when there are none yet; `folder` where
    /// its new terminals start ("" leaves it as it is). Null for an empty key.
    Q_INVOKABLE brulion::terminal::TerminalThread *thread(const QString &key, const QString &folder);
    /// The terminals of `key`, null when it has none.
    Q_INVOKABLE brulion::terminal::TerminalThread *find(const QString &key) const;
    /// The panel shows `key` now ("": none): it becomes the most recently shown; beyond MaxThreads
    /// the least recently shown others' idle terminals end.
    Q_INVOKABLE void shown(const QString &key);
    /// `from`'s terminals are `to`'s now (a new thread's page, a renamed thread). False when `from`
    /// has none or `to` has its own already.
    Q_INVOKABLE bool rekey(const QString &from, const QString &to);
    /// Ends every terminal of `key` (the thread was deleted).
    Q_INVOKABLE void close(const QString &key);
    /// Ends the terminals of `key` in which no program runs (the thread was archived).
    Q_INVOKABLE void closeIdle(const QString &key);
    /// A program runs in a terminal of `key`.
    Q_INVOKABLE bool isBusy(const QString &key) const;
    /// Ends every terminal: every program hung up on at once, the sessions deleted later.
    Q_INVOKABLE void closeAll();
    /// SIGHUP to every program at once, the sessions kept (the app quits: deleting the registry,
    /// a child of the application, reaps them).
    void hangUpAll();

    /// Room for one more terminal of `asking`: true below MaxSessions, after ending idle terminals
    /// of the least recently shown other threads when needed.
    bool makeRoom(const TerminalThread *asking);
    /// Other limits than MaxThreads and MaxSessions (tests).
    void setLimits(int maxThreads, int maxSessions);
    int maxSessions() const { return m_maxSessions; }

    // The notebook's events (terminalwiring.cpp).
    void pathRenamed(const QString &oldPath, const QString &newPath);
    void pathRemoved(const QString &path);
    void archived(const QString &path, bool on);

Q_SIGNALS:
    void threadsChanged();
    void busyKeysChanged();
    /// Whether a program runs in the terminals of `key` changed.
    void busyChanged(const QString &key, bool busy);

private:
    void touch(const QString &key);
    void forget(TerminalThread *thread);
    void updateBusy(TerminalThread *thread);
    void enforceThreadLimit();
    void prune();
    QList<TerminalThread *> leastRecentFirst(const TerminalThread *except) const;

    QHash<QString, TerminalThread *> m_threads;
    QStringList m_order; // most recently shown first
    QString m_shown;
    QStringList m_busy;
    int m_maxThreads = MaxThreads;
    int m_maxSessions = MaxSessions;
};

} // namespace brulion::terminal
