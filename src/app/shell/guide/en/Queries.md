---
tags: [documentation]
brulion: document
---
# Queries

A block with the language `query` gathers tasks, blocks, pages or reminders from the whole notebook and shows them live. You can tick tasks off right in the results. Here are the pages tagged #documentation, i.e. all pages of this notebook:

```query
type:pages tag:documentation sort:page
```

Insert a query with the `/Query` command or with the “Show as query” button in the [[Tasks|Tasks]] view. By default the result has at most 500 items; add `limit:20` to shorten it.

## Syntax
- Separate conditions with spaces or new lines. **All** conditions must be met; values separated by a comma mean “or” (`status:todo,doing`).
- A minus before a condition inverts it: `-tag:private`, `-buy`.
- `//` starts a comment until the end of the line, and a line starting with `# ` is a comment (for example the query's title).
- Shortcuts: a plain word searches the text, `#work` is `tag:work`, `[[Page]]` is `ref:Page`.
- Keys and values don't depend on letter case or accents, and they also have Polish equivalents (`stan:otwarte termin:jutro` is `status:open due:tomorrow`), so queries from Polish notes work too.
- When you rename a page or move a page or folder, the `tag:`, `ref:`, `[[…]]`, `page:` and `path:` filters that name them change together with them (a name with a space goes into `[[…]]`: `tag:[[My home]]`).

## Main keys
| Key | What it does |
|---|---|
| `type:` | `tasks`, `blocks`, `pages`, `reminders` |
| `status:` | `todo`, `doing`, `done`, `cancelled`, `deferred`, `open` (todo + doing), `closed`, `all` |
| `priority:` | `highest`, `high`, `medium`, `low`, `lowest`, `none` |
| `tag:` | a tag; `project` also catches `project/home` |
| `page:` | a page by title, path or alias; `this` is the page with the query |
| `path:` | the start of a path, e.g. `Projects/` |
| `ref:` | items that link to a page or carry its tag (`ref:Home` also catches `#home`) |
| `due:`, `scheduled:`, `start:`, `done:`, `created:` | a condition on a task's date |
| `remind:` | a condition on a reminder's date |
| `date:` | a condition on a journal date |
| `is:` | `blocked` — the task waits for a task that isn't done (`⛔`), `blocking` — another task waits for it, `running` — its timer is running, `tracked` — it has logged time; `-is:blocked` is the tasks that aren't blocked |
| `sort:` | e.g. `sort:due,-priority`; a minus reverses; also a property: `sort:-cost` |
| `group:` | `page`, `due`, `tag`, `status`, `priority`… or a property: `group:stage` |
| `limit:` | the maximum number of results |
| `key::value` | a property — see below |
| `view:` | `table`, `board`, `gallery`, `calendar`, `list` — see below |
| `columns:`, `summary:` | a view's columns and its sums, averages… — see below |

## Dates in conditions
The operators `<`, `<=`, `>`, `>=` go before the expression; without an operator the date must fall within the given day or period.

- days: `today`, `tomorrow`, `yesterday`, `2026-10-15`,
- offsets: `+7d`, `-2w`, `+1m`, `+1y`,
- periods: `this-week`, `next-week`, `this-month`, `last-month`…,
- a range: `today..+7d`,
- words: `overdue`, `none`, `any`.

Example: `due:<=+7d` means “due within a week, overdue ones included”.

## Examples
- `type:tasks status:open tag:work due:<=+7d sort:due` — open tasks tagged #work for the coming week,
- `remind:this-week` — reminders for this week,
- `type:pages tag:project sort:page` — project pages,
- `ref:this` — blocks that link to this page or carry its tag,
- `type:tasks ref:this status:open group:page` — open tasks related to this page, grouped,
- `status:open -is:blocked sort:due` — what you can do right now (without tasks that are waiting for something).

See also [[Reminders and alarms|Reminders and alarms]]. If there is an error in a query, a description of what to fix appears in place of the results.

## Properties
Two colons filter by a property — of a page (its front matter or `key:: value` lines at the top) or of a block (its own `key:: value` lines, else its parent's, else its page's):

- `status::done`, `status::"in progress"` — the text (letter case and accents don't matter); a list matches when one of its items does,
- `cost::>100`, `cost::<=1200`, `cost::100..200` — numbers as numbers,
- `due::<today`, `due::this-week`, `due::today..+7d` — dates, as in the date conditions above,
- `owner::[[John Smith]]` — a link to that page (an alias works too),
- `urgent::true` — a checkbox,
- `name::*house*` — a pattern,
- `due::none` — no such property (or an empty one), `rating::any` — it has one,
- `-status::done` — everything else; values after a comma mean “or”: `status::planned,"in progress"`.

A key doesn't depend on letter case or accents (`budzet::` finds `Budżet`); one with spaces goes in quotes: `"due date"::2026-11-01`. For pages `sort:` and `group:` take property names too (`type:pages sort:-cost group:status`); `status::` with two colons always means the property, also for tasks.

```text
type:pages tag:project status::"in progress",planned cost::>100 sort:-cost
type:tasks stage::shopping status:open
```

## Views: tables, boards, galleries, calendars
`view:` shows the results differently — and lets you change them in place:

- `view:table` — a table: `columns:` chooses its columns (`title`, `status`, `due`, `tags`, `path`, `mtime`… or any property), and a column `"Gross = cost * 1.23"` is a formula (Dataview's functions: `round`, `dateformat`, `length`…). `summary:cost=sum,rating=average` adds totals under the columns (also `min`, `max`, `median`, `count`, `unique`, `earliest`, `latest`…). Click a cell to change it: Brulion writes that one property into the page (or the `key::` line of a task) and keeps everything else; Ctrl+Z undoes it on this page, and an edit of another page has **Undo** in its message. Clicking a column's header sorts by it; its menu (a right click, a long press) moves or hides it; **Columns** adds one. These change the query's own `sort:` and `columns:`.
- `view:board` — a board: columns by `group:` (tasks by status by default), cards that you drag to another column with the mouse — on a phone after holding a finger on the card. Dropping a card writes the column's value: the task's status, or the property (`group:stage`). **+** in a column adds a page (or a task) with that value.
- `view:gallery` — cards with each page's cover or its first picture.
- `view:calendar` — a month with the rows on their dates (tasks by their due date, pages by a date property or `group:date-property`); drag an entry to another day to move it; the arrows change the month.
- `view:list` — one line per row.

**+** at the top makes a new row: a page in the folder of the query's `path:` with its `tag:` and the values of its `key::value` filters — or, for tasks, a task on this page.

Try it: the board below shows the tasks of this page by status — drag a card to “Done”:

```query
type:tasks page:this view:board
```

- [ ] Read about views
- [/] Try a board
- [ ] Build a table of my projects

```text
type:pages tag:project view:table sort:-cost
columns:title,status,cost,due,"Gross = cost * 1.23"
summary:cost=sum
```

## Obsidian Bases
A `.base` file from Obsidian opens in Brulion as a page with its views (table, cards, list, Kanban board; a map is shown as a table). Its filters (`file.hasTag("book")`, `file.inFolder("Books")`, `status != "done"`), formulas (`price.toFixed(2)`, `if(…)`), display names, sorting, groups, limits and summaries work as in Obsidian. A note shows a base's view where it embeds it — `![[Books.base]]`, or `![[Books.base#Reading]]` for the view of that name — and a code block with the language `base` shows a base's YAML right in a note. Editing a cell or dragging a card writes the note's property, and changing the sort or the columns writes them back into the base — only those lines change. Regular expressions and plugins' functions say they are not supported instead of failing.

## Dataview queries from Obsidian
A vault shared with Obsidian often has queries of the Dataview plugin. Brulion shows their results too: a code block with the language `dataview` lists pages, a table or tasks, read-only — only the tasks can be checked off.

```text
TABLE status, budget AS "Budget" FROM #project WHERE status != "done" SORT budget DESC
```

- `LIST`, `TABLE` (columns with `AS "name"`, `WITHOUT ID` hides the page column), `TASK` and `CALENDAR` (below),
- `FROM #tag`, `FROM "Folder"`, `FROM [[Page]]` (pages linking to it), combined with `AND`, `OR` and `-`,
- `WHERE`, `SORT … DESC`, `GROUP BY`, `FLATTEN`, `LIMIT`,
- fields from the frontmatter and from `key:: value` lines, and `file.name`, `file.mtime`, `file.tags`, `file.tasks`…,
- dates such as `date(today) + dur(7 days)` and Dataview's functions (`contains`, `length`, `round`, `dateformat`…).

`CALENDAR termin` shows a month with a dot for each page on the day of its date (here the `termin` field; `file.day` is a daily note's date). The arrows change the month; tap a day to open its page, or to choose one of its pages.

An **inline query** is inline code that starts with `=`, as in Dataview: this page is called `= this.file.name`, and a week from today is `= date(today) + dur(1 week)`. It shows the value and keeps the query in the file. The same code is also an [[Calculations#Inline calculations|inline calculation]]: when the calculator has a result (`= 52 * 7`), that result is shown; Dataview reads only what the calculator cannot.

Brulion reads these queries itself and never runs scripts: `dataviewjs` blocks and `$= …` queries stay code (they are JavaScript), and functions with regular expressions say they are not supported.
