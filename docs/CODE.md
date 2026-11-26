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
wait?}]}], worktreesRoot?, editor?, preferredEditor?, branchPrefix?, deletedWorktrees?, newThreadEnv?,
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
* `worktreesRoot` (default `~/.brulion/worktrees`), `branchPrefix` (default `brulion/`; only what a
  branch may start with), `deletedWorktrees` (`ask` | `delete` | `keep`), `threadSort` (`activity` |
  `created`), `projectOfFolder(folder)`.

### t3.json

A project's `t3.json` at its root (T3 Code's project file: `$schema`, `iconPath`,
`defaultThreadEnvMode`, `scripts` — the fields Brulion does not use, `worktreeSubmodules`,
`worktreeSparsePatternsPath`, `ignoreRepositories`, `worktreeLayout` and a script's `stopOnSettle`,
`runOnSettle`, `previewUrl`, `autoOpenPreview`, are ignored) is read when the project is added or
loaded and again when it changes (a `QFileSystemWatcher` on the project's folder and the file,
debounced), **never written**. `parseConfig()` is pure and strict: at most 256 KB, a JSON object;
`scripts` a list of objects with a non-empty `name` and `command` as text (a name cleaned of control
and format characters, at most 60 characters; a command at most 4000), `icon` one of `play` (the
default), `test`, `lint`, `configure`, `build`, `debug`, `runOnWorktreeCreate` and `async` booleans
(anything else their defaults: false, true); a second script of a name (letter case aside), the 51st,
and anything malformed is left out with a warning; `iconPath` a picture (`png`, `svg`, `jpg`, `jpeg`,
`webp`, `ico`, `gif`) inside the project — no absolute path, nothing that leaves it, no link pointing
out of it. A file that does not read is said in `configError` and nothing of it is used.

Its content is data: a command of it runs only when the user clicks its button, runs it from the
palette or its shortcut, or makes a worktree with it set to run there.

### Scripts

T3 Code's actions: buttons over the thread that run a command in a new tab of the thread's terminal
(`CodeView.runScript(command, name)` → `TerminalPanel.run`). `scripts(id)` merges this device's
scripts (`setScripts()`, at most 50) and the file's: the device's in their order, then the file's that
no script of the device's of the same name (letter case aside) overrides — a device's one of a file's
name replaces it ("overrides t3.json"). Each has an `id` (a unique slug of its name), its icon in
Brulion's set (`iconName`: play, circle-check, list-checks, sliders-horizontal, layers, eye), a Code
mode `shortcut`, `runOnWorktreeCreate` and `wait` (T3 Code's `async: false`), its `source` (`device` |
`t3json`). `setupScripts(id)` / `setupList(id, field, waited)`: those that run when a worktree is made.

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
  offline, what is known of it). `worktreeCreated(project, folder, branch, error)`.
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
* A project that is not on this device, a worktree that is gone, an agent on another computer: the
  turn fails with what to do.

## The view

`qml/code/CodeView.qml` (route `code`), `qml/code/CodeSidebar.qml` (AppShell's page panel and the
drawer's content in Code mode), `qml/code/CodeProjectDialog.qml`; docs/UI.md "Code mode".

## Changes, checkpoints, commits

The Changes panel (`GitChangesPanel`, `CodeGit`: `src/app/gitpanel`; core `src/core/gitcli`) is
described in **docs/CODE-GIT.md**: the working tree's changes against git with a diff per file, the
branch (switch, create), ahead / behind, Commit… (commit, commit and push, commit, push and create a
pull request with `gh`), Push, Pull, Create PR…, and a turn's changes with **Revert this turn…**.

**Checkpoints of threads**: before each turn of a thread's agent, `WorkspaceChats` takes a snapshot of
the thread's folder (`gitcli::Client::checkpoint`: a temporary index, `refs/brulion/checkpoints/<id>`;
the user's index, HEAD and stash never move) and starts the agent once it is made — the turn's status
says "Saving a checkpoint…" meanwhile; a folder that is no git repository, or git missing, starts the
turn without one; Stop meanwhile starts nothing. `WorkspaceChats.checkpoints(path)` lists them ({id,
at, message}: kept in the notebook's device state `code/checkpoints`, at most 30 a thread, the older
refs dropped), `lastCheckpoint(path)` the newest — CodeView gives it to the panel as its `checkpoint`
("This turn" shows what the agent's last turn changed, "Revert this turn…" puts it back). While the
agent works the panel polls git every 2 s; each answer written refreshes it.

## Terminal

The Terminal panel (`TerminalPanel`, `TerminalSession`, `TerminalView`: `src/app/terminal`; core
`src/core/term`; `src/platform/pty.h`) is described in **docs/CODE-TERMINAL.md**: shells and the
project's scripts in tabs, in the thread's folder (CodeView makes the panel for the thread's folder
when it is first shown, and again for another thread's folder), a real pseudo-terminal and an xterm
emulator (colours, the alternate screen, mouse reporting, bracketed paste, OSC 8 links opened by
Ctrl+click, OSC 52 ignored). POSIX desktops; Windows, Android and the browser say there is no
terminal (ConPTY: not yet).

## What is not there yet

* Windows: the terminal (ConPTY); everything else of Code mode is untried there.
* Staging or discarding single files or hunks, stash, merge conflicts, pull requests from forks or to
  another base in the UI (docs/CODE-GIT.md).
* A thread's terminals kept while another thread is shown (they end when the folder changes).
* Threads on the phone: a thread's agent runs on this computer only (an agent on another computer
  answers the notebook's conversations, docs/UI.md "AI agents (ACP)").
* t3.json's `stopOnSettle`, `runOnSettle`, `previewUrl` (no in-app browser), `worktreeSubmodules`,
  sparse checkouts, multi-repository projects and custom worktree layouts; a branch name the AI makes
  up for a new worktree (the prefix and the first message's slug only); automatic worktree cleanup
  (by age, merged pull requests).
* Settling and snoozing threads, "Regenerate title", dropping files on a thread's row, scratch threads
  without a project.
* Tried with the protocol's fake agent and real git, not yet with a real Claude Code and Codex in a
  real project.
