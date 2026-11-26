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
Beside the thread, the **Changes** panel shows the project's changes against git — each file with its diff; after an agent's turn you can see just what that turn changed and **revert the turn**. From there you **commit**, **push** and **create a pull request** (with the GitHub command line `gh` signed in). Under the thread, the **terminal** runs shells in the thread's folder (the terminal icon in the header shows and hides it). **Open in** (the `</>` icon, **Ctrl+O**) opens the folder in your editor; the arrow beside it lists the editors found on this computer — VS Code, Cursor, Zed, the JetBrains IDEs… — and the file manager, and remembers the one you pick (a command of your own goes in Settings → Extensions → Code mode).

## For the curious
- An agent of a thread gets Brulion's chat tools — notify you, ask another agent, ask for a secret — but none of your notes ([[Command line and AI agents]]).
- Asked "let me know when the tests pass", it can send you a notification of its own ([[AI#Notifications from the agent]]).
