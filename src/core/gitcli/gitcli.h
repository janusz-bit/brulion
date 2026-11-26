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
//   parseLog(), parseStashes(), parseRemotes(), parseRemoteUrl(), forkTarget(),
//   parsePullRequestView()   history, stashes, remotes, the fork a pull request comes from, gh's JSON
//                     (history.cpp)
//   parsePatch(), hunksTouching(), patchOf(), hasConflictMarkers()   one file's unified diff, the
//                     hunks that change some lines, a patch of some of them, git's conflict
//                     markers (patch.cpp)
//
// and small rules the client and the app share (isValidBranchName(), isValidCheckpointId(),
// isSafeRelativePath(), isSafeRevision(), isValidRepositoryName(), looksBinary(), subjectOf()). Malformed input never crashes a parser: a
// record that does not read is skipped, a truncated one at the end dropped. Paths are git's bytes
// as UTF-8 (with -z git never quotes them: spaces, newlines and any letters come through as they
// are).
//
// client.h: Client, which runs git and gh asynchronously (QProcess) and builds the operations on
// these parsers, checkpoints included.

#include <QByteArray>
#include <QByteArrayView>
#include <QList>
#include <QString>
#include <QStringList>
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

// ---- git log -----------------------------------------------------------------------------------

struct Commit {
    QString oid;
    /// Its parents' ids (none: a root commit; two or more: a merge).
    QStringList parents;
    QString authorName;
    QString authorEmail;
    /// When it was written (the author's time), seconds since 1970.
    qint64 authorTime = 0;
    /// The message's first line.
    QString subject;
};

/// The --format parseLog() reads (`tformat:`, six NUL-terminated fields a commit, the record ended
/// by a newline).
QString logFormat();
/// `git log --format=<logFormat()>`: the commits in git's order.
QList<Commit> parseLog(QByteArrayView data);

// ---- git stash list ----------------------------------------------------------------------------

struct Stash {
    /// n of stash@{n} (0 the newest) — when it was listed: another stash pushed meanwhile moves it.
    int index = -1;
    /// The stash's commit (what apply takes; drop finds its index again by it).
    QString oid;
    /// When it was made, seconds since 1970.
    qint64 time = 0;
    /// Its message ("On main: before the turn", "WIP on main: 1a2b3c4 subject").
    QString message;
    /// The branch it was made on ("" when the message does not say).
    QString branch;
};

/// The --format parseStashes() reads (four NUL-terminated fields a stash).
QString stashFormat();
/// `git stash list --format=<stashFormat()>`.
QList<Stash> parseStashes(QByteArrayView data);

// ---- Remotes -----------------------------------------------------------------------------------

struct Remote {
    QString name;
    QString fetchUrl;
    QString pushUrl;
};

/// `git remote -v`: "name<TAB>url (fetch)" and "(push)" lines, one Remote a name, in git's order.
QList<Remote> parseRemotes(QByteArrayView data);

/// A repository on a host, read from a remote's URL.
struct RepoSlug {
    QString host;
    QString owner;
    QString name;
    bool isValid() const { return !host.isEmpty() && !owner.isEmpty() && !name.isEmpty(); }
    /// "owner/name".
    QString path() const { return owner + u'/' + name; }
};

/// The host, owner and name of `git@github.com:owner/name.git`, `https://github.com/owner/name`,
/// `ssh://git@host:22/owner/name.git` …; invalid for anything else (a local path, one part only).
RepoSlug parseRemoteUrl(QStringView url);

/// Where `gh pr create` opens a pull request from a fork: `repo` the repository to open it in
/// ("owner/name", `--repo`) and `head` the branch as "owner:branch" (`--head`); both "" when the
/// branch is pushed to the repository gh targets anyway.
struct ForkTarget {
    QString repo;
    QString head;
};

/// The fork case: the branch is pushed to `pushRemote` ("" origin), and a remote called
/// "upstream" is another repository on the same host — the pull request goes there, from
/// "<push remote's owner>:<branch>".
ForkTarget forkTarget(const QList<Remote> &remotes, const QString &pushRemote, const QString &branch);

// ---- gh ----------------------------------------------------------------------------------------

/// The last pull request URL (http(s)://…/pull/<n>) in `gh pr create`'s output or error; "" none.
QString parsePullRequestUrl(QStringView output);

/// A pull request as `gh pr view --json number,url,state,isDraft,title,baseRefName` gives it.
struct PullRequestInfo {
    int number = 0;
    QString url;
    /// "open" | "draft" | "merged" | "closed" (a draft is open).
    QString state;
    QString title;
    QString baseRefName;
    bool isValid() const { return number > 0 && !url.isEmpty(); }
};

/// gh's JSON; invalid when it does not read (no number or no url).
PullRequestInfo parsePullRequestView(QByteArrayView json);

/// A name `gh repo create` takes: "name" or "owner/name", each of 1–100 of [A-Za-z0-9._-], not
/// "." or "..".
bool isValidRepositoryName(QStringView name);

// ---- Patches -----------------------------------------------------------------------------------

/// One hunk of a unified diff.
struct Hunk {
    int oldStart = 0;
    int oldLines = 0;
    int newStart = 0;
    int newLines = 0;
    /// What follows the second "@@" (a function's name; "" none), as git wrote it.
    QByteArray section;
    /// Its lines as written: ' ' / '-' / '+' and the text, or "\ No newline at end of file" —
    /// without the '\n' (a '\r' of a CRLF file stays: it is the line's).
    QList<QByteArray> lines;
    /// The lines it changes, counted on each side (first, last; 0, 0 when none there): a deletion
    /// is at the old side's lines it deletes and at the new side's line it comes before (and the
    /// other way round), so a range of either side's lines finds the hunks that change them.
    int oldFirst = 0;
    int oldLast = 0;
    int newFirst = 0;
    int newLast = 0;
};

/// One file's unified diff (`git diff -- <path>`).
struct FilePatch {
    /// The lines before the first hunk (diff --git, index, ---, +++ …), each ending with '\n'.
    QByteArray header;
    QList<Hunk> hunks;
    /// "Binary files … differ" / a GIT binary patch: no hunks to take.
    bool binary = false;
};

/// The first file of a unified diff; hunks whose counts do not add up are dropped.
FilePatch parsePatch(QByteArrayView data);
/// The hunks of `patch` that change a line of [first, last] (1-based) on the new side (`newSide`)
/// or the old one.
QList<int> hunksTouching(const FilePatch &patch, int first, int last, bool newSide);
/// A patch of the header and the hunks `indexes` alone, for `git apply`: the new sides' starts
/// counted again as if only they were applied — with `reverse` (`git apply -R`, to a file that
/// has all of them) the old sides' instead. "" when no index is a hunk.
QByteArray patchOf(const FilePatch &patch, const QList<int> &indexes, bool reverse = false);

/// A line starts with a conflict marker of git's ("<<<<<<< " or ">>>>>>> ", or the marker alone).
bool hasConflictMarkers(QByteArrayView data);

// ---- Rules -------------------------------------------------------------------------------------

/// A name `git check-ref-format --branch` takes (a little stricter): not empty, no leading '-',
/// no ASCII control characters, space, ~ ^ : ? * [ \, no "..", "@{", "//", no component starting
/// with '.' or ending with ".lock", not ending with '/' or '.', not "@" or "HEAD". Letters of any
/// language are fine.
bool isValidBranchName(QStringView name);
/// A checkpoint's id (the last part of refs/brulion/checkpoints/<id>): 1–200 of [A-Za-z0-9_-],
/// '/'-separated parts allowed (no empty part).
bool isValidCheckpointId(QStringView id);
/// A path inside the work tree as git writes it: relative, '/'-separated, no empty, ".", ".." or
/// ".git" part (".git" in any case), no backslash, no NUL. What restore() deletes passes this.
bool isSafeRelativePath(QStringView path);
/// A revision the client hands git as an argument (a commit's id, a branch, "HEAD~1",
/// "origin/main", a checkpoint's ref): not empty, no leading '-' (never an option), no space,
/// control character or ':' (a revision and a path are joined with ':'), at most 250 characters.
bool isSafeRevision(QStringView revision);
/// git's own test: a NUL in the first 8000 bytes.
bool looksBinary(QByteArrayView data);
/// The first line of a commit message that is not blank, trimmed — a pull request's default title.
QString subjectOf(QStringView message);

} // namespace brulion::gitcli
