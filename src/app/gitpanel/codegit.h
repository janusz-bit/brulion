// CodeGit — one folder's git for the "Code" mode's changes panel (qml/gitpanel/GitChangesPanel.qml,
// docs/CODE-GIT.md): the branch, its upstream, its pull request and the working tree's changes kept
// up to date, other comparisons (a turn, the branch against a base, a commit), a file's diff, files
// and hunks staged or thrown away, commits of chosen files, push / pull, branches, pull requests
// (gh, forks), publishing, merges and rebases in progress, stashes, worktrees and the checkpoints of
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
#include <QVariantMap>

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

    /// The paths of `changes` (a rename's new path), and those with something staged.
    Q_PROPERTY(QStringList changePaths READ changePaths NOTIFY stateChanged FINAL)
    Q_PROPERTY(QStringList stagedPaths READ stagedPaths NOTIFY stateChanged FINAL)
    Q_PROPERTY(int conflictCount READ conflictCount NOTIFY stateChanged FINAL)
    /// The conflicted files' paths.
    Q_PROPERTY(QStringList conflictPaths READ conflictPaths NOTIFY stateChanged FINAL)
    /// A merge, rebase … stopped in this work tree: "merge" | "rebase" | "cherryPick" | "revert" |
    /// "apply" ("" none).
    Q_PROPERTY(QString operation READ operation NOTIFY stateChanged FINAL)
    /// The repository's default branch ("main"; "" unknown) and the ref to compare with
    /// ("origin/main", else "main").
    Q_PROPERTY(QString defaultBranch READ defaultBranch NOTIFY stateChanged FINAL)
    Q_PROPERTY(QString defaultBranchRef READ defaultBranchRef NOTIFY stateChanged FINAL)
    /// The branch checked out is the default branch (the panel asks before pushing to it).
    Q_PROPERTY(bool onDefaultBranch READ onDefaultBranch NOTIFY stateChanged FINAL)
    /// The repository has a remote (`remoteNames`).
    Q_PROPERTY(bool hasRemote READ hasRemote NOTIFY stateChanged FINAL)
    Q_PROPERTY(QStringList remoteNames READ remoteNames NOTIFY stateChanged FINAL)
    /// The branch's pull request (gh): {number, url, state ("open" | "draft" | "merged" |
    /// "closed"), title, baseRefName}; {} none or unknown. Asked when the branch or its upstream
    /// changes, after a push or a pull request, and on refreshPullRequest().
    Q_PROPERTY(QVariantMap pullRequest READ pullRequest NOTIFY pullRequestChanged FINAL)
    /// The main action now (T3 Code's quick action): "commit" (changes), "push" (ahead, or not
    /// pushed), "pull" (behind), "createPr" (pushed, no pull request, not the default branch),
    /// "openPr" (it has one), "publish" (no remote), "init" (no repository); "" nothing to do.
    Q_PROPERTY(QString primaryAction READ primaryAction NOTIFY revisionChanged FINAL)

    /// A comparison of two revisions (compare()): its changes, as `changes`' rows.
    Q_PROPERTY(bool compareActive READ compareActive NOTIFY compareChanged FINAL)
    Q_PROPERTY(QString compareBase READ compareBase NOTIFY compareChanged FINAL)
    Q_PROPERTY(QString compareTarget READ compareTarget NOTIFY compareChanged FINAL)
    /// The base compared with: compareBase, or with compareMergeBase the commit the branch and
    /// it have in common ("" the empty tree).
    Q_PROPERTY(QString comparisonBase READ comparisonBase NOTIFY compareChanged FINAL)
    Q_PROPERTY(QVariantList compareChanges READ compareChanges NOTIFY compareChanged FINAL)
    Q_PROPERTY(bool compareLoaded READ compareLoaded NOTIFY compareChanged FINAL)
    Q_PROPERTY(QString compareError READ compareError NOTIFY compareChanged FINAL)
    /// After commits(): the branch's newest commits [{oid, shortOid, subject, author, time, when,
    /// parentOid}] (`when`: "5 minutes ago"; `parentOid` "" for a root commit).
    Q_PROPERTY(QVariantList commitList READ commitList NOTIFY commitListChanged FINAL)
    /// After stashes(): [{index, oid, message, branch, time, when}], the newest first.
    Q_PROPERTY(QVariantList stashList READ stashList NOTIFY stashListChanged FINAL)

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
    QStringList changePaths() const { return m_changePaths; }
    QStringList stagedPaths() const { return m_stagedPaths; }
    int conflictCount() const { return int(m_conflictPaths.size()); }
    QStringList conflictPaths() const { return m_conflictPaths; }
    QString operation() const { return m_operation; }
    QString defaultBranch() const { return m_defaultBranch; }
    QString defaultBranchRef() const { return m_defaultBranchRef; }
    bool onDefaultBranch() const { return !m_branch.isEmpty() && m_branch == m_defaultBranch; }
    bool hasRemote() const { return !m_remoteNames.isEmpty(); }
    QStringList remoteNames() const { return m_remoteNames; }
    QVariantMap pullRequest() const { return m_pullRequest; }
    QString primaryAction() const;
    bool compareActive() const { return m_compareOn; }
    QString compareBase() const { return m_compareBase; }
    QString compareTarget() const { return m_compareTarget; }
    QString comparisonBase() const { return m_comparisonBase; }
    QVariantList compareChanges() const { return m_compareChanges; }
    bool compareLoaded() const { return m_compareLoaded; }
    QString compareError() const { return m_compareError; }
    QVariantList commitList() const { return m_commitList; }
    QVariantList stashList() const { return m_stashList; }

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
    Q_INVOKABLE bool isValidRepositoryName(const QString &name) const;
    /// A branch name made of a commit message's first line ("fix-the-sync-conflict"; "" none).
    Q_INVOKABLE QString branchNameFor(const QString &message) const;
    /// The row of `rows` (changes, compareChanges) for `path` ({} none).
    Q_INVOKABLE QVariantMap rowOf(const QVariantList &rows, const QString &path) const;
    /// How commitFiles() commits these paths: "all" | "staged" | "paths" ("" none chosen).
    Q_INVOKABLE QString commitModeFor(const QStringList &paths) const;
    /// Why `action` cannot run now ("" it can): "commit", "push", "pull", "createPr", "publish",
    /// "init", "stash" — for a disabled entry's tooltip.
    Q_INVOKABLE QString unavailableReason(const QString &action) const;

    // ---- Diff sources (docs/CODE-GIT.md "The panel") ----------------------------------------------

    /// What compareChanges lists: `base` → `target` (both revisions, or a checkpoint's ref;
    /// `base` "" the empty tree, `target` "" the work tree now), or with `mergeBase` the commit
    /// HEAD and `base` have in common → `target` (the branch's changes against a base branch).
    /// Kept up to date with the refreshes; `on` false: none.
    Q_INVOKABLE void compare(bool on, const QString &base, const QString &target, bool mergeBase);
    /// A file's texts in two revisions (`target` "" the work tree; `base` "" nothing):
    /// diffReady() / diffFailed() as requestDiff().
    Q_INVOKABLE void requestDiffBetween(const QString &path, const QString &oldPath, const QString &base,
                                        const QString &target);
    /// Fills commitList: the newest 100 commits of HEAD.
    Q_INVOKABLE void commits();

    // ---- Files and hunks ----------------------------------------------------------------------------

    /// actionFinished("stage" / "unstage", …).
    Q_INVOKABLE void stage(const QStringList &paths);
    Q_INVOKABLE void unstage(const QStringList &paths);
    /// A file's changes thrown away (gitcli::Client::discard: new files to the trash):
    /// actionFinished("discard", …).
    Q_INVOKABLE void discard(const QString &path, const QString &oldPath);
    /// A conflicted file settled: `how` "ours" | "theirs" | "resolved": actionFinished("resolve", …).
    Q_INVOKABLE void resolve(const QString &path, const QString &how);
    /// The hunks that change the work tree's lines [first, last] staged or thrown away, or those
    /// that change HEAD's lines [first, last] unstaged: actionFinished("stageHunk" /
    /// "discardHunk" / "unstageHunk", …).
    Q_INVOKABLE void stageHunk(const QString &path, int first, int last);
    Q_INVOKABLE void unstageHunk(const QString &path, int first, int last);
    Q_INVOKABLE void discardHunk(const QString &path, int first, int last);

    // ---- Commits, pull requests, publishing ---------------------------------------------------------

    /// A commit of the chosen files (T3 Code's commit dialog), then as commit() pushed and a pull
    /// request opened (into `prBase`, "" the default). `paths`: every changed file → all of them
    /// (`git add -A`); exactly the staged ones → the index as it is (partly staged files stay
    /// so); else those files as they are in the work tree, new ones added (`git commit --only`).
    /// `newBranch` ("" none) is made and switched to first. Refused when a chosen file holds
    /// conflict markers. actionFinished("commit", ok, message, the pull request's url).
    Q_INVOKABLE void commitFiles(const QString &message, const QStringList &paths, const QString &newBranch, bool push,
                                 bool createPr, const QString &prTitle, const QString &prBody, bool draft,
                                 const QString &prBase);
    /// createPullRequest() into `base` ("" the default branch); from a fork (an "upstream"
    /// remote) with --repo and --head owner:branch.
    Q_INVOKABLE void createPullRequestTo(const QString &title, const QString &body, bool draft, const QString &base);
    /// The branch's pull request asked again (gh pr view): pullRequest.
    Q_INVOKABLE void refreshPullRequest();
    /// `git init` in `folder`: actionFinished("init", …).
    Q_INVOKABLE void initRepository();
    /// A GitHub repository for this one (gh repo create … --source --push): actionFinished("publish",
    /// ok, message, its address).
    Q_INVOKABLE void publish(const QString &name, bool isPublic);
    /// The work tree without what `base` → `target` changed (an older turn: its checkpoint and the
    /// next one): actionFinished("revertTurn", …).
    Q_INVOKABLE void revertBetween(const QString &base, const QString &target);

    // ---- Merges, rebases, stashes -------------------------------------------------------------------

    /// The operation in progress aborted / continued: actionFinished("abort" / "continue", …).
    Q_INVOKABLE void abortOperation();
    Q_INVOKABLE void continueOperation();
    /// Fills stashList.
    Q_INVOKABLE void stashes();
    /// actionFinished("stash" / "stashApply" / "stashPop" / "stashDrop", …); the list follows.
    Q_INVOKABLE void stashPush(const QString &message);
    Q_INVOKABLE void stashApply(const QString &oid);
    Q_INVOKABLE void stashPop(const QString &oid);
    Q_INVOKABLE void stashDrop(const QString &oid);

    // ---- The diff view's rows (gitdiffrows.h) -------------------------------------------------------

    /// The turns of a thread's checkpoints (WorkspaceChats.checkpoints(): [{id, at, message}],
    /// oldest first), the newest first: [{index, id, number, label ("Turn 3"), at, latest, base,
    /// target}] — `base` the turn's checkpoint's ref, `target` the next one's ("" for the latest:
    /// the work tree), so compare(true, base, target, false) shows what that turn alone changed.
    Q_INVOKABLE QVariantList turnRows(const QVariantList &checkpoints) const;
    Q_INVOKABLE QVariantList diffRows(const QString &oldText, const QString &newText, bool split, bool ignoreWhitespace,
                                      int context) const;
    Q_INVOKABLE QList<int> findRows(const QVariantList &rows, const QString &query) const;
    Q_INVOKABLE int longestLine(const QVariantList &rows) const;
    Q_INVOKABLE QString lineHtml(const QString &text, const QVariantList &segs, const QString &changeColor,
                                 const QString &query, const QString &findColor, bool wrap) const;
    Q_INVOKABLE QVariantList treeRows(const QVariantList &changes, const QStringList &collapsed) const;
    Q_INVOKABLE QString reviewComment(const QString &path, const QVariantList &rows, int from, int to,
                                      const QString &comment) const;

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
    void pullRequestChanged();
    void compareChanged();
    void commitListChanged();
    void stashListChanged();

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
    /// compareChanges for the comparison set (then `then`).
    void loadCompare(std::function<void()> then);
    /// The default branch and the remotes (when the branch changes; after actions).
    void loadRepoFacts();
    /// An action whose body is one gitcli call giving an Outcome.
    void simpleAction(const QString &action,
                      std::function<void(const QString &root, std::function<void(const gitcli::Outcome &)> done)> body);
    /// The commit() / commitFiles() tail: pushed, a pull request opened.
    void afterCommit(const QString &root, const gitcli::Outcome &committed, bool push, bool createPr,
                     const QString &message, const QString &prTitle, const QString &prBody, bool draft,
                     const QString &prBase);
    /// A pull request opened (the fork found first): finishAction(action, …) with its url.
    void openPullRequest(const QString &action, const QString &root, const gitcli::PullRequest &pr,
                         const QString &failurePrefix);

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
    QStringList m_changePaths;
    QStringList m_stagedPaths;
    QStringList m_conflictPaths;
    QString m_operation;
    QString m_defaultBranch;
    QString m_defaultBranchRef;
    QStringList m_remoteNames;
    /// The branch / upstream the facts and the pull request were last asked for.
    QString m_factsBranch;
    bool m_factsKnown = false;
    QString m_prBranch;
    QVariantMap m_pullRequest;
    int m_prSerial = 0;
    bool m_compareOn = false;
    QString m_compareBase;
    QString m_compareTarget;
    bool m_compareMergeBase = false;
    QString m_comparisonBase;
    QVariantList m_compareChanges;
    bool m_compareLoaded = false;
    QString m_compareError;
    int m_compareSerial = 0;
    QVariantList m_commitList;
    QVariantList m_stashList;

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
