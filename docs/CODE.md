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
* A project that is not on this device, a worktree that is gone, an agent on another computer: the
  turn fails with what to do.

## The view

`qml/code/CodeView.qml` (route `code`), `qml/code/CodeSidebar.qml` (AppShell's page panel and the
drawer's content in Code mode), `qml/code/CodeProjectDialog.qml`; docs/UI.md "Code mode".

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
uses `Workspace.projects.openInEditor` for the file.

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
* A commit message or a pull request's text written by AI when left empty, and the rest of
  docs/CODE-GIT.md "Not done (yet)".
* A thread's terminals kept while another thread is shown (they end when the folder changes).
* Threads on the phone: a thread's agent runs on this computer only (an agent on another computer
  answers the notebook's conversations, docs/UI.md "AI agents (ACP)").
* Tried with the protocol's fake agent and real git, not yet with a real Claude Code and Codex in a
  real project.
