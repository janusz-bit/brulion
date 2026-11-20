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
| `b.pages({ prop = "habit" })` | only the pages with that property among their own (`habit: every day` in the page's properties); `prop = { "habit", "nawyk" }` takes either, and a `tag` may be added |
| `b.page(name)` | `{ name, path, tags, props, date }`: the page's file in the notebook (`Projects/Home.md`), its own tags and properties, and its day when it is a journal page; `nil` when there is no such page |
| `b.read(name)` | the page's whole text, or `nil` |
| `b.blocks({ page = name })` | the page's blocks from the top, each with the blocks under it |
| `b.blocks({ tag = "idea" })` | every block that carries the tag itself; with `journal = true`, `from = "2026-11-01"` or `to = "2026-11-30"` only blocks of the journal |
| `b.blocks({ prop = "habit" })` | every block with that property under it (`habit:: every day`); with a `tag` the blocks that have both, with a `page` only that page's (also blocks deeper down) |
| `b.search(text, limit)` | what the search view finds: one hit per page, the best first (20 without a `limit`, 200 at most) |
| `b.query(text, limit, offset)` | a query in the language of [[Queries]]: `{ total, items }` (50 items without a `limit`, 1000 at most; `offset` skips that many first, so you can go through any number of them — `total` counts them all). Weeks start on your notebook's first day of the week |
| `b.lines(name)` | the page's lines exactly as written, each `{ text, ref }` — also lines that are no block of their own (a paragraph's second line, a comment): their `ref` goes to `b.replaceLine`, `b.insertLine` and `b.removeLine`; `nil` when there is no such page |
| `b.attachments()` | the notebook's files that are no notes — pictures, PDFs — as paths: `assets/receipt.jpg` |
| `b.revision()` | a number that changes whenever a note changes: what your code worked out from the notes is still right while it stays the same |
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
| `plain` | its words alone: a task without its dates, the tags left out, links as their names — `Pay the rent` |
| `task` | for a task, the character in its checkbox: `" "` open, `"x"` done, `"/"` in progress, `"-"` cancelled, `">"` deferred; `nil` for a block that is no task |
| `page`, `date` | the page it is on, and the day when that is a journal page |
| `tags` | the block's own tags, in lower case, without the hash |
| `props` | its properties: `{ author = "Bolesław Prus" }` for a line `author:: Bolesław Prus` under it |
| `children` | the blocks under it (from `b.blocks` and `ctx.block`) |
| `ref` | where the block is. Give it to `b.replaceBlock` and the other writing functions to change the block; it stops working when the line changes |
| `path`, `line` | from `b.blocks` and `ctx.block`: the page's file and the block's line in it, counted from 1 — `b.ui.link(block.page, nil, block.line)` opens it there |
| `line`, `excerpt` | from `b.search`: the line that matches best — the most of the words you searched for — counted from 1, and the text around it |
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
| `b.appendToPage(name, text)` | the same at the end of any page; a page that does not exist is made |
| `b.write(name, text)` | makes the page, or replaces **all** of its text; `"Meetings/Budget"` makes the folder too |
| `b.replaceBlock(ref, text, task)` | gives a block's first line the new text; with `task` (`"x"`, `" "`, `"/"`, `"-"`) it also sets the checkbox the way a click does (a finished task gets its date). Returns the block's new `ref` |
| `b.insertBlock(ref, text, where)` | puts new lines next to a block: `"after"` it and the blocks under it (without `where`), `"before"` it, or as the last block under it, `"child"` — indented to fit. Returns the first new line's `ref` |
| `b.removeBlock(ref)` | removes the block, and the blocks under it |
| `b.setTask(ref, status)` | only the checkbox, as a click sets it: `"x"` done (with today's date), `" "` open, `"/"` in progress, `"-"` cancelled. Returns the block's `ref` |
| `b.setProperty(ref, key, value)` | the block's property `key:: value`: changed when it is there, added under the block otherwise; `nil` or `""` removes it. Returns the block's `ref` |
| `b.setProperties(ref, { { key, value }, … })` | several properties of the block in one write (new ones in this order). Returns the block's `ref` |
| `b.replaceLine(ref, text)` | the whole line becomes `text`, exactly as given — indentation and all. Returns the line's new `ref` |
| `b.insertLine(ref, text, where)` | puts the lines of `text`, as written, after that line (`"before"`: before it). Returns the first new line's `ref` |
| `b.removeLine(ref)` | removes that one line |

`b.replaceBlock` keeps the bullet, the indentation and the task's dates (wherever they stand on the line) unless the new text has dates of its own. It and the other functions that take a `ref` **refuse** when the line has changed since your code read it — a sync arrived, you typed there — so nothing is ever written over a change the code did not see. The error has `stale` in it; read the block again. Lines added or removed above a block do not matter.

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

```luau
-- A first step under the task at the cursor, already in progress, and a note of when.
b.command("split", function(ctx)
    local block = ctx.block
    if not block or not block.task then
        return "Put the cursor in a task."
    end
    local step = b.insertBlock(block.ref, "- [ ] The first step", "child")
    b.setTask(step, "/")
    b.setProperty(block.ref, "split", b.today())
    return `A first step under “{block.plain}”`
end, { label = "Split the task", block = true })

-- The done tasks of the page on screen move to the page "Archive".
b.command("archive", function(ctx)
    if ctx.page == "" then
        return "Open a page first."
    end
    local moved = 0
    for _, block in b.blocks({ page = ctx.page }) do
        if block.task == "x" and #block.children == 0 then
            b.appendToPage("Archive", `- [x] {block.text} (from [[{ctx.page}]])`)
            b.removeBlock(block.ref)
            moved += 1
        end
    end
    return `{moved} done tasks are in [[Archive]]`
end, { label = "Archive the done tasks" })
```

## Helpers

| Function | What you get |
|---|---|
| `b.today()` | today's date: `2026-11-13` |
| `b.now()` | the date and the time now: `2026-11-13 09:05` |
| `b.daysBetween(a, b)` | how many days from date `a` to date `b` |
| `b.addDays(date, n)`, `b.addMonths(date, n)` | the date `n` days or months later (earlier when `n` is negative): `b.addDays("2026-12-31", 1)` is `2027-01-01`, `b.addMonths("2026-01-31", 1)` is `2026-02-28` |
| `b.weekday(date)` | the day of the week: 1 is Monday, 7 Sunday |
| `b.weekStart()` | the first day of the week in your notebook's settings, counted the same way |
| `b.weekdayName(n, form)`, `b.monthName(n, form)` | the name of a weekday (1–7) or a month (1–12) in the interface's language; `form` is `"full"` (without one), `"short"` (`Mon`) or `"of"` (the Polish form after a day: `listopada`) |
| `b.sorted(list)` | the texts in alphabetical order, Polish letters in their places and `Chapter 2` before `Chapter 10` |
| `b.fold(text)` | the text in lower case and without diacritics (`Żółw` gives `zolw`): for comparing and searching |
| `b.lower(text)` | the text in lower case, diacritics kept (`ŻÓŁW` gives `żółw`, which `string.lower` cannot do) |
| `b.plain(text)` | the words of a line: tags left out, links as their names (`Read [[Books/Dune]] #evening` gives `Read Dune`) |
| `b.parseRecurrence(text)` | how Brulion reads a repeat ("every 2 weeks on Monday", "co miesiąc 15."): `{ every = "week", interval = 2, weekdays = { 1 } }`, or `nil` |
| `b.parseWeekdays(text)` | a list of weekdays, `"mon, wed and fri"` gives `{ 1, 3, 5 }`; `nil` when it is not one |
| `b.formatRecurrence(rule, lang)` | such a repeat in words, in `"en"` or `"pl"`: `every week on Monday, Friday` |
| `b.formatNumber(n, decimals)` | a number as text, with the decimal comma in a Polish notebook: `82,5` |
| `b.toUtc(datetime)`, `b.fromUtc(text)` | a local date and time as the UTC moment other apps write (`2026-11-20 18:30` gives `2026-11-20T17:30:00.000Z` in Poland in November), and back; `b.fromUtc` gives `nil` for anything that is not such a moment |
| `b.lang()`, `b.uiLang()` | the language of the notes and of the interface: `"pl"` or `"en"` |
| `b.log(text)`, `print(…)` | a line in the Luau console |
| `b.status(text)` | says what a command waiting for an answer is doing now ("Reading “Home”…"); the AI chat shows it under the AI's dots, `b.status("")` clears it |
| `b.timer(seconds, label)` | starts the rest timer of the [[Gym]]: a notification when the time is up |
| `b.data(name)` | data that comes with a built-in extension (the gym's list of exercises, the AI assistant's reference for writing extensions); `nil` in your own |

## Commands
`b.command(name, function(ctx) … end, { label = "…", block = true })` puts a command into the palette under its `label`.

- `ctx.page` is the name of the page on screen, an empty text when there is none.
- With `block = true` the command works on the block under the cursor, `ctx.block`, and the palette offers it only in a note. Check that `ctx.block` is there.
- With `palette = false` the palette does not offer it: a view's button (`b.ui.run`) or the app runs it — the AI chat runs the AI assistant's `chat` this way.
- `ctx.args` is what the caller gave the command: a message's button (below), or `brulion-cli extension run … --args '{"page": "Home"}'`; `{}` when nothing was given (write `ctx.args or {}`: a turn of the AI chat has none).
- A text the function returns is shown as a message. Returning nothing is fine. A table `{ message = "…", tone = "success", button = { label = "Open", page = "Home" } }` is a message too: `tone` is `"success"`, `"warning"` or `"error"`, and the button opens a page, runs one of the extension's commands (`command = "…"`, its `args` as `ctx.args`) or — in what a view's action returns — that view's action again (`action = "…"`, `args`): an **Undo**.

## Views
`b.view(name, function(params) … end, actions, { label = "…", icon = "…" })` adds a view. Its function returns what to show, built from the pieces below; with a `label` the view is in the palette. `icon` is the name of one of Brulion's icons, for example `folder`, `star`, `calendar-check`, `chart-column`, `list-checks`.

| Piece | What it shows |
|---|---|
| `b.ui.column({ … })` | the pieces one under another: the usual outermost piece |
| `b.ui.card({ … })` | the pieces inside one card |
| `b.ui.heading(text)` | a heading |
| `b.ui.text(text, muted)` | a paragraph; with `true` as the second argument in a quieter colour, with `{ muted = true, icon = "flame", tone = "success" }` also with an icon before it and in a colour (`"success"`, `"danger"`, `"accent"`) |
| `b.ui.link(page, label, line)` | a link to a page; with a `line` (counted from 1) it opens the page there |
| `b.ui.button(label, action, args, options)` | a button that calls one of the view's actions; `options` may give it a keyboard shortcut (`key = "space"`, `"1"`, `"u"`), make it the main one (`primary = true`), colour it (`tone = "success"`, `"danger"`, `"warning"`, `"info"`), add a second line under its label (`hint = "4 days"`) or an `icon` |
| `b.ui.markdown(text, options)` | Markdown as a note shows it — bold, links that open their pages, highlights, lists; `{ size = "large" }` makes it bigger, `{ muted = true }` quieter |
| `b.ui.progress(value, max, label)` | a progress bar with a caption above it |
| `b.ui.stats({ { label = "Due", value = 12, tone = "accent" }, … })` | numbers side by side, each with its label under it |
| `b.ui.divider()` | a thin line across |
| `b.ui.menu(items, label)` | a "⋯" button with a menu (the items below) |
| `b.ui.row({ … })` | a card with controls side by side: checks, steppers, texts, links, buttons, menus |
| `b.ui.check(checked, action, args, { menu = …, label = … })` | a task's checkbox: `checked` is `true`, `false` or `"skipped"` (crossed out); `menu` opens with a right click or a long press. Only inside a row |
| `b.ui.stepper({ value = 3, step = 1, unit = "kg", action = "set", args = { … } })` | a number with − and +, which can also be typed in; the action gets the new number as `args.value`. Only inside a row |
| `b.ui.table(columns, rows)` | a table: a list of headings and a list of rows, each a list of texts. Best with up to four columns |
| `b.ui.chart({ kind = "line", labels = { … }, series = { { name = "…", values = { … } } } })` | a chart, `"line"` or `"bar"`, a number per label |
| `b.ui.month({ month = "2026-11", days = { ["2026-11-03"] = { state = "done", action = "…", args = { … }, menu = { … } } } })` | a month as a calendar, a square for each day: `"done"` green, `"missed"` red, `"skipped"` crossed out, `"due"` outlined, `"future"` faint, `"rest"` plain; a tap on a day with an `action` calls it, a right click or a long press opens its `menu`. The weeks start on your notebook's first day of the week, today has a ring |
| `b.ui.empty(title, text, icon)` | what to show when there is nothing to show, with an icon of your choice |
| `b.ui.open(label, view, params, options)` | opens another view of the extension, with `params` for its function; with `options` as a button's (`{ primary = true, icon = "play" }`) it looks like a button |
| `b.ui.run(label, command)` | a button that runs one of the extension's commands |

A **menu** is a list of items: `{ label = "…", action = "…", args = { … } }` calls one of the view's actions, `{ label = "…", page = "…", line = 5 }` opens a page, `{ label = "…", view = "…", params = { … } }` opens another view of the extension, `{ label = "…", journal = "2026-11-03" }` opens that day of the journal.

The view's function has to return a piece (a list of pieces is shown as a column): a view that returns nothing says so instead of staying empty. A piece that is not always there can be written `condition and b.ui.text("…") or nil` — a `nil` inside a column or a row is skipped, and a `nil` cell of a table shows empty.

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

```luau
-- The journal's month: days with a note are green, the past ones without one red. Every open
-- task due this month has a menu, and a tick says so with Undo.
b.view("journal-month", function(params)
    local today = b.today()
    local month = params.month or today:sub(1, 7)
    local days, written, day = {}, 0, month .. "-01"
    while day:sub(1, 7) == month do
        local state = if day > today then "future" elseif b.page(day) then "done" else "missed"
        written += if state == "done" then 1 else 0
        days[day] = { state = state, menu = { { label = "Open the journal", journal = day } } }
        day = b.addDays(day, 1)
    end
    local lines = {
        b.ui.heading(`{b.monthName(tonumber(month:sub(6, 7)))} {month:sub(1, 4)}`),
        b.ui.card({
            b.ui.text(`{written} days written`, { icon = "book-open", tone = "accent" }),
            b.ui.month({ month = month, days = days }),
        }),
    }
    local last = b.addDays(b.addMonths(month .. "-01", 1), -1)
    for _, task in b.query(`type:tasks status:open due:{month}-01..{last} sort:due`, 20).items do
        table.insert(lines, b.ui.row({
            b.ui.check(false, "set", { ref = task.ref, status = "x" }, {
                label = task.plain,
                menu = { { label = "Cancel it", action = "set", args = { ref = task.ref, status = "-" } } },
            }),
            b.ui.text(task.plain),
            b.ui.text(task.due, { muted = true, icon = "calendar" }),
            b.ui.menu({ { label = "Open its page", page = task.page } }),
        }))
    end
    return b.ui.column(lines)
end, {
    set = function(args)
        local ref = b.setTask(args.ref, args.status)
        return {
            message = if args.status == "x" then "Done" else "Cancelled",
            tone = "success",
            button = { label = "Undo", action = "set", args = { ref = ref, status = " " } },
        }
    end,
}, { label = "The journal's month", icon = "calendar" })
```

When the function has much to work out, it can keep the result in a variable together with `b.revision()`, and work it out again only when that number changed — the [[Habits]] do so.

## Reactions
`b.on("save", function(e) … end)` runs right after this device saved a page: `e.page` is its name. `b.on("change", function(e) … end)` runs after notes changed anywhere — your typing, a sync, another program — once the changes have settled: `e.pages` is the list of their names (a deleted page is in it too). What extensions write themselves does not come back as a change. A reaction cannot ask a question, and one that fails three times in a row is switched off until the extension is loaded again.

## Questions

| Function | What you get |
|---|---|
| `b.ask(question)` | a line of text you typed; an empty text when you closed the window |
| `b.choose(question, { "A", "B" })` | the option you tapped; `nil` when you closed the window |
| `b.form(title, fields)` | numbers, each with − and +, and lines of text: `{ key = number or text }`; `nil` when you closed the window |

A number field of a form is `{ key = "reps", label = "Reps", value = 8, step = 1, unit = "" }`, a text field `{ key = "name", label = "Name", kind = "text", value = "", placeholder = "e.g. Meditation" }` — the placeholder is the grey example in the empty field. Questions work in commands and in actions, not in views or reactions and not while the page is loading.

```luau
b.command("walk", function(ctx)
    local answer = b.form("A walk", {
        { key = "km", label = "Distance", value = 3, step = 0.5, unit = "km" },
        { key = "min", label = "Time", value = 40, step = 5, unit = "min" },
        { key = "where", label = "Where", kind = "text", value = "", placeholder = "e.g. the park" },
    })
    if answer == nil then
        return nil
    end
    local km, min = b.formatNumber(answer.km), b.formatNumber(answer.min)
    local where = if answer.where ~= "" then ` ({answer.where})` else ""
    b.appendToJournal(b.today(), `- Walk: {km} km in {min} min{where}`)
    return "The walk is in today's journal"
end, { label = "Log a walk" })
```

## AI
`b.ai("question")` asks the model chosen in **Settings → Extensions → AI** and returns its answer as text; a table adds `system`, `model`, `maxTokens`, `effort`, `images` (pictures of the notebook for a model that sees: `b.ai({ prompt = "What does it say?", images = { "receipt.jpg" } })`), `messages` for a conversation and `tools` the model may call. The page needs the permission `ai`. More in [[AI]].

## Luau in short
- Texts are joined with `..`, or written with backticks and braces: `` `{n} pages` ``. A list is `{ "a", "b" }` and counts from 1; `#list` is its length; `for i, item in list do … end` goes through it.
- `local` makes a variable, `function … end` a function, `--` starts a comment, `~=` means "not equal", `nil` means "nothing".
- A text counts its bytes, not its letters: `#"żółw"` is 7. Use `utf8.len(text)` for letters, `b.fold` to compare, `b.sorted` to sort.
- A number is written with a point: `82.5`. Show it with `b.formatNumber`.
- `b.now()` is the date and the time now, `os.date("%H:%M")` the time alone.
- There are no files, no network and no other programs: only `b`.

## Limits
One call may take 2 seconds (the time you spend answering a question does not count), and all extensions together 64 MB of memory; past either, the call is stopped and the rest keeps working. A view shows up to 2000 lines, a row up to 24 controls; `b.blocks` gives up to 20 000 blocks; one request to a model has up to 200 000 characters.

## Errors

| Message | What it means |
|---|---|
| `permission denied: write` | the page has no `permissions: [write]` in its properties |
| `permission denied: ai` | the same for `ai` |
| `stale: the block changed since it was read` | the line is no longer what the code read: read it again |
| `not a block reference` | `b.replaceBlock` (or `b.insertBlock`, `b.removeBlock`, `b.setTask`, `b.setProperty`) got something that is not the `ref` of a block: pass the `ref` as `b.blocks`, `b.query`, `b.search` or `ctx.block` gave it |
| `the task status must be one character` | `b.setTask` or `b.replaceBlock` got `"done"`: write `"x"` |
| `cannot set the property` | a property's name has letters, digits, `-` and `_` only |
| `must be one line` | a block's line or a property's value has a line break |
| `attempt to index nil` | something was not there: a page that does not exist, a block without a task, `ctx.block` outside a block |
| `argument 1 must be a string` | a function got something else than it takes; the number says which argument |
| `invalid date` | a date is not written `2026-11-13`, or there is no such day |
| `the query needs a page, a tag or a prop` | `b.blocks` got none of them |
| `argument 1 must be a table` | `b.pages("project")`: write `b.pages({ tag = "project" })` |
| `the view "…" returned nothing` | a view's function ended without returning what to show |
| `A view cannot wait for a question or for AI` | a view's function asked: ask in a command or an action |
| `a hook cannot ask a question` | a reaction asked |
| `cannot wait for an answer here` | a question or `b.ai` inside a function that `table.sort` or `string.gsub` calls |
| `an extension cannot ask a question while it loads` | a question outside a function: ask inside a command |
| `timeout: the extension ran too long` | one call took more than 2 seconds |

`pcall` catches an error, so the code can go on: `local ok, message = pcall(b.write, name, text)`. `error("…", 0)` stops the command with a message of your own.
