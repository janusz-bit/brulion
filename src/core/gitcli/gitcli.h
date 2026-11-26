#pragma once
// gitcli — git and the GitHub CLI for the "Code" mode (docs/CODE-GIT.md): the working tree's
// changes, commit / push / pull, branches, worktrees, pull requests and turn checkpoints.
//
// This header: the pure parsers of git's machine-readable output, unit-tested without git —
//
//   parseStatus()     `git status --porcelain=v2 --branch -z`: the branch (name, upstream, ahead /
//                     behind, detached, no commit yet) and the entries (ordinary, renamed, copied,
//                     unmerged, untracked, ignored), staged (X) and unstaged (Y) state apart
//   parseNumstat()    `git diff --numstat -z`: lines added / deleted per file, "-" for binary
//   parseDiff()       `git diff --raw -z` (diff-tree too), alone or with --numstat after it
//   parseWorktrees()  `git worktree list --porcelain [-z]`
//   parseBranches()   `git branch --format=<branchFormat()>`
//   parsePullRequestUrl()  the URL `gh pr create` prints (also in its "already exists" error)
//
// and small rules the client and the app share (isValidBranchName(), isValidCheckpointId(),
// isSafeRelativePath(), looksBinary(), subjectOf()). Malformed input never crashes a parser: a
// record that does not read is skipped, a truncated one at the end dropped. Paths are git's bytes
// as UTF-8 (with -z git never quotes them: spaces, newlines and any letters come through as they
// are).
//
// client.h: Client, which runs git and gh asynchronously (QProcess) and builds the operations on
// these parsers, checkpoints included.

#include <QByteArrayView>
#include <QList>
#include <QString>
#include <QStringView>

namespace brulion::gitcli {

// ---- git status --------------------------------------------------------------------------------

enum class EntryKind : quint8 {
    Ordinary,  ///< "1": changed in the index or the work tree
    Renamed,   ///< "2" with R: `oldPath` → `path`
    Copied,    ///< "2" with C: a copy of `oldPath`
    Unmerged,  ///< "u": a conflict of a merge, rebase, cherry-pick …
    Untracked, ///< "?"
    Ignored,   ///< "!" (only with --ignored)
};

/// One entry of `git status --porcelain=v2`.
struct StatusEntry {
    EntryKind kind = EntryKind::Ordinary;
    /// X: the index against HEAD — '.' unchanged, 'M' modified, 'T' type changed, 'A' added,
    /// 'D' deleted, 'R' renamed, 'C' copied, 'U' unmerged. '?' / '!' for untracked / ignored.
    char staged = '.';
    /// Y: the work tree against the index, the same letters.
    char unstaged = '.';
    QString path;
    /// Renamed / Copied: the path it came from.
    QString oldPath;
    /// Renamed / Copied: git's similarity, 0–100.
    int score = 0;
    /// A submodule ("S…" instead of "N...").
    bool submodule = false;
    /// The octal file modes in HEAD, the index and the work tree (0: none; Ordinary / Renamed /
    /// Copied / Unmerged — of an unmerged entry: stage 1 for HEAD, stage 2 for the index).
    quint32 headMode = 0;
    quint32 indexMode = 0;
    quint32 worktreeMode = 0;
};

/// The `# branch.*` headers.
struct BranchStatus {
    /// HEAD's commit; "" before the first commit (`initial`).
    QString oid;
    /// The branch checked out; "" when `detached` (or unknown).
    QString head;
    bool detached = false;
    /// No commit yet (`# branch.oid (initial)`): a new repository, an orphan branch.
    bool initial = false;
    /// The upstream ("origin/main"); "" none.
    QString upstream;
    /// Commits ahead of / behind the upstream (`# branch.ab`; 0 without one).
    int ahead = 0;
    int behind = 0;
    /// `# branch.ab` came: the upstream exists. An upstream named without it is gone (deleted on
    /// the remote and pruned).
    bool aheadBehindKnown = false;
    bool upstreamGone() const { return !upstream.isEmpty() && !aheadBehindKnown; }
};

struct Status {
    BranchStatus branch;
    QList<StatusEntry> entries;
    /// `# stash <n>` (with --show-stash), else 0.
    int stashCount = 0;
    /// Records that did not read (garbage, a truncated record): skipped.
    int skipped = 0;
};

Status parseStatus(QByteArrayView data);

/// What the UI says of a change, one word for every entry.
enum class Change : quint8 { Modified, Added, Deleted, Renamed, Untracked, Conflicted, Ignored };

/// An entry's change against HEAD: Unmerged → Conflicted, Untracked, Ignored; Renamed; Copied →
/// Added; else Deleted when the index or the work tree deleted it, Added when the index added it,
/// Modified (also a type change).
Change changeOf(const StatusEntry &entry);
/// "modified" | "added" | "deleted" | "renamed" | "untracked" | "conflicted" | "ignored".
QString changeName(Change change);
/// M A D R U C I — the letter the changes panel shows.
QString changeLetter(Change change);
/// A raw diff's status letter (A C D M R T U X) as a Change: C → Added, T → Modified,
/// U → Conflicted, anything else → Modified.
Change changeOfRawStatus(char status);

// ---- git diff --numstat / --raw ----------------------------------------------------------------

/// One file of `git diff --numstat -z`.
struct NumstatEntry {
    QString path;
    /// A rename or copy (with -M / -C): the path it came from.
    QString oldPath;
    int additions = 0;
    int deletions = 0;
    /// "-\t-": git counts no lines of a binary file.
    bool binary = false;
};

QList<NumstatEntry> parseNumstat(QByteArrayView data);

/// One file of `git diff --raw -z` / `git diff-tree -r -z`.
struct RawEntry {
    quint32 oldMode = 0;
    quint32 newMode = 0;
    QString oldOid;
    QString newOid;
    /// A C D M R T U X.
    char status = 'M';
    /// R / C: the similarity, 0–100.
    int score = 0;
    QString path;
    /// R / C: where it came from.
    QString oldPath;
};

struct DiffRecords {
    QList<RawEntry> raw;
    QList<NumstatEntry> numstat;
};

/// `--raw` records (":…"), `--numstat` records, or both in one output (`--raw --numstat`: git
/// writes the raw ones first).
DiffRecords parseDiff(QByteArrayView data);

/// The octal mode of a submodule's entry (a "gitlink").
inline constexpr quint32 GitlinkMode = 0160000;
inline constexpr quint32 SymlinkMode = 0120000;

// ---- git worktree list -------------------------------------------------------------------------

struct Worktree {
    QString path;
    /// Its HEAD commit ("" for a bare repository).
    QString head;
    /// The branch checked out, short ("main"); "" when detached or bare.
    QString branch;
    bool bare = false;
    bool detached = false;
    bool locked = false;
    QString lockReason;
    bool prunable = false;
    QString prunableReason;
};

/// `git worktree list --porcelain`, with -z (NUL after every line) or without (newlines).
QList<Worktree> parseWorktrees(QByteArrayView data);

// ---- git branch --------------------------------------------------------------------------------

struct Branch {
    /// "main", or a remote-tracking branch as "origin/main".
    QString name;
    /// "refs/heads/main" / "refs/remotes/origin/main".
    QString refName;
    QString oid;
    /// Its upstream ("origin/main"); "" none.
    QString upstream;
    int ahead = 0;
    int behind = 0;
    bool upstreamGone = false;
    /// The branch checked out in this worktree.
    bool current = false;
    bool remote = false;
    /// The worktree that has it checked out ("" none).
    QString worktreePath;
};

/// The --format parseBranches() reads (fields NUL-separated, the record ended by a NUL too,
/// which git follows with a newline).
QString branchFormat();
/// `git branch [-a] --format=<branchFormat()>`: local and remote-tracking branches, in git's
/// order; symbolic refs (origin/HEAD) left out.
QList<Branch> parseBranches(QByteArrayView data);

// ---- gh ----------------------------------------------------------------------------------------

/// The last pull request URL (http(s)://…/pull/<n>) in `gh pr create`'s output or error; "" none.
QString parsePullRequestUrl(QStringView output);

// ---- Rules -------------------------------------------------------------------------------------

/// A name `git check-ref-format --branch` takes (a little stricter): not empty, no leading '-',
/// no ASCII control characters, space, ~ ^ : ? * [ \, no "..", "@{", "//", no component starting
/// with '.' or ending with ".lock", not ending with '/' or '.', not "@" or "HEAD". Letters of any
/// language are fine.
bool isValidBranchName(QStringView name);
/// A checkpoint's id (the last part of refs/brulion/checkpoints/<id>): 1–200 of [A-Za-z0-9_-],
/// '/'-separated parts allowed (no empty part).
bool isValidCheckpointId(QStringView id);
/// A path inside the work tree as git writes it: relative, '/'-separated, no drive ("C:"), no empty,
/// ".", ".." or ".git" part (".git" in any case, and as NTFS also reads it: with trailing dots or
/// spaces, or its short name "git~1"), no backslash, no NUL. What restore() deletes passes this.
bool isSafeRelativePath(QStringView path);
/// git's own test: a NUL in the first 8000 bytes.
bool looksBinary(QByteArrayView data);
/// The first line of a commit message that is not blank, trimmed — a pull request's default title.
QString subjectOf(QStringView message);

} // namespace brulion::gitcli
