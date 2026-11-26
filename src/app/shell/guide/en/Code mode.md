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
Beside the thread, the **Changes** panel shows the project's changes — pick which in its menu: the **latest turn** or any earlier **turn** of the agent (just what that turn changed; **revert** it alone or go back to before it), everything **uncommitted**, the whole **branch** against its base, or one of the newest **commits**. Each file has its diff (side by side or one below the other, find with Ctrl+F); select lines and **Add to chat** to comment on them for the agent. A file's menu (right click or ⋯) stages, unstages or discards it; a hunk's buttons do the same for a part of it. The main button does what fits now — **commit** (only the files you check, on a new branch if you like), **push**, **pull**, **create a pull request** (with the GitHub command line `gh` signed in) or **publish** the project to GitHub — and asks before pushing to the default branch; the branch menu also **stashes** changes. When a merge or rebase stops on conflicts, a banner lets you take a side of each file, then continue or abort. Under the thread, the **terminal** runs shells in the thread's folder (the terminal icon in the header shows and hides it). **Open in editor** (the `</>` icon) opens the folder in your editor: set its command — `code`, `zed`, `cursor` — in Settings → Extensions → Code mode.

## For the curious
- An agent of a thread gets Brulion's chat tools — notify you, ask another agent, ask for a secret — but none of your notes ([[Command line and AI agents]]).
- Asked "let me know when the tests pass", it can send you a notification of its own ([[AI#Notifications from the agent]]).
