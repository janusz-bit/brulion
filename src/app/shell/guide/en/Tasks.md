---
tags: [documentation]
brulion: document
---
# Tasks

A task is a list item with a checkbox. The notation is compatible with the Obsidian Tasks plugin, so tasks stay readable in other apps.

## Statuses
| Notation | Status |
|---|---|
| `- [ ]` | to do |
| `- [/]` | in progress |
| `- [x]` | done |
| `- [-]` | cancelled |
| `- [>]` | waiting (deferred) |

**Ctrl+Enter** in a block switches the status in turn (none → to do → in progress → done → none). Clicking the checkbox ticks the task off or brings it back to to do. Logseq markers (`TODO`, `DOING`, `DONE`, `LATER`, `NOW`, `WAITING`, `CANCELED`) are tasks too.

## Choosing a status
You can pick any status directly, in three ways:

- **Status menu** — right-click the checkbox or long-press it. The menu has To do, In progress, Done, Waiting, Cancelled and No status; the current status is marked. It works wherever you see a task checkbox: in a note and the journal, in [[Queries|query]] results, in embedded blocks, in backlinks, on tag pages, in the calendar and in the Tasks view.
- **Keyboard** — **Ctrl+Shift+Enter** in the edited (or selected) block opens the same menu; the arrows choose the status, Enter confirms, Escape closes.
- **`/` command** — type `/` and the start of the name in a block: “Task: to do”, “Task: in progress”, “Task: done”, “Task: waiting”, “Task: cancelled”, “Task: no status”. Words like `/waiting`, `/deferred`, `/cancel`, `/none`, `/todo`, `/doing`, `/done` work too. The command's text disappears; the rest of the block and the cursor stay.

On a phone, long-press the **Task** button in the toolbar above the keyboard — the status menu opens (a short tap switches the status in turn).

A block that wasn't a task becomes a task. **No status** removes the checkbox (and the ✅ or ❌ date), and the text stays an ordinary list item. Tasks written with Logseq markers keep their style: `TODO` becomes `DOING`, `LATER` — `NOW`, waiting is `WAITING`. You undo a status change in an open note with Ctrl+Z, and in the Tasks view and the calendar with the “Undo” button in the notification.

## Due dates and other dates
The markers can go anywhere in the first line of a task:

| Marker | Meaning |
|---|---|
| `📅 2026-10-20` | due date (deadline) |
| `⏳ 2026-10-18` | scheduled for a day |
| `🛫 2026-10-16` | start |
| `✅ 2026-10-14` | completion date (added automatically) |
| `➕ 2026-10-10` | creation date |
| `⏰ 2026-10-19 18:00` | reminder — see [[Reminders and alarms|Reminders and alarms]] |

After the due, scheduled and start dates you can add a time: `📅 2026-10-20 15:00`.

**Changing a date without hunting for it in the text** — right-click a date marker (📅, ⏳, 🛫, ✅, ➕, ❌) or long-press it. The menu has “Change date…” (a calendar with a time), “Today”, “Tomorrow”, “A day later”, “A week later” and “Remove date”. Only that date changes — the rest of the line stays as it was, and you can undo the change (`Ctrl+Z`). A plain click on the marker still starts editing the text.

The menu works wherever you see a marker: in a note and in the journal, in table cells, in [[Queries|query]] results and in embedded blocks (the change then goes into the note where the task is written, and the notification has an **Undo** button), in the **Tasks** view (on the markers below a task) and in the calendar (on a day's entry).

The markers are ordinary emoji. When the system has no emoji font (Brulion in the browser, some Linux systems), Brulion draws them with its own font — single-colored, in the text color.

You don't need to know these markers by heart. After typing `📅` (or choosing “Due date…” from the `/` menu) just write in English or Polish: “tomorrow”, “friday”, “in 3 days”, “October 15”, “10/15 6:30pm”. Brulion inserts the date as `YYYY-MM-DD` — the file always keeps a date, never words.

## Priorities
`🔺` highest, `⏫` high, `🔼` medium, `🔽` low, `⏬` lowest. No marker means normal priority. Logseq's `[#A]`, `[#B]`, `[#C]` are understood too.

You change the priority from a menu: a right click on the priority marker (on a phone, a long press) gives “Highest” … “Lowest” and “No priority”; the current one is marked. A Logseq `[#B]` stays in Logseq notation (`[#A]`, `[#B]`, `[#C]`).

## Recurrence
The `🔁` marker with a rule: `🔁 every week`, `🔁 every 2 days`, `🔁 every month on the 15th`, `🔁 every weekday`, `🔁 every year`. Adding `when done` counts the next date from the day the task was done, not from the original date.

Rules can also be written in other ways, e.g. “every day”, “every 3 days”, “every week on Monday, Friday”, “every month on the last”, “every year when done” — and in Polish (“codziennie”, “co tydzień”, “po wykonaniu”). Brulion saves them in canonical form.

When you tick off a recurring task, Brulion adds its next occurrence above the completed one.

You change the rule from a menu: a right click on the 🔁 marker (on a phone, a long press) gives “Change rule…”, ready-made rules (“Daily”, “Weekdays”, “Weekly”, “Every 2 weeks”, “Monthly”, “Yearly”), the “From the completion date” switch (`when done`) and “Don't repeat”. Only the rule changes — in `🔁 every week buy milk` the text “buy milk” stays.

## Task dependencies
A task can wait for another one — written as in the Obsidian Tasks plugin: the task being waited for has the id `🆔 fund01`, and the waiting task `⛔ fund01` (several ids separated by commas: `⛔ fund01,walls`).

```
- [ ] Pour the foundation 🆔 fund01
- [ ] Build the walls ⛔ fund01 📅 2026-11-20
```

A task is **blocked** as long as any of the tasks it waits for (in any note) is neither done nor cancelled. In a note, in query results and in the Tasks view the `⛔` marker shows the text of the task being waited for — in red until it is done; a click opens that task. An id that no task has blocks nothing (the marker gets a question mark, the Tasks view says “Missing dependency”); tasks waiting for each other are marked “Dependency cycle”.

You add a dependency without typing markers: in the task's status menu (right click or long press on the checkbox) choose **Add dependency…** and find the task this one should wait for. Brulion gives it an id by itself (six characters) and writes the markers in Obsidian Tasks order. The same is in the command palette (**Ctrl+K**: “Add task dependency…”).

In [[Queries|queries]] `is:blocked` picks blocked tasks, and `-is:blocked` the ones you can do right now.

## Subtasks
A task with tasks indented below it shows a progress counter, e.g. **3/5**: done subtasks out of all of them (at any depth). Cancelled subtasks don't count — neither as done nor as waiting. You see the counter in a note, in query results and in the Tasks view.

## Time tracking
The task's status menu has **Start timer** (and **Stop timer** while it runs); the command palette has “Start or stop task timer” and “Stop timer”. One timer runs at a time: starting a new one stops the previous one. The time goes into the file, into a `:LOGBOOK:` drawer below the task, in Logseq's notation — that is why the timer survives closing the app and reaches your other devices:

```
- [ ] Write the report
  :LOGBOOK:
  CLOCK: [2026-11-03 Tue 10:00:00]--[2026-11-03 Tue 10:25:00] =>  00:25:00
  :END:
```

The times are local (as on the clock). A task shows its total time, e.g. “⏱ 1 h 05 min”, and with a running timer “from 10:00”. **Time spent** in the Tasks view sums up the time of the visible tasks (filters apply) today, this week, this month or overall — by days, pages, tags or tasks.

**Status history** — in the Settings (Editor) you can turn on “Log task status changes”. Then every status change adds a line with the time to `:LOGBOOK:`, e.g. `State "DONE" from "DOING" [2026-11-03 Tue 10:25]`, and the **In progress** status starts the timer and stops it on a change to another status — as in Logseq. Off by default.

## The Tasks view
The **Tasks** view gathers tasks from the whole notebook: a quick add field, filters and the sections Overdue, Today, Tomorrow, This week, Later, No date, Done. “Show as query” turns the current filters into a [[Queries|query]] block that you can paste into a note. Clicking a tag in a task filters the list by that tag; Ctrl+click opens the tag's page, and a right click (on a phone, a long press) the tag menu: “Filter by tag”, “Open tag page”, “Tag overview”, “Open in side panel”, “Copy tag”. A right click or a long press on a marker below a task (due, scheduled, start, reminder, recurrence, priority) opens its menu; you undo the change with the **Undo** button in the notification. Below a task you also see what it waits for (“Waiting for: …” — a click opens that task), how many tasks wait for it, the progress of its subtasks and the logged time.

Tasks with dates are also shown by the calendar ([[Journal, calendar and templates|Journal, calendar and templates]]).

## Android widgets
You can put Brulion widgets on the phone's home screen (long-press an empty spot → **Widgets** → **Brulion**):

- **Tasks — My day** — overdue and today's tasks as in the Tasks view; a larger widget also shows tomorrow's. Tapping a task opens the note at that line.
- **Calendar** — the month with dots and the agenda of the selected day: entries with a time first. A short widget shows only the agenda.
- **Notes** — **Today's journal**, **Quick note**, favorite and recent pages.

The **+** button adds a task without opening the app: type the text, choose **Today**, **Tomorrow**, **No date** or **Date…**, optionally **Time…** (a reminder) and **Alarm**, and tap **Add**. The task goes to the end of today's journal — just as from the app, for example `- [ ] Buy cheddar 📅 2026-10-28` — right away, even when Brulion is closed. Words in the text aren't dates here: “tomorrow at 9” stays text, and you pick the day and time with the buttons. When the entry can't be saved right away (for example the notebook folder happens to be unavailable), it waits, and Brulion adds it after opening — nothing you type gets lost.

The widgets show the state from the moment Brulion was last open: changes from other devices show up after you open the app. At midnight they move the tasks to the new day by themselves. You tick a task off in the app or with the **Done** button in a reminder's notification.
