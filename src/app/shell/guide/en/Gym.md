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
- **Today**: today's exercises as cards — tick one off, change the last set's weight and reps with − / +, add or remove a set. Every change rewrites just that line of the note. Adding a set starts the **rest timer** (`rest::` of the plan, else 2 minutes) with a notification when it ends.
- **Progress**: every exercise with its record (the estimated one-rep max) and the number of sessions; one exercise has a chart of its progress and all its sessions.

**Gym: exercise from the catalog** makes a page for an exercise from 51 common ones (muscles, equipment) in English or Polish, as your notes are written.

Everything stays plain Markdown: Obsidian and Logseq show the workouts as an ordinary outline with links and tasks. The gym is Brulion's first extension, written in Luau.
