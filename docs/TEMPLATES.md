# Templates

A template is an ordinary Markdown page in the notebook's templates folder (`templates/` by
default). There is no separate editor or format: you write it like any other note, and it syncs
together with the rest of the files. Syntax in brief: [SYNTAX.md](SYNTAX.md#12-templates).

## New template

1. In the sidebar, open the menu of the `templates` folder and choose **New page here**. If the
   folder does not exist yet, create it.
2. Name the page the way the template should be called, e.g. `Meeting`.
3. Type the content, optionally with [variables](#variables):

   ```markdown
   # {{title}}

   Date: {{date}} ({{weekday}}), {{time}}

   ## Participants
   -

   ## Decisions
   -
   ```

Templates can be arranged in subfolders (`templates/work/Client.md`) — they appear in the list
too.

## Inserting a template

In the editor type `/template` (the Polish `/szablon` works too), choose **Template…**, and then
a template from the list. The list has a search field and shows all pages from the templates
folder, the journal template included.

Where the text goes:

* from an empty block — it replaces that block;
* from a block with content — below it, after the whole top-level block with its children.

The cursor lands on the first inserted block. You undo the insertion like any other change.

There is no way yet to create a new page from a chosen template. Create the page and insert the
template into an empty block.

## Variables

| Variable | What it inserts | Example |
|---|---|---|
| `{{date}}` | date | `2026-10-14` |
| `{{date:FORMAT}}` | date and time in your own [format](#date-and-time-format) | `{{date:dd.MM.yyyy}}` → `14.10.2026` |
| `{{time}}` | time | `09:05` |
| `{{time:FORMAT}}` | the same as `{{date:FORMAT}}` | `{{time:HH:mm:ss}}` → `09:05:00` |
| `{{title}}` | title of the page you insert into | `Bathroom renovation` |
| `{{weekday}}` | day of the week | `Wednesday` (Polish: `środa`) |
| `{{month}}` | month name (in Polish the nominative form) | `October` (Polish: `październik`) |
| `{{week}}` | ISO week number | `42` |
| `{{end}}` | last day of the period in a [weekly or monthly note](#weekly-and-monthly-note-templates), elsewhere the same as `{{date}}` | `2026-10-18` |
| `{{end:FORMAT}}` | the same in your own format | `{{end:d MMMM yyyy}}` → `18 October 2026` (Polish: `18 października 2026`) |
| `{{cursor}}` | nothing — see below | |

* Case does not matter, and spaces are allowed inside the braces: `{{ Title }}` works like
  `{{title}}`.
* An unknown variable stays in the text as written (`{{project}}`).
* Day and month names are in the app's language. In `brulion-cli` the `--lang` option chooses it.
* `{{date}}` and `{{time}}` are the moment of insertion, also when you insert a template into
  yesterday's daily note. Only the [journal template](#journal-template) is filled with the date of
  its own day, and the [weekly and monthly templates](#weekly-and-monthly-note-templates) with
  their own period.
* `{{month}}` is the name for a heading ("październik 2026" in Polish). `{{date:MMMM}}` gives the
  genitive ("14 października"), right in a sentence with a day. In English both are "October".

**`{{cursor}}` does not set the cursor yet.** The variable is removed from the text, and the
cursor lands where described under [inserting](#inserting-a-template) and under the
[journal template](#journal-template).

### Date and time format

The format is a [Qt date pattern](https://doc.qt.io/qt-6/qdatetime.html#toString):

| Pattern | Meaning | Example |
|---|---|---|
| `yyyy`, `yy` | year | `2026`, `26` |
| `MM`, `M` | month number | `03`, `3` |
| `MMMM`, `MMM` | month name | `October`, `Oct` (Polish: `października`, `paź`) |
| `dd`, `d` | day of the month | `04`, `4` |
| `dddd`, `ddd` | day of the week | `Wednesday`, `Wed` (Polish: `środa`, `śr.`) |
| `HH`, `mm`, `ss` | hour, minute, second | `09`, `05`, `00` |
| `'text'` | literal text | `{{date:yyyy-MM-dd'T'HH:mm}}` → `2026-10-14T09:05` |

`{{date:dddd, d MMMM yyyy}}` gives `Wednesday, 14 October 2026` (Polish: `środa, 14 października
2026`). The format cannot contain the `}` character.

## Journal template

New daily notes start from the file `templates/journal.md`. Just create it:

```markdown
## {{weekday}}, {{date:dd.MM.yyyy}}

### Today
- [ ] Review tasks

### Notes
- 
```

* **The file is created on the first change.** A day without a note only shows the template as
  starting text, so just looking at a day creates nothing.
* **The date is the note's day.** `{{date}}` in yesterday's note is yesterday, and `{{time}}` is
  the time it was created. `{{title}}` is the file name, e.g. `2026-10-14`.
* **The cursor** lands on the first empty list item. When there is none, a new empty block is
  added at the end.
* **An empty item at the end.** End the template with an empty `- `, as above. Quick capture,
  sharing, Android widgets and `brulion-cli journal append` fill that item instead of adding a new
  one below it.
* **Note mode.** The template can set it in the header (`brulion: document` or `outline`,
  [SYNTAX.md](SYNTAX.md)).
* **No file.** When the template file does not exist or the field in the settings is empty, new
  days start from an empty page.

## Weekly and monthly note templates

Week notes (`journals/2026-W45.md`) and month notes (`journals/2026-11.md`) start from the files
`templates/weekly.md` and `templates/monthly.md`. They work like the journal template: the note is
created on the first change, and looking at a week or month creates nothing.

* **`{{date}}` is the first day of the period** — the Monday of the week or the 1st of the month —
  and **`{{end}}`** the last one: Sunday or the last day of the month. `{{title}}` is the file name
  (`2026-W45`), `{{week}}` the week number, `{{month}}` the month name.
* **Weeks are counted by ISO**, from Monday, even when the settings start the week on Sunday. At
  the turn of the year the number belongs to the week's year: 1 January 2027 is week 53 of 2026.

A week example:

```markdown
# Week {{week}}: {{date:d MMMM}} – {{end:d MMMM yyyy}}

## Goals
- 

## Summary
- 
```

and a month:

```markdown
# {{month}} {{date:yyyy}}

## Highlights
- 
```

You open them with **Ctrl+Alt+W** and **Ctrl+Alt+M**, from the command palette or from the
calendar (the week number, the month name in the header). You change the folder, file names and
templates in **Settings → Notebook** (`weeklyFolder`, `weeklyFileFormat`, `weeklyTemplate`,
`monthlyFolder`, `monthlyFileFormat`, `monthlyTemplate` in `.brulion/config.json`); format
details: [SYNTAX.md](SYNTAX.md#weekly-and-monthly-notes).

## Things to watch out for

* **Tasks and reminders in a template are visible like any others.** A template's content is
  indexed like an ordinary note: `- [ ] Review tasks` appears in the task list, and
  `⏰ 2026-11-05 09:00` in reminders. `⏰ {{date}} 09:00` is not a reminder in the template; it
  becomes one only after insertion. On the other hand, `- [ ] Something 📅 {{date}}` in a template
  is already a task, just without a due date.
* **Search covers templates.** Their text, with the variables, shows up in the results.
* **The YAML header works only at the top of a page.** A template starting with `---` inserted in
  the middle of a note does not set its properties. It works on an empty page and in the journal
  template.
* **Graph and Clean up.** The graph hides templates by default (the **Templates** toggle), and
  Clean up never suggests deleting them — nor weekly and monthly notes.

## Another folder or file

**Settings → Notebook** has the fields **Templates folder**, **Journal template**, **Weekly note
template** and **Monthly note template**. They are saved in `.brulion/config.json`
(`templatesFolder`, `journalTemplate`, `weeklyTemplate`, `monthlyTemplate`), so they apply on all
devices. When you move or rename the templates folder or a template file in Brulion, the settings
follow it.

An Obsidian vault without `.brulion/config.json` uses Obsidian's settings:
[IMPORT.md](IMPORT.md#obsidian-folder-without-importing).
