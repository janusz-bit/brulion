# Extensions (Luau)

Brulion is scripted in [Luau](https://luau.org), a small, fast Lua with optional types. An
**extension** is a script that adds commands to the palette, views of its own and reactions to
saves and changes, and that reads and writes the open notebook through one API, the table `b`.
Extensions come from two places:

* **the notebook itself** — pages tagged `#extension` (`#rozszerzenie`) whose ```` ```luau ````
  blocks are code (docs/SYNTAX.md §13): the notebook carries its own code, edited, synced, linked
  and versioned like any note. Saved, a page's code runs again at once;
* **built in** — the **gym** (`src/core/extensions/builtin/silownia.luau`, docs/SYNTAX.md §5 "Gym",
  docs/UI.md "Gym") and the **AI assistant** (`asystent.luau`, "AI" below), switched on in
  Settings → Extensions and written with the same API.

The idea is Emacs' and TiddlyWiki's: the program you use is made of code you can read and change
from inside it. Emacs gives every user an init file and `eval-buffer`, a `*scratch*` buffer and a
`*Messages*` log; TiddlyWiki keeps its own code in tiddlers, and a tiddler with a built-in one's
title replaces it (a "shadow tiddler"). Brulion has the same pieces: extension pages, the Luau
console, and the built-in gym's source as a page that then runs instead of the built-in code.

The first part of this document is for whoever **writes** an extension; "Sandbox and limits",
"Layers" and "Tests" at the end are for whoever changes Brulion. Users find the same in the app:
the documentation notebook's pages **Extensions**, **Extension API** and **Extension recipes**
(`src/app/shell/guide/en/`, in Polish `guide/pl/`: **Rozszerzenia**, **API rozszerzeń**, **Przepisy
na rozszerzenia**). Every ```` ```luau ```` example here and there is run by
`tests/core/extensions/tst_extensions_docs.cpp`, which also checks that this document and the
pages name every function of `b`.

| To … | read |
|---|---|
| write a first extension | [Quick start](#quick-start), [How the code runs](#how-the-code-runs) |
| look a function up | [The API (`b`)](#the-api-b), [Commands](#commands), [Views](#views), [Hooks](#hooks), [Questions](#questions), [AI](#ai-bai) |
| copy something that works | [Recipes](#recipes) |
| find out why it does not work | [Errors](#errors), [Troubleshooting](#troubleshooting), [The Luau console](#the-luau-console) |
| know what a page may do and when it runs | [Extensions in the notebook](#extensions-in-the-notebook), [Sandbox and limits](#sandbox-and-limits) |
| write one as an AI agent, from the command line | [Written by an AI agent](#written-by-an-ai-agent) |

## Quick start

1. The palette's **New extension** (or Settings → Extensions → **New extension**) writes
   `Extensions/My extension.md` (`Rozszerzenia/Moje rozszerzenie.md` with a Polish interface) with
   a command, a view and a hook to start from, approves it on this device and opens it.
2. The smallest extension is a page with the tag and one code block:

   ````markdown
   ---
   tags: [extension]
   description: My first extension.
   ---
   ```luau
   b.command("hello", function(ctx)
       return "Hello! Today is " .. b.today()
   end, { label = "Hello" })
   ```
   ````

3. Run **Hello** from the palette (Ctrl+K): the text the function returns shows as a message.
4. Change the code. When the page is saved (the editor saves by itself), Brulion loads it again
   **at once**; an error names the page's line, and the version before it keeps running.
5. Try the API in the **Luau console** (the palette): `b.today()`, `#b.pages()`,
   `b.query("type:tasks status:open").total`. **Run Luau block** (the palette, the caret in a
   ```` ```luau ```` block of any page) evaluates that block there.

What the page may do beyond reading is written in its properties: `permissions: [write]` to change
notes, `ai` to ask a model ("Extensions in the notebook" below).

## How the code runs

* **Loading.** The page's code — every ```` ```luau ```` block, in order, as one script — runs once
  from top to bottom whenever it is loaded: when the notebook opens, when the page is approved or
  changed, after "Reload extensions". This is where it defines its functions and **registers**
  them: `b.command`, `b.view`, `b.on`. Code at the top level runs on every load (every save of the
  page), so it should only set things up; it cannot ask a question or a model (`b.ask`, `b.choose`,
  `b.form`, `b.ai` are errors while loading).
* **Calls.** Afterwards Brulion calls what was registered: a command when it is chosen, a view's
  function whenever the view is drawn, an action when its control is tapped, a hook when its
  event happens. Every call has its own time limit (2 s, not counting the time it waits for an
  answer) and ends with a value or an error ("Errors" below).
* **State.** The script's variables stay in memory between calls — until its code is loaded again
  (the page changed, the extension was switched off and on, "Reload extensions", another
  interface language), the notebook is closed or the app quits. Nothing else is kept: what has to
  survive belongs in the notebook (`b.write`, `b.appendToJournal`).
* **One at a time.** One call executes at a time; a call that waits for an answer (a question, a
  model) lets others run meanwhile, and a hook never interrupts a call.
* **Apart.** Every extension has its own global variables and its own `b`; one extension cannot
  see or change another's, the standard library or `b` itself.

```luau
local runs = 0                          -- kept until this code is loaded again

b.command("count", function(ctx)
    runs += 1
    return `Run {runs} since the code was loaded`
end, { label = "Count the runs" })
```

## Luau in Brulion

Luau 0.743 (`3rdparty/luau`). It is Lua 5.1 plus: `continue`, `+=` and `..=`, `if … then … else …`
as an expression, string interpolation `` `Today: {b.today()}` `` (a literal brace is `\{`),
`for i, v in list do` without `ipairs` / `pairs`, `//` (floor division) and optional type
annotations (`local n: number = 0`).

* **Types are not checked.** Brulion ships Luau's compiler and virtual machine only: annotations
  and `--!strict` are accepted and ignored (a type checker on save is in TODO.md). Mistakes show
  when the line runs.
* **The library**: `string`, `table`, `math`, `utf8`, `bit32`, `buffer`, `vector`, `coroutine`,
  `os.date` / `os.time` / `os.clock` / `os.difftime`, `debug.traceback` / `debug.info`, and the
  base functions (`pcall`, `error`, `assert`, `tonumber`, `tostring`, `type`, `select`, `next`,
  `unpack`, `setmetatable` …). Not there: `io`, `require`, `load` / `loadstring` / `dofile`,
  `os.execute` / `os.getenv` / `os.remove`, `string.dump`, `collectgarbage` (`gcinfo()` tells the
  memory in use), the rest of `debug`. `print(…)` writes to the Luau console.
* **Text is UTF-8 bytes.** `#s` counts bytes (`#"żółw"` is 7), `string.upper` and the pattern
  classes `%a`, `%w`, `%l`, `%u` know ASCII only, `<` compares bytes. For people's text use
  `utf8.len(s)` (characters), `b.fold(s)` (to compare and search whatever the case and the
  diacritics) and `b.sorted(list)` (alphabetical order, Polish letters in their places).
* **Numbers** are written with `.` whatever the device's language (`82.5`, `tonumber("82.5")`,
  `string.format("%.1f", x)`). A number from a note may have a comma:
  `tonumber((s:gsub(",", ".")))`. Show numbers with `b.formatNumber`.
* **Dates** are text: `b.today()` gives `"2026-11-13"`, and such dates compare with `<` as they
  should. `os.date("%H:%M")` is the local time. To add days, see "Recipes".
* **Patterns backtrack**: `^(%d+[,.]?%d*)%s*(%a+)` on a line of 20 000 digits is quadratic
  (seconds, then the time limit). Read long input in anchored steps (`silownia.luau`,
  `readNumber`).

## The API (`b`)

Everything an extension can do with the notebook and the app is a function of `b`. Conventions:

* A **page** is named as in a `[[link]]`: its title, an alias or a path (`Projects/Home`). A daily
  note is named by its file (`2026-11-13`, unless the notebook's journal files are named
  otherwise; `b.page(name).date` is its day).
* A **date** is `"YYYY-MM-DD"` (a local day), a date with a time `"YYYY-MM-DD HH:MM"`.
* "Not there" is `nil` (a page that does not exist); a wrong argument, a missing permission or a
  refused write is an **error** ("Errors").
* Tables an extension gets are copies: changing them changes nothing in the notebook.
* Values cross between the script and the app as plain data: text, numbers, booleans, `nil`, and
  tables that are either a list (`{ "a", "b" }`) or a record with text keys
  (`{ ref = …, text = … }`), at most 64 levels deep. A function or a table that contains itself
  cannot cross (an error); a table mixing both kinds comes back with text keys (`"1"`, `"2"`).

### Blocks

`b.blocks`, `b.search`, `b.query` and a command's `ctx.block` describe a block — a list item, a
paragraph, a heading … — the same way:

| Field | |
|---|---|
| `ref` | where the block is: `"Journal/2026-11-11.md#L3@1a2b3c4d"` (the file, the line, a hash of the line's text). Give it to `b.replaceBlock`; do not take it apart. It goes **stale** when the line changes |
| `page` | the page's name |
| `date` | `"2026-11-11"` when the page is a daily note, else absent |
| `text` | the block's first line as written, without its indentation, list marker and checkbox: `"Pay the rent 📅 2026-11-13 #home"` — tags, dates and links are still in it |
| `task` | the checkbox's character when the block is a task: `" "` open, `"x"` done, `"/"` in progress, `"-"` cancelled, `">"` deferred (a Logseq keyword — `TODO`, `DONE` … — as its character); absent otherwise |
| `tags` | the block's own tags, lower case, without `#` |
| `props` | its properties (`key:: value` lines under it) as `{ key = "value" }`, keys in lower case, a list joined with `, ` |
| `children` | the blocks indented under it, each the same way (`b.blocks` and `ctx.block` only) |

### Reading

Reading needs no permission.

| Function | Gives |
|---|---|
| `b.pages()`, `b.pages({ tag = "project" })` | the names of all pages (daily notes too), sorted as `b.sorted` sorts; with `tag` only the pages whose own tags — the frontmatter's `tags:` or a `tags::` line — have it (with or without `#`, any case). The filter is a table: `b.pages("project")` is an error, not every page |
| `b.page(name)` | `{ name, tags, props, date? }` — the page's own tags and properties (as a block's `props`) — or `nil` |
| `b.read(name)` | the page's whole Markdown text, or `nil` |
| `b.blocks({ page = name })` | the page's top-level blocks in order, each with its `children`: list items, but also headings (`text` is `# Home`, with its hashes), paragraphs, code blocks (the fence line) and the frontmatter (`---`, its `props` the page's). `{}` for a page that does not exist |
| `b.blocks({ tag = "workout", journal = true, from = "2026-11-01", to = "2026-11-30" })` | every block that carries the tag itself (not through its parent or its page), each with its `children`: first those of ordinary pages (by path), then those of daily notes, oldest day first. `journal = true` — or a `from` / `to` day, both optional and inclusive — keeps daily notes only; a `from` or `to` that is no day (`"2026-11-31"`) is an error. At most 20 000 blocks |
| `b.search(text, limit?)` | full-text search as the search view does it (words in any form, diacritics optional, the best first): one hit per page, at most `limit` (20 by default, 200 at most), each `{ page, date?, excerpt, line?, ref?, text?, task? }` — `line` the first matching line, counted from 1 (none when only the title matched), `ref` / `text` / `task` of the block holding that line |
| `b.query(source, limit?)` | a query of the query language (docs/SYNTAX.md §10): `{ total, items }` with at most `limit` items (50 by default, 1000 at most; a smaller `limit:` in the query wins). Items are blocks without `children`, plus a task's `due`, `scheduled`, `start` and a reminder's `reminder` (`"2026-11-12 09:00"`); for `type:pages` they are `{ page, date? }`. A query that is none is an error with the reason |
| `b.checkExtension(name, text)` | loads `text` as the extension page `name` would load (a new page: where new pages go), in an engine of its own — its top-level code runs with 1 s and 32 MB, writes only computed, nothing stays registered, no approval needed: `{ ok, id, title, path, permissions, problems, commands = { { name, label, block } }, views = { { name, label, icon, actions } }, hooks = { events… }, printed, writes? }`, or `ok = false` with `error`, `line` (the page's line, from 1) and `status` (`"timeout"` …). Text that is no extension page (no tag, no code) has `ok = false` too. Inside such a check it reports an error instead of checking again |

`b.blocks({ tag = … })` and `tag:` in `b.query` differ: the first finds the blocks that carry the
tag themselves, the query also those under a tagged block or on a tagged page, and `tag:project`
matches `#project/home` too. `page:this` and `ref:this` mean nothing in a script (there is no
"page of the query").

```luau
-- An extension that writes extensions checks them first (the AI assistant's write_extension).
local page = "---\ntags: [extension]\n---\n```luau\nb.command(\"hi\", function() return \"Hi\" end)\n```\n"
local report = b.checkExtension("Greeting", page)
if not report.ok then
    return `Line {report.line}: {report.error}`
end
return `{#report.commands} command(s): {report.commands[1].name}`
```

```luau
b.command("numbers", function(ctx)
    local projects = b.pages({ tag = "project" })
    local open = b.query("type:tasks status:open", 1)
    local overdue = b.query("type:tasks due:overdue", 1)
    local tasks = `{open.total} open tasks ({overdue.total} overdue)`
    return `{#b.pages()} pages, {#projects} projects, {tasks}`
end, { label = "The notebook in numbers" })
```

```luau
-- Every block under `blocks`, children included, with its depth.
local function walk(blocks, visit, depth)
    depth = depth or 0
    for _, block in blocks do
        visit(block, depth)
        walk(block.children, visit, depth + 1)
    end
end

b.command("outline", function(ctx)
    if ctx.page == "" then
        return "Open a page first."
    end
    local lines = {}
    walk(b.blocks({ page = ctx.page }), function(block, depth)
        table.insert(lines, string.rep("  ", depth) .. block.text)
    end)
    print(table.concat(lines, "\n"))
    return `{#lines} blocks of {ctx.page} are in the Luau console`
end, { label = "Print this page's outline" })
```

### Writing

Writing needs the permission `write` (`permissions: [write]` on the page; the console has it).
Every write goes through the notebook like the editor's: the whole file is written, every other
line stays byte for byte, the index, the views and a sync follow.

| Function | Does |
|---|---|
| `b.write(name, content)` | replaces the page's whole text, or creates the page where new pages go (`"Meetings/Budget"` makes the folder). Returns `true`. Read first what you do not want to lose |
| `b.appendToJournal(date, markdown)` | adds `markdown` as written — start an item with `- ` — at the end of that day's note. A missing note is created from the journal template; a template's empty last bullet is filled instead of left above |
| `b.replaceBlock(ref, text, task?)` | gives the block's first line the new `text` (one line): its indentation and list marker stay, and so do the Obsidian Tasks dates (`✅ ❌ ➕ 📅 ⏳ 🛫`) and the block id at its end unless `text` brings its own. `task` — one character, as a block's `task` — sets the checkbox as the editor does (`"x"` adds `✅` with today's date; a plain line becomes a task). Returns the block's new `ref` |

`b.replaceBlock` never writes over what it did not see: when the line changed since `ref` was read —
a sync arrived, another pane edited it — the call is an error with `stale:` in it and nothing is
written (the same text up to 50 lines away still counts as the same line: lines added above do not
matter). Read the block again and decide again. A `ref` is taken as a block had it — a note of the
notebook, a line, the hash of the line's text: one without its hash, or with the path of a file
that is no note (`.git/config`, a file among the attachments), is `not a block reference`.

```luau
b.command("note", function(ctx)
    local text = b.ask("What to note?")
    if text == "" then
        return nil                              -- cancelled: nothing to say
    end
    b.appendToJournal(b.today(), `- {os.date("%H:%M")} {text}`)
    return "Noted in today's journal"
end, { label = "Note with the time" })
```

```luau
b.command("done", function(ctx)
    local block = ctx.block
    if not block or not block.task then
        return "Put the cursor in a task."
    end
    local ok, err = pcall(b.replaceBlock, block.ref, block.text, "x")
    if not ok then
        return "Nothing changed: " .. tostring(err)
    end
    return "Done: " .. block.text
end, { label = "Mark the task done", block = true })
```

### Helpers

| Function | Gives |
|---|---|
| `b.today()` | today as `"2026-11-13"` (the device's local day) |
| `b.daysBetween(a, b)` | the days from date `a` to date `b` (negative when `b` is earlier); an invalid date is an error |
| `b.sorted(list)` | a new list of the same texts in alphabetical order as Brulion sorts names: Polish letters in their places, any case, numbers by value (`"Chapter 2"` before `"Chapter 10"`). Texts only, at most 100 000 |
| `b.fold(text)` | the text in lower case without diacritics, spaces trimmed and single: `"  Leżąc  NA ławce "` → `"lezac na lawce"`. For comparing and searching |
| `b.formatNumber(n, decimals?)` | the number as text with at most `decimals` decimals (2 by default, 10 at most), trailing zeros dropped, with a decimal comma in a Polish notebook: `82,5`, `100`. Not a number: `—` |
| `b.lang()` | the language to write notes in: `"pl"` or `"en"` |
| `b.uiLang()` | the interface's language (labels, messages): `"pl"` or `"en"`. The app gives both the interface's language today; ask each for what it means |
| `b.log(text)`, `print(…)` | a line in the Luau console and in the event log, under the extension's name (at most 4000 characters). `print` joins its values with tabs and writes a table as `table`: build the text yourself |
| `b.timer(seconds, label?)` | starts the app's rest timer (1 … 86400 s) — the gym's: its countdown shows in the Gym view, and at the end a notification and a message say "`label` is over" and speak of the next set. One timer at a time: a new one replaces it |
| `b.data(name)` | read-only data bundled with a built-in extension (the gym's `b.data("exercises")`, the AI assistant's `b.data("reference")` — this document's text); `nil` for extension pages — unless the page overrides a built-in, which keeps the built-in's data |

## Commands

`b.command(name, fn, { label = …, block = … })` adds a command to the palette. `name` is its id
within the extension (the palette's id is `ext.<extension>.<name>`); `label` is what the palette
shows (else the name), with the extension's title under it. Registering a name again replaces it.

The function gets one table, `ctx`:

| Field | |
|---|---|
| `ctx.page` | the name of the page on screen, `""` when the view on screen is no page (the journal's stream, Tasks, a view of an extension) |
| `ctx.block` | with `block = true`: the block under the caret (above, "Blocks") |
| `ctx.selection` | always `""` so far |

With **`block = true`** the command works on the block under the caret: the palette offers it only
in an editor (not in the Markdown source), and the page is saved first, so the block is read as
shown. `ctx.block` can still be missing (the caret's line is no block of the saved page): check
it.

What the function **returns** is its message: a text is shown as a message (a toast), anything
else (or nothing) is silent. An error is shown as "The extension failed: …" and written, with its
traceback, to the console. A command may ask ("Questions") and wait for a model ("AI").

## Views

`b.view(name, render, actions?, { label = …, icon = … })` registers a view: `render(params)`
returns what to show as nodes made with `b.ui`, and Brulion draws them with its own components
(nothing of the extension is QML or HTML). With a `label` the palette offers the view (id
`extview.<extension>.<name>`, route `extension` `{extension, name, …params}`); `icon` is the name
of one of the app's icons (`assets/icons/`: `folder`, `calendar-check`, `chart-column`,
`list-checks`, `star` …; else a puzzle piece).

| Node | Shows |
|---|---|
| `b.ui.column(children)` | its children one under another. The usual root; columns inside columns just continue the list |
| `b.ui.heading(text)` | a heading |
| `b.ui.text(text, muted?)` | a paragraph that wraps; `muted = true` in the secondary colour |
| `b.ui.link(page, label?)` | a link that opens the page (and makes it when it does not exist) |
| `b.ui.button(label, action, args?)` | a button that calls the view's action `action` with `args` |
| `b.ui.row(children)` | a card whose controls sit side by side and wrap: `check`, `stepper`, `text`, `link`, `button`, `open`, `run` (at most 24) |
| `b.ui.check(checked, action, args?)` | the tasks' checkbox; a tap calls the action. **Only inside a row** |
| `b.ui.stepper({ value, step, unit?, action, args? })` | `− value +`: a tap calls the action with `args` and the new `value` (never below 0; `step` 1 by default; shown with at most two decimals and the unit). **Only inside a row** |
| `b.ui.table(columns, rows)` | a table: `columns` a list of headings, `rows` a list of lists of cells (text; format numbers yourself). Made for up to four columns: the second is the widest, from the third on they are right-aligned |
| `b.ui.chart({ kind, title?, labels, series })` | a chart drawn like the notes' charts: `kind` `"line"` (the default) or `"bar"`, `labels` a list of texts, `series = { { name = …, values = { … } } }` with a value per label (`nil` leaves a gap) |
| `b.ui.empty(title, text)` | an empty state: an icon, a title and a sentence |
| `b.ui.open(label, view, params?)` | opens another view of the extension (`""`: this one) with `params` — a link in a row, a "back"-like button on its own |
| `b.ui.run(label, command)` | a button that runs one of the extension's commands (`ctx.page` is `""` there) |

A view shows at most 2000 lines; nodes it does not know are left out. `params` are the route's
other keys — what an `open` node passed, `{}` from the palette; `view`, `extension`, `name` and
`title` are the route's own and never reach `render`.

`render` returns one node; a list of nodes is taken as a column of them, and anything else —
nothing, a text — is an error ("The view could not be drawn: … returned nothing"), never an empty
view. A `nil` among the children of a column or a row is left out, so a piece that is not always
there is written `show and b.ui.text(…) or nil`; a table's `nil` cell shows empty.

**Actions** are the functions of the third argument, by name. A control's `args` are stored with
the drawn view and given back to the action when it is tapped, so they must be plain data and
carry what the action needs — a block's `ref` and `text` — instead of something the function
would look up again: a line that changed meanwhile is then refused (`stale:`), not overwritten.
For that case the app says "The line changed in the meantime" and draws the view again.

**When it is drawn.** `render` runs when the view opens, after every command and action, and
whenever a note changes anywhere (the editor, a sync). It should be quick, read what it shows from
the notebook each time, and neither write nor wait: `b.ask` and `b.ai` there end the drawing with
"A view cannot wait for a question or for AI".

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

## Hooks

`b.on(event, fn)` calls `fn` with one table when something happens in the notebook (Emacs'
`add-hook`):

| Event | When | `e` |
|---|---|---|
| `b.on("save", fn)` | this device's editor saved a page (right after the save) | `{ page, path }`: the page's name and its file in the notebook |
| `b.on("change", fn)` | notes changed — the editor, a sync, another program, an import; once a burst of changes is over (0.6 s) | `{ pages = { names… } }`; a deleted page is named too (`b.read` gives `nil`) |

What extensions write — any of them, in a command, an action or a hook — is no `"change"` (no
echoes, no loops): the hooks hear the editor, a sync and other programs, so one extension does not
react to what another wrote. A hook may write and may ask a model, but never asks a question
(`b.ask` there is an error); it runs under the same limits; what it returns is written to the
console; and one that fails three times in a row is taken off until its extension loads again
(Emacs does the same to a failing `post-command-hook` function), its errors in the console and the
event log. At most 256 hooks in all. A view is not drawn again after a hook: keep what a view shows
in the notebook, not in variables a hook fills.

```luau
-- Keeps the page "Project index" in step with the pages tagged #project.
local function rebuild()
    local lines = { "Written by the extension; changes here are replaced.", "" }
    for _, name in b.pages({ tag = "project" }) do
        local open = b.query(`type:tasks status:open page:"{name}"`, 1).total
        table.insert(lines, `- [[{name}]] — open tasks: {open}`)
    end
    local text = table.concat(lines, "\n") .. "\n"
    if b.read("Project index") ~= text then     -- only a difference: nothing to sync otherwise
        b.write("Project index", text)
    end
end

b.on("change", function(e)
    rebuild()
end)
```

## Questions

| Function | Asks | Answer |
|---|---|---|
| `b.ask(prompt)` | for a line of text | the text; `""` when cancelled (the dialog takes no empty answer) |
| `b.choose(prompt, options)` | to pick one of a list of texts (one tap) | the option; `nil` when cancelled |
| `b.form(title, fields)` | for numbers, each with − / + and typing (`82,5` too) | `{ key = number }`; `nil` when cancelled |

A form's field is `{ key, label, value, step?, unit? }` (`step` 1 by default). The dialog draws
number fields only: a field with `kind = "text"` is accepted but cannot be typed in (its `value`
comes back unchanged) — ask for text with `b.ask`. A prompt's first line is the dialog's title;
further lines show under it as plain text (the AI assistant shows there what a change would
write).

Each of them suspends the call: the engine returns the question, the app shows it
(ShellExtensionQuestion), and the answer resumes the call where it stopped — no callbacks, no
nested event loop. They are refused at load time, inside a coroutine of the script and inside
library callbacks (`table.sort`'s comparator, `string.gsub`'s function). Any number of calls may
wait at once (questions, AI answers) while others run; the app shows one question at a time — a
new one answers the older "cancelled". A view never waits; a hook never asks.

```luau
b.command("water", function(ctx)
    local answer = b.form("Water", {
        { key = "glasses", label = "Glasses", value = 1, step = 1 },
        { key = "size", label = "A glass", value = 250, step = 50, unit = "ml" },
    })
    if answer == nil then
        return nil
    end
    local total = b.formatNumber(answer.glasses * answer.size)
    b.appendToJournal(b.today(), `- Water: {total} ml`)
    return `Logged {total} ml`
end, { label = "Log water" })
```

## AI (`b.ai`)

`b.ai(prompt)` or `b.ai({prompt = …, system = …, model = …, maxTokens = …, effort = …})` (or
`messages = {{role = "user" | "assistant", content}, …}` for a conversation, the last message the
user's) asks the model the user chose on this device and returns its text and the whole reply
(`{text, model, stopReason, inputTokens, outputTokens, raw}`); a failure is an error at the
script's line ("b.ai: the API key was refused (HTTP 401): …", "b.ai: no connection: …", "b.ai: the
model declined to answer") that `pcall(b.ai, …)` catches. It needs the permission `ai`
(`permissions: [ai]`, approved with the page like `write`; the console has it). `maxTokens` is a
whole number from 1 to 128000 (Claude gets 16000 without one); `effort` is `"low"`, `"medium"`,
`"high"`, `"xhigh"` or `"max"` and reaches Claude only.

```luau
b.command("tldr", function(ctx)
    local text = ctx.page ~= "" and b.read(ctx.page) or nil
    if not text or text == "" then
        return "Open a page with some text first."
    end
    local stop = utf8.offset(text, 20001)       -- 20 000 characters at most, never half a letter
    if stop then
        text = text:sub(1, stop - 1)
    end
    local summary = b.ai({
        system = "You sum a note up in at most three short sentences, in the note's language.",
        prompt = text,
        maxTokens = 400,
    })
    local line = summary:gsub("%s+", " ")
    b.appendToJournal(b.today(), `- Summary of [[{ctx.page}]]: {line}`)
    return summary
end, { label = "Sum this page up into the journal" })
```

**Tools** (an agent): `tools = {{name, description, input = <a JSON schema of an object>}}` lets
the model call functions the script runs (at most 64; a name is letters, digits, `_` and `-`). A
reply that calls some has `toolCalls = {{id, name, input}}`; the script runs them and asks again
with the conversation: the reply's turn as `{role = "assistant", raw = reply.raw}` — verbatim, the
providers check its thinking blocks — and then one `{role = "user", results = {{id, content,
isError?}}}` with every call's result, until a reply without calls. Both providers take the same
table (Anthropic's `tool_use` / `tool_result` blocks, an OpenAI-compatible API's `tool_calls` and
`tool` messages; a server of one's own needs a model that knows tools); a turn of the other
provider (the setting changed mid-conversation) is sent as its text. Forced tool use is not
offered (the current Claude models refuse it): a tool's description says when to call it.
The call waits for the answer like a question: nothing blocks — other commands, views and the UI
go on — and its result is handled as the command's (a message, the console).

```luau
local tools = {
    {
        name = "search_notes",
        description = "Full-text search over the user's notes. "
            .. "Call it before answering a question about them.",
        input = {
            type = "object",
            properties = { query = { type = "string", description = "Words to look for" } },
            required = { "query" },
        },
    },
}

-- A tool's result as text, and whether it failed.
local function runTool(call)
    if call.name == "search_notes" then
        local lines = {}
        for _, hit in b.search(tostring(call.input.query), 10) do
            table.insert(lines, `[[{hit.page}]]: {hit.excerpt}`)
        end
        return #lines > 0 and table.concat(lines, "\n") or "Nothing found."
    end
    return "No such tool.", true
end

b.command("ask-notes", function(ctx)
    local question = b.ask("Ask your notes")
    if question == "" then
        return nil
    end
    local messages = { { role = "user", content = question } }
    for turn = 1, 6 do
        local text, reply = b.ai({ messages = messages, tools = tools, maxTokens = 2000 })
        if not reply.toolCalls then
            return text                                         -- the answer
        end
        table.insert(messages, { role = "assistant", raw = reply.raw })     -- its turn, verbatim
        local results = {}
        for _, call in reply.toolCalls do
            local content, failed = runTool(call)
            table.insert(results, { id = call.id, content = content, isError = failed })
        end
        table.insert(messages, { role = "user", results = results })        -- all results at once
    end
    return "No answer after six turns."
end, { label = "Ask my notes (AI)" })
```

* **Settings → Extensions → AI**, per device: the provider — **Claude** (Anthropic's Messages API;
  `claude-opus-5-5` unless a model is named; `anthropic-version: 2026-10-21`; a request one model's
  safeguards decline is answered by the model Anthropic recommends, `fallbacks: "default"`, for the
  models that have it) or **OpenAI-compatible** (OpenAI, OpenRouter, or a server of one's own —
  Ollama `http://192.168.1.10:11434/v1`, LM Studio, llama.cpp — which needs no key); the API key
  (in `<app data>/secrets/ai-key`, owner-only like the Nextcloud password, masked in logs, never in
  the notebook); the model; the address; **Check** ("Say OK."). In the browser the key is in the
  site's storage, and Anthropic's API is told the request comes from a page.
* **Subscriptions** (Claude Pro/Max, ChatGPT Plus) cannot be used by other apps: the providers bill
  their APIs separately, by use. On a phone that leaves an API key (a question about a note costs
  fractions of a cent), a server of one's own (free, private; reachable through Tailscale or the
  home network), or the providers' own apps with a connector to the notes (a remote MCP server —
  `brulion-cli mcp` speaks stdio only so far: TODO.md).
* **Limits**: at most 200 000 characters per request (a script cannot send the notebook by
  accident), 5 minutes per answer; only extensions approved with `ai` send anything, and what they
  send goes to the chosen provider.
* `src/core/ai/ai.h`: the request from b.ai's table, the HTTP request of each API and reading the
  answer are pure functions (`tests/core/ai`); `ai::Client` sends through `sync::HttpTransport`.

**The AI assistant** (built in, `src/core/extensions/builtin/asystent.luau`; Settings → Extensions →
AI, off by default) is an agent: "AI: ask or ask to do…" asks for a question or a request
(`b.ask`) and sends it with the page on screen (at most 30 000 characters) and eleven tools —
`search_notes` (`b.search`), `query_notes` (`b.query`, the query language described in the tool's
description), `list_pages`, `read_page`, the changes `append_to_journal`, `append_to_page`,
`write_page`, `update_block` (`b.replaceBlock`: a task's status too), and for writing an extension
`extension_reference` (this document, `b.data("reference")`: the list of its sections, or the
sections whose heading has the words asked for), `check_extension` (`b.checkExtension`) and
`write_extension` (checked first and refused while it does not load or when the page is no
extension; a new page goes to `Rozszerzenia/` / `Extensions/`). The model calls them as it needs,
at most sixteen turns, each result at most 12 000 characters (the reference 40 000). **Every change waits for the
user**: `b.choose` with what would be written (the question dialog shows the lines after the
first under its title, as plain text) — Allow, Allow every change of this question, No; a no (or
the dialog closed) goes back to the model as a declined change. An extension it wrote is code
"changed elsewhere" like any `b.write` of a page: it runs once the user approves it on the page. The answer goes into today's note —
`- 🤖 <question> ([[Page]])`, its lines as items under it, then `✏️ Changed: [[…]]` with the
pages changed; "Asking AI…" is said once per question. "AI: summarise this page" sums up the page
on screen in one request without tools. Both are a tap away on the top bar's ✨ button while the assistant is
on (docs/UI.md "Top bar": the page on screen, in the journal's stream today's note; a spinner while
`aiPending`). Like the gym, its source can run from a page of the notebook and be changed there: a
page tagged as an extension, with `permissions: [write, ai]`, that holds the source and is titled
"Asystent AI" / "AI assistant" (or has `overrides: asystent`) — copied by hand so far: Settings has
"Edit the code" for the gym only.

## Errors

An error ends the call. It names the extension — a page by its title — and the line of the page,
with a traceback in the console; the errors of `b`'s functions name the function too, at the
caller's line:

```text
Word count:14: attempt to index nil with 'text'
Word count:9: b.replaceBlock: stale: the block changed since it was read (Home.md#L7@1a2b3c4d)
```

* `pcall(f, …)` catches an error: `local ok, err = pcall(b.write, name, text)`. It may wrap a
  question or `b.ai` too (the call waits inside it as usual). What cannot be caught: the time
  limit and the memory limit.
* `error("No plan pages yet", 0)` raises one of your own (`0`: without the line).
* Where it shows: a command's or an action's as a message ("The extension failed: …"), a view's in
  the view's header ("The view could not be drawn: …"), a hook's in the console only, a loading
  error on the page's state line and in Settings → Extensions — with the line, and a button to it.
  Every error is also in the Luau console and in the event log.

| Message | Means |
|---|---|
| `b.write: permission denied: write` | the page has no `permissions: [write]` |
| `b.ai: permission denied: ai` | the same for `ai` |
| `b.replaceBlock: stale: …` | the line changed since its `ref` was read, or its page is gone: read again |
| `b.replaceBlock: not a block reference: …` | the `ref` is not one a block had: made by hand, without its hash, or naming a file that is no note |
| `b.replaceBlock: the text must be one line` | no line breaks in a block's first line |
| `b.blocks: the query needs a page or a tag` | `b.blocks({})` |
| `b.blocks: invalid date: …` | `from` or `to` is not `YYYY-MM-DD`, or no such day |
| `b.pages: argument 1 must be a table …` | `b.pages("project")`: the filter is `{ tag = "project" }` |
| `…: the view "…" returned nothing` | `render` must return a node of `b.ui` (or a list of them) |
| `b.query: …` | the query language's own message (an unknown filter, a wrong date) |
| `b.appendToJournal: invalid date: …`, `b.daysBetween: invalid date: …` | not `YYYY-MM-DD`, or no such day |
| `b.read: argument 1 must be a string, not nil` | a wrong argument, by its position |
| `b.ask: cannot wait for an answer here (inside a coroutine or a library callback)` | a question or `b.ai` where the call cannot be suspended |
| `…: an extension cannot ask a question while it loads` | `b.ask`, `b.choose`, `b.form` or `b.ai` at the top level |
| `b.ai: …` | the model's or the provider's: no key, no connection, HTTP errors in words, "the request is too long" |
| `value nested too deeply (a cycle?)`, `cannot pass a value of type function` | a table that cannot cross to the app (a control's `args`, a returned value) |
| `timeout: the extension ran too long` | more than 2 s in one call |
| `not enough memory` | more than 64 MB |

## Recipes

Complete pieces to start from; the documentation notebook's "Extension recipes" page has more.
The ones that write need `permissions: [write]`.

**A day more or less.** Dates are text; `os.time` and `os.date` do the arithmetic (in UTC, at
noon, so daylight saving never moves a day). Here: the task under the caret, due a day later.

```luau
-- "2026-11-13" and 1 → "2026-11-14"; day 32 rolls into the next month, -1 goes back.
local function addDays(date, n)
    local y, m, d = date:match("^(%d+)-(%d+)-(%d+)$")
    local noon = os.time({ year = tonumber(y), month = tonumber(m), day = tonumber(d) + n })
    return os.date("!%Y-%m-%d", noon)
end

b.command("postpone", function(ctx)
    local block = ctx.block
    local due = block and block.text:match("📅 (%d%d%d%d%-%d%d%-%d%d)")
    if not due then
        return "Put the cursor in a task with a due date (📅)."
    end
    local later = addDays(due, 1)
    local text = block.text:gsub("📅 %d%d%d%d%-%d%d%-%d%d", "📅 " .. later, 1)
    b.replaceBlock(block.ref, text, block.task)
    return `Due {later}`
end, { label = "Postpone the task by a day", block = true })
```

**A note from a few questions.** `b.choose`, `b.ask`, a new page in a folder and a link to it in
today's journal.

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
    local name = `{kind}s/{title}`                  -- "Meetings/Budget": the folder is made
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

**A chart of the journal.** The daily notes of two weeks through a query, their words counted, a
bar chart.

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
        table.insert(labels, day.date:sub(6))               -- "11-13"
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

**An error of your own, and one caught.**

```luau
b.command("careful", function(ctx)
    local ok, err = pcall(b.appendToJournal, "2026-02-30", "- never written")
    if not ok then
        print(err)                          -- "b.appendToJournal: invalid date: 2026-02-30"
    end
    local projects = b.pages({ tag = "project" })
    if #projects == 0 then
        error("Tag a page #project first", 0)   -- "The extension failed: Tag a page #project first"
    end
    return `{#projects} projects`
end, { label = "Errors, caught and raised" })
```

## Extensions in the notebook

A page is an extension when its page-level tags have `rozszerzenie` or `extension` (frontmatter
`tags:` or a `tags::` line; a `#rozszerzenie` inside a block does not count). Its code is every
```` ```luau ```` (or ```` ```lua ````) block, in order, also inside a list item as Logseq writes it;
prose, headings and other blocks around it are documentation (literate programming, as an
Emacs `config.org`). The page's properties (docs/SYNTAX.md §13), in English or Polish:

| Property | |
|---|---|
| `tags: [extension]` (`rozszerzenie`) | makes the page an extension |
| `permissions: [write, ai]` (`uprawnienia: [zapis, ai]`) | what it may do beyond reading: `write` (`zapis`) changes notes, `ai` asks a model. An unknown word is reported and grants nothing |
| `description: …` (`opis:`) | shown in Settings → Extensions |
| `overrides: silownia` (`zastępuje:`) | the built-in extension the page replaces |

* **Its id** is its title folded ("Licznik słów" → `licznik-slow`): commands are
  `ext.licznik-slow.<name>`. Two pages with one id: the first by path runs, the other says so.
* **Line numbers**: the code is loaded with every other line of the page blanked, so an error names
  the page's own line ("Licznik słów:14: attempt to index nil") — in Settings, in the page's state
  line, in the console and the event log. The page's state line has a button to that line.
* **Approval, per device.** Code can arrive from anywhere: a sync, an import, a shared folder. A
  page's code runs on a device only after it was approved there (Settings → Extensions, or "Run" on
  the page's state line) — like VS Code's workspace trust or Emacs' questions before `eval` of a
  file's local variables. What is approved is the SHA-256 of the code and the permissions, kept in
  the notebook's device state (`LocalStore` `extensions/approved`, never synced; the newest 300);
  prose around the code needs no new approval. Code written on this device stays approved: when
  the app's editor saves an approved page, the new code is approved too — unless it asks for more
  permissions. Code that changes any other way (a sync, another program, an extension's `b.write`)
  waits again, and until it is approved the extension does not run at all — neither the new code
  nor the one before it (a page that overrides a built-in leaves the built-in code running
  meanwhile). "New extension" and "Edit the code" of a built-in write pages that are approved at
  once.
* **Live.** Whenever a page changes, its code is loaded again (only that extension; while a
  question waits, after its answer); its commands and views follow, its variables start anew and
  its calls waiting for an answer are dropped. A save that does not load (a syntax error, an error
  at the top level) keeps the version before it running and shows the error: half-written code
  never takes a working extension away. "Reload extensions" (the palette) starts every extension
  in a fresh VM.
* **Its state** shows in the page's bottom-left corner and in Settings → Extensions: runs (with its
  commands, views and reactions counted), an error with its line, new or changed elsewhere ("Run"),
  off on this device, another page has the same name, the built-in one it replaces is off, no
  ```` ```luau ```` block yet.
* **Switched off** on this device in Settings → Extensions (`extensions/off`).
* **Built-ins as pages.** "Edit the code" of the gym (Settings → Extensions) writes its source into
  `Rozszerzenia/Siłownia.md` (`Extensions/Gym.md` in an English notebook): `zastępuje: silownia`,
  one ```` ```luau ```` block per section of the source under its heading. While that page exists
  and is approved, the gym runs from it — with the built-in's bundled data (`b.data`) — and a
  change there works at once; "Restore" puts the page in the trash and the built-in code runs
  again. A page titled like a built-in ("Siłownia", "Gym", "Asystent AI", "AI assistant")
  overrides it too, as TiddlyWiki's shadow tiddlers. The built-in's switch still decides whether
  it runs at all.

## Written by an AI agent

The built-in **AI assistant** writes one when asked ("a command that counts today's words"): it
reads this reference, checks the page and writes it with the user's yes ("AI" below). An agent
outside the app (Claude Code, Codex, Hermes …) writes and tries extensions with `brulion-cli` or its MCP
tools (docs/CLI.md "Extensions"), without the app:

| Step | Command (MCP tool `brulion_extension_…`) |
|---|---|
| read this reference, or a part of it | `extension docs`, `extension docs views` |
| see what there is, learn from a built-in | `extension list`, `extension show silownia` |
| try code over the notebook | `extension eval 'b.query("type:tasks status:open").total'` |
| check a page text before writing it | `extension check "Word count" --stdin` — the error with the page's `line` |
| write the page | `extension write "Word count" --code … --description … --permissions write` (or `--content`, the whole page); replacing one needs `--expect-hash` |
| try a command, view, action or hook | `extension run "Word count" count --page Home`, `--view today --args '{…}'`, `--hook save --page Home`, `--answers '["…"]'` |

Every check and run is a fresh engine with the limits below, over the notebook as it is, and
**writes nothing**: `NotebookHost`'s dry run (`Options::dryRun`) computes each write with the same
edits and returns it as a diff; `b.ai` gets an error; `print` comes back as `printed`. The page
`extension write` makes is code "changed elsewhere": it runs in the app only once the user approves
it there, like code from a sync — the command line never approves anything.

## The Luau console

The palette's "Luau console" (route `console`): a line of Luau runs in an environment of its own
with the notebook's `b` and the permissions to write and to use AI (what is typed there is the
user's own). An expression shows its value (`b.today()`, `#b.pages({})`; tables as Luau literals),
statements run, globals stay for the next line (Emacs' `ielm`); Up / Down go through the history.
Below, as in Emacs' `*Messages*`: what extensions `print()` and `b.log`, their errors with lines,
"loaded again". "Run Luau block" (the palette, in the editor) evaluates the ```` ```luau ```` block
under the caret there — a block of any page, with the page's line numbers in errors — and shows the
result as a message too (Emacs' `C-x C-e`). Code typed in the console may register commands and
views (`b.command` …) until the extensions are reloaded; it cannot see an extension's variables.

```text
> b.today()
2026-11-13
> #b.pages()
214
> b.page("Home").tags
{"project"}
> for _, t in b.query("type:tasks status:open due:today").items do print(t.page, t.text) end
Home	Pay the rent 📅 2026-11-13
> rent = b.search("rent")[1]
> rent.page .. ":" .. rent.line
Home:7
```

## Troubleshooting

| What happens | Why, and what to do |
|---|---|
| The command is not in the palette | The page's state line says why: an error (the version before it still runs, without the new command), "new or changed elsewhere" (press **Run**), off on this device, or another page with the same title. A command with `block = true` shows only while the caret is in an editor |
| "new or changed elsewhere" after every sync | The code was changed on another device: each device approves what it runs. Text around the code never asks again |
| It asks for approval after I added `permissions` | More permissions are a new approval, also on the device that wrote them |
| `permission denied: write` | `permissions: [write]` is missing in the page's properties (frontmatter or a `permissions::` line), or misspelt: Settings → Extensions shows "May: change notes" when it is understood |
| `attempt to index nil with …` | Something was not there: `b.read`, `b.page` and `ctx.block` are `nil` for what does not exist, a block has no `task` when it is no task, `ctx.page` is `""` outside a page |
| `stale:` | The line changed after it was read. In a view, pass `ref` and `text` in the control's `args`; in a command, read the block again |
| A check or a stepper does not show | They are drawn inside `b.ui.row` only |
| "The view could not be drawn: … returned nothing" | `render` ended without `return`, or returned a text: return `b.ui.column({ … })` |
| The view shows old numbers | `render` reads variables a hook or a command filled: read the notebook in `render` instead |
| Polish letters sort or match wrongly | `table.sort`, `%a` and `string.lower` are ASCII: `b.sorted`, `b.fold`, `utf8.len` |
| "The extension took too long" | One call has 2 s. Ask the index for less (`b.query` with filters and a limit instead of reading every page), do not read long text with backtracking patterns |
| Nothing is printed | `print` writes to the Luau console (the palette), not to a message; a command's message is the text it returns |
| A variable is gone | Variables live until the code is loaded again — every save of the page. Keep what must stay in a note |

## Sandbox and limits

| Limit | |
|---|---|
| Time | 2 s per call, per load and per continuation after an answer |
| Memory | 64 MB for all extensions of the notebook together |
| A view | 2000 lines, 24 controls in a row, 32 levels of columns |
| `b.blocks` | 20 000 blocks |
| `b.search` / `b.query` | 200 hits / 1000 items |
| `b.sorted` | 100 000 texts |
| `b.log`, `print` | 4000 characters a line |
| `b.timer` | 1 … 86 400 s |
| `b.ai` | 200 000 characters a request, 64 tools, 5 minutes an answer |
| Hooks | 256 in all; three failures in a row take one off |
| Values crossing | 64 levels of nesting |

* One VM per notebook (made again when the language changes or on "Reload extensions"; an extension
  page that changes is loaded again alone). Every extension has an environment of its own (its
  globals) whose reads fall through to the frozen standard library, and its own read-only `b`; the
  library tables, the string metatable and the globals are read-only (`luaL_sandbox`). Nothing
  reaches files, the network, processes or the environment: Luau's library has no `io`,
  `os.execute`, `require`, `load`, `string.dump`; `print` goes to the event log. Brulion compiles
  the source itself — scripts never see a compiler or a bytecode loader.
* **Notes only**: `b.write` makes and replaces pages (`.md` files; a name's leading dots and
  `..` are taken out), `b.appendToJournal` writes daily notes, and `b.replaceBlock` takes only the
  ref of a line of a note the index knows. No function reads or writes another file of the
  notebook's folder — `.git/config`, `.brulion/config.json`, `.obsidian/`, the attachments —
  whatever text a script passes as a name or a ref (`tst_extensions_host`: `refsNameNotesOnly`).
* **Time**: 2 s per call (and per resume and load). The VM's interrupt callback runs on the
  engine's own thread at loop back edges, calls, returns, every step of the pattern matcher and of
  the plain string search; past the deadline the call's thread is yielded (a yield never unwinds
  through `pcall`, so the script cannot catch it) and an error is raised wherever a yield is
  impossible. No watchdog thread: the same in the browser.
* **Memory**: 64 MB through the allocator; a refused allocation ends the call (sticky, not
  catchable either). After either limit the VM stays usable.
* **Errors** name the extension and the line (`silownia:412: …`, a traceback); errors of the host's
  functions too (`b.replaceBlock: stale: …`).
* Luau errors are `setjmp`/`longjmp` (`LUA_USE_LONGJMP=1`; the browser's Qt links without C++
  exception catching): the engine's C functions never keep a C++ object alive when they raise or
  yield — each is a thin wrapper around an implementation that returns first (`engine.cpp`).
* Values cross as `QVariant`s and never run script code on the way (no metamethods): a whole
  number arrives as an integer, a list as a `QVariantList`, a record as a `QVariantMap`.

## Layers

| Where | What |
|---|---|
| `3rdparty/luau` | Luau 0.743, the VM and the compiler only, patched (see its CMakeLists.txt): the plain `string.find` honours the time limit; numbers are read and written with `.` whatever the C locale (Qt sets it from the environment — with a Polish locale Luau's own `strtod` rejected `82.5`) |
| `src/core/extensions/engine.h` | `Engine`: one VM; extensions, their commands and views; calls, questions, limits |
| `src/core/extensions/host.h` | `Host`: what an extension may ask of the app (blocks, pages, writes, today, languages, the timer, the log) |
| `src/core/extensions/notebookhost.h` | `NotebookHost`: the Host over a `Vault` and its `VaultIndex` — blocks from the index, content-preserving writes (`blockedit.h`, `notebook/journals.h`, `notebook/pages.h`); a dry run that only computes them (`brulion-cli extension …`) |
| `src/core/extensions/sandbox.h` | `checkExtension()`, `checkExtensionPage()`: an extension loaded in an engine of its own over a dry-run `NotebookHost` — `b.checkExtension`, `brulion-cli extension check / write` |
| `src/core/extensions/builtin.h` | the built-in extensions (resources `:/extensions/…`), `builtinFor()`: which one a page overrides |
| `src/core/ai/ai.h` | AI for `b.ai`: the request from a script's table, the HTTP requests of Anthropic's Messages API and of OpenAI-compatible chat APIs, reading their answers and errors; `ai::Client` over `sync::HttpTransport` |
| `src/core/extensions/pageextension.h` | extension pages: which pages are extensions, their id, their code with the page's line numbers, permissions, the hash an approval covers; the new-extension page and a built-in's source as a page |
| `src/app/shell/workspaceextensions.h` | `WorkspaceExtensions` (QML `Workspace.extensions`): the engine for the open notebook, the built-ins the settings switch on, the extension pages and their approval on this device, reloading a page's code when it changes, hooks, the console, questions, messages and the rest timer |
| `src/cli/commands_extensions.cpp` | `brulion-cli extension …` (docs/CLI.md "Extensions"): this reference (bundled), the extension pages and their state on the device, a sandbox for checks and runs, writing a page |
| `src/app/shell/extensionmodels.h` | `ExtensionListModel` (Settings → Extensions), `ExtensionConsoleModel` (the console's lines) |
| `src/app/shell/extensionviewmodel.h` | `ExtensionView` (QML): a view's node tree as a list; `qml/shell/ShellExtensionView.qml` draws it |
| `qml/views/ConsoleView.qml`, `qml/views/ExtensionRouteView.qml` | the Luau console (route `console`), an extension page's view (route `extension`) |
| `qml/shell/ShellExtensionQuestion.qml` | the dialog of `b.ask` / `b.choose` / `b.form` |

A new function of `b`: its implementation in `engine.cpp` (a thin `api…` wrapper around an `…Impl`
that returns before anything is raised), what it needs of the app in `Host` (`host.h`,
`notebookhost.cpp`, the tests' fake hosts), a test in `tests/core/extensions`, and its line in the
three references — this document and the documentation notebook's "Extension API" page in both
languages; `tst_extensions_docs` fails until all three name it.

**Why Luau**: of eleven languages measured with the same extensions, tests and hostile scripts (the
extension language experiment, branch `experiment/extension-languages`), it was the only ready-made
one whose sandbox, time limit and memory limit held without changes — also in the single-threaded
browser build — and the fastest; its type checker (not vendored yet) finds mistakes with line and
column.

## Tests

`tests/core/extensions`: the engine (sandbox escapes, limits that cannot be caught, questions,
views, hooks, evaluation, the locale), block edits, extension pages (ids, code with the page's
lines, fences in lists and in other fences, permissions, the hash, the new-extension page and the
gym's source as a page running like the built-in), the gym on a real notebook (writing lines
back in Brulion's spelling, history, plans with progression, today's view and its actions, a stale
line, the catalog, hostile lines, two years of workouts), the AI assistant as an agent, and the
documentation (`tst_extensions_docs`: every ```` ```luau ```` example of this document, of
docs/SYNTAX.md and of the documentation notebook's pages in both languages loads and runs on a
real notebook — its commands with their questions and the model answered, its views and their
actions, its hooks —, both languages have the same examples, and the references name every
function of `b`, of `b.ui` and every hook event). The app's self-test: `checkGym` switches
the gym on, writes a plan into today's note through the dialog and adds a set in the view;
`checkExtensionPages` writes an extension page as a sync would (it waits), approves it, opens its
view from the palette, saves a change in the editor (it runs at once, still approved), saves a
broken one (the error on its line, the previous version runs) and evaluates in the console.
