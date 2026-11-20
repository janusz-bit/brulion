# Extensions (Luau)

Brulion is scripted in [Luau](https://luau.org), a small, fast Lua with optional types. An
**extension** is a script that adds commands to the palette, views of its own and reactions to
saves and changes, and that reads and writes the open notebook through one API, the table `b`.
Extensions come from two places:

* **the notebook itself** — pages tagged `#extension` (`#rozszerzenie`) whose ```` ```luau ````
  blocks are code (docs/SYNTAX.md §13): the notebook carries its own code, edited, synced, linked
  and versioned like any note. Saved, a page's code runs again at once;
* **built in** — the **gym** (`src/core/extensions/builtin/silownia.luau`, docs/SYNTAX.md §5 "Gym",
  docs/UI.md "Gym"), the **habits** (`nawyki.luau`, docs/SYNTAX.md §5 "Habits", docs/UI.md
  "Habits"; on by default, and `brulion-cli habit …` runs it too) and the **AI assistant**
  (`asystent.luau`, "AI" below), switched on and off in Settings → Extensions and written with the
  same API: nothing of them is C++ but the views' QML headers.

The idea is Emacs' and TiddlyWiki's: the program you use is made of code you can read and change
from inside it. Emacs gives every user an init file and `eval-buffer`, a `*scratch*` buffer and a
`*Messages*` log; TiddlyWiki keeps its own code in tiddlers, and a tiddler with a built-in one's
title replaces it (a "shadow tiddler"). Brulion has the same pieces: extension pages, the Luau
console, and the source of the built-in gym or habits as a page that then runs instead of the
built-in code.

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
| look a function up | [The API (`b`)](#the-api-b), [Commands](#commands), [Views](#views), [Hooks](#hooks), [Questions](#questions), [Settings](#settings), [Timers](#timers), [Shared code](#shared-code), [AI](#ai-bai) |
| copy something that works | [Recipes](#recipes) |
| find out why it does not work | [Errors](#errors), [Troubleshooting](#troubleshooting), [The Luau console](#the-luau-console) |
| know what a page may do and when it runs | [Extensions in the notebook](#extensions-in-the-notebook), [Sandbox and limits](#sandbox-and-limits) |
| use an extension in another notebook | [Sharing between notebooks](#sharing-between-notebooks) |
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
  see or change another's, the standard library or `b` itself. Code shared between extensions
  ("Shared code") runs in each of them as a copy of its own.
* **What the top level returns** is no result: a page of shared code returns its table there,
  which its own load ignores and `b.require` gives the extensions that use it.

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

* **Types are checked on save** (desktop and Android: "Types" below): a function of `b` that is
  not there, an unknown global, a wrong count of arguments show as warnings with their line before
  the code runs; `--!strict` checks the annotations too. The code runs whatever they say; in the
  browser nothing checks them.
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
  should. `b.now()` is the local date and time, `b.addDays` / `b.addMonths` move a date
  ("Helpers"); `os.date` and `os.time` follow the system's clock, not the app's.
* **Patterns backtrack**: `^(%d+[,.]?%d*)%s*(%a+)` on a line of 20 000 digits is quadratic
  (seconds, then the time limit). Read long input in anchored steps (`silownia.luau`,
  `readNumber`).

## Types

Every time an extension page's code changes, Luau's own type checker (its Analysis library,
`3rdparty/luau/Analysis`) reads it against the declarations of `b` (`src/core/extensions/builtin/b.d.luau`, the API of this
document as Luau types) and says what is wrong before the code runs: a **warning** with the page's line, in the page's state line ("runs ·
2 type warnings, line 14: Unknown global 'prnt'", with a button to that line), in Settings →
Extensions and in the console. The code loads and runs whatever the warnings say — they point at
mistakes, they never stop a page.

* **Nonstrict** by default, as Luau checks code without annotations: unknown globals and
  functions (`b.raed`, `b.ui.colum`), a call with a wrong count of arguments, a `nil` called.
  `--!strict` as the first line checks every annotation too (`local n: number = b.today()` is a
  warning there); `--!nocheck` turns it off for a page.
* **Lints** that point at mistakes: code that cannot be reached, a local used before it has a
  value, a duplicate key of a table, a format string that does not fit its values, `a and b or c`
  that can mislead … (not the ones about style: unused locals, shadowing).
* `b.checkExtension` and `brulion-cli extension check` give them too (`types`: `[{line, column,
  message, kind}]`; `kind` "type" or the lint's name), so an AI writing an extension sees them.
* **Bounded**: 2 s per check (then "ran out of time: the rest is not checked"), on a worker thread
  of its own — never the GUI's —, at most 50 warnings; hostile code (nested thousands of times,
  fifty thousand lines) ends the check, never the app (`tst_extensions_types`). The checker runs no
  code: Luau's old solver, the default, has no user-defined type functions, and nothing is required.
* **Where.** Built with `BRULION_LUAU_CHECK` (on by default, `src/core/extensions/CMakeLists.txt`):
  in the desktop apps and the command line, and on Android. **Not in the browser**: the checker
  reports its own trouble with C++ exceptions, which the browser's Qt cannot catch (a hostile page
  would end the tab), and it would add 2.6 MB to the WebAssembly module (0.8 MB compressed) of a
  download the browser has to fetch before anything shows. What it adds, measured on 2026-11-20 as
  a small program with Luau's VM and compiler against the same with the checker linked in (`-O2`,
  sections collected, stripped; the sizes compressed with `gzip -9`, as a download or an APK
  stores them):

  | Platform | VM + compiler | with the type checker | added (compressed) |
  |---|---|---|---|
  | desktop (x86_64, Clang) | 1.17 MB | 5.22 MB | 4.0 MB (1.7 MB) |
  | Android (arm64, GCC's code as an estimate of the NDK's Clang) | 1.31 MB | 4.99 MB | 3.7 MB (1.6 MB) |
  | browser (wasm32, Emscripten, wasm exceptions) | 0.85 MB | 3.42 MB | 2.6 MB (0.8 MB) |

  On the desktop and on Android that is a few per cent of the app, for warnings the code gets on
  every save; in the browser the cost is a slower first load and a tab a hostile page could end,
  so `BRULION_LUAU_CHECK` is off there (`typecheck_none.cpp`: no warnings, the code runs as
  everywhere).

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
| `ref` | where the block is: `"Journal/2026-11-11.md#L3@1a2b3c4d"` (the file, the line, a hash of the line's text). Give it to `b.replaceBlock`, `b.insertBlock`, `b.removeBlock`, `b.setTask`, `b.setProperty`; do not take it apart. It goes **stale** when the line changes |
| `page` | the page's name |
| `path` | the page's file in the notebook, `"journals/2026-11-11.md"` (`b.blocks` and `ctx.block` only) |
| `line` | the block's first line in that file, counted from 1 — what `b.ui.link`'s `line` takes (`b.blocks` and `ctx.block` only) |
| `date` | `"2026-11-11"` when the page is a daily note, else absent |
| `text` | the block's first line as written, without its indentation, list marker and checkbox: `"Pay the rent 📅 2026-11-13 #home"` — tags, dates and links are still in it |
| `plain` | its words as a reader takes them: the text without a task's fields (dates, priority, ids …), tags left out, `[[links]]` as their labels (the alias, else the target's last part), white space single — `"Pay the rent"`. What `b.plain` gives of a line, without the task's fields |
| `task` | the checkbox's character when the block is a task: `" "` open, `"x"` done, `"/"` in progress, `"-"` cancelled, `">"` deferred (a Logseq keyword — `TODO`, `DONE` … — as its character); absent otherwise |
| `tags` | the block's own tags, lower case, without `#` |
| `props` | its properties (`key:: value` lines under it) as `{ key = "value" }`, keys in lower case, a list joined with `, ` |
| `children` | the blocks indented under it, each the same way (`b.blocks` and `ctx.block` only) |

### Reading

Reading needs no permission.

| Function | Gives |
|---|---|
| `b.pages()`, `b.pages({ tag = "project" })` | the names of all pages (daily notes too), sorted as `b.sorted` sorts; with `tag` only the pages whose own tags — the frontmatter's `tags:` or a `tags::` line — have it (with or without `#`, any case). The filter is a table: `b.pages("project")` is an error, not every page |
| `b.pages({ prop = "habit" })`, `b.pages({ prop = { "habit", "nawyk" }, tag = … })` | the pages whose page properties (the frontmatter, or `key:: value` lines at the page's top) have one of the keys (any case); with `tag` the pages that have both |
| `b.pages({ paths = true })`, `b.pages({ tag = "project", paths = true })` | the same pages as their paths in the notebook (`"Projects/Home.md"`), sorted: two pages of one name in different folders are one name in the list of names, two paths here. Every function that takes a page takes such a path (`b.read`, `b.page`, `b.blocks({ page = … })`, `b.write` …). `paths` that is no boolean is an error |
| `b.page(name)` | `{ name, path, tags, props, date? }` — its file in the notebook (`"Projects/Home.md"`), the page's own tags and properties (as a block's `props`) — or `nil` |
| `b.read(name)` | the page's whole Markdown text, or `nil` |
| `b.blocks({ page = name })` | the page's top-level blocks in order, each with its `children`: list items, but also headings (`text` is `# Home`, with its hashes), paragraphs, code blocks (the fence line) and the frontmatter (`---`, its `props` the page's). `{}` for a page that does not exist |
| `b.blocks({ tag = "workout", journal = true, from = "2026-11-01", to = "2026-11-30" })` | every block that carries the tag itself (not through its parent or its page), each with its `children`: first those of ordinary pages (by path), then those of daily notes, oldest day first. `journal = true` — or a `from` / `to` day, both optional and inclusive — keeps daily notes only; a `from` or `to` that is no day (`"2026-11-31"`) is an error. At most 20 000 blocks |
| `b.blocks({ prop = "habit" })`, `b.blocks({ prop = { "habit", "nawyk" }, tag = …, journal = …, from = …, to = … })` | every block that has one of the properties among its own (`habit:: …` under it; the frontmatter never — a page's properties are `b.pages`'), in the same order and with the same `journal` / `from` / `to`. With `tag` the blocks that have both. With `page` (`{ page = …, prop = … }`, and `{ page = …, tag = … }` too) only that page's matching blocks, at any depth — not its top level |
| `b.search(text, limit?)` | full-text search as the search view does it (words in any form, diacritics optional, the best first): one hit per page, at most `limit` (20 by default, 200 at most), each `{ page, date?, excerpt, line?, ref?, text?, task? }` — `line` the line that matches best (the most of the query's words; the first of those lines), counted from 1 (none when only the title matched), `ref` / `text` / `task` of the block holding that line |
| `b.query(source, limit?, offset?)` | a query of the query language (docs/SYNTAX.md §10): `{ total, items }` with at most `limit` items (50 by default, 1000 at most; a smaller `limit:` in the query wins) from item `offset` on (0, the first, by default): `total` counts them all, so a script pages through any number 1000 at a time. Items are blocks without `children`, `path` and `line`, plus a task's `due`, `scheduled`, `start` and a reminder's `reminder` (`"2026-11-12 09:00"`); for `type:pages` they are `{ page, date? }`. Weeks (`due:this-week` …) start on the notebook's first day of the week (`b.weekStart()`). A query that is none is an error with the reason |
| `b.attachments()` | the notebook's files that are no notes — pictures, PDFs, any other file —, as paths in the notebook (`"assets/receipt.jpg"`), sorted; nothing of the hidden and ignored folders |
| `b.revision()` | a number that changes whenever a note changes — the editor, a sync, any extension's write: what a script computed from the notebook stays true while it is the same (the habits keep their statistics so) |
| `b.checkExtension(name, text)` | loads `text` as the extension page `name` would load (a new page: where new pages go), in an engine of its own — its top-level code runs with 1 s and 32 MB, writes only computed, nothing stays registered, no approval needed: `{ ok, id, title, path, permissions, problems, commands = { { name, label, block } }, views = { { name, label, icon, actions } }, hooks = { events… }, printed, writes?, types }` — `types` what the type checker finds (`{ { line, column, message, kind } }`, "Types"; empty where there is none), or `ok = false` with `error`, `line` (the page's line, from 1) and `status` (`"timeout"` …). Text that is no extension page (no tag, no code) has `ok = false` too. Inside such a check it reports an error instead of checking again |

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
-- Pages of one name in different folders ("Projects/Home", "Archive/Home") are told apart by path.
local lines = {}
for _, path in b.pages({ tag = "project", paths = true }) do
    local page = b.page(path)
    local open = 0
    for _, block in b.blocks({ page = path }) do
        open += if block.task == " " then 1 else 0
    end
    table.insert(lines, `{page.path}: {open} open`)
end
return table.concat(lines, "\n")
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

```luau
-- Every open task, 100 at a time, and the blocks that define a habit, with their files' lines.
local words, offset = {}, 0
repeat
    local found = b.query("type:tasks status:open sort:due", 100, offset)
    for _, task in found.items do
        table.insert(words, task.plain)
    end
    offset += #found.items
until #found.items == 0 or offset >= found.total
local habits = {}
for _, block in b.blocks({ prop = { "habit", "nawyk" } }) do
    table.insert(habits, `{block.plain} ({block.path}:{block.line})`)
end
return `{#words} open tasks; habits: {table.concat(habits, ", ")}`
```

### Writing

Writing needs the permission `write` (`permissions: [write]` on the page; the console has it).
Every write goes through the notebook like the editor's: the whole file is written, every other
line stays byte for byte, the index, the views and a sync follow.

| Function | Does |
|---|---|
| `b.write(name, content)` | replaces the page's whole text, or creates the page where new pages go (`"Meetings/Budget"` makes the folder). Returns `true`. Read first what you do not want to lose |
| `b.appendToJournal(date, markdown)` | adds `markdown` as written — start an item with `- ` — at the end of that day's note. A missing note is created from the journal template; a template's empty last bullet is filled instead of left above |
| `b.appendToPage(name, markdown)` | the same at the end of any page — `markdown` its last block, an empty last bullet filled —, without reading and writing the whole page; a page that does not exist is made where new pages go |
| `b.replaceBlock(ref, text, task?)` | gives the block's first line the new `text` (one line): its indentation and list marker stay, and so do the Obsidian Tasks dates (`✅ ❌ ➕ 📅 ⏳ 🛫`, a date's time too) wherever they stand — before a tag as well, not in code — and the block id at its end, unless `text` brings its own: they go after the new text, in their order. `task` — one character, as a block's `task` — sets the checkbox as the editor does (`"x"` adds `✅` with today's date; a plain line becomes a task). Returns the block's new `ref` |
| `b.insertBlock(ref, markdown, where?)` | puts the lines of `markdown` `"after"` the block and its children (the default), `"before"` it, or under it as its last `"child"`: every line gets the block's indentation, a child also its first child's indentation beyond it (else one level: a tab where the block is indented with tabs, two spaces otherwise), and new lines end as the note's do (a note without a final line break keeps having none). Returns the `ref` of the first new line |
| `b.removeBlock(ref)` | removes the block with its children; the lines around it stay as they are |
| `b.setTask(ref, status)` | the checkbox alone, as a click sets it: `status` one character (`"x"`, `" "`, `"/"`, `"-"`, `">"`); done adds `✅` today, cancelled `❌` today, leaving them takes those away, a Logseq keyword stays a keyword (`DONE`, `CANCELED`), a recurring task gets its next occurrence (docs/SYNTAX.md §5), a plain line becomes a task. The same status again writes nothing. Returns the block's `ref` |
| `b.setProperty(ref, key, value)` | the block's property `key` (letters, digits, `-`, `_`): an existing `key:: …` line (any letter case) gets `value`, else a line `key:: value` goes after the block's last property line — or right under its first line —, indented as its other properties, else under its text. `value` is one line of text or a number; `nil` or `""` removes the property's lines. Returns the block's `ref` |

These calls never write over what they did not see: when the line changed since `ref` was read —
a sync arrived, another pane edited it — the call is an error with `stale:` in it and nothing is
written (the same text up to 50 lines away still counts as the same line: lines added above do not
matter, so the refs of a page's other blocks still work after an insert or a removal). Read the
block again and decide again. A `ref` is taken as a block had it — a note of the notebook, a line,
the hash of the line's text: one without its hash, or with the path of a file that is no note
(`.git/config`, a file among the attachments), is `not a block reference`. A ref that comes back
(`b.replaceBlock`, `b.setTask`, `b.setProperty`: the block's; `b.insertBlock`: the new line's) is
the one to use next.

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

```luau
-- The task under the caret gets a first step under it and a property; its step is in progress.
b.command("split", function(ctx)
    local block = ctx.block
    if not block or not block.task then
        return "Put the cursor in a task."
    end
    local step = b.insertBlock(block.ref, "- [ ] The first step", "child")
    b.setTask(step, "/")
    b.setProperty(block.ref, "split", b.today())    -- block.ref still names it: its line is the same
    return `A first step under “{block.plain}”`
end, { label = "Split the task", block = true })

-- The page's done tasks without children move to the page "Archive" (made when missing).
b.command("archive", function(ctx)
    if ctx.page == "" then
        return "Open a page first."
    end
    local moved = 0
    for _, block in b.blocks({ page = ctx.page }) do
        if block.task == "x" and #block.children == 0 then
            b.appendToPage("Archive", `- [x] {block.text} (from [[{ctx.page}]])`)
            b.removeBlock(block.ref)                -- the refs after it still work
            moved += 1
        end
    end
    return `{moved} done tasks are in [[Archive]]`
end, { label = "Archive the done tasks" })
```

### Helpers

| Function | Gives |
|---|---|
| `b.today()` | today as `"2026-11-13"` (the device's local day) |
| `b.now()` | the local date and time, `"2026-11-13 09:05"` — the app's clock, as `b.today()` (`brulion-cli --now` sets both; `os.date` and `os.time` are the system's) |
| `b.daysBetween(a, b)` | the days from date `a` to date `b` (negative when `b` is earlier); an invalid date is an error |
| `b.addDays(date, n)` | the date `n` days later (`n` < 0: earlier): `b.addDays("2026-12-31", 1)` → `"2027-01-01"`. `n` a whole number; an invalid date is an error |
| `b.addMonths(date, n)` | `n` months later or earlier, a day that the month lacks its last: `b.addMonths("2026-01-31", 1)` → `"2026-02-28"` |
| `b.weekday(date)` | the date's day of the week, 1 (Monday) … 7 (Sunday) |
| `b.weekStart()` | the notebook's first day of the week (its setting, `weekStartsOn`), 1 … 7 as `b.weekday` |
| `b.weekdayName(n, form?)`, `b.monthName(n, form?)` | the name of weekday `n` (1 … 7, Monday first) or month `n` (1 … 12) in the interface's language: `form` `"full"` (the default: `"środa"`, `"listopad"`), `"short"` (`"pn"`, `"Mon"`) or `"of"` — the Polish genitive, as in a date (`"listopada"`) |
| `b.sorted(list)` | a new list of the same texts in alphabetical order as Brulion sorts names: Polish letters in their places, any case, numbers by value (`"Chapter 2"` before `"Chapter 10"`). Texts only, at most 100 000 |
| `b.fold(text)` | the text in lower case without diacritics, spaces trimmed and single: `"  Leżąc  NA ławce "` → `"lezac na lawce"`. For comparing and searching |
| `b.lower(text)` | the text in lower case, every alphabet's letters (`string.lower` knows ASCII only), diacritics kept: `"ŻÓŁW Łódź"` → `"żółw łódź"` |
| `b.plain(text)` | the words of a line, as a block's `plain`: its first line, tags left out, `[[links]]` as their labels, white space single — `"Czytać [[Książki/Diuna]] #wieczór"` → `"Czytać Diuna"` (a task's dates stay: they are words of a text, fields only of a block) |
| `b.parseRecurrence(text)` | a recurrence as Brulion reads one (docs/SYNTAX.md §7, Polish or English): `{ every = "day" \| "week" \| "month" \| "year", interval, weekdays?, monthDay?, lastDay?, workdays?, whenDone? }` — `"co 2 tygodnie w poniedziałek i piątek"` → `{ every = "week", interval = 2, weekdays = { 1, 5 } }` —, or `nil` |
| `b.parseWeekdays(text)` | a list of weekdays as people write it — `"pn, śr i pt"` → `{ 1, 3, 5 }`, `"mondays and fridays"` → `{ 1, 5 }` —, or `nil` when any of them is no weekday |
| `b.formatRecurrence(rule, lang?)` | such a rule in words, in `lang` (`"pl"` or `"en"`, the interface's by default): `"every week on Monday, Friday"`. A rule that is none is an error |
| `b.formatNumber(n, decimals?)` | the number as text with at most `decimals` decimals (2 by default, 10 at most), trailing zeros dropped, with a decimal comma in a Polish notebook: `82,5`, `100`. Not a number: `—` |
| `b.lang()` | the language to write notes in: `"pl"` or `"en"` |
| `b.uiLang()` | the interface's language (labels, messages): `"pl"` or `"en"`. The app gives both the interface's language today; ask each for what it means |
| `b.log(text)`, `print(…)` | a line in the Luau console and in the event log, under the extension's name (at most 4000 characters). `print` joins its values with tabs and writes a table as `table`: build the text yourself |
| `b.status(text?)` | what the running command is doing now, for the user to see while it waits (a question, a model): "Reading “Home”…". The AI chat shows it under the AI's dots (the assistant says what each tool does); elsewhere nothing shows it yet. Whitespace made single, at most 300 characters; `""` or nothing clears it. Not a text: an error |
| `b.timer(seconds, label?)`, `b.timer(seconds, { label, message })` | starts the extension's own timer (1 … 86 400 s): its countdown shows in a banner under the top bar in every view; at the end a notification "`label` is over" with `message` (else the app's words), the message as a toast, and the extension's `"timer"` hooks ("Timers") |
| `b.stopTimer()` | ends the extension's timer without a word |
| `b.data(name)` | read-only data bundled with a built-in extension (the gym's `b.data("exercises")`, the AI assistant's `b.data("reference")` — this document's text); `nil` for extension pages — unless the page overrides a built-in, which keeps the built-in's data |

```luau
-- This week from the notebook's first day of the week, and a schedule in words.
local today = b.today()
local first = b.addDays(today, -((b.weekday(today) - b.weekStart() + 7) % 7))
local days = {}
for i = 0, 6 do
    local day = b.addDays(first, i)
    table.insert(days, `{b.weekdayName(b.weekday(day), "short")} {tonumber(day:sub(9))}`)
end
local rule = b.parseRecurrence("co 2 tygodnie w poniedziałek i piątek")
return `{table.concat(days, ", ")}; {b.formatRecurrence(rule, "en")}; {b.lower("ŻÓŁW")}`
```

## Commands

`b.command(name, fn, { label = …, block = …, palette = …, key = … })` adds a command to the
palette. `name` is its id within the extension (the palette's id is `ext.<extension>.<name>`);
`label` is what the palette shows (else the name), with the extension's title under it.
Registering a name again replaces it. Every command of the palette is in **Settings → Keyboard
shortcuts** too (as "<extension>: <label>", listed once the extension has loaded), where any key
chord can be given to it; **`key = "Ctrl+Alt+W"`** suggests one — taken only while no command of
the app has it by default, it types no text and the editor does not use it (ShortcutMap's rules),
and no command of an extension listed before took it. A shortcut given in the settings stays while
the extension is off. With **`palette = false`** the palette does not offer it: a view's `b.ui.run` button,
`brulion-cli extension run` or the app runs it — the AI chat runs the AI assistant's `chat` so
("AI"), `brulion-cli habit …` the habits' `list` and `set`; any other value of `palette` changes
nothing.

The function gets one table, `ctx`:

| Field | |
|---|---|
| `ctx.page` | the name of the page on screen, `""` when the view on screen is no page (the journal's stream, Tasks, a view of an extension) |
| `ctx.block` | with `block = true`: the block under the caret (above, "Blocks") |
| `ctx.args` | what the caller passes the command, a record: a toast's button (below), `brulion-cli extension run … --args '{"name": "Reading"}'`, the app's own callers (`brulion-cli habit set` runs the habits' `set` with `{ name, state, date }`); `{}` when nothing is passed. A turn of the AI chat has none: `ctx.args or {}` |
| `ctx.selection` | what is selected in the editor on screen when the command runs: the text selected in the block being edited (its Markdown as typed), else the blocks selected, as Markdown with their children; `""` when nothing is (or no editor is on screen, or the Markdown source is). At most 200 000 characters |
| `ctx.messages` | only in a turn of the AI chat (the AI assistant's `chat`, "AI"): the conversation's messages `{ role = "user" \| "assistant", content }`, the user's last |
| `ctx.conversation` | only in a turn of the AI chat: the name of the conversation's own page (`"AI/2026-11-16 Co mam jutro"`) |

With **`block = true`** the command works on the block under the caret: the palette offers it only
in an editor (not in the Markdown source), and the page is saved first, so the block is read as
shown. `ctx.block` can still be missing (the caret's line is no block of the saved page): check
it.

What the function **returns** is its message: a text is shown as a message (a toast), and so is a
table `{ message, tone?, button? }` — `tone` `"success"`, `"warning"` or `"error"` (else plain
information) colours it, and `button = { label, page | command | action, args? }` puts a button on
it: `page` opens that page (made when it does not exist), `command` runs one of the extension's
commands with `args` as its `ctx.args`, `action` — in what a view's action returns — runs that
view's action again with `args` (the habits' **Undo**). Each button works once, while its toast
shows. Anything else (or nothing, a table without `message`) is silent; `brulion-cli extension run`
gives back whatever was returned as its `value`. An error is shown as "The extension failed: …"
and written, with its traceback, to the console. A command may ask ("Questions") and wait for a
model ("AI").

```luau
-- A note for a page the caller names (ctx.args.page), else for the page on screen.
b.command("remark", function(ctx)
    local args = ctx.args or {}
    local page = args.page or ctx.page
    if page == "" then
        return { message = "Open a page first.", tone = "warning" }
    end
    b.appendToPage(page, `- {b.now()} checked`)
    return {
        message = `Noted on {page}`,
        tone = "success",
        button = { label = "Open", page = page },
    }
end, { label = "Note that the page was checked" })
```

```luau
-- The selection as a quote in today's journal, with the page it came from.
b.command("quote", function(ctx)
    if ctx.selection == "" then
        return { message = "Select some text first.", tone = "warning" }
    end
    local from = if ctx.page ~= "" then ` ([[{ctx.page}]])` else ""
    b.appendToJournal(b.today(), `- > {ctx.selection:gsub("%s+", " ")}{from}`)
    return "Quoted in today's journal"
end, { label = "Quote the selection in the journal", key = "Ctrl+Alt+Q" })
```

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
| `b.ui.card(children)` | its children — lines, rows, a month — as one card: they stay lines of the view, each drawn as its part of the card (a card in a card goes on as the outer one) |
| `b.ui.heading(text)` | a heading |
| `b.ui.text(text, muted?)`, `b.ui.text(text, { muted?, icon?, tone? })` | a paragraph that wraps; `muted = true` in the secondary colour; `icon` one of the app's icons before it (`"flame"`); `tone` `"success"`, `"danger"` or `"accent"` its colour |
| `b.ui.link(page, label?, line?)` | a link that opens the page (and makes it when it does not exist) — at its `line`, counted from 1 as a block's `line` |
| `b.ui.button(label, action, args?)` | a button that calls the view's action `action` with `args` |
| `b.ui.menu(items, label?)` | a "⋯" button whose menu lists `items` (below); `label` is its name for a screen reader and its tooltip ("More" without one). On a line of its own or in a row |
| `b.ui.row(children)` | a card whose controls sit side by side and wrap: `check`, `stepper`, `text`, `link`, `button`, `menu`, `open`, `run` (at most 24) |
| `b.ui.check(checked, action, args?, { menu?, label? })` | the tasks' checkbox; a tap calls the action. `checked` is `true` (done), `false` (open) or `"skipped"` (a cancelled task's look); `menu` — items, below — opens on a right-click or a long press; `label` names it for a screen reader ("Reading: done"). **Only inside a row** |
| `b.ui.stepper({ value, step, unit?, action, args? })` | `− value +`: a tap calls the action with `args` and the new `value` (never below 0; `step` 1 by default; shown with at most two decimals and the unit). The number can be typed in as well (`82,5` or `82.5`, the unit may follow; Enter or leaving the field): the action gets it the same way, only a number of at least 0 that differs from the one shown. − / + from a number between two steps go to the next step's value (`31` with a step of 2,5: 30 or 32,5). **Only inside a row** |
| `b.ui.table(columns, rows)` | a table: `columns` a list of headings, `rows` a list of lists of cells (text; format numbers yourself). Made for up to four columns: the second is the widest, from the third on they are right-aligned |
| `b.ui.chart({ kind, title?, labels, series })` | a chart drawn like the notes' charts: `kind` `"line"` (the default) or `"bar"`, `labels` a list of texts, `series = { { name = …, values = { … } } }` with a value per label (`nil` leaves a gap) |
| `b.ui.month({ month, days, label? })` | a month as a grid, a cell per day under the weekdays' heads (from the notebook's first day of the week), today ringed: `month` `"2026-11"`, `days = { ["2026-11-03"] = { state, action?, args?, menu? } }` — `state` `"done"` (the success tint), `"missed"` (the danger tint), `"skipped"` (struck out), `"due"` (a thin ring), `"rest"` (nothing due: the default), `"future"` or `"outside"` (faint); a day with an `action` is a button that calls it with its `args`, its `menu` opens on a right-click or a long press. `label` is a title above it |
| `b.ui.empty(title, text, icon?)` | an empty state: an icon (`icon`, one of the app's; else the gym's dumbbell), a title and a sentence |
| `b.ui.open(label, view, params?)` | opens another view of the extension (`""`: this one) with `params` — a link in a row, a "back"-like button on its own |
| `b.ui.run(label, command)` | a button that runs one of the extension's commands (`ctx.page` is `""` there) |

**Menus** — of `b.ui.menu`, of a check, of a month's day — are lists of items, each `{ label,
action, args?, icon? }` (the view's action), `{ label, page, line? }` (opens the page, at its line
counted from 1) or `{ label, journal = "2026-11-03" }` (that day's daily note); an item without a
label or without any of those is left out, and the app gives a page and a journal item its icon
when it has none.

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

**When it is drawn.** `render` runs when the view opens, after every command and action — at
once after an action of its controls —, and whenever a note changes anywhere (the editor, a sync).
It should be quick, read what it shows from the notebook each time, and neither write nor wait:
`b.ask` and `b.ai` there end the drawing with "A view cannot wait for a question or for AI". Drawn
again with nodes of the same kinds in the same order, the view changes in place: a number being
typed into a stepper keeps its field. What takes long to compute may be kept in a variable while
`b.revision()` is the number it was computed at — the habits read their thousands of checks again
only then.

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

```luau
-- The journal's month: a day with a note is done, a past one without a note missed; each open
-- task due that month in a row with menus, and a tick that says so with Undo.
local cached = { revision = -1, month = "", days = {}, written = 0 }

local function monthDays(month)
    if cached.revision == b.revision() and cached.month == month then
        return cached.days, cached.written                     -- no note changed since
    end
    local days, written, day = {}, 0, month .. "-01"
    while day:sub(1, 7) == month do
        local state = if day > b.today() then "future" elseif b.page(day) then "done" else "missed"
        written += if state == "done" then 1 else 0
        days[day] = { state = state, menu = { { label = "Open the journal", journal = day } } }
        day = b.addDays(day, 1)
    end
    cached = { revision = b.revision(), month = month, days = days, written = written }
    return days, written
end

b.view("journal-month", function(params)
    local month = params.month or b.today():sub(1, 7)
    local days, written = monthDays(month)
    local lines = {
        b.ui.heading(`{b.monthName(tonumber(month:sub(6, 7)))} {month:sub(1, 4)}`),
        b.ui.card({
            b.ui.text(`{written} days written`, { icon = "book-open", tone = "accent" }),
            b.ui.month({ month = month, days = days }),
        }),
    }
    local last = b.addDays(b.addMonths(month .. "-01", 1), -1)
    for _, task in b.query(`type:tasks status:open due:{month}-01..{last} sort:due`, 20).items do
        local late = task.due < b.today()
        table.insert(lines, b.ui.row({
            b.ui.check(false, "set", { ref = task.ref, status = "x" }, {
                label = task.plain,
                menu = { { label = "Cancel it", action = "set", args = { ref = task.ref, status = "-" } } },
            }),
            b.ui.text(task.plain),
            b.ui.text(task.due, { muted = not late, tone = if late then "danger" else nil }),
            b.ui.menu({ { label = "Open its page", page = task.page }, { label = "Tasks", page = "Tasks", line = 5 } }),
        }))
    end
    local previous = b.addMonths(month .. "-01", -1):sub(1, 7)
    table.insert(lines, b.ui.open("Previous month", "", { month = previous }))
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

## Hooks

`b.on(event, fn)` calls `fn` with one table when something happens (Emacs' `add-hook`):

| Event | When | `e` |
|---|---|---|
| `b.on("save", fn)` | this device's editor saved a page (right after the save) | `{ page, path }`: the page's name and its file in the notebook |
| `b.on("change", fn)` | notes changed — the editor, a sync, another program, an import, **another extension**; once a burst of changes is over (0.6 s) | `{ pages, paths, by, sources }`: the names and the paths changed (a deleted page too: `b.read` gives `nil`; two pages of one name are one name, two paths), `by` the other extensions among the writers (their ids), `sources` where the changes came from: `"app"` (this device's app: the editor, a capture, a daily note opened), `"extension"`, `"sync"`, `"other"` (another program, an import) |
| `b.on("task", fn)` | a task's status changed — ticked, opened again, cancelled, in progress — anywhere, as `"change"` | the task as a block (`ref`, `page`, `path`, `line`, `text`, `plain`, `task`, `date?`) with `previous` (its status before), `source` and `by` (the extension that wrote it, else `""`). A task found again by its text without the status and the done date (a recurring task completed: the done one); a task added, removed or rewritten is none. At most 200 of a burst |
| `b.on("journal", fn)` | a daily note was made — by this device (opening the day, a capture, an extension) or by a sync | `{ date, page, path, source, by }` |
| `b.on("open", fn)` | the notebook was opened: once, after every extension that runs there loaded (not for one approved later) | `{ date }` |
| `b.on("day", fn)` | a new day began while the app runs (midnight passed, the device woke up on a later day) | `{ date, previous }` |
| `b.on("timer", fn)` | the extension's own timer ended ("Timers"); only its own hooks hear it | `{ label, seconds }` |

**Who hears what.** A hook never hears what its own extension wrote — no echoes, no loop of one
extension —, and every other change it hears: one extension's tick in its view reaches the page
another one keeps. Extensions answering each other end: a write made by a hook that ran for a
change is one step deeper than that change (the editor, a sync, a command, an action, the console:
the first step), and a change deeper than four steps reaches no hook — the console says so ("…
stopped after 4 rounds"). Write only what differs (compare with `b.read`), and a chain never starts.

**Twice in a synced notebook.** A sync brings what another device's extensions wrote, and its
hooks ran there: `"change"`, `"task"` and `"journal"` say `source = "sync"` then (`sources`
contains it). A hook that writes something for each event — a line in the journal for each task
done — should answer this device's events only (`if e.source == "sync" then return end`), or
the notebook gets the line once per device.

A hook may write and may ask a model, but never asks a question (`b.ask` there is an error); it
runs under the same limits; what it returns is written to the console; and one that fails three
times in a row is taken off until its extension loads again (Emacs does the same to a failing
`post-command-hook` function), its errors in the console and the event log. At most 256 hooks in
all. A view is not drawn again after a hook: keep what a view shows in the notebook, not in
variables a hook fills.

```luau
-- Keeps the page "Project index" in step with the pages tagged #project — also when another
-- extension ticked a task off on one of them.
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

```luau
-- Every task done on this device gets a line in the day's journal; a new daily note a line to
-- start from; and the first opening of a day says so in the console.
b.on("task", function(e)
    if e.task == "x" and e.source ~= "sync" then
        b.appendToJournal(b.today(), `- Done: {e.plain} ([[{e.page}]])`)
    end
end)

b.on("journal", function(e)
    if e.source ~= "sync" then
        b.appendToPage(e.path, "- Three things for today:")
    end
end)

b.on("open", function(e)
    print(`Opened on {e.date}: {#b.query("type:tasks due:today", 1).items} task due today`)
end)

b.on("day", function(e)
    print(`A new day: {e.date} (yesterday was {e.previous})`)
end)
```

## Questions

| Function | Asks | Answer |
|---|---|---|
| `b.ask(prompt)` | for a line of text | the text; `""` when cancelled (the dialog takes no empty answer) |
| `b.choose(prompt, options)` | to pick one of a list of texts (one tap) | the option; `nil` when cancelled |
| `b.form(title, fields)` | for numbers, each with − / + and typing (`82,5` too), and for lines of text | `{ key = number or text }`; `nil` when cancelled |

A form's field is a number, `{ key, label, value, step?, unit? }` (`step` 1 by default), or a text
field, `{ key, label, kind = "text", value?, placeholder? }`: `value` is what it holds at first,
`placeholder` the grey example while it is empty ("e.g. Meditation"); the first text field has the
focus, and Enter in one answers. A prompt's first line is the dialog's title; further lines show
under it as plain text (the AI assistant shows there what a change would write; the habits' **Add
habit** asks again with what was wrong, "Not understood — try …", and with what was typed kept).

What a question shows has a limit: a prompt's first line is cut to 300 characters, the lines under
it to 200 lines and 10 000 characters ("…" where it was cut); `b.choose` takes at most 500
options of at most 500 characters each and `b.form` 50 fields (more is an error: a choice cut
would give back what the script cannot recognize), a field's label 200 characters, a text field's
value 10 000.

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

```luau
b.command("book", function(ctx)
    local answer = b.form("A book read", {
        { key = "title", label = "Title", kind = "text", value = "", placeholder = "e.g. Solaris" },
        { key = "author", label = "Author", kind = "text", placeholder = "e.g. Stanisław Lem" },
        { key = "stars", label = "Stars", value = 4, step = 1 },
    })
    if answer == nil or answer.title == "" then
        return nil
    end
    local by = if answer.author ~= "" then ` by {answer.author}` else ""   -- an empty field is ""
    b.appendToPage("Books", `- [[{answer.title}]]{by} {string.rep("★", answer.stars)}`)
    return { message = `{answer.title} is in Books`, tone = "success", button = { label = "Open", page = "Books" } }
end, { label = "Note a book" })
```

## Settings

`b.settings(fields)` declares the extension's settings: **Settings → Extensions** (and the state
line of its page) then has **Settings** for it, a dialog of these fields with their values on this
device. `b.setting(key)` reads one, `b.settings()` (without fields) all of them as a table;
`b.setSetting(key, value)` changes one from the code (a text, a number, `true` / `false`; `nil`
puts it back to its default) — no permission needed: they are the extension's own.

| Field | |
|---|---|
| `{ key, label, value, step?, unit?, description? }` with a number `value` | a number with − / + and typing (`82,5` too): `kind = "number"` |
| `{ key, label, value = "", placeholder? }` | a line of text: `kind = "text"` |
| `{ key, label, value = true }` | a switch: `kind = "toggle"` |
| `{ key, label, options = { "kg", "lb" }, value? }` | one of the options (the first by default): `kind = "choice"` |

`kind` may be written out; `value` is the default; `description` a sentence under the label. A key is
letters, digits, `-` and `_`. At most 32 settings; a text of at most 4000 characters; 64 KB for all
of an extension's values. Calling `b.settings` again (the page saved) declares them anew; the
values stay.

* **Kept on this device**, in the notebook's device state (`LocalStore` `extensions/settings/<id>`),
  never synced — as the AI's provider and an extension's switch are: a phone and a computer may want
  another rest between sets, and a notebook that arrives cannot change them. What every device of
  the notebook should share belongs on the extension's page (its properties, `b.page(…).props`) or
  on a page of its own.
* A stored value that does not fit its setting (another kind, an option no longer there, a hand
  edit) reads as the default.
* The built-ins use them too: the gym's **Rest between sets** (used where no `rest::` says
  otherwise).
* `brulion-cli extension settings <extension>` lists them, `--set key=value` changes one.

```luau
local settings = b.settings({
    { key = "currency", label = "Currency", value = "zł", placeholder = "e.g. €" },
    { key = "budget", label = "Monthly budget", value = 2000, step = 100, unit = "zł" },
    { key = "cents", label = "Show the cents", value = false },
    { key = "period", label = "Period", options = { "month", "week" } },
})

b.command("spent", function(ctx)
    local total = 0
    for _, block in b.blocks({ tag = "expense", from = b.addDays(b.today(), -30) }) do
        total += tonumber((block.text:match("^(%d+[,.]?%d*)") or "0"):gsub(",", ".")) or 0
    end
    local decimals = if b.setting("cents") then 2 else 0
    local left = b.setting("budget") - total
    return `{b.formatNumber(total, decimals)} {b.setting("currency")} this {b.setting("period")}, `
        .. `{b.formatNumber(left, decimals)} left`
end, { label = "Money spent" })

b.command("last-currency", function(ctx)
    b.setSetting("currency", if settings.currency == "zł" then "€" else "zł")
    return `Now in {b.setting("currency")}`
end, { label = "Switch the currency" })
```

## Timers

`b.timer(seconds, { label = …, message = … })` starts the extension's own timer — one per
extension, a new one replaces it; any number of extensions may time something at once. Its
countdown shows in a banner under the top bar in every view (the gym's rest in the Gym view's own
banner instead), with × to stop it; `b.stopTimer()` stops it from the code. When it ends: a
notification "`label` is over" with `message` (without one: "The timer of … has ended"), the
message as a toast, and the extension's `b.on("timer", fn)` hooks with `{ label, seconds }` — to
write the session into the journal, to start the next one. `b.timer(seconds, label)` is the short
form. A label is cut to 100 characters, a message to 300, both one line.

The timer runs while the app does: a notification of a closed app is in TODO.md ("Gym: next
steps").

```luau
local SESSION = 25 * 60

b.command("focus", function(ctx)
    b.timer(SESSION, { label = "Focus", message = "Time for a break — the session is in the journal." })
    return "25 minutes of focus"
end, { label = "Focus for 25 minutes" })

b.on("timer", function(e)
    if e.label == "Focus" then
        b.appendToJournal(b.today(), `- 🍅 {os.date("%H:%M")} {e.seconds // 60} min of focus`)
    end
end)

b.command("stop-focus", function(ctx)
    b.stopTimer()
    return "Stopped"
end, { label = "Stop the focus timer" })
```

## Shared code

`b.require(name)` runs the code of another extension page — `name` as a link names it — and gives
what its code returns: a table of functions shared by many extensions, written once (Lua's
`require`). The page is an extension page like any other (tagged `extension`, its code in
```` ```luau ```` blocks, ending with `return`), and it runs on this device only once approved here,
as any code: `b.require` of a page that waits for approval (or is switched off) is an error
("“Shared helpers” waits for approval on this device").

* **A copy each.** Every extension that requires a page runs its code in an environment of its
  own, with **its own `b`**: what the shared code does it does with the permissions of the
  extension that uses it, never with those of its page — a library cannot give anyone more than
  they have. Its variables are that extension's: two extensions never share a counter.
* **Once per load.** A page required again in the same extension gives the same table (until the
  extension loads again). When a page it required changes, the extension loads again with it;
  Settings → Extensions shows "Uses the code of: …".
* The page's own load runs its code too, ignoring what it returns: keep its top level to
  definitions.
* A cycle (A requires B requires A) is an error naming it; at most 16 pages deep. Its errors name
  its own lines ("Shared helpers:12: …").
* `brulion-cli extension copy` and "Copy to another notebook" take the pages an extension
  requires along (those whose names it writes out: `b.require("Shared helpers")`).

```luau
-- The page "Shared helpers" (tags: [extension]) ends with `return M`: a table of functions.
local shared = b.require("Shared helpers")

b.command("open-tasks", function(ctx)
    local open = b.query("type:tasks status:open", 1).total
    return shared.count(open, "open task", "open tasks")
end, { label = "How many open tasks" })

b.command("projects-list", function(ctx)
    return shared.bullets(b.pages({ tag = "project" }))
end, { label = "The projects as a list" })
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
`"high"`, `"xhigh"` or `"max"`: Claude's effort, and for a model on the device `"low"` means an
answer without thinking first.

`b.ai` returns once the answer is written. The app's own AI chat shows it while it is written
(docs/UI.md "AI chat"): its requests go out with a watcher (`ai::Client::send`'s `partial`) — a
provider on the network then streams the answer as server-sent events (`"stream": true`;
`ai::StreamReader` reads Anthropic's events and an OpenAI-compatible API's chunks, with their
thoughts, into the same reply an answer at once gives, a server-side fallback's boundary
included), the model on the device hands on its tokens — and a stopped turn stops its request
(`ai::Pending::stop`: the connection closed, the model's writing ended).

**Pictures**: `images = {"paragon.jpg", …}` next to `prompt` — or in a user's message of
`messages` — shows the model pictures of the notebook, named as notes embed them
(`![[paragon.jpg]]`) or by their path (`assets/skan.png`); at most 8 in a request. The app finds
each one in the notebook, decodes it, scales it down to 1568 pixels on its longer side and sends
it as a JPEG, before the message's text. A model that sees is needed: Claude, an
OpenAI-compatible model with vision, or LFM3-VL on the device; a picture the notebook does not
have is an error ("b.ai: the picture "brak.jpg": no such picture in the notebook").

```luau
b.command("opisz-zdjecie", function(ctx)
    local name = b.ask("Which picture? (as in ![[…]])")
    if name == "" then
        return nil
    end
    local seen = b.ai({
        prompt = "Describe this picture in two sentences and write out the text on it.",
        images = { name },
        maxTokens = 600,
    })
    b.appendToJournal(b.today(), `- ![[{name}]]: {seen:gsub("%s+", " ")}`)
    return seen
end, { label = "Describe a picture into the journal" })
```

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
  models that have it), **OpenAI-compatible** (OpenAI, OpenRouter, or a server of one's own —
  Ollama `http://192.168.1.10:11434/v1`, LM Studio, llama.cpp — which needs no key) or **On this
  device** (below; in builds that have it); the API key
  (in `<app data>/secrets/ai-key`, owner-only like the Nextcloud password, masked in logs, never in
  the notebook); the model; the address; **Check** ("Say OK."). In the browser the key is in the
  site's storage, and Anthropic's API is told the request comes from a page.
* **On this device**: a small model that runs inside the app — no key, no account, no network once
  its files are there, nothing sent anywhere. Three models, chosen in the settings: **Brulion
  Assistant 1.7B** (the default; `brulion-assistant-1.7b`, `janusz-bit/brulion-assistant-1.7b` on
  Hugging Face): LFM3 1.7B trained further for the built-in assistant (docs/FINETUNE.md), the best
  of the three at working through the notebook with it — it answers at once, without thinking,
  and reads text only; an adapter of 92 MB over LFM3 1.7B's file (below) —, and two by Liquid AI,
  **LFM3 1.7B** (text only, better than the next at working through a notebook with tools; 1.0 GB)
  and **LFM3-VL 2B** (it sees pictures; 1.2 GB). LFM3-VL 2B was the default before: a device that
  never chose a model and has its files (and not the default's) keeps it — AppController stores
  it as chosen (`LocalAi::keptChoice()`), so an update does not swap the model it uses for one
  still to download. Settings downloads the chosen one from
  Hugging Face into `<app data>/models` — one pinned revision, each file checked against its
  SHA-256; a download that breaks off goes on from where it stopped — and removes it. The files
  come over HTTP/1.1 with 4 MB read ahead: what the server sends while the app stands still (an
  Android app off the screen) waits there, not in the app's memory. The models
  are Liquid AI's, under the LFM Open License v1.0 (not a free-software licence; the app ships
  none of them) — Brulion Assistant 1.7B too: LFM3 1.7B changed by Brulion's training, under the
  same licence, neither made nor endorsed by Liquid AI (its `maker` is Brulion, its `baseName` and
  `baseMaker` LFM3 1.7B and Liquid AI; Settings' **Whose model** says so). They are slower and far
  less able than Claude: fine for a summary, a question about a note, reading a photographed
  receipt; unreliable for long chains of tool calls.
  * A model may be another one adapted to the app: a LoRA adapter (`LocalModel::adapter`, a GGUF
    of some tens of MB) that the runner adds to the weights of its maker's 4-bit file as it
    computes. The file stays as it is: its weights were trained for that precision and lie on its
    grid, so the adapter merged into them and rounded to 4 bits again would lose most of what was
    learnt. The adapted model and its maker's own share that file, downloaded once (Brulion
    Assistant 1.7B and LFM3 1.7B: LFM3-1.7B-QAD-Q4_0.gguf). A model is on the device when its
    download finished — `<id>.downloaded` in the folder; in a folder of an older version, which
    marked nothing, every model whose files are all there is marked so, once (`.adopted` then
    says it was done: a model removed later stays removed) — and removing one
    keeps a file the other needs. Settings says so under the model's files ("Shares a file with
    …", `LocalAi::modelSharesWith`) and, while part of a model is on the device already — a
    shared file, a download that stopped —, how much is left to download of the whole
    (`modelLeft`). A model trained to answer at once (`LocalModel::thinks` false: Brulion
    Assistant 1.7B) never thinks: every request it gets is taken as one of `effort = "low"`.
  * The model is loaded the first time it is asked (some seconds) and leaves memory after ten
    minutes without a request. While loaded it takes some 1.3 GB of the app's own memory, and
    its file's 1 GB stays mapped until the system wants that memory back (docs/PERFORMANCE.md
    "The model on the device"). Requests are answered one after another, on a thread of their
    own.
  * It holds 16 384 tokens at once on every device (`LocalAi::ContextTokens`; some 200 MB of
    memory) — the request and the answer together. A request that does not fit is cut rather
    than refused: its longest texts first, a tool's result at its end and a message in its middle
    (its start and its question stay), each marked `[… cut: the model on this device cannot read
    more at once …]`; only a request whose system text and tools alone are too long is an error.
    One answer is at most 2 048 tokens, thoughts included. A picture takes up to 1 024 tokens.
  * The model thinks before it answers (its thoughts are not part of `text`); `effort = "low"`
    asks for the answer at once — faster, less careful.
  * What it has read stays in its memory: the next turn of a tool loop reads only what is new,
    another question after the same system text and tools starts after them, and a request about
    something else in between (a look at a picture) does not push the conversation out. The
    model's state after a system text and tools of 256 tokens or more is kept on disk too (the
    app's cache, a few at most): the first request after the model was loaded again — ten minutes
    after the last one, or after a new start of the app — does not read them anew.
  * It computes on the processor: on a phone its fast cores only, one thread each (their slow
    cores would hold them back), on a PC one per performance core (not on its SMT sibling), at
    most 8, kept in a pool for as long as the model is loaded. On Android they spin between two
    tokens (the system keeps other work off those cores); elsewhere they sleep, so that a busy
    processor does not stall them.
  * What it writes is handed on token by token (`LocalRunner::Params::written`, read by
    `local::partial`: its thoughts, its words without a tool call being written): the chat shows
    it as it comes. A request stopped (`stopped`, the chat's Stop) ends its writing at the next
    token; one still waiting is never started.
  * How fast it is on a device is in the event log (`brulion.ai.local`): `lfm3-vl-2b opened:
    16384 tokens at once, ggml-cpu-alderlake, 6 threads on cores 0,2,4,6,8,10, in 1.42 s` — the
    processor plugin taken, its threads and their cores (on Android also that they spin:
    `ggml-cpu-android_armv8.6_1, 4 threads on cores 4-7, spinning`) and the time the files took to load —, and of every answer `answered: 1520 tokens read
    in 0.61 s (1400 kept from before, 0 from the disk), 85 written in 1.90 s, shown from 0.62 s,
    called search_notes`: reading costs only the tokens neither kept in memory nor read from the
    disk; "shown from" is when the first of its words or thoughts was there to show (a request
    the chat watches), "called" the tools it asked for, by name. A
    download is there too, from `lfm3-vl-2b download: 1.2 GB to fetch of 1.2 GB` to `downloaded:
    1.2 GB in 3 min 05 s`, `download stopped after 41.0 s` (by the user) or the warning `download
    failed after 1 min 02 s: …` with the reason.
  * A note's text never becomes part of the conversation's structure: the request is given to
    the model as pieces — its control tokens (a turn's end, a tool call) apart from the text, in
    which the same letters are only letters — and its answer is read the same way.
  * `brulion-cli` runs no model; the browser version has none (`available` is false there, and
    in any build without llama.cpp: docs/ARCHITECTURE.md "The model on the device").
* **Subscriptions** (Claude Pro/Max, ChatGPT Plus) cannot be used by other apps: the providers bill
  their APIs separately, by use. On a phone that leaves an API key (a question about a note costs
  fractions of a cent), a server of one's own (free, private; reachable through Tailscale or the
  home network), or the providers' own apps with a connector to the notes (a remote MCP server —
  `brulion-cli mcp` speaks stdio only so far: TODO.md).
* **Limits**: at most 200 000 characters and 8 pictures per request (a script cannot send the
  notebook by accident), 5 minutes per answer; only extensions approved with `ai` send anything,
  and what they send goes to the chosen provider — or, on the device, nowhere.
* `src/core/ai`: `ai.h` — the request from b.ai's table, the HTTP request of each API and reading
  the answer are pure functions; `ai::Client` sends through `sync::HttpTransport` and hands the
  provider "local" to an `ai::LocalBackend`. `local.h` — the models the app knows, a request
  written as a model's prompt and its answer read back (LFM3's chat format, compared in
  `tests/core/ai` with what the model's own template gives), cutting a request to fit.
  `localfiles.h` — the models' files. `localrunner.h` / `llamarunner.h` — what runs a model:
  llama.cpp, in a plugin the app loads on first use (`src/app/shell/localai.h`).

**The AI assistant** (built in, `src/core/extensions/builtin/asystent.luau`; Settings → Extensions →
AI, off by default) is an agent that the **AI chat** talks to (docs/UI.md "AI chat"): a
conversation is a page in `AI/` (docs/SYNTAX.md §14), and every message sent there runs one turn,
its command `chat` (`palette = false`: the palette does not offer it; run by hand it says to use
the chat). The app gives it the conversation's messages (`ctx.messages`, the user's last) and the
page it is about (`ctx.page`); today's date with its weekday ("Today is Wednesday, 2026-11-11.",
in the notes' language — not a calendar of the days after it: a small model took those dates for
what the user said, and wrote them into the journal) and that page's text (at most
30 000 characters, read again each turn) go before the first message — not into the system text,
which stays the same from day to day, so that a model on the device keeps what it read of it —;
what the command returns is
the AI's message, which the app writes into the conversation. The conversation's own page
(`ctx.conversation`) is none of its tools' finds — not in a search, a query or the list of
pages, nor the day's line that only links to it, and reading it is refused: it holds the question
word for word, and a small model read it instead of the notes (on a phone that cost minutes).
Twelve tools —
`search_notes` (`b.search`, which gives one line of a page, its best — on a person's page the line
"Vet", not the phone under it, and a small model made a number up: so under each of the first five
hits more of its page, as long as it fits in 3 000 bytes for all of them — a short page, at most
600 bytes and 12 lines with text, whole without its frontmatter and title (a person's page, a day
with both of its runs); of a longer one, by their numbers, the three lines after the hit's up to a
blank line or a heading (a heading's: the first three of its section) and three other lines with
the most of the search's words, in any form (not words of one or two letters, nor "what", "jaki"
and the like), a child with its parent above it; only the first 400 lines and 32 000 bytes of a
page are looked through. Tasks there are words, as in a query's result ("- done task: …"); a line
that links to the conversation is never shown, and a hit on one — today's journal links to the
conversation, whose name holds the question's words — becomes the day's best other line with the
search's words), `query_notes`
(`b.query`, the query language described in the tool's description, which starts with the queries
of the usual questions — overdue, due this week, in progress, unfinished, done this week or in the
last 7 days, with a tag — and says that `open` is unfinished; its result repeats the query and gives
a task's status as a word, "open task", "task in progress", so that a small model trusts what it
got. A page filter as a small model writes it is read as it was meant, and the result says so in
its first line: `page:"#home"` as `tag:home`, `page:"Dom."` with the user's full stop as
`page:"Dom"`, the name of no page but of a tag in use as that tag (not in a quoted phrase),
`tag:#auto` as `tag:auto`; a page
asked for that does not exist is "No page named …" with the pages of a close name, or else some of
the notebook's, before "Nothing matches" — never a day of the journal (a day that is not there: "the
journal has no note of that day") nor a conversation with the AI (a page in `AI/`). A query that
fails or finds nothing as written is read once more as a small model meant it — what a right query
finds never changes —, and when that reading finds something, its result says what was read
otherwise: a filter the language does not have is its words (`task:"Chłopi"`, `words:`, `word:`,
`query:`, `name:`, `title:`, `słowa:` → `"Chłopi"`; a bare `task:` is left out, `task:tasks` is
`type:tasks`), the label of a description's example copied before the query is left out ("in
progress: type:tasks status:doing"), so are `OR` / `AND` between its terms (every filter and word
must hold), a filter given twice once, a tag of joined words that no note has is those words
(`tag:umówić-przegląd-pieca` → `"umówić przegląd pieca"`), the description's placeholder `tag:name`
is left out, and a `?`, a full stop or brackets stuck to a token go; when it finds nothing either,
the result is the query's as written (its error, "Nothing matches")), `list_pages`,
`read_page`, the changes `append_to_journal` (not a line that starts with the words of a task this
question changed with `update_block` — a small model ticked a task off, then wrote it into the day's
journal too, "- [x] Kupić prezent dla mamy (odhaczono)", the user's "Już to zrobiłem" read as
something done for the journal: refused before the user is asked, "Not added: this is the task
changed above in [[Dom]] …", unless the user's message speaks of the journal), `append_to_page` (to a page that exists, named as in a
link, with `.md` or not: one that does not is refused before the user is asked, with the pages to
offer — a small model added a book "to the reading list" as a new page named after it —, and to add
to one of them when the user meant it, otherwise to ask which page (not a new page with `write_page`:
a small model then made a page of a task it was to move); so are a day, which is
`append_to_journal`'s, and a conversation with the AI),
`write_page`, `update_block`
(`b.replaceBlock`: a task's status too — without a text only the status changes, and a text written
with its marker and checkbox, `- [x] …`, sets the status; a change that changes nothing — a task
ticked off again, its own words again, its dates left out — is refused before the user is asked,
with what the call left out: "Not changed: the line already reads "[x] …" (done task), the status
given. Tell the user it is so already." for a task ticked off again, but for a call with no status
and the line's own text "… the call gives no status, and its text is the line's own … To change the
task, give what changes: its status (todo, doing, done, cancelled), or its text with the new date"
— a small model that forgot the date of a move was told "it is so already" and said so — and for a
task opened again, "if that status is all the user asked for…; to move the task, give its text with
the new date"; but on a line this question changed — on the ref its change gave, which is not the
one it had — such a call is the change made: "This change was made already, by the same call above
(Changed; its ref is now …)." for the same change again, "This line was changed already, by your
call above (…): it reads "[x] …" now. Do not change it again: tell the user what you did." for its
status or text again — a small model ticked a task off, called it again on its new ref, was told
"it is so already" and told the user nothing needed changing), `look_at_picture` (a picture
of the notebook — a photo, a scan, a screenshot — with a question about it: a request of its own
with the picture, `b.ai`'s `images`, whose answer is the tool's result as text; a model that sees
is needed), and for writing an extension
`extension_reference` (this document, `b.data("reference")`: the list of its sections, or the
sections whose heading has the words asked for), `check_extension` (`b.checkExtension`, with the
type checker's first five warnings: a misspelt function of `b` is fixed before the user sees it) and
`write_extension` (checked first and refused while it does not load or when the page is no
extension; a new page goes to `Rozszerzenia/` / `Extensions/`). A reminder asked for and written as a
due date with a time goes in as the reminder (docs/SYNTAX.md §6; a due date rings nothing, and a
small model wrote reminders so): when the user's message asks for one ("przypomnij", "przypomnienie",
"remind"), not for none ("nie przypominaj", "don't remind"), and names no due date ("termin", "due",
"deadline", 📅), a change of `append_to_journal`, `append_to_page` or `update_block` with no `⏰` of
its own writes `📅 2026-11-12 8:30` as `⏰ 2026-11-12 08:30` — in what the user approves too; a
task's line keeps its due date — and its result says so ("The time went in as a reminder, ⏰ …: 📅
is a due date and rings no alarm."); the same change written again as a reminder is the change made
already (a fine-tuned model wrote the line a second time so). Its system text is a list of rules, after the
notes' syntax: search before answering about the notes and answer once a result does; answer only
from what the tools gave, a result of the kind asked for under another name too; ask back when the request does not say what it is about; answer general
questions without tools; a request to add or change ("remind me…", "tick off…") makes the change,
searching only for what it changes; which tool for which change (a page the user names, the day's
journal, a task's status by its ref); never say a change is made before its result. The model calls them as it needs,
at most sixteen turns, each result at most 12 000 characters (the reference 40 000); a reading
tool called again with the same input is not run again — its result says it is the same call as
before and to answer or try something else (a small model asks the same in circles until its turns
are up; a change makes every read new) —, a change made is not made again by the same call (the
same tool and the same change, however written: a page as `[[Zakupy]]` or `Zakupy.md`, today's
journal with or without its date, the text with other line endings or blanks around it) — its
result says it was made already, with what it gave then, and to tell the user (a small model added
the same reminder sixteen times); another change to that page makes it one to make again (the page
may have changed back), and a change declined is none made: the same call asks the user again —,
and from the eighth turn on, the last result of a turn
says which turn of the sixteen it is, to answer soon. After a change, two turns in a row whose every
call only repeats — a change made again (on a line's new ref too), a read again, a change refused
again as changing nothing — end the question: the answer is "Gotowe." / "Done." with the pages
changed (a small model called its reminder fifteen times more and never answered). An answer that
starts by saying a change was made ("Odhaczyłam „Zły” w [[Książki]].", "Jasne, dopisałam …", "I've
added …": a first-person verb of a change, after a word such as "Gotowe!" at most — not a quote, a
list's item, nor after "nie") in a question that made none — none made, declined or found so already,
the user's message no question (its first word "co", "czy" but not "czy możesz", "jaki", "what",
"did" …), the answer before it no change of its own — is asked for once more: without that
answer, and the message it answered (the turn's last result, else the user's message) ending
"(Nothing is changed yet: no tool made a change for this question. If the user asked for one, make
it now with its tool; otherwise answer without saying that you changed anything.)", as a late
turn's result says to answer soon (a small model found the task to tick off, then said
"Odhaczyłam …" and called nothing). Before each
tool `b.status` says what it does ("Searching the notes: “rent”…", "Reading “Home”…"), before each
request to the model `b.status("")`. **Every change waits for the user**: `b.choose` with what
would be written — the chat shows it in the conversation as a card with the options (the dialog
asks it only while no chat view shows the conversation; it shows the lines after the first under
its title, as plain text) — Allow, Allow every change of this question, No; a no (or the dialog
closed) goes back to the model as a declined change. An extension it wrote is code "changed
elsewhere" like any `b.write` of a page: it runs once the user approves it on the page. The AI's
message is its answer as the model wrote it, then `✏️ Changed: [[…]]` with the pages changed (and,
after the first answer, a note when the page it is about went cut); sixteen turns without an
answer end with a word about it, in the notebook's language. The top bar's ✨ button opens the chat
while the assistant is on (docs/UI.md "Top bar": a new conversation about the page on screen — in
the journal's stream today's note —, one beside it in the side panel, the conversations, a summary
of the page as a new conversation; a spinner while `aiPending`, none while a chat view shows the turn — `WorkspaceChats.runningShown`). Like the gym, its source can run
from a page of the notebook and be changed there: a page tagged as an extension, with
`permissions: [write, ai]`, that holds the source and is titled "Asystent AI" / "AI assistant" (or
has `overrides: asystent`) — Settings → Extensions → AI → **The assistant's code → Edit the code**
writes it, as for the gym and the habits. How
well a model does its work is measured by the assistant's bench (docs/FINETUNE.md): tasks with a
known outcome on notebooks of their own, run by this script — a change of its tools or instructions
shows there.

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
| `b.replaceBlock: stale: …` (also `b.insertBlock`, `b.removeBlock`, `b.setTask`, `b.setProperty`) | the line changed since its `ref` was read, or its page is gone: read again |
| `b.replaceBlock: not a block reference: …` (and the same four) | the `ref` is not one a block had: made by hand, without its hash, or naming a file that is no note |
| `b.replaceBlock: the text must be one line`, `b.setProperty: the value must be one line` | no line breaks in a block's first line, nor in a property's value |
| `b.replaceBlock: the task status must be one character`, `b.setTask: …` | `task` / `status` is `"x"`, `" "`, `"/"` …, not `"done"` |
| `b.setProperty: cannot set the property "…"` | the key is no property name (letters, digits, `-`, `_`: not `"my key"`) |
| `b.insertBlock: argument 3 is "after", "before" or "child"`, `b.insertBlock: nothing to insert` | the place is none of the three; `markdown` is empty or blank lines only |
| `b.appendToPage: the page's name is empty`, `b.appendToPage: nothing to append` | a name of spaces; nothing but white space to add |
| `b.blocks: the query needs a page, a tag or a prop` | `b.blocks({})` |
| `b.blocks: prop must be a text or a list of texts`, `b.pages: …` | `prop = 3`: a key, or a list of keys |
| `b.blocks: invalid date: …` | `from` or `to` is not `YYYY-MM-DD`, or no such day |
| `b.pages: argument 1 must be a table …` | `b.pages("project")`: the filter is `{ tag = "project" }` |
| `…: the view "…" returned nothing` | `render` must return a node of `b.ui` (or a list of them) |
| `b.query: …` | the query language's own message (an unknown filter, a wrong date) |
| `b.appendToJournal: invalid date: …`, `b.daysBetween: invalid date: …`, `b.addDays: invalid date: …` (`b.addMonths`, `b.weekday`) | not `YYYY-MM-DD`, or no such day |
| `b.addDays: the count must be a whole number`, `b.addDays: the date is out of range` | `1.5` days; a date before year 1 or after 9999 |
| `b.weekdayName: argument 1 must be a whole number from 1 to 7` (`b.monthName`: 12), `…: the form is "full", "short" or "of"` | a day or month that is none, another form |
| `b.formatRecurrence: every must be "day", "week", "month" or "year"` (and `interval`, `weekdays`, `monthDay`, `the language is "pl" or "en"`) | the rule is none `b.parseRecurrence` gives |
| `b.read: argument 1 must be a string, not nil` | a wrong argument, by its position |
| `b.ask: cannot wait for an answer here (inside a coroutine or a library callback)` | a question or `b.ai` where the call cannot be suspended |
| `…: an extension cannot ask a question while it loads` | `b.ask`, `b.choose`, `b.form` or `b.ai` at the top level |
| `b.ai: …` | the model's or the provider's: no key, no connection, HTTP errors in words, "the request is too long", a picture the notebook does not have; on the device: "the model … is not on this device yet: download it in Settings → Extensions → AI", "… does not see pictures", "this device's processor is too old to run a model" |
| `value nested too deeply (a cycle?)`, `cannot pass a value of type function` | a table that cannot cross to the app (a control's `args`, a returned value) |
| `b.require: no page named “…”`, `… is no extension page`, `… has no ```luau block`, `… is the extension's own page` | the page to require is not there, not tagged `extension`, without code, or the extension's own |
| `b.require: “…” waits for approval on this device`, `… is switched off on this device` | the shared code runs here only as any code does: approve its page, switch it on |
| `b.require: a cycle: A → B → A`, `b.require: modules nested more than 16 deep` | pages that require each other |
| `b.settings: setting 2 needs a key …`, `… kind is "number", "text", "toggle" or "choice"`, `… its value must be …`, `… at most 32 settings` | a declaration that is none |
| `b.setSetting: "rest" is a number`, `b.setSetting: the extension's settings are full (64 KB)` | a value of another kind than its setting; too much kept |
| `b.choose: at most 500 options`, `b.choose: option 3 is longer than 500 characters`, `b.form: at most 50 fields` | a question a dialog cannot show: page through, ask in steps |
| `b.timer: label must be a text` | `b.timer(60, { label = 5 })` |
| `timeout: the extension ran too long` | more than 2 s in one call |
| `not enough memory` | more than 64 MB |

## Recipes

Complete pieces to start from; the documentation notebook's "Extension recipes" page has more.
The ones that write need `permissions: [write]`.

**A day more or less.** Dates are text, and `b.addDays` moves one (the end of a month and of a
year included; local calendar days, so daylight saving never moves a day). Here: the task under the
caret, due a day later.

```luau
b.command("postpone", function(ctx)
    local block = ctx.block
    local due = block and block.text:match("📅 (%d%d%d%d%-%d%d%-%d%d)")
    if not due then
        return "Put the cursor in a task with a due date (📅)."
    end
    local later = b.addDays(due, 1)                 -- "2026-11-13" → "2026-11-14"
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
  page's code runs on a device only after it was approved there (Settings → Extensions, or "Read
  and run" on the page's state line) — like VS Code's workspace trust or Emacs' questions before
  `eval` of a file's local variables. What is approved is the SHA-256 of the code and the
  permissions, kept in the notebook's device state (`LocalStore` `extensions/approved`, never
  synced; the newest 300); prose around the code needs no new approval.
* **The version that was read.** "Read and run" first shows the code (ShellExtensionApproval): the
  lines added and removed against the version approved here before — a sync changed three lines,
  those three are marked —, all of it when it is new on this device, what a page that overrides a
  built-in changed against the built-in's code; what it may do, and what it asks for beyond the
  version before. **Run this version** approves the hash of what was shown
  (`WorkspaceExtensions::approveVersion(path, hash)`): when the code changed while it was read (a
  sync arrived), nothing is approved, the dialog says so and shows the new version only on "Show
  the new version". The code approved last of each extension is kept for that comparison
  (`extensions/approvedCode/<id>`: its hash, permissions and code, up to 512 KB; longer code keeps
  its hash and shows whole the next time). Code written on this device stays approved: when
  the app's editor saves an approved page, the new code is approved too — unless it asks for more
  permissions, and only when the save was made over the approved code (`savedByEditor()` is told
  the text the editor writes over, `EditorHost::writeIfUnchanged`'s `expected`). A change that
  reached the file a moment before the save is merged into it by the editor: code that was merged
  in is not code somebody typed here, so such a save waits like any code from elsewhere. Code that
  changes any other way (a sync, another program, an extension's `b.write`)
  waits again, and until it is approved the extension does not run at all — neither the new code
  nor the one before it (a page that overrides a built-in leaves the built-in code running
  meanwhile). "New extension" and "Edit the code" of a built-in write pages that are approved at
  once. The code is what the page shows as code: `luau` blocks after the frontmatter the parser
  finds (`markdown::findFrontmatter`) — nothing a reader sees among the page's properties runs.
* **Live.** Whenever a page changes, its code is loaded again (only that extension; while a
  question waits, after its answer); its commands and views follow, its variables start anew and
  its calls waiting for an answer are dropped. A save that does not load (a syntax error, an error
  at the top level) keeps the version before it running and shows the error: half-written code
  never takes a working extension away. "Reload extensions" (the palette) starts every extension
  in a fresh VM.
* **Its state** shows in the page's bottom-left corner and in Settings → Extensions: runs (with its
  commands, views and reactions counted), an error with its line, new or changed elsewhere ("Read and run"),
  off on this device, another page has the same name, the built-in one it replaces is off, no
  ```` ```luau ```` block yet.
* **Switched off** on this device in Settings → Extensions (`extensions/off`).
* **Built-ins as pages.** "Edit the code" of the gym, the habits or the AI assistant (Settings →
  Extensions, the assistant's under AI) writes its source into `Rozszerzenia/Siłownia.md` /
  `Rozszerzenia/Nawyki.md` / `Rozszerzenia/Asystent AI.md` (`Extensions/Gym.md`, `Extensions/Habits.md`,
  `Extensions/AI assistant.md` in an English notebook): `zastępuje: silownia` (`nawyki`, `asystent`), one
  ```` ```luau ```` block per section of the source under its heading. While that page exists and
  is approved, the built-in runs from it — with its bundled data (`b.data`) — and a change there
  works at once; "Restore" puts the page in the trash and the built-in code runs again. An
  extension page titled like a built-in ("Siłownia", "Gym", "Nawyki", "Habits", "Asystent AI", "AI
  assistant") overrides it too, as TiddlyWiki's shadow tiddlers. The built-in's switch still
  decides whether it runs at all.

## Sharing between notebooks

An extension is a page of its notebook: it syncs, it is versioned and it can be linked with it — and
to use it in another notebook, it is copied there.

* **Copy to another notebook** (Settings → Extensions, the row's copy button) lists the notebooks
  this device knows (the recent ones of "Switch notebook", not the documentation) and writes the
  page into the chosen one at the same path (`Rozszerzenia/Licznik słów.md`), with the extension
  pages its code requires (`b.require` with the name written out, and theirs). The copy is
  **approved there on this device** (the user chose to run that code; the hashes and the code go
  into that notebook's device state, `extensions::approveInStore`): it runs as soon as that
  notebook is opened here. Other devices of that notebook approve it as code from elsewhere.
* A page there with the same text is left as it is; one with other text is replaced only after a
  yes (its text stays in that notebook's history).
* From then on the copies are two pages: a change of one reaches the other only by copying again.
* `brulion-cli extension copy <name> --to <notebook> [--replace]` does the same from the command
  line — and approves nothing (the command line never does).
* An extension page is a Markdown file: it travels by any way a note does (an export, a file sent
  by mail, a shared folder) and waits for approval wherever it arrives.

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
| see what changed since it was approved here | `extension diff "Word count"` — the code against the version approved on this device |
| its settings on this device | `extension settings "Gym"`, `--set rest=90` |
| copy it to another notebook | `extension copy "Word count" --to ~/Notes/Work` (with the pages it requires; approves nothing) |
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
| The command is not in the palette | The page's state line says why: an error (the version before it still runs, without the new command), "new or changed elsewhere" (press **Read and run**), off on this device, or another page with the same title. A command with `block = true` shows only while the caret is in an editor |
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
| A variable is gone | Variables live until the code is loaded again — every save of the page. Keep what must stay in a note (or in a setting: `b.setSetting`) |
| "waits for approval" from `b.require` | The page of shared code changed on another device (or is new): read it and run it (Settings → Extensions); the extensions using it load again by themselves |
| A hook writes the same line twice | Both devices' hooks answered: the second heard the first one's write through the sync. Skip `e.source == "sync"` |
| The command's shortcut does nothing | Settings → Keyboard shortcuts shows whose it is: the app's own commands and earlier ones win; give it another there |

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
| `b.timer` | 1 … 86 400 s; a label of 100 characters, a message of 300 (cut, one line); one timer per extension |
| Questions | a prompt's first line 300 characters, under it 200 lines and 10 000 characters (cut, "…"); `b.choose` 500 options of 500 characters, `b.form` 50 fields (more: an error); a label 200 characters, a text field's value 10 000 |
| `b.settings` | 32 settings, a text of 4000 characters, 64 KB of values per extension (on this device) |
| `b.require` | 16 pages deep; only extension pages approved and on here; a cycle is an error |
| `ctx.selection` | 200 000 characters |
| `b.ai` | 200 000 characters and 8 pictures a request, 64 tools, 5 minutes an answer; on the device also the model's context (a longer request is cut) and 2 048 tokens an answer |
| Hooks | 256 in all; three failures in a row take one off; hooks answering hooks end after four rounds; 200 `"task"` events of one burst |
| Values crossing | 64 levels of nesting |

* One VM per notebook (made again when the language changes or on "Reload extensions"; an extension
  page that changes is loaded again alone). Every extension has an environment of its own (its
  globals) whose reads fall through to the frozen standard library, and its own read-only `b`; the
  library tables, the string metatable and the globals are read-only (`luaL_sandbox`). Nothing
  reaches files, the network, processes or the environment: Luau's library has no `io`,
  `os.execute`, `require`, `load`, `string.dump`; `print` goes to the event log. Brulion compiles
  the source itself — scripts never see a compiler or a bytecode loader.
* **Notes only**: `b.write` and `b.appendToPage` make and change pages (`.md` files; a name's
  leading dots and `..` are taken out), `b.appendToJournal` writes daily notes, and `b.replaceBlock`,
  `b.insertBlock`, `b.removeBlock`, `b.setTask` and `b.setProperty` take only the ref of a line of a
  note the index knows. `b.attachments` lists the other files' paths and reads none of them. No function reads or writes another file of the
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
* **The browser too.** The same hostile extensions — loops for ever, in `pcall`, in an error
  handler, a coroutine or a sort's comparator, backtracking patterns, a plain search for ever,
  memory eaten, endless recursion, a module that never ends, modules in a cycle, a megabyte of
  question, thousands of options or fields, a long timer label, a look for a way out, writes to the
  library and to `b` — end as these limits say, in time, the engine working afterwards, on the
  desktop (`tst_extensions_hostile`) and in the browser build, single-threaded with JavaScript's
  clock (CI's smoke test in Chrome: `scripts/wasm-smoke.mjs --extensions`, `?selftest=extensions`,
  `extensions::hostileSelfTest`).
* Luau errors are `setjmp`/`longjmp` (`LUA_USE_LONGJMP=1`; the browser's Qt links without C++
  exception catching): the engine's C functions never keep a C++ object alive when they raise or
  yield — each is a thin wrapper around an implementation that returns first (`engine.cpp`).
* Values cross as `QVariant`s and never run script code on the way (no metamethods): a whole
  number arrives as an integer, a list as a `QVariantList`, a record as a `QVariantMap`.

## Layers

| Where | What |
|---|---|
| `3rdparty/luau` | Luau 0.743: the VM and the compiler, and the type checker (Analysis, Config: `brulion_luau_analysis`, with `BRULION_LUAU_CHECK`); patched (see its CMakeLists.txt): the plain `string.find` honours the time limit; numbers are read and written with `.` whatever the C locale (Qt sets it from the environment — with a Polish locale Luau's own `strtod` rejected `82.5`) |
| `src/core/extensions/engine.h` | `Engine`: one VM; extensions, their commands and views; calls, questions, limits |
| `src/core/extensions/host.h` | `Host`: what an extension may ask of the app (blocks and pages by tag and property, a query from an offset, attachments, the revision, writes — a page, a journal, a block's line, status and property, blocks inserted and removed —, today and now, the week's first day, languages, the timer, the log) |
| `src/core/extensions/notebookhost.h` | `NotebookHost`: the Host over a `Vault` and its `VaultIndex` — blocks from the index, content-preserving writes (`blockedit.h`, `notebook/journals.h`, `notebook/pages.h`, `markdown::setTaskStatus`), every ref resolved the same way (`resolveRef`: a note's line, stale when changed); a dry run that only computes them (`brulion-cli extension …`, `brulion-cli habit …`) |
| `src/core/extensions/blockedit.h` | the pure edits behind the writes: a block's line replaced, lines inserted around a block or under it, a block removed, a property set; `plainWords` (a block's `plain`, `b.plain`) |
| `src/core/extensions/sandbox.h` | `checkExtension()`, `checkExtensionPage()`: an extension loaded in an engine of its own over a dry-run `NotebookHost` — `b.checkExtension`, `brulion-cli extension check / write` |
| `src/core/extensions/typecheck.h` | Luau's type checker over an extension's code against `builtin/b.d.luau` (`typeCheck`: warnings with the page's lines; `typecheck_none.cpp` in a build without it) |
| `src/core/extensions/selftest.h` | hostile extensions against the engine where it runs (`hostileSelfTest`: the tests and the browser's `?selftest=extensions`) |
| `src/core/extensions/events.h` | what the hooks hear of a burst of changes: `ChangeBatch` (each extension's `"change"`, `"task"` and `"journal"` events — never its own writes, a chain of hooks cut after `kMaxDepth` rounds), a note's tasks and the tasks whose status changed (`taskStates`, `changedTasks`), where a change came from (`Origin`) |
| `src/core/extensions/share.h` | an extension copied into another notebook with the pages it requires (`copyExtension`, `requiredNames`), an approval written into a notebook's device state (`approveInStore`, `approvedCode`) |
| `src/core/extensions/builtin.h` | the built-in extensions (resources `:/extensions/…`: `silownia.luau` with `exercises.json`, `nawyki.luau`, `asystent.luau`), `builtinFor()`: which one a page overrides |
| `src/cli/commands_habits.cpp` | `brulion-cli habit list / set / tick / skip`: `nawyki`'s commands `list` and `set` over a dry-run `NotebookHost`, the write it computed applied through the command line's own edit (its lock, `--dry-run`, `--diff`, hashes, exit codes) |
| `src/core/ai/ai.h` | AI for `b.ai`: the request from a script's table (pictures through an `ai::ImageLoader`), the HTTP requests of Anthropic's Messages API and of OpenAI-compatible chat APIs, reading their answers and errors; `ai::Client` over `sync::HttpTransport`, the provider "local" handed to an `ai::LocalBackend` |
| `src/core/ai/local.h`, `localfiles.h`, `localrunner.h` | the model on the device: the models the app knows, LFM3's chat format (a request as pieces of text and control tokens, the answer read back, a long request cut), the models' files (downloads), what runs a model and the plugin with it (`llamarunner.h`: llama.cpp, only in builds with `BRULION_LOCAL_AI`) |
| `src/app/shell/localai.h` | `AppController.localAi`: the chosen model, its download and removal, requests answered on a worker thread |
| `src/core/extensions/pageextension.h` | extension pages: which pages are extensions, their id, their code with the page's line numbers, permissions, the hash an approval covers, the diff an approval shows (`codeDiff`, over `sync::diffLines`); the new-extension page and a built-in's source as a page |
| `src/app/shell/workspaceextensions.h` | `WorkspaceExtensions` (QML `Workspace.extensions`): the engine for the open notebook, the built-ins the settings switch on, the extension pages and their approval on this device (the version read: `approvalReview`, `approveVersion`), reloading a page's code when it or a page it requires changes, hooks and their events (`notesChanging` / `notesChanged` → `ChangeBatch`, "open", "day", "timer"), the console, questions, messages, the extensions' timers, settings, the editor's selection (`offerSelection`), copies to other notebooks; a turn of the AI chat (`runChat`: the assistant's `chat`, its answer, failure and `b.status` as signals, its question named by `questionChat`) |
| `src/app/shell/workspacechats.h` | `WorkspaceChats` (QML `Workspace.chats`): the AI chat's conversations as pages (core `chat`), one turn at a time, Stop, Try again (docs/UI.md "AI chat") |
| `src/cli/commands_extensions.cpp` | `brulion-cli extension …` (docs/CLI.md "Extensions"): this reference (bundled), the extension pages and their state on the device, a sandbox for checks and runs, writing a page |
| `src/app/shell/extensionmodels.h` | `ExtensionListModel` (Settings → Extensions), `ExtensionConsoleModel` (the console's lines) |
| `src/app/shell/extensionviewmodel.h` | `ExtensionView` (QML): a view's node tree as a list; `qml/shell/ShellExtensionView.qml` draws it |
| `qml/views/ConsoleView.qml`, `qml/views/ExtensionRouteView.qml` | the Luau console (route `console`), an extension page's view (route `extension`) |
| `qml/views/GymView.qml`, `qml/views/HabitsView.qml` | the built-ins' own views (routes `gym`, `habits`): a header of QML — tabs, a month row, buttons — over `ShellExtensionView` |
| `qml/shell/ShellExtensionQuestion.qml` | the dialog of `b.ask` / `b.choose` / `b.form` (number and text fields) |
| `qml/shell/ShellExtensionApproval.qml` | "Read and run": the code against the version approved before (`BDiffView`), the permissions, the version shown approved (`approvalReview`, `approveVersion`) |
| `qml/shell/ShellExtensionSettings.qml`, `ShellExtensionCopy.qml`, `ShellTimerBanner.qml` | an extension's settings (`settingsOf`, `setSetting`), "Copy to another notebook" (`copyToNotebook`), the extensions' timers under the top bar (`timers`) |

A new function of `b`: its implementation in `engine.cpp` (a thin `api…` wrapper around an `…Impl`
that returns before anything is raised), what it needs of the app in `Host` (`host.h`,
`notebookhost.cpp`, the tests' fake hosts), a test in `tests/core/extensions`, and its line in the
three references — this document and the documentation notebook's "Extension API" page in both
languages; `tst_extensions_docs` fails until all three name it.

**Why Luau**: of eleven languages measured with the same extensions, tests and hostile scripts (the
extension language experiment, branch `experiment/extension-languages`), it was the only ready-made
one whose sandbox, time limit and memory limit held without changes — also in the single-threaded
browser build — and the fastest; its type checker (Analysis, vendored for the check on save:
"Types") finds mistakes with line and column.

## Tests

`tests/core/extensions`: the engine (sandbox escapes, limits that cannot be caught, questions,
views, hooks, evaluation, the locale), what the hooks hear (`tst_extensions_events`: a task found
again after an edit — ticked, a Logseq keyword, a recurring one, one text twice, a text rewritten —,
each extension's events of a burst without its own writes, chains cut, daily notes made, a flood of
tasks), the samples' walls and the extension's own corner (`tst_extensions_shared`: pages of one
name as two paths, shared code with a copy and the permissions of each user, a cycle, a page that
is no extension or not approved, a module that fails or never ends, settings declared, kept, checked
and refused, timers with their words, what a question may show, the diff of an approval), the type
checker (`tst_extensions_types`: the mistakes it finds with the page's lines, none in code that works
— every built-in, sample and documentation example —, the declarations naming every function of
`b`, hostile code that ends the check in time), hostile extensions (`tst_extensions_hostile`, the
same checks the browser runs: `selftest.h`), block edits, the API added for the habits on a real notebook
(`tst_extensions_api`: blocks and pages by property, `plain`, `path`, a query's pages, attachments,
the revision after a write, the date, name and recurrence helpers, `b.appendToPage`, inserting and
removing blocks — the note's line endings and a last line without a break kept —, a task's status
and a block's properties, stale and forged refs, the permission), extension pages (ids, code with the page's
lines, fences in lists and in other fences, permissions, the hash, the new-extension page and the
gym's source as a page running like the built-in), the notebook host (the dry run, refs that name
notes only), the gym on a real notebook (writing lines back in Brulion's spelling, history, plans
with progression, today's view and its actions, a stale line, the catalog, hostile lines, two
years of workouts), the habits on a real notebook (`tst_extensions_habits`: schedules in both
languages and the words they read back as, names and keys, definitions and checks, streaks and
rates under every kind of schedule — skipped days, days without a note, a first period begun, DST,
the week's first day —, the month grid, writing a check with every other byte kept, the month
view's nodes, `set` with its Undo, `add`'s form asked again, `tick`, the empty state, 2000 daily
notes × 8 habits within the time limit), the AI assistant as an agent (a turn of a conversation: its history, the page it
is about, what it says it does), the documentation (`tst_extensions_docs`: every
```` ```luau ```` example of this document, of docs/SYNTAX.md and of the documentation notebook's
pages in both languages loads and runs on a real notebook — its commands with their questions and
the model answered, its views and their actions, its hooks —, both languages have the same
examples, and the references name every function of `b`, of `b.ui` and every hook event), and
**ten sample extensions** (`tests/core/extensions/samples/extensions`: pages as people write them,
Polish and English — a word count, a task review, a mood log, expenses, a project index kept by
hooks, a pomodoro timer, an extension that writes extensions, flashcards, a link check, action
items by AI) at work on a notebook of their own, all in one engine (`tst_extensions_samples`:
every command with its questions, every view held to what "Views" says a node is, every tap, the
hooks, both languages, a notebook of two thousand pages, notes with lines of 100 000 characters
and numbers that are none).

`tests/app/shell/tst_extensions.cpp` runs the same pages in the app's own class
(`WorkspaceExtensions`, wired as the Workspace wires it): approval per device — of the version that
was read, with the diff against the version before and a sync in between —, the questions the
app shows, an extension's own writes that are no "change" and the user's edits that always are,
another extension's writes that are (a tick in the review's view rewriting the project index),
two extensions answering each other until the chain ends, "task", "journal", "open", "day" and
"timer" hooks, settings set as the dialog sets them, shared code waiting while its page waits, the
editor's selection, a copy to another notebook with its library and its conflict,
code an extension writes waiting for approval, the editor's saves of an approved page;
`tests/app/shell/tst_habits.cpp` (`tst_shell_habits`) the Habits view as the app draws it — an
`ExtensionView` over the real `nawyki` (cards in the notebook's order with their menus, problems,
another month, a tick through the view and its toast's Undo, a tick in the journal drawn again in
place, the Add habit form, the extension off);
`tests/app/content/tst_content_chat.cpp` the AI chat on a real Workspace with the model on the device
played (a conversation's page and the journal's line, its turns, a change asked in it, Stop, a
failure and Try again). The app's
self-test: `checkGym` switches the gym on, writes a plan into today's note through the dialog and
adds a set in the view; `checkHabits` opens Habits from the palette, adds a habit through the form's
text fields (a schedule not understood asked again), ticks today's check and opens it again with
real taps, presses the toast's Undo and ticks a past day of the grid; `checkExtensionPages` writes an extension page as a sync would (it waits),
approves it, opens its view from the palette, saves a change in the editor (it runs at once, still
approved), saves a broken one (the error on its line, the previous version runs) and evaluates in
the console.
