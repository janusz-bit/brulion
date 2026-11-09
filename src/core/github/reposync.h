// A notebook kept in a folder of a GitHub repository's branch: the newest version pulled into the
// vault, the notebook's changes committed, or proposed as a pull request. Part of the github
// module — see github/github.h for the overview.
#pragma once

#include "github/client.h"
#include "types.h"

#include <QDateTime>
#include <QHash>
#include <QJsonObject>
#include <QList>
#include <QMap>
#include <QObject>
#include <QPointer>
#include <QSet>
#include <QString>
#include <QStringList>

#include <algorithm>
#include <functional>
#include <memory>
#include <optional>

namespace brulion {
class LocalStore;
class Vault;
} // namespace brulion

namespace brulion::github {

/// What a notebook is linked to: a branch of a repository and the folder of it that is the
/// notebook ("" = the whole repository).
struct RepoLink {
    RepoId repo;
    QString branch;
    QString folder;

    bool isValid() const { return repo.isValid() && !branch.isEmpty(); }
    /// The repository path of a notebook path.
    QString repoPath(const VaultPath &path) const;
    QJsonObject toJson() const;
    static RepoLink fromJson(const QJsonObject &json);
    friend bool operator==(const RepoLink &, const RepoLink &) = default;
};

/// The files of a repository folder that belong to the notebook: what the vault tracks and syncs
/// (sync::isSyncablePath: no .git, .github, .obsidian, .trash, temp files), without hidden files
/// and folders (".gitignore", ".editorconfig": the repository's, not notes) — except the
/// notebook's ".brulion/config.json" — and without names holding a backslash (a separator in the
/// vault: such a name stands for another file there).
bool isNotebookPath(const VaultPath &path);

/// A notebook file that differs from the version last pulled or committed.
struct LocalChange {
    enum class Kind { Added, Modified, Deleted };
    VaultPath path;
    Kind kind = Kind::Modified;
    /// The open proposal holds this very version.
    bool proposed = false;
    friend bool operator==(const LocalChange &, const LocalChange &) = default;
};
/// "added" | "modified" | "deleted".
QString changeKindName(LocalChange::Kind kind);

/// The notebook's pull request (the last one it proposed).
struct Proposal {
    int number = 0;
    QUrl url;
    QString title;
    QString state; ///< "open" | "closed" | "merged"
    RepoId head;   ///< the repository holding its branch: the user's fork, or the repository itself
    QString branch;
    QString commit; ///< the branch's last commit Brulion made
    QString base;   ///< the commit of the notebook's branch that commit was made on
    /// The notebook files it changes, with their versions there ("" = removed).
    QMap<VaultPath, QString> files;

    bool isOpen() const { return number > 0 && state == u"open"; }
    QJsonObject toJson() const;
    static std::optional<Proposal> fromJson(const QJsonObject &json);
    friend bool operator==(const Proposal &, const Proposal &) = default;
};

/// A file a pull or a commit left out or could not handle.
struct FileProblem {
    VaultPath path;
    /// Left out: "too-large", "lfs" (Git LFS keeps its content), "symlink", "submodule",
    /// "case-clash" (another file of the folder has its name in another letter case), "name"
    /// (two names that are one in Unicode's composed form). Failed: "download-failed",
    /// "integrity", "read-failed", "write-failed", "changed-meanwhile", "conflict-copy-failed".
    QString code;
    QString message; ///< technical (English)
    friend bool operator==(const FileProblem &, const FileProblem &) = default;
};

/// What one pull did.
struct PullReport {
    QString commit;                  ///< the branch's newest commit ("" for an empty repository)
    bool upToDate = false;           ///< the branch has not moved since the last complete pull
    bool folderMissing = false;      ///< the branch has no such folder (yet): a commit creates it
    bool emptyRepository = false;    ///< no commit at all yet
    int downloaded = 0;              ///< new or changed on GitHub, written here
    int deletedLocal = 0;            ///< removed on GitHub, moved to the trash here
    int renamed = 0;                 ///< renamed or moved on GitHub (unchanged), renamed here too
    int merged = 0;                  ///< changed on both sides, merged
    int kept = 0;                    ///< removed on GitHub but changed here: kept (a commit adds it again)
    QList<VaultPath> conflicts;      ///< changed on both sides and not merged: this device's version kept
    QList<VaultPath> conflictCopies; ///< GitHub's versions of those, saved beside them
    QList<FileProblem> skipped;      ///< left out on purpose
    QList<FileProblem> errors;       ///< failed; tried again by the next pull
    QList<VaultPath> written;        ///< every notebook path the pull wrote or removed
    GitHubError error;               ///< the pull stopped on this
    /// Why nothing was tried: "busy" (another operation runs), "not-linked" (no repository, or the
    /// vault or store is gone), "vault-unavailable" (the notebook's folder is missing or on another
    /// storage: Vault::isStorageAvailable()), "remote-empty" (every file pulled before is gone from
    /// the branch's folder — a deleted branch or folder: nothing is removed here); "" when nothing
    /// was refused.
    QString refused;
    std::optional<Proposal> proposal; ///< the proposal's state, when the pull asked for it
    qint64 durationMs = 0;
    bool failed() const { return error.isError() || !refused.isEmpty(); }
};

/// What one commit or proposal did.
struct CommitReport {
    PullReport pull;            ///< the pull that came first
    QString commit;             ///< the new commit ("" when none was made)
    QList<VaultPath> committed; ///< the files it added, changed or removed
    bool nothingToCommit = false;
    /// Why nothing was tried: "conflicts" (conflicts selected: merge them first — conflictsPending),
    /// "pull-incomplete" (the pull before could not bring every file up to date), "local-empty"
    /// (every file pulled before is gone here: the storage?), "no-token", "read-only" (the token
    /// may not push to the repository: propose instead), "proposal-moved" (the proposal's branch
    /// changed on GitHub), the pull's refusals; "" when nothing was refused.
    QString refused;
    QList<VaultPath> conflictsPending;
    std::optional<Proposal> proposal; ///< propose(): the pull request
    bool forked = false;              ///< propose() made the user's fork
    GitHubError error;
    bool failed() const { return error.isError() || !refused.isEmpty() || pull.failed(); }
};

/// Pulls, commits and proposes the notebook's changes. One operation at a time (another one asked
/// meanwhile fails at once with Validation "busy"); every callback runs from the event loop.
///
/// The bookkeeping (device-local, the vault's LocalStore under "github/"): the link it belongs to,
/// the commit last pulled or committed with its root tree and the folder's tree, the Git blob id of
/// every notebook file at that commit (the merge base: a file whose content hashes to it is
/// unchanged here), the files kept elsewhere (Git LFS), the names spelled otherwise in the
/// repository (decomposed Unicode) and the proposal.
///
/// Pull: the branch's head (If-None-Match: an unmoved branch costs nothing), the folder's tree
/// (its id from the parent folder's listing; an unchanged folder is not listed again), then per
/// file — new or changed there and unchanged here: downloaded (verified against its blob id);
/// removed there and unchanged here: to the trash (empty folders left behind removed) — unless
/// its content arrived under one new name there, then renamed here too (Vault::rename); changed on
/// both sides: text merged three-way against the version both had (sync::merge3, the config key by
/// key, drawings element by element), else this device's version kept and GitHub's saved beside it
/// as "Name.conflict-GitHub-<stamp>.ext" — once; removed here and changed there: restored; changed
/// here and removed there: kept. Files without a token come from raw.githubusercontent.com,
/// with one through the API. A pull with failures does not count as complete (the next one
/// compares everything again).
///
/// Commit: a pull first, then the selected changes (all by default; conflict copies never) as one
/// commit on the branch's head — text inline in the new tree, other files as uploaded blobs, the
/// executable bit and the repository's spelling of names kept — and the branch moved to it only
/// as a fast-forward (it moved meanwhile: pulled and committed again, once). An empty repository
/// gets its first file through the contents API.
///
/// Propose: a pull first, then the changes as a commit on a new branch "brulion/<stamp>" — in the
/// repository when the token may push there, else in the user's fork (made when missing, waited
/// for) — and a pull request to the notebook's branch. While it is open, proposing again updates
/// it: one more commit on its branch holding every file it changes at this device's version (a
/// merge commit when the notebook's branch moved meanwhile), never a second pull request.
class RepoSync : public QObject {
    Q_OBJECT
public:
    enum class Operation { None, Pull, Commit, Propose, Refresh };

    /// The device name in conflict copies of GitHub's versions.
    static constexpr QStringView ConflictDevice = u"GitHub";
    /// Larger files are left out (GitHub refuses files over 100 MB).
    static constexpr qint64 DefaultMaxFileSize = 50LL * 1024 * 1024;
    /// Text up to this size travels inline in a new tree; larger files and binary ones as blobs.
    static constexpr qint64 InlineTextLimit = 1024 * 1024;

    /// Store keys.
    static QString linkKey();
    static QString stateKey();
    static QString filesKey();
    static QString proposalKey();
    static QString repositoryKey();

    /// None of the pointers is owned.
    RepoSync(Vault *vault, GitHubClient *client, LocalStore *store, QObject *parent = nullptr);
    ~RepoSync() override;
    Q_DISABLE_COPY_MOVE(RepoSync)

    /// The link. Another one than the bookkeeping's forgets it: the next pull is a first one
    /// (nothing is removed here; files that differ keep this device's version and get a copy).
    void setLink(const RepoLink &link);
    RepoLink link() const { return m_link; }
    /// Clock of conflict copy names, proposal branches and times (default: current local time).
    void setClock(std::function<QDateTime()> clock);
    QDateTime now() const;
    void setMaxFileSize(qint64 bytes) { m_maxFileSize = bytes; }
    /// Downloads and uploads in flight (default 4).
    void setConcurrency(int count) { m_concurrency = std::max(1, count); }
    /// Wait between checks of a fork that is not ready yet (default 2000 ms; tests) and how many.
    void setForkWait(int ms, int attempts);

    Operation running() const { return m_operation; }
    bool busy() const { return m_operation != Operation::None; }

    void pull(std::function<void(const PullReport &)> done);
    /// Commit `paths` (empty: every change) with `message` to the linked branch.
    void commit(const QString &message, const QStringList &paths, std::function<void(const CommitReport &)> done);
    /// Propose `paths` (empty: every change) as a pull request titled `title` (`body`: its text).
    void propose(const QString &title, const QString &body, const QStringList &paths,
                 std::function<void(const CommitReport &)> done);
    /// Ask GitHub for the proposal's state (open, merged, closed).
    void refreshProposal(std::function<void(const GitHubError &, const std::optional<Proposal> &)> done);
    /// Stop the running operation: what is written stays, its callback reports Cancelled.
    void cancel();

    /// The notebook files that differ from the version last pulled or committed, sorted by path
    /// (files hashed only when their size or time changed); conflict copies made here are left out.
    QList<LocalChange> localChanges();
    /// A commit was pulled or made at least once.
    bool hasPulled() const;
    QString headCommit() const;
    /// When the last pull ended, ms since epoch (0: never).
    qint64 lastPullMs() const;
    /// The proposal (also a closed or merged one, until forgotten or replaced).
    std::optional<Proposal> proposal() const;
    /// Forget the proposal (the next proposal starts a new pull request).
    void forgetProposal();
    /// The repository as last asked for (permissions: may the token push?).
    std::optional<Repository> repository() const;
    /// Forget all bookkeeping.
    void resetState();

Q_SIGNALS:
    /// "branch", "tree", "download" (done / total files), "upload", "commit", "fork", "proposal".
    void progress(const QString &phase, int done, int total);
    /// The bookkeeping changed (head, files, proposal, repository).
    void stateChanged();

private:
    struct Run;
    struct State;
    struct Job;

    // reposync.cpp: bookkeeping, local files, pulling.
    State loadState() const;
    void saveState(const State &state);
    /// The notebook files here and their blob ids; `unknown` gets those that cannot be read or
    /// are too large (never taken for removed).
    QHash<VaultPath, QString> localShas(QSet<VaultPath> *unknown = nullptr);
    std::optional<QString> localSha(const VaultPath &path);
    void onVaultChanged(const QList<brulion::VaultChange> &changes);
    /// `operation` started: its run, now the current one — or none while another operation runs.
    std::shared_ptr<Run> beginRun(Operation operation);
    void finishOperation();
    /// The run is still the current operation of a RepoSync that has all it needs. Static: a
    /// callback that outlived its RepoSync (Run::owner) must not touch it.
    static bool alive(const Run &run);
    void startPull(std::shared_ptr<Run> run);
    void pullBranch(std::shared_ptr<Run> run);
    void pullFolder(std::shared_ptr<Run> run);
    void pullTree(std::shared_ptr<Run> run, const QString &folderSha);
    void planPull(std::shared_ptr<Run> run, const QList<TreeEntry> &entries);
    void runJobs(std::shared_ptr<Run> run);
    void runJob(std::shared_ptr<Run> run, int index);
    void fetchContent(std::shared_ptr<Run> run, const VaultPath &path, const QString &sha, const QString &commit,
                      std::function<void(const GitHubError &, const QByteArray &)> done);
    void applyDownload(Run &run, const Job &job, const QByteArray &remote, const QByteArray *base);
    void endPull(std::shared_ptr<Run> run);
    void pruneEmptyFolders(const QSet<QString> &folders);
    /// GitHub's `content` of `path` is saved beside it already.
    bool hasConflictCopy(const VaultPath &path, const QByteArray &content);
    /// `current` with the state and title of its pull request; stored when they changed.
    Proposal updateProposal(const Proposal &current, const PullRequest &pr);
    void reportPull(std::shared_ptr<Run> run);

    // reposync_commit.cpp: commits and proposals.
    void queueCommit(Operation operation, const QString &message, const QString &title, const QString &body,
                     const QStringList &paths, std::function<void(const CommitReport &)> done);
    void startCommit(std::shared_ptr<Run> run);
    void afterPullForCommit(std::shared_ptr<Run> run);
    void checkProposal(std::shared_ptr<Run> run);
    bool selectTargets(std::shared_ptr<Run> run);
    void storeRepository(const Repository &repository);
    void newProposalHead(std::shared_ptr<Run> run);
    void commitFirstFile(std::shared_ptr<Run> run);
    void prepareEntries(std::shared_ptr<Run> run, const std::function<void()> &then);
    void uploadBlobs(std::shared_ptr<Run> run, const std::function<void()> &then);
    void writeCommit(std::shared_ptr<Run> run);
    void moveBranch(std::shared_ptr<Run> run, const QString &commit, const QString &tree);
    void afterCommitted(std::shared_ptr<Run> run, const QString &commit, const QString &tree);
    void waitForFork(std::shared_ptr<Run> run, int attempt);
    void writeProposal(std::shared_ptr<Run> run);
    void moveProposalBranch(std::shared_ptr<Run> run, const QString &commit);
    void createProposalBranch(std::shared_ptr<Run> run, const QString &commit, int attempt);
    void openPullRequest(std::shared_ptr<Run> run, const QString &commit, int attempt);
    void finishProposal(std::shared_ptr<Run> run, const QString &commit);
    void reportCommit(std::shared_ptr<Run> run);
    void failCommit(std::shared_ptr<Run> run, const GitHubError &error);

    QPointer<Vault> m_vault;
    QPointer<GitHubClient> m_client;
    QPointer<LocalStore> m_store;
    RepoLink m_link;
    std::function<QDateTime()> m_clock;
    qint64 m_maxFileSize = DefaultMaxFileSize;
    int m_concurrency = 4;
    int m_forkWaitMs = 2000;
    int m_forkAttempts = 30;
    Operation m_operation = Operation::None;
    int m_generation = 0;
    std::shared_ptr<Run> m_run;
    struct CachedSha {
        qint64 size = -1;
        qint64 mtime = 0;
        QString sha;
    };
    QHash<VaultPath, CachedSha> m_shas;
};

} // namespace brulion::github
