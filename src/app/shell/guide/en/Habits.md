---
tags: [documentation]
brulion: document
---
# Habits

A habit is an item you want to check off regularly: meditation, running, reading. Brulion counts your streak of days without a break, shows how often you succeed and draws a month calendar. Everything stays in ordinary notes: a habit is a list item with a property, and every check-off is a task in that day's journal — so it syncs like any note and is readable in Obsidian and Logseq.

Habits are an [[Extensions|extension]] built into Brulion and on from the start. **Settings → Extensions → Habits** switches them off (the sidebar's Habits and the palette's command go with them, your notes stay as they are) and on again; **The habits' code → Edit the code** puts their code into a page of your notebook, where you can read and change it ([[Extensions#The code of the gym and the habits]]).

## The Habits view
Open **Habits** from the sidebar (below Tasks) or from the command palette (Ctrl+K, “Habits”). At the top it says how many of today's habits are done (“2 of 3 done today”), and every habit has a card:

- the **circle** on the left checks the habit off for today; a second click undoes it. A right click or a long press opens a menu: **Done today**, **Skip today** — an excused day that doesn't break the streak (illness, travel) —, **No check today**, **Open its definition** (the page where the habit is written, at its line) and **Open today's journal**. The card's **“⋯”** button has the same menu;
- next to the name is the **schedule** in words;
- a flame shows the **streak** (how many times in a row it worked), the longest streak, the **completion rate** in percent, and for “N times a week” habits the week's progress, e.g. “2 of 3 this week”;
- the **month calendar**: green days are done, red ones missed, crossed-out ones skipped, a day the habit is due has a thin outline and today a ring. Clicking a past day checks it off or undoes the check (when you forgot to enter yesterday); a right click or a long press on a day offers done, skipped (no streak lost), no check, that day's journal and the habit's definition. The arrows above the cards (or ← and →, PgUp and PgDn) change the month, and **This month** comes back.

Every check-off from the view can be undone with the **Undo** button in the notification. A habit whose schedule Brulion doesn't understand, or one written twice, has a card that says so, with a link to where it is written.

## New habit
The **Add habit** button (or the N key in the view) asks for a name and how often — “every day”, “mon, wed, fri”, “3 times a week”; left empty, it is every day. When Brulion doesn't understand the schedule, or a habit of that name exists already, it says so and asks again, with what you typed still there. The habit goes onto the page with your first habit, and when there is none yet — onto a new page “Habits”; the notification's **Open** button takes you there.

You can also write a habit yourself, in any note: it is a list item with a `habit::` property (or `nawyk::`), whose value is the schedule. For example, these three items are real habits of this notebook — take a look at the Habits view:

- Meditation
  habit:: every day
- Running #health
  habit:: mon, wed, fri
- Reading
  habit:: 3 times a week

The name is the item's text (tags and link brackets don't count, and neither does letter case). You make a whole page a habit by adding `habit: every day` to its YAML front matter — the name is then the page's title. A `start::` property with a date (`start:: 2026-11-01`) says when to start counting; without it, counting starts with the first check-off.

## Schedules
| Notation | When |
|---|---|
| `every day`, `daily`, `every 2 days` | every day, every other day |
| `mon, wed, fri`, `monday and thursday`, `every weekday` | on these days of the week |
| `every 2 weeks on Monday` | every other Monday |
| `every month on the 15th`, `every month on the last` | on that day of the month |
| `3 times a week`, `once a week`, `twice a month`, `every week` | any days, as long as it's that many times a week (month) |

Polish notations are understood too: `codziennie`, `pn, śr, pt`, `3 razy w tygodniu`. A schedule Brulion doesn't understand (e.g. “every other tuesday”) isn't counted by the card — it shows a warning so you can fix it.

## Checking off in the journal
A check-off is a task in the journal with the `#habit` tag (or `#nawyk`) and the habit's name:

```markdown
- [x] Meditation #habit ✅ 2026-11-03
- [-] Running #habit ❌ 2026-11-03
- [ ] Reading #habit
```

`[x]` is done, `[-]` (cancelled) — skipped, and `[ ]` — not yet. The Habits view writes exactly such lines — it ticks the day's line of the habit when there is one, else adds `- [x] Meditation #habit ✅` with today's date next to the day's other check-offs —, but you can write and tick them yourself in the journal — they count the same. When one day has two journal notes (a copy a sync left, say), the check-offs of both count, and the view looks at both before it writes. It's handy to add your habits to the journal template (see [[Journal, calendar and templates|Journal, calendar and templates]]): every new day then starts with a list of `- [ ] Meditation #habit` to check off.

## How the streak is counted
- For habits on specific days only those days count. A day on which the habit was scheduled but not checked off — including a day **without a journal note** — breaks the streak. Today doesn't break it while it lasts.
- For “N times a week” habits whole weeks (months) count: a week with at least N check-offs extends the streak. Every skipped day lowers the required number by one. The first, incomplete week counts only if it succeeded.
- Skipped days (`[-]`) don't break the streak and don't lower the completion rate.
- An extra check-off on a day the habit wasn't scheduled shows in the calendar but doesn't change the streak.

> [!tip] From the command line
> `brulion-cli habit list` shows your habits with their streaks, and `brulion-cli habit tick Meditation` checks a habit off for today — see [[Command line and AI agents|Command line and AI agents]].
