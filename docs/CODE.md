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

`shell/codeprojects.h` (`Workspace.projects`, QML type `CodeProjects`): this device's projects, kept in
its settings (`code/projects`, JSON: `{projects: [{id, name, path, scripts: [{name, command, icon}]}],
worktreesRoot?, editor?}`) — **never in the notebook**: a path is this device's, and a synced file must
never point an agent at a folder.

* `add(folder)` (a path or a `file:` URL) gives the project's id: a slug of the folder's name
  (`slug()`: ASCII letters, digits and dashes, Polish letters without their marks — "Projekt żółwia" →
  `projekt-zolwia`), `-2`, `-3` … when taken; a folder added again gives its own id. At most 100.
* `rename()`, `move()`, `remove()` (its folder and threads stay), `setScripts()` / `setScriptsText()`
  (a line each, `name: command`; a line without a short name is a command named after itself; at most 12).
* `worktreesRoot` (default `~/.brulion/worktrees`) and `editorCommand` ("Open in editor": the command
  split as a shell would — `splitCommand()` — with the folder as its last argument, started detached; empty:
  the system's handler of folders).
* `worktreeFor(id, title)`: `{folder: <worktreesRoot>/<id>/<slug of title>, branch: brulion/<slug>}`, a
  free folder; `createWorktree(id, title)` runs `git -C <project> worktree add -b <branch> <folder> HEAD`
  (no shell, `GIT_TERMINAL_PROMPT=0`) without waiting and signals `worktreeCreated(project, folder,
  branch, error)`.
* **Projects of the user's other computers** (`remote`, `shell/remoteprojects.h`, QML
  `Workspace.projects.remote`): listed after this device's — `{id, name, path "", exists false, scripts
  [], remote true, host, remotePath}`, read-only, `folderOf()` "" — when a computer that shares its
  agents with this device offers them ("Threads on the phone" below). `isRemote(id)`; a project id of
  this device's own hides one of a computer, and of two computers the first one's is listed.

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

* `WorkspaceChats.startThread(project, text, pictures, partner, worktree, branch)`: a new thread —
  answered by an **AI agent** only (`partner`, else `threadPartner()`: the default partner when it is
  an agent, else the first agent set up; none: the turn's failure says to add one), **no line in the
  journal** (a thread is no event of the day). `threadPlace(path)`: `{project, projectName, worktree,
  branch, folder, known, exists}`; `threadFolder(path)`; `setThreadWorktree(path, worktree, branch)`.
  `agentPartners()` lists the agents.
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
  the same naming as `worktreeFor()`), whose folder and branch the thread's page then names.
* **Checkpoints** are taken there, before the prompt reaches the agent; the computer tells the device
  (`_brulion/checkpoint`, also in the turn it keeps for a device that lost the stream), which records
  it as its own (`WorkspaceAgents::checkpointMade` → the thread's checkpoints).
* **Changes and the terminal are that computer's**: on the phone `CodeRemoteChanges` shows what the
  last turn changed there (the files since its checkpoint, or every change not committed) and a file's
  diff, read-only (`GET projects/<id>/changes`, `…/diff`); committing, pushing and the terminal are at
  that computer.

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
* Threads on the phone: committing, pushing, pull requests, the branch picker, reverting a turn and
  the terminal of a project of another computer (the phone shows its changes read-only); a new
  worktree there from another base than its HEAD in the UI; projects of two computers with the same id
  (the first computer's is listed).
* Tried with the protocol's fake agent and real git, not yet with a real Claude Code and Codex in a
  real project.
