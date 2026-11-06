# Search benchmark dataset spec (personal notes, mostly Polish)

We are measuring search quality for Brulion, a Markdown notes app (BM25 vs small embedding
models). You produce one slice of the benchmark: a folder of notes plus search queries whose
correct answer is known.

## Output layout (inside your slice directory `<DIR>`)

* `<DIR>/notes/<Title>.md` — one note per file. The file name (without `.md`) is the page title,
  exactly like Obsidian/Logseq: natural titles such as `Wymiana opon zimowych.md`,
  `Szarlotka babci.md`, `Trip to Lisbon.md`. Journal pages are named by date: `2025-03-14.md`.
  No subfolders. Titles must be unique within your slice.
* `<DIR>/queries.jsonl` — one JSON object per line:
  `{"id": "<slice>-001", "query": "...", "relevant": ["<Title>.md"], "category": "..."}`
  `relevant` normally holds exactly one file name (just the file name, no folder).

Write files with the Bash tool (heredocs, several notes per call is fine) or Write. Use UTF-8,
real Polish diacritics (ąćęłńóśźż).

## Notes

* Personal notes written by one person: short and long (30–350 words, varied), Markdown with
  Logseq-style bullet outlines (`- `), some headings, tasks (`- [ ] `, `- [x] `), `#tags`,
  `[[links]]` to other pages, dates, amounts, names, phone-like numbers, addresses. Realistic,
  specific, sometimes messy (abbreviations, half sentences), like real notes.
* About 85% Polish, 15% English (the person sometimes writes in English).
* Around 10% are journal pages (`YYYY-MM-DD.md`) mixing several small topics of that day.
* IMPORTANT — distractors: group notes into topic clusters. Within each theme write 7–10 notes
  that are similar to each other (e.g. a car cluster: tyre change, oil change, insurance OC/AC,
  inspection, a breakdown on the motorway, buying a used car, car wash membership, parking fines),
  so that a vague query could match several of them and ranking matters.

## Queries

Short, the way people type into a search box: mostly 2–5 words, sometimes 1 word, sometimes a
natural question ("kiedy wymieniałem olej"). Each query must have one clearly correct note in your
slice — check that no other note in your slice fits it equally well. Write about 2–3 queries per
note on average, over most notes (not every note needs one), mixing categories so that the totals
are roughly: dokladne 25%, odmiana 25%, synonim 30%, jezyk 10%, literowka 10%.

Categories (follow strictly, this is what we measure):

* `dokladne` — the query uses words that literally appear in the note (same word forms).
* `odmiana` — the same meaning as words in the note but every content word is in a DIFFERENT
  inflected or derived form than anywhere in the note (case, number, tense, aspect, gender,
  verb↔noun): note says "wymieniłem opony na zimowe" → query "wymiana opony zimowej". No content
  word of the query may appear in the note in the identical form (stop words like "w", "na", "z"
  do not count).
* `synonim` — describes what the note is about in different vocabulary, sharing NO content-word
  stems with the note (also not with its title): note "Szarlotka babci" with "jabłka, kruche ciasto"
  → query "deser z owocami od babci"... careful: "babci" shared → not allowed; use "rodzinny przepis
  na ciasto owocowe" only if "ciasto" is absent from the note. Think how a person remembers the gist
  but not the words.
* `jezyk` — the query is in the other language: English query for a Polish note, or Polish query
  for an English note. Do not reuse words that are identical in both languages (names are fine
  only if they are not the main signal).
* `literowka` — a `dokladne`-style query typed sloppily: a typo (swapped/missing/extra letter) or
  missing Polish diacritics, or both ("wymiana olju w aucie", "szarlotka babcy").

Avoid queries that only a date or a single proper name would answer (one or two such is fine).

## Finish

At the end run a quick self-check with python3 (no extra packages): every line of
`queries.jsonl` parses, every `relevant` file exists in `<DIR>/notes`, ids are unique, and print
counts per category and the number of notes. Fix problems. Report only those counts.
