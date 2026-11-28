# Brulion "Code" — git (module `gitcli`, area `gitpanel`)

The "Code" mode works on projects that are git repositories: an agent's thread runs in a
project's folder or in a worktree of its own. This part is the git underneath — the working
tree's changes and their diffs (of an agent's turn, uncommitted, the branch against a base, a
commit), single files and hunks staged, unstaged or thrown away, commits of chosen files, push /
pull, branches, worktrees, stashes, merges and rebases stopped on conflicts, pull requests through
the GitHub CLI (from forks, into any base; the branch's own as a badge), publishing and `git init`,
and **checkpoints**: a snapshot of the working tree before each agent turn, so the user can see what
the turn changed and put the files back. It has two layers:

| Layer | Where | What |
|---|---|---|
| core module `gitcli` | `src/core/gitcli/` (QtCore only, namespace `brulion::gitcli`) | parsers of git's machine-readable output (`gitcli.h`) and `Client` (`client.h`), which runs `git` and `gh` asynchronously with QProcess; checkpoints in `checkpoints.cpp` |
| app area `gitpanel` | `src/app/gitpanel/` (library `brulion_gitpanel`), `src/app/qml/gitpanel/` | `CodeGit` (QML type): one folder's git, kept up to date, as properties and signals; `gitdiffrows.h`: the panel's diff rows, file tree and review comments; the changes panel and its dialogs |

Tests: `tests/core/gitcli` (`tst_gitcli`: parsers, no git needed; `tst_gitcli_repo`: `Client` with
real git in temporary repositories), `tests/core/codeai` (`tst_codeai`: the AI's questions and its
answers read — core module `codeai`, Code mode's texts written by the AI) and `tests/app/gitpanel`
(`tst_codegit`: `CodeGit` with real git; `tst_gitdiffrows`: the pure rows). The tests with real git skip themselves where `git` is not on PATH; the Nix check build has
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
| `parseLog()` | `git log --format=<logFormat()>` — six NUL-terminated fields a commit (id, parents, author's name, e-mail, time, subject) |
| `parseStashes()` | `git stash list --format=<stashFormat()>` — `stash@{n}`, the commit, the time, the message (and the branch it names) |
| `parseRemotes()`, `parseRemoteUrl()`, `forkTarget()` | `git remote -v`; a remote's URL as host / owner / name (scp-like, https with a token, ssh with a port; never a local path); the fork case: the branch pushed to one remote and an `upstream` remote another repository on the same host → `--repo <upstream>` (with the host for GitHub Enterprise) and `--head <owner>:<branch>` |
| `parsePullRequestView()` | `gh pr view --json number,url,state,isDraft,title,baseRefName` — state "open" / "draft" / "merged" / "closed"; invalid without a number or an http(s) URL |
| `parsePatch()`, `hunksTouching()`, `patchOf()` | one file's unified diff: the header and the hunks (counts that do not add up drop a hunk; `\ No newline` lines kept; a CR stays its line's), each with the lines it changes on each side (a deletion counts on the new side at the line it comes before, an insertion on the old side likewise, a replaced line at itself); the hunks that change a range of lines; a patch of some hunks for `git apply` — the new sides' starts counted again as if only they were applied (`reverse`: the old sides', for `git apply -R`) |
| `hasConflictMarkers()` | a line that starts with `<<<<<<<` or `>>>>>>>` followed by a space or nothing |

`changeOf()` turns an entry into one word for the UI (`Change`: Modified, Added, Deleted, Renamed,
Untracked, Conflicted, Ignored — a copy is Added, a type change Modified), `changeName()` /
`changeLetter()` give "modified" / "M" and so on. Rules shared with the app: `isValidBranchName()`
(git's `check-ref-format --branch` rules, a little stricter; letters of any language allowed),
`isValidCheckpointId()`, `isSafeRelativePath()` (relative, `/`-separated, no empty, `.`, `..` or
`.git` part, no backslash, no drive), `isSafeRevision()` (no leading `-`, no space, control
character, `:` or backslash: every revision the client hands git), `isValidRepositoryName()`
(`name` or `owner/name` of `[A-Za-z0-9._-]`), `looksBinary()` (git's own test: a NUL in the first
8000 bytes), `subjectOf()` (a message's first non-blank line).

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
| `fileTexts(folder, {path, oldPath, base, target})` | `cat-file blob <base>:<oldPath or path>`; the new text read from disk (a symbolic link's target as git stores it), or with `target` `cat-file blob <target>:<path>` | `FileTexts`: oldText, newText, oldExists, newExists, binary, tooLarge (> 2 MiB) |
| `branches(folder)` | `branch -a --sort=-committerdate --format=…` | `BranchList` |
| `worktrees(folder)` | `worktree list --porcelain -z` | `WorktreeList` |
| `commitAll(folder, message)` | `add -A`, `commit --file=-` (the message on standard input; hooks run) | `Outcome` (value: the commit) — "Nothing to commit", an empty message refused |
| `push(folder)` | `push`; without an upstream `push -u <origin or the only remote> <branch>`; refused on a detached HEAD | `Outcome` |
| `pull(folder)` | `pull --ff-only` | `Outcome` ("Already up to date") |
| `createBranch(folder, name)` | `switch -c <name>` (the working tree's changes come along) | `Outcome` |
| `checkout(folder, name)` | `switch <name>`; a remote-tracking branch without a local one: `switch --track <name>` | `Outcome` |
| `addWorktree(folder, path, branch, base)` | `worktree add [-b <branch>] -- <path> [<base>]` | `Outcome` (value: the folder) |
| `removeWorktree(folder, path, force)` | `worktree remove [--force] -- <path>` (without force git refuses one with changes) | `Outcome` |
| `createPullRequest(folder, {title, body, draft, head, base, repo})` | `gh pr create --title … --body … [--draft] [--head …] [--base …] [--repo …]` in the folder | `Outcome` with `url`; an existing pull request for the branch is a success with its url; gh missing or not signed in say so ("run gh auth login") |
| `log(folder, revision, max)` | `log -n <max> --format=<logFormat()> <revision or HEAD> --` | `CommitList` (none, no error, in a new repository) |
| `mergeBase(folder, a, b)` | `merge-base a b` | `Outcome` (value: the commit; "no commit in common" otherwise) |
| `defaultBranch(folder)` | `symbolic-ref refs/remotes/origin/HEAD`, else `main` / `master` (remote-tracking first), else init.defaultBranch's branch when it exists | `DefaultBranch`: name ("main"), ref to compare with ("origin/main" or "main") |
| `remotes(folder)`, `pullRequestTarget(folder, branch)` | `remote -v`; with `for-each-ref --format=%(push:remotename)` (git's own rules: pushRemote, pushDefault, the branch's remote) `forkTarget()` | `RemoteList`; `ForkTarget` {repo, head} |
| `pullRequestFor(folder, head, repo)` | `gh pr view [<head>] [--repo …] --json number,url,state,isDraft,title,baseRefName` | `PullRequestStatus`: ok (gh answered), found, the pull request; "no pull requests found" is an answer |
| `initRepository(folder)`, `publish(folder, name, isPublic)` | `init`; `gh repo create <name> --private\|--public --source <folder> --remote origin --push` | `Outcome` (publish: `url` the repository's address gh printed) |
| `stage(folder, paths)`, `unstage(folder, paths)` | `add -A` / `restore --staged` (before the first commit `rm --cached -r --ignore-unmatch`), the paths as literal pathspecs on standard input (`--pathspec-from-file=- --pathspec-file-nul`, `GIT_LITERAL_PATHSPECS=1`) | `Outcome` |
| `discard(folder, path, oldPath)` | refused for a conflicted file (`ls-files -u`), a folder, a path that leaves the work tree or goes through a link; paths HEAD has (`ls-tree`): `restore --source=HEAD --staged --worktree`; others: `rm --cached`, then the file to the system's trash (`QFile::moveToTrash`; deleted where there is none) | `Outcome` |
| `resolve(folder, path, Ours \| Theirs \| Resolved)` | `checkout --ours\|--theirs` + `add` (a side without the file — `ls-files -u` has no stage 2 / 3 — `rm`); Resolved: refused while `hasConflictMarkers()`, else `add` | `Outcome` |
| `applyHunk(folder, path, Stage \| Unstage \| Discard, first, last)` | git's own diff of the file (`diff -U3` index → work tree; Unstage `diff --cached` HEAD → index), `hunksTouching()` the lines (the work tree's; HEAD's for Unstage), `patchOf()` → `apply --cached` / `apply --cached -R` / `apply -R` (the work tree) | `Outcome`; a binary file or lines without such changes say so |
| `conflictMarkers(folder, paths)` | the files read 16 (or 4 MiB) at a time from the event loop: those with `hasConflictMarkers()` (binary, missing, > 2 MiB ones not) | the paths |
| `commit(folder, {message, mode, paths, untracked, newBranch})` | refused when a file of `paths` holds conflict markers (naming them); `switch -c <newBranch>` first; mode "all": `add -A` + `commit --file=-`; "staged": `commit --file=-` (the index as it is); "paths": the untracked ones `add`ed, then `commit --only --file=- --pathspec-from-file=<temp> --pathspec-file-nul` (the work tree's text of those paths only; what is staged of other files stays staged) — on failure the added files go out of the index again | `Outcome` (value: the commit) |
| `operationIn(gitDir)` (static) | the files git keeps meanwhile: `rebase-merge/`, `rebase-apply/` (`applying`: am), `MERGE_HEAD`, `CHERRY_PICK_HEAD`, `REVERT_HEAD` | `Operation` ("merge", "rebase", "cherryPick", "revert", "apply") |
| `abortOperation(folder, op)`, `continueOperation(folder, op)` | `<merge\|rebase\|cherry-pick\|revert\|am> --abort`; a merge `commit --no-edit` (its MERGE_MSG), the others `--continue` — `GIT_EDITOR=true`, `GIT_SEQUENCE_EDITOR=true` (never an editor) | `Outcome` ("Resolve every conflicted file first" while some are) |
| `stashes(folder)`, `stashPush(folder, message)`, `stashApply(folder, oid, drop)`, `stashDrop(folder, oid)` | `stash list`; `stash push --include-untracked [--message …]`; `stash apply <commit>` (Pop: then dropped); `stash drop stash@{n}` with n found again by the commit just before (the stash stack is shared by the repository's worktrees) | `StashList`, `Outcome` |
| `checkpoint`, `changesSince`, `changesBetween`, `revertBetween`, `restore`, `dropCheckpoints`, `checkpoints` | below | |

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

**Between any two** (`changesBetween(folder, base, target)`): the same with `base` any revision
(`""` the empty tree, `hash-object -t tree --stdin` of nothing: SHA-1 or SHA-256) and `target` a
revision or `""` the working tree's snapshot — a turn's own changes (its checkpoint and the next
one), a commit's (its first parent, or nothing for a root commit), a branch's (the commit it shares
with a base, `mergeBase()`, and the working tree).

**An older turn undone alone** (`revertBetween(folder, base, target)`): `diff-tree -p --binary
--no-renames <base> <target>` applied in reverse to the work tree (`apply -R`: all or nothing — when
a later turn changed the same lines, nothing is written and the message says so). The index and HEAD
stay; files the turn added go, files it deleted come back.

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
| `changes` | list | rows `{path, oldPath, status, letter, staged, unstaged, additions, deletions, binary, name, dir, kind, depth, collapsed, fileCount}` — status "modified" \| "added" \| "deleted" \| "renamed" \| "untracked" \| "conflicted", letter M A D R U C, additions/deletions -1 when unknown, `name` the file's name, `dir` its folder ("" at the top), `kind` "file" and the tree's other keys (treeRows()); at most `maxRows` (2000) |
| `changePaths`, `stagedPaths`, `conflictPaths`, `conflictCount` | | every changed path (beyond maxRows too), those with something staged, the conflicted ones |
| `operation` | string | a merge, rebase … stopped here: "merge" \| "rebase" \| "cherryPick" \| "revert" \| "apply" ("" none) — `Client::operationIn()` at each refresh |
| `defaultBranch`, `defaultBranchRef`, `onDefaultBranch` | string, string, bool | `Client::defaultBranch()` (asked again when the branch, its upstream, the config or origin/HEAD change) |
| `hasRemote`, `remoteNames` | bool, list | the repository's remotes |
| `pullRequest` | map | the branch's pull request `{number, url, state ("open" \| "draft" \| "merged" \| "closed"), title, baseRefName}` ({} none); asked (gh, the fork's `--repo` / `owner:branch` when there is one) when the branch or its upstream changes, after a push or a pull request, on `refreshPullRequest()` — a failing gh (offline) keeps what was known. For the panel's badge and the sidebar |
| `primaryAction` | string | T3 Code's quick action: "commit" (changes), "push" (no upstream, or ahead), "pull" (behind), "openPr" (it has an open one), "createPr" (pushed, gh, not the default branch), "publish" (no remote, gh), "init" (no repository); "" nothing (or an operation in progress) |
| `compareActive`, `compareBase`, `compareTarget`, `comparisonBase`, `compareChanges`, `compareLoaded`, `compareError` | | compare()'s comparison: its rows (as `changes`', staged always false), the base it used (merge-base's commit) |
| `commitList` | list | after `commits()`: `{oid, shortOid, subject, author, time, when ("5 minutes ago"), parentOid}`, the newest 100 of HEAD |
| `stashList` | list | after `stashes()`: `{index, oid, message, branch, time, when}`, the newest first |
| `changeCount` | int | all changes (also beyond maxRows) |
| `turnChanges`, `turnLoaded`, `turnError` | list, bool, string | the same rows since `turnCheckpoint` (staged always false); `turnError` when they cannot be had (the checkpoint is gone) |
| `branchList` | list | after `branches()`: `{name, remote, current, upstream, ahead, behind, gone, worktreePath}` — the current one first, then local ones, then remote ones no local branch tracks |
| `worktreeList` | list | after `worktrees()`: `{path, branch, head, bare, detached, locked, prunable, current}` |
| `busy`, `busyAction` | bool, string | an action runs or waits; the one running |
| `refreshing` | bool | |
| `lastError` | string | the last action's failure (until one succeeds), else the last refresh's |
| `revision` | int | bumps on every change of the state above |
| `gitAvailable`, `ghAvailable` | bool | `git` / `gh` found on PATH |

**Invokables** — `refresh()`; `requestPatch(paths)` (the checked files' changes as text for the AI: `patchReady(text)`);
`commitQuestion(patch)` (the AI's question for a commit message of it on this branch: `{system, prompt}`,
`codeai::commitMessage`); `requestPullRequestQuestion(base)` (the AI's question for a pull request's title
and description: the branch's commits — `git log base..HEAD`, their bodies too, the newest 200 —, `git diff
--stat base...HEAD` and `git diff -U2 base...HEAD`, each bounded — `codeai::pullRequest` —, against `base`
or "" the default branch, a branch the default branch's remote has by that name first:
`pullRequestQuestionReady(system, prompt, "")`, or `("", "", why)` — no branch, a base that is none or
looks like an option, nothing the base lacks); `pullRequestText(answer)` (the answer read,
`codeai::readPullRequest`: `{title, body}`, title "" none);
`requestDiff(path, checkpoint)` ("" = against HEAD; a renamed row's
old text from its old path); `commit(message, push, createPr, prTitle, prBody, draft)` (T3 Code's
"Commit" / "Commit and push" / "Commit, push and create PR"; `prTitle` "" = the message's first
line); `push()`; `pull()`; `branches()`; `createBranch(name)`; `checkout(name)`;
`createPullRequest(title, body, draft)` (pushes first when there is no upstream or the branch is
ahead); `checkpoint()` → id; `changesSince(checkpoint)`; `restore(checkpoint)`;
`dropCheckpoints(ids)`; `worktrees()`; `addWorktree(path, branch, base)`;
`removeWorktree(path, force)`; helpers `subjectOf(message)`, `isValidBranchName(name)`.

The diff sources: `compare(on, base, target, mergeBase)` (`target` "" the working tree; `mergeBase`:
the commit HEAD shares with `base`, recomputed with each refresh); `requestDiffBetween(path,
oldPath, base, target)`; `commits()`; `turnRows(checkpoints)` (WorkspaceChats.checkpoints() →
`{index, id, number, label ("Turn 3"), at, latest, base, target}`, the newest first: a turn's
checkpoint and the next one's ref). Files and hunks: `stage(paths)`, `unstage(paths)`,
`discard(path, oldPath)`, `resolve(path, "ours" | "theirs" | "resolved")`, `stageHunk(path, first,
last)` / `discardHunk` (the work tree's lines) / `unstageHunk` (HEAD's lines). Commits:
`commitFiles(message, paths, newBranch, push, createPr, prTitle, prBody, draft, prBase)` —
`commitModeFor(paths)`: every changed file → "all", exactly the staged ones → "staged" (also a merge
in progress → "all"), else "paths" (new files among them added); `createPullRequestTo(title, body,
draft, base)` (from a fork: `pullRequestTarget()`); `refreshPullRequest()`; `initRepository()`;
`publish(name, isPublic)`; `revertBetween(base, target)`. Merges and stashes: `abortOperation()`,
`continueOperation()`, `stashes()`, `stashPush(message)`, `stashApply(oid)`, `stashPop(oid)`,
`stashDrop(oid)`. Pure helpers for the panel (gitdiffrows.h): `diffRows(old, new, split,
ignoreWhitespace, context)`, `findRows(rows, query)`, `longestLine(rows)`, `lineHtml(…)`,
`treeRows(changes, collapsed)`, `reviewComment(path, rows, from, to, comment)`, `rowOf(rows, path)`,
`branchNameFor(message)`, `isValidRepositoryName(name)`, `unavailableReason(action)` (why "commit",
"push", "pull", "createPr", "publish", "init", "stash" cannot run now — a disabled entry's tooltip).

**Signals** — `diffReady(path, oldText, newText, binary, tooLarge)`, `diffFailed(path, message)`,
`patchReady(text)`, `pullRequestQuestionReady(system, prompt, error)`,
`actionFinished(action, ok, message, url)` with action "commit", "push", "pull", "createBranch",
"checkout", "pullRequest", "restore", "dropCheckpoints", "addWorktree" (`url`: the new folder),
"removeWorktree", "stage", "unstage", "discard", "resolve", "stageHunk", "unstageHunk",
"discardHunk", "init", "publish" (`url`: the repository), "revertTurn", "abort", "continue",
"stash", "stashApply", "stashPop", "stashDrop", or a failed "checkpoint"; `checkpointMade(id, ok,
message)`, `changesSinceReady(checkpoint, rows)`, `branchesReady(rows)`, `worktreesReady(rows)`,
`pullRequestChanged()`, `compareChanged()`, `commitListChanged()`, `stashListChanged()`.

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

**`GitChangesPanel`** — properties `folder`, `checkpoints` (WorkspaceChats.checkpoints(): the
thread's turns), `checkpoint` / `checkpointLabel` (one turn instead, kept for older owners),
`pollInterval`, `source` ("turn" | "uncommitted" | "branch" | "commit"; "turn" without turns shows
Uncommitted), `turnIndex` (-1 the latest), `baseRef` ("" the default branch), `commitOid`; view
options kept per device in AppSettings (`code/diffSplit`, `code/diffWrap`,
`code/diffHideWhitespace`, `code/diffTree`); read-only `git` (its CodeGit), `rows`, `selectedPath`,
`mode` ("turn" | "all"); functions `setSource(value)` / `setMode(value)`, `showTurn(index)`,
`setBase(ref)`, `pickCommit(oid, parent, title)`, `openDiff(path, oldPath)`, `closeDiff()`,
`openCommit()`, `openPullRequest()`, `openBranchMenu()`, `openNewBranch()`, `run(action)`. Signals —
the owner does what the panel cannot: `actionFinished(action, ok, message, url)` (toasts; a disabled
entry chosen on a touch screen says why this way too), `openFile(absolutePath, line)` (CodeView:
`Workspace.projects.openInEditor`), `addToChat(text)` (CodeView: `ChatView.addToComposer`),
`copyText(text)`, `openUrl(url)` (the pull request).

* Header (`gitHeader`): the branch (`gitBranchButton` → `gitBranchMenu`: `gitBranchItem_<i>`,
  `gitNewBranch` → `gitNewBranchDialog` with `gitNewBranchName`, `gitStashes` → the stashes), the
  pull request's badge (`gitPrBadge`: "#12 open" / draft / merged / closed; a click opens it), ahead /
  behind (`gitAheadBehind`: `gitAhead`, `gitBehind`) or "not pushed" / "no remote"
  (`gitNoUpstream`), a spinner (`gitBusy`), the view options (`gitViewOptions` → `gitOptionsMenu`:
  `gitOptSplit` — side by side, on panels at least 760 px wide —, `gitOptWrap`, `gitOptWhitespace`,
  `gitOptTree`, `gitOptFind`), `gitRefresh` (also asks for the pull request again).
* The source (`gitSourceRow`): `gitSource` → `gitSourceMenu` — "Latest turn" and "Turn N" for each
  older one (`gitSource_turn_<i>`, the newest first, with their time), `gitSource_uncommitted`,
  `gitSource_branch` (then `gitBaseButton` → `gitBaseMenu`: `gitBase_<i>`, local and remote-tracking
  branches, "default" marked), `gitSource_commits` (then `gitCommitPick` → `gitCommitMenu`:
  `gitCommitItem_<i>` — subject, short id, when; until one is picked `gitPickCommit`); for a turn its
  time (`gitTurnLabel`). A turn's own changes are its checkpoint against the next one's (the latest:
  against the working tree) — changes the user made between two turns count to the turn before
  them.
* A merge, rebase, cherry-pick, revert or `git am` stopped (`gitOperationBanner`): what it is
  (`gitOperationTitle`), how many files are conflicted (`gitOperationHint`), each conflicted file
  (`gitConflict_<i>`: `gitConflictOurs`, `gitConflictTheirs`, `gitConflictResolved`,
  `gitConflictOpen`), `gitAbort` (confirmed: `gitAbortDialog`), `gitSkip` ("Skip this commit", not for
  a merge: `CodeGit.skipOperation()` → `gitcli::Client::skipOperation()`, `git rebase|cherry-pick|revert|am
  --skip` without an editor — the commit that stopped it left out, the operation going on) and
  `gitContinue` ("Commit the merge" / "Continue", once no file is conflicted). The quick action is
  hidden meanwhile.
* The list `gitChangeList` of `GitChangeRow`s — flat (`gitChange_<i>`: `gitChangeLetter` in the
  status's colour, `gitChangeName`, `gitChangeDir` dimmed — or "from <old path>" for a rename —, a dot
  when something is staged (`gitChangeStaged`, amber when only partly), `gitChangeBinary`,
  `gitChangeAdditions`, `gitChangeDeletions`, `gitChangeMore` on hover) or a tree (`gitFolder_<i>`:
  a chevron folds it, chains of single folders in one row, the count of files under it); more than
  2000: `gitHiddenCount`. A right click, a long press, ⋯ or the Menu key: `gitFileMenu` —
  `gitFileOpen`, `gitFileCopyPath`, and for Uncommitted `gitFileStage`, `gitFileUnstage`,
  `gitFileOurs` / `gitFileTheirs` / `gitFileResolved` (a conflicted file), `gitFileDiscard`
  (confirmed: `gitDiscardDialog` — a new file goes to the trash).
* A click shows the file's diff (`gitDiffPane`: `gitDiffPath`, `gitDiffMore` — the file's menu —,
  `GitDiffView`, `gitDiffMessage` for a binary or too large file or a failure): beside the list when
  the panel is at least 760 px wide (`gitDiffClose`), in its place otherwise (`gitDiffBack`). The diff
  follows refreshes and closes when its file has no changes any more.
* Empty states: `gitNoChanges` (per source), `gitTurnError` (the comparison failed: a checkpoint
  gone, a base with nothing in common), `gitNotRepo` ("Not a git repository" with
  `gitInitButton`; "No folder"; git not installed).
* **Fetching by itself**: `CodeGit.fetchInterval` (GitChangesPanel's `fetchInterval`, five minutes)
  runs a quiet fetch while the branch has an upstream and no action runs (`fetchQuietly()`: no
  actionFinished(), a failure kept in `lastFetchError`), then refreshes: ahead / behind and Pull follow
  the remote.
* Footer (`gitFooter`): the quick action `gitPrimary` (CodeGit.primaryAction: "Commit…", "Push N",
  "Pull N", "Create PR…", "Open PR #12", "Publish…", "Initialize git"; "Up to date" disabled) and
  `gitMoreActions` → `gitActionsMenu` (`gitCommit`, `gitPush`, `gitPull`, `gitFetch` — `git fetch
  --prune --quiet`, `Client::fetch()`, actionFinished("fetch") —, `gitCreatePr`,
  `gitPublish`, `gitStash`, `gitNewBranchAction`; a disabled entry says why in its tooltip:
  CodeGit.unavailableReason()); for a turn `gitRevertTurn` (that turn alone: the latest restores its
  checkpoint, an older one `revertBetween()`) and `gitRevertTo` ("Revert to before turn N…": its
  checkpoint restored, later turns' changes go too) → `gitRevertDialog`; the last failure in
  `gitError`. Push (the quick action or the menu) on the default branch asks first.

**`GitDiffView`** (`gitDiffView`; properties `git`, `path`, `oldText`, `newText`, `split`, `wrap`,
`ignoreWhitespace`, `expanded`, `canStage` / `canUnstage` / `canDiscard`; signals
`hunkRequested(action, first, last)`, `commented(text)`): the rows of CodeGit.diffRows
(`gitDiffRows`) — a header per hunk (`gitHunk_<i>`: "@@ -a,b +c,d @@" and, for Uncommitted files
that are tracked and not conflicted, `gitHunkStage` / `gitHunkUnstage` / `gitHunkDiscard` —
discarding asks first), "N unchanged lines" rows that show the whole file, changed words marked,
line numbers; long lines wrapped or, stacked, scrolled sideways (side by side: elided); hidden
whitespace changes (`gitDiffIdentical` when nothing else changed). **Find** (Ctrl+F while the diff
has focus, or the options' "Find in the diff"): `gitDiffFindBar` (`gitDiffFind`,
`gitDiffFindCount` "3 of 12", `gitDiffFindPrevious` / `gitDiffFindNext` — Shift+Enter / Enter —,
`gitDiffFindClose`; Escape), the whole file shown while searching, matches marked, the current one's
row highlighted. **Review comments**: a click on a line selects it, Shift+click (a long press on touch
screens) extends the selection; `gitDiffCommentBar` (`gitDiffSelection`, `gitDiffComment`,
`gitDiffCommentAdd` "Add to chat", `gitDiffCommentCancel`) — the file, the lines ("lines 10–14", or
"old line 3" for deleted ones), the lines in a fenced block (the file's language; `diff` with + / −
when deleted lines are among them; the fence longer than any run of backticks) and the comment go to
the composer, where several comments collect before sending.

**`GitCommitDialog`** (`gitCommitDialog`; property `git`, `openFor()`, signal `commitRequested(…)`,
which the panel checks against the default branch): the files (`gitCommitFiles`:
`gitCommitFile_<i>` checkboxes — all checked, or the staged ones when something is staged;
`gitCommitAll` All / None) and what the commit takes (`gitCommitMode`: everything / the staged
changes / only the checked files as they are), the message (`gitCommitMessage`; **Write with AI**
`gitCommitWriteAi` — T3 Code's AI-written message — while the owner says the AI can (`aiAvailable`):
the checked files' changes from `CodeGit.requestPatch(paths)` → `patchReady(text)` (`git diff HEAD`
of the tracked ones with two lines of context, a new file's first lines, at most
`MaxPatchChars`), a question of them (`CodeGit.commitQuestion`) to the owner (`writeRequested(system,
prompt)` → the panel's `writeWithAi`; CodeView's `AiAsk`, the AI of Settings → Extensions → AI,
src/app/shell/aiask.h) whose answer fills the field (`aiWritten(ok, text)`; a failure under it:
`gitCommitAiError`); while it is written the files and the field wait and the dialog is `busy`). **The
field left empty** — T3 Code's way — with the AI there: a note says so (`gitCommitAiNote`: "Left empty,
the AI writes the message from the checked files' changes when you commit."), and the commit asked for
(the button, Ctrl+Enter: `acceptRequested`) writes the message first (the button "Writing the
message…", busy) and then accepts as the button would — with the message in the field to see, the PR
title and a new branch's name made of it as when typed; a branch name it makes that git refuses keeps
the dialog open, the AI's failure commits nothing and says why. Without the AI an empty message
commits nothing, as before; Cancel while it writes drops the answer. "Commit on a new
branch" (`gitCommitNewBranch`, `gitCommitBranchName` — made of the message until edited), the choice
`gitCommitChoice_commit` / `_push` / `_pr` (the last only with gh; push and PR not on a detached HEAD
unless on a new branch), and for a pull request its title (`gitPrTitle`, the message's first line
until edited), description (`gitPrBody`), base (`gitPrBase`, a `GitBasePicker`) and `gitPrDraft`.

**`GitDefaultBranchDialog`** (`gitDefaultBranchDialog`): a commit and push or a push on the default
branch — Cancel, Continue, or `gitGuardBranch` "Create a branch and continue" (`gitGuardBranchName`).

**`GitPullRequestDialog`** (`gitPullRequestDialog`; property `git`, `openFor()`):
`gitPullRequestTitle`, `gitPullRequestBody`, `gitPullRequestBase` (`GitBasePicker`: the default
branch unless another is chosen; a remote-tracking branch as its name on the remote),
`gitPullRequestDraft`. **Write with AI** (`gitPullRequestWriteAi`, while the owner says the AI can:
`aiAvailable` — T3 Code's AI-written pull request): the question of the branch against the base chosen
(`CodeGit.requestPullRequestQuestion(base)` → `pullRequestQuestionReady`) to the owner
(`writeRequested(system, prompt)`, the same `writeWithAi` / `aiWritten(ok, text)` as the commit
dialog's), whose answer — the title on its first line, then the description in Markdown with "##
Summary" and "## Testing" — fills both fields (`CodeGit.pullRequestText`), to edit before Create PR; no
title in it, a base that is none or a branch without commits of its own: `gitPullRequestAiError`. From a fork (an "upstream" remote that is another repository) the pull request
goes into the upstream repository (`--repo`, `--head owner:branch`).

**`GitPublishDialog`** (`gitPublishDialog`): the name (`gitPublishName`, the folder's by default) and
`gitPublishVisibility` (private / public) for `gh repo create … --source --remote origin --push`.

**`GitStashDialog`** (`gitStashDialog`): `gitStashMessage` and "Stash changes" (untracked files
too), the stashes (`gitStashList`: `gitStash_<i>` with `gitStashApply`, `gitStashPop`,
`gitStashDrop` — confirmed by `gitDropStashDialog`; `gitNoStashes`).

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
* **gh.** Only `gh pr create` (the title, body, `--draft`, `--head`, `--base`, `--repo` the user
  or the remotes gave), `gh pr view … --json` (the branch's pull request) and `gh repo create <name>
  --private|--public --source <folder> --remote origin --push` (Publish…, the name checked:
  `[A-Za-z0-9._-]`, `owner/name`), in the project's folder, with gh's own sign-in (`gh auth login`);
  Brulion never sees or stores a GitHub token. `gh pr create` pushes nothing itself: the branch is
  pushed with git first.
* **Paths and revisions as data.** Paths reach git as literal pathspecs (`--pathspec-from-file=-`
  with `GIT_LITERAL_PATHSPECS=1`, or `:(literal)`): a file called `:(glob)*` or `-n` is that file.
  Every revision is checked (`isSafeRevision()`: no leading `-`, no `:`); hunks reach `git apply`
  as patches made of git's own diff of the file, on standard input.
* **Throwing away.** `discard()` refuses a conflicted file, a folder (a nested repository) and a path
  through a symbolic link; files HEAD does not have go to the system's trash (deleted where there is
  none: another volume without a trash folder). A hunk thrown away and Discard… ask first.
* **Merges and rebases.** Continue never opens an editor (`GIT_EDITOR=true`,
  `GIT_SEQUENCE_EDITOR=true`); "Mark resolved" refuses a file that still holds conflict markers, as a
  commit does.

## Not done (yet)

* The pull request of "Commit, push and create PR" written by AI as well (its title follows the
  commit message's first line, its description is typed; the pull request dialog's "Write with AI"
  writes both), and a repository's pull request template or the thread's conversation in the question
  (T3 Code sends them).
* A turn's changes exclude what the user changed between turns in T3 Code; here they count to the
  turn before them (the snapshots are of the whole working tree).
* Hunks of untracked, conflicted or binary files; lines (not whole hunks) staged; the hunks of the
  other sources (a turn, the branch) staged or thrown away.
* Editing the message of a merge.
* Watching the whole working tree for changes git does not see (the panel relies on
  `pollInterval`, actions and explicit refreshes).
* Expand / collapse all files of a source at once (the panel shows one file's diff at a time) and the
  diff's own colours for colour-blind users.
