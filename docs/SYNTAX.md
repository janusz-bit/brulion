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
* Device-local state (sync bookkeeping, merge bases, history snapshots, caches) is **never**
  stored inside the vault — it lives in IndexedDB on the device. This keeps Syncthing /
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
  "weekStartsOn": 1
}
```
`journalFileFormat` uses date-fns tokens. Missing keys fall back to these defaults.

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

## 3. Tags

`#tag`, `#nested/tag`, `#[[tag with spaces]]`. Tag body: Unicode letters/digits, `_`,
`-`, `/`; must contain at least one non-digit (`#1` is not a tag); must be preceded by
start of line, whitespace or `(`. Never recognised inside code spans, fenced code,
URLs or link targets. A heading `# Title` is not a tag. Frontmatter `tags: [a, b]` or
`tags: a, b` also tags the page. Tags compare case-insensitively; display keeps the
first-seen casing.

## 4. Blocks and outline

* Every **list item** is a block. Nesting by indentation (tab = one level; for spaces the
  child must be indented past the parent's marker). Markers: `-`, `*`, `+`, `1.`, `1)`.
* Outside lists, each paragraph, heading, fenced code, table, blockquote/callout, `---`
  rule is a top-level block (depth 0).
* **Block id**: ` ^abc-123` (caret + `[A-Za-z0-9-]+`) at the end of a block's first or last
  line (Obsidian style; Brulion writes it at the end of the first line for list items and
  at the end of the last line for paragraphs). Generated ids are 6 chars `[a-z0-9]`.
* **Block properties** (Logseq/Dataview style): lines `key:: value` directly in the block
  (continuation lines of a list item, or lines of a paragraph). Keys `[A-Za-z0-9_-]+`
  (lower-cased). Values: raw string; comma-separated values that are all `[[links]]` or
  `#tags` are parsed as lists. Logseq's `id:: <uuid>` property is treated as the block id.
* **Page properties**: YAML frontmatter at the very start (`---` … `---`), or `key:: value`
  lines at the very top of the file before any other block (Logseq style).

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

Metadata tokens anywhere on the task's first line (Obsidian Tasks signifiers); dates are
`YYYY-MM-DD`, Brulion additionally allows an optional ` HH:mm` after due/scheduled/start:

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

Reminders may appear in any block (not only tasks). A block may carry several. A `🔁` rule
in the same block makes the reminder recur. A reminder on a task whose status is `done`
or `cancelled` never fires. Times are **local wall-clock time** (no time zone stored).
The notification title is the block text with all tokens stripped; tapping it opens the
note at that block.

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

### Calc blocks
````
```calc
cena = 12.50
ilość = 4
cena * ilość          → 50
rabat = 15%
ans - rabat           → 42.5
5 km to mi            → 3.11 mi
20% of 150            → 30
sum                   → (sum of the result lines above, up to the previous blank line)
```
````
* Each line is evaluated top-to-bottom with a shared scope (mathjs semantics + units).
* `name = expr` assigns a variable (Unicode names allowed, e.g. `ilość`). `ans` = previous
  result. `sum`/`total`/`suma` alone on a line = sum of results above until a blank line;
  `avg`/`średnia` likewise.
* Percent sugar: `x + 10%`, `x - 10%`, `20% of x`, `20% z x`.
* `#` or `//` start a comment; text after `:` is a label (`Czynsz: 1800` → 1800).
* Decimal comma is accepted (`12,5`) when unambiguous.
* Results are rendered next to lines; the file keeps only the inputs.

### Inline calculations
Inline code starting with `=`: `` `= cena * 2` `` renders as the result. Variables from
calc blocks earlier on the same page are in scope, as are numeric page properties.

### Table formulas
Any GFM table cell whose content starts with `=` is a formula, rendered as its result:

| Ref | Meaning |
|---|---|
| `A1`, `C3` | cell by column letter / data-row number (row 1 = first row under header) |
| `A1:A5`, `B2:C4` | range |
| `[Cena]` | cell in the **current row** of the column whose header is `Cena` |
| `above`, `left` | all numeric cells above (same column) / left (same row) |
| `sum avg min max count round abs if ...` | functions (mathjs) |

Example: `| Suma | =sum(above) |`, `| | =[Cena]*[Ilość] |`.
Errors render as `#ERR` with a tooltip; cycles render `#CYCLE`.

## 10. Queries

A fenced block with language `query` renders live results. One filter per line or space
separated; all filters are ANDed:

````
```query
type:tasks status:todo,doing tag:praca due:<=+7d sort:due limit:20
```
````

| Filter | Values |
|---|---|
| `type:` | `tasks` (default when any task filter used), `blocks`, `pages`, `reminders` |
| `status:` | `todo,doing,done,cancelled,deferred`, also `open` (= todo,doing) |
| `tag:` / `-tag:` | tag name without `#` (include / exclude) |
| `page:` / `path:` | page name / path prefix |
| `due:` `scheduled:` `start:` `done:` `remind:` | date expr: `today`, `tomorrow`, `yesterday`, `YYYY-MM-DD`, `+7d`, `-2w`, `this-week`, `next-week`, `this-month`, `overdue`, `none`, `any`, with optional `<`, `<=`, `>`, `>=` |
| `priority:` | `highest,high,medium,low,lowest` |
| `"text"` or bare words | full-text contains (case/diacritic-insensitive) |
| `sort:` | `due`, `scheduled`, `priority`, `page`, `created`, `-due` (descending) |
| `group:` | `page`, `due`, `tag`, `status` |
| `limit:` | number |

## 11. Other rendering extensions (Obsidian compatible)

`==highlight==`, `%%hidden comment%%`, callouts `> [!note] Title` (`note`, `tip`,
`warning`, `danger`, `info`, `todo`, `quote`), footnotes `[^1]`, GFM tables, task lists,
strikethrough, autolinks. Raw HTML in notes is rendered **sanitised** (DOMPurify), never
executed.

## 12. Templates

Files in `templatesFolder`. Variables: `{{date}}` (yyyy-MM-dd), `{{date:FORMAT}}`
(date-fns tokens), `{{time}}` (HH:mm), `{{title}}`, `{{weekday}}` (localised),
`{{cursor}}` (caret position after insert). `journalTemplate` is applied to new daily notes.
