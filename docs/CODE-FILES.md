# Brulion "Code" — a project's files (module `codefiles`, area `codefiles`)

What T3 Code calls workspace files (its diffs.md "Workspace files", composer.md mentions,
keybindings.md `mod+p` / `mod+shift+f`): beside an agent's thread the user finds a file of the
project by name (**the file picker**, Ctrl+P), reads it (**the file viewer**: syntax colours, line
numbers, pictures, a line marked), searches the project's text (**project search**,
Ctrl+Shift+F), names files in a message (**@-mentions**), comments on lines (**review comments**,
into the composer) and follows the files an agent's answer links to (**links in answers**). Two
layers:

| Layer | Where | What |
|---|---|---|
| core module `codefiles` | `src/core/codefiles/` (QtCore only, namespace `brulion::codefiles`) | `FileIndex` (`fileindex.h`), the fuzzy matcher (`fuzzy.h`), ripgrep / git grep argument lists and parsers and Brulion's own search (`search.h`), the viewer's reading, file links, @-mentions, review comments (`viewer.h`), syntax colours (`highlight.h`, the languages in `highlight_languages.cpp`) |
| app area `codefiles` | `src/app/codefiles/` (library `brulion_codefiles`), `src/app/qml/codefiles/` | `FileIndexCache` / `CodeFileIndex`, `CodeFileDocument`, `CodeSearch`, the `CodeFiles` singleton; the panels and the picker |

Tests: `tests/core/codefiles` (`tst_codefiles`: fuzzy matching, the index, the viewer's helpers,
mentions, links, review comments, a generated tree of 50 000 files timed; `tst_codefiles_search`:
the search's arguments and parsers, with real `rg` and `git` where they are on PATH;
`tst_codefiles_highlight`: every lexer, states over lines, garbage) and `tests/app/codefiles`
(`tst_codefiles_app`: the index over a real repository and a plain folder, queries off the GUI
thread, the viewer's model, the three search engines, the QML helpers). `tests/app/shell`
`tst_shell_agents` (`codeThreads`) checks the links an agent gets for a thread's @-mentions.

## The index

`FileIndex` holds the files and folders of a folder as paths relative to it, `/`-separated, sorted,
in one buffer (about twice the paths' characters plus 12 bytes an entry); folders are derived from
the files' paths (a folder that holds no file is not listed). Input that is no relative path inside
the folder — absolute, with `.` or `..` parts, a NUL or a line break — is skipped
(`cleanRelativePath()`). At most **200 000 entries** (files and folders together): what does not
fit is left out and the index says it is `truncated` (the picker says so).

Where the files come from (`FileIndexCache::build()`):

* a git work tree: `git -C <folder> ls-files -co --exclude-standard -z` through `gitcli::Client`
  (no shell, no prompts, read-only, the environment rules of docs/CODE-GIT.md "Security"; at most
  64 MiB of output) — tracked and untracked files, ignored ones not, its output parsed on a pool
  thread;
* else (no git, no repository): a breadth-first walk of the folder on a pool thread
  (`FileIndex::walk()`) — no symbolic link to a folder followed, at most 32 levels, hidden folders
  (`.git`, `.cache` …), `node_modules`, build output (`build`, `build-*`, `cmake-build-*`, `dist`,
  `out`, `target`, `obj`, `__pycache__`, `venv`, `result` …) skipped; hidden files listed.

`FileIndexCache` (one for the app) keeps one index per folder (at most 8 folders, the least
recently asked for goes) and shares it between every view of it; while a build runs the last
index answers. It builds again when asked: each turn's end (CodeView, when a thread's answer is
written), every 15 s while the picker is open (`CodeFileIndex.live`), Reload — and by itself when the
disk changes: a `QFileSystemWatcher` on the folder and its indexed folders (`FileIndex::folders()`,
shallow first, at most `MaxWatched` = 2 000 — the system's inotify watches are shared) builds it again
`WatchDelayMs` (400 ms, coalesced) after a file is made, deleted or renamed in one of them; the
watched set follows each build (a new folder watched, a deleted one dropped). Not on Windows, where a
watched folder cannot be deleted while its change handle is open (a worktree removed, a folder the
agent deletes): there the refreshes above follow the disk. `CodeFileIndex` (QML)
is one view of a folder's index: `query`, `kinds` ("all", "files", "folders"), `limit`, `recent`
(paths listed first while nothing is typed), `active` (false: nothing computed — a thread's
composer while no "@" is being written); its `rows` carry `path`, `name`, `folder`, `isFolder`,
`title` (a folder's path with a "/"), `nameMarkup` / `folderMarkup` (escaped StyledText, the
matched letters bold in `markColor`) and `iconName`. Queries over at most 20 000 entries run at once
on the GUI thread, bigger ones on a pool thread (the newest query's rows are kept).

Measured with the generated tree of `tst_codefiles` (50 000 files in 1 111 folders; x86_64, one
core, 2026-11-26): building the index from `git ls-files -z` output **~35 ms** in
a release build (~300 ms in a Debug one); a query of ten letters over all of it **~4 ms** (Debug
~70 ms), a one-letter query **~6 ms** (Debug ~140 ms) — so the 20 000 entries a query may take on
the GUI thread cost ~2 ms there, and bigger indexes never hold typing up.

### Fuzzy matching (`fuzzy.h`)

The query's characters must appear in the path in order; case and Latin marks are ignored one
UTF-16 unit for one (`foldChar()`: "zolw" finds `Żółw.md`, `żółw` finds `Zolw.md`, positions stay
the path's own), spaces in the query are dropped. Among the ways to match, the best is scored by
dynamic programming (as fzf does): each character 16, more at the start or after "/" (10, doubled
for the first character), after `_`, `-`, `.`, a space or other punctuation (8), at a camelCase
hump or where letters turn to digits (7), +4 for each character right after the previous one,
−3 to open a gap and −1 for each further skipped character, +2 for an upper-case letter typed and
matched exactly. A query that fits inside the base name is scored there with a bonus of 48 —
the base name's hits come first —, +32 more when the name starts with it, +64 when the name (or
the name without its extension) is it; a query with a "/" is matched against the whole path. Ties:
the shorter path. Queries longer than 64 characters and paths longer than 512 match nothing.

## The file picker

`CodeFilePicker.qml` (T3 Code's `mod+p`): a popup like the command palette
(a floating panel in the window's upper third, full screen on phones) over `CodeFileIndex`
(`kinds: "files"`, 60 rows, `live` while open): the name with its matched letters marked, its
folder dimmed after it, the number of files at the bottom; the recent files (the viewer's, per
folder) first while nothing is typed. ↑/↓ (and Ctrl+N / Ctrl+P, Ctrl+J / Ctrl+K), Page up/down;
**Enter** opens the file in the viewer, **Ctrl+Enter** in the external editor
(`Workspace.projects.openInEditor(<absolute path>)`), Esc closes. A breadcrumb's folder in the
viewer opens the picker with that folder typed.

### Keys

In Code mode **Print page's key (Ctrl+P)** opens the file picker and **Search's key
(Ctrl+Shift+F)** the project search: AppShell's `shortcutEnabled()` / `runShortcut()` turn
`page.print` and `go.search` into the uiActions `code:findFile` and `code:search`, which CodeView
carries out (not while the terminal has the keyboard: it keeps Ctrl+P for the shell; Ctrl+Shift
chords reach the app anyway, so Ctrl+Shift+F in the terminal does nothing). A custom key of either
command works the same. The palette has them as **Go to file…** (`code.findFile`) and **Search in
the project** (`code.search`), in the Code view only.

## The file viewer

The right panel of Code mode has tabs (`CodePanelTabs`, kept in the device setting
`code/rightTab`): **Changes** (the git panel, docs/CODE-GIT.md), **Files** (`CodeFilesPanel`),
**Search** (`CodeSearchPanel`); the Files and Search tabs are made when first shown.
`CodeView.openFileInViewer(path, line)` opens a file there — a path relative to the thread's
folder or absolute inside it, `line` 0 for the top; anything outside the folder is refused with a
toast. The picker, the search's results and the chat's links call it; the Changes panel's and the
terminal's "open the file" are to call it too.

`CodeFileDocument` reads the file on a pool thread (`readForViewer()`): a picture by its extension
(png, jpg, jpeg, gif, webp, bmp, svg, ico — shown as a picture, decoded at most twice the panel's
size), a folder, a missing or unreadable file, a **binary** one (a NUL in its first 8 KiB, or more
than a tenth of it control characters: "Binary file" and its size), else its text — UTF-8,
invalid bytes as U+FFFD, a byte order mark dropped, at most the **first 2 MiB** cut after the last
whole line ("Only the first 2 MB of this 5.3 MB file are shown."). A path that leaves the folder
is never read ("Outside the project"). Each line is lexed for its colours on the same thread, and
the rows (`number`, `html`, `text`) give a line as escaped StyledText — tabs to spaces (`tabWidth`
4), every space a `&nbsp;` (StyledText folds runs of spaces), each span in its colour of `palette`,
at most 2 000 characters of a line —, never the file's own markup.

`CodeFileView` shows them in a ListView (only the lines in view exist): the numbers in a gutter
that stays at the left edge while long lines scroll sideways, the marked line tinted (`Theme.mark`:
the line a link or a search opened the file at, put in the middle), the selected ones in the
selection colour. Lines are **selected** by a click (Shift+click extends), a drag over the numbers,
Esc clears; over a selection a bar — "Comment on lines 12–14…", Copy the lines (Ctrl+C), clear —
and a right click's menu: Comment…, Copy the lines, Copy the path with the lines
(`src/a.cpp:12-14`). The panel's header: the recent files (a menu; kept per folder in the device's
settings, `code/recentFiles/<hash of the folder>`, at most 20), the path as a breadcrumb, Go to
file…, Copy path, Open in editor, Reload. A thread's answer written reads the file shown again,
keeping its place.

### Editing

The header's **Edit** (`codeFilesEdit`, a pencil) — for a text file read whole, valid UTF-8
(`CodeFileDocument.editable`: not cut at 2 MiB, not `TextShape::lossy`) — puts the file's text
(`editText()`) in a monospace editor (`codeFileEditor`, no wrapping) in place of the lines; **Save**
(Ctrl+S) writes it, **Cancel** (Esc while nothing changed) leaves it. `save(text)` writes atomically
(QSaveFile) what was typed in the file's own shape (`codefiles::shapeOf()` of the bytes read: its line
break — the first one in it —, a UTF-8 byte order mark, a break after the last line;
`encodeEdited()`), and refuses when the file changed on disk since it was read (a SHA-1 of its bytes):
"reload it, then edit it again" — never overwriting an agent's or another program's change. Saved, the
file is read again. Another file is not opened while the edit has changes ("Save or cancel the edit of
this file first.").

### Find in the file

Ctrl+F in the viewer (or its header's search button, `codeFilesFind`) opens `CodeFileFindBar` under
the header: what is typed is found a moment later in every line of the file shown
(`CodeFileDocument.find(pattern, caseSensitive, wholeWord, regex)`, the project search's rules —
`searchExpression()` / `matchRanges()`, **Aa**, **ab**, **.\*** —, at most 10 000 matches), each match
bold in the palette's `match` colour (`lineHtml()` splits the line at the spans' and matches' edges;
`Theme.accentText`); "3 of 12" (`matchCount`, `matchLine(i)`, `nextMatch(i, ±1)` wrapping around),
Enter the next one and Shift+Enter the one before — shown in the middle and marked —, an invalid
expression's reason (`findError`) in red, Esc closes it (the matches go). The file read again keeps
the query: its matches are found in the new text.

### Review comments

"Comment…" asks for a short text (`codeFileCommentDialog`), then the thread's composer gets
(`ChatView.addToComposer()`, nothing is sent):

````
src/app/main.cpp:12-14
```cpp
<the lines as they are>
```
<the comment>
````

— the fence longer than any run of backticks in the lines, its language from the file's
extension (`languageForPath()`). Comments on several files collect in one message.

### Syntax colours (`highlight.h`)

One lexer, driven by a table of languages (`highlight_languages.cpp`: line and block comments —
nested where the language nests them —, quotes, keywords, types, constants, builtins, the C
preprocessor, decorators and attributes, shell variables and sigils, case-insensitive keywords),
serves the C family (C, C++, Objective-C, GLSL, C#, Java, Kotlin, Scala, Groovy, Dart, Swift, Go,
Rust, Zig, WGSL, protobuf, HCL), JavaScript and TypeScript (with JSX), QML (its bindings as
properties), Python, Ruby, PHP, Perl, Elixir, Julia, R, shells (sh, bash, zsh, fish, PowerShell,
batch files), CMake, Nix, Makefiles, Dockerfiles, SQL, Haskell, OCaml, Erlang, LaTeX, Clojure, Vim
script and GraphQL; lexers of their own do markup (HTML, XML, SVG, Vue and Svelte: tags,
attributes, strings, comments, entities), JSON (keys apart from values), YAML, TOML and INI (keys,
sections), Markdown (headings, fenced code, inline code, links), diffs, `.gitignore` and CSS / SCSS
/ Less; Luau and Lua are core `syntax`'s lexer — the colours of a ```` ```luau ```` block in a
note. What a line leaves open goes to the next as a small state: block comments, triple-quoted
strings, template strings, Go's raw strings, C++ `R"x(…)x"` and Rust `r#"…"#` strings, Nix's `''…''`,
HTML comments and tags over lines, Markdown's fences. Malformed code never fails; lines past
10 000 characters get no colours there. The kinds and their colours (CodeFilesPanel's palette;
the first ones are the Luau blocks' of the editor): keyword `chipScheduledFg`, constant and number
`warningText`, string `chipRecurrenceFg`, comment `fgFaint`, builtin, type and property `link`,
function `fg`, preprocessor and tag `dangerText`, attribute `warningText`, variable `tagFg`,
inserted / deleted `diffAddFg` / `diffDelFg`.

## Project search

`CodeSearchPanel` (T3 Code's `mod+shift+f`) over `CodeSearch`: what is typed is searched for a
moment after the last key (Enter: at once), with toggles **Aa** (match case), **ab** (whole
words), **.\*** (regular expression); changing one searches again. Engines, the first there is:

1. **ripgrep** — `rg --json --no-config --no-messages --hidden --glob=!.git/ --sort=path
   --max-filesize=4194304 --max-count=200 [--case-sensitive|--ignore-case] [--fixed-strings]
   [--word-regexp] --regexp=<pattern> -- .` in the folder (QProcess, an argument list, standard
   input closed, `RIPGREP_CONFIG_PATH` removed): its matches as they come (`parseRipgrepLine()`:
   the submatches' byte offsets of the UTF-8 line turned into UTF-16 ones; paths and lines given
   as base64 bytes decoded);
2. **git grep** in a work tree — `git -C <folder> grep -n -I --untracked --no-color -z --column
   (-F|-E) [-i] [-w] -e <pattern>` through `gitcli::Client` (its environment rules; ignored and
   binary files not searched); the matches marked again with the same pattern (`matchRanges()`);
   git's `-E` is POSIX's extended syntax (no `\d`), its `-i` folds ASCII only in a C locale;
3. **Brulion's own** — the folder's index (`FileIndexCache`), each file up to 4 MiB and not binary
   read and searched on a pool thread with QRegularExpression (Unicode word boundaries).

At most **2 000 matches** (200 a file; the status says "The first 2,000 results"), 30 s and 32 MiB
of output; start() again, Stop or clear() stop what runs. A pattern is literal unless the regular
expression toggle is on; an invalid one says why and runs nothing. Results: a row per file (its
icon, name, folder, number of matches) and a row per matching line (its number, the line without
its leading blanks, the matches bold in the link colour, lines past 400 characters cut around the
first match with "…"). ↓ from the field and ↑/↓ in the list move between matches; Enter or a click
opens the file in the viewer at the line (a file's row: at its first match).

## @-mentions

In a thread (ChatView with `filesFolder`, CodeView gives it the thread's folder), "@" at the start
or after a space or "(" and the characters of a path (letters, digits, marks, `-_./+~`) offer the
folder's **files and folders** by fuzzy match (`CodeFileIndex`, 8 of them, folders with a "/") in
place of the notebook's pages — pages are not offered in threads. Tab, Enter or a click writes
`@src/app/main.cpp ` (a path with a space quoted: `@"docs/a b.md" `); the rest of a word the caret
was in is replaced too. In the message as sent, each **existing file** of the thread's folder it
names with "@" (at most 10; not folders, not paths outside the folder or not there) also goes to
the agent as an ACP `resource_link` block (`file://` URI, its name) after the text
(`WorkspaceAgents`, `mentionedPaths()`); the text keeps the "@path" itself, which agents read as a
path anyway.

## Links in answers

An agent's answer that links to a file of the thread's folder — `[main.cpp](src/main.cpp:42)`,
`[x](/home/ala/projekt/src/a.cpp#L12-L20)`, `file:///…` —, and a tool call's files (its ACP
`locations` and diff paths, listed under the call while the turn runs: `ChatActivity.filesFolder`),
open the file viewer at the line (`CodeFiles.linkTarget()`: `path`, `path:12`, `path:12:5`,
`path:12-20`, `path#L12`, `path#L12-L20`, `path#L12C5`, percent-encoded or not). A link to a file
outside the folder, one that is not there, a page of the notebook or a web address goes where it
went before. (InlineRenderer shows such links in the colour of a page not written yet: they are no
pages of the notebook.)

## Security

* Programs run with QProcess argument lists, never a shell; a search pattern is one argument
  (`--regexp=<pattern>`, `-e <pattern>`), never an option; rg always gets a path (it would read
  its standard input otherwise).
* git runs through `gitcli::Client` with its environment rules (docs/CODE-GIT.md "Security"):
  read-only, no prompts, the fsmonitor hook off.
* Paths from git, rg, agents and links are checked (`cleanRelativePath()`, `relativeInside()`):
  nothing outside the thread's folder is listed, read or opened; a symbolic link inside it is
  followed by the reads as the agent's own tools would.
* Files' text is shown as escaped StyledText generated here, never as the file's own markup.

## Not done (yet)

* Rendered views of Markdown, HTML, CSV, PDF and ZIP files (here: their text; PDFs as binary files);
  an editor with the viewer's colours (the edit is plain text).
* T3 Code's mentions of threads and models, and `#` for pull requests.
* Review comments as chips in the composer: here they are text.
* The index following the disk on Windows (only the refreshes there: a watched folder cannot be
  deleted).
