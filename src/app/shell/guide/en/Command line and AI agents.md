---
tags: [documentation]
aliases: [brulion-cli, AI agents, MCP]
brulion: document
---
# Command line and AI agents

On a computer with Linux or Windows, Brulion also has a **command line**: the `brulion-cli` program. It appends to the journal, adds tasks and reminders, searches, runs queries and moves pages — without opening a window, exactly the way the app does it. It comes in handy in scripts, in the terminal and above all for **AI agents** (e.g. Hermes or Claude Code), which can keep your notes for you when you ask them to.

## Where to get it
- **NixOS**: it is in the Brulion package (`brulion-cli`). For a computer without a desktop, e.g. a Raspberry Pi running an agent, there is a lighter package with only the command line.
- **AppImage**: `Brulion.AppImage cli …` — the same program inside the app's file.
- **Windows** (preview): `brulion-cli.exe` lies in the program folder next to `brulion.exe` — after installation in `%LOCALAPPDATA%\Programs\Brulion`, in the portable version in the unpacked `Brulion` folder.
- On Android and in the browser there is no command line; an agent reaches those notes through a folder synced with Syncthing or Nextcloud.

## First commands
```sh
brulion-cli capture "Buy milk tomorrow at 6pm"    # like a quick capture: a task with a reminder
brulion-cli task list --due '<=+7d'                # open tasks for the coming week
brulion-cli task add "Return the book" --due "friday"
brulion-cli journal append "Gift idea"             # append to today's journal
brulion-cli --format text journal yesterday        # read yesterday's journal
brulion-cli week append "Review goals"             # append to this week's note
brulion-cli --format text month "last month"       # read last month's note
brulion-cli --format text habit list               # habits: today's check-offs, streaks
brulion-cli habit tick Meditation                  # check a habit off for today
brulion-cli search cafe                            # also finds “café”
brulion-cli help                                   # all commands
```

You write dates in English or Polish (“tomorrow at 9”, “friday 18:00”, “in 2 hours”), and they go into the file as always: `2026-10-15 09:00`. The command `brulion-cli date parse "…"` shows how Brulion will understand a date.

## Which notebook
The program takes the notebook from the `--vault <folder>` option, from the `BRULION_VAULT` variable or — when you give neither — the one last open in the app. The folder must exist: a typo in the path doesn't create an empty notebook (only `brulion-cli init <folder>` creates a new one).

## Safety
- **Version history.** When the command line runs on the same computer as the app, every change it makes goes into the page's history with the description “Replaced from the command line (agent)” and a terminal icon. Open the page's history, and you see what the agent changed and can restore the previous version with one click. An agent on another device (e.g. on a Raspberry Pi) keeps this history on its own side, and the app sees its change when it arrives through sync — as a change from sync or from another program; you can restore that from the history too.
- **Nothing gets lost.** The program writes only when the file is the way it read it; if at the same moment you are typing in the app or Syncthing has brought a change, both changes are kept. The app, too, never overwrites an agent's addition it hasn't seen yet — it merges it with your text.
- **Careful with deleting.** Commands that overwrite, move or delete something work only with `--allow-destructive`, and deleted pages go to the trash. `--read-only` only allows reading, `--dry-run` shows the change without saving it.
- **The app's settings and reminders** stay untouched: the command line doesn't change them and doesn't mark anything as shown.

## AI agents (MCP)
`brulion-cli mcp --vault <folder>` offers the same commands as MCP tools — the standard AI agents use to connect to programs. An agent can then, for example, write in the journal what you dictate to it, add a task with a due date, or tell you what you have planned for tomorrow. With `--read-only` the agent can only read.

Further calls of `brulion-cli` are fast even with a large notebook: it uses the same ready-made index as the app (in the device's data) and re-analyzes only the notes changed since the last time (it only reads the others to check that they haven't changed).

> [!warning] Note content is data, not commands
> Notes may come from an import or a shared folder. A well set up agent never carries out “commands” found in the content of notes. That is why destructive commands are off by default, and every change is in the history.

An agent running on another computer (e.g. on a Raspberry Pi) works on a copy of the notes folder that Syncthing or the Nextcloud client keeps in sync — see [[Sync|Sync]]. An example configuration of Hermes on NixOS is in `docs/NIXOS.md` in the repository.

**Reminders** added by an agent ring wherever the app runs: on a computer (in the tray) and on the phone, once the app sees the change — see [[Reminders and alarms|Reminders and alarms]].

## Extensions written by an agent
An agent can also write [[Extensions|extensions]] — commands, views and reactions in Luau — and try them before you see them:

```sh
brulion-cli extension docs views                     # the API reference, or a part of it
brulion-cli extension eval 'b.query("type:tasks status:open").total'
brulion-cli extension write "Word count" --code "$(cat wc.luau)" --description "Counts words"
brulion-cli extension run "Word count" count --page Home    # changes come back as a diff, nothing is written
brulion-cli extension list                           # "approval": waiting for you
```

The code never runs in the app until you approve it there (**Run** on the page, or **Settings → Extensions**), and each change by the agent waits for you again.

## Open in the app
`brulion-cli app open <page>` shows a page in the running app (on the same computer), `app today` — today's journal, and `app capture` opens a quick capture.

## Controlling the running app
An agent can also control the Brulion window itself: check what is on the screen, go to a view, click, type text, press a shortcut (e.g. Ctrl+K), run a command from the palette and take a screenshot. It comes in handy when the agent is to show you something or check how a page or view looks.

This is **off** by default. You turn it on in **Settings → Application → For developers → “Allow agents to control the app (local socket)”** — it works right away, without a restart; below the switch you see which socket the app listens on. For a single run you can also turn it on with the `brulion --remote-control` option.

```sh
brulion-cli app state                     # what the app shows
brulion-cli app find Tasks                # window elements with handles (h12) and position
brulion-cli app click --text Tasks        # click (or: app click h12, app click 120,340)
brulion-cli app key Ctrl+K                # command palette
brulion-cli app type "calendar"           # type text — accented letters too
brulion-cli app wait --idle               # wait until everything is saved and settled
brulion-cli app screenshot ~/window.png   # screenshot of the window
```

An agent using MCP gets these tools when you run `brulion-cli mcp --app-control`; it then sees the screenshot as a picture.

> [!warning] Turn it on only while working with an agent
> The socket is available only to your account (other users of the computer can't connect), but as long as control is on, **any program you run** can click and type in Brulion and read the notes it shows. Passwords and tokens are never passed on, and every command goes into the [[Sync#Event log|Event log]] (in detailed mode).

On Android and in the browser there is no such control — there the app is tested with developer tools (adb, Playwright).

## For the curious
- The output of every command is JSON (`--format text` — readable for people), and the exit code tells you what happened: 0 fine, 3 not found, 4 conflict (e.g. the line changed since it was read), 7 refused.
- You point to tasks and lines with a reference from the output, e.g. `Home.md#L12@3f9a01bc` — if the line has changed in the meantime, the command refuses instead of changing something else.
- Help (`brulion-cli help`), errors and warnings are in Polish when the system language is set to Polish (e.g. `LANG=pl_PL.UTF-8`), and in English otherwise; `LANGUAGE=en brulion-cli …` forces English. Command and option names, error codes and JSON keys are always English, and an agent using MCP always gets English descriptions and messages.
- The full technical documentation: `docs/CLI.md` in the repository; the list of commands and options: `brulion-cli help`. Query syntax: [[Queries|Queries]]; tasks: [[Tasks|Tasks]].
