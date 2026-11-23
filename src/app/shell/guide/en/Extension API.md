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
| `b.pages({ paths = true })` | the same pages as their files in the notebook (`Projects/Home.md`): two pages called "Home" in different folders are one name in the list of names, two paths here. Every function that takes a page takes such a path too |
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
| `b.timer(seconds, { label = "Tea", message = "The tea is ready." })` | starts the extension's own timer (below, Timers) |
| `b.stopTimer()` | stops it |
| `b.data(name)`, `b.data(name, key)` | data that comes with a built-in extension (the gym's catalog of exercises, the AI assistant's reference for writing extensions); with `key` one entry of it (the gym's instructions of one exercise: `b.data("instructions", "back-squat")`); `nil` in your own |

## Commands
`b.command(name, function(ctx) … end, { label = "…", block = true })` puts a command into the palette under its `label`.

- `ctx.page` is the name of the page on screen, an empty text when there is none.
- With `block = true` the command works on the block under the cursor, `ctx.block`, and the palette offers it only in a note. Check that `ctx.block` is there.
- With `palette = false` the palette does not offer it: a view's button (`b.ui.run`) or the app runs it — the AI chat runs the AI assistant's `chat` this way.
- `ctx.selection` is what you selected in the note before choosing the command: the text selected in the block you are editing (or in the Markdown source), or the selected blocks as Markdown — what the AI in the editor works on too; an empty text when nothing is.
- With `key = "Ctrl+Alt+W"` the command suggests a keyboard shortcut; it gets it only while none of Brulion's own commands has it. Every command of an extension is in **Settings → Keyboard shortcuts**, where you can give it any shortcut.
- With `chat = true` it is a **chat partner**: the AI chat offers it as who answers a conversation. It gets `ctx.messages` (the conversation, the user's last; pictures as `images`) and `ctx.omitted` (how many first messages did not fit the model), and what it returns is its message in the conversation. More in [[AI]].
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
| `b.ui.stats({ { label = "Due", value = 12, tone = "accent" }, … })` | numbers side by side, each with its label under it |
| `b.ui.divider()` | a thin line across |
| `b.ui.menu(items, label)` | a "⋯" button with a menu (the items below) |
| `b.ui.row({ … })` | a card with controls side by side: checks, steppers, texts, links, buttons, menus |
| `b.ui.check(checked, action, args, { menu = …, label = … })` | a task's checkbox: `checked` is `true`, `false` or `"skipped"` (crossed out); `menu` opens with a right click or a long press. Only inside a row |
| `b.ui.stepper({ value = 3, step = 1, unit = "kg", action = "set", args = { … } })` | a number with − and +, which can also be typed in; the action gets the new number as `args.value`. Only inside a row |
| `b.ui.table(columns, rows)` | a table: a list of headings and a list of rows, each a list of texts. Best with up to four columns |
| `b.ui.chart({ kind = "line", labels = { … }, series = { { name = "…", values = { … } } } })` | a chart, `"line"` or `"bar"`, a number per label |
| `b.ui.month({ month = "2026-11", days = { ["2026-11-03"] = { state = "done", action = "…", args = { … }, menu = { … } } } })` | a month as a calendar, a square for each day: `"done"` green, `"missed"` red, `"skipped"` crossed out, `"due"` outlined, `"future"` faint, `"rest"` plain; a tap on a day with an `action` calls it, a right click or a long press opens its `menu`. The weeks start on your notebook's first day of the week, today has a ring |
| `b.ui.heatmap({ columns = { "3.11", "10.11" }, rows = { { label = "Chest", values = { 4200, 3900 }, texts = { … }, note = "3900 kg" } } })` | a grid of squares, a row per thing and a column per period (the gym's volume per muscle and week), each square stronger the bigger its value against the row's largest (`scale = "all"`: the whole grid's); `nil` or 0 is an empty square. `texts` are what a square says when you point at it or tap it, `note` a short text at the row's end |
| `b.ui.pictures({ "assets/cat.jpg", { src = "https://example.com/dog.jpg", alt = "A dog on a meadow", caption = "Rex" } }, { label = "Pets" })` | pictures side by side (up to eight): a picture of your notebook by its path, as `b.attachments()` names it, or one from the internet by its address. Those from the internet show as in notes — from this device's cache, downloaded once you tap **Show the pictures** or with "Show pictures from the internet without asking" on. `alt` describes a picture for a screen reader, `caption` goes under it |
| `b.ui.empty(title, text, icon)` | what to show when there is nothing to show, with an icon of your choice |
| `b.ui.open(label, view, params, options)` | opens another view of the extension, with `params` for its function; with `options` as a button's (`{ primary = true, icon = "play" }`) it looks like a button |
| `b.ui.run(label, command)` | a button that runs one of the extension's commands |
| `b.ui.input({ placeholder = "…", label = "…", action = "add", submit = "Add" })` | a text field with a button: Enter or the button gives the action what was typed as `args.value`, then the field empties (`keep = true` keeps it). `multiline = true` makes it several lines: Ctrl+Enter or the button |
| `b.ui.select({ options = { "A", { label = "B", value = "b" } }, value = "b", action = "pick" })` | a button with the chosen option; its menu lists them all, and a choice gives the action the option's `value` |
| `b.ui.slider({ value = 3, min = 1, max = 5, step = 1, unit = "", action = "set" })` | a number picked on a line, shown beside it; the action gets it as `args.value` when you let go (or press an arrow key) |
| `b.ui.date({ value = "2026-11-21", action = "move" })` | a day on a button that opens a calendar; the action gets the day picked — or `""` for "No date" |
| `b.ui.toggle(on, action, args, { label = "…" })` | a switch; a tap gives the action `args.value`, `true` or `false` |
| `b.ui.progress(value, max, label)` or `b.ui.progress({ value = 3, max = 4, label = "3 of 4", tone = "success" })` | a progress bar filled to `value` of `max` (1 if you leave it out), with its label above it; a `tone` (`"success"`, `"danger"`, `"warning"`) colours the label and the bar |

The last six can stand in a row or on a line of their own, which they then fill.

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

The view below takes what you type and choose: a slider, a choice, a note, a day and a switch. With `panel = "journal"` it is also a section of the side panel beside every day of the journal.

```luau
local mood = { level = 3, weather = "sun", walked = false }

b.view("mood", function(params)
    return b.ui.column({
        b.ui.heading(`How is {params.date or b.today()}?`),
        b.ui.slider({ value = mood.level, min = 1, max = 5, step = 1, label = "Mood", action = "level" }),
        b.ui.select({
            options = { { label = "Sun", value = "sun" }, { label = "Rain", value = "rain" }, "Snow" },
            value = mood.weather, label = "Weather", action = "weather",
        }),
        b.ui.input({ placeholder = "What made it so?", action = "note", submit = "Write it down" }),
        b.ui.row({
            b.ui.date({ value = params.date or b.today(), label = "Day", action = "day" }),
            b.ui.toggle(mood.walked, "walked", nil, { label = "A walk" }),
        }),
        b.ui.progress({ value = mood.level, max = 5, label = `{mood.level} of 5`, tone = "accent" }),
    })
end, {
    level = function(args) mood.level = args.value end,
    weather = function(args) mood.weather = args.value end,
    walked = function(args) mood.walked = args.value end,
    day = function(args) mood.day = args.value end,
    note = function(args)
        local walk = if mood.walked then ", a walk" else ""
        b.appendToJournal(mood.day or b.today(), `- Mood {mood.level}/5, {mood.weather}{walk}: {args.value}`)
        return "Noted"
    end,
}, { label = "Mood", icon = "heart", panel = "journal" })
```

## Views in a page and in the side panel
A view can be drawn **inside a page**: write a `view` code block whose first line names it — the extension's id, a slash, the view's name — and whose next lines are its params, `name: value`. The slash menu offers every view with a label as **View: …**, which writes the block for you. The view works there as on its own: its buttons, checks and fields act, and it follows your notes. It also gets the page's `page`, `path` and `date` among its params. A click beside its controls edits the block's text.

````markdown
```view
docs/mood
```
````

`panel = true` in a view's last argument makes it a section of the **side panel** beside every page and day; `panel = "page"` beside pages only, `panel = "journal"` beside days of the journal only. On a phone the side panel is the drawer from the right edge. `panel = "sidebar"` puts it into the **pages panel** on the left instead, under Favorites and Recent.

## Macros
`b.macro(name, function(ctx) … end, { description = "…", args = { "…" }, example = "…" })` gives your notes a **macro**: writing `{{ext:<extension>/<name> arguments}}` anywhere in a note shows what the function returns, in the text, always up to date. The extension is named by its id (its page's title in small letters, without Polish letters, with `-` for spaces: "Word count" is `word-count`). Typing `{{` in a note offers the macros with their descriptions.

- `ctx.args` is the list of arguments, separated by commas: `{{ext:a/b Squat, 30}}` gives `{ "Squat", "30" }`; `ctx.text` is them as written.
- `ctx.page` and `ctx.path` name the note the macro is in, `ctx.date` its day when it is a day of the journal.
- Return a text, a number, `b.ui.text` (muted, or in a colour), `b.ui.link`, `b.ui.heading` (bold) — or a list of them. It is always shown as text, never as HTML.
- A macro is computed again by itself when a note it read changes, and when one of its extension's settings does. It may not ask a question nor change anything — no writes, no settings set, no timers —, and it has a quarter of a second.
- A macro of an extension that has not run on this device shows a lock: a click shows the extension's code, to read before you run it.

```luau
b.macro("words", function(ctx)
    local name = ctx.args[1] or ctx.page
    local n = 0
    for _ in (b.read(name) or ""):gmatch("%S+") do
        n += 1
    end
    return { b.ui.heading(tostring(n)), " words in ", b.ui.link(name) }
end, { description = "How many words a page has", args = { "page" }, example = "Dom" })
```

## Template variables
`b.variable(name, function(ctx) … end, { description = "…" })` gives your [[Journal, calendar and templates|templates]] a variable: `{{<extension>/<name>}}` in a template is replaced by what the function returns when the template is used — Markdown, as many lines as you like. `ctx.date` is the day the template is filled for. Unlike a macro the value is written into the note once.

```luau
b.variable("agenda", function(ctx)
    local day = ctx.date or b.today()
    local lines = {}
    for _, task in b.query("type:tasks status:open sort:due", 50).items do
        if task.due and task.due <= day then
            table.insert(lines, `- [ ] {task.plain} ([[{task.page}]])`)
        end
    end
    return if #lines > 0 then table.concat(lines, "\n") else "- Nothing due"
end, { description = "Open tasks due by the note's day" })
```

## Reactions
`b.on(event, function(e) … end)` runs the function when something happens:

| Event | When | `e` |
|---|---|---|
| `"save"` | right after this device saved a page | `page`, `path` |
| `"change"` | after notes changed anywhere — your typing, a sync, another program, **another extension** — once the changes have settled | `pages` (their names; a deleted page too), `paths`, `by` (the other extensions that wrote), `sources` (`"app"`, `"extension"`, `"sync"`, `"other"`) |
| `"task"` | a task was ticked off, opened again, cancelled or put in progress | the task as a block, with `task` (its status now), `previous`, `source` and `by` |
| `"journal"` | a day's note was made | `date`, `page`, `path`, `source`, `by` |
| `"open"` | the notebook was opened (once, after the extensions loaded) | `date` |
| `"day"` | a new day began while Brulion runs | `date`, `previous` |
| `"timer"` | the extension's own timer ended | `label`, `seconds` |
| `"route"` | the screen shows something else: a page, a day, a view | `view`, `page`, `path`, `date`, `previous` |

A reaction never hears what its own extension wrote — so it never answers itself — but it hears every other extension: a task ticked in one extension's view reaches the page another one keeps. Extensions answering each other stop after four rounds. A sync brings what another device's extensions wrote, with `source = "sync"`: a reaction that adds something for every event should skip those, or the line comes once from each device. A reaction cannot ask a question, and one that fails three times in a row is switched off until the extension is loaded again.

```luau
-- A line in today's journal for every task done on this device.
b.on("task", function(e)
    if e.task == "x" and e.source ~= "sync" then
        b.appendToJournal(b.today(), `- Done: {e.plain} ([[{e.page}]])`)
    end
end)

-- A new day's note gets a line to start from.
b.on("journal", function(e)
    if e.source ~= "sync" then
        b.appendToPage(e.path, "- Three things for today:")
    end
end)
```

## Questions

| Function | What you get |
|---|---|
| `b.ask(question)` | a line of text you typed; an empty text when you closed the window |
| `b.choose(question, { "A", "B" })` | the option you tapped; `nil` when you closed the window |
| `b.form(title, fields)` | numbers, each with − and +, and lines of text: `{ key = number or text }`; `nil` when you closed the window |

A number field of a form is `{ key = "reps", label = "Reps", value = 8, step = 1, unit = "" }`, a text field `{ key = "name", label = "Name", kind = "text", value = "", placeholder = "e.g. Meditation" }` — the placeholder is the grey example in the empty field. Questions work in commands and in actions, not in views or reactions and not while the page is loading. A dialog shows at most the first 10 000 characters of a question's text; `b.choose` takes up to 500 options and `b.form` up to 50 fields.

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

## Settings
`b.settings({ … })` gives the extension settings of its own: **Settings → Extensions** (and the corner of its page) then has **Settings** for it, where you change them. `b.setting(key)` reads one; `b.setSetting(key, value)` changes one from the code. They are kept on this device, not in the notebook: a phone and a computer may each have their own.

| A setting | What it is |
|---|---|
| `{ key = "budget", label = "Monthly budget", value = 2000, step = 100, unit = "zł" }` | a number with − and +, which can also be typed in |
| `{ key = "currency", label = "Currency", value = "zł" }` | a line of text |
| `{ key = "cents", label = "Show the cents", value = false }` | a switch |
| `{ key = "period", label = "Period", options = { "month", "week" } }` | one of the options |

`value` is what it is until you change it, `description = "…"` a sentence under it. The [[Gym]] has one too: the rest between sets.

```luau
b.settings({
    { key = "glass", label = "A glass", value = 250, step = 50, unit = "ml" },
    { key = "goal", label = "A day's goal", value = 2000, step = 250, unit = "ml" },
})

b.command("water", function(ctx)
    local glass, goal = b.setting("glass"), b.setting("goal")
    b.appendToJournal(b.today(), `- Water: {glass} ml`)
    local drunk = 0
    for _, block in b.blocks({ page = b.today() }) do
        drunk += tonumber(block.text:match("^Water: (%d+) ml")) or 0
    end
    return `{drunk} of {goal} ml today`
end, { label = "A glass of water" })
```

## Timers
`b.timer(seconds, { label = "Tea", message = "The tea is ready." })` starts the extension's own timer: its countdown shows under the top bar in every view (with × to stop it), and when the time is up a notification says "Tea is over" with your message. Each extension has one timer — a new one replaces it —, `b.stopTimer()` stops it, and `b.on("timer", function(e) … end)` runs when it ends (`e.label`, `e.seconds`).

On Android the notification comes even when Brulion is closed: the timer's end is set as an alarm when it starts (and taken back when it stops). On a computer it comes while Brulion runs — also in the tray with its window closed —, in the browser while its tab is open. Brulion started again during a timer shows it counting on; one that ended meanwhile runs its `"timer"` reactions when the notebook opens, with `e.late` true.

```luau
b.command("tea", function(ctx)
    b.timer(4 * 60, { label = "Tea", message = "The tea is ready: take the bag out." })
    return "Four minutes"
end, { label = "Tea timer" })

b.on("timer", function(e)
    b.appendToJournal(b.today(), `- {os.date("%H:%M")} {e.label} ({e.seconds // 60} min)`)
end)
```

## Shared code
`b.require("Shared helpers")` gives what the code of another extension page returns — usually a table of functions that several extensions use, so each of them does not carry its own copy. That page is an extension like any other (tagged `extension`, its code in `luau` blocks, ending with `return`), and it runs only once you approved it on this device. Each extension that uses it gets a copy of its own, with its own permissions: shared code can never do more than the extension using it may. When the shared page changes, the extensions using it load again.

```luau
-- The page "Shared helpers" ends with `return M`: a table of functions.
local shared = b.require("Shared helpers")

b.command("open-tasks", function(ctx)
    return shared.count(b.query("type:tasks status:open", 1).total, "open task", "open tasks")
end, { label = "How many open tasks" })
```

## AI
`b.ai("question")` asks the model chosen in **Settings → Extensions → AI** and returns its answer as text; a table adds `system`, `model`, `maxTokens`, `effort`, `thoughts = true` (the reply's `thoughts`: what the model thought — Claude's summary of its thinking, a server's reasoning, the model on the device's thoughts), `images` (pictures of the notebook for a model that sees: `b.ai({ prompt = "What does it say?", images = { "receipt.jpg" } })`), `messages` for a conversation and `tools` the model may call. The page needs the permission `ai`. More in [[AI]].

## The app
`b.app` is Brulion itself: what it shows, its commands, this device's settings, its colours and its toolbar, status bar and menus. Reading needs nothing; changing needs the permission `app` (`permissions: [app]`). What it changes is the app on this device — like the settings you change by hand — and what an extension put into the app goes away when it is switched off.

| Function | What it does |
|---|---|
| `b.app.state()` | what is on screen: `view` (`"journals"`, `"page"`, `"calendar"` …), `page`, `date`, `dark`, `touch`, `narrow`, `width`, `height`, `platform`, `language` |
| `b.app.commands()` | the command palette's commands, each `{ id, title, shortcut, enabled }` |
| `b.app.run(id)` | runs one: `b.app.run("go.calendar")`, `b.app.run("theme.toggle")` |
| `b.app.settings()` | the settings an extension may change, with their values and what they take |
| `b.app.get(key)`, `b.app.set(key, value)` | one setting: `b.app.set("density", "compact")`, `b.app.set("focusMode", true)`, `b.app.set("editorFontSize", 18)` |
| `b.app.navigate(view)` | shows a view: `"calendar"`, `"tasks"`, `{ view = "tasks", query = "status:open" }` |
| `b.app.open(page, { line = 12, side = true })` | opens a page, at a line, in the side panel |
| `b.app.back()` | goes back |
| `b.app.toast(text, { tone = "success", button = { label = "Undo", command = "undo" } })` | a message, with a button that runs one of the extension's commands |
| `b.app.theme({ … })` | Brulion's colours, corners and fonts while the extension runs (below) |
| `b.app.contribute(place, item)` | puts a button or an entry into the app (below) |
| `b.app.remove(place, id)`, `b.app.contributions()` | takes one away; lists every extension's |
| `b.app.inputRule({ match = "->", replace = "→" })`, `b.app.inputRules()` | text replaced as you type (below); every extension's rules |
| `b.app.tagStyle({ tag = "pilne", tint = "danger" })`, `b.app.tagStyles()` | blocks with a tag drawn in a colour (below); every extension's styles |

Moving around (`navigate`, `open`, `back`, `run`) and messages happen after the call — in commands, actions and reactions, not while the page loads. `"route"` among the reactions runs when the screen shows something else: `e.view`, `e.page`, `e.date`.

**Colours.** `b.app.theme` takes the names of Brulion's design tokens: `bg`, `bgElevated`, `bgSunken` (the surfaces), `fg`, `fgMuted` (text), `accent`, `link`, `tagFg`, `border`, `selection`, `mark` (highlights), `danger`, `warning`, `success` and more, the corners `radius`, `radiusSm`, `radiusLg`, and the fonts `fontUi`, `fontSans`, `fontSerif`, `fontMono`. A colour is `"#rrggbb"`; colours for one mode go into `dark = { … }` or `light = { … }`. A misspelt name is an error that names the right one.

```luau
-- Sepia: paper-coloured pages in the light mode, a teal accent, rounder corners.
b.app.theme({
    accent = "#1f8a80",
    radius = 9,
    light = { bg = "#f4ecd8", bgElevated = "#fbf5e6", bgSunken = "#ebe1c8", fg = "#3b2f20" },
    dark = { bg = "#14110c" },
})
```

**Buttons and entries.** `b.app.contribute(place, item)` puts an item into one of six places: `"toolbar"` (a button of the top bar), `"statusbar"` (a text at the window's bottom), `"sidebar"` (an entry of the side bar), `"pageMenu"` (a page's menu), `"blockMenu"` (a block's menu) and `"slash"` (the `/` menu). An item has an `id`, a `label`, an `icon`, and says what it does: `command` (one of the extension's commands), `view`, `page`, `run` (a palette command) or, in the `/` menu, `insert` (text to write). The same `id` again replaces the item — a status bar text that counts something simply contributes itself again.

```luau
-- Today's open tasks in the status bar, always up to date, and a `/` entry for a meeting.
local function count()
    local due = b.query("type:tasks status:open due:<=today", 1).total
    b.app.contribute("statusbar", {
        id = "due",
        label = if due == 0 then "Nothing due" else `{due} due today`,
        icon = "calendar-check",
        run = "go.tasks",
    })
end

count()
b.on("change", function(e)
    count()
end)

b.app.contribute("slash", {
    id = "meeting",
    label = "Meeting notes",
    insert = "## Meeting\n- Who:\n- Decisions:\n- [ ] Next step",
})
```

**As you type.** `b.app.inputRule({ match, replace })` replaces text the moment you type it: `->` becomes `→` when you type the `>`. One Ctrl+Z brings back what you typed. With `word = true` only a whole word is replaced, once you type a space or punctuation after it. Nothing is replaced in code, formulas, link targets or web addresses, nor when you paste. Brulion comes with no rules of its own — an extension declares the ones you want — and `b.app.remove("inputRule", "->")` takes one away.

```luau
-- Arrows and dashes as you type, and a word corrected.
b.app.inputRule({ match = "->", replace = "→" })
b.app.inputRule({ match = "<-", replace = "←" })
b.app.inputRule({ match = "--", replace = "–" })
b.app.inputRule({ match = "...", replace = "…" })
b.app.inputRule({ match = "teh", replace = "the", word = true })
```

**Blocks by tag.** `b.app.tagStyle({ tag, tint })` draws every block with that tag on a coloured background with a bar at its left edge. The `tint` is one of the theme's colours — `"accent"`, `"success"`, `"danger"`, `"warning"`, `"info"`, `"neutral"`, which follow the light and the dark mode — or a colour `"#rrggbb"`. `fill = false` keeps only the bar, `bar = false` only the background. A style of `projekt` also colours `#projekt/dom`.

```luau
-- Urgent blocks on red, waiting ones with an amber bar.
b.app.tagStyle({ tag = "pilne", tint = "danger" })
b.app.tagStyle({ tag = "waiting", tint = "warning", fill = false })
b.app.tagStyle({ tag = "idea", tint = "#8e6cd8" })
```

An AI agent you work with can do all of this in the running app at once, without a page — and you can undo it in the top bar's **Live** menu. See [[Extensions]] and the developer's reference.

## Luau in short
- Texts are joined with `..`, or written with backticks and braces: `` `{n} pages` ``. A list is `{ "a", "b" }` and counts from 1; `#list` is its length; `for i, item in list do … end` goes through it.
- `local` makes a variable, `function … end` a function, `--` starts a comment, `~=` means "not equal", `nil` means "nothing".
- A text counts its bytes, not its letters: `#"żółw"` is 7. Use `utf8.len(text)` for letters, `b.fold` to compare, `b.sorted` to sort.
- A number is written with a point: `82.5`. Show it with `b.formatNumber`.
- `b.now()` is the date and the time now, `os.date("%H:%M")` the time alone.
- There are no files, no network and no other programs: only `b`.

## Limits
One call may take 2 seconds (the time you spend answering a question does not count) — a macro a quarter of a second, a template variable one second —, and all extensions together 64 MB of memory; past either, the call is stopped and the rest keeps working. A macro shows at most 400 characters; the macros of one page have 2 seconds together. A view shows up to 2000 lines, a row up to 24 controls; `b.blocks` gives up to 20 000 blocks; one request to a model has up to 200 000 characters. A question's text shows up to 10 000 characters, `b.choose` up to 500 options, `b.form` up to 50 fields; a timer's label 100 characters; an extension's settings 64 KB; `b.require` goes up to 16 pages deep.

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
| `b.require: … waits for approval on this device` | the shared code changed elsewhere, or is new: approve its page |
| `b.require: no page named …`, `… is no extension page` | the page to use is not there, or not tagged `extension` |
| `b.require: a cycle` | two pages require each other |
| `b.setSetting: "…" is a number` | a value of another kind than its setting |
| `b.choose: at most 500 options` | too many options for one question: ask in steps |
| `timeout: the extension ran too long` | one call took more than 2 seconds |
| `a macro or a template variable cannot change notes` (or `… settings or timers`) | a macro or a variable wrote, set a setting or a timer: do that in a command or an action |
| `a macro returns a text, a number, b.ui.text, b.ui.link or a list of them` | a macro returned something it cannot show |

`pcall` catches an error, so the code can go on: `local ok, message = pcall(b.write, name, text)`. `error("…", 0)` stops the command with a message of your own.
