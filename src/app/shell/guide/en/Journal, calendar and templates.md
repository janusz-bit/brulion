---
tags: [documentation]
brulion: document
---
# Journal, calendar and templates

## Journal
Every day has its own page — a file in the `journals/` folder named after the date (by default `2026-10-14.md`). The **Journal** view (Ctrl+J) shows the days one below the other: today at the top, earlier days further down, and “Go to day…” opens any date.

- A day's page is created with your first change. Just looking at it doesn't create anything.
- A link to a day, e.g. `[[2026-10-14]]`, is shown as a readable date (“Wed, Oct 14”). A right click on it (on a phone, a long press) opens a menu: “Open day”, “Open in side panel”, “Copy link”, “Change date…”, “Today”, “Tomorrow”, “A day later”, “A week later”. Only the date in the link changes (a label after `|` stays), and you undo the change with `Ctrl+Z`.
- A quick capture (Ctrl+Shift+N) is added at the end of today's journal.
- Text, a link or a `.md`/`.txt` file from another app is saved with the **Save to Brulion** dialog: on Android via **Share → Brulion**, on a computer via **Open with → Brulion** in Windows Explorer or **Open with → Other application → Brulion** in a file manager on Linux, and in the browser version installed as an app (Chrome or Edge on Android) via **Share → Brulion** — text and links. The dialog lets you edit the text and choose **Today's journal** (a block at the end of the day; several files — a block per file) or a **New page** (title from the file name, content unchanged). A file from outside the notebook is copied, and the file itself stays unchanged; a file that is already in the open notebook simply opens. Dates in shared text don't create reminders.
- You can select and copy blocks from several days at once: by Shift+clicking a block of another day, dragging with the mouse or going past the edge of a day with Shift+arrows (on a phone, by tapping a block of another day while selecting is on). In the clipboard each day starts with a link to it — see [[Document and outline#Selecting and copying|Selecting and copying]].
- **Ctrl+F** in the journal searches all days at once: the counter (“3 of 12 · 4 days”) covers every day, Enter and the arrows go to the next day with a result (the journal scrolls there by itself), and matches are highlighted in every visible day. **Ctrl+H** adds replacing: “Replace all” changes all the days, and a single **Undo** restores them all. The search covers the last 1000 days; you find older ones with “In the whole notebook…”.
- You change the journal's file name and folder in *Settings → Notebook*.
- A day saved under the wrong date is moved or renamed like any file: in the folder tree, in the **All pages** list or after “Open as page” (F2, the **More** menu) — see [[Getting started#Folders|Folders]].

## Weekly and monthly notes
Next to the day pages you can keep a page for the **week** (ISO week, starting on Monday) and the **month** — for a plan, a summary or a review. By default they live in the journal folder and are named as in Obsidian: `2026-W45.md` (week 45 of 2026) and `2026-11.md`.

- You open them with **Ctrl+Alt+W** (this week) and **Ctrl+Alt+M** (this month), from the command palette (Ctrl+K: “This week's note”, “Last week's note”, “Next week's note”) or from the calendar: click the **week number** on the left of the month (in the week view — in the top left corner) or the **month's name** or the week's range in the header. The **W** and **M** keys in the calendar open the week and month of the selected day.
- A week number that already has a note is bold and in the accent color, and a header with a note gets a small book.
- Like a day's page, the note is created with your first change — from its own template: `templates/weekly.md` or `templates/monthly.md`. In it, `{{date}}` is the first day of the period, `{{end}}` the last one, `{{week}}` the week number and `{{month}}` the month's name.
- The arrows below the title go to the previous and next week or month.
- Watch out at the turn of the year: January 1, 2027 belongs to week `2026-W53`, and December 30, 2024 to `2025-W01` — that is how the ISO standard counts.
- You change the folder, the file names and the templates in *Settings → Notebook*. In the week's name `yyyy` is the week's year and `ww` its number, and literal text goes in apostrophes (`yyyy-'W'ww`); the month's name is an ordinary date format without the day (`yyyy-MM`, `MMMM yyyy`).

## Calendar
The **Calendar** view has a month, week and agenda layout. It shows:
- due 📅 and scheduled ⏳ dates of tasks,
- reminders ⏰ and alarms,
- days that have a journal entry.

On a phone the month shows a small icon for each kind of open entry: a red exclamation mark — overdue, an amber bell — a reminder or alarm, a blue calendar — due, a violet hourglass — scheduled. Done entries get no icon. A short grey line under the day's number means the day has a journal entry.

Clicking a day — in the agenda, its header — opens its plan, from which you can add a task or reminder without leaving the calendar. A right click on an entry (on a phone, a long press) opens the menu of its date or reminder — the change goes into the note, and the notification has an **Undo** button. The side panel of a journal page also has a small month calendar.

### Calendar from the tray icon
On a computer (Windows and Linux) you open a small calendar without bringing up the Brulion window: from Brulion's icon in the system tray, next to the clock. Just click the icon (or choose **Calendar** from its menu, right click). The pop-up shows today's date, the month with dots on days with entries, and the list of tasks and reminders of the selected day. Clicking a task or **Open journal** shows that day in the Brulion window, and at the bottom there are **Open Brulion** and **Quick capture…**. The calendar goes away when you click elsewhere, press Escape or click the icon again.

What a click on the icon does is chosen in *Settings → Application → Click on the tray icon*: **Calendar** (the default) or **Window**. A double click always shows the window. Brulion doesn't replace the calendar of the Windows clock — Windows doesn't allow that — but you can hide the system clock and pin Brulion's icon to the taskbar. How to do that is described in `docs/WINDOWS.md` in the repository (section “Tray calendar”).

See [[Tasks|Tasks]] and [[Reminders and alarms|Reminders and alarms]].

## Templates
Templates are ordinary notes in the `templates/` folder. You insert them with the `/Template…` command. The variables in them are replaced when inserting:

| Variable | Value |
|---|---|
| `{{date}}` | today's date `YYYY-MM-DD`; `{{date:dd.MM.yyyy}}` — your own format |
| `{{time}}` | the time `HH:MM` |
| `{{weekday}}` | the name of the day of the week |
| `{{week}}` | the week number (ISO) |
| `{{month}}` | the month's name (“November”) |
| `{{end}}` | the last day of the week or month in their notes (elsewhere — the date); `{{end:d MMMM}}` — your own format |
| `{{title}}` | the page's title |
| `{{cursor}}` | where to put the cursor after inserting |

The file `templates/journal.md` is the template of a **new journal day**: an empty day shows it as starting text, and the note is created with your first change. `templates/weekly.md` and `templates/monthly.md` work the same way for weekly and monthly notes. The Sample notebook has ready-made templates `journal` and `Meeting`.

Back to the [[Contents|contents]].
