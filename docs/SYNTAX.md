# Brulion Markdown dialect (developer spec)

Brulion stores every note as a plain UTF-8 `.md` file. The dialect is CommonMark + GFM
plus a small set of extensions chosen to be **compatible with Obsidian (incl. the Tasks
plugin) and readable by Logseq**. Anything Brulion writes must stay readable as plain
Markdown in any other editor.

This file is the single source of truth for parsers, renderers, importers and exporters.

---

## 1. Vault layout

```
<vault>/
  journals/2026-10-14.md     daily notes (folder + file-name format configurable)
  <any>/<folders>/Page.md    pages; folders act as namespaces ("Projekty/Dom")
  assets/                    attachments (images, pdf, ...)
  templates/                 templates (journal.md = template for new daily notes)
  .brulion/config.json       vault settings shared between devices (synced)
  .trash/                    deleted files (restorable)
```

* **Page title** = file base name without `.md` (`Projekty/Dom.md` → title `Dom`,
  full name `Projekty/Dom`). Frontmatter `aliases:` adds extra link names.
* **Letter case**: names that differ only in case (`dom.md` / `Dom.md`, `projekty/` /
  `Projekty/`) are one page / folder — on case-insensitive storage (Android shared storage, FAT,
  macOS) they are one file, and links do not tell them apart. Brulion never creates such a twin:
  a new page with that name opens the existing one, a rename to it is refused, an imported page
  gets ` 2`, and new files go into the folder as it is spelled.
* Device-local state (sync bookkeeping, merge bases, history snapshots, caches) is **never**
  stored inside the vault — it lives in the app data directory on the device (`QStandardPaths::AppDataLocation`). This keeps Syncthing /
  Nextcloud from syncing machine-specific data.
* Ignored when indexing: `.brulion/`, `.trash/`, `.git/`, `.obsidian/`, `logseq/`,
  `.stfolder`, `.stversions/`, `node_modules/`, temp files (`~syncthing~*.tmp`,
  `.syncthing.*.tmp`, `.~lock.*#`, `*.part`). Syncthing conflict copies
  (`Name.sync-conflict-20261014-101010-ABCDEF.md`) are **not** indexed as pages but are
  surfaced in the Sync Center as conflicts to merge.

### `.brulion/config.json`

```json
{
  "version": 1,
  "journalsFolder": "journals",
  "journalFileFormat": "yyyy-MM-dd",
  "newPagesFolder": "",
  "attachmentsFolder": "assets",
  "templatesFolder": "templates",
  "journalTemplate": "templates/journal.md",
  "defaultReminderTime": "09:00",
  "weekStartsOn": 1,
  "journalMode": "outline",
  "pageMode": "document"
}
```
`journalFileFormat` uses Qt `QDate` format strings (`yyyy-MM-dd`, `dd.MM.yyyy`, …).
`journalMode` / `pageMode` (`"outline"` | `"document"`) are the note modes of daily notes and of
pages the content does not decide (§4 "Tryb notatki"). Missing keys and invalid values fall back
to these defaults; unknown keys are kept when Brulion saves the file.

---

## 2. Links and references

| Syntax | Meaning |
|---|---|
| `[[Page]]` | link to page |
| `[[Page\|Alias]]` | link with display text |
| `[[Page#Heading]]` | link to heading |
| `[[Page#^abc123]]` | link to block with id `abc123` in Page |
| `[[#^abc123]]` / `[[#Heading]]` | same-page link |
| `![[Page]]`, `![[Page#Heading]]`, `![[Page#^id]]` | embed (transclusion) |
| `![[photo.png]]`, `![[doc.pdf]]` | attachment embed |
| `((abc123))` | Logseq-style block reference — resolves the id **vault-wide** |
| `!((abc123))` | block embed |
| `[text](url)`, `[text](relative/path.md)`, `<https://..>`, bare `https://...` | standard links |

**Link resolution** (`target` → file): 1) exact vault path (with or without `.md`,
case-insensitive); 2) path relative to the linking file's folder; 3) unique base-name
match (case-insensitive, Unicode NFC); if several, the one with the shortest path, then
alphabetical; 4) frontmatter `aliases`. Unresolved links point to a page that will be
created on click (in `newPagesFolder`).

**Display:** a link naming a daily note — `[[2026-10-13]]` or the vault's `journalFileFormat`,
without an alias — is shown as a readable date in the UI language (`wt 13 paź`, `Tue, Oct 13`;
with the year when it is not the current one). The file keeps `[[2026-10-13]]`.

## 3. Tags

`#tag`, `#nested/tag`, `#[[tag with spaces]]`. Tag body: Unicode letters/digits, `_`,
`-`, `/`; must contain at least one non-digit (`#1` is not a tag); must be preceded by
start of line, whitespace or `(`. Never recognised inside code spans, fenced code,
formulas (§11), URLs or link targets. A heading `# Title` is not a tag. Frontmatter
`tags: [a, b]` or `tags: a, b` also tags the page. Tags compare case-insensitively; display
keeps the first-seen casing.

## 4. Blocks and outline

* Every **list item** is a block. Nesting by indentation (tab = one level; for spaces the
  child must be indented past the parent's marker). Markers: `-`, `*`, `+`, `1.`, `1)`.
* Outside lists, each paragraph, heading, fenced code, table, blockquote/callout, `---`
  rule is a top-level block (depth 0). A display formula between two lines that are only `$$`
  stays in one block whatever its lines start with (§11).
* **Block id**: ` ^abc-123` (caret + `[A-Za-z0-9-]+`) at the end of a block's first or last
  line (Obsidian style; Brulion writes it at the end of the first line for list items and
  at the end of the last line for paragraphs). Generated ids are 6 chars `[a-z0-9]`.
* **Block properties** (Logseq/Dataview style): lines `key:: value` directly in the block
  (continuation lines of a list item, or lines of a paragraph). Keys `[A-Za-z0-9_-]+`
  (lower-cased). Values: raw string; comma-separated values that are all `[[links]]` or
  `#tags` are parsed as lists. Logseq's `id:: <uuid>` property is treated as the block id.
* **Page properties**: YAML frontmatter at the very start (`---` … `---`), or `key:: value`
  lines at the very top of the file before any other block (Logseq style).

### Tryb notatki / Note mode

Every note is shown in one of two modes; the Markdown is the same in both:

* **outline** (*konspekt*, Logseq-like) — the block editor: a bullet per block, Tab indents,
  folding, zoom, guide lines;
* **document** (*dokument*, Obsidian-like live preview) — a continuous formatted document:
  paragraphs and headings without bullets, lists drawn as ordinary Markdown lists; clicking a
  block edits its Markdown in place.

An explicit choice is stored in the note itself — portable and synced — as the frontmatter key
`brulion`, following the Obsidian plugin convention (like Kanban's `kanban-plugin: board`):

```yaml
---
brulion: document   # or: outline
---
```

**Resolution** (`markdown::resolveNoteMode`, first match wins):

1. the frontmatter key `brulion` (lower-case key) with the value `document` or `outline`
   (case-insensitive, may be quoted); any other value is ignored;
2. daily notes → `journalMode` of `.brulion/config.json` (default `outline`);
3. other pages → their content: the note's top-level blocks as defined above (YAML
   frontmatter and Logseq page properties do not count; headings, code, tables, quotes… count
   as non-list blocks; nested list items are part of their top-level item). With **at least
   two** such blocks: **≥ 70 %** list items → outline, **≤ 30 %** → document;
4. otherwise (empty, a single block, more than 30 % but less than 70 % list items) →
   `pageMode` (default `document`).

So imported Logseq pages (bullets only) open as outlines and Obsidian notes (headings and
paragraphs) as documents without anything being written.

**Writing.** The key is written only on an explicit choice: by the new-page dialog when the chosen
mode differs from what the resolution gives for the initial content (template), and by every
mode switch (`markdown::setNoteModeKey`). A switch changes nothing but that one entry: an
existing `brulion:` entry is replaced in place (keeping its line ending; duplicates are dropped),
a missing one is appended before the closing `---`, and a note without frontmatter gets
`---` / `brulion: …` / `---` in front (after a BOM, with the note's line ending). Other keys,
comments, their order, line endings and the body stay byte-for-byte. Removing the key (no UI
offers it) also removes a frontmatter block that holds nothing else afterwards. Frontmatter that
cannot be edited safely (malformed YAML, a `brulion` key that is not on a line of its own at
the start of the line, e.g. in an indented mapping, or YAML that is no mapping at all — a list,
a single value, or plain text between a first `---` and a later one used as horizontal rules) is
left unchanged — a second `brulion` entry is never added, because other YAML readers refuse
duplicate keys. Obsidian and Logseq show `brulion` as an ordinary page property.

## 5. Tasks (Obsidian Tasks compatible)

A list item starting with a checkbox:

| Checkbox | Status |
|---|---|
| `- [ ]` | `todo` |
| `- [/]` | `doing` |
| `- [x]` / `- [X]` | `done` |
| `- [-]` | `cancelled` |
| `- [>]` | `deferred` |

Logseq markers are also tasks: `- TODO`/`- LATER` → todo, `- DOING`/`- NOW` → doing,
`- DONE` → done, `- CANCELED`/`- CANCELLED` → cancelled, `- WAITING` → deferred.
Logseq priorities `[#A]`/`[#B]`/`[#C]` map to high/medium/low.

Metadata tokens anywhere on the task's first line, outside code spans and formulas (Obsidian
Tasks signifiers); dates are `YYYY-MM-DD`, Brulion additionally allows an optional ` HH:mm`
after due/scheduled/start:

| Token | Field |
|---|---|
| `📅 2026-10-15` | due (deadline) |
| `⏳ 2026-10-15` | scheduled |
| `🛫 2026-10-15` | start |
| `➕ 2026-10-14` | created |
| `✅ 2026-10-14` | done date |
| `❌ 2026-10-14` | cancelled date |
| `🔁 every week` | recurrence (see §7) |
| `🔺` `⏫` `🔼` `🔽` `⏬` | priority highest / high / medium / low / lowest |

Logseq planning lines inside the block are understood too:
`SCHEDULED: <2026-10-15 Thu 10:00 .+1w>` and `DEADLINE: <2026-10-20 Tue>` (repeaters
`+1d`, `++1w`, `.+1m`, `+1y` → recurrence).

**Completing a recurring task** inserts the next occurrence as a new task line directly
above the completed one (dates shifted by the rule), exactly like Obsidian Tasks, and the
completed line gets `✅ <today>`.

## 6. Reminders and alarms

| Syntax | Meaning |
|---|---|
| `⏰ 2026-10-15 09:00` | reminder notification at local date-time |
| `⏰ 2026-10-15` | reminder at `defaultReminderTime` |
| `⏰! 2026-10-15 06:30` | **alarm**: loud, high-priority, stays until dismissed |
| `(@2026-10-15 09:00)` | obsidian-reminder plugin syntax, read-only compatibility |

Reminders may appear in any block (not only tasks), outside code spans and formulas. A block
may carry several. A `🔁` rule in the same block makes the reminder recur. A reminder on a
task whose status is `done` or `cancelled` never fires. Times are **local wall-clock time**
(no time zone stored). The notification title is the block text with all tokens stripped;
tapping it opens the note at that block.

## 7. Recurrence rules

Canonical (written by Brulion, compatible with Obsidian Tasks): `every day`,
`every 3 days`, `every weekday`, `every week`, `every 2 weeks`,
`every week on Monday, Friday`, `every month`, `every month on the 15th`,
`every month on the last`, `every year`, `every 2 years`; optional suffix `when done`
(next date computed from completion date instead of the original date).

Polish input is accepted and normalised to canonical form when Brulion rewrites the line:
`codziennie`, `co 3 dni`, `w dni robocze`, `co tydzień`, `co 2 tygodnie`,
`co tydzień w poniedziałek, piątek`, `co miesiąc`, `co miesiąc 15.`,
`co miesiąc ostatniego`, `co rok`, `co 2 lata`, suffix `po wykonaniu`.

## 8. Natural-language dates (editor input only)

The date picker / slash commands accept Polish and English phrases and insert canonical
tokens: `dziś`, `jutro`, `pojutrze`, `w piątek`, `w przyszły poniedziałek`, `za 2 dni`,
`za 3 godziny`, `za tydzień`, `15 października`, `15.10`, `15.10.2026 18:30`, `o 9`,
`jutro o 7:30`, `today`, `tomorrow`, `next friday`, `in 2 hours`, `Oct 15 6pm`.
Files never contain natural-language dates — only `YYYY-MM-DD[ HH:mm]`.

## 9. Calculations

Brulion's own expression engine (`src/core/calc`, no script evaluation): numbers, variables,
percentages, units, currencies and functions. Results are formatted in the UI language
(Polish `42,5`, English `42.5`) and shown next to the input; the file keeps only the inputs.

### Calc blocks
````
```calc
cena = 12.50          → 12,5
ilość = 4             → 4
cena * ilość          → 50
rabat = 15%           → 15%
ans - rabat           → 42,5
5 km to mi            → 3,11 mi
20% of 150            → 30
sum                   → 139
```
````
* The `→ …` column is what Brulion shows next to each line, not part of the file.
* Each line is evaluated top-to-bottom with a shared scope (one expression, or several
  separated by `;`).
* `name = expr` assigns a variable (Unicode names allowed, e.g. `ilość`); `f(x) = x^2`
  defines a function. `ans` = the previous result (percentages don't replace it).
* `sum`/`total`/`suma`/`razem` alone on a line = the sum of the results above up to the
  previous blank line; `avg`/`average`/`mean`/`średnia` = their average. Percentages,
  aggregate lines and errors don't count; when the results don't add up (a length and plain
  numbers) only the plain numbers are summed — in the example `3,11 mi` is left out:
  12,5 + 4 + 50 + 42,5 + 30 = 139.
* Percent sugar: `x + 10%`, `x - 10%`, `20% of x` / `20% z x` / `20% od x`,
  `x as % of y` / `x jako % z y`; a percentage variable keeps its meaning (`cena - rabat`).
* Units and conversions: `5 km`, `3 h + 20 min`, `100 km / 2 h` → `50 km/h`,
  `5 km to mi` (also `in`, `na`, `w`), `20°C to °F`. Currencies are tags that survive
  arithmetic: `12 zł + 5 zł` → `17 zł`, `€10`, `$5`, `5 PLN`; different currencies are never
  converted.
* Functions (case-insensitive, `,` or `;` between arguments): `sum`/`suma`, `avg`/`średnia`,
  `min`, `max`, `count`, `median`, `round(x[, n])`/`zaokr`, `floor`, `ceil`, `abs`, `sqrt`,
  `pow`, `exp`, `ln`, `log`, trigonometry, `if(cond, a[, b])`/`jeżeli`/`jeśli`, …
* `#` or `//` start a comment (a whole line or after an expression); text before `:` is a
  label (`Czynsz: 1800` → 1800).
* Decimal comma is accepted (`12,5`) outside function arguments (inside calls use `;` between
  arguments: `round(2,345; 2)`); also `1 000 000`, `1e3`, English `1,000.5`.
* Display rounding — only the display, values keep full precision (`5 km to mi to km` is
  `5 km` again): plain numbers up to 10 significant digits; **quantities with a unit at most
  2 decimals** (`3,11 mi`, `3,33 h`; magnitudes below 1 keep two significant digits:
  `0,0042 km`); money exactly 2 decimals when not whole (`12,50 zł`, `17 zł`).

### Inline calculations
Inline code starting with `=`: `` `= cena * 2` `` renders as the result. Variables from
calc blocks earlier on the same page are in scope, as are numeric page properties
(frontmatter and `key:: value`; `due-date` becomes `due_date`).

### Table formulas
Any GFM table body cell whose content starts with `=` is a formula, rendered as its result:

| Ref | Meaning |
|---|---|
| `A1`, `C3` | cell by column letter / data-row number (row 1 = first row under the header) |
| `A1:A5`, `B2:C4`, `B:B` | range, whole column |
| `[Cena]` | cell in the **current row** of the column whose header is `Cena` (case-insensitive) |
| `above`, `below`, `left`, `right` (`powyżej`, `poniżej`) | all numeric cells above / below (same column), left / right (same row) |
| `sum avg min max count round abs if …` | the calc functions |

Example: `| Suma | =sum(above) |`, `| | =[Cena]*[Ilość] |`. Plain cells are read like
people write them (`12,50 zł`, `23%`); empty cells count as 0, ranges skip text. Formulas
are evaluated in dependency order and may use the page's calc variables. Errors render as
`#ERR` with a tooltip; cycles render `#CYCLE`.

## 10. Queries

A fenced block with language `query` renders live results in the editor (tasks can be checked
off right there). Without `limit:` it shows at most 500 items (the total is still reported).

````
```query
# Praca na najbliższy tydzień
type:tasks status:open tag:praca due:<=+7d   // otwarte zadania #praca
sort:due,-priority limit:20
```
````

**Syntax.**
* Tokens are separated by spaces or line breaks. All filters are **ANDed**; the
  comma-separated values of one filter are **ORed** (`status:todo,doing`, `due:today,+7d`).
* `-` before a filter negates it (`-tag:prywatne`, `-path:journals/`); before a word or phrase
  it excludes it (`-kupić`, `-"bez daty"`). `sort`, `group`, `limit` and `type` cannot be
  negated.
* Comments: `//` up to the end of the line; a line starting with `#` followed by a space (or a
  lone `#`) is a comment line (`# Moje zadania`). `#praca` is a tag filter; a `#` in the middle
  of a line is plain text.
* Quotes `"…"`, `„…”`, `“…”`, `«…»` group a phrase; `[[…]]` may contain spaces.
* Shorthands: bare words and quoted phrases = `text:`, `#tag` = `tag:tag`,
  `[[Strona]]` = `ref:Strona`. A value with `//` right after the colon is text (URLs).
* Keys and keyword values are case- and diacritic-insensitive (`ścieżka:` = `sciezka:`,
  `STATUS:Zrobione`) and have Polish spellings. An unknown key or value shows a helpful error
  (in the UI language) instead of results.

| Key (aliases) | Values |
|---|---|
| `type:` (`typ:`) | `tasks` (`zadania`), `blocks` (`bloki`), `pages` (`strony`, `notes`, `notatki`), `reminders` (`przypomnienia`). Default: `tasks` when a task filter, sort or group is used (status, priority, due, scheduled, start, done, created), `reminders` when `remind:` is, else `blocks`. |
| `status:` (`stan:`) | `todo` (`do-zrobienia`), `doing` (`in-progress`, `w-toku`), `done` (`zrobione`, `wykonane`), `cancelled` (`canceled`, `anulowane`), `deferred` (`waiting`, `odłożone`), `open` (`otwarte`) = todo + doing, `closed` (`zamknięte`) = done + cancelled, `all` (`any`, `wszystkie`) |
| `priority:` (`priorytet:`, `prio:`) | `highest` (`najwyższy`), `high` (`wysoki`), `medium` (`średni`), `low` (`niski`), `lowest` (`najniższy`), `none` (`normal`, `brak`, `normalny`) = no priority set |
| `tag:` (`tags:`, `tagi:`), `#tag` | tag with or without `#`; `projekt` also matches nested `projekt/dom`. A block has its own tags, its ancestors' and the page's tags. |
| `page:` (`strona:`) | page title, path, alias or link target; `*` wildcards (`page:*akup*`); `this` (`current`, `ta`, `ten`, `bieżąca`, `tutaj`) = the page containing the query |
| `path:` (`ścieżka:`, `folder:`) | path prefix (`Projekty/`, `journals/2026-10`); with `*` a wildcard pattern matched from the start (`journals/*-13`) |
| `ref:` (`link:`, `links:`, `linki:`, `odnośnik:`), `[[Strona]]` | items linking to a page (`ref:[[Jan Kowalski]]`, `ref:this`); a block counts when it or an ancestor holds the link |
| `text:` (`tekst:`), words, `"phrase"` | contains, case- and diacritic-insensitive (`zolw` finds `żółw`) |
| `due:` (`termin:`, `deadline:`), `scheduled:` (`zaplanowane:`, `plan:`), `start:` (`początek:`), `done:` (`zrobione:`, `wykonane:`), `created:` (`utworzone:`) | date condition on the task's 📅 ⏳ 🛫 ✅ ➕ date |
| `remind:` (`reminder:`, `przypomnienie:`) | date condition on ⏰ reminders |
| `date:` (`data:`, `dzień:`) | date condition on the journal date of the page |
| `sort:` (`sortuj:`) | comma list of `due` (`termin`), `scheduled` (`zaplanowane`), `start`, `done` (`zrobione`), `created` (`utworzone`), `remind` (`przypomnienie`), `date` (`data`), `priority` (`priorytet`), `status`, `page` (`title`, `strona`, `tytuł`), `path` (`ścieżka`); `-key` descending. Missing values sort last. Default order: tasks and blocks by path then line, reminders by time, pages by title. |
| `group:` (`grupuj:`) | `page` (`strona`), `due` (`termin`), `scheduled` (`zaplanowane`), `date` (`data`), `tag` (one group per tag), `status`, `priority` (`priorytet`) |
| `limit:` | 1 … 10000 |

**Date conditions.** An optional operator `<`, `<=`, `>`, `>=` (or `=`) followed by an
expression; dates compare by day, and an item without that date matches only `none`:
* days: `today` (`dziś`, `dzisiaj`), `tomorrow` (`jutro`), `yesterday` (`wczoraj`),
  `pojutrze`, `przedwczoraj`, `2026-10-15`;
* offsets from today: `+7d`, `-2w`, `+1m`, `+1y`, `7d`, `+7` (days); Polish units `+2t`,
  `+1r`, `+3dni`, `+2tyg`;
* periods: `this-week`, `next-week`, `last-week`, `this-month`, `next-month`, `last-month`,
  `this-year`, `next-year`, `last-year` (`week` = `this-week`, …), Polish `ten-tydzień`,
  `przyszły-tydzień`, `zeszły-tydzień`, `ten-miesiąc`, `przyszły-miesiąc`, `ten-rok`, …
  (weeks start on the vault's `weekStartsOn`);
* ranges `A..B`: `today..+7d`, `2026-10-13..2026-10-16`;
* keywords (no operator): `overdue` (`zaległe`) — an open task's date before today, for
  `remind:` before now; `none` (`brak`) — no such date; `any` (`dowolna`) — has one.

Without an operator the date must lie inside the expression's day or period; `<` means before
its start, `<=` up to its end, `>` after its end, `>=` from its start (`due:<=+7d` = due
within a week, overdue included).

**Results.** `type:blocks` hides blocks whose ancestor also matched (they are shown inside
it), frontmatter and `query` blocks. `type:pages` accepts only `tag`, `page`, `path`, `ref`,
`text` and `date` filters, sorting by `page`, `path`, `date` and grouping by `page`, `tag`,
`date`.

More examples:

````
```query
// Przypomnienia na ten tydzień (type:reminders, po kolei)
remind:this-week
```
```query
type:pages tag:projekt sort:page        // strony projektów
```
```query
ref:this                                // bloki, które linkują do tej strony
```
```query
type:tasks ref:this status:open group:page
```
````

## 11. Other rendering extensions (Obsidian compatible)

`==highlight==`, `%%hidden comment%%`, callouts `> [!note] Title` (`note`, `tip`,
`warning`, `danger`, `info`, `todo`, `quote`), footnotes `[^1]`, GFM tables, task lists,
strikethrough, autolinks. Raw HTML in notes is rendered as escaped text (never
executed).

### Math formulas

Formulas are written as LaTeX and drawn by Brulion as Unicode rich text (no KaTeX, no web
view, nothing is executed); the note keeps the LaTeX source. Inline: `$E = mc^2$`. Display,
also over several lines: `$$\frac{a}{b}$$`. A fenced block:

````
```math
\sum_{i=1}^{n} i = \frac{n(n+1)}{2}
```
````

The fence languages are `math`, `latex` and `tex`; such a block is edited in place (the
LaTeX source while editing, the formula otherwise), and Enter starts a new formula line.
Supported: Greek letters, common operators, arrows and set symbols; big operators (`\sum`,
`\int`, `\bigcup`, `\bigoplus`…; `\limits`, `\nolimits` and the styles `\displaystyle`…
are accepted and show nothing); `^` and `_` (also `^{…}`, `_{…}`); `\frac`/`\dfrac`/`\tfrac`
(vulgar fractions where one exists), `\sqrt[n]{…}`; `\text` (with `\&`, `\%`, `\$`, `\#`,
`\_`, `\{`, `\}`), `\operatorname`, `\mathbf`, `\mathbb`, `\boldsymbol`/`\bm`, `\vec`,
`\hat`, `\bar`, accents; `\left…\right`, `\middle` and the sizes `\big`, `\Big`, `\bigg`,
`\Bigg` (also with `l`, `r`, `m`) before a delimiter: a character (`<` and `>` are ⟨ ⟩, `.` is
none), an escaped one (`\{`, `\}`, `\|` is ‖) or a command (`\langle`); matrices (`pmatrix`,
`bmatrix`, `Bmatrix`, `vmatrix`, `Vmatrix`), `cases`, `aligned` and `array`, also nested (only
an environment's own `&` and `\\` separate its cells and rows, not those in braces or escaped).
Unknown commands stay as written; a part nested deeper than 48 levels (braces, arguments,
environments) is shown as its source. Spaces between atoms are ignored as in LaTeX (`\ `,
`\,`, `\quad` are explicit spaces); lowercase Greek is italic, uppercase upright. The HTML
export shows the formula (display formulas are centred).

Where a formula is (in a block's inline text, after code spans and comments are masked):

* `$$…$$` runs to the next `$$`, also over several lines; content of spaces only is no formula.
* For the block structure (§4), a line that is only `$$` (any indentation, also inside a list
  item or a quote) opens a display formula that runs to the next line that is only `$$`, like a
  fence: the lines between belong to the formula and its block even when they look like block
  syntax (`+ b`, `- c`, `> d`, `1. e`, `# f`, `---`). A blank line before that closing line ends
  it (TeX allows none in a formula): the `$$` line is then ordinary text and the following lines
  are blocks as usual. A `$$` that shares its line with the formula does not hold the lines
  together (`$$a` / `+ b$$` breaks at `+ b`) — write such formulas between `$$` lines or in a
  ```` ```math ```` block.
* Inline `$…$` follows pandoc's `tex_math_dollars`, tightened for prices and variables: the
  opening `$` has a non-space character right after it and does not directly follow a letter
  or digit; the closing `$` has a non-space character right before it and is not followed by
  a letter or digit. A `$` with a space before it never closes a formula: the attempt ends
  there, and that `$` may open a formula itself (`$5 a wzór $x$`: only `$x$`). An inline
  formula may go on to the next line of the same block but never crosses a blank line. Text,
  not formulas: `$5 i $10`, `15$, 10$`, `10$/h … 15$/h`, `$PATH:$HOME`, `Niech$x$`.
* A `$` or `$$` without a partner is text and hides no later formula.
* What starts inside a formula belongs to it — in the editor, the HTML export and the index
  (tags, backlinks, tasks, reminders) alike, as with a code span: a `#tag`, `[[link]]`, URL,
  task date (`📅 …`), reminder (`⏰ …`) or `key:: value` there is part of the formula. Whatever
  starts first wins: a formula in a link label (`[$x^2$](Wykres.md)`) leaves the link a link.
  The formula is found in the block's text as the editor shows it: a list item's blank lines
  and code lines end an inline formula like a blank line.
* `\$` is a literal dollar. `\(`, `\)`, `\[` and `\]` are ordinary escapes (literal brackets,
  as the importers write them), not math delimiters — as in Obsidian. (Brulion 0.3.3–0.3.5
  read `\(…\)` and `\[…\]` as formulas; such notes now show the brackets and should use
  `$…$` / `$$…$$`.)

Polish typography (display only, the file is unchanged): a one-letter word (`a`, `i`, `o`, `u`,
`w`, `z`) never ends a line — the space after it is shown as a no-break space.

## 12. Templates

Files in `templatesFolder`. Variables: `{{date}}` (yyyy-MM-dd), `{{date:FORMAT}}`
(Qt date format), `{{time}}` (HH:mm), `{{title}}`, `{{weekday}}` (localised),
`{{cursor}}` (caret position after insert). `journalTemplate` is applied to new daily notes:
a day without a note shows the template as its starting text, and the note is created — with
the template, filled for that day — by the first change; looking at a day never creates it.
