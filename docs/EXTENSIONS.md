# Extensions (Luau)

Brulion's extensions are small programs in [Luau](https://luau.org) (a typed Lua) that add commands,
views and reactions to the app and read and write the open notebook through one API, `b`. They come
from two places:

* **built in** — the **gym** (`src/core/extensions/builtin/silownia.luau`, docs/SYNTAX.md §5 "Gym",
  docs/UI.md "Gym"), switched on in Settings → Extensions;
* **the notebook itself** — pages tagged `#rozszerzenie` (`#extension`) whose ```` ```luau ````
  blocks are code (docs/SYNTAX.md §13): the notebook carries its own code, edited, synced, linked
  and versioned like any note. Saved, a page's code runs again at once.

The idea is Emacs' and TiddlyWiki's: the program you use is made of code you can read and change
from inside it. Emacs gives every user an init file and `eval-buffer`, a `*scratch*` buffer and a
`*Messages*` log; TiddlyWiki keeps its own code in tiddlers, and a tiddler with a built-in one's
title replaces it (a "shadow tiddler"). Brulion has the same pieces: extension pages, the Luau
console, and the built-in gym's source as a page that then runs instead of the built-in code.

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
| `src/core/extensions/builtin.h` | the built-in extensions (resources `:/extensions/…`), `builtinFor()`: which one a page overrides |
| `src/core/ai/ai.h` | AI for `b.ai`: the request from a script's table, the HTTP requests of Anthropic's Messages API and of OpenAI-compatible chat APIs, reading their answers and errors; `ai::Client` over `sync::HttpTransport` |
| `src/core/extensions/pageextension.h` | extension pages: which pages are extensions, their id, their code with the page's line numbers, permissions, the hash an approval covers; the new-extension page and a built-in's source as a page |
| `src/app/shell/workspaceextensions.h` | `WorkspaceExtensions` (QML `Workspace.extensions`): the engine for the open notebook, the built-ins the settings switch on, the extension pages and their approval on this device, reloading a page's code when it changes, hooks, the console, questions, messages and the rest timer |
| `src/app/shell/extensionmodels.h` | `ExtensionListModel` (Settings → Extensions), `ExtensionConsoleModel` (the console's lines) |
| `src/app/shell/extensionviewmodel.h` | `ExtensionView` (QML): a view's node tree as a list; `qml/shell/ShellExtensionView.qml` draws it |
| `qml/views/ConsoleView.qml`, `qml/views/ExtensionRouteView.qml` | the Luau console (route `console`), an extension page's view (route `extension`) |
| `qml/shell/ShellExtensionQuestion.qml` | the dialog of `b.ask` / `b.choose` / `b.form` |

## Sandbox and limits

* One VM per notebook (made again when the language changes or on "Reload extensions"; an extension
  page that changes is loaded again alone). Every extension has an environment of its own (its
  globals) whose reads fall through to the frozen standard library, and its own read-only `b`; the
  library tables, the string metatable and the globals are read-only (`luaL_sandbox`). Nothing reaches files, the network, processes or the environment:
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
first) and is offered only in an editor. `b.view(name, render(params), actions?, {label?, icon?})` —
a view and its named actions; with a `label` the palette offers it ("Projekty"), on the route
`extension` (`{extension, name, …params}`). `b.on(event, fn(e))` — a hook (below).

## Hooks

`b.on(event, fn)` calls `fn` with one table when something happens in the notebook (Emacs'
`add-hook`):

| Event | When | `e` |
|---|---|---|
| `save` | this device's editor saved a page (after the save) | `{page, path}` |
| `change` | notes changed — the editor, a sync, another program, an import; once a burst of changes is over (0.6 s) | `{pages = {names…}}` |

What extensions write themselves never comes back to them as `change` (no echoes, no loops). A
hook never asks a question (`b.ask` there is an error), runs under the same limits, and one that
fails three times in a row is taken off until its extension loads again (Emacs does the same to a
failing `post-command-hook` function); its errors go to the console and the event log.

## Extensions in the notebook

A page is an extension when its page-level tags have `rozszerzenie` or `extension` (frontmatter
`tags:` or a `tags::` line; a `#rozszerzenie` inside a block does not count). Its code is every
```` ```luau ```` (or ```` ```lua ````) block, in order, also inside a list item as Logseq writes it;
prose, headings and other blocks around it are documentation (literate programming, as an
Emacs `config.org`). The page's properties (docs/SYNTAX.md §13): `uprawnienia::` / `permissions::`
(`zapis` / `write`, `ai`), `opis::` / `description::`, `zastępuje::` / `overrides::` (a built-in's id).

* **Its id** is its title folded ("Licznik słów" → `licznik-slow`): commands are
  `ext.licznik-slow.<name>`. Two pages with one id: the first by path runs, the other says so.
* **Line numbers**: the code is loaded with every other line of the page blanked, so an error names
  the page's own line ("Licznik słów:14: attempt to index nil") — in Settings, in the page's state
  line, in the console and the event log. The page's state line has a button to that line.
* **Approval, per device.** Code can arrive from anywhere: a sync, an import, a shared folder. A
  page's code runs on a device only after it was approved there (Settings → Extensions, or "Run" on
  the page's state line) — like VS Code's workspace trust or Emacs' questions before `eval` of a
  file's local variables. What is approved is the SHA-256 of the code and the permissions, kept in
  the notebook's device state (`LocalStore` `extensions/approved`, never synced); prose around the
  code needs no new approval. Code written on this device stays approved: when the app's editor
  saves an approved page, the new code is approved too — unless it asks for more permissions. Code
  that changes any other way (a sync, another program, an extension's `b.write`) waits again.
  "New extension" and "Edit the code" of a built-in write pages that are approved at once.
* **Live.** Whenever a page changes, its code is loaded again (only that extension); its commands
  and views follow. A save that does not load (a syntax error, an error at the top level) keeps the
  version before it running and shows the error: half-written code never takes a working extension
  away. "Reload extensions" (the palette) starts every extension in a fresh VM.
* **Switched off** on this device in Settings → Extensions (`extensions/off`).
* **Built-ins as pages.** "Edit the code" of the gym (Settings → Extensions) writes its source into
  `Rozszerzenia/Siłownia.md` (`Extensions/Gym.md` in an English notebook): `zastępuje:: silownia`,
  one ```` ```luau ```` block per section of the source under its heading. While that page exists
  and is approved, the gym runs from it — with the built-in's bundled data (`b.data`) — and a
  change there works at once; "Restore" puts the page in the trash and the built-in code runs
  again. A page titled like a built-in ("Siłownia", "Gym") overrides it too, as TiddlyWiki's
  shadow tiddlers. The built-in's switch still decides whether the gym runs at all.

## The Luau console

The palette's "Luau console" (route `console`): a line of Luau runs in an environment of its own
with the notebook's `b` and the permissions to write and to use AI (what is typed there is the
user's own). An expression shows its value (`b.today()`, `#b.pages({})`; tables as Luau literals),
statements run, globals stay for the next line (Emacs' `ielm`); Up / Down go through the history.
Below, as in Emacs' `*Messages*`: what extensions `print()` and `b.log`, their errors with lines,
"loaded again". "Run Luau block" (the palette, in the editor) evaluates the ```` ```luau ```` block
under the caret there — a block of any page, with the page's line numbers in errors — and shows the
result as a message too (Emacs' `C-x C-e`). Code typed in the console may register commands and
views (`b.command` …) until the extensions are reloaded.

## Questions

`b.ask(prompt)` → text (`""` when cancelled), `b.choose(prompt, options)` → one option or nil,
`b.form(title, fields)` → `{key = value}` or nil (fields `{key, label, kind = "number" | "text",
value, step, unit}`: numbers with − / + and typing, `82,5` too). Each suspends the call: the engine
returns the question, the app shows it (ShellExtensionQuestion), and the answer resumes the call
where it stopped — no callbacks, no nested event loop. Refused at load time, inside a coroutine of
the script and inside library callbacks. Any number of calls may wait at once (questions, AI
answers) while others run; the app shows one question at a time — a new one answers the older
"cancelled". A view never waits; a hook never asks.

## AI (`b.ai`)

`b.ai(prompt)` or `b.ai({prompt = …, system = …, model = …, maxTokens = …, effort = …})` (or
`messages = {{role = "user" | "assistant", content}, …}` for a conversation) asks the model the
user chose on this device and returns its text and the whole reply (`{text, model, stopReason,
inputTokens, outputTokens}`); a failure is an error at the script's line ("b.ai: the API key was
refused (HTTP 401): …", "b.ai: no connection: …", "b.ai: the model declined to answer"). It needs
the permission `ai` (`uprawnienia:: ai`, approved with the page like `zapis`; the console has it).
The call waits for the answer like a question: nothing blocks — other commands, views and the UI
go on — and its result is handled as the command's (a message, the console).

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
AI, off by default): "AI: ask about this page" asks for a question (`b.ask`), sends it with the page
on screen (at most 60 000 characters) and writes the answer into today's note — `- 🤖 <question>
([[Page]])`, the answer's lines as items under it; "AI: summarise this page" does the same with a
summary and the page's tasks. Both are a tap away on the top bar's ✨ button while the assistant is
on (docs/UI.md "Top bar": the page on screen, in the journal's stream today's note; a spinner while
`aiPending`). Like the gym, its source can become a page of the notebook (title
"Asystent AI" / "AI assistant", or `zastępuje:: asystent`) and be changed there.

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
views, hooks, evaluation, the locale), block edits, extension pages (ids, code with the page's
lines, fences in lists and in other fences, permissions, the hash, the new-extension page and the
gym's source as a page running like the built-in), and the gym on a real notebook (writing lines
back in Brulion's spelling, history, plans with progression, today's view and its actions, a stale
line, the catalog, hostile lines, two years of workouts). The app's self-test: `checkGym` switches
the gym on, writes a plan into today's note through the dialog and adds a set in the view;
`checkExtensionPages` writes an extension page as a sync would (it waits), approves it, opens its
view from the palette, saves a change in the editor (it runs at once, still approved), saves a
broken one (the error on its line, the previous version runs) and evaluates in the console.
