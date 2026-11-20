---
tags: [documentation]
brulion: document
---
# Gym

The gym is a training log in your journal: you write the sets as ordinary notes, and Brulion keeps a plan, adds weight when you are ready, times the rest between sets and draws the progress of every exercise. Switch it on in **Settings → Extensions → Gym**; **Gym** then appears in the sidebar and in the command palette (Ctrl+K).

## Writing a workout
A workout is an item tagged `#workout` (or `#trening`) in a day's journal, with one exercise per sub-item:

```markdown
- Workout A #workout
  - [x] [[Squat]] 100 kg 5×5
  - [x] [[Bench press]] 60 kg × 10; 80 kg × 5, 5, 4 @9
  - [x] [[Pull-up]] BW × 10, 8, 6
  - [ ] [[Dip]] BW+10 kg 3×8
  - [[Plank]] 3×60 s
- [[Running]] 5 km 27:30 #workout
```

- `100 kg 5×5` — 5 sets of 5 at 100 kg; `80 kg × 5, 5, 4` — three sets with their reps; groups at different weights go after `;`.
- `BW` (or Polish `MC`) — bodyweight, `BW+10 kg` with a weight, `BW-20 kg` assisted; `3×60 s`, `10 min` — timed sets; `5 km 27:30` — distance and time; `@8` — RPE.
- Typing on a phone is fine: `100kg 3x5`, `× 5,5,4`, `4x8 60kg` are understood, and **Gym: tidy the sets** (palette, the cursor on the line) writes the line in Brulion's spelling. Comments stay: `(knee hurts)`, `— lighter`.
- A done task or a plain item is history; an open task `[ ]` is a plan for today.

## Plans
A plan is a page tagged `workout-plan` (or `plan-treningowy`) with the exercises without weights:

```markdown
tags:: workout-plan
rest:: 3 min

- [[Squat]] 3×5
- [[Deadlift]] 1×5
  progression:: +5 kg
```

**Start a workout** (the Gym view's button, or the palette) asks for the plan and writes it into today's note as tasks, each at the weight of the last session — 2,5 kg heavier (5 lb, or the line's `progression::`) when every planned set was done there with the planned reps. The exercises' checkboxes are sets, not to-dos: the Tasks view leaves them out (a query with `tag:trening` shows them).

## The Gym view
- **Today**: today's exercises as cards — tick one off, change the last set's weight and reps with − / + or type them in (tap the number, type `32,5`, then Enter or tap elsewhere), add or remove a set. Every change rewrites just that line of the note. Adding a set starts the **rest timer** (`rest::` of the plan, else 2 minutes) with a notification when it ends — on Android it comes with Brulion closed too.
- **Progress**: every exercise with its record (the estimated one-rep max) and the number of sessions; one exercise has a chart of its progress and all its sessions.
- **Volume**: sets × reps × weight for every muscle, week by week — this week against the average of the weeks before, a chart of the weeks and a heat map: a row per muscle, a square per week, the stronger the more work (tap a square to see the numbers). An exercise's primary muscles get its whole volume, the secondary ones half; bodyweight sets count with the body weight of your log (below).
- **Weight**: the body weight log. Type a weigh-in — `82.5`, `82.5 kg 18% morning` — and Enter: `- 82.5 kg 18% morning #weight` goes into today's note. The chart shows the weigh-ins and the trend (an average smoothed by 10 % a day), the tiles the latest weigh-in, the trend and its change over 7 and 30 days.

Choose kg or lb in **Settings → Extensions → Gym → Settings**.

## Body weight
A weigh-in is an ordinary journal item tagged `#weight` (or `#waga`):

```markdown
- 82.5 kg 18% morning #weight
```

The weight with `kg` or `lb` (a number alone: the unit of the settings), the body fat in per cent if you like, any other words a note. Several weigh-ins a day: the last one counts. **Gym: log body weight** in the palette writes one too, and `{{ext:silownia/waga}}` in any note shows the latest weigh-in and the trend.

## The exercise catalog
The **Exercise catalog** (a button in Today, or the palette) has over 870 exercises: Brulion's 51 and all of free-exercise-db (public domain). Search by name — in English or Polish, any case — and narrow by muscle, equipment or kind. An exercise shows its muscles and instructions; add it to today's workout or create its page:

```markdown
catalog:: back-squat
muscles:: quadriceps
secondary-muscles:: calves, glutes, hamstrings, lower back
equipment:: barbell
```

The muscles of an exercise's page (`muscles::`, `secondary-muscles::`) count in the volume before the catalog's — that is how your own exercise gets its muscles. **Gym: exercise from the catalog** in the palette asks for a name and creates the page at once.

Everything stays plain Markdown: Obsidian and Logseq show the workouts as an ordinary outline with links and tasks. The gym is Brulion's first extension, written in Luau: its code can become a page of your notebook and be changed there (see [[Extensions]]).
