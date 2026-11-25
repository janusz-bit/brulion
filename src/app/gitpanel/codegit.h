// CodeGit — one folder's git for the "Code" mode's changes panel (qml/gitpanel/GitChangesPanel.qml,
// docs/CODE-GIT.md): the branch, its upstream and the working tree's changes kept up to date, a
// file's diff, commit / push / pull, branches, pull requests (gh), worktrees and the checkpoints of
// an agent's turns. Everything runs asynchronously through gitcli::Client (src/core/gitcli); the
// results come as properties and signals, never as calls into the rest of the app.
//
//   CodeGit { id: git; folder: project.folder; turnCheckpoint: thread.lastCheckpoint }
//   git.changes            [{ path, oldPath, status, letter, staged, unstaged, additions, deletions,
//                             binary, name, dir }]  (status: "modified" | "added" | "deleted" |
//                             "renamed" | "untracked" | "conflicted")
//   git.requestDiff(path, "")              → diffReady(path, oldText, newText, binary, tooLarge)
//   git.commit(message, push, createPr, prTitle, prBody, draft) → actionFinished("commit", ok, …)
//   const id = git.checkpoint()            → checkpointMade(id, ok, message) — the snapshot exists
//                                             only then: start the agent's turn after it
//
// `folder` may be any folder inside a work tree (a repository's or a linked worktree's); the work
// tree's top folder (`topLevel`) is what every command runs in. refresh() runs when `folder`
// changes, after every action, when git changes HEAD or the index (a QFileSystemWatcher on this
// work tree's git folder — `.git`, or `.git/worktrees/<name>` where `.git` is a file — and on the
// top folder's own entries, 300 ms after the first change) and every `pollInterval` ms (0: never).
// Refreshes never overlap (one asked for during another runs after it); the actions run one after
// another in the order asked (`busy`, `busyAction`), so a restore never meets a checkpoint.
#pragma once

#include "gitcli/client.h"

#include <QFileSystemWatcher>
#include <QList>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QTimer>
#include <QVariantList>

#include <functional>

namespace brulion::gitpanel {

class CodeGit : public QObject {
    Q_OBJECT
    /// The folder (a repository's work tree or a worktree, or a folder inside one).
    Q_PROPERTY(QString folder READ folder WRITE setFolder NOTIFY folderChanged FINAL)
    /// A checkpoint whose changes `turnChanges` lists ("" none), kept up to date with the rest.
    Q_PROPERTY(QString turnCheckpoint READ turnCheckpoint WRITE setTurnCheckpoint NOTIFY turnCheckpointChanged FINAL)
    /// A refresh every so many milliseconds (0: only on changes git makes and on request).
    Q_PROPERTY(int pollInterval READ pollInterval WRITE setPollInterval NOTIFY pollIntervalChanged FINAL)

    /// The first refresh of this folder is done (before it, isRepo says nothing).
    Q_PROPERTY(bool loaded READ loaded NOTIFY stateChanged FINAL)
    Q_PROPERTY(bool isRepo READ isRepo NOTIFY stateChanged FINAL)
    /// The work tree's top folder ("" when no repository).
    Q_PROPERTY(QString topLevel READ topLevel NOTIFY stateChanged FINAL)
    /// The branch checked out ("" when detached).
    Q_PROPERTY(QString branch READ branch NOTIFY stateChanged FINAL)
    /// Its upstream ("origin/main"; "" none).
    Q_PROPERTY(QString upstream READ upstream NOTIFY stateChanged FINAL)
    Q_PROPERTY(int ahead READ ahead NOTIFY stateChanged FINAL)
    Q_PROPERTY(int behind READ behind NOTIFY stateChanged FINAL)
    Q_PROPERTY(bool detached READ detached NOTIFY stateChanged FINAL)
    /// HEAD is a commit (false in a new repository).
    Q_PROPERTY(bool hasCommits READ hasCommits NOTIFY stateChanged FINAL)
    /// HEAD's commit ("" none).
    Q_PROPERTY(QString headOid READ headOid NOTIFY stateChanged FINAL)
    /// The working tree's changes against HEAD (at most maxRows rows), staged or not.
    Q_PROPERTY(QVariantList changes READ changes NOTIFY stateChanged FINAL)
    /// How many there are (also beyond maxRows).
    Q_PROPERTY(int changeCount READ changeCount NOTIFY stateChanged FINAL)
    /// The changes since `turnCheckpoint` (the same rows; staged is always false there).
    Q_PROPERTY(QVariantList turnChanges READ turnChanges NOTIFY turnChangesChanged FINAL)
    /// `turnChanges` is the answer for the checkpoint set.
    Q_PROPERTY(bool turnLoaded READ turnLoaded NOTIFY turnChangesChanged FINAL)
    /// Why there are no `turnChanges` ("" when they came): the checkpoint is gone, no repository…
    Q_PROPERTY(QString turnError READ turnError NOTIFY turnChangesChanged FINAL)
    /// After branches(): [{ name, remote, current, upstream, ahead, behind, gone, worktreePath }] —
    /// the current branch first, then local ones, then remote ones no local branch tracks.
    Q_PROPERTY(QVariantList branchList READ branchList NOTIFY branchListChanged FINAL)
    /// After worktrees(): [{ path, branch, head, bare, detached, locked, prunable, current }].
    Q_PROPERTY(QVariantList worktreeList READ worktreeList NOTIFY worktreeListChanged FINAL)
    /// An action runs or waits (commit, push, pull, a branch, a checkpoint, a restore, …).
    Q_PROPERTY(bool busy READ busy NOTIFY busyChanged FINAL)
    /// The action running ("" none): the names actionFinished() gives.
    Q_PROPERTY(QString busyAction READ busyAction NOTIFY busyChanged FINAL)
    Q_PROPERTY(bool refreshing READ refreshing NOTIFY refreshingChanged FINAL)
    /// The last failure: of an action (until one succeeds), else of the last refresh.
    Q_PROPERTY(QString lastError READ lastError NOTIFY lastErrorChanged FINAL)
    /// Bumps whenever any of the state above changes.
    Q_PROPERTY(int revision READ revision NOTIFY revisionChanged FINAL)
    Q_PROPERTY(bool gitAvailable READ gitAvailable NOTIFY toolsChanged FINAL)
    /// The GitHub CLI is installed (pull requests).
    Q_PROPERTY(bool ghAvailable READ ghAvailable NOTIFY toolsChanged FINAL)
    Q_PROPERTY(int maxRows READ maxRows CONSTANT FINAL)

public:
    explicit CodeGit(QObject *parent = nullptr);
    ~CodeGit() override;

    static constexpr int MaxRows = 2000;

    QString folder() const { return m_folder; }
    void setFolder(const QString &folder);
    QString turnCheckpoint() const { return m_turnCheckpoint; }
    void setTurnCheckpoint(const QString &id);
    int pollInterval() const { return m_pollInterval; }
    void setPollInterval(int ms);

    bool loaded() const { return m_loaded; }
    bool isRepo() const { return m_info.isRepo; }
    QString topLevel() const { return m_info.topLevel; }
    QString branch() const { return m_branch; }
    QString upstream() const { return m_upstream; }
    int ahead() const { return m_ahead; }
    int behind() const { return m_behind; }
    bool detached() const { return m_detached; }
    bool hasCommits() const { return m_hasCommits; }
    QString headOid() const { return m_headOid; }
    QVariantList changes() const { return m_changes; }
    int changeCount() const { return m_changeCount; }
    QVariantList turnChanges() const { return m_turnChanges; }
    bool turnLoaded() const { return m_turnLoaded; }
    QString turnError() const { return m_turnError; }
    QVariantList branchList() const { return m_branchList; }
    QVariantList worktreeList() const { return m_worktreeList; }
    bool busy() const { return m_taskRunning || !m_queue.isEmpty(); }
    QString busyAction() const { return m_currentAction; }
    bool refreshing() const { return m_refreshRunning; }
    QString lastError() const { return m_actionError.isEmpty() ? m_refreshError : m_actionError; }
    int revision() const { return m_revision; }
    bool gitAvailable() const { return !m_client.gitProgram().isEmpty(); }
    bool ghAvailable() const { return !m_client.ghProgram().isEmpty(); }
    static int maxRows() { return MaxRows; }

    /// The client (C++: its programs and time limits, for tests).
    gitcli::Client &client() { return m_client; }
    /// Other git / gh programs ("" none); `gitAvailable` / `ghAvailable` follow.
    void setGitProgram(const QString &program);
    void setGhProgram(const QString &program);

    /// The state again (coalesced: one asked for while one runs follows it).
    Q_INVOKABLE void refresh();
    /// A file's old text — in HEAD (`checkpoint` "") or in the checkpoint — and its text now:
    /// diffReady() or diffFailed(). A renamed row's old text comes from its old path.
    Q_INVOKABLE void requestDiff(const QString &path, const QString &checkpoint);
    /// Everything staged (`git add -A`) and committed with `message`; then pushed (`push` or
    /// `createPr`), then a pull request opened (`createPr`; `prTitle` "" = the message's first
    /// line): actionFinished("commit", ok, message, the pull request's url).
    Q_INVOKABLE void commit(const QString &message, bool push, bool createPr, const QString &prTitle,
                            const QString &prBody, bool draft);
    /// actionFinished("push" / "pull", …).
    Q_INVOKABLE void push();
    Q_INVOKABLE void pull();
    /// Fills branchList (and branchesReady()).
    Q_INVOKABLE void branches();
    /// A new branch from HEAD, switched to (the working tree's changes come along):
    /// actionFinished("createBranch", …).
    Q_INVOKABLE void createBranch(const QString &name);
    /// actionFinished("checkout", …); a remote branch ("origin/x") gets a local one tracking it.
    Q_INVOKABLE void checkout(const QString &name);
    /// Pushed first when the branch has no upstream or is ahead of it, then `gh pr create`:
    /// actionFinished("pullRequest", ok, message, url).
    Q_INVOKABLE void createPullRequest(const QString &title, const QString &body, bool draft);
    /// A checkpoint of the working tree: its id at once, the snapshot after the actions asked
    /// before it — checkpointMade(id, ok, message) says it exists (a failure also comes as
    /// actionFinished("checkpoint", false, …)).
    Q_INVOKABLE QString checkpoint();
    /// The changes since `checkpoint`: changesSinceReady() (and turnChanges, when it is the
    /// turn's checkpoint).
    Q_INVOKABLE void changesSince(const QString &checkpoint);
    /// The working tree back to `checkpoint` (gitcli::Client::restore): actionFinished("restore", …).
    Q_INVOKABLE void restore(const QString &checkpoint);
    /// The checkpoints' refs deleted: actionFinished("dropCheckpoints", …).
    Q_INVOKABLE void dropCheckpoints(const QStringList &ids);
    /// Fills worktreeList (and worktreesReady()).
    Q_INVOKABLE void worktrees();
    /// `git worktree add [-b branch] <path> [base]`: actionFinished("addWorktree", ok, message,
    /// the new worktree's folder in `url`).
    Q_INVOKABLE void addWorktree(const QString &path, const QString &branch, const QString &base);
    /// actionFinished("removeWorktree", …); without `force` git refuses one with changes.
    Q_INVOKABLE void removeWorktree(const QString &path, bool force);

    /// The first non-blank line of a message, trimmed (a pull request's default title).
    Q_INVOKABLE QString subjectOf(const QString &message) const;
    Q_INVOKABLE bool isValidBranchName(const QString &name) const;

Q_SIGNALS:
    void folderChanged();
    void turnCheckpointChanged();
    void pollIntervalChanged();
    void stateChanged();
    void turnChangesChanged();
    void branchListChanged();
    void worktreeListChanged();
    void busyChanged();
    void refreshingChanged();
    void lastErrorChanged();
    void revisionChanged();
    void toolsChanged();

    /// requestDiff()'s answer: the texts ("" when binary or tooLarge).
    void diffReady(const QString &path, const QString &oldText, const QString &newText, bool binary, bool tooLarge);
    void diffFailed(const QString &path, const QString &message);
    /// An action ended: "commit", "push", "pull", "createBranch", "checkout", "pullRequest",
    /// "restore", "dropCheckpoints", "addWorktree", "removeWorktree" (and a failed "checkpoint").
    /// `message` is for the user; `url` a pull request's address (addWorktree: the new folder).
    void actionFinished(const QString &action, bool ok, const QString &message, const QString &url);
    void checkpointMade(const QString &id, bool ok, const QString &message);
    void changesSinceReady(const QString &checkpoint, const QVariantList &rows);
    void branchesReady(const QVariantList &rows);
    void worktreesReady(const QVariantList &rows);

private:
    struct Task {
        QString action;
        std::function<void()> run;
        /// A checkpoint's task: its id (checkpointMade() when it is dropped).
        QString checkpointId;
    };
    /// Runs `run` (which ends with finishAction() or taskDone()) after the actions before it.
    void enqueue(const QString &action, std::function<void()> run, const QString &checkpointId = QString());
    /// An action in the repository's top folder (`body` ends with finishAction()); it fails
    /// without running when there is no repository or the folder changed in the meantime.
    void runAction(const QString &action, std::function<void(const QString &root)> body);
    void startNext();
    void taskDone();
    /// actionFinished(), lastError, the next action and a refresh.
    void finishAction(const QString &action, bool ok, const QString &message, const QString &url);
    /// `done(true)` with m_info for the current folder (resolved again when unknown).
    void withRepo(std::function<void(bool isRepo)> done);
    void endRefresh();
    void applyStatus(const gitcli::StatusReport &report);
    void applyTurn(const QString &checkpoint, const gitcli::ChangesReport &report);
    /// No repository (any more): the state emptied, `error` as the refresh's.
    void setNotRepo(const QString &error, bool wasRepo);
    void setRefreshError(const QString &error);
    void setActionError(const QString &error);
    void updateWatches();
    void bump();

    gitcli::Client m_client;
    QFileSystemWatcher m_watcher;
    QTimer m_debounce;
    QTimer m_poll;

    QString m_folder;
    QString m_turnCheckpoint;
    int m_pollInterval = 0;
    /// Bumps with the folder: answers for an older one are dropped.
    int m_generation = 0;
    gitcli::RepoInfo m_info;
    QString m_infoFolder;

    bool m_loaded = false;
    QString m_branch;
    QString m_upstream;
    int m_ahead = 0;
    int m_behind = 0;
    bool m_detached = false;
    bool m_hasCommits = false;
    QString m_headOid;
    QVariantList m_changes;
    int m_changeCount = 0;
    QVariantList m_turnChanges;
    bool m_turnLoaded = false;
    QString m_turnError;
    QVariantList m_branchList;
    QVariantList m_worktreeList;

    QList<Task> m_queue;
    bool m_taskRunning = false;
    QString m_currentAction;
    bool m_refreshRunning = false;
    bool m_refreshAgain = false;
    QString m_refreshError;
    QString m_actionError;
    int m_revision = 0;
};

} // namespace brulion::gitpanel
