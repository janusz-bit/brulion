# Queries for a real note vault (search benchmark)

We measure search quality of a notes app (BM25 vs small embedding models) on REAL note collections.
Your vault: `<DIR>/notes/*.md` (one note per file; the file name without `.md` is the title). It is
large (hundreds to thousands of notes). Do not modify the notes.

Write `<DIR>/queries_gen.jsonl`, one JSON object per line:
`{"id": "<name>-g001", "query": "...", "relevant": ["<file name>.md"], "category": "..."}`

## How

1. Look over the vault (ls, sample files with head, grep) and pick about 80 diverse target notes —
   different topics, short and long, popular and obscure.
2. For each target write 1–3 queries the way a person types into a notes app search box: mostly
   2–5 words, sometimes 1 word or a short question. Aim for about 150 queries in total.
3. Each query must have ONE clearly best note. The vault is big and has near-duplicates, so check
   with grep (on the key words, also folded without diacritics) that no other note fits the query
   equally well; if two notes fit equally, list both in `relevant` (best first) or drop the query.

## Categories (follow strictly)

* `dokladne` — words that literally appear in the target note (same forms).
* `odmiana` — (Polish notes only) same meaning, but every content word in a DIFFERENT inflected or
  derived form than anywhere in the note ("wymieniłem opony" → "wymiana opony").
* `synonim` — describes the note's content in different vocabulary, sharing no content-word stems
  with the note or its title; how someone remembers the gist but not the words.
* `jezyk` — the query in the other language: Polish query for an English note, English query for a
  Polish note (avoid words identical in both languages; proper names only as a minor signal).
* `literowka` — a `dokladne`-style query typed sloppily: a typo and/or missing Polish diacritics.

Mix for a POLISH vault: dokladne 25%, odmiana 20%, synonim 30%, jezyk 10%, literowka 15%.
Mix for an ENGLISH vault (the user is Polish and often searches in Polish): dokladne 20%,
synonim 25% (English paraphrase), jezyk 45% (Polish queries, mixing plain and paraphrased ones),
literowka 10%.

## Finish

Self-check with python3: lines parse, ids unique, every `relevant` file exists in `<DIR>/notes`;
print counts per category. Report only the counts and anything unusual about the vault.
