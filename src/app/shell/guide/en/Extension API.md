---
tags: [documentation]
aliases: [Luau API, Scripting API]
brulion: document
---
# Extension API

Everything the code of an [[Extensions|extension]] can do with your notebook is a function of one table, `b`. This page lists them all; [[Extension recipes]] shows them at work. To try one, open the **Luau console** (the palette) and type it — `b.today()`, `#b.pages()` — or put the cursor in an example below and choose **Run Luau block**: what the example returns shows as a message, and a command or view it defines is in the palette until the extensions are loaded again.

## Words used here
- A **page** is named as in a link: its title, an alias or a path (`Projects/Home`). A day of the journal is named by its date: `2026-11-13`.
- A **date** is a text like `2026-11-13`; with a time, `2026-11-13 09:00`.
- What is not there is `nil`: `b.read` of a page that does not exist. A mistake — a wrong argument, a missing permission, a line that changed in the meantime — is an **error**: the command stops and says why (see Errors at the end).
- What a function gives you is a copy. Changing it changes nothing in your notes.

## Reading
Reading needs no permission.

| Function | What you get |
|---|---|
| `b.pages()` | the names of all pages, the journal's days too, in alphabetical order |
| `b.pages({ tag = "project" })` | only the pages with that tag among their own tags (`tags: [project]` in the page's properties) |
| `b.page(name)` | `{ name, tags, props, date }`: the page's own tags and properties, and its day when it is a journal page; `nil` when there is no such page |
| `b.read(name)` | the page's whole text, or `nil` |
| `b.blocks({ page = name })` | the page's blocks from the top, each with the blocks under it |
| `b.blocks({ tag = "idea" })` | every block that carries the tag itself; with `journal = true`, `from = "2026-11-01"` or `to = "2026-11-30"` only blocks of the journal |
| `b.search(text, limit)` | what the search view finds: one hit per page, the best first (20 without a `limit`, 200 at most) |
| `b.query(text, limit)` | a query in the language of [[Queries]]: `{ total, items }` (50 items without a `limit`, 1000 at most) |
| `b.checkExtension(name, text)` | tries `text` as the extension page `name` without saving it or asking you: `{ ok, commands, views, hooks, … }`, or `ok = false` with the `error` and the page's `line`. It writes nothing; this is how the AI assistant checks an extension before it writes one |

```luau
local soon = b.query("type:tasks status:open due:<=+7d sort:due")
local lines = { soon.total .. " tasks are due within a week" }
for _, task in soon.items do
    table.insert(lines, task.due .. "  " .. task.text)
end
return table.concat(lines, "\n")
```

`b.blocks({ tag = … })` finds the blocks that carry the tag themselves. A query's `tag:` also finds what is under a tagged block or on a tagged page, and `tag:project` catches `project/home` too.

## A block
A block is a list item, a paragraph or a heading. `b.blocks`, `b.search`, `b.query` and `ctx.block` describe one the same way:

| Field | What it is |
|---|---|
| `text` | the block's first line as you wrote it, without the bullet and the checkbox; tags, dates and links are still in it |
| `task` | for a task, the character in its checkbox: `" "` open, `"x"` done, `"/"` in progress, `"-"` cancelled, `">"` deferred; `nil` for a block that is no task |
| `page`, `date` | the page it is on, and the day when that is a journal page |
| `tags` | the block's own tags, in lower case, without the hash |
| `props` | its properties: `{ author = "Bolesław Prus" }` for a line `author:: Bolesław Prus` under it |
| `children` | the blocks under it (from `b.blocks` and `ctx.block`) |
| `ref` | where the block is. Give it to `b.replaceBlock` to change the block; it stops working when the line changes |
| `line`, `excerpt` | from `b.search`: the line of the first match, counted from 1, and the text around it |
| `due`, `scheduled`, `start`, `reminder` | from `b.query`: a task's dates and a reminder's time |

```luau
-- The page "Tasks" as an outline: every block, the blocks under it indented.
local lines = {}
local function show(blocks, indent)
    for _, block in blocks do
        table.insert(lines, indent .. block.text)
        show(block.children, indent .. "  ")
    end
end
show(b.blocks({ page = "Tasks" }), "")
return table.concat(lines, "\n")
```

## Writing
To change notes, the extension's page needs `permissions: [write]` in its properties. The console may always write.

| Function | What it does |
|---|---|
| `b.appendToJournal(date, text)` | adds the text at the end of that day's note; a missing note is made from your journal template. The text goes in as written: begin an item with `- ` |
| `b.write(name, text)` | makes the page, or replaces **all** of its text; `"Meetings/Budget"` makes the folder too |
| `b.replaceBlock(ref, text, task)` | gives a block's first line the new text; with `task` (`"x"`, `" "`, `"/"`, `"-"`) it also sets the checkbox the way a click does (a finished task gets its date). Returns the block's new `ref` |

`b.replaceBlock` keeps the bullet, the indentation and the dates at the end of the line. It **refuses** when the line has changed since your code read it — a sync arrived, you typed there — so nothing is ever written over a change the code did not see. The error has `stale` in it; read the block again.

```luau
b.command("done", function(ctx)
    local block = ctx.block
    if not block or not block.task then
        return "Put the cursor in a task."
    end
    b.replaceBlock(block.ref, block.text, "x")
    return "Done: " .. block.text
end, { label = "Mark the task done", block = true })
```

## Helpers

| Function | What you get |
|---|---|
| `b.today()` | today's date: `2026-11-13` |
| `b.daysBetween(a, b)` | how many days from date `a` to date `b` |
| `b.sorted(list)` | the texts in alphabetical order, Polish letters in their places and `Chapter 2` before `Chapter 10` |
| `b.fold(text)` | the text in lower case and without diacritics (`Żółw` gives `zolw`): for comparing and searching |
| `b.formatNumber(n, decimals)` | a number as text, with the decimal comma in a Polish notebook: `82,5` |
| `b.lang()`, `b.uiLang()` | the language of the notes and of the interface: `"pl"` or `"en"` |
| `b.log(text)`, `print(…)` | a line in the Luau console |
| `b.timer(seconds, label)` | starts the rest timer of the [[Gym]]: a notification when the time is up |
| `b.data(name)` | data that comes with a built-in extension (the gym's list of exercises, the AI assistant's reference for writing extensions); `nil` in your own |

## Commands
`b.command(name, function(ctx) … end, { label = "…", block = true })` puts a command into the palette under its `label`.

- `ctx.page` is the name of the page on screen, an empty text when there is none.
- With `block = true` the command works on the block under the cursor, `ctx.block`, and the palette offers it only in a note. Check that `ctx.block` is there.
- A text the function returns is shown as a message. Returning nothing is fine.

## Views
`b.view(name, function(params) … end, actions, { label = "…", icon = "…" })` adds a view. Its function returns what to show, built from the pieces below; with a `label` the view is in the palette. `icon` is the name of one of Brulion's icons, for example `folder`, `star`, `calendar-check`, `chart-column`, `list-checks`.

| Piece | What it shows |
|---|---|
| `b.ui.column({ … })` | the pieces one under another: the usual outermost piece |
| `b.ui.heading(text)` | a heading |
| `b.ui.text(text, muted)` | a paragraph; with `true` as the second argument in a quieter colour |
| `b.ui.link(page, label)` | a link to a page |
| `b.ui.button(label, action, args)` | a button that calls one of the view's actions |
| `b.ui.row({ … })` | a card with controls side by side: checks, steppers, texts, links, buttons |
| `b.ui.check(checked, action, args)` | a task's checkbox. Only inside a row |
| `b.ui.stepper({ value = 3, step = 1, unit = "kg", action = "set", args = { … } })` | a number with − and +; the action gets the new number as `args.value`. Only inside a row |
| `b.ui.table(columns, rows)` | a table: a list of headings and a list of rows, each a list of texts. Best with up to four columns |
| `b.ui.chart({ kind = "line", labels = { … }, series = { { name = "…", values = { … } } } })` | a chart, `"line"` or `"bar"`, a number per label |
| `b.ui.empty(title, text)` | what to show when there is nothing to show |
| `b.ui.open(label, view, params)` | opens another view of the extension, with `params` for its function |
| `b.ui.run(label, command)` | a button that runs one of the extension's commands |

**Actions** are the functions in the third argument, by name. A button, a check or a stepper names one and carries `args` for it — put there what the action needs, such as a block's `ref` and `text`. The view is drawn again after every action and whenever a note changes, so its function should read the notebook each time and never ask or write. The view below has an action that writes, so its page needs `permissions: [write]`.

```luau
b.view("ideas", function(params)
    local rows = { b.ui.heading("Ideas") }
    for _, block in b.blocks({ tag = "idea" }) do
        table.insert(rows, b.ui.row({
            b.ui.text(block.text),
            b.ui.link(block.page),
            b.ui.button("To today", "copy", { text = block.text, page = block.page }),
        }))
    end
    if #rows == 1 then
        return b.ui.empty("No ideas yet", "Tag a line with the idea tag.")
    end
    return b.ui.column(rows)
end, {
    copy = function(args)
        b.appendToJournal(b.today(), `- {args.text} (from [[{args.page}]])`)
    end,
}, { label = "Ideas", icon = "star" })
```

## Reactions
`b.on("save", function(e) … end)` runs right after this device saved a page: `e.page` is its name. `b.on("change", function(e) … end)` runs after notes changed anywhere — your typing, a sync, another program — once the changes have settled: `e.pages` is the list of their names (a deleted page is in it too). What extensions write themselves does not come back as a change. A reaction cannot ask a question, and one that fails three times in a row is switched off until the extension is loaded again.

## Questions

| Function | What you get |
|---|---|
| `b.ask(question)` | a line of text you typed; an empty text when you closed the window |
| `b.choose(question, { "A", "B" })` | the option you tapped; `nil` when you closed the window |
| `b.form(title, fields)` | numbers, each with − and +: `{ key = number }`; `nil` when you closed the window |

A field of a form is `{ key = "reps", label = "Reps", value = 8, step = 1, unit = "" }`. Questions work in commands and in actions, not in views or reactions and not while the page is loading.

```luau
b.command("walk", function(ctx)
    local answer = b.form("A walk", {
        { key = "km", label = "Distance", value = 3, step = 0.5, unit = "km" },
        { key = "min", label = "Time", value = 40, step = 5, unit = "min" },
    })
    if answer == nil then
        return nil
    end
    local km, min = b.formatNumber(answer.km), b.formatNumber(answer.min)
    b.appendToJournal(b.today(), `- Walk: {km} km in {min} min`)
    return "The walk is in today's journal"
end, { label = "Log a walk" })
```

## AI
`b.ai("question")` asks the model chosen in **Settings → Extensions → AI** and returns its answer as text; a table adds `system`, `model`, `maxTokens`, `effort`, `messages` for a conversation and `tools` the model may call. The page needs the permission `ai`. More in [[AI]].

## Luau in short
- Texts are joined with `..`, or written with backticks and braces: `` `{n} pages` ``. A list is `{ "a", "b" }` and counts from 1; `#list` is its length; `for i, item in list do … end` goes through it.
- `local` makes a variable, `function … end` a function, `--` starts a comment, `~=` means "not equal", `nil` means "nothing".
- A text counts its bytes, not its letters: `#"żółw"` is 7. Use `utf8.len(text)` for letters, `b.fold` to compare, `b.sorted` to sort.
- A number is written with a point: `82.5`. Show it with `b.formatNumber`.
- `os.date("%H:%M")` is the time now.
- There are no files, no network and no other programs: only `b`.

## Limits
One call may take 2 seconds (the time you spend answering a question does not count), and all extensions together 64 MB of memory; past either, the call is stopped and the rest keeps working. A view shows up to 2000 lines, a row up to 24 controls; `b.blocks` gives up to 20 000 blocks; one request to a model has up to 200 000 characters.

## Errors

| Message | What it means |
|---|---|
| `permission denied: write` | the page has no `permissions: [write]` in its properties |
| `permission denied: ai` | the same for `ai` |
| `stale: the block changed since it was read` | the line is no longer what the code read: read it again |
| `attempt to index nil` | something was not there: a page that does not exist, a block without a task, `ctx.block` outside a block |
| `argument 1 must be a string` | a function got something else than it takes; the number says which argument |
| `invalid date` | a date is not written `2026-11-13`, or there is no such day |
| `the query needs a page or a tag` | `b.blocks` got neither |
| `A view cannot wait for a question or for AI` | a view's function asked: ask in a command or an action |
| `a hook cannot ask a question` | a reaction asked |
| `cannot wait for an answer here` | a question or `b.ai` inside a function that `table.sort` or `string.gsub` calls |
| `an extension cannot ask a question while it loads` | a question outside a function: ask inside a command |
| `timeout: the extension ran too long` | one call took more than 2 seconds |

`pcall` catches an error, so the code can go on: `local ok, message = pcall(b.write, name, text)`. `error("…", 0)` stops the command with a message of your own.
