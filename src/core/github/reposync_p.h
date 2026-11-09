// Internal: the bookkeeping and the state of one RepoSync operation, shared by reposync.cpp
// (pulling) and reposync_commit.cpp (commits and proposals).
#pragma once

#include "github/reposync.h"

#include <QElapsedTimer>

namespace brulion::github {

/// What the vault's LocalStore keeps under "github/" (RepoSync's class comment).
struct RepoSync::State {
    RepoLink link;         ///< the link the bookkeeping belongs to
    QString commit;        ///< the commit last pulled or made ("" none)
    QString tree;          ///< its root tree
    QString folderTree;    ///< the notebook folder's tree in it ("" unknown or missing)
    QString etag;          ///< of the branch's answer, for If-None-Match
    bool complete = false; ///< the last pull handled every file (commit and etag can be trusted)
    bool folderMissing = false;
    bool emptyRepository = false;
    qint64 pulledAt = 0;             ///< ms since epoch
    QHash<VaultPath, QString> files; ///< notebook path → blob id at `commit` (the merge base)
    QSet<VaultPath> executable;      ///< mode 100755
    QHash<VaultPath, QString> names; ///< notebook path → the repository's spelling, when it differs
    QHash<VaultPath, QString> lfs;   ///< Git LFS pointers left out: path → blob id
};

/// One file of a pull to bring up to date.
struct RepoSync::Job {
    /// Rename: a file GitHub removed whose unchanged content it added under another name — moved
    /// here too (Vault::rename: its history, favourite and folds follow), nothing downloaded.
    enum class Kind { Download, Merge, Delete, Rename };
    Kind kind = Kind::Download;
    VaultPath path;
    QString remote; ///< blob id on GitHub ("" Delete)
    QString base;   ///< blob id both had ("" none)
    QString local;  ///< blob id here when planned ("" missing)
    VaultPath from; ///< Rename: the old name
};

/// One running operation. Kept alive by the callbacks waiting for GitHub.
struct RepoSync::Run {
    /// The RepoSync running it: a callback that outlived it ends at alive() without touching it.
    QPointer<RepoSync> owner;
    Operation operation = Operation::Pull;
    int generation = 0;
    QElapsedTimer timer;
    std::function<void(const PullReport &)> pullDone;
    std::function<void(const CommitReport &)> commitDone;
    PullReport pull;
    CommitReport commit;
    State state;     ///< the bookkeeping being brought up to date
    State before;    ///< as the operation found it
    BranchHead head; ///< the branch's head (empty: unchanged or no commit)

    // ---- pull ---------------------------------------------------------------------------
    QString folderSha;                ///< the folder's tree ("" missing)
    QHash<VaultPath, QString> remote; ///< GitHub's notebook files → blob id
    QHash<VaultPath, QString> remoteNames;
    QSet<VaultPath> remoteExecutable;
    QHash<VaultPath, QString> local; ///< this device's notebook files → blob id
    QSet<VaultPath> unknown;         ///< files here that could not be read or are too large
    QList<Job> jobs;
    qsizetype nextJob = 0;
    int activeJobs = 0;
    int doneJobs = 0;
    QSet<QString> emptiedFolders; ///< folders of files the pull removed

    // ---- commit and propose -------------------------------------------------------------
    QString message;
    QString title;
    QString body;
    QStringList paths;               ///< selected (none: every change)
    QList<VaultPath> targets;        ///< the changed files to commit or propose
    QString defaultBranch;           ///< the repository's (an empty repository's first commit)
    QList<VaultPath> firstCommitted; ///< an empty repository's first file, committed on its own
    QString branchName;              ///< a new proposal's branch
    int attempts = 0;                ///< commits tried again after the branch moved
    RepoId target;                   ///< where blobs, trees and commits are made
    QList<NewTreeEntry> entries;
    /// An entry waiting for its blob.
    struct Blob {
        qsizetype entry = 0; ///< index in `entries`
        QByteArray bytes;
        QString sha; ///< Git's id of `bytes`
    };
    QList<Blob> blobs;
    qsizetype nextBlob = 0;
    int activeBlobs = 0;
    QHash<VaultPath, QString> newShas; ///< committed: path → blob id ("" removed)
    std::optional<Proposal> proposal;  ///< the open proposal being updated
};

} // namespace brulion::github
