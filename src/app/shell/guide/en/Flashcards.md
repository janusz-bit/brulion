---
tags: [documentation]
brulion: document
---
# Flashcards

A flashcard is a question with its answer that you want to remember: a word of a language, a capital, a date, a formula. Brulion asks each card just before you would forget it — an easy card rarely, a hard one more often — so that a few minutes a day keep hundreds of cards in your memory. It schedules them with **FSRS**, the algorithm Anki uses.

The cards are ordinary lines of your notes, written as the Obsidian **Spaced Repetition** plugin and **Logseq** write them, so a notebook shared with those apps keeps working in both: each review is saved next to its card, in their own formats. Flashcards are an [[Extensions|extension]] built into Brulion and on from the start; **Settings → Extensions → Flashcards** switches them off and on, sets how many new cards a day you learn and which tags make a note a deck.

## Writing cards
A note becomes a **deck** when it has the `#flashcards` tag (or `#fiszki`, or a tag under one: `#flashcards/spanish`) — anywhere: in its properties or in a line of its own. In such a note:

| You write | You get |
|---|---|
| `Capital of Peru::Lima` | a card: the question before `::`, the answer after it |
| `turtle:::żółw` | two cards, both ways: turtle → żółw and żółw → turtle |
| a question, a line `?`, the answer | a card of several lines; the answer ends at a blank line |
| a question, a line `??`, the answer | the same, both ways |
| `The highest are ==Rysy== in the ==Tatras==.` | a cloze: one card per highlighted part, which the question hides as `[…]` |
| `Born in ==2;;Toruń==, ==1;;Copernicus;;astronomer== …` | numbered clozes: one card per number; the text after `;;` is a hint shown instead of `[…]` |
| `A {{c1::turtle::animal}} is slow` | Anki's clozes, numbered the same way |

A line like `capital:: Warsaw` — one word, `::` and a space, not a list item — is a property, not a card; write `capital :: Warsaw` or `- capital:: Warsaw`. Cards in code — blocks or `inline` — don't count.

In **Logseq** a card is a block tagged `#card`: the block is the question and the blocks under it the answer. `{{cloze Paris}}` in it is hidden while you are asked, and `{{cloze Paris \\ city}}` shows the hint `(city)`. Logseq cards work in Brulion as they are.

## Settings of the cards
The Spaced Repetition plugin lets you change how cards are written, and so does Brulion, with the same settings. They are kept in the notebook (its `.brulion/config.json`), so every device reads your notes alike — **Settings → Extensions → Flashcards**:

- **Separators** — the text between a question and its answer (`::`, and `:::` for both ways) and the line that splits a card of several lines (`?`, and `??` for both ways) can be anything you like, say `->` and `<->`. An empty field switches one off.
- **End of a multi-line card** — set it to, say, `---`, and a card runs up to a line with only that, so it can hold blank lines.
- **Clozes** — besides highlights, as in `a ==b==`, you can make `**bold**` text or `{{curly}}` text a cloze; Anki's `{{c1::…}}` always are.
- **Folders as decks** — every note in a folder is read, tagged or not, and its cards are in the deck of the folder: `Languages/Spanish` becomes `#languages/spanish`.
- **Tags of notes to review** — which tag makes a whole note something to review (`#review` by default; see "Notes to review" below).

## Decks
**Flashcards** in the sidebar (and on the phone's bottom bar, when you choose it) shows how many cards are waiting today — the same number is on its icon. The **Decks** tab lists your decks **by tag** — `#flashcards/spanish` with its sub-decks below it —, **by page** or **by folder**, each with how many cards are due and how many new ones you can learn today. A tap on a deck starts its session; its **⋯** menu also offers studying ahead and the deck's statistics. **Study all** (or Enter) studies every deck at once.

To study only the cards of the page you are reading, run **Review this page's cards** from the command palette (Ctrl+K).

## A session
The card shows its question, large, with the page and the headings it comes from above it. Think of the answer, then:

- **Show answer** (Space) — the answer appears under a line;
- grade how well you knew it: **Again** (1) — you forgot, **Hard** (2), **Good** (3), **Easy** (4). Each button shows when you would see the card again: a new card comes back in 1 or 10 minutes until you know it, a known one in days, weeks, months;
- **Undo** (U) takes the last grade back — the card comes back with its answer shown;
- **Edit** opens the note at the card's line; the **⋯** menu can make a card start over.

New cards come after the reviews, at most 20 a day (Settings changes the number). When everything is done, Brulion says when the next review is due; **Study ahead** brings tomorrow's reviews, and you can learn ten more new cards. The **Statistics** tab shows how many cards you graded each day, how many of your reviews you remembered (aim for about 90 %), how many cards are new, learning, young and mature, and how many come back on each of the next days.

## Notes to review
Not everything is a question. To go back to a whole note now and then — an article, a plan, a page of your notes — tag it **`#review`** (a tag of your own can be set in the settings). On the **Decks** tab a **Notes to review** entry appears with how many notes are due and how many are new; a tap opens them one at a time, the note drawn as it reads, scrollable, with **Edit** to open it in the editor.

Read it, then grade how well you know it: **Hard** (1), **Good** (2) or **Easy** (3) — each button shows in how many days the note comes back. **Skip** (S) leaves a note for later today, **Undo** (U) takes the last grade back. A note you have never reviewed is new; one that is due comes first, the one waiting longest earliest.

The state goes into the note's properties, the way the Spaced Repetition plugin writes it, and nothing else of the note changes:

```markdown
---
tags: [review]
sr-due: 2026-11-23
sr-interval: 3
sr-ease: 250
---
```

`sr-due` is the day the note comes back, `sr-interval` the days between the last two reviews, `sr-ease` how easy the note is for you (250 = 2.5×; Easy raises it, Hard lowers it). The plugin spreads the notes over the days a little, so a hundred notes reviewed on one day do not all return on one day. These numbers — the base ease, the Easy bonus, the interval after Hard, the longest interval and the spreading — are settings of the notebook: *Settings → Extensions → Flashcards*, as in the plugin.

## Where the reviews are saved
After a review, the card's line gets a comment at its end — invisible in Brulion, in Obsidian's reading view and in Logseq:

```markdown
Capital of Peru::Lima <!--SR:!fsrs,2026-11-23T08:00:00.000Z,3,3.17,5.28,2,2,0,0,2026-11-20T08:00:00.000Z-->
```

It is the Spaced Repetition plugin's own format, so the plugin set to FSRS carries on from it (cards it scheduled with SM-2 — `<!--SR:!2026-11-25,10,250-->` — Brulion carries on from too). A Logseq card gets Logseq's `card-…::` properties, so Logseq knows when to ask it. The card's text itself never changes.

Every grade is also a line of a review log, one page a month: `Fiszki/Powtórki 2026-11` (in an English notebook `Flashcards/Reviews 2026-11`). The statistics are read from it; you can delete it at any time, only the statistics start again.

Because a review changes one line, two devices that review different cards of one note merge cleanly through Nextcloud, and when you review the same card on both, the later review stays. With Syncthing the same happens when you merge the conflict copy in the sync center.

> [!tip] From the command line
> `brulion-cli card due` lists the cards to study now and `brulion-cli card review <id> good` grades one — an AI agent can quiz you this way; so `brulion-cli card notes --due` and `brulion-cli card review-note <page> good` do for notes to review; see [[Command line and AI agents]].

## Try it
This page is a deck itself: the cards below wait in **Flashcards**.

#flashcards/brulion

Where does Brulion keep a card's review?::next to the card, in a comment of the Spaced Repetition plugin
Which algorithm schedules the cards?::FSRS
What does Undo take back in a session?
?
The last grade — the card comes back with its answer shown.

Brulion asks a card just before you would ==forget== it.
