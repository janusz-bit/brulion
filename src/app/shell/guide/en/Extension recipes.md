---
tags: [documentation]
aliases: [Luau recipes, Scripting examples]
brulion: document
---
# Extension recipes

Ready pieces for your own [[Extensions|extensions]]. To keep one, make a page with **New extension** (the palette) and put the code into its `luau` block; where a recipe says **Needs: write**, add `permissions: [write]` to the page's properties. To only try one, put the cursor in its code here and choose **Run Luau block** — its command or view is then in the palette until the extensions are loaded again. Every function used here is explained in [[Extension API]].

## The notebook in numbers
A command that only reads and answers with a message.

```luau
b.command("numbers", function(ctx)
    local projects = b.pages({ tag = "project" })
    local open = b.query("type:tasks status:open", 1)
    local overdue = b.query("type:tasks due:overdue", 1)
    local tasks = `{open.total} open tasks ({overdue.total} overdue)`
    return `{#b.pages()} pages, {#projects} projects, {tasks}`
end, { label = "The notebook in numbers" })
```

## A task for tomorrow, a task a day later
**Needs: write.** Dates are texts, and `addDays` moves one by some days. The first command asks for a task and writes it into today's journal, due tomorrow; the second moves the due date of the task under the cursor by a day.

```luau
-- "2026-11-13" and 1 give "2026-11-14"; the end of a month and of a year are handled.
local function addDays(date, n)
    local y, m, d = date:match("^(%d+)-(%d+)-(%d+)$")
    local noon = os.time({ year = tonumber(y), month = tonumber(m), day = tonumber(d) + n })
    return os.date("!%Y-%m-%d", noon)
end

b.command("tomorrow", function(ctx)
    local text = b.ask("A task for tomorrow")
    if text == "" then
        return nil
    end
    local due = addDays(b.today(), 1)
    b.appendToJournal(b.today(), `- [ ] {text} 📅 {due}`)
    return `Added, due {due}`
end, { label = "Task for tomorrow" })

b.command("postpone", function(ctx)
    local block = ctx.block
    local due = block and block.text:match("📅 (%d%d%d%d%-%d%d%-%d%d)")
    if not due then
        return "Put the cursor in a task with a due date."
    end
    local later = addDays(due, 1)
    local text = block.text:gsub("📅 %d%d%d%d%-%d%d%-%d%d", "📅 " .. later, 1)
    b.replaceBlock(block.ref, text, block.task)
    return `Due {later}`
end, { label = "Postpone the task by a day", block = true })
```

## A note from a few questions
**Needs: write.** A list to pick from, a question, a new page in a folder and a link to it in today's journal.

```luau
b.command("new-note", function(ctx)
    local kind = b.choose("What kind of note?", { "Meeting", "Book", "Idea" })
    if kind == nil then
        return nil
    end
    local title = b.ask(`{kind}: its title?`)
    if title == "" then
        return nil
    end
    local name = `{kind}s/{title}`
    if b.read(name) ~= nil then
        return `{name} is there already`
    end
    b.write(name, table.concat({
        "---",
        `tags: [{kind:lower()}]`,
        `created: {b.today()}`,
        "---",
        `# {title}`,
        "",
        "- ",
    }, "\n") .. "\n")
    b.appendToJournal(b.today(), `- New {kind:lower()}: [[{name}]]`)
    return `Created {name}`
end, { label = "New note from a kind" })
```

## What is due this week, with checkboxes
**Needs: write.** A view over a query. Ticking a task calls the action with the task's `ref` and `text`; if the line has changed in the meantime, nothing is written and the view is drawn again.

```luau
b.view("week", function(params)
    local result = b.query("type:tasks status:open due:<=+7d sort:due", 100)
    if #result.items == 0 then
        return b.ui.empty("Nothing due", "No open task is due within a week.")
    end
    local lines = { b.ui.heading(`Due within a week: {result.total}`) }
    for _, task in result.items do
        table.insert(lines, b.ui.row({
            b.ui.check(false, "done", { ref = task.ref, text = task.text }),
            b.ui.text(task.text),
            b.ui.link(task.page),
        }))
    end
    return b.ui.column(lines)
end, {
    done = function(args)
        b.replaceBlock(args.ref, args.text, "x")
    end,
}, { label = "Due this week", icon = "calendar-check" })
```

## Projects in a table
A view with a table: every page tagged `project`, its open tasks and the nearest due date.

```luau
b.view("projects", function(params)
    local rows = {}
    for _, name in b.pages({ tag = "project" }) do
        local open = b.query(`type:tasks status:open page:"{name}" sort:due`, 1)
        local first = open.items[1]
        table.insert(rows, { name, tostring(open.total), first and first.due or "—" })
    end
    if #rows == 0 then
        return b.ui.empty("No projects", "Give a page the project tag in its properties.")
    end
    local lines = {
        b.ui.heading("Projects"),
        b.ui.table({ "Project", "Open", "Next due" }, rows),
    }
    for _, row in rows do
        table.insert(lines, b.ui.link(row[1]))
    end
    return b.ui.column(lines)
end, nil, { label = "Projects", icon = "folder" })
```

## Words per day
A chart of the journal: the daily notes of two weeks through a query, their words counted.

```luau
local function words(text)
    local n = 0
    for _ in text:gmatch("%S+") do
        n += 1
    end
    return n
end

b.view("writing", function(params)
    local days = b.query("type:pages date:-13d..today sort:date", 14)
    local labels, counts = {}, {}
    for _, day in days.items do
        table.insert(labels, day.date:sub(6))
        table.insert(counts, words(b.read(day.page) or ""))
    end
    if #labels == 0 then
        return b.ui.empty("No daily notes", "Nothing was written in the last two weeks.")
    end
    return b.ui.column({
        b.ui.heading("Words per day"),
        b.ui.chart({
            kind = "bar",
            labels = labels,
            series = { { name = "Words", values = counts } },
        }),
    })
end, nil, { label = "Words per day", icon = "chart-column" })
```

## Counters with − and +
**Needs: write.** Lines such as `- Glasses of water 3 #counter` anywhere in the notebook become a view with a stepper each; a tap rewrites the number in its line.

```luau
b.view("counters", function(params)
    local rows = {}
    for _, block in b.blocks({ tag = "counter" }) do
        local name, number = block.text:match("^(.-)%s+(%d+)%s+#counter")
        if name then
            table.insert(rows, b.ui.row({
                b.ui.text(name),
                b.ui.stepper({
                    value = tonumber(number),
                    step = 1,
                    action = "set",
                    args = { ref = block.ref, name = name },
                }),
            }))
        end
    end
    if #rows == 0 then
        return b.ui.empty("No counters", "Write a line that ends with a number and the counter tag.")
    end
    return b.ui.column(rows)
end, {
    set = function(args)
        b.replaceBlock(args.ref, `{args.name} {b.formatNumber(args.value, 0)} #counter`)
    end,
}, { label = "Counters", icon = "list-checks" })
```

## A work log by itself
**Needs: write.** A reaction to saves: every page you edit lands once in today's journal as a link.

```luau
b.on("save", function(e)
    local page = b.page(e.page)
    if not page or page.date then
        return                          -- gone in the meantime, or a day of the journal itself
    end
    local link = `[[{e.page}]]`
    local today = b.read(b.today()) or ""
    if not today:find(link, 1, true) then
        b.appendToJournal(b.today(), `- Worked on {link}`)
    end
end)
```

## A page that writes itself
**Needs: write.** A reaction to changes keeps the page "Project index" in step with the pages tagged `project`. It writes only when something is different, so a sync has nothing to carry otherwise, and what an extension writes never comes back to it as a change.

```luau
local function rebuild()
    local lines = { "Written by an extension; changes here are replaced.", "" }
    for _, name in b.pages({ tag = "project" }) do
        local open = b.query(`type:tasks status:open page:"{name}"`, 1).total
        table.insert(lines, `- [[{name}]] — open tasks: {open}`)
    end
    local text = table.concat(lines, "\n") .. "\n"
    if b.read("Project index") ~= text then
        b.write("Project index", text)
    end
end

b.on("change", function(e)
    rebuild()
end)

b.command("rebuild", function(ctx)
    rebuild()
    return "The project index is up to date"
end, { label = "Rebuild the project index" })
```

## The week in review, by AI
**Needs: write, ai** (and a model chosen in **Settings → Extensions → AI**, see [[AI]]). The tasks finished this week go to the model; its answer goes into today's journal.

```luau
b.command("review", function(ctx)
    local done = b.query("type:tasks status:done done:this-week", 200)
    if #done.items == 0 then
        return "No task was finished this week."
    end
    local lines = {}
    for _, task in done.items do
        table.insert(lines, `- {task.text} ({task.page})`)
    end
    local answer = b.ai({
        system = "You write a short, warm review of a week from a list of finished tasks: "
            .. "three sentences, no list.",
        prompt = table.concat(lines, "\n"),
        maxTokens = 600,
    })
    local line = answer:gsub("%s+", " ")
    b.appendToJournal(b.today(), `- The week in review: {line}`)
    return "The review is in today's journal"
end, { label = "Review my week (AI)" })
```

## Good habits
- Check what may be missing: `b.read` and `b.page` give `nil` for a page that is not there, `ctx.block` is not there outside a block, `ctx.page` is an empty text outside a page.
- In a view, give a control what its action needs (`ref`, `text`) instead of looking it up again: a line that changed is then refused, not overwritten.
- Ask the notebook for less: a query with filters and a limit is faster than reading every page.
- Compare texts with `b.fold` and sort them with `b.sorted`, so that Polish letters behave.
- Keep what must stay in notes. Variables are gone when the page is saved again.
