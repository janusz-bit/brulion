# Extensions (Luau)

Brulion's extensions are small programs in [Luau](https://luau.org) (a typed Lua) that add commands
and views to the app and read and write the open notebook through one API, `b`. Today only the
built-in ones run — the **gym** (`src/core/extensions/builtin/silownia.luau`, docs/SYNTAX.md §5
"Gym", docs/UI.md "Gym"); extensions written by users inside their notebook are the next step
(TODO.md "Extensibility as in Emacs and TiddlyWiki").

Why Luau: of eleven languages measured with the same extensions, tests and hostile scripts (the
extension language experiment, branch `experiment/extension-languages`), it was the only ready-made
one whose sandbox, time limit and memory limit held without changes — also in the single-threaded
browser build — and the fastest; its type checker finds mistakes with line and column.

## Layers

| Where | What |
|---|---|
| `3rdparty/luau` | Luau 0.743, the VM and the compiler only, patched (see its CMakeLists.txt): the plain `string.find` honours the time limit; numbers are read and written with `.` whatever the C locale (Qt sets it from the environment — with a Polish locale Luau's own `strtod` rejected `82.5`) |
| `src/core/extensions/engine.h` | `Engine`: one VM; extensions, their commands and views; calls, questions, limits |
| `src/core/extensions/host.h` | `Host`: what an extension may ask of the app (blocks, pages, writes, today, languages, the timer, the log) |
| `src/core/extensions/notebookhost.h` | `NotebookHost`: the Host over a `Vault` and its `VaultIndex` — blocks from the index, content-preserving writes (`blockedit.h`, `notebook/journals.h`, `notebook/pages.h`) |
| `src/core/extensions/builtin.h` | the built-in extensions (resources `:/extensions/…`) |
| `src/app/shell/workspaceextensions.h` | `WorkspaceExtensions` (QML `Workspace.extensions`): the engine for the open notebook, the extensions the settings switch on, their questions, messages and the rest timer |
| `src/app/shell/extensionviewmodel.h` | `ExtensionView` (QML): a view's node tree as a list; `qml/shell/ShellExtensionView.qml` draws it |
| `qml/shell/ShellExtensionQuestion.qml` | the dialog of `b.ask` / `b.choose` / `b.form` |

## Sandbox and limits

* One VM per notebook (made again when the settings or the language change). Every extension has
  an environment of its own (its globals) whose reads fall through to the frozen standard library,
  and its own read-only `b`; the library tables, the string metatable and the globals are
  read-only (`luaL_sandbox`). Nothing reaches files, the network, processes or the environment:
  Luau's library has no `io`, `os.execute`, `require`, `load`, `string.dump`; `print` goes to the
  event log. Brulion compiles the source itself — scripts never see a compiler or a bytecode
  loader.
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
* Patterns backtrack: `^(%d+[,.]?%d*)%s*(%a+)` on a line of 20 000 digits is quadratic (seconds).
  Read numbers in anchored steps (`silownia.luau`, `readNumber`).

## The API (`b`)

Reading the notebook:

| Function | |
|---|---|
| `b.pages({tag?})` | page names (sorted, Polish order); with `tag` the pages whose `tags` property has it |
| `b.page(name)` | `{name, tags, props, date?}` or nil |
| `b.read(name)` | the page's text or nil |
| `b.blocks({page} \| {tag, journal?, from?, to?})` | blocks from the index: a page's top-level blocks, or every block carrying a tag (daily notes only with `journal`; `from`/`to` ISO dates), each `{ref, page, date?, text, task?, tags, props, children}` — `text` is the first line after the marker and checkbox, verbatim; `task` the status character (`" "`, `"x"`, `"-"`, `"/"`, `">"`; a Logseq keyword as its character) |

Writing (permission `write`; every write is the whole file through the vault, other lines byte for
byte):

| Function | |
|---|---|
| `b.write(name, content)` | creates or replaces a page |
| `b.appendToJournal(date, markdown)` | a block at the end of that day's note (created from the template; a template's empty bullet is filled) |
| `b.replaceBlock(ref, text, task?)` | the block's first line gets `text` (its indentation, marker, Obsidian Tasks dates and block id kept); `task` sets the status (✅ / ❌ dates as the editor). Refused with `stale: …` when the line changed since `ref` was read (the same text up to 50 lines away still counts): a sync arriving mid-workout is never overwritten. Returns the new ref |

The rest: `b.today()`, `b.daysBetween(a, b)`, `b.sorted(list)` (Polish order, numbers by value),
`b.fold(s)` (lower case, no diacritics: "Leżąc" → "lezac"), `b.formatNumber(n, decimals?)` (a decimal
comma in Polish notes), `b.lang()` (the notes' language), `b.uiLang()` (the interface's),
`b.log(text)`, `b.timer(seconds, label)` (the rest timer: a countdown in the app, a notification
at the end), `b.data(name)` (read-only data bundled with the extension: the gym's
`exercises.json`).

Registering: `b.command(name, fn(ctx), {label, block?})` — a palette command ("Siłownia: dodaj
serię"); with `block = true` it works on the block under the caret (`ctx.block`, the editor saves
first) and is offered only in an editor. `b.view(name, render(params), actions?)` — a view and its
named actions.

## Questions

`b.ask(prompt)` → text (`""` when cancelled), `b.choose(prompt, options)` → one option or nil,
`b.form(title, fields)` → `{key = value}` or nil (fields `{key, label, kind = "number" | "text",
value, step, unit}`: numbers with − / + and typing, `82,5` too). Each suspends the call: the engine
returns the question, the app shows it (ShellExtensionQuestion), and the answer resumes the call
where it stopped — no callbacks, no nested event loop. Refused at load time, inside a coroutine of
the script and inside library callbacks. A new call abandons a waiting one.

## Views

A view is a function of the notebook that returns nodes made with `b.ui`; the app draws them with
its own components (nothing of the extension is QML or HTML):

`column(children)`, `row(children)` (a card of controls that wrap), `heading(text)`,
`text(text, muted?)`, `link(page, label?)`, `button(label, action, args?)`, `check(checked, action,
args?)` (the tasks' checkbox), `stepper{value, step, unit?, action, args?}` (− value +),
`chart{kind = "line" | "bar", title?, labels, series = {{name, values}}}` (the charts' own drawing;
nil values are gaps), `table(columns, rows)`, `empty(title, text)`, `open(label, view, params)`
(another view of the extension), `run(label, command)`.

An action is called with the node's `args` (a stepper adds `value`); it changes notes, and the view
renders again — after every command and action and whenever the index changes. The args carry what
the action needs (a block's `ref` and `text`), so a stale line is refused instead of overwritten.

## Tests

`tests/core/extensions`: the engine (sandbox escapes, limits that cannot be caught, questions,
views, the locale), block edits, and the gym on a real notebook (writing lines back in Brulion's
spelling, history, plans with progression, today's view and its actions, a stale line, the
catalog, hostile lines, two years of workouts). The app's self-test (`checkGym`) switches the gym on,
writes a plan into today's note through the dialog and adds a set in the view.
