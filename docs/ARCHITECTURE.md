# Brulion — architecture (C++20 / Qt 6)

Brulion is an offline-first Markdown notes app: Obsidian-style plain files, a Logseq-style
block outliner with daily journals, calendar, reminders and alarms, calculations and tables,
live queries, sync through Syncthing (plain folder) or Nextcloud (WebDAV), import/export from
other apps.

One C++/QML code base, four targets, all built against **Qt 6.12.0** (CMake accepts ≥ 6.12 for the app — its QML uses `Item.mutabilityGroup` —, ≥ 6.8 for brulion-cli alone):

| Target | How | Vault storage | Reminders |
|---|---|---|---|
| **NixOS / Linux desktop** | Qt from nixpkgs: flake package + NixOS module; AppImage (Qt from aqt) for other distributions | a folder (e.g. a Syncthing folder) | `org.freedesktop.Notifications` over D-Bus, tray as fallback; timed in-app (`ReminderScheduler`), the app can live hidden in the tray (`--hidden`, autostart); the tray calendar from the icon's click (a setting) or menu, under Wayland a layer-shell surface (LayerShellQt, `src/platform/traywindow.h`) |
| **Windows** 10/11 x86_64 (test version, docs/WINDOWS.md) | MSVC 2022 + Qt from aqt, `windeployqt`; portable ZIP and a per-user Inno Setup installer from GitHub Actions | a folder (default `Documents\Brulion`) | toast notifications through C++/WinRT (`WindowsToastBus`: Snooze/Done/OK, alarms on screen with a looping sound; the tray icon's messages when toasts are off), timed in-app (`ReminderScheduler`) while the app runs, also hidden in the tray; optional start at sign-in (the Run key, `Autostart`); a click on the tray icon opens the tray calendar (a setting; the system clock's flyout cannot be replaced) |
| **Android** (9+, API 28–36) | Qt for Android (aqt) + `androiddeployqt`, signed APK from GitHub Actions | a folder on shared storage (Syncthing-Fork; "All files access") or app storage + Nextcloud | native `AlarmManager` alarms (Kotlin via JNI) — fire while the app is closed and after a reboot; while it is closed a periodic system job reads the notebook (after one Nextcloud sync) in a process of its own and arms what it finds (`src/app/background`, docs/ANDROID.md "Checking notes in the background"); "Done" taps queued natively (`DoneQueue.kt`) and written by C++ at once while the app is in front, else when it returns to the front or starts (Qt's event loop is paused in the background); home-screen widgets drawn by Kotlin from a snapshot the app publishes (their quick add writes today's journal itself, see below) |
| **Browser** | Qt for WebAssembly (single-threaded) + emsdk, offline PWA shell (`wasm/`) for any web server, and the same in one HTML file opened from disk | Emscripten IDBFS (IndexedDB) + import/export, Nextcloud sync | web Notification API (service worker where needed) while the page is open |

On the desktop (Linux, Windows) a command line for scripts and AI agents comes with it:
`brulion-cli` (also `brulion cli …`), with an MCP server (`brulion-cli mcp`; over HTTPS with sign-in for
claude.ai and ChatGPT: `brulion-cli mcp serve`) — see "Command line" below and docs/CLI.md.

## Layers

```
┌──── src/app  (executable "brulion", QML module "Brulion") ─────────────────────────────────┐
│ QML: qml/theme, qml/components (B* design system), qml/shell, qml/views, qml/editor,       │
│      qml/content, qml/planning, qml/io, qml/drawing, qml/canvas, qml/dev (gallery, …)      │
│ C++ area libraries: brulion_ui_helpers · brulion_ui_touch · brulion_ui_scroll · …_files ·  │
│      brulion_editor (+ brulion_editorhost) · brulion_shell · brulion_notify ·              │
│      brulion_content · brulion_planning · brulion_ioviews · brulion_diag · brulion_drawing │
│      · brulion_canvasview                                                                  │
├──── src/cli  (executable "brulion-cli", library brulion_cli; desktop) ─────────────────────┤
│ the command registry → argv parser, help, JSON Schema, MCP tools; `brulion cli …` from the │
│ app's binary; built on core/notebook only (never on the app's shell)                       │
├──── src/ipc  (library brulion_ipc; desktop) ───────────────────────────────────────────────┤
│ SingleInstance (one app per session) and the messages of its socket (app open …); the      │
│ remote control's protocol, socket (ControlServer, owner-only) and client                  │
├──── src/platform  (library brulion_platform) ──────────────────────────────────────────────┤
│ Android: AndroidBridge (JNI ↔ android/src/…/BrulionAndroid.kt, ImeInsets.kt)               │
│ WebAssembly: WasmStorage (IDBFS), WasmNotifier, WasmShare (shares from another tab),       │
│      wasmshell (loading screen, visualViewport), self-tests                                │
├──── src/core  (brulion_core_<module> libraries; QtCore + QtNetwork, io also QtGui) ────────┤
│ log · util · text · dates · markdown · calc · vault · index · sync · reminders · notebook  │
│ · io · mstodo · webpicture · drawing · canvas · appupdate · backup · stats · github        │
│ · dataview · views (database views, Obsidian Bases) · chart · diagram · periodic · qr      │
│ · spell · ai · chat · writing (the editor's AI assistant) · semantic (prototype)           │
│ · syntax · caldav (CalDAV, ICS) · clipper (the Web Clipper's host) · media · speech        │
│ · cloudimport (OneNote, Todoist and Airtable downloads for the import)                     │
│ · attachments (the text of PDFs and pictures for the search) · vim (Vim keys' engine)      │
│ · publish (a site on GitHub Pages, WebDAV, Nextcloud with a public link, a folder)         │
│ · extensions (Luau: 3rdparty/luau; built in: gym, habits, flashcards, the AI assistant)    │
│ · automation (brulion:// addresses: read, checked, carried out)                            │
│ · crypto (BearSSL's AES-GCM, SHA-2, HMAC; PBKDF2) · encryption (Meld Encrypt)              │
│ · applock (the app lock's state machine, the PIN's hash)                                   │
└────────────────────────────────────────────────────────────────────────────────────────────┘
```

### Principles

1. **Files are the truth.** Notes are `.md` files in a folder; indexes are rebuilt from them.
   Device-local state (sync bookkeeping + merge bases, version history, settings, caches) lives
   in `QStandardPaths::AppDataLocation`, never inside the vault (so Syncthing never syncs it).
2. **Offline first.** Everything works without network. Sync is background and opportunistic,
   never blocks editing and never loses data (3-way merge, else keep both versions + merge UI).
3. **Compatible with Obsidian (+Tasks plugin) and readable by Logseq** — see docs/SYNTAX.md.
4. **Core is GUI-free and fully unit-tested** (QtTest). It compiles unchanged for Linux,
   Android and WebAssembly. No `#ifdef` platform code in `src/core`.
5. **English first, Polish too.** Source strings in English with `tr()`/`qsTr()`; English is
   the default UI language (also in the browser and on a Polish system); the Polish translation
   (`i18n/brulion_pl.ts`) is chosen in the intro's first step or the settings.
6. **Round-trip fidelity.** Editing one block never rewrites other bytes of the file
   (line endings, indentation, BOM, unknown syntax preserved).
7. **Deterministic.** Core code never reads the clock itself (callers pass `now`/`today`);
   the app takes `--now` for reproducible screenshots and tests.
8. **Threads are injected.** Core is single-threaded by contract (a `Vault`, a `VaultIndex` are
   used from their owner's thread), except pure functions that run on any thread: `parseNote`,
   `VaultIndex::prepare`, `SearchIndex::prepare`, `Vault::readDetached`, the index cache's
   decoder and its encoder of a snapshot. `notebook::IndexLoader` / `IndexCacheKeeper` run them on
   a `notebook::WorkerPool` the caller passes (the app's `indexworkers.h`, brulion-cli's own pool),
   and the loader gives the pool to the index for the full-text upkeep after edits
   (`VaultIndex::setSearchWorker()`: a changed note is tokenised there, its document applied on the
   owner's thread only while the note still has that text); without one (WebAssembly,
   `BRULION_INDEX_THREADS=0`) they work in slices on the calling thread. The one exception to
   "synchronous on the owner's thread" is an editor's save, `Vault::writeTextIfUnchangedAsync()`,
   whose work runs on a writer the app injects (`Vault::setWriteExecutor()`; "Vault threading"
   below). Core never starts a thread itself and checks no `QT_CONFIG(thread)`. A static cache added to the parse path must
   be thread-safe (`tst_notebook_indexloader` compares multi-threaded and serial loads).

## Core modules (`src/core/<module>/`, namespace `brulion`)

Each module is a static library `brulion_core_<module>` (`brulion_add_core_module(… DEPS …)`
in its own `CMakeLists.txt`, picked up automatically). Its public header — named after the
module (`dates/dates.h`, `markdown/markdown.h`, `index/index.h`, …) — starts with an API
overview; `*_p.h` headers are internal. Tests: `tests/core/<module>/tst_<module>*.cpp`, plus
cross-module scenarios in `tests/core/integration`. Shared data model: `src/core/types.h` —
compact where the index keeps hundreds of thousands of them (docs/PERFORMANCE.md, L8):
`Block::task` is a `Boxed<Task>` (on the heap, shared by copies, read like `std::optional`),
`Block::raw` a `BlockSource` (a slice of the note's text when its own lines are one piece of it,
else its own copy; read as `QStringView` or `toString()`), `Properties` and `ParsedNote::blockIds`
are `QMap`s (by key). **Changing what `parseNote()` returns** (any field, any input) needs a bump
of `markdown::ParserVersion` (`markdown/parse.h`): the index cache keeps parsed notes on the device;
`tst_markdown_model`'s golden digest of a frozen corpus fails until then. A new field in `Block`,
`Task`, `Reminder`, `Link` or `ParsedNote` also needs the cache's codec (`indexcache.cpp`, a
`static_assert` on the sizes says so), `tests/common/notedump.h` and `indexcache::FormatVersion`.

| Module (deps) | Responsibility (main classes / functions) |
|---|---|
| `log` (—) | the event log (UI "Event log", see "Logging" below): the `brulion.*` logging categories and their UI groups, `Entry` + the line format, `redact()` + the registry of live secrets (`ScopedSecret`), `Logger` (chained Qt message handler, ring buffer, rotating file, rate limits, verbosity, export), `SlowScope` / `formatBytes` / `formatMs` |
| `util` | `paths.h`: vault paths (normalize/NFC, join, dirname, basename, `pageTitle`, `isPicturePath`, `isPdfPath`), ignored paths (under every name a storage opens them by, "Data safety") and temp files, Syncthing / Brulion conflict copy names, `sanitizeFileName`, unique paths, `isAttachmentExtension` (a link target naming a file: link resolution and the path parts of a link); `unicode.h`: `unicode::nfc` — NFC in linear time, used instead of `QString::normalized` everywhere |
| `text` (util) | diacritic folding (`foldDiacritics`, `foldKey`: "ŻÓŁW" → "zolw"), Unicode word boundaries and regex helpers, EOL-preserving `splitLines`/`joinLines`, FNV-1a `contentHash`, `compareNames` (Polish alphabetical order with numbers by value, no ICU: the index, All pages and the sidebar's tree sort alike on every platform); `find.h`: `findMatches` (literal text, case, diacritics and whole word optional, ranges of the original text) and `replaceMatches` (docs/UI.md "Find and replace"); `decode.h`: `decodeText` — files of unknown origin (imports, shares) as text: UTF-8 with or without BOM, UTF-16 with BOM, else windows-1250 when Polish letters dominate, otherwise windows-1252 |
| `dates` (text) | local dates (`DateSpec`, never through UTC); `parseNaturalDate` (Polish + English: "jutro o 9", "w piątek 18:00", "za 2 godziny", "next friday 6pm"); `RecurrenceRule` parse/format/`nextOccurrence`/`occurrencesBetween`, Logseq repeaters; query date expressions (`resolveDateExpr`: `today`, `+7d`, `this-week`, `today..+7d`, `overdue`); human formatting ("today 09:00", "Fri, Oct 16"; in Polish "dziś 09:00", "pt 16 paź") |
| `periodic` (util, text, dates) | weekly and monthly notes (docs/SYNTAX.md §1 "Weekly and monthly notes"), without the vault: ISO weeks (`isoWeek`, `isoWeeksInYear`, `isoWeekStart`; the week-year at the year's ends), period starts / ends / shifts, the file name formats both ways (`nameFor` / `dateFromName`: its own weekly format `yyyy-'W'ww`, Qt formats of a month's 1st, Polish and English month names), paths in the period's folder (`pathFor`, `periodicNoteFromPath`), labels ("Week 45 (Nov 2–8, 2026)", in Polish "Tydzień 45 (2–8 listopada 2026)") and typed phrases (`parsePeriodPhrase`: "ten tydzień", "2026-W45", "listopad 2026", any natural date); creating a note from its template is `notebook/periodicnotes.h` |
| `markdown` (util, text, dates) | `parseNote` → `ParsedNote` (blocks/outline, links, tags, tasks, reminders, properties, YAML-subset frontmatter, headings, block ids, journal date); metadata tokens; pure edits (`setTaskStatus` incl. the next occurrence of recurring tasks, `removeTaskMarker` for "no status", `renameLinks`, `renameTags` (the tags naming a renamed page), `retargetLinks` / `retargetTags` (links by their range, tags by key: what a move writes), `ensureBlockId`, frontmatter values); typed page properties (`pageprops.h`: `readPageProperties` — the YAML frontmatter and the leading `key::` lines as text / number / checkbox / date / date and time / list / tags / aliases —, verified single-property edits `setPageProperty`, `removePageProperty`, `renamePageProperty`, `addPageListItem` / `removePageListItem` changing only that property's lines, list style and comments kept); a page's icon and cover (`pagelook.h`: `icon` / `banner_icon` — an emoji or Iconize's `LiBookOpen` —, `banner` / `banner_y`, read by `pageLook`, the values Brulion writes); coloured highlights (`highlight.h`: Highlightr's `<mark style="background: …;">` / `<mark class="hltr-…">` read into a fixed palette — never the note's CSS —, `findMarkPairs`, `findHighlights`, `setHighlight`: the editor's "Highlight"); single-token edits of the token menus (`tokenedit.h`: dates, reminders, 🔁 rules, priorities and links to days at a chip's range — `applyTokenAction` —, `TokenRef` naming a token across copies of a block and `editBlockToken` writing it into a file's text, only the token's bytes); tag occurrences with their ranges (`findNoteTags`), the tag naming a page (`tagSyntax`); journals and templates; note modes (outline / document: `resolveNoteMode` from the `brulion:` frontmatter key, journal / page defaults and a content heuristic; `setNoteModeKey`); links to headings and blocks (`linkformat.h`: `headingFragment`, the one heading matcher `findHeading` of links, embeds and copying — Obsidian heading paths included —, `uniqueHeadingFragment`, `formatReference` of the copied `((id))`, `[[Page#^id]]`, `[[Page#Heading]]`, embeds; `pdfFragmentPage` — Obsidian's `#page=3` of a PDF link or embed); **`BlockDocument`** (editable block list with lossless round trip: split/merge/indent/outdent/move subtree/toggle task/list item to paragraph; ranges of a block selection: `removeRange`, `indentRange` — all or nothing —, `outdentRange`, `moveRangeUp/Down`; block uids unique in the process; undo: `version()` and `diffFrom()` → a `Delta` — the changed run of blocks, a block in both versions with only its changed range of text —, `revert` / `reapply` as operations with row changes, `composed` for typing, `adoptUids` for a re-read text; `BlockDocument::Locator`: one block of a note as the document holds it, found by line from the note's `ParsedNote` and parsed alone — the linked references render it without the whole note); what a block selection copies (`blockclipboard.h`: `topRows`, `blocksMarkdown` — exact blocks re-based to column 0 by visual columns, LF, no BOM or frontmatter —, `blocksPlainText`, `stripInlineMarkup` shared with the editor's one-line plain text — paired marks only, code spans verbatim —, `withoutBlockIds` for pastes that must not duplicate `((id))` targets); `markdownDestination` (where a Markdown link's or picture's destination is written, by the parser's own rules: what retargeting a picture from the internet replaces); **`InlineRenderer`** → escaped Qt rich text with `page:`/`tag:`/`block:`/`ext:`/`task:`/`reminder:` links (and, for the token menus, `date:`/`recur:`/`prio:`/`day:` ranges) and date/reminder/recurrence chips (Polish one-letter words joined to the next by a no-break space); the path parts of `[[Folder/Page]]` (`linkPathParts`, `LinkPartTarget`, `part:<k>:<target>` links with `RenderContext::resolveLinkPart`: SYNTAX §2 "Path parts"), `withHoveredLink` (the hover underline of one href), `withFindHighlight` (the find bar's matches tinted in rendered text) and `linkUrlAt` (the link at a caret: Alt+Enter) |
| `syntax` (—) | syntax colours of code in notes: `lexLuauLine` (`luau.h`: one line of Luau as coloured spans — keywords, constants, the library and `b` with its fields, function names, numbers, strings with their interpolations, comments — and the state a line leaves open: a long string or comment, a backtick string continued), what the editor's `MarkdownHighlighter` paints in ```` ```luau ```` blocks and the source mode |
| `stats` (markdown) | word and character counts and the reading time of a note or a selection (docs/UI.md "Statistics and focus mode"): `readableText` — what a reader sees of the Markdown (frontmatter, properties, block ids, tokens, comments, embeds, code and formulas left out, links as their labels) —, `countWords` (runs between whitespace with a letter or digit; no-break spaces group digits only), `countPlainText` (grapheme clusters, without line breaks / without spaces), `countMarkdown`, `readingMinutes` (200 words a minute) |
| `qr` (markdown; 3rdparty/qrcodegen) | QR codes (docs/UI.md "QR code"): a note or blocks as codes another phone's camera reads as text. `encode` / `encodeBytes` (`qrcode.h`: byte mode with the text's UTF-8, no ECI header; the smallest version at error correction L, then as much correction as it allows; a lone surrogate as U+FFFD; more than `MaxBytes` = 2953 bytes refused before Nayuki's QR Code generator — vendored in `3rdparty/qrcodegen`, MIT — would throw), `noteText` (`qrtext.h`: Markdown as written — LF, no BOM, no blank lines around — or `markdown::blocksPlainText`, the title first: in Markdown a `# ` heading after the frontmatter and page properties, none for a note with a level-1 heading of its own, `hasOwnTitle`; "" for a note with no block besides those), `utf8Size`, `splitText` (as few codes as hold a text, about the same size each, cut after a line break near the share, else a space, never inside a grapheme cluster unless it alone is too large; joined they are the text), `MaxCodes` = 20 |
| `calc` (text, dates) | own engine (lexer + Pratt parser + evaluator, bounded work, no `eval`): Polish decimal comma, variables (Unicode names), `ans`, percent sugar, units and conversions, currencies as tags, functions, `sum`/`suma` lines; calc blocks, inline `= expr`, page scope (calc variables + numeric properties); GFM tables: parse/serialize/replace, rows and columns inserted, removed and moved with the formulas' references following (`insertTableRow`… in `table.h`; references found by the formula parser, a deleted one written `REF!`) and **table formulas** (A1, ranges, `[Kolumna]`, `above`/`left`…, dependency order, `#CYCLE`); sorting and filtering tables (`tablesort.h`: `sortTableText` — numbers, dates, Polish order via `text::compareNames`, stable, empty cells last, summary rows in place, each row's line kept byte for byte unless A1 references follow it, `permuteTableRows`; `tableValueMatches` / `tableRowsShown`: the view's filters) |
| `vault` (util, text, log, periodic) | `Vault` (folder scan, cached listing, atomic `QSaveFile` writes, `QFileSystemWatcher` on the folders and the files open in editors + rescans (when the app returns to the foreground, every 2 minutes) for external changes, echo suppression by content hash — the echo of an own save is recognised by its folder's nanosecond modification time without listing the folder — only in folders whose clock a probe at their first own save found fine (`vaultutil::probeFolderClock`; NTFS, Linux before 6.13 and Android stamp with a clock tick, so there every echo is listed) —, and diffs walk only the listed folders' known children —, `files()` / `markdownFiles()` / `folders()` kept sorted and updated in place by saves, a save that reads and hashes the note once, an editor's save on a writer thread (`writeTextIfUnchangedAsync`: read-compare, History, the atomic write, stat and hash there, merged on the owner's thread; every other operation waits for it — "Vault threading"), atomic writes that sync their own temporary file and stamp the folder around each step (`vaultutil::saveAtomicallyStamped`, with the platform's file calls: `vault/filesync.h`, set by `platform::installFileSync()` — fdatasync/fsync, Windows' FlushFileBuffers and MoveFileExW with long paths, a rename refused by a virus scanner tried again for 0.3 s (then the editor's own retries); without them, as in brulion-cli, QSaveFile's writes and the echo lists the folder) and, for a conditional write, check the file once more right before the rename (`ReadMark`, `stillAsRead`: a compare-and-swap; the bytes read again where the stamps cannot vouch), files whose coarse stamp cannot vouch for their content compared by content by the next listings (`noteUnsettled`), the leftovers of interrupted writes removed at `open()` (`removeLeftoverTemp`), trash `.trash/`, rename/move, `.brulion/config.json` — without it, the settings of an Obsidian vault (`obsidiansettings.h`: `.obsidian/daily-notes.json`, `app.json`, `templates.json`, moment.js → Qt date formats, guards for folders Brulion already used) —, storage checks — see "Data safety"); `readDetached` (static, any thread) + `adoptRead` = `readText()` split in two for the index loader's workers, with exactly its bookkeeping); `History` (device-local version snapshots, throttled — a throttled one decided from the newest snapshot remembered while its index file keeps its modification time, without reading the index — and pruned); `LocalStore` (small JSON key-value store per vault in AppData) |
| `index` (util, text, dates, markdown, canvas) | `VaultIndex` (incremental, coalesced `changed()`: notes — and canvases as notes of their cards' text, `canvas::indexText`: their links count, their words are searched, the file's JSON kept beside for `planRelocation`, which rewrites canvases through `canvas::rewriteCanvas`; `readsText` —, link resolution per SYNTAX §2, backlinks incl. `((id))` refs, unlinked references, block ids, tags — also as links to the pages they name, SYNTAX §3: `resolveTag` (notes only), `tagsNaming`, `tagReferences`, `tagsTo`, resolved once per tag and index version —, tasks, reminders, journals, attachments, page/block suggestions, page-property keys and values in use (`propertyKeys` with each key's most common type, `propertyValues`: the properties panel's suggestions), graph data; the path parts of a link: `resolveLinkPart` (page beside the folder, page by name, folder with notes, else new — SYNTAX §2 "Path parts") and `noteFolder` (folders holding notes, kept with each note); moves: `linkTarget` / `tagTarget` (the shortest name reaching a file) and `planRelocation` — the notes to rewrite so every link and tag keeps its target, and the ```query filters naming a moved file or a renamed tag (`findQueryNames` / `retargetQueryNames`, `notesWithQueries`), resolved with the lookup tables of before and of after the move — an overlay of the moved names on
those of before —, `relocate.cpp`; `RelocationPlanner` makes the same plan a few notes at a time); `SearchIndex` (BM25+, prefix + fuzzy, diacritic-insensitive, excerpts; documents tokenised anywhere with `prepare()`, move-only, documents point into the dictionary); the query language (`parseQuery`/`runQuery`, SYNTAX §10); following the index across changes (docs/PERFORMANCE.md, L3): every change is logged (`changesSince(version)` → `IndexChanges`: the changed notes — also renamed and removed ones, notes whose links now resolve elsewhere, notes whose `((id))` show a changed block —, whether names changed, incomplete after `clear()`, a re-parse or a log overflow; `serial()` tells indexes apart), the derived lists (sorted entries, `paths`, `tasks`, `reminders`, `journals`, `notesWithQueries`) are patched with the changed note instead of built again (tags, missing pages, tag targets and property lists dropped only when the change touches them; lists nobody read for many changes — a bulk load — are dropped and built once at the next read), `notecache.h` (`IndexFollower`, `NoteCache<Value>`: per-note values computed again only for the logged notes; a full sync walks `forEachNote()` or fills the values from the index's own task / reminder lists) and `QueryCache` (`querycache.h`: a query's matches kept in result order, the changed notes evaluated and merged in; `runQuery` is the same code without history; `queryResolvesNames()` — which queries follow the index fully after a name change — for anything following the index beside it); loading on other threads: `VaultIndex::prepare` / `prepareParsed` → `PreparedNote` with its `NoteKeys` (`vaultindex_p.h`: the keys the note is filed under — NFC, lower case, its folders, the link keys naming it, its tags' block counts — and a complete `ResolvePlan` of each link, `resolve_p.h`: the paths, name and alias keys a target is looked up by; `NameTable::resolve(target, from)` is the same plan with its keys derived as the lookups need them; a link that resolves to nothing keeps its plan, `Entry::unresolvedPlans`, for the relinks), `updatePrepared` (only looks up and inserts; re-parsed when prepared with another `parseFingerprint`), `reserveSequence` (recency by path order whatever the merge order), `adoptSearch` (a full-text index built elsewhere, documents kept only when their `contentKey` — FNV-1a over the note's UTF-16 — is the note's); the index cache `indexcache.h` (`Identity` — format, `markdown::ParserVersion`, `SearchIndex::FormatVersion`, Qt and app versions, parse fingerprint, vault id —, `Reader` (validated, bounds-checked, per-record checksums; `decodeNote`, `SearchDecoder` in steps), `Snapshot` / `SnapshotTaker` (what a file holds, copied from the index in steps of a few milliseconds — implicitly shared notes and posting lists, so reference counts, not data —, given up when the index or its full-text index changes in between; immutable afterwards), `Writer` / `encode()` (a snapshot encoded in slices or at once, on any thread); own binary format documented in the header). Internal lookup maps use `smallsets_p.h` (`PathSet` / `PathCounts`: a sorted list up to 32 paths, a hash above) |
| `dataview` (util, text, dates, markdown, index) | Obsidian Dataview queries (docs/SYNTAX.md §10 "Dataview queries"), read-only, Brulion's own code — nothing from a note runs as a script: `parseQuery` (a lexer and a recursive-descent parser of the Dataview Query Language — LIST / TABLE / TASK, FROM sources, WHERE, SORT, GROUP BY, FLATTEN, LIMIT, expressions with lambdas — into `ast_p.h`; a translated message with its line, context `brulion::Dataview`), `Value` (`value.h`: null, boolean, number, string, date, duration, link, list, object, function; Dataview's truthiness, ordering and equality; field values typed from `key:: value` text and frontmatter), `runQuery` (`run.cpp`: the pages of FROM from the index, the rows, the commands one after another, the result as Markdown cells: `toMarkdown`) over objects that compute their fields when first read (`objects.cpp`: a page's frontmatter and inline fields, `file.*`, list items and tasks), the evaluator and Dataview's built-in functions (`eval.cpp`, `functions.cpp`; no regular expressions, no `csv()`), bounded by a step budget and a row limit (`Options::maxSteps`, `MaxRows`); standalone expressions (`expression.h`: `parseExpression` in Dataview's syntax — the formulas of database views — or Obsidian Bases' — `bases.cpp` reads its JavaScript-like syntax into the same tree, methods and Bases' operators as "b:" built-ins —, an `Evaluator` row by row over pages, blocks and any file). QtCore only |
| `vim` (—) | Vim keys (docs/UI.md "Vim mode"), GUI-free: `Engine` — keys → modes, counts, motions, operators, text objects, registers, marks, the dot repeat, the command line (`:w` `:q` `:s` of plain text) — over an abstract `Buffer` of lines and columns that the editors implement (`VimPageBuffer`: a page's blocks; `VimSourceBuffer`: source mode's text); `Shared` (the app's registers, last search and change), `TextBuffer` (in memory, the tests'), `keysFromNotation` ("ciw<Esc>") |
| `attachments` (util, text, index) | the text of PDFs and pictures in the whole-notebook search (docs/UI.md "Searching attachments"), pure — the app reads the files (`src/app/attachments`): `Record` (`record.h`: what one file says — its kind, pages, each page's text and where it came from: text layer, OCR with its languages and words' boxes in fractions of the page, waiting for OCR —; `encodeRecord` / `decodeRecord`, one page read alone `readRecordPage` / `decodeRecordPage` — a header with a page table, every page checksummed, bounds-checked against hostile files —; `ocrCharBoxes` — a box per UTF-16 unit for the highlights —, `joinOcrWords`), `Manifest` (`manifest.h`: a notebook's attachments by path — size, mtime, SHA-256 naming their record —, `hashFile` in chunks, `recordFileName`), `AttachmentSearch` (`attachmentsearch.h`: a `SearchIndex` of one document per page with text and one per file name; `prepare` on any thread, `set` / `setPage` / `remove` / `rename` without re-reading; `search` → one `Hit` per file at its best page with the pages that match, `literal` for merging with the notes' hits, an excerpt and `find` — what the PDF viewer searches for to show the hit — from a page-text callback), `parseSearchFilters` (`searchfilters.h`: the search field's `in:` / `type:` / `w:` / `typ:` filters, English and Polish values, diacritics optional). QtCore only |
| `spell` (text, markdown) | spell checking, GUI-free (docs/UI.md "Spell checking"): `findWords` / `wordAt` (`spellwords.h`: the words of a line of Markdown worth checking — code, comments, formulas, links (a Markdown link's text is checked), URLs, e-mail addresses, tags, task and date tokens, block ids, property names, task keywords, HTML skipped by the markdown module's own scanners; words with digits, single letters, acronyms, mixed case, file names and paths left out; `checkedText` / `normalizedWord`: NFC, ASCII apostrophes), `SpellChecker` (`spellchecker.h`: one `SpellBackend` per language — a word is right when one knows it —, the personal list and ignored words with Hunspell's letter-case rules, hyphenated words by their parts, "np." with its dot, batched `checkAll`, merged suggestions; `WordListBackend` for tests and the playground), `resolveLanguages` (`languages.h`: "auto" → the UI language, the system's, English), `encodeDictionaryText` / `decodeDictionaryText` (`dictencoding.h`: UTF-8, ISO8859-1/-2/-15 tables — Qt has no Latin-2 without ICU). The backends live in the platform layer (`spellbackends.h`) |
| `appupdate` (QtNetwork) | is a newer Brulion released: `normalizedVersion` (only plain `X.Y.Z`, with or without `v`), `isNewerVersion`, `versionFromRelease` (GitHub's answer; drafts and pre-releases name no version), `releasePageUrl` (built from the version, never taken from the answer), `UpdateChecker` (one request: no cookies, no language, the agent `Brulion` without a version, at most 2 MB, a 30 s stall fails). The message: `shell/appupdates.h` |
| `webpicture` (util; QtNetwork) | pictures from the internet in notes (docs/SYNTAX.md §2): `isWebUrl` / `webUrl` (http(s) with a host), `sniffFormat` (the picture format by its signature — an HTML error page is none), `attachmentName` / `displayName`; `isPrivateHost` (loopback, private-use, shared 100.64/10, link-local, site-local, unspecified IPs; single-label names and home-network suffixes such as `.local`, `.lan`, `.home.arpa`, `.internal`; other names not resolved), `redirectAllowed`; `PictureFetcher` (GET only, every redirect checked by hand — never https → http, never from a host of the internet to a private one —, no cookies, a generic User-Agent and `Accept-Language: *` — `setSimpleRequests` in the browser: neither, CORS "simple" requests —, size limit also on the announced length, stall timeout, one request per URL, 4 at once, `cancel(url)` and `cancel(urls)` — the queue cleared of them all before a running one is aborted, a refused redirect failing as `Redirect`); `PictureCache` (the device's cache folder, `<sha1 of the URL>.<format>`, pruned least recently used first — `lookup()` marks a file used by its modification time, at most once an hour) |
| `clipper` (util, text, dates, markdown, io, webpicture; QtNetwork) | the Web Clipper's side in Brulion (docs/CLIPPER.md; the extension is `clipper/` at the repository's top): native messaging frames (`nativemessaging.h`: `FrameDecoder` — a 32-bit length in the machine's order, then JSON; 64 MiB in, a frame over it ends the stream without allocating it, 1 MiB out), the clip request read as hostile (`request.h`: kinds, places — a new page, the journal, a heading's section —, sizes bounded, folders and pages with `..`, hidden, absolute or drive parts refused, links only http(s), `file:`, `mailto:`), the clip's templates (`cliptemplate.h`: the built-ins brulion, obsidian, joplin, link; `fillClipTemplate()` over `markdown::applyTemplate()`'s variables, values as data — YAML-safe in a frontmatter, one line in `key::` lines, escaped Markdown in the text; lines of empty variables left out; `frontmatterAsBlockProperties()`), a page's HTML as Markdown (`article.h`: `io::htmlToMarkdown()` with the page as the base of relative addresses, links only to http(s), mailto: and the page, players as links, pictures as tokens), the pictures' download (`pictures.h`: `PictureBatch` over `webpicture::PictureFetcher` — count, size, total and time limits, private hosts only for a page there, data: pictures decoded) and the host manifests (`nativehost.h`: the host's name, the extension ids, the manifest per browser family, its place per browser, system and scope — files, and registry keys on Windows —, `isNativeMessagingLaunch()`). Pure but for the download; the command line writes the clip (`src/cli/commands_clip.cpp`) |
| `drawing` (—; zlib through the vendored miniz) | drawings in notes (docs/SYNTAX.md §2 "Drawings"): the model (`Drawing`, `Stroke` = `Element`: one Excalidraw element Brulion draws — `ElementType` freedraw, rectangle, ellipse, diamond, line, arrow, text, image; points with pressure or a box, angle, colours, fill and stroke styles, roundness, groups, bindings, labels, text, picture —, `Background`, `PaperTheme` — as the app, light, dark: `appState.brulionTheme` —; `bounds`, `editable` — no frames, embedded pages or unknown elements —, `normalizeStroke`), the outline of a freehand line (`stroke.h`: a port of perfect-freehand 1.2, MIT, with Excalidraw's freehand options; `outlineToSvgPath`), the look and hit tests of elements (`geometry.h`: `elementShape` — paths for fills, hatching (bounded: `MaxHatchLines` a shape, `DrawingHatchLines` a drawing), outlines, arrowheads, rotated —, `elementBounds`, `textLines`, `hitTest`, `touchesSegment` — the eraser —, `lassoSelects`), the edits (`edit.h`: `selectionClosure`, `moveElements` — labels and bound arrows follow —, `duplicateElements`, `deleteElements`, `recolorElements`, `reorderElements`, `newShape`, `newText`, `setText` — an unchanged text no edit —, `addLabel`, `holdsLabel` / `labelMaxWidth` / `labelCenter` — labels in shapes and polygons —, `alignElements` / `distributeElements` — a selection lined up or spaced evenly, part by part —, `wrapText`, `isLocked` — a locked shape's label too —; `diffElements` / `applyDiff` — the editor's undo steps; `newElementId` — nanoid as Excalidraw — and `randomEditStamp`, the stamp of the editor and of new drawings made elsewhere), the merge of two versions (`merge.h`: `merge` against a `MergeBase` — new here on top, changed here wins, deleted here goes; unknown base: a union —, `mergeKey`; shared by the editor, the sync center and Nextcloud's sync; `mergeCounts`, `sameDrawing`, `containsDrawing` — how two versions relate, the sync center's cards —; `mergeDrawingFiles` — three versions of a drawing file merged against the base and written in the remote's format, nothing when one cannot be merged: not editable, broken, no base, other formats, an element changed without a new version, both sides' plugin Markdown changed), the Excalidraw format (`excalidraw.h`: `parseScene` — `elementFromJson` for the elements Brulion draws, every other element kept as foreign, deleted ones dropped, unknown fields kept per element —, `sceneJson` — `elementJson`: unchanged elements as read, changed ones from the model, fractional indices kept while they rise; a polygon's label as a text grouped with it, `polygonLabelGroup` —, the SVG payload as excalidraw.com's "Embed scene" writes it: `encodePayload`, `decodePayload` (versions 1 and 2, compressed or not), `svgPayload`, `looksLikeExcalidrawSvg` (its first 4 KB), `deflate` / `inflate` with a size limit), the colours of the light and the dark theme (`palette.h`: `darkRgba` / `darkColor` — Excalidraw's dark-mode mapping, invert 93 % + hue-rotate 180° —, `displayInk` — highlighters keep their colour, opacity × 1.25 —, `displayPaper` — always opaque —, `displayRule`, `rendersDark`, `darkSvg` — another program's SVG with its colours mapped one by one, pictures and emoji untouched —; the WCAG `contrast` the tests check), the SVG Brulion writes (`svgwrite.h`: `toSvg` — integer width and height on the root (`pictureRect`), the paper, lines or grid, every element in the drawing's theme: paths, `<text>` per line, `<image>` of safe data URLs; CSS only for "as the app": one `@media (prefers-color-scheme: dark)` block for browsers, ignored by Qt —, `svgWithScene` — another program's picture of the scene with the scene (a `Drawing` or its JSON as given) embedded right after its root's start tag, a scene it had (as Excalidraw writes one) replaced, every other byte kept (an imported Trilium canvas) —, `safeColor`, `safeDataUrl`, `fontFamilyCss`; `SvgOptions::pictureImages` — the dark raster of a Dark drawing's backdrop, which core cannot make), the backdrop (`backdrop.h`: a picture or a PDF page under the ink — the locked image element at index 0 with `customData.brulionBackdrop {path, page}`, its raster in `files` —: `backdropIndex`, `backdropSource`, `setBackdrop` / `removeBackdrop`, `newDrawingOnBackdrop` — a new drawing on a picture (Jasne) or a PDF page (Jak motyw) —, `backdropSceneSize`, `fileIdOf` — SHA-1 as Excalidraw —, `dataUrl`, `parsePageRanges` / `formatPageRanges` — "1-3, 7", "wszystkie", at most 60), the Obsidian Excalidraw plugin's files (`excalidrawmd.h`: `looksLikeExcalidrawMarkdown` — the frontmatter key `excalidraw-plugin` —, `readExcalidrawMarkdown` — the `## Drawing` block, compressed or json, the `## Text Elements` entries, the deleted elements —, `writeExcalidrawMarkdown` — only the block and changed or removed text entries rewritten, erased elements as tombstones —, `embeddedFiles` / `embeddedFileEntry` — the pictures of `## Embedded Files`, shown, never written into the scene —, `stringifyJson` — JSON.stringify's layout), LZ-String (`lzstring.h`: `compressToBase64` / `decompressFromBase64`, a port of lz-string 1.5, MIT), one API of drawing files (`drawingfile.h`: `readDrawingFile` / `writeDrawingFile` — SVG, `.excalidraw`, `*.excalidraw.md`, each written back in its own format —, `looksLikeDrawingFile`, `previewDrawing` — dashed boxes for foreign elements), elements copied and pasted as Excalidraw's clipboard (`clipboard.h`: `clipboardJson`, `parseClipboard`, `pasteElements`), `readDrawing` (an SVG, a `.excalidraw` file or a plugin file → a drawing, or why not) |
| `canvas` (util, text, markdown) | whiteboards in the JSON Canvas format (docs/SYNTAX.md §2 "Canvases"; Obsidian Canvas), Brulion's own reader — no QJson, which sorts keys and forgets the layout: `json_p.h` (a bounded JSON reader that keeps every value's span, objects' members in order with duplicates, arrays' whitespace; JSON.stringify's strings and numbers), the model and the file (`canvas.h`: `Node` — text, file with `subpath`, link, group with label and background, any other type kept —, `Edge` — sides, ends, colour, label —, each with its `Origin` as read; `readCanvas` — strict JSON, limits `MaxCanvasChars`, `MaxNodes`, `MaxEdges`, the reader's depth and value count; ids made for missing and duplicate ones —, `writeCanvas` — the file's layout kept: unchanged nodes and edges byte for byte, a changed one with its keys in order, unknown keys as written, new ones in the file's style; a new canvas as Obsidian writes it —, `fragmentText` / `parseFragment` — the clipboard —, `isCanvasPath`), the geometry (`geometry.h`: `nodeRect`, `bounds`, sides and anchors, `edgeShape` — a Bézier out of the sides, facing ones when unnamed, arrowheads, the label's point —, `looseShape`, `distanceTo`, `svgPath`, `nodeAt` — groups by frame —, `edgeAt`, `nodesIn`, `groupContents` / `withGroupContents`, `colorHex` — Obsidian's presets, lighter when dark, validated hex —), pure edits (`edit.h`: add, `connectNodes`, `removeItems`, `moveNodes` — whole numbers, an axis not moved kept —, `setNodeRect`, text, file, url, labels, backgrounds, `setColor`, `setEdgeEnds`, `reverseEdges`, `duplicate`, `paste`, `group` / `ungroup`, `align`, `distribute`, `reorder`, `newId`), the merge (`merge.h`: `merge` against a base field by field or as a union, edges of removed nodes dropped, theirs' layout; `mergeCounts`; `mergeCanvasFiles` — Nextcloud's and GitHub's sync), the index text (`indextext.h`: `indexText` — the cards as Markdown, file nodes as links, segments by node —, `indexNote` — parseNote without tasks, reminders, properties, aliases, block ids —, `nodeAtLine`, `rewriteCanvas` — paths and text nodes' text rewritten, every other byte kept), the picture (`svg.h`: `toSvg` — escaped text, presets and validated colours, only picture data URLs —, `svgSize`, `plainLines`). QtCore only |
| `diagram` (—) | Mermaid diagrams in notes (docs/SYNTAX.md §11 "Mermaid diagrams"), Brulion's own renderer — nothing from a note is executed: `detectKind` (flowchart, sequence, pie; the other Mermaid types recognised as unsupported), `render(source, Metrics)` → `Result` (a `Scene` or an `Error` code with its line, translated by the app), bounded by `limits` (source size, nodes, links, subgraphs, nesting, participants, messages, slices, layout nodes); parsers of flowcharts (every node shape, link, label form, chains, groups, nested subgraphs, `classDef`/`class`/`:::`/`style`/`linkStyle` with validated colours), sequence diagrams (participants, actors, every arrow, activations, notes, blocks, boxes, autonumber) and pie charts; a layered layout (`layered_p.h`: DFS cycle breaking, longest-path ranks with sources pulled down, doubled ranks with label nodes, barycentre sweeps bounded and the best order kept, coordinates by weighted isotonic regression), subgraphs laid out from the inside out in their own `direction`; the `Scene` — paths and text in painting order, each colour a `Role` of a `Palette` (`themedPalette` from a few theme colours, Obsidian-like) or an explicit validated colour; text measured by the caller (`Metrics::measure`: QFontMetricsF in the app, `approximateMetrics()` for the export and tests); `svg.h` `toSvg`: safe SVG (escaped text, validated colours, light attributes plus a `prefers-color-scheme: dark` style sheet; no scripts, `foreignObject`, event attributes or references). QtCore only |
| `chart` (text, calc, diagram) | charts in notes (docs/SYNTAX.md §9 "Charts"), Brulion's own code — nothing from a note is executed: `parseSpec` (a ```` ```chart ```` block's `key: value` lines, Polish names, comments, an `Error` with its line, translated in context `ChartMessages`), `pageSources` / `resolveData` (the numbers of a table of the page — formulas evaluated, summary rows left out, columns by header or letter —, a ```` ```calc ```` block or a query's groups through a runner the caller gives: `QueryHost` in the app, the export's index), `layoutChart` (bars, lines, a pie with axes, nice ticks, thinned and elided labels, a legend: a `diagram::Scene` in the palette's `Slice`/`Series`/`Grid` roles, painted by the app's `ChartView` or written by `diagram::toSvg`), `describeChart` (the text alternative); bounded by `limits` (source, points, series, slices, label length, magnitudes; NaN/inf are gaps). QtCore only |
| `sync` (util, text, vault, log, drawing, canvas; QtNetwork) | `HttpTransport` seam + `NetworkHttpTransport` (protocol, transfer progress, a body read while it arrives — `HttpRequest::onData` —, a request stopped by its sender — `HttpAbort`); `WebDavClient` (PROPFIND/GET/PUT/DELETE/MKCOL/MOVE, ETags, Basic auth; every request observed: `exchanged`, in-flight / idle time, one `brulion.net` line per request in the detailed log); `SyncEngine` (bidirectional, per-file state + merge bases in LocalStore, conflict copies, deletions, mass-deletion guard; a local file with the size and modification time of its last sync is not read again — a binary file always, a note once its content was read at least `SyncFileState::SettledAfterMs` after that time (a second write within the storage's timestamp granularity must not hide behind the first), and only to conclude "nothing to do": before a merge, a download or a deletion the note is read after all; folders: empty ones both ways, deletions of emptied ones, the folders synced before kept per folder, a folder or page renamed in the vault moved on the server with one MOVE and its states following it, a folder or page renamed on the server — known by its `oc:fileid` (recorded in the sync state, backfilled from listings), else by the ETag a move keeps and its size — renamed in the vault with `Vault::rename` (`syncfolders.cpp`); the paths without deletions run first, so content that arrived under a new path counts as moved, not deleted; numbered runs with a trigger, phase timings, request statistics, per-file error codes, `activity()`); `merge3()` line diff3 with a union of concurrent appends (also lines added next to lines the other side changed: a journal's last block edited while an agent appends; flashcards both devices reviewed merged by their later review, `cardmerge.h`), `mergeJson3()` for the config; a drawing (an SVG with an Excalidraw scene, `*.excalidraw.md` with the plugin's key, `.excalidraw`) never merged line by line — merged element by element against its base (`drawing::mergeDrawingFiles`), both versions kept when it cannot be; a canvas merged node by node and field by field (`canvas::mergeCanvasFiles`); `SyncScheduler` (interval, debounce after edits, online/foreground triggers, retry back-off; `runFinished` for every run; stall watchdog while syncing: "stalled" after 20 s without progress, stopped and retried after 180 s); `SyncHistory` (the last 20 runs, quiet ones folded, in its own `sync-history.json`); Syncthing: `SyncthingClient` (REST status), conflict copies, `.stignore` template |
| `reminders` (util, text, dates, markdown, index, vault) | `upcomingNotifications()` (stable ids, recurrence expansion, done tasks skipped, `taskLine` — the task a notification's "Done" completes); `NotificationSink` (implemented by the app's notifiers); `ReminderScheduler` (in-app timing for platforms without background alarms, missed-reminder detection; one `NotificationCache` of the days from a week back to the horizon serves its refresh and in-app checks, so a save computes the saved note only); `calendarItems()`; `CalendarCache` / `NotificationCache` (per-note results kept across index changes, `NoteCache`; the free functions are these caches without history; the kept notification instants are computed again when the local time zone changes); `snoozeReminder()` |
| `ai` (util, sync) | AI for extensions (docs/EXTENSIONS.md "AI"): `requestFrom()` (b.ai's table → a request: prompt or messages, system, model, maxTokens, effort; at most 200 000 characters), `httpRequest()` (Anthropic's Messages API — `anthropic-version: 2026-10-21`, `claude-opus-5-5` by default, server-side fallbacks for the models that have them — or an OpenAI-compatible chat API: OpenAI, OpenRouter, Ollama, LM Studio), `readReply()` (text, usage, refusals and HTTP errors with the provider's message), `StreamReader` / `readStream()` (an answer streamed as server-sent events — Anthropic's and an OpenAI-compatible API's — read while it comes into a `Partial`: words and thoughts so far; at its end into the reply `readReply()` gives), `Client` over `sync::HttpTransport` (`send()` with a `PartialCallback` streams; it returns a `Pending` whose `stop()` ends the request); pictures in a request (`images`: loaded by the app through an `ImageLoader`, sent to either API). **The model on the device** (the provider "local", handed by `Client` to a `LocalBackend`): `local.h` — `localModels()` (LFM3-VL 2B and LFM3 1.7B by Liquid AI, and Brulion Assistant 1.7B — LFM3 1.7B's file with Brulion's LoRA adapter: their files on Hugging Face, one revision, SHA-256), `local::prompt()` / `local::reply()` (LFM3's chat format: a request as pieces — control tokens apart from text, pictures —, the answer read back with its thoughts and tool calls, written back token for token), `local::shorten()` (a request cut to fit a small context); `localfiles.h` — `LocalModelFiles` (resumable downloads into the app's models folder, checked by SHA-256); `localrunner.h` — `LocalRunner` (what runs a model; `Params::written`: what it has written, token by token), `answerLocally()` (prompt, cut while too long, answer; `local::partial()` of what is written for a watcher), `loadLocalRunner()` (the plugin `brulion_localai`, loaded on first use); `cpus.h` — `cpuCores()` (the online cores from Linux's sysfs: capacity, highest clock, SMT siblings), `computeCores()` (the cores a model computes on: one logical processor per physical core, a phone's slow cluster and a PC's efficiency cores left out, at most 8; none chosen where the cores do not tell their capacity — an older kernel). The runner itself, `llamarunner.h` over llama.cpp — one thread per fast physical core, kept on those cores in a pool of its own for the model's life, spinning between steps only where the system keeps other work off those cores (`Options::spinWait`: Android); the state after a prompt's system text and tools kept on disk (`Options::stateDir`, the app's cache) for the first request after the model was opened again —, is the library `brulion_ai_llama` (and that plugin), built only with `BRULION_LOCAL_AI` and linked by neither the app nor brulion-cli ("The model on the device" below) |
| `chat` (util) | the AI chat's conversations as pages (docs/SYNTAX.md §14 "Conversations with AI", pure functions): `parse()` (a page's `- 🧑` / `- 🤖` messages with their lines, times — `czas::` / `time::` — and pictures, fenced code and the frontmatter skipped, the page a conversation is about, its `partner:`), `formatMessage()` / `appendMessage()` (a message written into a page with its time, line endings kept, reminders put in a code span so a conversation never rings, an open fence closed), `newConversation()`, `setPartner()`, `picturesIn()` / `withPictures()` / `withoutPictures()`, `pageTitle()` (`AI/<date> <first words>`), `splitTitle()`, `journalEntry()`, `history()` (b.ai's messages: one side's in a row joined, the newest pictures as `images`), `firstSent()` (a conversation too long for the model cut by whole turns), `preview()`. The app's side: `shell/workspacechats.h` (a turn per conversation, `ChatTurn`; the model on the device's line; through `WorkspaceExtensions::runChat` and the partner's chat command), `content/chatmodels.h` (the messages rendered, the list) |
| `writing` (ai) | the editor's AI assistant (docs/UI.md "AI in the editor", pure functions): `request()` (Rewrite after a preset or the user's words, Continue, Summarize: the text, its context, the note's Markdown kept), `clean()` (the answer as it goes into the note: fences, preambles, quotes, a list marker an inline text does not take, a repeated end), `edit()` / `apply()` (where the result goes: a pure edit), `diff()` (the change word by word). The app's side: `shell/writingsession.h` (a request on its way, the result as it comes, sent through `WorkspaceExtensions::sendAi`), `qml/editor/WritingDialog.qml` |
| `extensions` (util, text, dates, markdown, vault, index, notebook, sync; 3rdparty/luau) | Brulion's extensions in Luau (docs/EXTENSIONS.md): `Engine` (`engine.h`: one sandboxed VM — an environment and a read-only `b` per extension, the source compiled by the host —; commands with labels, views with named actions, `b.ui` nodes; questions `b.ask`/`b.choose`/`b.form` that suspend a call and `resume()`; a time limit through the VM's interrupt on its own thread and a memory limit through the allocator, neither catchable; errors with the extension's line; `setjmp`/`longjmp` errors, so the C functions are wrappers that raise or yield after their C++ objects are gone), `Host` (`host.h`: what an extension may ask of the app), `NotebookHost` (`notebookhost.h`: over a `Vault` and its `VaultIndex` — blocks as maps with an `ItemRef` line reference, page tags, `appendToJournal` through `notebook/journals.h`, `replaceBlock` refused when the line changed (`resolve()`) or when the ref names anything but a line of a note the index knows — no function reaches another file of the notebook's folder —, pages through `notebook/pages.h`), `blockedit.h` (pure: a block line's text and status replaced, Obsidian Tasks dates and a block id kept, `markdown::setTaskStatus`), the built-in extensions (`builtin.h`, resources: the gym `silownia.luau` and its catalog — `exercises.json` and `exercise-instructions.json`, Brulion's 51 exercises and free-exercise-db's, made by `scripts/gym-catalog.py`; `b.data(name, key)` gives one entry —, the habits `nawyki.luau` — docs/SYNTAX.md §5 "Habits", `brulion-cli habit …` runs it too —, the flashcards `fiszki.luau` — docs/SYNTAX.md §15, FSRS-6, `brulion-cli card …` —, the AI assistant `asystent.luau`), extension pages (`pageextension.h`: pages tagged `#rozszerzenie` — their id, their ```` ```luau ```` code with the page's line numbers, permissions, the SHA-256 an approval covers, the new-extension page, a built-in's source as a page that overrides it); macros and template variables (`b.macro`, `b.variable`; `macros.h`: what a value shows, `MacroCache` — values with what their calls read, which `Engine::startRecording` notes — and the bounds) and views as panels (`meta.panel`); `b.on` hooks (`save`, `change`, `task`, `journal`, `open`, `day`, `timer`; what each extension hears of a burst of changes: `events.h`), shared code (`b.require`: another page's code run in each user's own environment), settings and timers of an extension's own, copies to another notebook (`share.h`), Luau's type checker on save (`typecheck.h` over `3rdparty/luau/Analysis`, with `BRULION_LUAU_CHECK`: not in the browser), hostile extensions as a self-test (`selftest.h`, also `?selftest=extensions` in the browser), `Engine::evaluate` (the console) and `b.ai` (a call that waits for the host's answer: any number of calls may wait at once, each under its id, while others run; `resume(call, answer)`). Luau 0.743 vendored in `3rdparty/luau` (VM + compiler, and Analysis + Config for the type checker; patched: the plain string search honours the time limit; numbers read and written with `.` whatever the C locale). Its documentation is tested (`tst_extensions_docs`): every ```` ```luau ```` example of docs/EXTENSIONS.md, docs/SYNTAX.md and the documentation notebook's pages runs on a real notebook, and the references name every function of `b`; ten sample extensions (`tests/core/extensions/samples`, pages as people write them) run together on a notebook of their own (`tst_extensions_samples`; in the app's class: `tst_shell_extensions`) |
| `views` (util, text, dates, markdown, index, dataview) | database views (docs/SYNTAX.md §10 "Views", "Obsidian Bases"; docs/UI.md "Database views"): `buildQueryView` (`view.h`: a query's results as a `ViewResult` — columns of built-ins, properties and formulas, cells as Markdown with a value typed for editing, groups, a board's columns, a calendar's month, summaries, the new row), pure edits (`edits.h`: `applyRowEdit` — a page property through `markdown/pageprops.h`, a block's `key::` line through `markdown/blockprops.h`, a task's status, priority or date —, `moveEdit` / `dateEdit` for cards and calendar entries, `newPageContent` / `newTaskLines`, `setQueryOption` for the query's own `columns:` / `sort:`), Obsidian Bases (`bases.h`: `parseBase`, `runBase`, `setBaseViewOrder` / `setBaseViewSort` writing a view back verified by reading it), formulas and Bases' expressions through `dataview/expression.h`. QtCore only |
| `notebook` (util, text, dates, markdown, vault, index, dataview, views, reminders, drawing, periodic) | what the app and the command line do to a notebook, so both behave identically (Workspace delegates here, `brulion-cli` is built on it): quick capture lines (`capture.h`: `buildCaptureLine`), a block placed right after (or before) the block embedding an attachment (`embeds.h`: `insertBlockAfterEmbed` — the PDF viewer's drawing under its PDF —, `insertBlockBeforeEmbed` — the drawing editor's previous PDF page above the current one's), journals (`journals.h`: `journalFileFor` under any accepted name, `ensureJournal` from the template — never over a note another program just created —, pure `appendBlock` / `blockFromText`), weekly and monthly notes (`periodicnotes.h`: `periodicFileFor`, `newPeriodicText`, `ensurePeriodicNote` from the period's template; the names: the `periodic` module), pages and folders (`pages.h`: `newPagePath`, `createPage` / `plannedPage` with letter-case twins, `createFolder`, `folderInfo`, every rename and move through `relocate()` on `VaultIndex::planRelocation()` with `.brulion/config.json`'s special folders rebased, `deletePath`; problems as `PathProblem` kinds the app turns into its messages), the links the editor, the page menu and the CLI copy (`links.h`: `pageLinkTarget` — a daily note's file name, else `VaultIndex::linkTarget` —, `blockLinks`, `copiedLink` — the link menus' "Copy link" of a rendered link), item references (`itemref.h`: `Dom.md#L12@hash`, `^id`; `resolve()` → Ok / Moved / Stale), JSON of the model (`json.h`, also the names of the JSON export; `dataviewResultJson`: a Dataview result as `QueryHost::runDataview` gives it), what other apps share ("Share → Brulion", the web share target, files opened with Brulion on a desktop; `share.h`: `sharedContentFromJson` — the platform's JSON read defensively — and `sharedContentToJson`, `sharedContentFromQuery` — the share target's form-encoded query —, `splitOpenArguments` / `openedFilesShare` / `mergeOpenedShares` — `--open` files, Windows' one launch per file merged —, `pageForOpenedFile` — an opened file that already is a page of the notebook —, `composeSharedText` — "[Title](url) — text" —, `checkSharedFile` / `looksBinary` — text/* only, `SharedFileMaxBytes` —, `decodeSharedBytes`, `sharedPageTitle` / `sharedPageContent` for a new page and `sharedFileBlock` for one journal block per file), and `Notebook` (`session.h`: a vault opened headless — a missing folder is never created —, a lazy index kept up to date, the device's write lock, `edit()` = intent + compare-and-swap retries + History reason "cli"; `deviceStatePath()` shared with the app), `IndexLoader` (`indexloader.h`: opens an index — reads (`Vault::readDetached`), decodes, takes notes from the index cache or parses them on the `WorkerPool`'s threads, builds the full-text index on one more, merges on the owner's thread in slices (`step()`, `finish()`; the workers' results taken in batches, one lock and one wake-up per batch), journals first, back-pressure, `forget()` for files changed meanwhile; inline without threads) and `IndexCacheKeeper` (`indexcachekeeper.h`: `<device state>/index-cache.bin` — in the browser in a cache mount of its own — written after a load that parsed, when the app asks (`saveIfDirty()`: the app goes to the background, the notebook closes) and at the latest 10 minutes after the first unsaved change while the app stays in front — never after every pause in typing; the owner's thread only takes the snapshot, the encoding and the `QSaveFile` run on a pool thread — inline without threads: in slices, at once in `saveIfDirty()` —, a newer snapshot's file never replaced by an older one; none under 200 notes; `indexCacheBudget()` caps it where storage has a quota); `cleanup.h`: `unreferencedFiles` — the notes and attachments nothing refers to (docs/UI.md "Cleaning up"); `replace.h`: text replaced in the whole notebook with a preview (`ReplaceScan` on the `WorkerPool`, `applyToFile` refusing a file changed since the plan, `undoReplacedFile`; docs/UI.md "Find and replace") |
| `backup` (util, vault, notebook, log; zip through the vendored miniz) | automatic backups (`backup.h`: names `Brulion-<notebook>-yyyy-MM-dd-HHmmss.zip`, `listBackups`, `backupManifest` / `readBackupManifest` — the ZIP's comment: notebook id, complete or not —, `backupsToPrune` / `pruneBackups` — only this notebook's by that id, complete and incomplete copies counted apart, never a protected one —, `collectSources` + `fingerprint`, `isDue` / `decide` — a clock set back is due, an empty notebook never backed up by schedule —, `isInsideFolder`, `checkFolderWritable`; `archive.h`: `BackupWriter` — one ZIP streamed with miniz through a `QSaveFile`, a file that changes while being read read again before its entry is committed, compressed formats stored; `ArchiveReader` + `checkEntryPath` — strict validation of a backup, no entry trusted —; `FolderWork`; `restore.h`: pure `planRestore`, `RestoreChecker` — validates, compares by size then CRC-32, inflates what would be written —, `RestoreApplier` — through the Vault, `ChangeSource::Import`, trash last). Threads: a `notebook::WorkerPool`, without one slices on the caller's thread. The app's side: `shell/workspacebackups.h` |
| `automation` (util, text, dates, markdown, vault, index, notebook) | brulion:// addresses (docs/UI.md "Automation"): `parseUri` / `buildUri` (`uri.h`: the scheme's spellings, a form-encoded query — "+" a space, %XX UTF-8 —, aliases, the common parameters `vault`, `key`, `x-source`, `id`, `x-success`/`x-error`/`x-cancel`, limits, `redactedUri` for logs, `withQuery` for callbacks); `accessOf` / `decide` (`policy.h`: opening always, writing and extension commands with the notebook's key or the user's confirmation, policies ask / key / off, `mayCallBack`), `KeyStore` (a random key per notebook and device in `<app data>/secrets/automation-keys.json`, compared in constant time); `insertBlock` (`edits.h`: a block at the end or start of a page, a heading's section or a block's children, untouched bytes kept) and `buildTask` (natural-language dates); `plan` / `execute` (`plan.h`: pages by path, name, alias, ISO date or phrase, daily notes from the journal template, new pages from templates never over an existing one, routes of open / daily / search / view; the write through a `Writer` — `NotebookWriter` headless, the app's vault writer), `matchNotebook`; used by the app (`appautomation.cpp`), brulion-cli (`uri …`) and Android's background run (`src/app/background/automationrun.h`) |
| `io` (util, text, dates, markdown, index, notebook, drawing, diagram, calc, chart, canvas; QtGui) | importers (Obsidian, Logseq, Notion, Evernote ENEX, Google Keep, Microsoft To Do (Graph JSON: the `mstodo` download or a Graph Explorer file; lists → pages of tasks, dateTimeTimeZone values converted to the vault's zone), Todoist (a project's CSV template, a backup ZIP or the Sync API's JSON → pages of tasks), CSV and Airtable (`csvimport.h`: encoding / separator / header detection, inferred property types, title templates, links between rows, attachments, an Obsidian base or one table page; Airtable's CSV and its API bundle), OneNote (the `cloudimport` download's folder: Graph's page HTML → Markdown, page levels → folders), Apple Notes (a Mac's NoteStore.sqlite read by `sqlitereader_p.h` — a read-only SQLite file reader with the write-ahead log — and `protobuf_p.h`; tables from their CRDT), Joplin, Simplenote, Roam, Trilium (a canvas note → one drawing SVG: Trilium's picture with the Excalidraw scene embedded, `drawing::svgWithScene` — a scene Brulion only shows as Trilium stored it, in its order —, the pictures pasted into it in the scene's files while their data URLs fit a scene; else `drawing::toSvg`), Bear, Markdown/TXT, HTML, iCalendar) with auto-detection and safe target paths; exporters (Markdown ZIP, Obsidian, Logseq, HTML site, single HTML, PDF, DOCX, Pandoc Markdown, JSON, iCalendar, OPML, CSV tasks; paper documents (`paper.h`: the single HTML export made into a `QTextDocument` that loads only the export's own pictures — checkbox markers, SVG diagrams as pictures, dates in words, huge tables cut — painted by `QPdfWriter` with embedded fonts, walked into Office Open XML written by Brulion for DOCX, or painted on any `QPagedPaintDevice` by `printNotes()`: the app's `QPrinter` on the desktop; A4 or Letter by locale); ```` ```mermaid ```` diagrams and ```` ```chart ```` charts (with their values in words) as inline SVG in the HTML exports (`diagram::toSvg`, light and dark) with their source folded under them, as code elsewhere; one table as CSV and back (`tablecsv.h`: formulas as results, separator detection, bounded); a tag links to the page it names in HTML and Pandoc and is written `#[[Namespace/Page]]` for Logseq; `HtmlRenderer` and `PandocRenderer` share inline atoms and emphasis pairing in `rendercommon_p.h`); ZIP (vendored `3rdparty/miniz`), TAR, CSV, iCalendar; HTML→Markdown via `QTextDocument` (also pasted HTML: `EditorHost::htmlToMarkdown`); `clipboardHtml` — copied blocks as escaped semantic HTML for the clipboard's text/html (`EditorHost::renderHtml`; pictures as links, never data: URLs; dates of any block; `ClipboardHtmlMarker`: a paste recognises Brulion's own copy and takes its Markdown); pictures from the internet in the HTML and Pandoc exports: `webPictureTargets` (the URLs they show) and `ExportInput::webPictures` (copies the app supplies from its cache: `assets/web/…` files or data: URIs; the exporters never touch the network) |
| `publish` (util, text, markdown, index, io, github, sync, log, chat; QtNetwork) | publishing a page or chosen notes as a website (docs/UI.md "Publishing", docs/CLI.md "Publishing"). `selection.h`: `selectFiles(index, PublishScope)` — a target's items (pages, folders, tags with sub-tags, the notebook), one step of links, `publish:` / `publikuj:` (`publishFlag`), what is never published (hidden paths, conflict copies, encrypted pages and files — `stripEncrypted`: Meld Encrypt's markers, ```` ```encrypted ```` / `secret` / `age` / `pgp` fences, armoured messages, `encrypted: true`) and what only when chosen (extension pages, the `AI/` folder, templates), with every file left out and its reason; `site.h`: `buildSite` — the HTML site export as files (`io::htmlSiteFiles` with the search box) or the HTML document as `index.html`, fragments cut out, `.nojekyll` for GitHub, each file's SHA-256 and Git blob id; `manifest.h`: what the last publish put where (by `identity()` of the place); `plan.h`: `planPublish` / `planUnpublish` — upload, update, unchanged, delete, conflict, compared by Git blob id (GitHub), SHA-256 (a folder) or the ETag the last upload left (WebDAV) — the dry run; `publisher.h`: `Publisher` over `sync::HttpTransport` with a backend per kind — GitHub Pages (`github::GitHubClient`: one commit, text inline in the tree, binaries as blobs, a missing branch made without history, an empty repository started with the contents API, a fast-forward only — the plan made again once when the branch moved —, Pages turned on for `/` or `/docs`), WebDAV (`sync::WebDavClient`: If-None-Match / If-Match, a 412 a conflict, emptied folders removed, the ETags a PUT did not give taken from a listing), Nextcloud (WebDAV + `ocs.h`: `OcsShareClient`, the public link made, kept, its password and expiry set, removed), a folder (QSaveFile, hidden files never touched); `store.h`: `PublishStore` — `publish/targets.json` and a manifest per target in the notebook's device state folder (the app and brulion-cli share them), secrets in owner-only files (`secretFile`, `githubTokenFile`); `messages.h`: the texts. The app's side: `io/publishcontroller.h`; brulion-cli: `commands_publish.cpp` |
| `github` (util, text, vault, log, sync, drawing, canvas; QtNetwork) | a notebook kept in a folder of a GitHub repository's branch (docs/UI.md "Notebooks from GitHub") — GitHub's REST API, not Git: one implementation on every platform, the browser too (api.github.com answers CORS requests), only the folder's files travel. `address.h`: `parseRepoAddress` (`owner/name`, github.com pages of a repository, folder or file, `git@`/`ssh://` remotes, raw.githubusercontent.com; percent-decoded), `splitRefPath` (the branch and the folder of `…/tree/<a>/<b>/<c>`: the longest existing branch wins — a name may hold "/"), `isValidOwner` / `isValidRepoName` / `isValidBranchName` (git check-ref-format), `normalizeFolder`, `webUrl` / `commitUrl` / `fileUrl` / `newTokenUrl`; `gitobjects.h`: `blobSha` (Git's blob id: a file's version known without downloading it), `isLfsPointer`; `client.h`: `GitHubClient` over `sync::HttpTransport` — user, repository (permissions), branch (If-None-Match: an unmoved branch answers 304, free of the rate limit), matching refs, trees, the contents API's folder listing (a folder's tree id), blobs (raw media type), raw.githubusercontent.com files (public repositories without a token: not rate limited like the API), blob / tree (inline text, removals) / commit creation, references (never forced), forks, pull requests, `putFile` (an empty repository's first commit); `GitHubError` (Auth, Forbidden, RateLimited with the reset time or Retry-After, NotFound, EmptyRepository, NotFastForward, Validation, Server, Network, Timeout, Parse, Integrity, TooLarge, Cancelled); the token masked in logs (`log::ScopedSecret`), sent to api.github.com only; `reposync.h`: `RepoSync` (vault, client, the vault's LocalStore: "github/…" — the link, the commit last pulled or made, its trees, every notebook file's blob id there = the merge base, LFS pointers left out, names spelled otherwise, the proposal) — `pull` (the branch, the folder's tree, then per file: downloaded and verified, to the trash, merged three-way like Nextcloud's — `sync::merge3`, `mergeJson3`, `drawing::mergeDrawingFiles` — or GitHub's version as a conflict copy `Name.conflict-GitHub-<stamp>.ext`, once; nothing removed when the folder lost every file), `commit` (a pull first, the selected changes as one commit on the head, the branch fast-forwarded — pulled and committed again once when it moved), `propose` (a pull request from a branch of the repository or of the user's fork, made and waited for; updated while open, a merge commit when the base moved), `localChanges`, `isNotebookPath` (no hidden files: the repository's). The app's side: `shell/workspacegithub.h` |
| `cloudimport` (sync, log, io; QtNetwork) | Downloads for the import from web services: `ApiClient` (a service's requests: the token as a Bearer header only to its hosts, https only, retries after 429/503/504 with `Retry-After`, a body limit), `OneNoteDownload` (Graph with a token of `mstodo`'s `DeviceCodeLogin` and scope `Notes.Read`: notebooks, nested section groups, paged pages with levels, page HTML, resources within a size budget → `OneNote/onenote.json` + pages + resources), `TodoistDownload` (the Sync API with a personal token, comment files), `AirtableDownload` (bases, tables, paged records, attachments with a personal access token), `UrlFilesDownload` (a CSV's attachment cells, no credentials) → input files for the io importers; tokens in memory, registered with `log::redact` |
| `mstodo` (sync, log; QtNetwork) | Microsoft To Do download for the import: `DeviceCodeLogin` (OAuth 2.0 device-code sign-in at login.microsoftonline.com/{authority}/oauth2/v2.0 with the user's own public-client app registration, scope `Tasks.Read`, polling with `authorization_pending` / `slow_down`, AADSTS answers explained, no refresh token), `TodoDownload` (Graph `/me/todo/lists`, every list's tasks with `$expand=checklistItems,linkedResources` — task by task when Graph refuses it or leaves the expansion out —, attachments within a size budget, `@odata.nextLink` paging only on graph.microsoft.com, `Retry-After` on 429/503, a lost connection or an expired sign-in stops it) → the JSON bundle the io importer `mstodo` reads; HTTP only through sync's `HttpTransport`, the token in memory and registered with `log::redact` while in use, nothing stored |
| `caldav` (util, text, dates, markdown, index, vault, notebook, log, sync, io; QtNetwork) | calendars of other programs (docs/UI.md "Calendars", docs/SYNTAX.md §5 "Tasks synced with CalDAV"): `rrule.h` — RFC 5545 recurrence in full (`RRule`, `parseRRule` / `formatRRule`, `expandRRule` in the series' own wall clock: COUNT counted from DTSTART, UNTIL, every BYxxx with RFC 5545's expand / limit rules, BYSETPOS, WKST; bounded by `ExpandLimits` — a rule that never matches or never ends stops —, windows far from DTSTART skip the periods before them); `zones.h` — a TZID through the system's database (IANA ids, `/mozilla.org/…` prefixes, Windows names) else the file's own VTIMEZONE (`ZoneDefinition`: observances with DTSTART, RRULE, RDATE; gaps taken in the offset before, repeated hours the first time — WebAssembly has no database), `ZoneTable`; `events.h` — `CalendarData` (VEVENTs parsed within `ParseLimits`: bytes, events, text lengths), `occurrences()` into the device's wall clock (RDATE incl. PERIOD, EXDATE incl. a DATE on a timed series, RECURRENCE-ID overrides and cancelled instances, all-day and multi-day events, spans capped at a year); `feed.h` — `feedUrl` (webcal://), `FeedFetcher` (conditional GET, redirects by hand never down to http, 16 MB, an HTML page refused, CORS told apart in the browser), `FeedHttpTransport` (QNAM following redirects to other servers: no credentials travel); `client.h` — `CalDavClient` (discovery, `sync-collection` with the ETag listing as fallback, `calendar-multiget` with GET as fallback, If-Match / If-None-Match writes, Basic and Digest, `mayCarryCredentials`: the account's server or its site over https); `taskmap.h` — `TaskFields` (a task line ↔ a VTODO, normalised: priority classes, waiting as to do, trailing tags as categories), `applyFieldsToBody` (only the tokens that change), `patchTodo` / `newTodo` (the server's VTODO patched: what Brulion does not know stays; VTIMEZONE of the device's zone), `blockIdForUid` / `uidForBlockId`; `eventedit.h` — events written by the event dialog (a series, an override, an EXDATE); `store.h` — `CalendarStore` (`<device state>/calendars/`: settings, feed caches with validators, per calendar the sync token, members, tracked tasks with both sides' last fields and the outbox of events, the log); `calendarsync.h` — `CalendarSync`, a `sync::SyncRunner` (due subscriptions, every calendar: outbox, changes, the task reconciliation of `tasksync_p.h` — merged field by field, conflicts by the account's policy, deletions behind a guard, a recurring task completed in Brulion advancing the server's series —, a device lock so that the app and Android's background check never run together). The app's side: `shell/workspacecalendars.h` (an `planning::EventSource` for `CalendarModel`) |
| `media` (util, markdown) | audio and video in notes, the part without a player (docs/SYNTAX.md §2 "Audio and video"): `kindOf` (a file's player by its name, over `paths::isAudioPath` / `isVideoPath`), `parseTimeFragment` / `timeFragment` (W3C Media Fragments' `t=` — seconds, `[hh:]mm:ss`, `npt:`, spans, YouTube's `1m23s` — and back), `formatTime` (the player's "1:23" / "1:02:03"), `recordingName` (a voice note's "Nagranie 2026-11-20 18.30.m4a"); `transcript.h` — `transcriptText` (what a recognizer heard as one plain paragraph: joined, collapsed, Markdown at its start and links, embeds, templates and tags inside escaped), `placeTranscript` (the transcript under the block that embeds the recording: a list item's first child, a paragraph's next paragraph — a `BlockDocument` edit, every other byte kept); `pcm.h` — WAV files (`wavHeader`, `wavFile`, `readWav`: integer and float PCM, WAVE_FORMAT_EXTENSIBLE, RF64, cut-off files), `toMono`, `resample` (a windowed sinc from a table: 16 kHz for Whisper), `meterLevel` (the recorder's meter: the peak from −60 to 0 dB) |
| `speech` (ai, media) | speech recognition on the device (docs/UI.md "Voice notes"): `speechModels()` (Whisper Small / Base / Large v3 Turbo by OpenAI, MIT — whisper.cpp's conversions on Hugging Face, one revision, SHA-256, as `ai::LocalModel`s of the format "whisper", downloaded by `ai::LocalModelFiles`), `speechLanguages()` (auto, pl, en), `spokenTexts()` (Whisper's notes for no speech and subtitle credits left out); `speechrunner.h` — `SpeechRunner` (16 kHz mono in, segments out; progress, cancel) and `loadSpeechRunner()` (the plugin `brulion_speech`, loaded on first use). The recognizer, `whisperrunner.h` over whisper.cpp (`3rdparty/whisper`) and llama.cpp's ggml — the processor plugin the language models loaded, or the first this processor runs; beam search of 3, at most 4 threads —, is the library `brulion_speech_whisper` (and that plugin), built only with `BRULION_SPEECH` |
| `crypto` (—; 3rdparty/bearssl) | the cryptographic primitives, GUI-free and pure, for any module (docs: `crypto/crypto.h` and "Security" below): `SecureBytes` (a byte buffer zeroed when destroyed, resized or cleared; deep copies; `fromUtf8` straight into it), `wipe()` (volatile stores and a fence; QByteArray / QString only when unshared), `constantTimeEquals()`; `RandomSource` / `systemRandom()` (QRandomGenerator::system) / `randomBytes()`; `encodeBase64` / `decodeBase64` (Strict: canonical, padded; Forgiving: as the browser's atob); `digest` / `Hasher` (SHA-256, SHA-512), `hmac`, `pbkdf2` (RFC 8018 over BearSSL's HMAC with the key's pads hashed once: two compressions a round); `aesgcm::seal` / `open` (AES-128/192/256-GCM, any IV length, the 16-byte tag after the ciphertext as WebCrypto lays it out; the tag checked in constant time before any plaintext is handed out); `hashSecret` / `verifySecret` (`pbkdf2-sha512$<rounds>$<salt>$<hash>`, rounds bounded). BearSSL (MIT) vendored unmodified — aes_ct64 (bitsliced, no secret-indexed tables), ghash_ctmul64, GCM, SHA-2, HMAC; compiled as C, `-O2` in every build type. Tests: the FIPS 180-4, RFC 4231, RFC 7914 and GCM specification vectors |
| `encryption` (util, markdown, crypto) | encrypted notes in Obsidian's Meld Encrypt formats (docs/SYNTAX.md §11 "Encrypted text"): `decryptPayload` (inline fragments v0, α, β, γ), `encryptPayload` (γ: suite 1, PBKDF2-SHA512 220 000 rounds, the JSON header as AAD), `readNoteFile` / `decryptNote` / `openNote` / `openNoteWithKey` / `writeNoteFile` (`.mdenc` 1.0 / 2.0 read, 3.0 read as the plugin validates it and written byte for byte as its `JSON.stringify(…, null, 2)`; `NoteKey`: an open page's key, saves without a derivation), `Failure` (WrongPassword, Malformed, Unsupported, TooLarge) and `failureMessage`; the edits of fragments (`fragments.h`: `encryptFragment`, `decryptFragment`, `reencryptFragment` — hint and look kept, an old version made γ —, `changeFragmentPassword`, `decryptInPlace`); `Keyring` (the session's passwords per page in `SecureBytes`, at most two candidates a try, all forgotten after an inactivity timeout or `clear()`). The syntax of fragments is the markdown module's (`markdown/encrypted.h`). Tests: fixtures made by the plugin's own code, its deterministic output reproduced byte for byte, wrong passwords, malformed and tampered payloads and files |
| `applock` (crypto) | the app lock (docs/UI.md "App lock"): `Policy`, `LockState` (locked on start, after the background, after idle time, on lockNow(); waits after wrong PINs — `retryDelayMs`: none for four, then 30 s … 15 min — kept across restarts by the app, a clock set back ends a wait), `isValidPin` (4–16 ASCII digits), `hashPin` / `verifyPin` (crypto's salted PBKDF2-SHA512, 210 000 rounds). No clock, no platform: the app feeds it (`AppLockController`) |
| `semantic` (—) | search by meaning, a prototype (docs/SEMANTIC.md): `chunkNote` (notes cut into ~120-word chunks), `Calibration` (gradient-free edit of the embeddings: means and the Polish–English direction), `VectorIndex` (brute-force cosine, a note scores as its best chunk), `fuseConvex` (with the full-text hits). The model runner `Embedder` (llama.cpp, GPU through Vulkan) is the separate library `brulion_semantic_llama`, built only when CMake finds llama.cpp and linked by no app target yet |

## Platform layer (`src/platform/`, library `brulion_platform`)

Only what needs OS APIs; compiled per target (no `#ifdef`s in core). `platform.h`:
`targetName()` ("linux" | "windows" | "android" | "wasm").

* **Everywhere** (`platform.h`): `logDirectory()` (Linux `$XDG_STATE_HOME/Brulion/Brulion/logs`,
  Android the app's files dir + `/logs`, the browser none: memory only) and `deviceDescription()`
  for the event log; `keepScreenOn(on)` — Android: the activity's window keeps the screen on while
  a model on the device is downloaded or answers (`LocalAi::working()`; nothing elsewhere); the app
  lock's (docs/UI.md "App lock"): `setSecureWindow(secure, hideRecents)` (Android: FLAG_SECURE, the
  recent apps' preview on Android 13+), `deviceAuthAvailable()` / `requestDeviceAuth()` (Android
  10+: BiometricPrompt, the answer as `AndroidBridge::deviceAuthFinished`), `copySensitiveText()`
  (Android 13+: a clip marked sensitive) — nothing elsewhere;
  `allowForegroundActivation()` — Windows: `AllowSetForegroundWindow` before a
  second launch hands its files or request to the running Brulion, so its window comes to the
  front (nothing elsewhere); `endStartupNotification(id)` — X11: such a launch ends the
  launcher's startup notification (`StartupNotify=true`) itself, through Qt's xcb plugin, which
  says "remove" once the process shows a window — a 1×1 tooltip window off the screen (nothing
  elsewhere).
* **Crash report** (`crashreport.h`; tests/platform `tst_crashreport`): `installCrashReport()`,
  first in `main()` — Linux with glibc (the desktop, the AppImage, the Nix package, the test runs
  of CI): on SIGSEGV, SIGBUS, SIGFPE, SIGILL or SIGABRT stderr gets the signal, the faulting
  address, the crashing thread's name and id and that thread's stack (`backtrace_symbols_fd`:
  exported symbols by name, the rest as `file(+offset)` for `addr2line -f -C -e <file>
  <offset>` with the same build), then the process dies of the same signal (exit status and core
  dump unchanged). Nothing elsewhere and in sanitizer builds, whose own reports win.
* **PDF pages** (`pdfpages.h`; tests/platform `tst_pdfpages`): `pdfAvailable()`, `pdfInfo(file,
  pageSizes)` — the pages and their sizes in points (all, or the first page's only), a
  password-protected file `locked` —,
  `renderPdfPage(file, page, maxSide)` — a page on white, its long side `maxSide` px (a drawing's
  backdrop, the page picker's thumbnails); thread-safe, every call opens the file. Both are built
  on `PdfReader` (the PDF viewer's, docs/UI.md "PDF viewer"): `PdfReader::open(file, &info)` parses
  the file once (the page count and the first page's size only), then `pageSize(page)`,
  `render(page, full, clip)` — the page scaled to `full` pixels, of it the part `clip` (the viewer's
  detail pictures; at most `MaxPdfFullSide` 32768 px long, `MaxPdfClipPixels` 16 MP per picture; a
  huge or hostile page costs no more memory than the picture asked for) —, `pageText(page)` and
  `find(page, query)` (`PdfTextHit`: a range of the text and its rectangles in points, the page as
  shown), `selectText(page, from, to)` / `selectRange(page, start, length)` (`PdfTextSelection`:
  the range, the clipboard's text, its rectangles), `outline()` (`PdfOutlineItem`s depth first,
  capped; poppler only — Qt PDF's bookmark model has no guard against a looping outline) and
  `links(page)` (`PdfLink`: an area to a page of the PDF or to an http(s) / mailto address; Qt PDF
  and poppler); `open(file, info, password)` opens a protected PDF (Qt PDF and poppler:
  `pdfPasswordAvailable()`); a reader is used by one thread at a time (`attachToCurrentThread` / `detachFromThread`:
  Qt PDF's document is pulled to the leasing thread). `pdfTextAvailable()`: Qt PDF and poppler yes,
  Android from Android 15 (API 35), none no — the viewer hides its search otherwise;
  `pdfSelectionAvailable()`: Qt PDF and poppler (Android's API 35 selection is not wired yet). One backend
  per build, chosen by `src/platform/CMakeLists.txt` (`BRULION_PDF_BACKEND`, default `auto`):
  Android `pdfpages_android.cpp` (JNI to `BrulionAndroid.pdfOpen` / `pdfPageSizes` /
  `pdfRenderInto` / `pdfPageText` / `pdfSearch` / `pdfClose` → `PdfPages.kt`: sessions of the
  system's `android.graphics.pdf.PdfRenderer`, at most 16 open — well above the reader pool; a
  reader whose session was closed under it is `lost()` and dropped —, a page drawn straight into the
  QImage's pixels through a direct ByteBuffer; no library in the APK), else Qt PDF when Qt has the
  module (`pdfpages_qtpdf.cpp`, `QPdfDocument`: Windows, the AppImage, CI — aqt's `qtpdf`; text
  from `getAllText`, rectangles from `getSelectionAtIndex`), else poppler-qt6 through pkg-config
  (`pdfpages_poppler.cpp`: the Nix package — nixpkgs has Qt PDF only inside QtWebEngine; text from
  `textList()`'s words and their characters' boxes), else none (`pdfpages_none.cpp`: the browser).
  `pdfBackendName()` names it; sizes and the reasons: docs/PERFORMANCE.md "Size".
* **Searching a PDF's text** (`pdfsearch.h`; pure, tests/platform `tst_pdfsearch`):
  `findPdfText(text, query, maxHits)` — `text::foldKey` on both (case and Polish letters: "gesla"
  finds "gęślą"), runs of white space equal, soft hyphens ignored, ranges never splitting a
  surrogate pair or a letter from its marks —, `pdfRangeRects(boxes, range)` (one rectangle per
  line from the characters' boxes), `pdfMatchSpellings` (the spellings Android's own,
  case-insensitive-only search is asked for).
* **Text picked by pointing and what a PDF says about itself** (`pdftext.h`; pure, tests/platform
  `tst_pdftext`): `pdfNearestBox` / `pdfCaretAt` / `pdfRangeBetween` (the characters between two
  points from per-character boxes: poppler's whole selection, Qt PDF's points moved onto lines),
  `pdfWordAt` (a long press's word), `pdfCopyText` (the clipboard's text), `pdfCleanTitle` (an
  outline title), `pdfLinkUrlAllowed` (a PDF's link that may open: http, https, mailto).
* **Text recognition (OCR)** (`ocr.h`; tests/platform `tst_ocr`, skipped without Tesseract): one
  backend per build (`BRULION_OCR_BACKEND`, default `auto`): `ocr_tesseract.cpp` on the desktop —
  Tesseract 4/5 loaded at run time through its C API (`QLibrary`: no headers, nothing linked; the
  Nix package compiles in its own library's store path, `BRULION_TESSERACT_LIBRARY`, and the
  English and Polish `tessdata_fast` models, `BRULION_TESSDATA_DIRS`; elsewhere the system's or a
  Windows installation's), OpenMP held to one thread (`OMP_THREAD_LIMIT=1`); `ocr_none.cpp` on
  Android and in the browser. `ocrAvailable()`, `ocrUnavailableReason()` ("unsupported", "noLibrary",
  "noLanguages"), `ocrLanguages()` (the models found), `OcrEngine::create(languages)` (one per thread)
  → `recognize(image, dpi, cancelled)`: `OcrWord`s (text, box in pixels, line, block, confidence;
  below 40 left out), cancellable mid-page through Tesseract's monitor, a 120 s deadline a page.
  Where it looks (`ocrdata.h`; pure, tests/platform `tst_ocrdata`): `tesseractLibraryCandidates`
  (`BRULION_TESSERACT_LIBRARY=none` turns it off), `tessdataCandidates` (the user's `<app
  data>/tessdata` first, `BRULION_TESSDATA_DIR`, `TESSDATA_PREFIX`, the build's, beside the library,
  the system's), `tessdataLanguages`, `tesseractCode` (UI language → Tesseract's code),
  `ocrLanguagesFor` ("auto": the UI language and English, of the models installed).
* **Which attachments may go to another app** (`externalfiles.h`; pure, tests/platform
  `tst_externalfiles`): `externalFileType(name, head)` — an allow-list (PDF, Office Open XML,
  OpenDocument, EPUB, old Office, RTF, audio, video, text) by the extension AND the first 64 KiB
  matching it; its MIME type, or refused (a program, a script, a page, an archive, or one renamed).
* **Spell checking** (`spellbackends.h`; tests/platform `tst_spellbackends`, `tst_spelldictionaries`;
  docs/UI.md "Spell checking"): `spellEngine()`, `spellLanguages()` (cheap: the dictionaries found,
  the system checker's languages) and `createSpellBackend(language)` — a `spell::SpellBackend` made
  and used on the editor's spell-checking thread. One backend per build (`BRULION_SPELL_BACKEND`,
  default `auto`): Android `spell_android.cpp` (JNI to `BrulionAndroid.spellLanguages` /
  `spellOpen` / `spellCheck` / `spellSuggest` → `SpellCheck.kt`: a `SpellCheckerSession` per
  language on a `HandlerThread`, the caller waiting at most 3 s; words as JSON, one call per batch),
  the browser none (`spell_none.cpp`), elsewhere Hunspell when the build has it
  (`spell_hunspell.cpp`; `cmake/BrulionHunspell.cmake`: pkg-config `hunspell` — the Nix package,
  the AppImage — or a Hunspell release compiled in from `BRULION_HUNSPELL_SOURCE_DIR` — Windows),
  else none. Dictionaries (`spelldictionaries.h`, pure): DICPATH, next to the app, the build's
  `BRULION_SPELL_DICTIONARY_DIRS` (Nix store paths), XDG data folders, the system's; pl_PL, en_US
  preferred; their 8-bit encodings through the core's `spell/dictencoding.h`.
* **The tray calendar under Wayland** (`traywindow.h`; docs/UI.md "The tray calendar"):
  `trayPopupAnchorable()` (the build has it and the app runs on Wayland) and
  `anchorTrayPopup(window, margin)` — the window becomes a layer-shell surface in the bottom-right
  corner of the active screen's work area (`AppController::placeTrayPopup`; Wayland ignores a
  window's position and the tray's click comes without one). One backend per build
  (`BRULION_LAYER_SHELL`, default `auto`): KDE's LayerShellQt when found
  (`traywindow_layershell.cpp`; the Nix package — it uses Qt's private Wayland API, so it must be
  built against the same Qt), else none (`traywindow_none.cpp`: the AppImage, Windows, Android, the
  browser). A compositor without the protocol (GNOME) leaves the window ordinary.
* **A file handed to the system's app** (`openexternal.h`; one per build): `openFileExternally(path,
  mime, name, bytes)` → `Opened` | `NoApp` | `Blocked` | `Failed` — the desktop's default app
  (`openexternal_desktop.cpp`: `QDesktopServices::openUrl` of the local file), Android's
  ACTION_VIEW (`openexternal_android.cpp` → `AndroidBridge::viewFile` → `OpenWith.kt`: a content://
  URI of the FileProvider `<package>.qtprovider` with a read grant — its roots in
  `android/res/xml/qtprovider_paths.xml`: the app's files and cache and the shared storage; a file
  on a memory card copied into `cache/open/` first —), the browser's new tab (`wasm/wasmopen.cpp`:
  a blob: URL of the bytes typed explicitly, `window.open` inside the user's click, the opener cut,
  the last three URLs kept; blocked → the bytes saved as a download). `setOpenFileHook` records
  instead (tests, self-tests). The caller decides what may be opened: `Workspace::openInOtherApp`
  hands over only files of the notebook that `externalFileType()` allows (a PDF's first bytes hold
  `%PDF-`, a DOCX is a zip with Word's parts…), with that MIME type.
* **Android documents that name their file** (`documentpaths.h`; pure, tests/platform
  `tst_documentpaths`): `localPathOfDocument(url, primaryStorage)` — the system's picker hands out
  `content://` URIs also for files of the phone's storage; the storage provider's ids
  (`primary:Notatki/a.pdf`, `home:`, a memory card's `1234-ABCD:…`) and Downloads' `raw:` paths
  give the file, so `Workspace::importAttachment` uses a picked file of the notebook where it is
  instead of copying it (a drawing's backdrop); `primaryStorageOf(path)` — `/storage/emulated/<n>`.
* **Windows, pure parts everywhere** (tests/platform): `autostart.h` — "Start when you sign in":
  `Autostart` over two injected `QSettings` (production: `HKCU\…\CurrentVersion\Run` and Task
  Manager's `…\Explorer\StartupApproved\Run`; tests: INI files), the value `Brulion` =
  `"<exe>" --hidden`, on only when it starts this program and Task Manager has not switched it
  off, `refresh()` re-points an entry of a moved portable copy; unsupported off Windows, disabled
  with a separate `BRULION_PROFILE` (AppController's `autostartAvailability` / `startAtLogin`).
  `appidentity.h` — the AppUserModelID `JanuszBit.Brulion` (`.<profile>` for a profile) set for
  the process and registered under `HKCU\Software\Classes\AppUserModelId` (name, icon) by
  `applyAppIdentity()` in main(): what an unpackaged app needs for toasts.
* **Android** (`android/androidbridge.h`): `AndroidBridge`, the JNI side of
  `android/src/io/github/januszbit/brulion/android/` (Kotlin: `BrulionAndroid`,
  `AlarmScheduler`, `ReminderNotifier`, `AlarmActivity`, `ShareActivity`, receivers incl.
  `BOOT_COMPLETED`):
  all-files access, notification permission, exact alarms, full-screen alarm screen,
  `scheduleReminders(json)` (replaces the armed set), notification taps (`activated`,
  `takeActivation()` after a cold start), shares from other apps (`shared` / `takeShare()`: the
  JSON of what `ShareActivity` received, its files copied to `files/share-in/`; docs/ANDROID.md
  "Share → Brulion"), "Done" taps (`DoneQueue.kt`, persisted while Qt is not running or
  in the background, where Qt for Android pauses its event loop: `doneRequestsQueued` +
  `takeDoneRequests()`), the home-screen widgets (`updateWidgets(json)`:
  the snapshot of `src/app/widgets` — the app computes, Kotlin only renders; `takeWidgetActions()`
  / `widgetActionsQueued`: quick-add entries Kotlin could not write, `requeueWidgetActions()`
  those the app could not write either; `widgetNoteWritten`: one it wrote — the reminder set is
  handed over again so Kotlin's provisional reminder gives way), "Save to Downloads" (MediaStore), `shareFile()` (the
  system share sheet for a file below filesDir through Qt's FileProvider), picked documents
  for imports (`documentInfo` / `listDocuments`: DocumentsContract, callable from worker
  threads) and the on-screen keyboard (`imeChanged`: how much of Qt's view the IME covers, from
  `ImeInsets.kt`, frame by frame while it slides on Android 11+, where the window is drawn behind
  the system bars; for `KeyboardInsets`), the system gesture zones at the view's edges
  (`systemGesturesChanged`, same observer; for `GestureInsets`: the drawer's edge swipe starts
  beyond gesture navigation's "back" zone) with the system bars' icons over that window
  (`setSystemBarsLight`). `android/selftest` builds an APK exercising the bridge.
* **WebAssembly** (`wasm/`): `WasmStorage` mounts IDBFS on the vault and app-data folders and
  on the cache mount of the index cache (`indexCacheRoot()`: its own IndexedDB database, so a
  quota failure there is no storage error and never aborts the transaction of History or
  settings — the cache's files are dropped and `cacheNotSaved()` stops the keeper) and on the
  backups' mount (`backupsRoot()`: the same, but nothing is dropped — `backupsNotSaved()`, docs/WEB.md), loads them
  before the app opens anything and writes changes back per mount (debounced, on page hide);
  `WasmNotifier` (Notification API, service-worker fallback, clicks → `activated`);
  `wasmfiles.h` (the browser's own file picker — several files, those above a limit never read —
  and its save picker or a download, over Qt's private `QWasmLocalFileAccess`; the browser build
  has no Qt Widgets; the pick's bookkeeping `PickCollector` is the pure `pickedfiles.h`, tested
  everywhere); `WasmShare` (the share target launched in another tab while Brulion runs here: `wasm/index.html`
  posts the launch's query on the BroadcastChannel `brulion-share:<site path>`, the tab holding the
  storage lock answers and emits `received(query)` → `AppController::receiveShareQuery`; the
  listener is installed before the storage asks for its lock, queries before the controller exists
  are kept; docs/WEB.md "Sharing to Brulion");
  `wasmshell.h` (`setShellStage`/`hideShellLoadingScreen`: the loading screen of
  `wasm/index.html`; `watchVisualViewport`: readings of the browser's viewports and Qt's container
  for the on-screen keyboard — the computation is `ViewportKeyboard` in `ui/keyboardinsets.h`,
  unit-tested); `wasmselftest.h` (`runStorageSelfTest`/`runNotifierSelfTest` for
  `?selftest=storage,notify`, `runFilePickerSelfTest` for `?selftest=pickfiles,savefile` in a test
  tab, `publishSelfTestResult` — also the share dialog's data for `?selftest=share` —, driven by
  `scripts/wasm-smoke.mjs`). Self-tests that need the running app
  live in the app: `src/app/diag/webselftest.h` (library `brulion_webselftest`, linked into the
  browser build only; `?selftest=webpictures`: pictures from the internet as the editor fetches
  them and an export with copies — only where the device shows such pictures without asking —,
  `?selftest=pen`: the drawing canvas under a pen), driven by the Playwright tests in
  `tests/web`. `wasm/index.html` passes those two flows, `pickfiles`, `savefile` and `?route=`/`?now=` only in a tab
  marked for tests (sessionStorage `brulion-selftest`, which a link cannot set), each as one
  `--option=value` argument.

The notifier adapters themselves live in the app (`src/app/notify`, below), because they also
talk to the tray and the settings.

## App (`src/app/`, QML module `Brulion`)

`main.cpp` creates the `QApplication` (desktop: tray, `FilePicker`'s fallback dialog) /
`QGuiApplication` (Android and the browser: no Qt Widgets there; the AppImage first asks for the
XDG portal's platform theme — the desktop's own file pickers; on every platform the QML engine's
garbage collector does whole runs, `QV4_GC_TIMELIMIT=0` — docs/UI.md "The QML engine's garbage
collector"), the translator,
the fallback fonts for emoji and symbols (`installFallbackFonts()`: always in the browser, whose
Qt has only DejaVu Sans/Mono; on Linux only without a system emoji font, else just the symbol
font for fontconfig's fallbacks; never on Android —
docs/UI.md "Emoji and symbol fonts"), `DevTools` and `AppController`, then loads `Main.qml` (or
a dev tool window). Each area is a
directory with an `area.cmake` (included by `src/app/CMakeLists.txt`) and, for its C++, a static
library that tests link without the app; classes reach QML through `QML_FOREIGN` wrappers
(`qmlforeign.h`, `editorforeign.h`, `shellqml.h`, `contentqml.h`, `planningqml.h`, `ioqml.h`, `qrqml.h`).

| Area | C++ (main classes) | QML |
|---|---|---|
| `ui` — design-system helpers | `DatesHelper` (natural dates, formatting, recurrence, month grids), `TextHelper` (folding, fuzzy ranking, escaping), `DiffHelper` (line/word diffs, conflict hunks), `KeyboardInsets` (the on-screen keyboard's overlap of the window, one source of truth for the shell, the docked editing toolbar, sheets and toasts; its platform sources connected by the app's `keyboardsources.cpp`), `GestureInsets` (the system's gesture zones at the window's edges — Android's gesture navigation — for the drawer's edge swipe), `ThemePalette` (the colours the appearance settings change: surfaces, accent presets and custom accents made readable by WCAG contrast, text colours; the font pickers' family filter) in `brulion_ui_helpers`; `TouchFlick` (touch flicks at the finger's speed on Android) in `brulion_ui_touch`; `RowScroll` (the scroll bar of long lists from the rows at the view's edges, for `BRowScrollBar`) and `placedRow()` (which row of an item view a child of its content item shows — none for a delegate in ListView's reuse pool; the scroll probe's) in `brulion_ui_scroll`; `Icons` + `IconImageProvider` (`BIcon`'s pictures: Lucide's SVGs rasterised and recoloured as Qt's `IconImage` did, one image and texture per icon, size and colour through Qt Quick's pixmap cache; docs/UI.md "Design system in code") in `brulion_ui_icons`; `FilePicker` (the system's file picker for QML instead of QtQuick.Dialogs: the platform theme's dialog helper — Android's document picker, Windows' IFileDialog, GTK / KDE / XDG portal — or Qt Widgets' `QFileDialog` on desktops without one; docs/UI.md "File pickers") in `brulion_ui_files`; the QML engine's local-only network access (`LocalOnlyNetworkFactory`) in `brulion_ui_network`; `installFallbackFonts()` + the bundled emoji/symbol fonts (`assets/fonts`, `fallbackfonts.h`) in `brulion_ui_fonts`; `layoutDump()` (`--dump-layout`: what a window shows as JSON for the UI benchmark, `tests/app/uibench`; docs/UI.md "UI benchmark") in `brulion_ui_layoutdump`; app-only `DeviceInfo` (also the installed font families; `screenReader`: Qt accessibility active, or simulated), `TextHelper.hoveredLinkHtml` (the hover underline of rendered text), `DevTools` (dev/test options) | `qml/theme/Theme.qml`, `qml/components/B*.qml`, `qml/dev/Gallery.qml` |
| `editor` — block editor | `EditorHost` (contract with the shell — `assetUrl`, `drawingUrl` (a drawing's preview in the app's theme), `isDrawing`, … —; header-only, moc'ed once in `brulion_editorhost`, which also has `picturesize.h`: a picture's size from its header or an SVG's root, for the editor and the browser's data: URLs, whether the device decodes its format, and whether a file is a drawing — `localFileIsDrawing`, its name and first 4 KB: an SVG, a `.excalidraw`, an Obsidian Excalidraw plugin `.md` —, the size of a `.excalidraw` or plugin drawing's preview (`localDrawingSize`) and the plugin's export beside one (`drawingExportOf`) — the picture view and the embeds name one it cannot; the clipboard's `renderHtml` / `htmlToMarkdown`, implemented by `Workspace` in `workspaceclipboard.cpp` with core io), `QueryHost`, `PageController` (commands, undo — each step a `BlockDocument::Delta`, applied as an operation —, debounced saves — the autosave in the background through `EditorHost::writeIfUnchangedAsync`, one write in flight, `save()` waits for it ("Vault threading") —, external-change merge, note mode: outline / document; the block selection in `pagecontroller_selection.cpp`: anchor and focus uids, copy / cut / delete / indent / move / paste over it, clipboard writes with the size limit and read back; `LazyHtmlMimeData`: the copy's text/html rendered only when an application asks for it, at once on Android, in the browser and on Windows), `StreamSelection` (a block selection over several days of the journal stream: paths and rows, each attached day's part, copy with the days' links; the days not attached read from their files), `BlockListModel` (the selection as a uid set with a `selectionEdge` role), `MarkdownHighlighter` (also spell checking's underlines), `SpellCheck` (QML singleton: the core's `spell::SpellChecker` over the platform's backends on a worker thread of its own, verdicts cached per word, bound to the device's settings by main.cpp; docs/UI.md "Spell checking"), `RichTextPills` (the rounded shapes of a rendered block's tinted spans — chips, tags, inline code, highlights —: takes their square backgrounds off the read-only rich text's document and reports one rectangle per line segment for QML to draw; also the rendered text's fixed line height; for the lists' `BRichText` it sets the HTML itself and cuts it after `maximumLineCount` lines with "…"), `LinkPreview` (what a link's hover preview shows: `BlockListModel::linkPreview()`, the transclusion of the page, section or block a `page:` / `block:` link opens; follows the links clicked in it through the host), `RichTextLinks` (the anchors of a rendered text with their rectangles: the accessible link items of `BLinkTargets.qml` while a screen reader is on), `RenderedTextItem` (a block's rendered text drawn from its runs through the scene graph, Z10 stage A: `BRULION_TEXT_ITEM=1`), `JournalDayItem` (a journal day that is not edited drawn as one item — decorations, a `RenderedTextItem` per block — instead of its editor's rows, Z10 stage A2: `BRULION_JOURNAL_DAY_ITEM=1`; docs/UI.md "A journal day as one item"), `EditorHost::linkPart` (what a path part of a link opens; `linkPartRoute`: the route of a `part:` link), `SuggestionModel`, `QueryResultModel`, `EditorInput`, `SourceTextSync` (source mode's TextEdit hands `PageController` one edit at a time, not its whole text), `PageItem` (one item per page, Z10 stage C behind `BRULION_PAGE_ITEM=1`, docs/UI.md "One item per page": draws the text rows on screen with `RenderedTextLayout` — `RenderedTextItem`'s layout and drawing, shared — and hosts every other row as the editor's `BlockDelegate`; `PageRows` / `ScrollAnchor` / `RowHeightTree`, pure: the rows' heights in a Fenwick tree and the scroll position as a row and an offset, kept in step with the model's row signals), pure `blocktext.h`, `FakeEditorHost` (playground, tests) | `qml/editor/*` (BlockEditor — keys, the drag-selection tracker, the touch selection mode —, PageItemView — the PageItem over a Flickable —, delegates for calc/code/table/query/embeds, BlockMenu — every block's menu with Copy / Cut / Select and "Copy link" —, SelectionToolbar, TaskBox and TaskStatusMenu — the task checkbox and its status menu, used by every view that shows tasks —, MobileEditToolbar — docked on the keyboard at the bottom of the editor's `toolbarHost`, also the selection's tools —, dialogs), `qml/dev/EditorPlayground.qml`, `qml/dev/SelectionSelfTest.qml`, `qml/dev/PageScript.qml` |
| `shell` — app frame | `WorkspaceBackups` (automatic backups, `Workspace.backups`: schedule, folder, restore; "Data safety"), `WorkspaceExtensions` (`Workspace.extensions`: the Luau engine over a `NotebookHost` of the open notebook, the built-in extensions `AppSettings` switches on — the gym, the habits, the flashcards (their settings given with each call, `flashcardsDue` counted for the sidebar) —, the extension pages of the notebook — found when the index is ready and followed through `Workspace::onVaultChanged`, approved per device in the vault's `LocalStore`, loaded again alone when their page changes, the version before a failed load kept —, hooks (`save` from `Workspace::write`, `change` after a burst of changes), the Luau console (`ExtensionConsoleModel`), Settings' list (`ExtensionListModel`), their palette commands for `CommandModel` (`ext.<id>.<name>`, `extview.<id>.<name>`, `go.gym`, `go.habits`, `go.flashcards`, `flashcards.reviewPage`, `go.console`, `extensions.*`), the question a call waits on, messages as toasts (with a button: a result `{message, tone, button}`), the extensions' timers — armed on the platform, kept in the device state over a restart, late hooks for those that ended meanwhile; docs/EXTENSIONS.md) and `ExtensionViewModel` (QML `ExtensionView`: a view's node tree as list lines, a row's controls in a model of their own, actions and steppers — numbers typed in too —, the same view drawn again changed in place; docs/UI.md "Extension views"), `AppController` (lifecycle, vault, navigation history, side panel, permissions, dev options; `appshare.cpp`: `receiveShare` / `receiveShareQuery` / `pendingShare` / `saveShare` / `dismissShare` — a share from another app, the web share target or files opened with Brulion (`--open`, a second launch's `share` message; source "open": files without a type typed by `QMimeDatabase`, merged while the dialog waits, one that is a page of the open notebook opens as that page, copies saved and the files never changed) waits for a user notebook, goes to today's journal or new pages through `notebook/share.h`, its copies removed only inside `<dataDir>/share-in`), `AppSettings` (QSettings + owner-only secret file; the Nextcloud switch and server folder per notebook under the id of its folder — `Vault::idForPath`, as its device state —, the settings of 0.3.0 and older handed to the notebook that was current then or whose sync bookkeeping names their account and folder, `notebookOpened`), `WorkspaceGitHub` (`Workspace.github`: the open notebook kept in a folder of a GitHub repository — its link per notebook in AppSettings, the token in an owner-only file, `github::RepoSync` over a `NetworkHttpTransport`, pulls when the notebook opens and every 15 minutes in front, commits and proposals, the changes list, new notebooks from GitHub in `<app data>/GitHub`; its status is the sync seam's while Nextcloud is off; docs/UI.md "Notebooks from GitHub"), `Workspace` (owns `Vault` + `VaultIndex`; implements `EditorHost`/`QueryHost` — `runQuery` and `calendarItems` keep caches of the last 8 queries and 4 ranges, `runDataview` the last 8 Dataview results until the index changes or the minute turns — a query matching more than 5000 items is not kept —, `workspacequery.cpp` —; pages, history, trash, journals, quick capture (`notebook/capture.h`); folders and moves in `workspacefolders.cpp`: `movePath` / `renamePage` / `renameFolder` through one `relocate()` — the index's plan, `Vault::rename`, the planned notes written once each, the special folders of `.brulion/config.json` rebased —, `moveImpact` (kept per target until the
notebook changes) and `requestMoveImpact` (the dragged row's ghost: planned 8 ms per event-loop turn,
`moveImpactReady`), `folderPaths`, `folderInfo`, `subfolders`, `folderRenamed` for routes and the tree) with `WorkspaceReminders` (the scheduler over the index; snooze and a notification's or the missed banner's "Done" written into the note — the occurrence looked up again by id, the edit written compare-and-swap against the indexed content so a stale index never edits another line; notifications carry the notebook's id, one from another notebook is refused; Android's queued "Done" stays in its persisted queue until that notebook is indexed, `doneTarget`) and `WorkspaceSync` (+ `workspacesyncruns.cpp`: the throttled `live` progress, `history`, `runDetails`; a notebook whose server folder another notebook of the device syncs with — `NextcloudSettings::sharedWith` from `AppSettings::nextcloudFolderUser`, left by 0.3.1–0.3.5 — syncs nothing and says why), wired by `workspaceintegration.h`; journals, quick capture, pages, folders and moves delegate to the core module `notebook` (the Workspace keeps the translated messages, notices and signals); `RouteHistory` (`navigation.h`), `CommandModel`, `CleanupSelection` (CleanupView's shown and ticked files, so its QML compiles), `FolderTreeModel` (the sidebar's tree: lazy, applied as inserted and removed rows, Polish order, the journals folder pinned), the tray calendar's placement and reopen guard (`traypopup.h`, pure; `ShellTrayCalendar`), `DemoVault` (the sample notebook and the documentation notebook in the UI language, English or Polish: "Sample notebook" / "Przykładowy brulion", `sampleNotebookFolder()`; the separate notebook "Brulion Documentation" / "Dokumentacja Brulionu", `notebook::docsNotebookFolder()`, is made of Brulion notes in `shell/guide/{en,pl}/*.md`, embedded as resources and brought up to date on every open by `updateDocsVault`, which records the hash of every written page in the notebook's LocalStore and recognises 0.3.0–0.3.5 pages by their known hashes, so pages the user changed or deleted stay so; the documentation is never a recent or start notebook (the start drops it from the recent notebooks of 0.3.0–0.3.5) and arms no reminders — `Workspace::setRemindersPaused` keeps the user notebook's armed set; the sidebar button next to the notebook name switches notebooks, `openRecentVault` never recreates a missing folder), `DesktopTray` (`SingleInstance` is in `src/ipc`; `AppController::handleInstanceMessage` takes its messages — unknown verbs change nothing — and `openRoute` shows a route asked for from outside, brulion-cli `app open`, also once a notebook still opening is ready); dev: `ShellSelfTest`, `ShellJourney`, `RemoteControl` (`--remote-control`: AI agents drive the running app over a control socket, docs/CLI.md), all three on the UI driver (`uidriver.h`, `brulion_uidriver`: real input, item finding) | `qml/Main.qml`, `qml/shell/*` (AppShell, the tray calendar `ShellTrayCalendar`, sidebar with the tree's menus and drag and drop, top bar, bottom nav, route view, dialogs — `ShellMoveDialog` for "Move to…", `ShellShareDialog` for shares), `OnboardingView`, `SettingsView` |
| `notify` — notifier adapters (`brulion_notify`) | `Notifier` (a `reminders::NotificationSink` + signals; `createPlatformNotifier()`), `DesktopNotifier` + `DBusNotificationBus` (snooze/done/OK actions — `doneRequested` only for a task's reminder —, ringing alarms, tray fallback, sound hints) or, on Windows, `WindowsToastBus` (C++/WinRT toasts; the XML and the activation arguments in the pure `toastxml.h`, tested everywhere; a bus that `ringsItself()` per message: alarm toasts with a looping sound loop it and get one silent copy when the ring time is over, alarms with a short sound are sent again every ring interval as on Linux; available only while the AppUserModelID is registered and Windows does not turn Brulion's toasts off (an unknown setting before the first toast counts as allowed, `toastsAllowed()`); its own sounds — `ms-winsoundevent` names — and no sound files; toasts cleared at quit), `AndroidNotifier` + `androidpayload` (JSON for the bridge and its queued "Zrobione" requests, tested on the desktop), `WasmNotifierSink`; the ends of extensions' timers (`armTimer` / `cancelTimer`, apart from the reminders' set: Android arms an exact alarm in `TimerAlarms.kt`, the others arm nothing and the app shows the end; `WorkspaceExtensions::setTimerAlarms`); `ReminderSounds` + `soundchoice.h` (the sounds of reminders and alarms: device-local in AppSettings, per platform — Android ringtones on versioned notification channels, freedesktop theme sounds / files, the browser's tone); dev: the settings self-test (`notifyselftest.h`) | `qml/notify/ReminderSoundSetting.qml` (in `SettingsView`) |
| `content` — reading views | `WorkspaceListModel` base (diffed updates), `JournalListModel`, `BacklinksModel` (links and tags naming the page) / `TagBlocksModel` (`GroupedRefsModel`), `TagListModel`, `PageListModel`, `SearchResultModel` (notes and attachments merged, the query's `in:` filters), `HistoryModel`, `OutlineModel`, `PageTools` | `JournalsView`, `PageView`, `AllPagesView`, `TagsView`, `SearchView`, `TrashView`, `HistoryView`, `qml/content/*` (side panel, backlinks, dialogs) |
| `planning` — calendar, tasks, graph | `CalendarModel` (month/week/agenda; the range's entries from a `CalendarCache`, the overdue list from an `OverdueCache` — open tasks due before today per note, kept across changes), `TaskListModel` (sections, filters run by the query engine through a `QueryCache`; after a save the changed notes' tasks are merged into the sorted list), `ForceLayout` (Barnes–Hut) on a worker thread (`GraphSimulation`: one `QThread` per graph, pacing, newest-frame-wins; inline in slices on the GUI thread in the browser) + `GraphModel` (a mirror of the newest positions; drags apply at once) + `GraphItem` (zoom/pan/pinch; drawn through the scene graph — vertex-antialiased meshes in world units under a camera transform, `QSGTextNode` labels, `GraphSceneNode` — or with QPainter into an image node under the software renderer), `PlanningActions` (task toggles with undo, journal appends), `PlanningContext` | `CalendarView`, `TasksView`, `GraphView`, `qml/planning/*` |
| `background` — Android's background check of the notes (`brulion_background`) | `Target` (the notebook whose reminders are armed, its ring time, language, interval and Nextcloud folder — JSON the Kotlin side keeps; never the password), `upcomingFor()` (the very set `ReminderScheduler` hands Android, so the alarms' ids match), `Check` (opens the notebook as brulion-cli does, one Nextcloud sync with the app's engine and bookkeeping — `SyncEngine`'s store lock keeps the two processes from syncing at once —, reads the reminders; an unreadable or empty folder arms nothing); built and tested everywhere. Android only: `runBackgroundCheck()` (`backgroundentry.h`), the `main()` of the process `:background` that `BackgroundCheckService.kt` (a `QtService`) starts with `--background-check` — no QGuiApplication, no QML; `BackgroundCheckJob.kt` (JobScheduler, in the app's process) arms the result | — (Kotlin: `android/src/…/BackgroundCheck*.kt`; Settings → Przypomnienia in `SettingsView`) |
| `widgets` — Android home-screen widgets (`brulion_widgets`) | `buildSnapshot()` (`widgetsnapshot.h`: TasksView's open tasks with their raw dates and every reminder of their block — filed by `TaskListModel::filingDate`/`nextReminder`, which Kotlin mirrors for a later day —, CalendarModel's agenda, favourite and recent pages, where quick add writes; plain JSON, built and tested on every platform), `widgetactions.h` (the lines of quick-add entries, the queued ones the app writes); Android only: `WidgetPublisher` (created by main.cpp: publishes the snapshot of the notebook whose reminders run — debounced, when the app leaves the front, at midnight, empty on onboarding; it keeps the snapshot's caches, `SnapshotCaches`, across publishes — and writes queued entries into today's journal, putting back those it cannot write). The only Kotlin write into a notebook is the widgets' quick add with the app closed (`JournalAppend.kt`: the same line and append rules, checked against the same golden files as the C++; atomic, only into an existing notebook folder and inside it, compare-before-rename; `Workspace` likewise never appends to a daily note that is not UTF-8) — waiting for Qt to start would take seconds and show the UI, and the note changes like a Syncthing change would | — (Kotlin: `android/src/…/Widget*.kt`, `QuickAddActivity.kt`) |
| `io` — sync center, import/export (`brulion_ioviews`) | `ImportController` (also the CSV mapping: `csvOptions` → `io::analyzeCsv` on the dry run's thread → `csvMapping` for the mapping card), `TokenImport` (Todoist and Airtable with a personal token, base and tables to choose), `ImportAttachments` (a CSV's attachment cells downloaded into the input), `MsTodoImport` (the Microsoft To Do and OneNote cards' sign-in — OneNote with `Notes.Read` and the notebooks to choose —: client id and authority in `AppSettings`, core `mstodo`, the bundle handed to `ImportController::setInput`; other input reaching the controller cancels the sign-in), `ExportController`, `PublishController` (the Publish tab: targets, items, the dry run, publishing — core `publish`), `FileGatherer` (listing and reading off the GUI thread) / `DocumentProvider` (Android `content://` documents and document trees) / `writeToUrl` (`fileexchange.h`), `NextcloudSetup`, `SyncthingSetup`, `ConflictResolver` (merge3 hunks, base from History; drawings: both versions and their union — `drawing::merge` with no common version — for the previews, "Scal rysunki" through `WorkspaceSync::resolveConflictBytes`; an Obsidian plugin file's notes around the scene must survive — `drawing::keepsMarkdownOf` — or the copy stays); dev: the import self-test (`ioselftest.h`) | `SyncView` (live progress, the history of runs and their details: `SyncLiveProgress`, `SyncHistoryList`, `SyncRunDetails`), `ImportExportView`, `qml/io/*` |
| `drawing` — the drawing editor (`brulion_drawing`) | `DrawingCanvas` (`QQuickPaintedItem`: shows a core `drawing::Drawing`; stylus pressure and its eraser end from the window's tablet events — the canvas takes them when it is the topmost item under the pen —, mouse, fingers — drawing or moving, a pinch —, the wheel and touchpad pinches; pen, highlighter, element eraser, and in `drawingcanvas_tools.cpp` the selection — lasso or tap, moving, `deleteSelection`, `duplicateSelection`, `recolorSelection`, `reorderSelection`, `nudgeSelection` —, the shapes and text — `textEditRequested` / `commitText` with QML's editor —; the paper's pattern, its light or dark (`theme`, `appDark` from QML's `Theme.dark`, `dark`), undo / redo of core `ElementDiff`s, one per gesture or action; `load` / `fileBytes` — the file in its own format, `format` `svg` | `excalidraw` | `excalidrawMd`, core `drawing::writeDrawingFile` — / `svg` / `takeExternal` — a version written elsewhere merged with `drawing::merge`, a gesture in progress kept, a plugin file's Markdown theirs —; `interactive: false` — a preview, the sync center's: no input, the whole drawing in view —; palm rejection while the pen is down; every element prepared once by version, nonce and `updated` (a drawing's hatch lines within `drawing::DrawingHatchLines`), a cached image of the view rendered on the GUI thread, `paint()` drawing a snapshot taken in `updatePaintNode()`), `drawingrender.h` (the paper and the elements in the light or the dark theme — `paintPaper`, `prepare` / `paintElement`, `renderDrawing` —, the fonts of text `setTextFonts` / `textMetrics`, Excalidraw's dark filter per pixel `darkImage`; shared by the canvas and the previews), `DrawingImageProvider` (the QML image provider `drawing`, registered in `main.cpp`: `image://drawing/<path>?v=…&dark=…` — the previews of drawings in notes, the picture view and the editor's notice, `EditorHost::drawingUrl` / `Workspace::drawingUrl`; stateless, in Qt Quick's image reader thread; a drawing in the frame its file names, as the embeds are sized; Qt SVG + `darkSvg`'s colours for files Brulion does not draw; `.excalidraw` and Obsidian Excalidraw plugin files too — one Brulion does not draw by the plugin's export beside it or its elements with dashed boxes; a backdrop decoded at the size shown and turned dark with the drawing), the backdrop (`setBackdrop` / `removeBackdrop` on the canvas — one undo step each, drawn "document dark" through `darkImage`, a Dark drawing's file with that dark raster —; `backdropimage.h`: `prepareBackdropPicture` / `prepareBackdropPdfPage` — the raster stored, a PNG or JPEG kept as it is or encoded the smaller of PNG and JPEG, at most 2400 px and 4 MB —; `DrawingBackdrops` — QML's singleton: `pdfAvailable`, `pdfInfo` (with the first page's size), `pageUrl`, `prepare` / `cancel` (jobs on Qt's thread pool, inline in the browser; with `drawings` the new drawings' files are written there too — `toDrawingVariant`, core `newDrawingOnBackdrop` —, and inserting 60 slides only stores them on the GUI thread), `pickInBrowser`, vault paths through a resolver and an importer `main.cpp` sets —; the canvas keeps decoded only the pictures its elements show; `PdfPageImageProvider` — the image provider `pdfpage` of the page picker's thumbnails; PDF pages through `brulion_platform`'s `pdfpages.h`) | `qml/drawing/DrawingView.qml` (route `drawing`: the tools, autosave through `Workspace.readAttachment` / `writeAttachment` — the file read before each save, a change not reported yet taken in —, external changes — one it cannot edit shown, the strokes drawn here kept beside it, `Workspace.createAttachment` —, renames, the notices of files it does not edit), `DrawingToolbar`, `DrawingSelectionBar` (the selection's colours, duplicate, front, back, delete), `DrawingTextEditor` (text and labels written on the paper), `DrawingBackdropDialog` (a PDF's pages as backdrops: thumbnails, "Strony", progress — also the note's "Rysunek na obrazie lub PDF…", `EditorDialogs.pickDrawingBackdrop`); `drawingqml.h` registers `DrawingCanvas`, the `DrawingFonts` and `DrawingBackdrops` singletons |
| `canvas` — the canvas view (`brulion_canvasview`) | `CanvasEditor` (`QQuickItem` under the cards: the core `canvas::Canvas`, undo as snapshots, the view — `zoom`, `panX` / `panY`, fit, focus a card or an index line, animated —, the selection, every gesture in `canvaseditor_input.cpp` — mouse, touch, wheel, a touchpad's pinch: select, rubber band, move with groups' cards, resize, connect from side handles, pan, pinch —, the clipboard, `load` / `fileBytes` / `markSaved` / `takeExternal` — merged by `canvas::merge` —, `exportPicture` — SVG, PNG through Qt SVG), `CanvasNodeModel` / `CanvasEdgeModel` (only the cards and edges in or near the view, rows changed in place), `CanvasCards` / `CanvasCardContent` (what cards show: a text card's Markdown — `BlockListModel::textPreview` — and a note's page or section — `linkPreview` —, picture and drawing URLs, PDF thumbnails, typed rows for QML; `openNode`, `openLink`), `CanvasColors` (QML singleton: canvasColor → hex), `CanvasImageProvider` (image provider `canvas`: a canvas file's picture) | `qml/canvas/CanvasView.qml` (route `canvas`), `CanvasCard`, `CanvasCardRows`, `CanvasEdge`, `CanvasEdgeLabel`, `CanvasToolbar`, `CanvasSelectionBar`, `CanvasTextEditor`, `CanvasTextDialog` |
| `attachments` — the text of PDFs and pictures for the search (`brulion_attachments`) | `AttachmentIndexer` (owned by the shell's `Workspace`, QML `Workspace.attachments`: one worker thread at the lowest priority reading the vault's PDFs — their text layer through `PdfReaderPool` leases, 16 pages a lease — and pictures, then OCR page by page (`platform/ocr.h`); waits while paused or while a Nextcloud sync holds the PDFs (`PdfReaderPool::idleSuspended`); the device's cache `<app cache>/attachment-text` — records by content hash, the notebook's manifest —, pruned to 512 MiB; follows `Vault::changed` — a rename moves the text without reading, a change reads again —; merges into core `AttachmentSearch` on the GUI thread 24 pages a turn; `search()` with excerpts from the cache, `page()`, `pageOfFile()` / `pageLookup()` for the PDF viewer's search on scanned pages — `PdfViewModel::setOcrLookup`, set by `Workspace` —, `matchRects()` / `recognizedText()` for the picture view; state, progress, pause, the settings `AppController` hands over from `AppSettings`); `isSearchableAttachment`, `unorientedBox` (an EXIF-oriented picture's box back on its file's pixels), `ocrPagePixels` (~300 dpi); `attachqml.h` registers the type | `qml/attachments/AttachmentSettings.qml` (Settings → Search); the search view's attachment rows and progress line (`SearchView`), the picture view's highlights and recognized text (`ContentPictureView`) |
| `pdf` — the PDF viewer (`brulion_pdfview`) | `PdfRenderPool` (at most two threads: visible pages, detail pictures, thumbnails, page sizes, search — in that order), `PdfReaderPool` (open `platform::PdfReader`s leased to one thread at a time, two idle a file and four in all, closed after 10 s idle and when the file changes), `PdfThumbnails` (the thumbnails of embedded PDFs as PNG files in the device's cache — key: path, size, mtime, page, pixel size; text chunks with the page count and size; failures as `.err`; pruned to 64 MiB, 32 on Android — owned by the shell's `Workspace`: `EditorHost::pdfThumbnail`), `PdfPageLayout` (pure: every page's exact rectangle, fit width, zoom anchors), `PdfViewModel` (QML `PdfViewModel`: opening off the GUI thread, the pages near the view as `PdfVisiblePages`, zoom, the current page, base pictures of at most a screenful of pixels and detail pictures when zoomed in, search in batches), `PdfViewImageProvider` (the image provider `pdfview`: asynchronous, cancellable, registered in `main.cpp` where a renderer exists); `pdfqml.h` registers the model | `qml/content/ContentPdfView.qml` (route `page` with a PDF's path: the bar, zoom, page jump, search, "Open in another app", "Draw on this page", the fallback panel without a renderer), `qml/editor/PdfEmbedTile.qml` (an embed's thumbnail) |
| `media` — audio, video and voice notes (`brulion_media`) | `MediaPlayback` (QML singleton: the app's one player — one file at a time, where each was left, time fragments and their end, the surfaces of a video — the last one attached wins, a video pauses unseen —, the views count of the now-playing bar, `info()`: a file's length, picture and first frame read by a `MediaProber` in the background, the poster kept as PNG in the cache), `MediaEngine` / `MediaProber` / `decodeAudio()` (`mediaengine.h`: Qt Multimedia — `mediaengine_qt.cpp`, `BRULION_MULTIMEDIA` — or nothing — `mediaengine_none.cpp`: WAV only), `VoiceRecorder` (QML type: the microphone's permission, QAudioSource's samples — the meter, the time — into QMediaRecorder through QAudioBufferInput (M4A, else Ogg), the system's own recorder where the backend has no buffer input, else WAV; pause, cancel, `BRULION_TEST_MICROPHONE`), `Transcriber` (`AppController.speech`: the Whisper models' files, the settings, the queue on a worker thread, the transcript placed by the shell) | `MediaEmbedTile`, `MediaControls`, `MediaSlider`, `MediaEmbedMenu`, `MediaFullscreen`, `MediaVideoSurface` (`qml/media/video/` with Qt Multimedia's `VideoOutput`, else the stand-in in `qml/media/novideo/`), `MediaNowPlaying` (AppShell), `VoiceRecorderDialog` (the editor, AppShell), `MediaSpeechSettings` (SettingsView), `ContentMediaView` (PageView) |
| `diag` — the event log (`brulion_diag`) | `LogController` (QML singleton `Logs`: copy, share, save, clear, the export header, the detailed mode), `LogModel` (the Logger's entries filtered by area, level, diacritic-insensitive search and a time window, following new ones), `logexport.h` (header, file name, clipboard cap) | `qml/diag/LogsView.qml` (route `logs`), `LogRow` |
| `qr` — QR codes (`brulion_qrview`) | `QrShare` (the dialog's text: core `qr::noteText` of a note's or the selected blocks' Markdown — as written or plain text, the title first or not —, cut into codes by `qr::splitText`, at most `qr::MaxCodes`; the code shown, `next()` / `previous()`; `state` "ok" / "empty" / "tooLong"; `((id))` references through its `EditorHost`), `QrCodeView` (`QQuickPaintedItem`: one code black on white with its quiet zone, whole device pixels a module, centred; `picture()` for tests) | `QrCodeDialog` (opened by the editor's `EditorDialogs.showQrCode`: the page menu, the top bar's More, the palette's `page.qrCode`, a block's menu, the selection's More) |

Routes and views: docs/UI.md "Navigation". Area tests: `tests/app/<area>/` (C++ unit tests plus
QML smoke tests and screenshot scripts that run the real app headless).

## Command line (`src/cli`) and IPC (`src/ipc`) — desktop only

* **`brulion-cli`** (docs/CLI.md): headless commands over a notebook folder for scripts and AI
  agents — pages, folders, journals, capture, tasks, reminders, query, search, tags, backlinks,
  calendar, dates, calculations, block links, history, web clips —, JSON envelopes and stable exit codes,
  `brulion-cli mcp` (MCP over stdio, tools only, the index kept warm), `brulion-cli mcp serve`
  (the same tools over Streamable HTTP for the providers' apps: a small HTTP/1.1 server of its own
  over `QTcpServer`/`QSslServer`, an OAuth 2.1 authorization server with sign-in and consent
  pages, a state file of hashed tokens — docs/CLI.md "Remote MCP server"), the Web Clipper's native
  messaging host (`clip host`, also started by a browser with its own arguments — `run()` and the
  app's `main()` recognise them — and `clip install` for the browsers' host manifests;
  docs/CLIPPER.md). One registry
  (`registry.cpp`) gives the argv parser, the help, the JSON Schema and the MCP tools. Built on
  `core/notebook` (never on the app's shell): it never creates a `ReminderScheduler`,
  `WorkspaceSync` or `AppSettings`, reads the app's settings file only for the current notebook
  and language, and shares the device state (History, LocalStore) with the app on one computer.
  Writes: the device's lock, compare-and-swap with retries, History reason "cli" (the app's
  history shows "Replaced from the command line (agent)"). The app's `main()` hands `brulion cli …`
  over before anything else (the AppImage's one entry point). CMake `BRULION_BUILD_APP=OFF` builds
  only the core and the command line (no Qt Quick/QML/SVG): the Nix package `brulion-cli`.
* **`brulion_ipc`**: `SingleInstance` (one app per session over a `QLocalServer` in
  `$XDG_RUNTIME_DIR`, a lock file decides the primary; only in a folder of the user's own —
  `socketFolderProblem()`, never the shared temp folder: without one the app runs without a
  single instance and the event log says why — and both ends talk only to this user's processes,
  `trustedPeer()` of the peer's `SO_PEERCRED`; on Windows a named pipe with an access list of the
  user, its key and lock file in `%TEMP%`, the peer process's SID compared) and the messages of
  its socket
  (`appmessage.h`: `activate`, `capture`, `today`, `open {"route", "vault"}`, `uri {"uri"}` (a
  brulion:// address of a second launch or `brulion-cli uri open`: docs/UI.md "Automation"), `hidden` (a second
  launch with `--hidden`, e.g. Windows' start at sign-in while Brulion runs: no window), `share
  {"share"}` (a second launch with `--open <file>…`: the files as share JSON, absolute paths; the
  receiver drops its "dir") — one-way lines a second launch or `brulion-cli app open` sends; a
  second launch adds its `activationToken`, the launcher's `XDG_ACTIVATION_TOKEN`, which the app
  presents when it raises its window on Wayland); the **remote control** of the running app
  (`controlprotocol.h`: JSON-line requests and responses, the methods and their parameters;
  `controlserver.h`: a second socket, `<key>-control`, owner-only, other users' connections
  refused; `controlclient.h` for `brulion-cli app …`); `profile.h`: the app's names shared by the
  app and brulion-cli, `--profile <name>` / `BRULION_PROFILE=<name>` for a separate profile (test runs on Windows;
  a self-test, journey or screenshot run without a home of its own gets "test": `chooseProfile()`,
  docs/CLI.md "Profiles"). The remote control is off unless `--remote-control` or the device
  setting `devtools/remoteControl`; the app's side is `src/app/shell/remotecontrol.h`, its input
  and item finding the UI driver `src/app/shell/uidriver.h` (library `brulion_uidriver`), shared
  with the shell self-test and the journey. Nothing nested: gestures play from timers, waits
  poll; see docs/CLI.md "Remote control".
* **Concurrent writers** (the app, the command line, Syncthing, Nextcloud): every write is atomic
  and snapshots what it replaces; the editor's saves and the command line's edits are
  compare-and-swap (`Vault::writeTextIfUnchanged`): a change another program wrote a moment ago is
  merged (editor) or the intent applied again (command line), never overwritten.

## Logging (the event log)

Users see it as **"Event log"** (route `logs`; in Polish "Rejestr zdarzeń" — not "Dziennik",
which is the journal). It answers "what is the app / the sync doing, and did something fail?"
and is what a user sends us.

* **Writing log lines:** Qt's own macros with the categories of `log/categories.h` —
  `qCInfo(brulion::logging::lcSync) << …`: `brulion.app`, `.sync`, `.net`, `.vault`, `.index`,
  `.reminders`, `.io`, `.platform` (older `brulion.workspace`, `brulion.notify.*`,
  `brulion.android` map to the same UI groups). Levels: debug = detail (only in the detailed
  mode), info = what happened, warning = failed but Brulion copes or retries, critical = needs
  the user. Log lines are English and technical (grep-able); the UI around them is translated.
  Sync lines start with the run number (`#42`); one sync run writes 2–8 lines in the normal
  mode (start, warnings, one summary line with phase timings and request statistics), the
  detailed mode adds a line per phase and per HTTP request (method, path relative to the
  WebDAV folder, status, ms, bytes — never headers or bodies).
* **Never logged:** passwords, app passwords, tokens, cookies, note contents, request or
  response bodies. Paths and titles of notes, the server address and the Nextcloud user may
  appear (the export header says so).
* **Capture:** `main.cpp` installs `logging::Logger` right after the single-instance check: a
  chained `qInstallMessageHandler` takes every Qt message (`brulion.*`, Qt's `qt.*`, QML
  warnings, `console.*`), **redacts** it, collapses identical repeats ("×37"), rate-limits
  debug/info bursts per category (with a "dropped N lines" note), keeps the newest 5000 entries
  in a ring and appends them to `brulion.log` (rotating to `brulion.log.1` at 1 MB, 3 MB in the
  detailed mode; owner-only files in `platform::logDirectory()`; the browser keeps the log in
  memory — its file system is IndexedDB). stderr / logcat keep receiving everything (redacted).
  Session start/end lines reveal a session the system killed. No timer runs while nothing is
  logged; `appended()` is coalesced to ≤ 4 per second. Developer windows (gallery, editor
  playground) log to memory only; test and screenshot scripts give the app a private
  `XDG_STATE_HOME`.
* **Redaction** (`log/redact.h`) applies to every message and to every sync error that is
  stored or shown: registered live secrets (`ScopedSecret`: the app password in `AppSettings`,
  the password and the Basic token in each `WebDavClient`, the Syncthing API key) in plain,
  percent-encoded and Base64 form; `Authorization` / `Cookie` / API-key header values;
  `Basic …` / `Bearer …`; `user:password@` in URLs; secret-like query parameters and
  `key=value` / `"key": "value"` pairs; Nextcloud app passwords; JWTs. Tests check end to end
  that a secret never reaches the ring, the files, the export, stderr, the sync history or the
  UI (`tst_log_*`, `tst_sync_secrets`, `tst_diag_logexport`, `tst_shell_workspace_sync`, the
  shell self-test).
* **Verbosity:** normal / detailed (`AppSettings` key `diagnostics/logVerbosity`; the switch on
  the log page and in Settings → Application) through `QLoggingCategory::setFilterRules` on
  the named `brulion.*` categories; `QT_LOGGING_RULES` still wins for developers.
* **Sync history** (`sync::SyncHistory`, `<device state>/sync-history.json`, never in the sync
  bookkeeping store): the last 20 runs with trigger, outcome, error code, phases, statistics
  and per-file errors (quiet runs folded); `WorkspaceSync.history` / `runDetails()` for the sync
  center, `historyLines()` for the export header.

## Vault threading

A `Vault` is used from its owner's thread, with one exception: an editor's autosave (Zed's save,
docs/PERFORMANCE.md "The save on a writer thread"). `Vault::writeTextIfUnchangedAsync()` takes the
text (an implicitly shared `QString`, O(1)) and what the vault knows of the file — its stat and
hash, the folder, whether a watcher batch for it is queued, the History reason and time — into a
`WriteJob`, and hands the job to the writer `Vault::setWriteExecutor()` gave it: the app's
`Workspace` passes a `QThreadPool` of one thread at normal priority per notebook (not the index
pool, busy at low priority while indexing), so writes stay in order; WebAssembly and
`BRULION_INDEX_THREADS=0` give none and the same job runs at once. On the writer
(`WriteJob::run()`, no vault state): the file's stat must still be the known one (else
*Fallback*), its exact bytes are read and their text compared with `expected` (*Changed*: nothing
written), the hash checked against the known one, the BOM kept, equal bytes are *Unchanged*;
otherwise the old bytes are snapshotted into History (`History` is thread-safe: a mutex in every
public function, the snapshot's time taken on the owner's thread), the bytes written atomically
with fsync (`saveAtomicallyStamped`) — right before the rename the file must still be what was
read (its `ReadMark`: stat and fine time from before the read, the bytes read again where the
stamps cannot vouch; else *Changed*, nothing written) —, the file and folder stamped, the bytes hashed, and the
caller's `afterWrite` runs over the text written (the app's `VaultIndex::prepare()`). The job then
posts its merge to the vault's thread (before it counts as finished, so a waiting vault is still
there). `mergeWrite()` on the owner's thread: *Written* — `trackFile`, `rememberContent`, the
folder's stamp for the echo (`noteOwnWrite`, after the file is tracked), the file's watch;
*Changed* — the other program's change taken in (External, kept in History); *Fallback* — the
synchronous `writeTextIfUnchanged()`, which knows every rule; *Failed* — the error. Then the
caller's `done` (before `changed()`, so an editor knows its new merge base when its own write is
reported) and `changed()` with `reportingWrite()` = the job's ticket (the Workspace's index takes
the note prepared on the writer — `updatePrepared`, no read — for that ticket only).

Rules that keep data safe:

* Every other vault call that changes files (every write, `create*`, `rename`, `remove*`,
  `restore`, `emptyTrash`, `saveConfig`, `rescan`, `processPendingEvents`, `open`,
  `setFollowsOutsideLinks`) first waits for the writes in flight, merges and reports them
  (`waitForWrites()`, oldest first; a ticket's merge happens once). A write never lands after a
  rename or a deletion of its file, the trash holds the newest text, and a save over the text in
  flight compares with what is on disk.
* Reads and queries (`readText`, `readBinary`, `adoptRead`; `stat()` and `exists()` of a path being
  written) wait and merge too — they see the write — but its reports (`done`, `changed()`) come
  from the event loop (`settleWrites()`), never inside the caller: a dataview query that stats
  files while it holds the index's notes, or a model building a row that reads an embedded page,
  sees no index or editor change under it. The next call that changes files reports them first,
  in order; `hasWritesInFlight()` stays true until they are reported. `files()` and the other
  queries answer what the vault knew until the merge.
* A watcher batch waits while a write is in flight (its echo — the temporary file, the rename — is
  the vault's own and is recognised once merged), at most 2 s, then for the write.
* `open()` waits and counts a generation up (a result of an older generation is dropped); the
  destructor waits for the writer and reports nothing — `Workspace::close()` waits first, so the
  result is reported while everything still listens.
* One write in flight per editor: `PageController::saveInBackground()` (the save timer) sends the
  text and the merge base; until `done` `savedText` stays the file's text, edits made meanwhile keep
  the page dirty and are sent next (over the text written); `save()` — navigation, another host,
  the destructor, quitting, Android's pause, the shell's `saveOpenEditorsRequested`, `savedLineAt`,
  QML's `page.save()` — waits for it (`EditorHost::waitForWrites()`) and then writes synchronously;
  a result of an older generation (another page loaded since) is ignored; a *Changed* result goes
  to `onFileChanged` (merge or conflict) as before and the merged text is sent again; a failure
  leaves the page dirty. The echo of the own write is recognised without reading the page
  (`EditorHost::holdsText` → `Vault::holdsText`: the very string written, nothing seen since).
  The shell's "save" hooks of extensions run before the write is started, as for the synchronous
  save.

## Data safety

Notes may come from anywhere (imports, sync, other editors) and storage may disappear under the
app; these guards keep data from being lost:

* **Vault storage checks** (`Vault::scanStatus()`, `isStorageAvailable()`): a missing vault
  folder (unmounted SD card), a folder that now lies on another storage than at `open()` (an
  empty mount point) or folders that cannot be listed are never turned into deletions; writes
  fail while the folder is missing or on another storage, so nothing lands on an empty mount
  point. Files changed on disk behind the app are taken in (and kept in History) before an
  overwrite; every overwrite is atomic (a temporary file in the same folder, synced, renamed over
  the file; `QSaveFile` where the C library has no fsync()) and snapshotted into History first —
  also for an editor's save on the writer thread ("Vault threading").
* **Compare-and-swap of a save** (`writeTextIfUnchanged`, `writeTextIfUnchangedAsync`): the
  exact bytes decide, and right before the rename the file is checked again against what was read
  (`vaultutil::ReadMark`, `stillAsRead`): another program's write while the data was written and
  synced, or a file created meanwhile, is taken in (*Changed*), never renamed over. Equal stamps
  prove nothing on coarse storage (FAT's 2 s, exFAT, 1 s mounts, Android's providers) nor for a
  file written just before the read (`stampsVouch`): then the file's bytes are read again (O(the
  note), never the notebook). After a write of its own, another program's or a read on such
  storage, a file is *unsettled* for a tick (`noteUnsettled`, 3 s): listings compare its content,
  since a same-size write within the tick keeps its stat.
* **The echo of an own save** (`noteOwnWrite`, `processPendingEvents`): the folder is not listed
  when its stamp shows that only the save's own steps changed it — the vault knew the folder at
  the stamp before the save, the stamp stayed while the data was written and synced, and each own
  step was one system call right after a look at it. That proves nothing where the file system
  stamps changes with its clock's tick (NTFS: the system time, 0.5–15.6 ms; Linux before 6.13 and
  Android: the kernel's tick, 1–10 ms): another program's change in the tick of the temporary file
  keeps the stamp the temporary file gave it, and the fsync between the steps mostly lets a tick
  pass before the rename, so the save's own stamps rarely show such a clock (a Windows CI run of
  `foreignChangeWhileSaving` missed a created note, 2026-11-19). So the first own save into a
  folder (while watching) probes the folder's clock (`vaultutil::probeFolderClock`, part of the
  save's steps): four own changes of its entries, each right after a look, all within 0.4 ms — less
  than any clock's tick, so a tick-stamped folder must keep the stamp at least once. Only a folder
  found *Fine* (multigrain times: Linux 6.13 and later on ext4, XFS, Btrfs, tmpfs) trusts the echo;
  a *Coarse* or *Unknown* verdict anywhere, or two own steps that kept a stamp, and every echo is
  listed from then on. Per folder, not per notebook: a folder mounted or linked inside
  may be on another file system.
* **Leftovers of interrupted writes** (`.brulion-tmp-…` when a crash, a kill or a power cut comes
  between the temporary file and the rename): ignored everywhere (`paths::isIgnored`: listing,
  watcher, sync, export, backups, Clean up, index), removed when the vault opens
  (`vaultutil::removeLeftoverTemp`, also History's) — only regular files with our own names (never
  a case-only rename's shorter name, which holds a note), never links or folders, only a day after
  their newest time, so another instance's or a sync client's file in flight stays.
* **Links and sizes in the vault** (`src/core/vault/vault.h`, "Semantics"): a symlink to a file is
  never followed, wherever it points — not listed, read or written, so a link planted by a sync peer
  or a checkout (`x.md → ~/.ssh/id_rsa`) is neither indexed nor uploaded nor written through;
  folder links only when they point outside the vault and the device allows it
  (`Vault::setFollowsOutsideLinks`, off by default; Settings → Notebook, `AppSettings.followsOutsideLinks`,
  device-local — never `.brulion/config.json`, which a sync peer could change; brulion-cli and
  the background check read it), so a planted `assets → ~/Documents` makes no notes either. Devices
  that followed such links before keep them, so their sync does not take the files for deleted: while
  the setting was never chosen, a notebook's first open checks once and switches it on only when this
  device knew the notebook before (state in its device folder, or a recent notebook) and it has such
  a link — a new device or a link planted after the check never does (`AppSettings::checkOutsideLinksOnce`,
  before the index and sync see the vault; a background target of an older version keeps following
  them). What is not followed is not the vault's: nothing in it is read, written, moved or deleted
  (`Vault::reaches()`, also for pictures, PDFs and drawings read by their file), and no trash
  operation or `rename()` goes through a link inside `.trash/`. Every read is
  bounded whatever size the file reports
  (`/proc` files report none): scans read text files up to 16 MB, `readText()` / `readBinary()`
  up to 1 GiB.
* **Sync guards** (`SyncEngine`): a **mass-deletion guard** holds back every deletion of a run
  that would delete more than 25 % and at least 10 files of one side (or all of them) until the
  user confirms the list in the sync center (`needsConfirmation`, `pendingDeletions`); a side
  on which every previously synced file vanished is never mirrored (`LocalEmpty`/`RemoteEmpty`);
  no run while the vault storage is unavailable (`VaultUnavailable`). Moves are not deletions:
  a run transfers the paths without deletions first, and a deleted file whose content was
  uploaded or downloaded under a new path in the same run (matched one to one by content hash,
  by a version a server-side move kept, or as the same name in another letter case) counts for
  neither guard — moving a big folder never asks, a real mass deletion still does. A side that
  lost every synced file passes only when more than half of them are found under new paths (a
  notebook kept in one renamed folder); on that side any other deletion waits for confirmation,
  however few, and a folder is moved only when none of its files changed — an older copy put back
  after the storage was lost never overwrites or deletes the newer notes without the user. Folders
  are deleted only when they hold nothing: a local
  one with `rmdir()` after an empty-on-disk check (ignored and hidden files count; never while the
  server's folder of another letter case is the same folder on disk — `std::filesystem::equivalent`,
  case-insensitive storage), a server one
  (WebDAV deletes recursively) only when the run's listing and a fresh Depth-1 listing right
  before the DELETE show nothing in it (an entry the client cannot name as a child fails that
  listing: the folder stays); while deletions are held back, no folder is deleted
  either. A folder renamed in the vault is moved with `MOVE` and `Overwrite: F` (never over an
  existing name; any refusal falls back to per-file upload and deletion), only when it is a
  rename (the old folder missing here, still on the server; at least half of it proven below a
  new folder nothing of which was ever synced — a file unchanged there or recorded by the rename
  journal; the evidence of a folder is counted by its parts, each file directly in it and each
  subfolder as one, so a big subfolder moved elsewhere on its own never takes its parent along);
  an interrupted move is
  recognised next time (identical files on both sides are only recorded). A synced folder
  another device renamed in letter case only, which on case-insensitive storage is the same
  folder on disk under the server's new name, is renamed here too before anything is compared and
  its states follow (`adoptCaseOnlyFolderRenames()`): its notes are never trashed under the old
  name while being downloaded under the new one. Any other folder renamed or moved on the server
  (another device's MOVE, the Nextcloud web UI or desktop client) is renamed here too after the
  empty-side guard (`adoptRemoteFolderMoves()`): a synced folder still here and gone from the
  server of which at least half (counted by parts as above) is the server's very files (the same
  `oc:fileid`; without ids the same real ETag and size) at their places below a new server folder
  of which nothing was ever synced and nothing exists here in any letter case; when the server
  looked empty, every file found there must be proven. Then single files the same way
  (`adoptRemoteFileMoves()`: one new server file with the synced file's id — or, without ids and
  unless the server looked empty, the one file with its ETag and size, an ETag no other synced
  file had: servers whose ETags are mtime and size repeat them). A synced name the server now
  fills with another file (both ids known and different: a file renamed onto it, or a new note
  made under the old name of a renamed one) counts as gone there and as a new server file, so a
  chain (Y → Z, then X → Y) is renamed here from its end and an edit made here stays with its
  page instead of being merged into the other one; a cycle (a swap) is compared as before. It is `Vault::rename`
  with `ChangeSource::Sync` (history, favourites, folds and open editors follow the `Renamed`
  changes; links are not rewritten — the renaming device did that, and its edits arrive as
  downloads), the states and merge bases follow, and the files are then compared at their new
  paths as usual (edits on either side merge there). Something at the new name here, a refused
  rename (a file held open on Windows, a read-only folder) or no proof for the folder: the files
  proven one by one are renamed singly, the others travel as before (downloaded under the new
  name, the old ones trashed). A page renamed in the vault is moved with one `MOVE` per file after
  the folder moves, so the other devices see the rename too: proven by the rename journal
  (`SyncEngine` records every `Renamed` change of a synced file that is not the sync's own or a
  rescan's guess — `ChangeSource::Sync`, `External` — as `sync/renamed/<new path>` → the synced
  path, chains followed to their start, dropped on deletion, once the new path is synced and by
  `resetState()`; a folder rename of the same run, here or adopted from the server, carries the
  journal along, `rebaseJournal()`), so text typed or links rewritten after the rename are
  uploaded after the MOVE as an edit; else by its synced content, unchanged, in exactly one new
  local file (empty files and look-alikes without a telling name prove nothing); never when this
  device looked empty, and never when the server's file at the old name is not the synced one
  (another `oc:fileid`: deleted and made anew there — a MOVE would carry it over the renamed note;
  the same holds for a folder MOVE): the renamed page is uploaded as a new one instead. Many
  single renames in one run cost one step each (the run's listings and states are updated in
  place, not rebuilt per file). Concurrent edits are
  merged three-way (drawings element by element, never by lines); when that fails both versions
  stay (conflict copy) for `ConflictResolver`.
* **Publishing** (core `publish`): only what a target's items name leaves the device, through the
  HTML export's escaping (no note content ever becomes markup that runs; the only script is the
  site's own search, which builds its results as text); hidden folders, conflict copies, encrypted
  pages, files and fragments and pages marked `publish: false` are never published, extension pages,
  conversations with AI and templates only when chosen; a published page shows only published
  pages (links to others are plain text, backlinks, tag pages and queries count published pages),
  macros of extensions stay as written. A file changed at the target since the last publish is never
  replaced or deleted without the user's "Replace them" (WebDAV: If-Match / If-None-Match; GitHub:
  the blob ids; the branch only fast-forwarded); unpublishing removes only what the manifest says
  was published there. Targets live in the device state, never in the notebook (a synced notebook
  cannot name a server); passwords and tokens in owner-only files, masked in logs.
* **GitHub** (core `github`, `WorkspaceGitHub`): a file is written only when its downloaded bytes are
  the blob the tree names; files removed on GitHub go to the vault's trash, and a branch or folder
  that lost every file the notebook had removes nothing (`remote-empty`); a file changed here is
  never replaced — changed on both sides it is merged or GitHub's version is saved beside it, and
  the commit waits until that copy is merged; a commit is made on the branch's newest version and
  the branch only fast-forwarded, a proposal's branch someone else changed is never overwritten;
  nothing is committed while a pull left files behind or every pulled file is gone here (a lost
  storage); files that cannot be read here are never taken for removed. Hidden files and folders of
  the repository are never touched.
* **Backups** (core `backup`, `WorkspaceBackups`): device-local settings and state in the notebook's
  LocalStore (`backup/…`), never in the vault. Scheduled runs (at open, hourly, when the app returns
  to the foreground) skip a notebook whose storage cannot be seen and one with no files but its
  settings, and write nothing when the fingerprint (paths, sizes, modification times) equals the last
  complete backup's and that file still exists. A run that archived no file of the notebook, or left
  files out while the notebook's storage went, is a failure and its ZIP is removed. A run that left
  files out otherwise is kept as an **incomplete** copy (manifest, the list says so): no fingerprint,
  so the next due check tries again, and it prunes only older incomplete copies, never a complete one;
  restoring it sends nothing to the trash but blockers. A run writes a temporary file beside the target (`QSaveFile`) and renames it on
  success, so failed or cancelled runs leave no `.zip`; a chosen folder that is missing is never
  created (it would land on an empty mount point) and becomes a status plus a warning in the log.
  Pruning runs after a successful backup only, and takes only copies whose manifest names this
  notebook (`Vault::id()`, from its path, so copies from before a lost device state are still its
  own). A copy with another id or none — another notebook of the same folder name in the same folder,
  this one when it lay elsewhere — is listed as foreign: never pruned, and restoring or deleting it
  takes a confirmation of its own. A folder inside the notebook is refused for the default folder too.
  Restore: an archive with no file of a notebook is refused, on case-insensitive storage also names
  colliding after case folding (elsewhere names differing in letter case are different files); every entry
  validated first (absolute, `..` also Windows' `.. `, backslashes, drive letters, control characters, non-NFC,
  ignored paths, duplicates, a file used as a folder, encrypted, sizes deflate cannot give — the
  whole file refused), bytes to write inflated and their checksums compared, the plan confirmed by
  the user, open editors saved, Nextcloud's sync held (a run in flight is waited for, `SyncScheduler::setHeld`), a safety
  backup made (its failure aborts, and so does a file the plan touches missing from it; the source protected from pruning),
  a marker stored (`backup/restoring`: closing or a kill is told on the next open, the safety backup kept and scheduled
  backups off until the user dismisses it), then writes through the Vault with History snapshots from the disk (a file
  that exists but cannot be read is never replaced), and only when all succeeded the rest to the trash; an editor
  showing a trashed page drops its edits, which are in the safety backup. Sync is released once: one run sees
  every deletion at once, so the mass-deletion guard holds them back as any others (`tst_backup_restore`,
  `tst_shell_workspace_sync`). Syncthing watches the folder itself and cannot be held. A single entry above 1 GB is left alone and reported (Vault writes
  hold a file in memory).
* **Editor**: unsaved edits are merged three-way with external changes; an unresolved conflict
  is saved as a conflict copy, never over the other version. Deleting moves to `.trash/`.
* **Browser**: IndexedDB write-back never starts before the initial load succeeded; private
  mode / blocked storage is reported before the user writes anything.
* **The folders nobody syncs, under every name** (`paths::isIgnored`, `util/paths.h`): `.git`,
  `.obsidian`, `.brulion`, `.trash` and the others are left out by the sync, the GitHub sync, the
  backups, the index and the command line — and so is every other name the storage opens the
  same folder by: another letter case (`.GIT`; also the letters only a file system folds, `ı`
  and `ſ`), a trailing dot or space (`.git.`, Windows), an NTFS stream (`.git::$INDEX_ALLOCATION`),
  an 8.3 short name (`GIT~1`, `OBSIDI~1`) and a name with characters that are not shown
  (zero-width ones). A server, a repository or an archive that names `.GIT/hooks/pre-commit` or
  `.Brulion/config.json` reaches neither a program's hooks nor the notebook's settings; a page
  cannot be created under such a name (`notebook::plannedPage`: `HiddenPath`).
* **What a server may send** (core `sync`): an answer is as long as its request accepts
  (`HttpRequest::maxBodyBytes`; `NetworkHttpTransport` stops at the announced length and at the
  bytes kept, `HttpResponse::tooLarge`, `WebDavErrorCode::Oversized`) — a download twice its
  listed size or a mebibyte more, a folder's listing 128 MiB, the answer to a change 1 MiB, any
  other request 256 MiB — instead of whatever a server goes on sending; a listing gives up at
  500 000 entries or folders 64 deep (`ListLimits`: a server with a folder in every folder);
  an oversized file is an error of that file, the rest is synced. A redirect is followed only to
  the same server — host and port, or http's default port to https's — and never down to http
  (`sync::isSameServer`): the `Authorization` header goes nowhere else.
* **Text in linear time** (`unicode::nfc`, `util/unicode.h`): names, links and search keys are
  normalised (NFC) in time linear in their length. Qt's `QString::normalized` takes quadratic
  time on a long run of combining marks and on text after one decomposed letter — a note or a
  file name from anywhere could stop the app for hours; a letter with more than 64 marks keeps
  them as written.
* **The device's state** (`<app data>/vaults/<id>`: the history of every note, the texts a sync
  merges with, the search index) is closed to other users of the device:
  `notebook::keepDeviceStatePrivate()` makes the folder with owner-only permissions and closes
  one an older version left open (the app, and brulion-cli for the folder it shares with the
  app; one named with `--state-dir` keeps the permissions the user gave it). Secrets
  (`<app data>/secrets`) were owner-only already; Android's own backup leaves them out
  (docs/ANDROID.md "Backups").
* **Archives**: an entry whose announced size its compressed bytes cannot give (a forged
  header) is skipped before memory is set aside for it (`io::readZip`; a backup with one is
  refused, `ArchiveProblem::TooLarge`). A backup never follows a link put in a file's place
  after the files were listed (`WriteTask::addOnce`, reported as skipped).
* **Rendering**: note text reaches QML only through the escaping `InlineRenderer` (or
  `TextHelper`/`DiffHelper` escaping); the calculator never evaluates note text as script.
  Behind that, the QML engine's network access manager serves local URLs only (`file:` of this
  machine, `qrc:`, `data:`; `LocalOnlyNetworkFactory` in `src/app/ui/localnetwork.h`; any other
  URL — also `file://server/share/…`, a UNC path that Windows would connect to — gets a
  transparent pixel, never a request): nothing in QML loads from the network, so even a rich-text
  `<img>` with a web address that slipped through cannot make a synced or imported note contact a
  server. Qt Quick reads `file:` URLs itself (Image, rich text), never through that manager, so
  the engine also has a URL guard (`RemoteFileUrlGuard`, same header): a URL property — Image's or
  Loader's source — naming a file of another machine (a host but localhost, `file:////server/…`,
  `%5C%5C…`, `webdavs:`) becomes an empty URL, unless the file lies in the open notebook: the
  pictures of a notebook on `\\server\share` show (the Workspace builds their URLs from vault
  paths). The engine's own QML and scripts are not checked (an app installed on a share). An
  `<img>` inside rich text is resolved by Qt without the guard: that none comes from note text is
  the renderers' job.
  Pictures from the internet (`![x](https://…)`) are fetched by the Workspace itself
  (`workspacepictures.cpp`, core `webpicture`) only when the user allows it — "Show" on the
  picture, the device setting `webPicturesShown` (off by default) or "Save to notebook" — and
  shown from the device's cache as a local file: http(s) only, no cookies, at most 20 MB, the bytes
  checked to be a picture. The setting never fetches from this device or its private network
  (a synced note must not send requests to the router or printer), and no redirect leads there
  from a host of the internet. Indexing, sync and import never fetch them; an HTML or Pandoc export
does only with "Include copies" (`ExportController`: the cached copies, read in chunks, the
missing ones fetched through the same Workspace, explicitly asked for like "Save to notebook" —
but never from this device or its private network, which a note alone must not make it ask; at
most 256 MB of copies in all, the rest keep their addresses). In the browser the
fetches are CORS "simple" requests the browser itself sends (and redirects itself: the manual
redirect check does not apply there); a failed one is told as CORS or mixed content.

## Security (encrypted notes and the app lock)

What protects what (docs/UI.md "Encrypted notes", "App lock"; docs/SYNTAX.md §11 "Encrypted text"):

* **Encrypted notes** protect text at rest and in transit — the notebook's folder, other devices,
  sync servers, backups, GitHub — with a password: AES-256-GCM, the key from PBKDF2-HMAC-SHA512
  (220 000 rounds, a 16-byte random salt per fragment or page, a 12-byte random IV per save; the
  parameters authenticated as AAD), in Obsidian's Meld Encrypt format so one notebook opens in both
  apps. The primitives are BearSSL's constant-time code (core `crypto`); comparisons of tags and
  hashes are constant-time; keys, passwords and decrypted bytes live in `crypto::SecureBytes`,
  zeroed when they go. A QString that came from QML (a password field, the dialog's text) cannot be
  wiped reliably — Qt may hold copies —: the fields are emptied after use and the dialog's text when
  it closes or the notes lock; that is the extent of "wiped where practical".
* **Plaintext never reaches the disk or any other copy.** A fragment is decrypted in its dialog only
  (`WorkspaceEncryption::openFragment`); an encrypted page is decrypted by PageView's
  `EncryptedPageHost`, so the block editor holds Markdown in memory while the Vault writes (and
  History snapshots, the index cache, the sync, backups and exports read) only Meld Encrypt's JSON.
  The Workspace refuses to write anything else into a `.mdenc` file (`refusesPlaintext`: an editor
  or a view that does not decrypt it). The index sees an encrypted page as a file of its path, and
  of fragments only that they are there: their Base64 makes no words (`SearchIndex`), links, tags,
  tasks and reminders inside encrypted text are never indexed (no backlinks, no Tasks, no alarms).
  The AI assistant and extensions read notes through the index and the vault: they get ciphertext
  only. Exports show `🔒` (or keep the markers in Markdown); an encrypted page is not exported,
  printed or turned into a QR code. Logs never carry a password or decrypted text (nothing logs
  them; the crash report holds only a stack). The clipboard: "Copy" marks the copy sensitive where
  the platform knows how (Android 13+'s `EXTRA_IS_SENSITIVE`, KDE's `x-kde-passwordManagerHint`,
  Windows' clipboard-history exclusion) — the copy itself is plain text, as the dialog says. The
  decrypted text's field asks the keyboard for no predictions (`Qt.ImhNoPredictiveText`).
* **Text that was plain before.** "Encrypt selection" / "Encrypt page" cannot reach copies made
  before: earlier backups, other devices' histories, a sync server's versions, the sync's merge
  base (until the next sync). "Encrypt page" writes the encrypted JSON over the page in place (never
  a plaintext file under the encrypted name), renames it with every link following, deletes its
  version history when asked (on by default) and writes the index cache again. New secrets go best
  straight into `/encrypt` or "New encrypted page", which never put plaintext on disk.
* **The session**: passwords typed are kept per page (`encryption::Keyring`) and every unlocked
  thing locks after a time without use (5 minutes by default), on the app lock and with the
  notebook; nothing decrypts by itself — only a user's action tries remembered passwords, at most
  two (each try costs a key derivation of up to two million rounds, which a hostile synced note
  could ask for). A page changed elsewhere under another password locks and is never written over.
* **The app lock** protects the running app on this device from someone holding it unlocked — not
  the files. The PIN is stored as `pbkdf2-sha512$210000$…` in this device's settings (never synced);
  a 4-digit PIN cannot resist an offline attack on that hash, which is not what it is for: wrong
  PINs on the lock screen wait, up to 15 minutes, across restarts. While locked the stage and page
  windows are hidden (also from screen readers), the encrypted notes lock, notifications leave the
  note's text out (also those Android's background check arms), Android's window is kept out of the
  recent apps (Android 13+) or FLAG_SECURE, and the widgets show a placeholder. The phone's own
  check is the framework's BiometricPrompt (Android 10+; biometrics or the screen lock).
* **Not protected**: someone with access to the running, unlocked app (or its process memory);
  file names, folders, hints and the size of encrypted text; a weak password (PBKDF2 slows guessing,
  it does not stop it). Meld Encrypt's implementation and Brulion's have not been audited.

## Build, test, packaging

* CMake ≥ 3.24 + Ninja, C++20, `qt_standard_project_setup(REQUIRES 6.8)`, `qt_add_qml_module`;
  `QT_NO_KEYWORDS`, `QT_NO_CAST_FROM_ASCII`. Warnings: `cmake/BrulionWarnings.cmake`
  (`-DBRULION_WERROR=ON` in CI and the dev scripts). Translations: `qt_add_translations`
  (`i18n/brulion_pl.ts`, `brulion_en.ts` for plurals).
* Build times (docs/PERFORMANCE.md "Build times"): precompiled Qt and standard headers
  (`cmake/BrulionPch.cmake`, `BRULION_PCH`, on for Clang on Linux; a library or program with
  three or more sources has its own, the tests share one per set of Qt modules; ccache is told to
  cache them); a Debug build is `-O0` — the dev shell turns off nixpkgs' fortify hardening, whose
  compiler wrapper put `-O2` in front of every compile; of llama.cpp's processor variants only the
  ones packaged are compiled (`3rdparty/llama`).
* Hardening: `cmake/BrulionHardening.cmake` (on by default, `-DBRULION_HARDENING=OFF` for a
  plain build) gives every target — Brulion's code and the vendored libraries — the options Qt
  builds its own libraries with and the OpenSSF compiler hardening guide recommends: stack
  protector and stack clash protection, Intel CET / ARM64 branch protection, zero-initialised
  locals, `_FORTIFY_SOURCE=3` (glibc, optimised builds), checked element access in the standard
  library's containers (`_GLIBCXX_ASSERTIONS`, libc++'s fast hardening mode on Android),
  read-only relocations bound at start, no executable stack, position-independent executables;
  MSVC: Control Flow Guard, CET shadow stack, ASLR and DEP. Each option is tried at configure
  time and left out where the toolchain does not know it; the browser build has none (the
  WebAssembly sandbox). A memory error in a parser of notes that came from anywhere ends in a
  stop of the program. What it costs: docs/PERFORMANCE.md "Hardened builds".
* Tests: QtTest via CTest (`brulion_add_test`), core in `tests/core/<module>`, app areas in
  `tests/app/<area>` (`QT_QPA_PLATFORM=offscreen`), the command line in `tests/cli` (the real
  `brulion-cli` on temporary notebooks, also its MCP server) and the socket in `tests/ipc`, plus headless QML checks of the real app:
  `tst_shell_qml` (`--selftest-shell`), `tst_shell_journey*` (`--journey`), `tst_*_qml` smoke
  tests; shell scripts in `tests/scripts` (`tst_scripts_release`: `scripts/release.sh` in
  throwaway git repositories; `tst_scripts_appimage_update_check`). Development builds:
  `scripts/remote-build.sh` (x86_64 laptop) or
  `scripts/dev-build.sh` (locked, `-j2`) — see CLAUDE.md. Sanitizer builds (`-fsanitize=` in
  `CMAKE_CXX_FLAGS`, CI) get `BRULION_TEST_SLOWDOWN=25`, an unoptimised Debug build 4 (its stack
  frames are about four times larger): tests multiply their wall-clock limits and worker-thread
  stack sizes by it (1 otherwise), the editor playground's self-tests their
  waits for something asynchronous (`DevTools.testSlowdown`); CI builds them at `-O1`. The GUI
  tests run side by side with the others (`PROCESSORS 2` for a whole app's self-test): their
  checks wait for the state they read, never for a fixed time, and the self-tests' `waitFor()`
  counts a stall of the GUI thread as 250 ms of its deadline (`uidriver.h`: a 5 s save on the
  Windows runner had run out a 3 s wait before the click it waited for); only measurements that compare
  wall-clock times run alone (`RUN_SERIAL`: `tst_shell_workspace_perf`, `tst_scroll_jumps_qml`).
  CI runners stall (Windows' run of 2026-11-19 lost two seconds in a one-line extension command),
  so time in a test follows `tests/common/testtiming.h`: a limit on how long work takes reads the
  thread's CPU time (`CpuTimer`), a comparison of two ways takes medians or the fastest of a few
  runs, a wall-clock budget the test is not about (an extension's time limit) is `kRoomyMs`, and a
  worker that must be busy while the test looks waits for a gate the test opens, not for a delay.
  A fresh build directory starts the longest tests first (`tests/ctest-costs.txt` becomes
  ctest's cost data until ctest has its own timings).
* Nix: `nix develop` (Qt 6.12 from nixpkgs, CMake, Ninja, ccache, clang-tools, gdb);
  `nix build .#brulion` (builds without the tests — doCheck = false —, validates the desktop file and AppStream
  metainfo, runs `brulion-cli`); `nix build .#brulion-cli` (the command line alone, `nix/cli.nix`); NixOS module `programs.brulion` (+ autostart hidden in the tray) and
  `services.brulion-web` (the browser version behind nginx) — docs/NIXOS.md; `nix flake check` (also the whole ctest suite: `checks.<system>.tests`).
* Windows (test version): `scripts/windows-build.ps1` — MSVC 2022 (`/utf-8`, `/permissive-`,
  `/Zc:__cplusplus`, `/bigobj`, `/W4` without `/WX`), Qt 6.12.0 `win64_msvc2022_64`, `windeployqt`
  + the app-local MSVC runtime → portable ZIP + Inno Setup installer (`packaging/windows/`: the
  `.iss`, the icon, the executables' version resources); the desktop integration
  (`BRULION_DESKTOP`: tray, single instance over a named pipe of this user, brulion-cli, the QML
  self-tests) as on Linux; `BRULION_PROFILE` (src/ipc/profile.h) isolates test runs there; the
  toast bus uses the Windows SDK's C++/WinRT headers and `WindowsApp.lib`; the installer has the
  task "autostart" (the Run value) and the AppUserModelID on its shortcuts — docs/WINDOWS.md.
* Linux: `scripts/appimage-build.sh` (Qt 6.12.0 from aqt, linuxdeploy; the Qt Quick styles and
  QtQuick.Dialogs that linuxdeploy-plugin-qt copies with `qml/QtQuick` are removed before packing,
  and so is its AppRun hook, which set `QT_QPA_PLATFORMTHEME=gtk2` on GNOME and XFCE; the XDG
  portal's platform theme is kept for the file pickers, and the check asks Qt which theme it tries
  first under GNOME) → AppImage x86_64 +
  aarch64, each with a `.zsync` file for AppImageUpdate (checked before publishing by
  `scripts/appimage-update-check.sh`); desktop entry and metainfo in `packaging/`.
* Android: `scripts/android-build.sh` — Qt 6.12.0 for Android from aqt with host tools of the
  same version, NDK r27c, JDK 21; ABIs `BRULION_ANDROID_ABIS` (default arm64-v8a; armeabi-v7a,
  x86_64, x86 → one multi-ABI APK); `android/` holds the manifest, resources and Kotlin
  sources (+ Kotlin unit tests); signed APK in GitHub Actions — docs/ANDROID.md.
* WebAssembly: `scripts/wasm-build.sh` — Qt 6.12.0 `wasm_singlethread` + matching emsdk; our
  HTML shell, manifest and service worker in `wasm/` (offline PWA; the shell gives every
  `setTimeout` with a wait 2 ms more, as Qt 6.11.2's dispatcher stops all its timers when one
  runs early — docs/WEB.md "Technical limitations"); `scripts/wasm-single-file.py` packs the
  built site into one HTML file opened from disk (`file://`, no server) — docs/WEB.md.
  Browser tests of the built site: `scripts/wasm-smoke.mjs` (Chrome over a dependency-free
  DevTools client) and `tests/web` (an npm project CMake ignores: Playwright in Chromium and
  Firefox — pictures from the internet, the pen, the shell —, `scripts/web-test.sh`).
* **The model on the device** (docs/EXTENSIONS.md "AI"; `cmake/BrulionLocalAi.cmake`,
  `3rdparty/llama`): llama.cpp is not vendored — one release (`3rdparty/llama/pin.json`: tag, Nix
  hash, the archive's SHA-256; LFM3 needs v0.8.0 or newer) is compiled from sources given as
  `BRULION_LLAMA_SOURCE_DIR`: the Nix package, the Android APK of `nix/android.nix` and the dev
  shell fetch them by the pin, `scripts/android-build.sh` downloads and checks them
  (`scripts/llama-source.sh`). `BRULION_LOCAL_AI=auto` (the default) builds it whenever the
  sources are given, for the app, 64-bit, not for the browser; the AppImage and Windows builds
  pass none yet and have no "On this device" (TODO.md). Built as llama.cpp ships itself: shared
  libraries `llama`, `mtmd` (pictures), `ggml`, `ggml-base`, and the processor code as plugins
  `ggml-cpu-<variant>`, one per instruction set — x86-64: `haswell` (AVX2), `alderlake`
  (AVX2 + VNNI), `skylakex` (AVX-512), `zen4`; Android arm64: `android_armv8.2_2` (dot product,
  half floats), `android_armv8.6_1` (int8 matrix multiply); Android x86-64: `haswell` and `sse42`
  (the emulator's processor has no AVX2); Linux arm64: `armv8.0_1` … `armv8.6_1` —
  of which the runner loads the best one the processor can run (each refuses to load on one
  that lacks its instructions; none loads: "this device's processor is too old"). Not one static
  library: code compiled for AVX2 or ARMv8.2 then stays inside its plugin, where linked statically
  the linker could pick such a copy of a shared template for the whole program. The app links
  none of it: `brulion_localai` (`src/core/ai/localplugin.cpp`) is loaded the first time a model
  is asked, from `<lib>/brulion` next to the program (Android: `lib/<abi>/` of the APK, by name —
  copied there by the build, not given to Qt as `QT_ANDROID_EXTRA_LIBS`, which its loader loads
  at every start; docs/ANDROID.md "A model on the phone"),
  so startup and an app that never asks a model are as before. 16 MB installed on x86-64
  (llama.cpp knows some 170 model architectures; four processor plugins), 9.2 MB in the arm64 APK. The Linux CI job, the
  browser and brulion-cli builds are without it; `tst_ai_localplugin` loads the plugin as the app
  does in every build that has it, `tst_ai_llama` runs a real model when `BRULION_LOCAL_MODEL`
  names one.
* **Speech recognition** (docs/UI.md "Voice notes"; `cmake/BrulionSpeech.cmake`,
  `3rdparty/whisper`): whisper.cpp is not vendored either — one release (`3rdparty/whisper/pin.json`,
  v1.9.5) given as `BRULION_WHISPER_SOURCE_DIR` (the Nix builds and the dev shell by the pin,
  `scripts/llama-source.sh --whisper` for the scripts). Only its library is compiled (static,
  position independent, into the plugin `brulion_speech`), over llama.cpp's `ggml` target — never
  whisper.cpp's own ggml copy: whisper.cpp 1.9.5 bundles ggml 0.26.0, llama.cpp v0.9.0 has 0.29.0,
  and whisper.cpp reaches the processor code only through ggml's backend registry, so it compiles
  and runs against the newer one unchanged. `BRULION_SPEECH=auto` builds it with the model on the
  device when the sources are given; the plugin goes into the same folder (`<lib>/brulion`, the
  APK's `lib/<abi>/`), loads the processor plugin the language models loaded or the best one itself.
  0.9 MB on x86-64. `tst_speech_plugin` loads it as the app does; with `BRULION_SPEECH_MODEL` it hears
  a recording.
* **Audio and video** (docs/UI.md "Audio and video"; `cmake/BrulionMultimedia.cmake`):
  `BRULION_MULTIMEDIA=auto` builds the players and the recorder when Qt has Qt Multimedia (the Nix
  package: `nix/multimedia.nix`, a Qt Multimedia without GStreamer over an FFmpeg of the formats notes
  use; the AppImage, Windows and the Linux CI job: aqt's `qtmultimedia`; Android: Qt's Android
  backend without the FFmpeg one — `BRULION_ANDROID_FFMPEG`; the browser: none, "Open in a new
  tab"). The sizes: docs/PERFORMANCE.md "Audio and video: what Qt Multimedia costs".
* CI (`.github/workflows`): `ci.yml` (Nix build + flake checks on x86_64/aarch64, aqt build
  with ASan/UBSan and `-Werror`, actionlint/shellcheck/clang-format), `android.yml`,
  `appimage.yml`, `windows.yml`, `wasm.yml` (the site and the single file, the smoke test and the
  Playwright tests — job `browsers`; the Android emulator's browsers — job `keyboard-android`),
  `fdroid.yml` (Brulion's own F-Droid repository on GitHub Pages — docs/ANDROID.md),
  `release.yml` (tag `v*` → draft release with APK, AppImages, the Windows installer and ZIP,
  web ZIP and single HTML file, sources, checksums; then the F-Droid repository) — docs/RELEASE.md.
