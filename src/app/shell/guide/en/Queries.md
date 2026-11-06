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
| `sort:` | e.g. `sort:due,-priority`; a minus reverses |
| `group:` | `page`, `due`, `tag`, `status`, `priority`… |
| `limit:` | the maximum number of results |

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
