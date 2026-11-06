---
tags: [documentation]
brulion: document
---
# Habits

A habit is an item you want to check off regularly: meditation, running, reading. Brulion counts your streak of days without a break, shows how often you succeed and draws a month calendar. Everything stays in ordinary notes: a habit is a list item with a property, and every check-off is a task in that day's journal — so it syncs like any note and is readable in Obsidian and Logseq.

## The Habits view
Open **Habits** from the sidebar (below Tasks) or from the command palette (Ctrl+K, “Habits”). Every habit has a card:

- the **circle** on the left checks the habit off for today; a second click undoes it. A right click or a long press also gives **Skip** — an excused day that doesn't break the streak (illness, travel);
- below the name is the **schedule** in words, and for “N times a week” habits the week's progress, e.g. “2 of 3 this week”;
- a flame shows the **streak** (how many times in a row it worked), the longest streak and the **completion rate** in percent;
- the **month calendar**: green days are done, red ones missed, crossed-out ones skipped, and today has an outline. Clicking a past day checks it off or undoes the check (when you forgot to enter yesterday). The arrows above the cards change the month.

The card's “⋯” menu opens the page where the habit is written, or today's journal. Every check-off from the view can be undone with the **Undo** button in the notification.

## New habit
The **Add habit** button (or the N key in the view) asks for a name and how often. Brulion shows right away how it understands the schedule. The habit goes onto the page with your first habit, and when there is none yet — onto a new page “Habits”.

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

`[x]` is done, `[-]` (cancelled) — skipped, and `[ ]` — not yet. The Habits view writes exactly such lines (next to the day's other check-offs), but you can write and tick them yourself in the journal — they count the same. It's handy to add your habits to the journal template (see [[Journal, calendar and templates|Journal, calendar and templates]]): every new day then starts with a list of `- [ ] Meditation #habit` to check off.

## How the streak is counted
- For habits on specific days only those days count. A day on which the habit was scheduled but not checked off — including a day **without a journal note** — breaks the streak. Today doesn't break it while it lasts.
- For “N times a week” habits whole weeks (months) count: a week with at least N check-offs extends the streak. Every skipped day lowers the required number by one. The first, incomplete week counts only if it succeeded.
- Skipped days (`[-]`) don't break the streak and don't lower the completion rate.
- An extra check-off on a day the habit wasn't scheduled shows in the calendar but doesn't change the streak.

> [!tip] From the command line
> `brulion-cli habit list` shows your habits with their streaks, and `brulion-cli habit tick Meditation` checks a habit off for today — see [[Command line and AI agents|Command line and AI agents]].
