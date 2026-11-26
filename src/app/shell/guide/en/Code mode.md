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
**Add a project…** (the folder icon next to *Projects*, or in the palette) and pick its folder. Each project lists its threads under it; its **+** starts a new thread, **⋯** (or a right click) opens it in your editor, shows the folder, sets its **name and scripts** and takes it off the list — its folder and its threads stay. Projects are kept on this device only.

**Scripts** are the project's buttons over the thread — a line each, `name: command`:

```
dev: npm run dev
test: npm test
```

A click runs the command in the thread's folder, in a new tab of the terminal.

## Threads
A new thread starts with your first message. Above it choose where the agent works:
- **Local** — in the project's folder itself;
- **New worktree** — in a git worktree of its own on a new branch (`brulion/<name>`), so several agents can work at once without getting in each other's way. Worktrees go to `~/.brulion/worktrees` (change it in Settings → Extensions → Code mode).

Everything of the [[AI|AI chat]] works in a thread: writing on while it answers, the agent's model and effort, approvals and plans, its subscription's limits, side questions, asking other agents. The sidebar shows each thread's state: a spinner while the agent works, a question mark when it asks you something, a red mark when it failed, a dot when its answer is unread. A right click archives a thread or moves it to the trash.

A thread is kept as a page in your notebook's `AI` folder, with its project named in it — never in the journal, and never in the notes' AI chat.

## Changes and terminal
Beside the thread, the **Changes** panel shows the project's changes against git — each file with its diff; after an agent's turn you can see just what that turn changed and **revert the turn**. From there you **commit**, **push** and **create a pull request** (with the GitHub command line `gh` signed in). Under the thread, the **terminal** runs shells in the thread's folder (the terminal icon in the header shows and hides it). **Open in editor** (the `</>` icon) opens the folder in your editor: set its command — `code`, `zed`, `cursor` — in Settings → Extensions → Code mode.

## The terminal
Each thread has terminals of its own: switch to another thread and back, and its shells, their output and the programs running in them — a dev server, the tests — are still there. **Ctrl+\`** shows and hides the terminal. A tab can hold up to four terminals: **split right** (Ctrl+Shift+D) or **down** (Ctrl+Alt+Shift+D); **Ctrl+Alt+]** and **Ctrl+Alt+[** move between them, **Ctrl+Shift+T** opens a new one, **Ctrl+Shift+W** closes one (Brulion asks first if a program still runs in it). A dot on a tab means new output, a bell that a program rang one, and "exit 1" how a program ended.

**Ctrl+Shift+F** searches the output (Enter goes up to older matches), **Ctrl+Shift+A** selects all of it, **Ctrl+Shift+Home** and **End** jump to its start and to the latest line. Hold **Ctrl** over a web address or a file path such as `src/main.cpp:12` — it is underlined — and click to open it (files in your editor). A right click offers **Add to chat** — the selection, or what the terminal shows, goes into the thread's message field — and Copy, Paste, Clear and the rest. Deleting a thread ends its terminals; archiving it ends those in which nothing runs. Change the keys in Settings → Keyboard shortcuts.

## For the curious
- An agent of a thread gets Brulion's chat tools — notify you, ask another agent, ask for a secret — but none of your notes ([[Command line and AI agents]]).
- Asked "let me know when the tests pass", it can send you a notification of its own ([[AI#Notifications from the agent]]).
