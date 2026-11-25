# Brulion "Code" — git (module `gitcli`, area `gitpanel`)

The "Code" mode works on projects that are git repositories: an agent's thread runs in a
project's folder or in a worktree of its own. This part is the git underneath — the working
tree's changes and their diffs, commit / push / pull, branches, worktrees, pull requests through
the GitHub CLI, and **checkpoints**: a snapshot of the working tree before each agent turn, so the
user can see what the turn changed and put the files back. It has two layers:

| Layer | Where | What |
|---|---|---|
| core module `gitcli` | `src/core/gitcli/` (QtCore only, namespace `brulion::gitcli`) | parsers of git's machine-readable output (`gitcli.h`) and `Client` (`client.h`), which runs `git` and `gh` asynchronously with QProcess; checkpoints in `checkpoints.cpp` |
| app area `gitpanel` | `src/app/gitpanel/` (library `brulion_gitpanel`), `src/app/qml/gitpanel/` | `CodeGit` (QML type): one folder's git, kept up to date, as properties and signals; the changes panel and its dialogs |

Tests: `tests/core/gitcli` (`tst_gitcli`: parsers, no git needed; `tst_gitcli_repo`: `Client` with
real git in temporary repositories) and `tests/app/gitpanel` (`tst_codegit`: `CodeGit` with real
git). The tests with real git skip themselves where `git` is not on PATH; the Nix check build has
it (`nix/package.nix`, `nativeCheckInputs`). Pull requests are tested with a fake `gh` (a shell
script: Unix only).

## The parsers (`gitcli.h`)

Pure functions, unit-tested with data-driven tests; malformed input never crashes them — a record
that does not read is skipped, a truncated one at the end dropped. Paths come as git writes them
with `-z` (never quoted: spaces, tabs, newlines and any letters as they are), decoded as UTF-8.

| Function | Reads |
|---|---|
| `parseStatus()` | `git status --porcelain=v2 --branch -z`: the `# branch.*` headers (oid or "(initial)", head or "(detached)", upstream, `+ahead -behind`, an upstream without `branch.ab` is gone) and `# stash`; entries `1` ordinary, `2` renamed / copied (with the original path and the score), `u` unmerged, `?` untracked, `!` ignored — X (staged) and Y (unstaged) letters, the three modes, submodules |
| `parseNumstat()` | `git diff --numstat -z`: lines added / deleted, `-\t-` binary, renames as two paths |
| `parseDiff()` | `--raw -z` records (modes, oids, status letter and score, one or two paths) and/or `--numstat` records in one output (`diff-tree --raw --numstat` writes the raw ones first) |
| `parseWorktrees()` | `git worktree list --porcelain`, with `-z` or with newlines: path, HEAD, branch, bare, detached, locked / prunable with their reasons |
| `parseBranches()` | `git branch [-a] --format=<branchFormat()>` — seven NUL-terminated fields per record (refname, oid, upstream, track, HEAD, worktree path, symref), so no field needs escaping; symbolic refs (origin/HEAD) and the "(HEAD detached at …)" line left out |
| `parsePullRequestUrl()` | the last `http(s)://…/pull/<n>` in what `gh pr create` printed, also in its "already exists" error |

`changeOf()` turns an entry into one word for the UI (`Change`: Modified, Added, Deleted, Renamed,
Untracked, Conflicted, Ignored — a copy is Added, a type change Modified), `changeName()` /
`changeLetter()` give "modified" / "M" and so on. Rules shared with the app: `isValidBranchName()`
(git's `check-ref-format --branch` rules, a little stricter; letters of any language allowed),
`isValidCheckpointId()`, `isSafeRelativePath()` (relative, `/`-separated, no empty, `.`, `..` or
`.git` part, no backslash, no drive), `looksBinary()` (git's own test: a NUL in the first 8000
bytes), `subjectOf()` (a message's first non-blank line).

## The client (`client.h`)

```cpp
gitcli::Client git;
git.status(folder, [](const gitcli::StatusReport &r) { … r.status.branch … r.changes … });
git.commitAll(folder, message, [](const gitcli::Outcome &o) { … o.ok, o.message, o.value … });
```

Every program runs with QProcess and never blocks the thread: the result comes to the callback on
the client's thread, always after the call has returned (an early failure too). No callback comes
after the client is destroyed; destroying it kills what still runs. Each run has a time limit:
local commands 60 s, network ones (push, pull, gh) 180 s, a commit with its hooks 300 s
(`setLocalTimeoutMs()` …); standard output is capped (256 MiB; a file's text 2 MiB). `folder` is a
work tree's top folder (`repository()` finds it from any folder inside); result paths are relative
to it. Where processes cannot run (WebAssembly) every call fails with a message.

| Call | Runs | Gives |
|---|---|---|
| `repository(folder)` | `rev-parse --is-bare-repository --path-format=absolute --absolute-git-dir --git-common-dir --show-toplevel` | `RepoInfo`: isRepo, topLevel, gitDir (`.git/worktrees/<name>` in a linked worktree), commonDir, bare |
| `status(folder)` | `status --porcelain=v2 --branch -z --untracked-files=all`, then `diff <HEAD or the empty tree> --numstat -z -M` — untracked files counted through a copy of the index where they are intent-to-add (`add --intent-to-add --pathspec-from-file`, literal pathspecs), at most 2000 | `StatusReport`: the parsed status and `FileChange` rows (change, staged, unstaged, additions, deletions, binary, modes) |
| `fileTexts(folder, {path, oldPath, base})` | `cat-file blob <base>:<oldPath or path>`; the new text read from disk (a symbolic link's target as git stores it) | `FileTexts`: oldText, newText, oldExists, newExists, binary, tooLarge (> 2 MiB) |
| `branches(folder)` | `branch -a --sort=-committerdate --format=…` | `BranchList` |
| `worktrees(folder)` | `worktree list --porcelain -z` | `WorktreeList` |
| `commitAll(folder, message)` | `add -A`, `commit --file=-` (the message on standard input; hooks run) | `Outcome` (value: the commit) — "Nothing to commit", an empty message refused |
| `push(folder)` | `push`; without an upstream `push -u <origin or the only remote> <branch>`; refused on a detached HEAD | `Outcome` |
| `pull(folder)` | `pull --ff-only` | `Outcome` ("Already up to date") |
| `createBranch(folder, name)` | `switch -c <name>` (the working tree's changes come along) | `Outcome` |
| `checkout(folder, name)` | `switch <name>`; a remote-tracking branch without a local one: `switch --track <name>` | `Outcome` |
| `addWorktree(folder, path, branch, base)` | `worktree add [-b <branch>] -- <path> [<base>]` | `Outcome` (value: the folder) |
| `removeWorktree(folder, path, force)` | `worktree remove [--force] -- <path>` (without force git refuses one with changes) | `Outcome` |
| `createPullRequest(folder, {title, body, draft, head, base})` | `gh pr create --title … --body … [--draft] [--head …] [--base …]` in the folder | `Outcome` with `url`; an existing pull request for the branch is a success with its url; gh missing or not signed in say so ("run gh auth login") |
| `checkpoint`, `changesSince`, `restore`, `dropCheckpoints`, `checkpoints` | below | |

## Checkpoints

T3 Code's turn checkpoints: before an agent's turn the app takes a snapshot of the working tree;
afterwards it can list and diff what the turn changed, and revert it.

**A snapshot** (`checkpoint(folder, id)`) is the whole working tree — tracked files as they are on
disk, untracked files too, ignored files not — made without touching the user's index, HEAD,
branches or stash:

1. the real index is copied into a temporary folder (`rev-parse --git-path index`; a copy makes
   the next step hash only what changed since the last `git add`; no copy when there is none);
2. `GIT_INDEX_FILE=<copy> git -c core.splitIndex=false add -A` — the copy now describes the
   working tree;
3. `GIT_INDEX_FILE=<copy> git write-tree` — a tree;
4. `git commit-tree --no-gpg-sign <tree> [-p HEAD] -m "Brulion checkpoint <id>"` with the identity
   "Brulion <brulion@localhost>" (a repository without user.name still gets checkpoints; no
   signing prompt, no hooks);
5. `git update-ref refs/brulion/checkpoints/<id> <commit>` — the ref keeps it (and its blobs) from
   `git gc`. An id taken again replaces the ref.

Ids are 1–200 characters of `[A-Za-z0-9_-]` with `/`-separated parts; `Client::newCheckpointId()`
makes 32 random hex digits. The refs are the repository's, shared by all its worktrees (ids must
be unique across them; random ones are). `checkpoints(folder)` lists the ids,
`dropCheckpoints(folder, ids)` deletes the refs in one `update-ref --stdin` (missing ones are no
error); the objects go with git's next gc.

**The changes since one** (`changesSince(folder, id)`): a snapshot tree of the working tree now
(steps 1–3), then `diff-tree -r -z -M --raw --numstat <checkpoint>^{tree} <now>` — added, deleted,
modified, renamed files with lines added / deleted. `fileTexts()` with `base =
checkpointRef(id)` gives a file's text in the checkpoint for the diff.

**Restore** (`restore(folder, id)`) puts the working tree back to the snapshot:

1. a snapshot tree of now, and `diff-tree -r -z --no-renames --raw <checkpoint> <now>`;
2. files changed or deleted since are to be written back, files added since to be deleted;
   submodules (mode 160000) are left as they are, either way (`kept`);
3. files added since that stand where a file comes back go first — a file where a folder was
   (`x` over `x/inner`), files in a folder where a file was (`y/a` where `y` was a file) — and the
   folders left empty by that; a folder still in the way (it holds ignored files) keeps its path
   from being restored (`kept`);
4. the checkpoint's files are written out of a **temporary** index of its tree:
   `GIT_INDEX_FILE=<temp> git read-tree <tree>`, then `git checkout-index -f -z --stdin` with the
   paths — contents, executable bits and symbolic links as they were, the real index neither read
   nor written;
5. the other files added since are deleted unless `git check-ignore` (against the restored
   `.gitignore` files and the real index) says they are ignored — a file the turn un-ignored by
   editing `.gitignore` may have been there, ignored, before the checkpoint, and is kept; then the
   folders that became empty are removed (never the work tree's own folder).

Deletions only take paths that pass `isSafeRelativePath()` and whose folders on the way are no
symbolic links, so nothing outside the work tree is ever deleted. The index, HEAD and the branches
are not moved: when the turn committed, the commit stays and the restored files show as changes
against it. Ignored files are never part of a snapshot and are never touched. Not restored:
submodules' contents, empty folders (git has no record of them), file modes on file systems
without them. Filters of `.gitattributes` (LFS, line endings) apply as with `git add` /
`git checkout`. A turn that writes big untracked files makes the snapshots hash them (into the
object store, unreachable once the checkpoint is dropped).

## CodeGit (QML type, `src/app/gitpanel/codegit.h`)

One folder's git for the changes panel. Everything is asynchronous; results come as properties and
signals — nothing in the area calls into the rest of the app (the owner turns `actionFinished` into
toasts).

**Properties**

| Property | Type | |
|---|---|---|
| `folder` | string, writable | the repository's or a worktree's folder (any folder inside one) |
| `turnCheckpoint` | string, writable | a checkpoint whose changes `turnChanges` lists ("" none) |
| `pollInterval` | int, writable | a refresh every so many ms (0: none) |
| `loaded` | bool | the first refresh of this folder is done |
| `isRepo`, `topLevel` | bool, string | the work tree's top folder |
| `branch`, `upstream`, `ahead`, `behind`, `detached`, `hasCommits`, `headOid` | | from `# branch.*` |
| `changes` | list | rows `{path, oldPath, status, letter, staged, unstaged, additions, deletions, binary, name, dir}` — status "modified" \| "added" \| "deleted" \| "renamed" \| "untracked" \| "conflicted", letter M A D R U C, additions/deletions -1 when unknown, `name` the file's name, `dir` its folder ("" at the top); at most `maxRows` (2000) |
| `changeCount` | int | all changes (also beyond maxRows) |
| `turnChanges`, `turnLoaded`, `turnError` | list, bool, string | the same rows since `turnCheckpoint` (staged always false); `turnError` when they cannot be had (the checkpoint is gone) |
| `branchList` | list | after `branches()`: `{name, remote, current, upstream, ahead, behind, gone, worktreePath}` — the current one first, then local ones, then remote ones no local branch tracks |
| `worktreeList` | list | after `worktrees()`: `{path, branch, head, bare, detached, locked, prunable, current}` |
| `busy`, `busyAction` | bool, string | an action runs or waits; the one running |
| `refreshing` | bool | |
| `lastError` | string | the last action's failure (until one succeeds), else the last refresh's |
| `revision` | int | bumps on every change of the state above |
| `gitAvailable`, `ghAvailable` | bool | `git` / `gh` found on PATH |

**Invokables** — `refresh()`; `requestDiff(path, checkpoint)` ("" = against HEAD; a renamed row's
old text from its old path); `commit(message, push, createPr, prTitle, prBody, draft)` (T3 Code's
"Commit" / "Commit and push" / "Commit, push and create PR"; `prTitle` "" = the message's first
line); `push()`; `pull()`; `branches()`; `createBranch(name)`; `checkout(name)`;
`createPullRequest(title, body, draft)` (pushes first when there is no upstream or the branch is
ahead); `checkpoint()` → id; `changesSince(checkpoint)`; `restore(checkpoint)`;
`dropCheckpoints(ids)`; `worktrees()`; `addWorktree(path, branch, base)`;
`removeWorktree(path, force)`; helpers `subjectOf(message)`, `isValidBranchName(name)`.

**Signals** — `diffReady(path, oldText, newText, binary, tooLarge)`, `diffFailed(path, message)`,
`actionFinished(action, ok, message, url)` with action "commit", "push", "pull", "createBranch",
"checkout", "pullRequest", "restore", "dropCheckpoints", "addWorktree" (`url`: the new folder),
"removeWorktree", or a failed "checkpoint"; `checkpointMade(id, ok, message)`,
`changesSinceReady(checkpoint, rows)`, `branchesReady(rows)`, `worktreesReady(rows)`.

**`checkpoint()`** returns the new id at once and queues the snapshot; `checkpointMade(id, ok,
message)` says when it exists. The agent's turn must start after that signal — files written
before it would be part of the snapshot.

**Refreshes** run when `folder` changes, after every action, every `pollInterval` ms, and on the
watcher: a QFileSystemWatcher on this work tree's git folder (from `rev-parse --absolute-git-dir`:
`.git`, or `.git/worktrees/<name>` where `.git` is a file), on its `HEAD` and `index` (git replaces
both by renaming over them: the folder's watch sees that, and the files are watched again after
each refresh) and on the top folder's own entries — 300 ms after the first change. A folder that
is no repository is watched for a `git init`. The refreshes' own commands write nothing there
(`GIT_OPTIONAL_LOCKS=0`, temporary indexes outside the repository), so a refresh never starts
another. Changes deep in the tree that git does not see (an agent editing files) show with the
next action, `refresh()` or the poll. Refreshes never overlap: one asked for while one runs follows
it.

**Actions** run one after another in the order asked, so a restore never meets a checkpoint
being taken. When `folder` changes, actions still waiting are dropped (`actionFinished(…, false,
"The folder changed before it ran")`, and `checkpointMade(id, false, …)` for a checkpoint), the one
running ends where it ran, and answers for the old folder are ignored.

## The panel (`src/app/qml/gitpanel`)

**`GitChangesPanel`** — properties `folder`, `checkpoint` ("" none), `checkpointLabel` ("Turn 3"),
`pollInterval`, `mode` ("turn" | "all"); read-only `git` (its CodeGit), `rows`, `selectedPath`;
functions `openDiff(path)`, `closeDiff()`, `setMode(value)`, `openCommit()`, `openPullRequest()`,
`openBranchMenu()`, `openNewBranch()`; signal `actionFinished(action, ok, message, url)`.

* Header: the branch (`gitBranchButton` → `gitBranchMenu`: `gitBranchItem_<i>`, `gitNewBranch` →
  `gitNewBranchDialog` with `gitNewBranchName`), ahead / behind (`gitAheadBehind`: `gitAhead`,
  `gitBehind`) or "not pushed" (`gitNoUpstream`), a spinner (`gitBusy`), `gitRefresh`.
* With a checkpoint: `gitChangesMode` ("This turn" / "All changes") and `gitTurnLabel`.
* The list `gitChangeList` of `GitChangeRow`s (`gitChange_<i>`: `gitChangeLetter` in the status's
  colour, `gitChangeName`, `gitChangeDir` dimmed — or "from <old path>" for a rename —,
  `gitChangeBinary`, `gitChangeAdditions`, `gitChangeDeletions`); more than 2000: `gitHiddenCount`.
* A click shows the file's diff (`gitDiffPane`: `gitDiffPath`, `gitDiffView` — BDiffView, unified
  on narrow widths —, `gitDiffMessage` for a binary or too large file or a failure): beside the
  list when the panel is at least 760 px wide (`gitDiffClose`), in its place otherwise
  (`gitDiffBack`). The diff follows refreshes and closes when its file has no changes any more.
* Empty states: `gitNoChanges`, `gitTurnError`, `gitNotRepo` ("Not a git repository" with a hint;
  "No folder"; git not installed).
* Footer (`gitFooter`): `gitCommit` → `GitCommitDialog`, `gitPush` (with no upstream or ahead),
  `gitPull` (with an upstream), `gitCreatePr` (only with gh) → `GitPullRequestDialog`,
  `gitRevertTurn` (with a checkpoint) → `gitRevertDialog` (BConfirmDialog, destructive); the last
  failure in `gitError`.

**`GitCommitDialog`** (`gitCommitDialog`; property `git`, `openFor()`): the message
(`gitCommitMessage`), the choice `gitCommitChoice_commit` / `_push` / `_pr` (the last only with gh,
push and PR not on a detached HEAD), and for a pull request its title (`gitPrTitle`, the message's
first line until edited), description (`gitPrBody`) and `gitPrDraft`.

**`GitPullRequestDialog`** (`gitPullRequestDialog`; property `git`, `openFor()`):
`gitPullRequestTitle`, `gitPullRequestBody`, `gitPullRequestDraft`.

## Security

* **No shell.** Every program gets its arguments as a list (QProcess): a commit message, a branch
  name, a path or a pull request's text is never parsed as a command line (tested with `$(…)`,
  backticks and quotes). The commit message goes on standard input. Branch names are checked
  before git sees them (no leading `-`, so never an option), worktree paths are made absolute,
  checkpoint ids are a strict character set, and the paths `restore()` deletes are checked
  (relative, no `..`, no `.git`, no symbolic link on the way).
* **No prompts.** `GIT_TERMINAL_PROMPT=0` (a push without credentials fails at once instead of
  waiting for a password), standard input always closed, `GH_PROMPT_DISABLED=1`; every run has a
  time limit and is killed after it. Credential helpers and ssh agents the user has set up work
  as in a terminal.
* **The environment.** Variables that would point git elsewhere (`GIT_DIR`, `GIT_WORK_TREE`,
  `GIT_INDEX_FILE`, `GIT_OBJECT_DIRECTORY`, …, pathspec modes) are removed; `LC_ALL=C` (git's
  English messages, which the client reads); `GIT_PAGER=cat`, `--no-pager`, `color.ui=false`.
* **Reading never writes.** Status and the other reading commands run with
  `GIT_OPTIONAL_LOCKS=0` (they never take the index's lock from the agent's own git) and change
  nothing in the repository; snapshots go through temporary indexes. The checkpoints' refs are
  the only thing Brulion adds to a repository (`refs/brulion/checkpoints/`).
* **The repository's own configuration.** git reads the repository's configuration, which can
  run programs (hooks on commit, filters on add / checkout, `core.fsmonitor`). The commands that
  run by themselves — refreshes, diffs, branches, snapshots — turn `core.fsmonitor` off; diffs use
  `--no-ext-diff --no-textconv`; `cat-file` reads blobs without filters. A commit runs the
  repository's hooks, as `git commit` does. Open only folders you would run git in.
* **gh.** Only `gh pr create`, with the title, body, `--draft`, `--head` the user gave, in the
  project's folder, with gh's own sign-in (`gh auth login`); Brulion never sees or stores a
  GitHub token. gh pushes nothing itself: the branch is pushed with git first.

## Not done (yet)

* Staging single files or hunks, discarding a file's changes, stash, merge / rebase conflicts
  beyond listing them as "conflicted".
* Pull requests for forks (`--head owner:branch`) and a base branch chosen in the UI (the core
  takes `base`).
* Watching the whole working tree for changes git does not see (the panel relies on
  `pollInterval`, actions and explicit refreshes).
* The panel is not wired into the "Code" mode's shell here (`src/app/qml/shell`): the owner gives
  it `folder` / `checkpoint` and turns `actionFinished` into toasts.
