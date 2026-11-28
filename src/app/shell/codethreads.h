// CodeThreads — Code mode's threads settled and snoozed, as T3 Code does (docs/CODE.md "Settling and
// snoozing"; QML `Workspace.codeThreads`). What a thread's page says (core codethreads: `settled:`,
// `snoozed:`, `auto-settle: never` in its frontmatter) is written here; the sidebar's sections
// (ChatListModel) read it.
//
//   Workspace.codeThreads.settle(path)              // "Settle": done for now — the Settled section
//   Workspace.codeThreads.unsettle(path)
//   Workspace.codeThreads.snooze(path, "3h")        // hidden until then ("1h", "3h", "evening",
//                                                   // "tomorrow", "monday", or words: "jutro o 18")
//   Workspace.codeThreads.wake(path)
//   Workspace.codeThreads.setNeverAutoSettle(path, true)
//
// Settling (by the user or by itself) tells settledChanged(path, true): the thread's idle terminals
// end, its scripts with `stopOnSettle` stop and those with `runOnSettle` run in its worktree
// (terminalwiring.cpp, TerminalRegistry::settle()). A new message of a thread takes its settling and
// snooze away (WorkspaceChats writes it with codethreads::reactivated()).
//
// By itself: every hour, and when the notebook is ready, a thread untouched for autoSettleDays (a
// device setting, default 7, 0 never) settles unless something holds it (WorkspaceChats::heldBack():
// a turn running, a question, a failure, unread, queued messages, a draft, a sub-conversation that
// will wake it, on screen) or its page says `auto-settle: never` — autoSettled(paths).
#pragma once

#include "chat/chat.h"

#include <QObject>
#include <QSet>
#include <QString>
#include <QStringList>
#include <QTimer>
#include <QVariantMap>

namespace brulion::shell {

class Workspace;
class WorkspaceChats;

class CodeThreads : public QObject {
    Q_OBJECT
    /// Days without activity after which a thread settles by itself (0 never). A device setting
    /// (AppSettings `code/autoSettleDays`).
    Q_PROPERTY(int autoSettleDays READ autoSettleDays WRITE setAutoSettleDays NOTIFY autoSettleDaysChanged FINAL)
    /// When a settled thread's worktree is removed by itself (codethreads::cleanupOf(): "never" — the
    /// default —, "merged", "7", "30"). A device setting (AppSettings `code/worktreeCleanup`).
    Q_PROPERTY(QString worktreeCleanup READ worktreeCleanup WRITE setWorktreeCleanup NOTIFY worktreeCleanupChanged FINAL)

public:
    static constexpr int DefaultAutoSettleDays = 7;

    CodeThreads(Workspace *workspace, WorkspaceChats *chats, QObject *parent = nullptr);

    /// Settles the thread `path` now / takes it out of Settled. False for a page that is not a
    /// thread of Code mode (no `project:`) or not there.
    Q_INVOKABLE bool settle(const QString &path);
    Q_INVOKABLE bool unsettle(const QString &path);
    /// Snoozes it until `preset`'s end (codethreads::snoozeEnd()); "" when done, else why not.
    Q_INVOKABLE QString snooze(const QString &path, const QString &preset);
    Q_INVOKABLE bool wake(const QString &path);
    Q_INVOKABLE bool setNeverAutoSettle(const QString &path, bool never);
    /// {settled ("yyyy-MM-dd HH:mm" | ""), snoozedUntil (the same), snoozed (still ahead), never}.
    Q_INVOKABLE QVariantMap state(const QString &path) const;
    /// When a snooze preset would end, in words for the menu ("" none): "18:00", "jutro 9:00" …
    Q_INVOKABLE QString snoozeLabel(const QString &preset) const;

    int autoSettleDays() const { return m_autoSettleDays; }
    void setAutoSettleDays(int days);
    /// Settles every thread untouched for autoSettleDays(): the paths (autoSettled() follows when
    /// there are any).
    QStringList settleUntouched();
    QString worktreeCleanup() const { return m_cleanup; }
    void setWorktreeCleanup(const QString &cleanup);
    /// Every thread with a worktree of its own on this device: its pull request asked of gh — merged
    /// or closed, it settles (while autoSettleDays() > 0) —, then, settled, its worktree removed when
    /// worktreeCleanup() says so (codethreads::removesWorktree(): no uncommitted change, no other
    /// thread at work there). Runs with settleUntouched(). How many threads it looks at.
    int tidy();

Q_SIGNALS:
    void autoSettleDaysChanged();
    /// `path` was settled (`on`) or taken out of Settled by settle() / unsettle() / auto-settling.
    void settledChanged(const QString &path, bool on);
    /// `path` was snoozed or woken.
    void snoozedChanged(const QString &path);
    /// `days` 0: settled because their pull request was merged or closed.
    void autoSettled(const QStringList &paths, int days);
    void worktreeCleanupChanged();
    /// The worktree `folder` of the settled thread `path` was removed by itself (its branch stays).
    void worktreeCleaned(const QString &path, const QString &folder);
    /// tidy() looked at every thread it started with.
    void tidied();

private:
    /// settle() (`on`) / unsettle().
    bool markSettled(const QString &path, bool on);
    /// The page of a thread (`project:` set) — "" with `ok` false for anything else.
    QString threadContent(const QString &path, bool *ok) const;
    /// Writes `edit(content)` of the thread `path`; false when it is no thread or the write failed.
    template<typename Edit>
    bool edit(const QString &path, Edit edit);

    Workspace *m_workspace;
    WorkspaceChats *m_chats;
    /// tidy()'s answers for one thread: its pull request's state, then its worktree's changes.
    void pullRequestKnown(const QString &path, const QString &state);
    void changesKnown(const QString &path, const QString &state, int changes);
    void tidyDone();
    /// The threads of this device's projects that have a worktree and branch of their own.
    QList<std::pair<QString, chat::Conversation>> worktreeThreads() const;
    /// When a thread last changed: its last message's time, else its page's (`mtime`).
    static QDateTime lastActivity(const chat::Conversation &conversation, qint64 mtime);
    /// Another thread than `path` works in `folder` and is not settled.
    bool othersActiveIn(const QString &path, const QString &folder) const;

    int m_autoSettleDays = DefaultAutoSettleDays;
    QString m_cleanup = QStringLiteral("never");
    int m_tidying = 0;
    /// Worktrees being removed (one removal of a folder two settled threads share).
    QSet<QString> m_removing;
    QTimer m_timer; // every hour: settleUntouched() and tidy() (each does nothing when off)
};

} // namespace brulion::shell
