#pragma once
// gitcli::Client — git and the GitHub CLI (`gh`) run asynchronously for the "Code" mode
// (docs/CODE-GIT.md), on the parsers of gitcli.h.
//
//   gitcli::Client git;
//   git.status(folder, [](const gitcli::StatusReport &r) { … r.changes … });
//   git.checkpoint(folder, id, [](const gitcli::Outcome &o) { … });   // before an agent's turn
//   git.changesSince(folder, id, [](const gitcli::ChangesReport &r) { … });
//   git.restore(folder, id, [](const gitcli::RestoreReport &r) { … }); // the turn undone
//
// Every program runs with QProcess — never through a shell: the arguments are a list, a user's
// text (a commit message, a branch name, a path) never meets a command line's parsing — and never
// blocks the thread: the result comes to the callback, on the client's thread, after the call has
// returned (an early failure too). A callback never comes after the client is gone; destroying it
// kills what still runs. Each run has a time limit (killed after it).
//
// The environment of every run: the process's own, without the variables that would point git
// elsewhere (GIT_DIR, GIT_WORK_TREE, GIT_INDEX_FILE …), and GIT_TERMINAL_PROMPT=0 (git never waits
// for a password on a terminal: a push without credentials fails at once), LC_ALL=C (the messages
// that are read are git's English ones), GIT_PAGER=cat; read-only commands add GIT_OPTIONAL_LOCKS=0
// (a status never takes the index's lock from the agent's own git). Commands that run by
// themselves (status, diffs, branches, checkpoints) turn the repository's core.fsmonitor hook off.
// gh: GH_PROMPT_DISABLED=1, GH_NO_UPDATE_NOTIFIER=1, NO_COLOR=1, run in the folder.
//
// `folder` is a work tree's top-level folder (repository() finds it from any folder inside). The
// paths of the results are relative to it, '/'-separated, as git writes them.
//
// Checkpoints (checkpoint(), changesSince(), restore(), dropCheckpoints()): a snapshot of the
// whole work tree — untracked files included, ignored ones not — as a commit kept by the ref
// refs/brulion/checkpoints/<id>, made through a temporary index (GIT_INDEX_FILE, a copy of the
// real one, `git add -A`, `git write-tree`, `git commit-tree`), so the user's index, HEAD, branches
// and stash never move. restore() puts the work tree back to it (docs/CODE-GIT.md "Checkpoints").
//
// Where processes cannot run (WebAssembly: QT_CONFIG(process) off) every call fails with a message.

#include "gitcli/gitcli.h"

#include <QByteArray>
#include <QList>
#include <QObject>
#include <QPair>
#include <QString>
#include <QStringList>

#include <functional>
#include <memory>

namespace brulion::gitcli {

/// What one run of git or gh gave.
struct RunResult {
    int exitCode = -1;
    QByteArray out;
    /// Standard error as text (its first 256 KiB).
    QString err;
    /// Why it did not run to its end ("" when it did): not installed, did not start, timed out,
    /// crashed, wrote more than allowed.
    QString failure;
    /// Its standard output was longer than RunOptions::maxOutput (the run was stopped there).
    bool outputCut = false;
    bool ok() const { return failure.isEmpty() && exitCode == 0; }
    /// For the user: the failure, else the last lines of standard error ("fatal: " / "error: "
    /// taken off), else of standard output, else the exit code.
    QString message() const;
};

struct RunOptions {
    /// GIT_OPTIONAL_LOCKS=0 and no fsmonitor hook: a command that only reads.
    bool readOnly = false;
    /// The fsmonitor hook off without the rest of readOnly (commands that run by themselves).
    bool noHooks = false;
    /// Written to standard input, which is closed after it (always closed: nothing waits on it).
    QByteArray input;
    /// More environment (GIT_INDEX_FILE …), after the defaults.
    QList<QPair<QString, QString>> env;
    /// 0: the client's local limit (networkTimeoutMs() for push, pull and gh).
    int timeoutMs = 0;
    /// Standard output kept at most (the run is stopped beyond it, `outputCut`); 0: 256 MiB.
    qsizetype maxOutput = 0;
};

/// The result of an action (commit, push, a branch, a checkpoint …).
struct Outcome {
    bool ok = false;
    /// For the user: what was done, or why it failed.
    QString message;
    /// A pull request's address (createPullRequest()).
    QString url;
    /// What it made: a commit's id (commitAll(), checkpoint()).
    QString value;
};

/// Where a folder is in git (repository()).
struct RepoInfo {
    bool isRepo = false;
    QString error;
    /// The work tree's top-level folder.
    QString topLevel;
    /// This work tree's git folder (".git", or ".git/worktrees/<name>" of a linked worktree).
    QString gitDir;
    /// The repository's shared git folder (the main ".git").
    QString commonDir;
    bool bare = false;
};

/// One changed file, as the changes panel lists it.
struct FileChange {
    QString path;
    /// A rename or copy: where it came from.
    QString oldPath;
    Change change = Change::Modified;
    /// Something of it is staged (the index differs from HEAD).
    bool staged = false;
    /// The work tree differs from the index (always for untracked files).
    bool unstaged = false;
    /// Lines added / deleted against HEAD (or the checkpoint); -1 unknown.
    int additions = -1;
    int deletions = -1;
    bool binary = false;
    /// Octal mode before / after (0 unknown or none) — a submodule is GitlinkMode.
    quint32 oldMode = 0;
    quint32 newMode = 0;
};

struct StatusReport {
    bool ok = false;
    QString error;
    Status status;
    /// Every entry but ignored ones, in git's order, with lines added / deleted.
    QList<FileChange> changes;
};

struct ChangesReport {
    bool ok = false;
    QString error;
    QList<FileChange> changes;
};

/// The two texts of a file's diff.
struct FileTexts {
    bool ok = false;
    QString error;
    QString path;
    QString oldText;
    QString newText;
    /// The file was in the base / is in the work tree.
    bool oldExists = false;
    bool newExists = false;
    bool binary = false;
    /// One side is longer than Client::MaxTextBytes: no texts.
    bool tooLarge = false;
};

struct FileTextsRequest {
    QString path;
    /// A rename: the path in the base.
    QString oldPath;
    /// The revision of the old text: "HEAD", a commit, checkpointRef(id); "" none (all new).
    QString base = QStringLiteral("HEAD");
};

struct BranchList {
    bool ok = false;
    QString error;
    QList<Branch> branches;
};

struct WorktreeList {
    bool ok = false;
    QString error;
    QList<Worktree> worktrees;
};

struct PullRequest {
    QString title;
    QString body;
    bool draft = false;
    /// The branch it is from (`--head`); "" gh's own choice (the current branch).
    QString head;
    /// The branch it goes into (`--base`); "" the repository's default.
    QString base;
};

/// What restore() did.
struct RestoreReport {
    bool ok = false;
    QString error;
    /// Files written back as the checkpoint had them (changed or deleted since).
    QStringList restored;
    /// Files added since, deleted.
    QStringList removed;
    /// Left as they are: added since but ignored now (by the restored .gitignore), submodules, a
    /// path in the way that could not be cleared.
    QStringList kept;
};

class Client : public QObject {
    Q_OBJECT
public:
    explicit Client(QObject *parent = nullptr);
    ~Client() override;

    /// The biggest text a diff shows (each side): 2 MiB.
    static constexpr qsizetype MaxTextBytes = 2 * 1024 * 1024;
    /// The most untracked files whose lines status() counts.
    static constexpr int MaxCountedUntracked = 2000;

    /// This build can start programs (QT_CONFIG(process)).
    static bool processesAvailable();

    /// The git program (default: `git` found on PATH; "" none — every call fails).
    void setGitProgram(const QString &program) { m_git = program; }
    QString gitProgram() const { return m_git; }
    /// The GitHub CLI (default: `gh` on PATH; "" none).
    void setGhProgram(const QString &program) { m_gh = program; }
    QString ghProgram() const { return m_gh; }
    /// Time limits: local commands (default 60 s), network ones (push, pull, gh: 180 s), a commit
    /// with its hooks (300 s).
    void setLocalTimeoutMs(int ms) { m_localTimeoutMs = ms; }
    int localTimeoutMs() const { return m_localTimeoutMs; }
    void setNetworkTimeoutMs(int ms) { m_networkTimeoutMs = ms; }
    int networkTimeoutMs() const { return m_networkTimeoutMs; }
    void setCommitTimeoutMs(int ms) { m_commitTimeoutMs = ms; }
    /// Runs going on.
    int running() const;

    using Done = std::function<void(const RunResult &result)>;
    /// `git -C <folder> <arguments>`.
    void git(const QString &folder, const QStringList &arguments, const RunOptions &options, Done done);
    /// `gh <arguments>` in `folder`.
    void gh(const QString &folder, const QStringList &arguments, const RunOptions &options, Done done);

    // ---- Reading ---------------------------------------------------------------------------------

    /// Whether `folder` is in a work tree, and its folders.
    void repository(const QString &folder, std::function<void(const RepoInfo &)> done);
    /// `git status` with lines added / deleted per file against HEAD (untracked files counted
    /// through a temporary index with them as intent-to-add; at most MaxCountedUntracked).
    void status(const QString &folder, std::function<void(const StatusReport &)> done);
    /// A file's text in `request.base` and in the work tree now (read from disk).
    void fileTexts(const QString &folder, const FileTextsRequest &request, std::function<void(const FileTexts &)> done);
    /// Local and remote-tracking branches, the most recently committed first.
    void branches(const QString &folder, std::function<void(const BranchList &)> done);
    void worktrees(const QString &folder, std::function<void(const WorktreeList &)> done);

    // ---- Actions ---------------------------------------------------------------------------------

    /// `git add -A`, then `git commit` with `message` (standard input; the repository's hooks
    /// run). Outcome::value: the new commit's id.
    void commitAll(const QString &folder, const QString &message, std::function<void(const Outcome &)> done);
    /// `git push`; without an upstream `git push -u <remote> <branch>` (origin, else the only
    /// remote). Not on a detached HEAD.
    void push(const QString &folder, std::function<void(const Outcome &)> done);
    /// `git pull --ff-only`.
    void pull(const QString &folder, std::function<void(const Outcome &)> done);
    /// `git switch -c <name>`: a new branch from HEAD, the work tree's changes kept.
    void createBranch(const QString &folder, const QString &name, std::function<void(const Outcome &)> done);
    /// `git switch <name>`; a remote-tracking branch ("origin/x") without a local one:
    /// `git switch --track <name>`.
    void checkout(const QString &folder, const QString &name, std::function<void(const Outcome &)> done);
    /// `git worktree add [-b <branch>] <path> [<base>]` (`path` relative to `folder` when not
    /// absolute).
    void addWorktree(const QString &folder, const QString &path, const QString &branch, const QString &base,
                     std::function<void(const Outcome &)> done);
    /// `git worktree remove [--force] <path>` (without force git refuses one with changes).
    void removeWorktree(const QString &folder, const QString &path, bool force,
                        std::function<void(const Outcome &)> done);
    /// `gh pr create --title … --body … [--draft] [--head …] [--base …]`; Outcome::url the pull
    /// request's address — also when one exists already for the branch (ok, with that message).
    void createPullRequest(const QString &folder, const PullRequest &request,
                           std::function<void(const Outcome &)> done);

    // ---- Checkpoints -----------------------------------------------------------------------------

    /// A new id: 32 random hex digits.
    static QString newCheckpointId();
    /// refs/brulion/checkpoints/<id>.
    static QString checkpointRef(const QString &id);
    /// A snapshot of the work tree kept as checkpointRef(id) (replaced when it exists).
    /// Outcome::value: the snapshot's commit.
    void checkpoint(const QString &folder, const QString &id, std::function<void(const Outcome &)> done);
    /// The files that differ between the checkpoint and the work tree now (renames found), with
    /// lines added / deleted.
    void changesSince(const QString &folder, const QString &id, std::function<void(const ChangesReport &)> done);
    /// The work tree back to the checkpoint: changed and deleted files written back, files added
    /// since deleted (with folders left empty) — not ignored ones, not submodules. The index,
    /// HEAD and the branches stay as they are.
    void restore(const QString &folder, const QString &id, std::function<void(const RestoreReport &)> done);
    /// The refs of these checkpoints deleted (their objects go with git's next gc).
    void dropCheckpoints(const QString &folder, const QStringList &ids, std::function<void(const Outcome &)> done);
    /// The ids of the checkpoints the repository keeps.
    void checkpoints(const QString &folder,
                     std::function<void(bool ok, const QStringList &ids, const QString &error)> done);

private:
    struct Run;
    void start(const QString &program, const QString &folder, const QStringList &arguments, const RunOptions &options,
               bool isGh, Done done);
    /// The work tree as a tree object, through a temporary index (`tree` "" on failure).
    void snapshotTree(const QString &folder, std::function<void(const QString &tree, const QString &error)> done);
    /// `done` later, on this object's thread (never from inside the call).
    void later(std::function<void()> done);

    QString m_git;
    QString m_gh;
    int m_localTimeoutMs = 60000;
    int m_networkTimeoutMs = 180000;
    int m_commitTimeoutMs = 300000;
    QList<std::shared_ptr<Run>> m_runs;
};

} // namespace brulion::gitcli
