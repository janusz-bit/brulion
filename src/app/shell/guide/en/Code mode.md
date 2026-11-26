---
tags: [documentation]
aliases: [Code, Projects, Threads, T3 Code]
brulion: document
---
# Code mode

Brulion has a second mode for programming with AI agents, the way T3 Code works: **projects** — folders on this computer, git repositories usually — and **threads**, each one task you give an agent such as Claude Code, Codex or OpenCode. The agent works in the project with your subscription; you see what it changes, run the project's scripts in a terminal, commit and open pull requests — and Brulion tells you with a [[Reminders and alarms#The list of notifications|notification]] when it answers, fails or needs you.

Switch with the **top button of the icon bar** (the `</>` icon; in Code mode a notebook icon brings the notes back), with *Code mode* / *Notes mode* in the command palette, or in **Settings → Extensions → Code mode**. Code mode shows nothing of your notes; your notebook stays open behind it — reminders, sync and notifications keep working.

> [!info] What you need
> An AI agent set up in **Settings → Extensions → AI agents** ([[AI#An AI agent of your own (ACP)]]) — on a computer: agents run in the desktop app.

## Projects
**Add a project…** (the folder icon next to *Projects*, or in the palette) and pick its folder. Each project lists its threads under it; its **+** starts a new thread, **⋯** (or a right click) opens it in your editor, shows the folder, **pins** it (pinned projects come first), moves it (or drag it with the mouse), opens its **project settings** and takes it off the list — its folder and its threads stay. Projects are kept on this device only.

**Clone a repository…** (in the same menu, or in the palette) makes a project of a repository that is not on this computer yet, as in T3 Code. Pick one of **your repositories on GitHub** — listed when the [GitHub CLI](https://cli.github.com/) is signed in (`gh auth login`), a lock on private ones — or type `owner/name`, paste a git address (`https://…`, `git@host:owner/name.git`) or just the address of a repository's page (`…/tree/main/src` and `…/pull/12` are cut to the repository). Then choose its folder: `~/.brulion/projects/<name>` unless you pick another (**Settings → Extensions → Code mode → Cloned repositories** changes the default). The clone runs in the background with git's progress over the projects — close the window, it goes on; **×** stops it. When ssh cannot sign in, Brulion tries https once (and the other way round). Done, the folder is a project and a new thread opens. A clone that failed stays there with its reason: its round arrow tries it again, **×** dismisses it.

**Project settings** — its name, an emoji as its icon, the agent and the place of its new threads, and its **scripts**: buttons over the thread that run a command in a new tab of the terminal, in the thread's folder. Each has a name, a command, an icon, a keyboard shortcut (Code mode only; also in the palette as *Run: …*), and can **run when a worktree is made** — with *Wait for it before the agent starts*, the first message waits for it (an install, a build).

A project with a **`t3.json`** (T3 Code's project file) brings its scripts, its icon (`iconPath`) and where new threads work (`defaultThreadEnvMode`): Brulion reads it again when it changes and never writes it. Its scripts are marked *from t3.json*; change one and it becomes yours, overriding the file's of that name. A command runs only when you start it.

## Threads
A new thread starts with your first message. Above it choose where the agent works (**Ctrl+Shift+X**):
- **Local** — in the project's folder itself;
- **New worktree** — in a git worktree of its own on a new branch (`brulion/<name>`; the prefix is in Settings), from the branch you pick (*From main*; a remote one is fetched first), so several agents can work at once without getting in each other's way. Worktrees go to `~/.brulion/worktrees` (change it in Settings → Extensions → Code mode);
- **an existing worktree** — also *New thread in this worktree* in a thread's menu.

The **branch** in the header opens the branch picker (**Ctrl+Shift+G**): switch to a branch, type a new name to create one, or type `#123`, `123` or a pull request's address to **check out the pull request** — in the project's folder or in a new worktree, where a new thread works on it (the agent is told which pull request it is). Brulion does not switch a branch while an agent works in that folder.

Everything of the [[AI|AI chat]] works in a thread: writing on while it answers, the agent's model and effort, approvals and plans, its subscription's limits, side questions, asking other agents. The sidebar shows each thread's state: *Plan* or *Approve* when it waits for you, a spinner while the agent works, *Failed* in red, a dot when its answer is unread; pinned threads come first. Filter the threads by title or branch, and sort them by last activity or creation. A thread's menu renames it (also **F2**, or a double click on its name in the header), pins it, marks it unread, copies its path, branch or link, archives it or moves it to the trash — with a worktree of its own that no other thread uses, Brulion asks whether to delete the worktree too (its branch stays; *Undo* brings both back).

**Keys** in Code mode: **Ctrl+Shift+O** a new thread, **Ctrl+Shift+N** a new local thread, **Ctrl+1–9** the threads of the sidebar, **Ctrl+Shift+[** / **]** the previous and next one, **Ctrl+O** open in your editor, **Ctrl+D** the changes. Change them in Settings → Keyboard shortcuts.

A thread is kept as a page in your notebook's `AI` folder, with its project named in it — never in the journal, and never in the notes' AI chat.

## Changes and terminal
Beside the thread, the **Changes** panel shows the project's changes — pick which in its menu: the **latest turn** or any earlier **turn** of the agent (just what that turn changed; **revert** it alone or go back to before it), everything **uncommitted**, the whole **branch** against its base, or one of the newest **commits**. Each file has its diff (side by side or one below the other, find with Ctrl+F); select lines and **Add to chat** to comment on them for the agent. A file's menu (right click or ⋯) stages, unstages or discards it; a hunk's buttons do the same for a part of it. The main button does what fits now — **commit** (only the files you check, on a new branch if you like; **Write with AI** writes the message from the changes when the AI is set up), **push**, **pull**, **create a pull request** (with the GitHub command line `gh` signed in) or **publish** the project to GitHub — and asks before pushing to the default branch; the branch menu also **stashes** changes. When a merge or rebase stops on conflicts, a banner lets you take a side of each file, then continue or abort. Under the thread, the **terminal** runs shells in the thread's folder (the terminal icon in the header shows and hides it). **Open in** (the `</>` icon, **Ctrl+O**) opens the folder in your editor; the arrow beside it lists the editors found on this computer — VS Code, Cursor, Zed, the JetBrains IDEs… — and the file manager, and remembers the one you pick (a command of your own goes in Settings → Extensions → Code mode).

## On the phone
A computer that shares its agents with your phone ([[AI#An agent on another computer]]) shares its projects too: on the phone, Code mode lists them with the computer's name. Write in a thread — one you started on the computer, or a new one, **Local** or in a **new worktree** — and the agent works on that computer, in the project, as if you were sitting at it. The checkpoint of each turn is taken there, so back at the computer the **Changes** panel shows what the phone's turn changed. On the phone you see the last turn's changed files and their diffs; committing, pushing and the terminal are at the computer.

What the computer's app runs itself, the phone follows live: **On your computers** above the threads lists each conversation working, waiting or asking there. Open one and you see its answer as it is written, its tool calls and plan; answer its question (with the change it asks for), **Stop** it, or write the next message — it waits in line there, or **steer** it in. The answer, a question and a failure come as notifications. While the computer's Brulion shares its agents, a thread of its project runs its turns in the app there — one agent, seen on both screens — and a new thread you start on the phone starts there too.

## Files and search
The panel beside the thread has three tabs: **Changes**, **Files** and **Search**.
- **Ctrl+P** (or *Go to file…* in the palette) finds a file of the project by a few letters of its name — `cvw` finds `CodeView.qml`; Enter opens it in the **Files** tab, Ctrl+Enter in your editor.
- **Files** shows the file read-only, with colours and line numbers (pictures as pictures). Click a line — Shift+click or a drag over the numbers for more — and choose **Comment…**: the lines and your comment go into the thread's message, for the agent to act on.
- **Ctrl+Shift+F** searches the text of every file of the project (ignored files left out); a result opens the file at its line. `Aa`, `ab` and `.*` match case, whole words and regular expressions.
- Type **@** in a thread's message to name a file or folder of the project (`@src/main.cpp`); the agent also gets the named files themselves.
- A file the agent links to in its answer opens in the Files tab at its line.

## The terminal
Each thread has terminals of its own: switch to another thread and back, and its shells, their output and the programs running in them — a dev server, the tests — are still there. **Ctrl+J** shows and hides the terminal. A tab can hold up to four terminals: **split right** (Ctrl+Shift+D) or **down** (Ctrl+Alt+Shift+D); **Ctrl+Alt+]** and **Ctrl+Alt+[** move between them, **Ctrl+Shift+T** opens a new one, **Ctrl+Shift+W** closes one (Brulion asks first if a program still runs in it). A dot on a tab means new output, a bell that a program rang one, and "exit 1" how a program ended. On Windows the terminal runs PowerShell 7 (else Windows PowerShell, else cmd) and needs Windows 10 version 1809 or later; the project's scripts run through that shell, as if typed.

**Ctrl+Shift+F** searches the output (Enter goes up to older matches), **Ctrl+Shift+A** selects all of it, **Ctrl+Shift+Home** and **End** jump to its start and to the latest line. Hold **Ctrl** over a web address or a file path such as `src/main.cpp:12` — it is underlined — and click to open it (files in your editor). A right click offers **Add to chat** — the selection, or what the terminal shows, goes into the thread's message field — and Copy, Paste, Clear and the rest. Deleting a thread ends its terminals; archiving it ends those in which nothing runs. After a restart a thread's tabs come back in their folders, a command typed and waiting for Enter. Change the keys in Settings → Keyboard shortcuts.

## For the curious
- An agent of a thread gets Brulion's chat tools — notify you, ask another agent, ask for a secret — but none of your notes ([[Command line and AI agents]]).
- Asked "let me know when the tests pass", it can send you a notification of its own ([[AI#Notifications from the agent]]).
