# Code mode — projects and AI agents' threads, as in T3 Code

Brulion has two modes (`AppSettings.appMode`, key `ui/mode`, a device setting):

* **Notes** (the default): the notebook — journal, pages, tasks, the AI chat about the notes.
* **Code**: projects — folders of this computer, git repositories usually — and AI agents' threads in
  them, the way [T3 Code](https://github.com/pingdotgg/t3code) works: a thread is one task given to
  Claude Code, Codex, OpenCode or any agent of the Agent Client Protocol (docs/UI.md "AI agents
  (ACP)"), working in the project's folder or in a git worktree of its own, with the project's changes
  beside the conversation, a terminal under it, commits, pushes and pull requests — and Brulion's
  notifications when an agent answers, fails, asks something or tells the user something itself.

The mode switches on the rail's top button (`modeSwitch`), in the palette ("Code mode" `mode.code`,
"Notes mode" `mode.notes`) and in Settings → Extensions → Code mode. Switching navigates to the mode's
home: Code mode's route `code`, Notes mode's journal. A notebook opened in Code mode starts in route
`code` (AppController's first route), unless a link or a notification asks for another.

Code mode needs a notebook of the user's open — its threads are pages of it — but shows none of the
notebook: the rail offers its threads alone, the page panel is replaced by the projects' panel, the
bottom navigation is gone. Reminders, sync, notifications and the rest of the notebook go on.

## Projects

`shell/codeprojects.h` (`Workspace.projects`, QML type `CodeProjects`; `codeprojects.cpp` the list and
settings, `codeconfig.cpp` t3.json, `codeworktrees.cpp` git and gh, `codeeditors.cpp` "Open in…"):
this device's projects, kept in its settings (`code/projects`, JSON: `{projects: [{id, name, path,
pinned?, icon?, agent?, env?, scripts?: [{name, command, icon, shortcut?, runOnWorktreeCreate?,
wait?}]}], worktreesRoot?, clonesRoot?, editor?, preferredEditor?, branchPrefix?, deletedWorktrees?, newThreadEnv?,
threadSort?}`) — **never in the notebook**: a path is this device's, and a synced file must never point
an agent at a folder.

* `add(folder)` (a path or a `file:` URL) gives the project's id: a slug of the folder's name
  (`slug()`: ASCII letters, digits and dashes, Polish letters without their marks — "Projekt żółwia" →
  `projekt-zolwia`), `-2`, `-3` … when taken; a folder added again gives its own id. At most 100.
* `list`: pinned projects first, each part in the user's order; `move(id, index)` moves one within its
  part (the sidebar's drag), `setPinned()`, `rename()`, `remove()` (its folder and threads stay).
* `setIcon(id, emoji)` (one grapheme; without one the sidebar shows t3.json's `iconPath` picture, else a
  folder), `setDefaults(id, agent, env)`: the agent of its new threads ("" the device's default) and
  where they work ("" the device's default, `local`, `worktree`).
* `newThreadEnv(id)` — where a new thread works unless its header chooses otherwise: the project's own
  `env`, else the device's `newThreadEnvDefault` (Settings → Extensions → Code mode → "New threads
  work"), else t3.json's `defaultThreadEnvMode`, else `local` (T3 Code's order: the project's override,
  the device's setting, the repository's file, the built-in default).
* `worktreesRoot` (default `~/.brulion/worktrees`; Windows `%USERPROFILE%\.brulion\worktrees`), `branchPrefix` (default `brulion/`; only what a
  branch may start with), `deletedWorktrees` (`ask` | `delete` | `keep`), `threadSort` (`activity` |
  `created`), `projectOfFolder(folder)`. On Windows a folder added in another case is the same
  project; an editor's command is split by Windows' rules (`splitWindowsCommand()`, a path's
  backslashes kept) and an editor that is a batch file — VS Code's and Cursor's `code.cmd` — runs
  through `cmd.exe /d /v:off /s /c` with every argument quoted (`batchCommandLine()`, which refuses a
  quote, `%` or a line break; `startEditor()`).
* **Projects of the user's other computers** (`remote`, `shell/remoteprojects.h`, QML
  `Workspace.projects.remote`): listed after this device's — `{id, name, path "", exists false, scripts
  [], remote true, host, remotePath}`, read-only, `folderOf()` "" — when a computer that shares its
  agents with this device offers them ("Threads on the phone" below). `isRemote(id)`; a project id of
  this device's own hides one of a computer, and of two computers the first one's is listed.

### Cloning a repository

"Clone a repository…" (T3 Code's Add project → clone; the sidebar's add menu `codeAddProjectMenu`,
the empty states, the header's project menu, the palette's `code.cloneRepository` — route
`{view: "code", cloneRepository: true}`) makes a project of a repository that is not on the device
yet. `codeclones.cpp` over core `gitcli` (`clone.cpp`):

* **What is typed** — `gitcli::parseCloneSource()`, T3 Code's rules: `owner/name` is GitHub
  (`https://github.com/owner/name.git`, `shorthand`), `github.com/owner/name`, https / http / ssh /
  git:// addresses and scp-like `git@host:owner/name.git`, a repository's **web page** cut to the
  repository (GitHub: the first two segments; elsewhere at `/-/`, `/tree/`, `/blob/`, `/src/`,
  `/pull/`, `/issues` …; the query and fragment dropped, percent escapes read), a repository on this
  computer (an absolute path, a `file:` URL). Refused: anything starting with `-` (an option to git),
  another transport (`ext::`, `fd::`, `host::…`), `.` / `..` segments, spaces or control characters,
  a host that is no host name, an owner without a name ("Type the whole owner/repository name.").
  The default folder's name is the repository's (safe on every platform, `repository` else).
* **The picker** (`CodeClonePicker`, a BPalette): the user's repositories from `gh repo list --limit
  200 --json nameWithOwner,description,url,sshUrl,isPrivate,isFork,isArchived,updatedAt` (only where
  gh is; `CodeProjects.listRepositories()` → `repositoriesListed()`, the last list shown at once while
  gh is asked again; rows `repositoryItems()`: private · fork · archived · the description, a lock on
  private ones), and `cloneOffers()`: "Clone “x”" for what is typed when it names a repository none of
  them is; `cloneProblem()` says why it is none when it looks like an address (a `/`, `:` or `@`).
* **The folder** (`CodeCloneDialog`): `cloneFolder(source, parent)` — `<clonesRoot>/<name>`
  (`clonesRoot` default `~/.brulion/projects`, Settings → Extensions → Code mode → "Cloned
  repositories", `codeClonesSetting`), `-2`, `-3` … while the folder is taken (not empty, a file, a
  clone running into it); a folder chosen in the picker gets `<chosen>/<name>`, unless it is an empty
  folder of the repository's name itself. A clone goes only into a missing or empty folder.
* **The clone** — `cloneRepository(source, folder)` ("" started, else why not) runs
  `git clone --progress -- <url> <folder>` (`gitcli::Client::clone()`: the parent made first, an hour's
  limit, ssh asks only through an askpass program — `SSH_ASKPASS_REQUIRE=force` unless the user set
  it —, https never: `GIT_TERMINAL_PROMPT=0`; no submodules, no hooks). Its progress
  (`gitcli::parseCloneProgress()`: counting, receiving, resolving, checkout, the percent, git's
  "12.4 MiB | 3.1 MiB/s") is a row of `clones` — `[{folder, source, url (no login), name, phase
  running | failed, stage, percent, detail, status, error}]`, `clonesChanged()` at most four times a
  second — shown by the dialog and by `CodeCloneRow` above the projects (Cancel `cancelClone()`: the
  run stopped by its tag, `Client::stop()`, what it cloned taken away). When signing in fails
  (`isAuthenticationFailure()`: publickey, host key, "could not read Username", 401 / 403) the clone is
  tried once more over the other protocol (`cloneUrlForProtocol()`: ssh ↔ https on the same host and
  path; not with a custom port). Done, the folder is added (`add()`) and `cloned(folder, project, "",
  false)` — the dialog, if still shown, opens a new thread of it; hidden, a toast says so. Failed, the
  row stays with git's message (`retryClone()` the address as typed again, `dismissClone()`); a
  folder git made is never left behind, an empty folder that was there stays. Quitting the app stops
  a running clone and takes away the folder it made.

### t3.json

A project's `t3.json` at its root (T3 Code's project file: `$schema`, `iconPath`,
`defaultThreadEnvMode`, `worktreeSubmodules`, `worktreeSparsePatternsPath`, `scripts` — the fields
Brulion does not use, `ignoreRepositories` and `worktreeLayout`, are ignored) is read when the project is added or
loaded and again when it changes (a `QFileSystemWatcher` on the project's folder and the file,
debounced), **never written**. `parseConfig()` is pure and strict: at most 256 KB, a JSON object;
`scripts` a list of objects with a non-empty `name` and `command` as text (a name cleaned of control
and format characters, at most 60 characters; a command at most 4000), `icon` one of `play` (the
default), `test`, `lint`, `configure`, `build`, `debug`, `runOnWorktreeCreate`, `async`, `stopOnSettle`,
`runOnSettle` and `autoOpenPreview` booleans (anything else their defaults: false, true, false, false,
false), `previewUrl` an http or https address with a host (`previewUrlOf()`; anything else is left out
with a warning, and its `autoOpenPreview` with it); a second script of a name (letter case aside), the 51st,
and anything malformed is left out with a warning; `iconPath` a picture (`png`, `svg`, `jpg`, `jpeg`,
`webp`, `ico`, `gif`) inside the project — no absolute path, nothing that leaves it, no link pointing
out of it; `worktreeSubmodules` `recursive`, `top-level` or `none` (anything else left out with a
warning; not said: recursive, T3 Code's default); `worktreeSparsePatternsPath` a file inside the project
(at most 64 KB; its lines that are not empty and no `#` comment are the patterns — git's non-cone
syntax —, `Config::sparsePatterns`; a path that leaves it or a missing file: none, warned). A file that does not read is said in `configError`
and nothing of it is used.

Its content is data: a command of it runs only when the user clicks its button, runs it from the
palette or its shortcut, or makes a worktree with it set to run there.

### Scripts

T3 Code's actions: buttons over the thread that run a command in a new tab of the thread's terminal
(`CodeView.runProjectScript(script)` → `runScript(command, name)` → `TerminalPanel.run`). A script
with a `previewUrl` is listed in the scripts' menu as "Open preview: Dev" (the system's browser —
Brulion has no browser of its own), and with `autoOpenPreview` that address opens when it runs. `scripts(id)` merges this device's
scripts (`setScripts()`, at most 50) and the file's: the device's in their order, then the file's that
no script of the device's of the same name (letter case aside) overrides — a device's one of a file's
name replaces it ("overrides t3.json"). Each has an `id` (a unique slug of its name), its icon in
Brulion's set (`iconName`: play, circle-check, list-checks, sliders-horizontal, layers, eye), a Code
mode `shortcut`, `runOnWorktreeCreate` and `wait` (T3 Code's `async: false`), `stopOnSettle`,
`runOnSettle`, `previewUrl`, `autoOpenPreview` ("Settling and snoozing" below), its `source` (`device` |
`t3json`); `scriptFromMap()` / `scriptToMap()` the fields as kept and as QML gives them. `setupScripts(id)` / `setupList(id, field, waited)`: those that run when a worktree is made.

The project dialog edits them (`CodeScriptsModel`, docs/UI.md "Code mode"); the palette lists the shown
project's as "Run: Testy — kalkulator" (`code.script.<project>.<id>`), with their shortcuts
("Shortcuts" below).

### Worktrees, branches, pull requests

Everything through `gitcli::Client` (QProcess argument lists, never a shell; docs/CODE-GIT.md
"Security"), asynchronous, each run ending with a signal:

* `worktreeFor(id, title)`: `{folder: <worktreesRoot>/<id>/<slug of title>, branch:
  <branchPrefix><slug>}`; `createWorktree(id, title, base)`: a free name — no file of that folder name,
  no branch of that name (`git for-each-ref refs/heads/`) — then `git worktree add -b <branch> -- <folder>
  <base or HEAD>`; a remote-tracking base (`origin/main`) is fetched first (`git fetch -- origin main`;
  offline, what is known of it). With sparse patterns the worktree is made without a checkout
  (`--no-checkout`), then `git sparse-checkout set --no-cone --stdin` (the patterns on standard
  input) and `git checkout` bring only what they name (`checkOutSparse()`; a failure is the
  worktree's). A worktree with a `.gitmodules` then gets its submodules
  (`checkOutSubmodules()`: `git submodule update --init [--recursive]` as t3.json's
  `worktreeSubmodules` says — recursive by default, `top-level` without `--recursive`, `none` nothing);
  `worktreeCreated(project, folder, branch, error)` follows only then, so a thread's first message
  waits for them; one that cannot be fetched keeps the worktree and says so (`submodulesFailed(folder,
  message)`: CodeView's toast).
* **The AI names the branch** (T3 Code's AI-made branch name), when the AI of Settings → Extensions → AI
  is set up: before the worktree is made, the thread's first message (`title`) goes to it
  (`setBranchNamer()`; Workspace's asks `codeai::branchName()`: a few lowercase words joined by hyphens,
  no prefix), and `worktreeName(answer, title, prefix)` makes the name of the answer — its first line
  or JSON's `branch`, a fence, quotes, a label and a prefix (`feature/…`) taken off
  (`codeai::readBranchName()`; more than 64 characters is a sentence, no name), then `slug()` — when
  `branchPrefix` + it is a valid branch name (`gitcli::isValidBranchName`). Anything else — no AI, a
  failure, no answer within 20 seconds (`BranchNameTimeoutMs`; the question is stopped then), an answer
  that is no name — gives today's name, the message's slug. The folder takes the same name; a name
  taken gets `-2`, `-3` … as before. The message waits meanwhile (the header's spinner), as T3 Code's
  agent waits for its name; the name is known before the worktree exists, so the thread's page names
  its branch from the start and no branch is renamed under a running agent (T3 Code makes a temporary
  branch and renames it). Another computer's project: that computer names it (below).
* `listWorktrees(id)` → `worktreesListed(project, [{folder, branch, detached, exists}])` (not the
  project's own folder); `listBranches(folder)` → `branchesListed(folder, [{name, remote, current,
  isDefault (origin's HEAD, else main / master / trunk), worktree (the folder that has it checked out),
  upstream, ahead, behind}], error)` — local ones first, remote ones a local branch tracks left out;
  `branchItems()` / `branchOffers(query, branches, canCreate)` the branch picker's rows and offers.
* `switchBranch(folder, name)` (`git switch`, a remote-tracking one `--track`), `createBranch(folder,
  name)` → `branchSwitched(folder, branch, ok, message)`. The view refuses while an agent's turn of a
  thread of that folder runs (`WorkspaceChats.runningIn(folder)`).
* `pullRequestNumber(text)`: `#123`, `123`, a URL ending in `/pull/123` (`/files`, a query, a fragment
  after it). `checkoutPullRequest(id, number, inWorktree)`: **Local** — refused while the project's
  folder has uncommitted changes (untracked files aside); `gh pr checkout <n>` there, else `git fetch --
  <remote> pull/<n>/head:<branchPrefix>pr-<n>` (origin, else the first remote) and `git switch`. **New
  worktree** — with gh a detached worktree (`git worktree add --detach`) where `gh pr checkout <n>` runs
  (removed again when gh fails), without it the fetch into a free `<branchPrefix>pr-<n>` and `git
  worktree add` of that branch. `gh pr view <n> --json title` gives the title. →
  `pullRequestCheckedOut(project, number, folder ("" the project's), branch, title, error)`.
* `checkWorktree(folder)` → `worktreeChecked(folder, changes (files with uncommitted changes, untracked
  ones too), branch, error)`; `removeWorktree(id, folder, force)` (`git worktree remove [--force]`; never
  the project's own folder; its branch stays) → `worktreeRemoved`; `restoreWorktree(id, folder,
  branch)` (`git worktree prune`, then `git worktree add -- <folder> <branch>`) → `worktreeRestored`.
* A deleted thread's Undo: `keepUndo(toast, trashPaths, id, folder, branch, removed)` — AppController's
  toast (`toastWithAction`) pressed (`toastActionTriggered` → `undo()`) brings the pages back from the
  trash (`setRestorer`: `Workspace::restoreAll`) and makes the worktree again from its branch when it
  was removed; the toast gone (`toastRemoved`) forgets it.

### Open in an editor

`editors`: the editors found on PATH (`knownEditors()`, in this order: VS Code `code`, `code-insiders`,
VSCodium `codium`, Cursor, Windsurf, Zed `zed` / `zeditor`, Sublime Text `subl`, IntelliJ IDEA, PyCharm,
WebStorm, CLion, GoLand, RustRover, PhpStorm, Rider, Kate, GNOME Text Editor, gedit, Notepad++ — Windows
finds their `.exe` / `.cmd` by itself), then the custom command (`editorCommand`, split as a shell
would — `splitCommand()` — with the path as its last argument) when set, then the file manager.
`preferredEditor` (a device setting) — else the custom command, else the first found, else the file
manager: `effectiveEditor`. `openInEditor(path, line = -1)` / `openWith(editor, path, line)` open a
folder or a file (started detached, no shell); a file at a line where the editor takes one
(`editorArguments()`: `--goto file:line` for VS Code, VSCodium, Cursor, Windsurf; `file:line` for Zed
and Sublime Text; `--line N file` for the JetBrains IDEs and Kate; `+N file` for gedit; `-nN` for
Notepad++). The other streams' panels (a file of the Changes panel, a path in the terminal, the file
viewer) call `openInEditor(file, line)`.

### Shortcuts

Code mode's commands (`CommandModel`, ShortcutMap's scope `code`: their shortcuts work in Code mode
only, may be keys of commands that are everywhere — they win there, AppShell turns the other off — and
keys reserved for what Code mode never shows: drawings, the PDF viewer, the canvas, the tabs' Ctrl+1–9,
the AI chat's list):

| Command | Default | |
|---|---|---|
| `code.newThread` New thread | Ctrl+Shift+O | the project shown, its defaults |
| `code.newLocalThread` New local thread | Ctrl+Shift+N | (Notes mode: quick capture) |
| `code.previousThread` / `code.nextThread` | Ctrl+Shift+[ / Ctrl+Shift+] | in the sidebar's order |
| (fixed) the n-th thread listed | Ctrl+1 … Ctrl+9 | (Notes mode: the tabs) |
| `code.renameThread` Rename thread | F2 | (Notes mode: rename page) |
| `code.branches` Branches… | Ctrl+Shift+G | switch, create, a pull request |
| `code.environment` Where the new thread works… | Ctrl+Shift+X | |
| `code.openInEditor` Open in editor | Ctrl+O | (Notes mode: go to page) |
| `code.toggleChanges` Show or hide the changes | Ctrl+D | |
| `code.script.<project>.<id>` Run: … | the script's own | the shown project's scripts |

Those that need the view come back to it as uiActions `code:<what>` (`code:branches`,
`code:script:<id>` …: CodeView, CodeSidebar). A script's shortcut is its default in Settings → Keyboard
shortcuts (a custom one there wins); one a Code mode command has already loses it, and the project
dialog checks it (`ShortcutMap.checkIn("code", …)`). The scripts are actions only while their project
is shown: two projects' scripts may share a key.

## Threads

A thread is a conversation of the AI chat (docs/SYNTAX.md §14) whose frontmatter names its project:

```markdown
---
tasks: ignore
partner: acp:claude
project: sklep
worktree: "/home/ala/.brulion/worktrees/sklep/dodaj-rabat-10"
branch: brulion/dodaj-rabat-10
---
- 🧑 Dodaj rabat 10% do koszyka
  time:: 2026-11-25 23:18
- 🤖 …
```

`project:` is the id of a project of this device; `worktree:` and `branch:` say where it works when it
has a worktree of its own (`chat::Conversation::project / worktree / branch`, `chat::setField()`).
Everything the AI chat does works for a thread: the queue and Send now, approvals, plans, the agent's
options (model, mode, effort), its subscription's limits and usage, tool calls, side chats,
sub-conversations, scheduled conversations, notifications, unread marks, search.

* `WorkspaceChats.startThread(project, text, pictures, partner, worktree, branch, pullRequest)`: a new thread —
  answered by an **AI agent** only (`partner`, else `threadPartner()`: the default partner when it is
  an agent, else the first agent set up; none: the turn's failure says to add one), **no line in the
  journal** (a thread is no event of the day). `threadPlace(path)`: `{project, projectName, worktree,
  branch, folder, known, exists}`; `threadFolder(path)`; `setThreadWorktree(path, worktree, branch)`.
  `agentPartners()` lists the agents. `pullRequest` ("#123 Title") is its page's `pr:` and the
  agent's first context names it ("The branch checked out there is that of the pull request …").
* **"Regenerate title"** (T3 Code's; the sidebar's thread menu — docs/UI.md "Code mode"):
  `regenerateTitle(path)` (`shell/chattitles.cpp`) asks the AI of Settings → Extensions → AI
  (`canRegenerateTitles`) for a title from the thread's messages and its name now —
  `codeai::titleContext()`: "USER:" / "ASSISTANT:" sections, its first question, then the newest
  questions, then the newest answers, each at most 2,000 characters (a longer one keeps its start and
  end), 8,000 in all, pictures left out; `codeai::threadTitle()`: the lasting subject and outcome in the
  language of the user's messages, 3 to 8 words, without the project's name, the name kept when it
  fits. The answer read (`codeai::readTitle()`: its first line or JSON's `title`, a fence, a heading's
  `#`, a label, quotes and trailing punctuation taken off, at most 60 characters cut at a word) renames
  the page with its date kept (`rename()`): `titleRegenerated(path, newPath, "renamed" | "unchanged" |
  "failed", message)` — the same name is kept, a page renamed or gone meanwhile, a name another
  conversation has and the AI's failure say why. One question of a thread at a time
  (`regeneratingTitle(path, titleSerial)`); closing the notebook stops them.
* **Files dropped on a thread's row** (T3 Code's "Drop files onto a thread"; CodeThreadRow's
  `codeThreadDrop`): the thread opens and they go into its message — `CodeProjects::dropOnThread(path,
  folder, urls)` keeps them (`codefiles::droppedFiles()`: pictures apart, the others named — `@path`
  inside the thread's folder, quoted with a space, `@./` the folder itself, a path in a code span
  outside it —, local files only, each once) and tells `threadDropped(path)`; CodeView takes them when
  it shows that thread (`takeDropPictures()` → `ChatView.attachFiles`, `takeDropText()` →
  `addToComposer`). Nothing is sent.
* `threadsIn(folder)`: the threads that work in a folder (a deleted thread's worktree: does another one
  use it?); `runningIn(folder)`: a turn of one of them runs (no branch switch under it);
  `markUnread(path)`: the sidebar's "Mark unread"; `draftSerial`: drafts changed — a new thread's draft
  is kept per project (`draft("code:<project>")`, ChatView) and shown in the sidebar.
* The AI chat's list (`ChatListModel.project` "") leaves threads out; `ChatListModel.project` = an id
  lists a project's threads, `"*"` all of them.
* **Its agent's session** (`WorkspaceAgents::codePlace()`): the session's `cwd` is the thread's folder
  (its worktree's, else its project's); Brulion's MCP server is started with `--no-notes` (docs/CLI.md
  "MCP server": the app's chat tools — notify, delegate, secrets — and with "Let it change the app" the
  live tools, never the notebook's) and none when it would have no tool; the first prompt is
  `codeContext()` — the project, its folder, the worktree's branch, the tools, nothing of the notebook.
  `fs/read_text_file` and `fs/write_text_file` read and write the thread's files directly (absolute
  paths, or relative to its folder; the agent runs with the user's rights anyway, its own permissions
  decide); a terminal's default folder is the thread's. A thread's turn is not tracked by the chat's
  "What a turn changed" card: git shows its changes (the Changes panel below).
* A project that is not on this device, a worktree that is gone: the turn fails with what to do. **The
  agent follows the project**: a thread of this device's project is answered by an agent of this device
  — the partner when it is one, else the agent of the same id (a partner paired from a computer,
  `<computer>-claude`, finds `claude`), else the first one —, a thread of another computer's project by
  that computer's agents the same way (below).

## Threads without a project

T3 Code's scratch threads: a thread that needs no project. The projects' list ends with **No project**
(`CodeProjects::ScratchId`, `scratch`; `isScratch()`; never kept with the device's projects, not counted
in `count`, its `list()` entry `{id: "scratch", name, path: scratchRoot(), scratch: true}` — a folder
named so becomes `scratch-2`). A new thread of it always works in a folder of its own
(`newThreadEnv()` "worktree", no environment or base-branch chip): `createWorktree("scratch", title)`
makes `<scratchRoot>/<yyyy-MM-dd>-<its first words>` (`-2`, `-3` … while taken; `scratchRoot()` default
`~/.brulion/scratch`), no git, and the thread's page names it as its `worktree:` — the agent, the
terminal and the files work there as in any thread. Deleting such a thread leaves its folder (no
worktree to take away). **Turn into project** (its menu; `CodeThreads::turnIntoProject(path)`) adds the
folder as a project and makes the page name it (`project:` the new id, no `worktree:`); "Initialize
git" in its Changes panel then makes it a repository.

## Settling and snoozing

T3 Code's sidebar sections: **Settle** ("the task is done for now"), **Snooze** ("defer until later").
`shell/codethreads.h` (QML `Workspace.codeThreads`) over core `codethreads` (pure: a page's
frontmatter in, its state out, edits as content in → content out). The thread's page says it —
synced with the notebook, so every device shows the thread where another put it; the keys the same in
every language (as `project:`), the times local wall-clock time:

```markdown
---
project: sklep
settled: 2026-11-28 10:15
snoozed: 2026-11-28 18:00
auto-settle: never
---
```

* `settle(path)` / `unsettle(path)` (`settled:` written with Workspace::now(), a snooze taken away) →
  `settledChanged(path, on)`; `snooze(path, preset)` ("" done, else why not) / `wake(path)` →
  `snoozedChanged(path)`; `setNeverAutoSettle(path, never)`; `state(path)`: {settled, snoozedUntil,
  snoozed (still ahead), never}. A page without `project:` is no thread: refused.
* **Presets** (`codethreads::snoozeEnd()`): `1h`, `3h`, `evening` (18:00 today, else tomorrow),
  `tomorrow` (9:00), `monday` (9:00 next Monday); anything else is read as a date in words or digits,
  Polish or English (`dates::parseNaturalDate`: "jutro o 18", "in 2 hours", "2026-12-01 9:00"; a day
  without a time is 9:00 of it); a time not ahead is refused. `snoozeLabel(preset)` the end in words
  for the menu ("18:00", "tomorrow 9:00").
* **A new message reactivates it** (T3 Code's "any new message reactivates it"): WorkspaceChats writes
  the user's message and the agent's answer through `codethreads::reactivated()`, which takes
  `settled:` and `snoozed:` away. A snooze whose time passed is no snooze (its line stays until the
  next message); a thread whose turn asks the user or failed is listed as active even while snoozed or
  settled (`codethreads::sectionOf()`: archived, then needing the user, snoozed, settled, pinned,
  active).
* **The list** (`ChatListModel`): sections `pinned`, `recent`, `snoozed`, `settled`, `archived`, in that
  order; `settled` / `snoozedUntil` roles; Settled and Snoozed folded unless `showSettled`, counted in
  `settledCount` / `snoozedCount` (a query lists them anyway); a timer rebuilds the list when the next
  snooze ends. CodeProjectSection shows one row "3 settled · 1 snoozed" under a project's threads that
  unfolds them; a settled row has a check, a snoozed one an alarm clock with its time.
* **By itself**: every hour and when the notebook gets ready (`settleUntouched()`), a thread whose last
  message (its time, else the page's last change) is `autoSettleDays` old settles — Settings →
  Extensions → Code mode → "Settle threads untouched for" (`AppSettings.codeAutoSettle`, key
  `code/autoSettle`: 1, 3, 7 — the default, T3 Code's —, 30 days or never; a device setting), never in
  the documentation notebook — unless `WorkspaceChats::heldBack()` holds it (its turn runs, asks or
  failed, it is unread, on screen, has queued messages, a draft, a sub-conversation that will wake it)
  or its page says `auto-settle: never`. `autoSettled(paths, days)`: AppController's toast with Undo.
  The AI chat's "Archive conversations untouched for" leaves threads alone.
* **Its pull request, its worktree** (`tidy()`, with `settleUntouched()`): every thread with a worktree
  of its own on this device has its branch's pull request asked of gh (`CodeProjects::pullRequestState()`
  → `gh pr view <branch> --json …`); merged or closed, the thread settles (T3 Code's second auto-settle
  rule; `codethreads::settlesFromPullRequest()`: not held, not `auto-settle: never`) — `autoSettled(paths,
  0)`. Then, settled, its worktree is removed (`git worktree remove`, never `--force`: the branch and its
  commits stay) when Settings → Extensions → Code mode → "Remove the worktrees of settled threads"
  (`AppSettings.codeWorktreeCleanup`, key `code/worktreeCleanup`: never — the default —, merged — its
  pull request merged —, 7 or 30 days after it settled, or merged) says so, it has no uncommitted change
  (`CodeProjects::worktreeChanges()`, untracked files counted) and no other thread that is not settled
  works there (`codethreads::removesWorktree()`) — `worktreeCleaned(path, folder)`: AppController's toast,
  whose Undo makes the worktree again from its branch (`restoreWorktree()`). `tidied()` when every
  thread was looked at.
* **What settling does** (`terminalwiring.cpp` → `TerminalRegistry::settle()`, docs/CODE-TERMINAL.md
  "Settling"): the thread's terminals running a script with `stopOnSettle` end, its idle ones too, and
  its scripts with `runOnSettle` run in its worktree (none for a thread in the project's own folder, as
  T3 Code skips main checkouts) — one at a time across every thread, a tab that ended with 0 closed, a
  failed one kept open.
* **The thread's menu** (CodeThreadRow): Settle / Unsettle, Snooze ▸ (CodeSnoozeMenu: the presets with
  their times, "Until…" — CodeSidebar's `codeSnoozeDialog`, a time typed, its end shown as it is typed),
  Wake, "Never settle by itself"; settling and snoozing show a toast whose Undo puts it back
  (`AppController.toastUndoChatMark(…, "settled" | "snoozed", on)`).

## Threads on the phone

A thread runs on the computer that has its project — also when the user writes on the phone (or
another computer of theirs): as in T3 Code, a phone continues the threads that run on the computer,
and starts new ones there.

* **The computer** shares its agents (`brulion-cli acp serve`, the desktop app's "Share agents with
  the phone", docs/CLI.md "AI agents for other devices") with its projects: the desktop app writes
  them to `<app data>/agent-share/projects.json` (`--projects`, written again whenever they change; the
  server reads it again by itself), a server of its own takes the same file.
* **The phone** (any device paired with that computer) asks it for them (`GET projects`: id, name,
  folder there — `RemoteProjects`, again whenever Code mode's panel is shown and when the agents change;
  the last lists kept in the notebook's device state, `code/remoteProjects`, so they show while the
  computer is not reached) and lists them in Code mode with the computer's name. Their threads are the
  notebook's pages as any thread's (`project:`, `worktree:`, `branch:` — the folders there), so a thread
  started on the desktop is continued on the phone and the other way round: the agent follows the
  project (above).
* **A turn** goes to that computer's agent with the project in its session's request
  (`_meta["brulion/code"]: {project, worktree, branch}`, core `acp/codehost.h`): the computer takes it
  only for a project of its own and a worktree of that project (under its worktrees' root, or one
  `git worktree list` names), sets the session's `cwd` there, gives it Brulion's tools without the
  notebook's (`mcp --no-notes`), answers its files and terminals in that folder only — and refuses
  anything else, so the turn fails with why. The first prompt is `codeContext()` with that computer's
  folder. A thread's folder changed (its worktree set or gone): the next turn makes its session anew
  there.
* **A new thread**: Local, or New worktree — made by the computer (`POST projects/<id>/worktrees`,
  the same naming as `worktreeFor()`, without the phone's AI), whose folder and branch the thread's page
  then names.
* **Checkpoints** are taken there, before the prompt reaches the agent; the computer tells the device
  (`_brulion/checkpoint`, also in the turn it keeps for a device that lost the stream), which records
  it as its own (`WorkspaceAgents::checkpointMade` → the thread's checkpoints).
* **Changes and the terminal are that computer's**: on the phone `CodeRemoteChanges` shows what the
  last turn changed there (the files since its checkpoint, or every change not committed) and a file's
  diff, read-only (`GET projects/<id>/changes`, `…/diff`); committing, pushing and the terminal are at
  that computer.

## The computer's own turns on the phone

A turn the **desktop app** runs — a thread started there, any conversation with an agent there — is
followed on the phone **live** and steered from it, as T3 Code's phone continues what its computer
runs (core `acp/appchats.h`, `shell/computerchats.h`; the computer's side: docs/CLI.md "AI agents for
other devices" → "The app's own conversations").

* **How it reaches the phone**: the desktop app's agent sharing (`acp serve`, "Share agents with the
  phone") gets a grant of the app's own chat (`BRULION_APP_SHARE_TOKEN`, ipc `GrantScope::Share`) and
  hands the app's conversations on while the app runs (GET info's feature `chats`). The phone
  (`ComputerChats`, owned by WorkspaceAgents, the computers of its paired agents) asks each computer
  `GET chats` in a long poll — while the app is in front, or while a turn of a computer runs here
  (`setActive`; a question that comes while the phone is in a pocket is a notification): the
  conversations whose turn runs, waits or asks there and those that ended there in the last 10
  minutes (`acp::RemoteChat` — also a turn that ended before the app shared its conversations or
  started again: `WorkspaceChats::endedSince()`, the notebook's device state `chat/ended`), and the
  one shown here in full (`watch()`, the conversation a chat
  view showed last: its answer as it is written, tool calls, plan, the question with its options, its
  plan to approve, the file change it asks for, the messages waiting and whether they are held, its
  agent's options — `acp::RemoteChatDetail`).
  A computer whose app does not run answers 503 (asked again every 20 s), an older computer has no
  such endpoint (every 10 minutes), a network gone keeps the last state for two minutes, a device
  unpaired there asks no more.
* **Mirrored into the turn** (`WorkspaceChats::mirror`): the conversation's `ChatTurn` runs here with
  `remoteHost` the computer's name — its status, answer, thoughts, tool calls and plan as the
  computer says them —, so everything that shows a turn shows it: the chat view (with "Running on
  nixos — answer, stop or write from here: your messages wait there." above the field:
  `chatOnComputer`), the list's states, Code mode's sidebar, the top bar's count. `asking()`,
  `questionText()`, `questionOptions()`, `questionDiff()`, `questionPlan()` are the computer's, and
  `answer()` goes there (POST chats/answer); Stop goes there (chats/stop); a message written while it
  runs waits **there** (chats/send `mode: queue`; `steer()` sends it ahead, `mode: steer`) — the
  waiting messages are the computer's (`queued()` gives them with `remote: true`: shown, not taken
  back nor sent ahead from here). **Pictures** go along: each prepared as this device's own turn
  would send it to the model (`Workspace::aiPicture`: JPEG, 1568 pixels a side at most;
  `picturesForComputer()`), sent with the text (`pictures` of chats/send and chats/start), stored in
  the computer's notebook there and named by its page; once that computer took them this device's
  copies are deleted (`sentThere()`: the computer's come with the notebook's sync) — refused there,
  the turn runs from here with them as before. `stopAll()` and a
  page deleted here stop none of the computer's turns. A turn of this device's own is never taken
  over by a computer that lists the same conversation.
* **Its agent's options and held messages** (`computerDetail()`, `mirrorExtras()`): for a
  conversation that computer's app runs or ended lately, the options picker above the field shows
  that app's agent's options (`agentOptions()`: its model, mode, effort … as they are there) and a
  choice goes there (`setAgentOption()` → POST chats/option, for its next turn); the messages it
  holds after its turn was stopped or failed there are the conversation's held queue here
  (`queued()` with `remote: true`, `queueHeld()`), and Resume lets them go on there
  (`resumeQueue()` → POST chats/resume).
* **Its end**: the turn here ends with it; the answer is in the computer's page, which the phone reads
  (GET chats/page) whenever the computer's copy has more messages than this device's — the
  notebook's sync is slower — and shows until its own copy catches up (`computerPage()`,
  `ChatMessageModel`, `threadPlace()`); the answer's first line is a notification (as one of this
  device's would be), the conversation is unread when not shown, a failure there is the turn's
  failure here ("On nixos: …").
* **"On your computers"** (`ChatComputerList`, `chatComputers`, `Workspace.chats.computerChats()`):
  what the desktop app is doing — each conversation running, waiting, asking or lately ended there,
  with the computer's name and what it does or asks — above the chat's list of conversations and in
  Code mode's sidebar; a row opens it (also one this device has no page of yet: the computer's is
  shown).
* **One agent, both screens**: a thread of a project of a computer whose app shares its
  conversations — and any conversation that app runs or ended lately — runs its next turn **in that
  app** (`computerFor()`; POST chats/send) — its agent, its session, seen on both screens — instead of
  a session of this device through the shared agent (its partner here is this device's agent of that
  computer: `partnerOf()`); refused there (its copy of the notebook lacks the page yet, the app quit meanwhile), the turn
  runs from here as before. A new thread of such a project starts there too (`startThreadThere()`:
  POST chats/start with this device's agent's id there and the worktree that computer made;
  "Starting the thread on nixos…", `threadStartedThere`), and the view follows it from the
  computer's page until the notebook's sync brings it.

## The view

`qml/code/CodeView.qml` (route `code`; its header's `CodeThreadTitle`, `CodeOpenIn`,
`CodeBranchPicker`, `CodePullRequestDialog`, `CodeSetupBanner`), `qml/code/CodeSidebar.qml` (AppShell's
page panel and the drawer's content in Code mode; `CodeProjectSection`, `CodeThreadRow`),
`qml/code/CodeProjectDialog.qml` (`CodeScriptsEditor` over `shell/codescriptsmodel.h`); docs/UI.md
"Code mode".

**Setup scripts** (T3 Code's `runOnWorktreeCreate`): once a new thread's worktree is made
(`createWorktree`, before its first message is sent), the project's scripts that run then start in
the thread's terminal, in the worktree — those not waited for at once, those to wait for (`async:
false`, "Wait for it before the agent starts") one after another while `CodeSetupBanner` says "Setting
up the worktree…"; when they all ended with code 0 the message goes, when one failed (its exit code,
or it did not start) the banner says which and the user sends anyway or not (the message stays in
the field, the worktree stays: the next Send goes there without setting it up again). Without a
terminal on the device the scripts are skipped, said in a toast, and the message goes.

## Changes, checkpoints, commits

The Changes panel (`GitChangesPanel`, `CodeGit`: `src/app/gitpanel`; core `src/core/gitcli`) is
described in **docs/CODE-GIT.md**: T3 Code's diff sources (Latest turn / Turn N — what one turn
changed —, Uncommitted, the branch against a base, one of the newest 100 commits) with a diff per
file (side by side or stacked, wrapped, whitespace hidden, find, a tree of files; hunks staged,
unstaged or thrown away; lines commented into the chat's composer), a file's actions (open, copy the
path, stage, unstage, discard, take a side of a conflict), the branch (switch, create, stash), its pull
request's badge, ahead / behind, one quick action from the state (Commit… with a checklist of files
and a new branch, Push, Pull, Create PR… into a chosen base and from forks, Publish…, Initialize git;
pushing to the default branch asks first), a banner for a merge / rebase / cherry-pick with its
conflicts, and a turn's **Revert this turn…** / **Revert to before turn N…**.

**Checkpoints of threads**: before each turn of a thread's agent, `WorkspaceChats` takes a snapshot of
the thread's folder (`gitcli::Client::checkpoint`: a temporary index, `refs/brulion/checkpoints/<id>`;
the user's index, HEAD and stash never move) and starts the agent once it is made — the turn's status
says "Saving a checkpoint…" meanwhile; a folder that is no git repository, or git missing, starts the
turn without one; Stop meanwhile starts nothing. `WorkspaceChats.checkpoints(path)` lists them ({id,
at, message}: kept in the notebook's device state `code/checkpoints`, at most 30 a thread, the older
refs dropped), `lastCheckpoint(path)` the newest — CodeView gives the list to the panel as its
`checkpoints` (turn N's own changes: checkpoint N against checkpoint N + 1, the newest against the
working tree; "Revert this turn…" takes that turn out, "Revert to before turn N…" puts checkpoint N
back). While the agent works the panel polls git every 2 s; each answer written refreshes it. The
panel's review comments go into the thread's composer (`ChatView.addToComposer`); its Open in editor
uses `Workspace.projects.openInEditor` for the file. "Edit from here" on a message of a thread
offers to put the files back to the checkpoint of that message's turn (`checkpointOf(path, number)`: the page's message number → the
turn, `restoreCheckpoint(path, id)` → `checkpointRestored(path, ok, message)`; docs/UI.md "AI chat").

**Where a thread's checkpoints live**: the list is this device's (device state, above: when, which
message, which folder — what the device ran or heard of), and **the newest turn's id is also in the
thread's page**, `checkpoint: <id>` in its frontmatter, written by whichever device ran the turn — the
desktop for its own turns, the phone for a turn the computer of the project ran for it
(`checkpointMade`). The page is what every device of the notebook shares (synced as the notebook is):
the desktop that has the project shows a turn the phone started ("This turn", "Revert this turn…": the
ref is in its repository — that computer took it), and the phone asks the computer for the changes
since the desktop's last turn. `lastCheckpoint()` takes the page's first; `checkpoints()` lists the
page's last when this device has not got it (its time unknown). A checkpoint id is no secret (a git
ref's name), and a page that names one of another repository shows nothing of it.

## Terminal

The Terminal panel (`TerminalPanel`, `TerminalSession`, `TerminalView`, `TerminalRegistry`:
`src/app/terminal`; core `src/core/term`; `src/platform/pty.h`) is described in
**docs/CODE-TERMINAL.md**: each thread's terminals, kept — running, with their scrollback — while
another thread is shown (`TerminalRegistry`, keyed by the thread's page; a new thread's by its folder
until its first message), ended when the thread is deleted, their idle ones when it is archived,
their layout (tabs, splits, folders, commands typed, not run) back after a restart;
tabs of up to four terminals side by side or stacked, the project's scripts in new tabs; a find bar
over the scrollback, a context menu with Add to chat (the selection or the screen into the
thread's field), URLs and file paths (`path:line:column`) opened with Ctrl+click, marks on tabs
(new output, a bell, the exit code), shortcuts (Ctrl+J shows or hides it); a real pseudo-terminal
and an xterm emulator (colours, the alternate screen, mouse reporting, bracketed paste, OSC 8 links,
OSC 52 ignored). POSIX desktops and Windows 10 1809+ (ConPTY: PowerShell 7, Windows PowerShell or
cmd.exe); Android and the browser say there is no terminal.

## Files

The project's files beside the thread are described in **docs/CODE-FILES.md** (core
`src/core/codefiles`, area `src/app/codefiles`): the right panel's tabs Changes | **Files** |
**Search**, the file picker (Ctrl+P in Code mode: a fuzzy match over the folder's index from `git
ls-files` or a walk of the folder), the file viewer (syntax colours, line numbers, pictures, a line
marked; lines selected → a review comment in the composer), the project search (Ctrl+Shift+F:
ripgrep, git grep or Brulion's own), a thread's **@-mentions** of its files and folders (each named
file also sent to the agent as a `resource_link`) and links of answers and tool calls to the
thread's files opening the viewer at their line (`CodeView.openFileInViewer(path, line)`).

## What is not there yet

* Windows: Code mode's tests run there (CI: the terminal on ConPTY, git and gh with Windows paths,
  the Changes panel), not yet a session by hand with a real agent; a worktree of a repository whose
  paths pass 260 characters needs git's `core.longpaths`; a terminal before Windows 10 1809.
* The rest of docs/CODE-GIT.md "Not done (yet)".
* Threads on the phone: committing, pushing, pull requests, the branch picker, reverting a turn and
  the terminal of a project of another computer (the phone shows its changes read-only); pictures
  for a turn the computer's app runs, its agent's options (model, mode) changed from the phone, a
  waiting message taken back there; a new
  worktree there from another base than its HEAD in the UI; projects of two computers with the same id
  (the first computer's is listed).
* Multi-repository projects and custom worktree layouts (t3.json's ignoreRepositories, worktreeLayout); T3 Code's branch naming
  modes (a semantic prefix the AI picks, the user's own instructions for it); worktrees cleaned up by
  their age alone, whatever their threads (settled threads' worktrees are: "Settling and snoozing").
* Cloning: other hosts' repository lists (GitLab, Forgejo, Bitbucket — their addresses clone, their
  lists are not shown), T3 Code's "Clone into host folders" (`<host>/<owner>/<repo>`), a new project
  made from nothing (`git init` with a README, a GitHub repository published for it), copying a
  project's ignored files (`.env`) to a clone on another computer, cloning on a computer that shares
  its agents from the phone.
* A title
  the AI gives a new thread by itself (its first message's words name it; "Regenerate title" asks),
  T3 Code's in-app browser for
  `previewUrl` (the system's opens it).
* Rendered HTML and PDF files in the viewer, mentions of threads and pull requests
  (docs/CODE-FILES.md "Not done").
