# Brulion — architecture (C++20 / Qt 6)

Brulion is an offline-first Markdown notes app: Obsidian-style plain files, a Logseq-style
block outliner with daily journals, calendar, reminders and alarms, calculations and tables,
live queries, sync through Syncthing (plain folder) or Nextcloud (WebDAV), import/export from
other apps.

One C++/QML code base, four targets, all built against **Qt 6.11.2** (CMake accepts ≥ 6.8):

| Target | How | Vault storage | Reminders |
|---|---|---|---|
| **NixOS / Linux desktop** | Qt from nixpkgs: flake package + NixOS module; AppImage (Qt from aqt) for other distributions | a folder (e.g. a Syncthing folder) | `org.freedesktop.Notifications` over D-Bus, tray as fallback; timed in-app (`ReminderScheduler`), the app can live hidden in the tray (`--hidden`, autostart); the tray calendar from the icon's menu (or its click, a setting) |
| **Windows** 10/11 x86_64 (test version, docs/WINDOWS.md) | MSVC 2022 + Qt from aqt, `windeployqt`; portable ZIP and a per-user Inno Setup installer from GitHub Actions | a folder (default `Documents\Brulion`) | toast notifications through C++/WinRT (`WindowsToastBus`: Snooze/Done/OK, alarms on screen with a looping sound; the tray icon's messages when toasts are off), timed in-app (`ReminderScheduler`) while the app runs, also hidden in the tray; optional start at sign-in (the Run key, `Autostart`); a click on the tray icon opens the tray calendar (a setting; the system clock's flyout cannot be replaced) |
| **Android** (9+, API 28–36) | Qt for Android (aqt) + `androiddeployqt`, signed APK from GitHub Actions | a folder on shared storage (Syncthing-Fork; "All files access") or app storage + Nextcloud | native `AlarmManager` alarms (Kotlin via JNI) — fire while the app is closed and after a reboot; "Zrobione" taps queued natively (`DoneQueue.kt`) and written by C++ at once while the app is in front, else when it returns to the front or starts (Qt's event loop is paused in the background); home-screen widgets drawn by Kotlin from a snapshot the app publishes (their quick add writes today's journal itself, see below) |
| **Browser** | Qt for WebAssembly (single-threaded) + emsdk, offline PWA shell (`wasm/`) on GitHub Pages | Emscripten IDBFS (IndexedDB) + import/export, Nextcloud sync | web Notification API (service worker where needed) while the page is open |

On the desktop (Linux, Windows) a command line for scripts and AI agents comes with it:
`brulion-cli` (also `brulion cli …`), with an MCP server (`brulion-cli mcp`) — see "Command line" below and
docs/CLI.md.

## Layers

```
┌──── src/app  (executable "brulion", QML module "Brulion") ─────────────────────────────────┐
│ QML: qml/theme, qml/components (B* design system), qml/shell, qml/views, qml/editor,       │
│      qml/content, qml/planning, qml/io, qml/drawing, qml/dev (gallery, editor playground)  │
│ C++ area libraries: brulion_ui_helpers · brulion_ui_touch · brulion_ui_scroll ·            │
│      brulion_editor (+ brulion_editorhost) · brulion_shell · brulion_notify ·              │
│      brulion_content · brulion_planning · brulion_ioviews · brulion_diag · brulion_drawing │
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
│ · io · mstodo · webpicture · drawing                                                       │
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
5. **Polish first.** Source strings in English with `tr()`/`qsTr()`; the Polish translation
   (`i18n/brulion_pl.ts`) is the default UI language; English selectable.
6. **Round-trip fidelity.** Editing one block never rewrites other bytes of the file
   (line endings, indentation, BOM, unknown syntax preserved).
7. **Deterministic.** Core code never reads the clock itself (callers pass `now`/`today`);
   the app takes `--now` for reproducible screenshots and tests.
8. **Threads are injected.** Core is single-threaded by contract (a `Vault`, a `VaultIndex` are
   used from their owner's thread), except pure functions that run on any thread: `parseNote`,
   `VaultIndex::prepare`, `SearchIndex::prepare`, `Vault::readDetached`, the index cache's
   decoder. `notebook::IndexLoader` / `IndexCacheKeeper` run them on a `notebook::WorkerPool` the
   caller passes (the app's `indexworkers.h`, brulion-cli's own pool); without one (WebAssembly,
   `BRULION_INDEX_THREADS=0`) they work in slices on the calling thread. Core never starts a
   thread itself and checks no `QT_CONFIG(thread)`. A static cache added to the parse path must
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
| `log` (—) | the event log (UI "Rejestr zdarzeń", see "Logging" below): the `brulion.*` logging categories and their UI groups, `Entry` + the line format, `redact()` + the registry of live secrets (`ScopedSecret`), `Logger` (chained Qt message handler, ring buffer, rotating file, rate limits, verbosity, export), `SlowScope` / `formatBytes` / `formatMs` |
| `util` | `paths.h`: vault paths (normalize/NFC, join, dirname, basename, `pageTitle`), ignored paths and temp files, Syncthing / Brulion conflict copy names, `sanitizeFileName`, unique paths |
| `text` (util) | diacritic folding (`foldDiacritics`, `foldKey`: "ŻÓŁW" → "zolw"), Unicode word boundaries and regex helpers, EOL-preserving `splitLines`/`joinLines`, FNV-1a `contentHash`, `compareNames` (Polish alphabetical order with numbers by value, no ICU: the index, All pages and the sidebar's tree sort alike on every platform); `decode.h`: `decodeText` — files of unknown origin (imports, shares) as text: UTF-8 with or without BOM, UTF-16 with BOM, else windows-1250 when Polish letters dominate, otherwise windows-1252 |
| `dates` (text) | local dates (`DateSpec`, never through UTC); `parseNaturalDate` (Polish + English: "jutro o 9", "w piątek 18:00", "za 2 godziny", "next friday 6pm"); `RecurrenceRule` parse/format/`nextOccurrence`/`occurrencesBetween`, Logseq repeaters; query date expressions (`resolveDateExpr`: `today`, `+7d`, `this-week`, `today..+7d`, `overdue`); human formatting ("dziś 09:00", "pt 17 paź") |
| `markdown` (util, text, dates) | `parseNote` → `ParsedNote` (blocks/outline, links, tags, tasks, reminders, properties, YAML-subset frontmatter, headings, block ids, journal date); metadata tokens; pure edits (`setTaskStatus` incl. the next occurrence of recurring tasks, `removeTaskMarker` for "no status", `renameLinks`, `renameTags` (the tags naming a renamed page), `retargetLinks` / `retargetTags` (links by their range, tags by key: what a move writes), `ensureBlockId`, frontmatter values); typed page properties (`pageprops.h`: `readPageProperties` — the YAML frontmatter and the leading `key::` lines as text / number / checkbox / date / date and time / list / tags / aliases —, verified single-property edits `setPageProperty`, `removePageProperty`, `renamePageProperty`, `addPageListItem` / `removePageListItem` changing only that property's lines, list style and comments kept); single-token edits of the token menus (`tokenedit.h`: dates, reminders, 🔁 rules, priorities and links to days at a chip's range — `applyTokenAction` —, `TokenRef` naming a token across copies of a block and `editBlockToken` writing it into a file's text, only the token's bytes); tag occurrences with their ranges (`findNoteTags`), the tag naming a page (`tagSyntax`); journals and templates; note modes (outline / document: `resolveNoteMode` from the `brulion:` frontmatter key, journal / page defaults and a content heuristic; `setNoteModeKey`); links to headings and blocks (`linkformat.h`: `headingFragment`, the one heading matcher `findHeading` of links, embeds and copying — Obsidian heading paths included —, `uniqueHeadingFragment`, `formatReference` of the copied `((id))`, `[[Page#^id]]`, `[[Page#Heading]]`, embeds); **`BlockDocument`** (editable block list with lossless round trip: split/merge/indent/outdent/move subtree/toggle task/list item to paragraph; ranges of a block selection: `removeRange`, `indentRange` — all or nothing —, `outdentRange`, `moveRangeUp/Down`); what a block selection copies (`blockclipboard.h`: `topRows`, `blocksMarkdown` — exact blocks re-based to column 0 by visual columns, LF, no BOM or frontmatter —, `blocksPlainText`, `stripInlineMarkup` shared with the editor's one-line plain text — paired marks only, code spans verbatim —, `withoutBlockIds` for pastes that must not duplicate `((id))` targets); `markdownDestination` (where a Markdown link's or picture's destination is written, by the parser's own rules: what retargeting a picture from the internet replaces); **`InlineRenderer`** → escaped Qt rich text with `page:`/`tag:`/`block:`/`ext:`/`task:`/`reminder:` links (and, for the token menus, `date:`/`recur:`/`prio:`/`day:` ranges) and date/reminder/recurrence chips (Polish one-letter words joined to the next by a no-break space) |
| `calc` (text, dates) | own engine (lexer + Pratt parser + evaluator, bounded work, no `eval`): Polish decimal comma, variables (Unicode names), `ans`, percent sugar, units and conversions, currencies as tags, functions, `sum`/`suma` lines; calc blocks, inline `= expr`, page scope (calc variables + numeric properties); GFM tables: parse/serialize/replace and **table formulas** (A1, ranges, `[Kolumna]`, `above`/`left`…, dependency order, `#CYCLE`) |
| `vault` (util, text, log) | `Vault` (folder scan, cached listing, atomic `QSaveFile` writes, `QFileSystemWatcher` on the folders and the files open in editors + rescans (when the app returns to the foreground, every 2 minutes) for external changes, echo suppression by content hash, trash `.trash/`, rename/move, `.brulion/config.json` — without it, the settings of an Obsidian vault (`obsidiansettings.h`: `.obsidian/daily-notes.json`, `app.json`, `templates.json`, moment.js → Qt date formats, guards for folders Brulion already used) —, storage checks — see "Data safety"); `readDetached` (static, any thread) + `adoptRead` = `readText()` split in two for the index loader's workers, with exactly its bookkeeping); `History` (device-local version snapshots, throttled and pruned); `LocalStore` (small JSON key-value store per vault in AppData) |
| `index` (util, text, dates, markdown) | `VaultIndex` (incremental, coalesced `changed()`: notes, link resolution per SYNTAX §2, backlinks incl. `((id))` refs, unlinked references, block ids, tags — also as links to the pages they name, SYNTAX §3: `resolveTag` (notes only), `tagsNaming`, `tagReferences`, `tagsTo`, resolved once per tag and index version —, tasks, reminders, journals, attachments, page/block suggestions, page-property keys and values in use (`propertyKeys` with each key's most common type, `propertyValues`: the properties panel's suggestions), graph data; moves: `linkTarget` / `tagTarget` (the shortest name reaching a file) and `planRelocation` — the notes to rewrite so every link and tag keeps its target, and the ```query filters naming a moved file or a renamed tag (`findQueryNames` / `retargetQueryNames`, `notesWithQueries`), resolved with the lookup tables of before and of after the move — an overlay of the moved names on
those of before —, `relocate.cpp`; `RelocationPlanner` makes the same plan a few notes at a time); `SearchIndex` (BM25+, prefix + fuzzy, diacritic-insensitive, excerpts; documents tokenised anywhere with `prepare()`, move-only, documents point into the dictionary); the query language (`parseQuery`/`runQuery`, SYNTAX §10); loading on other threads: `VaultIndex::prepare` / `prepareParsed` → `PreparedNote`, `updatePrepared` (re-parsed when prepared with another `parseFingerprint`), `reserveSequence` (recency by path order whatever the merge order), `adoptSearch` (a full-text index built elsewhere, documents kept only when their `contentKey` — FNV-1a over the note's UTF-16 — is the note's); the index cache `indexcache.h` (`Identity` — format, `markdown::ParserVersion`, `SearchIndex::FormatVersion`, Qt and app versions, parse fingerprint, vault id —, `Reader` (validated, bounds-checked, per-record checksums; `decodeNote`, `SearchDecoder` in steps), `Writer` (in slices, aborts when the index changes); own binary format documented in the header). Internal lookup maps use `smallsets_p.h` (`PathSet` / `PathCounts`: a sorted list up to 32 paths, a hash above) |
| `webpicture` (util; QtNetwork) | pictures from the internet in notes (docs/SYNTAX.md §2): `isWebUrl` / `webUrl` (http(s) with a host), `sniffFormat` (the picture format by its signature — an HTML error page is none), `attachmentName` / `displayName`; `isPrivateHost` (loopback, private-use, shared 100.64/10, link-local, site-local, unspecified IPs; single-label names and home-network suffixes such as `.local`, `.lan`, `.home.arpa`, `.internal`; other names not resolved), `redirectAllowed`; `PictureFetcher` (GET only, every redirect checked by hand — never https → http, never from a host of the internet to a private one —, no cookies, a generic User-Agent and `Accept-Language: *` — `setSimpleRequests` in the browser: neither, CORS "simple" requests —, size limit also on the announced length, stall timeout, one request per URL, 4 at once, `cancel(url)` and `cancel(urls)` — the queue cleared of them all before a running one is aborted, a refused redirect failing as `Redirect`); `PictureCache` (the device's cache folder, `<sha1 of the URL>.<format>`, pruned least recently used first — `lookup()` marks a file used by its modification time, at most once an hour) |
| `drawing` (—; zlib through the vendored miniz) | drawings in notes (docs/SYNTAX.md §2 "Drawings"): the model (`Drawing`, `Stroke` = `Element`: one Excalidraw element Brulion draws — `ElementType` freedraw, rectangle, ellipse, diamond, line, arrow, text, image; points with pressure or a box, angle, colours, fill and stroke styles, roundness, groups, bindings, labels, text, picture —, `Background`, `PaperTheme` — as the app, light, dark: `appState.brulionTheme` —; `bounds`, `editable` — no frames, embedded pages or unknown elements —, `normalizeStroke`), the outline of a freehand line (`stroke.h`: a port of perfect-freehand 1.2, MIT, with Excalidraw's freehand options; `outlineToSvgPath`), the look and hit tests of elements (`geometry.h`: `elementShape` — paths for fills, hatching (bounded: `MaxHatchLines` a shape, `DrawingHatchLines` a drawing), outlines, arrowheads, rotated —, `elementBounds`, `textLines`, `hitTest`, `touchesSegment` — the eraser —, `lassoSelects`), the edits (`edit.h`: `selectionClosure`, `moveElements` — labels and bound arrows follow —, `duplicateElements`, `deleteElements`, `recolorElements`, `reorderElements`, `newShape`, `newText`, `setText` — an unchanged text no edit —, `addLabel`, `labelMaxWidth`, `wrapText`, `isLocked` — a locked shape's label too —; `diffElements` / `applyDiff` — the editor's undo steps), the merge of two versions (`merge.h`: `merge` against a `MergeBase` — new here on top, changed here wins, deleted here goes; unknown base: a union —, `mergeKey`; shared by the editor, the sync center and Nextcloud's sync; `mergeCounts`, `sameDrawing`, `containsDrawing` — how two versions relate, the sync center's cards —; `mergeDrawingFiles` — three versions of a drawing file merged against the base and written in the remote's format, nothing when one cannot be merged: not editable, broken, no base, other formats, an element changed without a new version, both sides' plugin Markdown changed), the Excalidraw format (`excalidraw.h`: `parseScene` — `elementFromJson` for the elements Brulion draws, every other element kept as foreign, deleted ones dropped, unknown fields kept per element —, `sceneJson` — `elementJson`: unchanged elements as read, changed ones from the model, fractional indices kept while they rise —, the SVG payload as excalidraw.com's "Embed scene" writes it: `encodePayload`, `decodePayload` (versions 1 and 2, compressed or not), `svgPayload`, `looksLikeExcalidrawSvg` (its first 4 KB), `deflate` / `inflate` with a size limit), the colours of the light and the dark theme (`palette.h`: `darkRgba` / `darkColor` — Excalidraw's dark-mode mapping, invert 93 % + hue-rotate 180° —, `displayInk` — highlighters keep their colour, opacity × 1.25 —, `displayPaper` — always opaque —, `displayRule`, `rendersDark`, `darkSvg` — another program's SVG with its colours mapped one by one, pictures and emoji untouched —; the WCAG `contrast` the tests check), the SVG Brulion writes (`svgwrite.h`: `toSvg` — integer width and height on the root (`pictureRect`), the paper, lines or grid, every element in the drawing's theme: paths, `<text>` per line, `<image>` of safe data URLs; CSS only for "as the app": one `@media (prefers-color-scheme: dark)` block for browsers, ignored by Qt —, `svgWithScene` — another program's picture of the scene with the scene (a `Drawing` or its JSON as given) embedded right after its root's start tag, a scene it had (as Excalidraw writes one) replaced, every other byte kept (an imported Trilium canvas) —, `safeColor`, `safeDataUrl`, `fontFamilyCss`), the Obsidian Excalidraw plugin's files (`excalidrawmd.h`: `looksLikeExcalidrawMarkdown` — the frontmatter key `excalidraw-plugin` —, `readExcalidrawMarkdown` — the `## Drawing` block, compressed or json, the `## Text Elements` entries, the deleted elements —, `writeExcalidrawMarkdown` — only the block and changed or removed text entries rewritten, erased elements as tombstones —, `stringifyJson` — JSON.stringify's layout), LZ-String (`lzstring.h`: `compressToBase64` / `decompressFromBase64`, a port of lz-string 1.5, MIT), one API of drawing files (`drawingfile.h`: `readDrawingFile` / `writeDrawingFile` — SVG, `.excalidraw`, `*.excalidraw.md`, each written back in its own format —, `looksLikeDrawingFile`, `previewDrawing` — dashed boxes for foreign elements), `readDrawing` (an SVG, a `.excalidraw` file or a plugin file → a drawing, or why not) |
| `sync` (util, text, vault, log, drawing; QtNetwork) | `HttpTransport` seam + `NetworkHttpTransport` (protocol, transfer progress); `WebDavClient` (PROPFIND/GET/PUT/DELETE/MKCOL/MOVE, ETags, Basic auth; every request observed: `exchanged`, in-flight / idle time, one `brulion.net` line per request in the detailed log); `SyncEngine` (bidirectional, per-file state + merge bases in LocalStore, conflict copies, deletions, mass-deletion guard; folders: empty ones both ways, deletions of emptied ones, the folders synced before kept per folder, a folder or page renamed in the vault moved on the server with one MOVE and its states following it, a folder or page renamed on the server — known by its `oc:fileid` (recorded in the sync state, backfilled from listings), else by the ETag a move keeps and its size — renamed in the vault with `Vault::rename` (`syncfolders.cpp`); the paths without deletions run first, so content that arrived under a new path counts as moved, not deleted; numbered runs with a trigger, phase timings, request statistics, per-file error codes, `activity()`); `merge3()` line diff3 with a union of concurrent appends (also lines added next to lines the other side changed: a journal's last block edited while an agent appends), `mergeJson3()` for the config; a drawing (an SVG with an Excalidraw scene, `*.excalidraw.md` with the plugin's key, `.excalidraw`) never merged line by line — merged element by element against its base (`drawing::mergeDrawingFiles`), both versions kept when it cannot be; `SyncScheduler` (interval, debounce after edits, online/foreground triggers, retry back-off; `runFinished` for every run; stall watchdog while syncing: "stalled" after 20 s without progress, stopped and retried after 180 s); `SyncHistory` (the last 20 runs, quiet ones folded, in its own `sync-history.json`); Syncthing: `SyncthingClient` (REST status), conflict copies, `.stignore` template |
| `reminders` (util, text, dates, markdown, index, vault) | `upcomingNotifications()` (stable ids, recurrence expansion, done tasks skipped, `taskLine` — the task a notification's "Done" completes); `NotificationSink` (implemented by the app's notifiers); `ReminderScheduler` (in-app timing for platforms without background alarms, missed-reminder detection); `calendarItems()`; `snoozeReminder()` |
| `notebook` (util, text, dates, markdown, vault, index, reminders) | what the app and the command line do to a notebook, so both behave identically (Workspace delegates here, `brulion-cli` is built on it): quick capture lines (`capture.h`: `buildCaptureLine`), journals (`journals.h`: `journalFileFor` under any accepted name, `ensureJournal` from the template — never over a note another program just created —, pure `appendBlock` / `blockFromText`), pages and folders (`pages.h`: `newPagePath`, `createPage` / `plannedPage` with letter-case twins, `createFolder`, `folderInfo`, every rename and move through `relocate()` on `VaultIndex::planRelocation()` with `.brulion/config.json`'s special folders rebased, `deletePath`; problems as `PathProblem` kinds the app turns into its messages), the links the editor, the page menu and the CLI copy (`links.h`: `pageLinkTarget` — a daily note's file name, else `VaultIndex::linkTarget` —, `blockLinks`, `copiedLink` — the link menus' "Kopiuj link" of a rendered link), item references (`itemref.h`: `Dom.md#L12@hash`, `^id`; `resolve()` → Ok / Moved / Stale), JSON of the model (`json.h`, also the names of the JSON export), what other apps share ("Udostępnij → Brulion", the web share target, files opened with Brulion on a desktop; `share.h`: `sharedContentFromJson` — the platform's JSON read defensively — and `sharedContentToJson`, `sharedContentFromQuery` — the share target's form-encoded query —, `splitOpenArguments` / `openedFilesShare` / `mergeOpenedShares` — `--open` files, Windows' one launch per file merged —, `pageForOpenedFile` — an opened file that already is a page of the notebook —, `composeSharedText` — "[Title](url) — text" —, `checkSharedFile` / `looksBinary` — text/* only, `SharedFileMaxBytes` —, `decodeSharedBytes`, `sharedPageTitle` / `sharedPageContent` for a new page and `sharedFileBlock` for one journal block per file), and `Notebook` (`session.h`: a vault opened headless — a missing folder is never created —, a lazy index kept up to date, the device's write lock, `edit()` = intent + compare-and-swap retries + History reason "cli"; `deviceStatePath()` shared with the app), `IndexLoader` (`indexloader.h`: opens an index — reads (`Vault::readDetached`), decodes, takes notes from the index cache or parses them on the `WorkerPool`'s threads, builds the full-text index on one more, merges on the owner's thread in slices (`step()`, `finish()`), journals first, back-pressure, `forget()` for files changed meanwhile; inline without threads) and `IndexCacheKeeper` (`indexcachekeeper.h`: `<device state>/index-cache.bin` — in the browser in a cache mount of its own — written after a load that parsed and after quiet changes, encoded in slices, written with `QSaveFile` on a pool thread; none under 200 notes; `indexCacheBudget()` caps it where storage has a quota) |
| `io` (util, text, dates, markdown, index, notebook, drawing; QtGui) | importers (Obsidian, Logseq, Notion, Evernote ENEX, Google Keep, Microsoft To Do (Graph JSON: the `mstodo` download or a Graph Explorer file; lists → pages of tasks, dateTimeTimeZone values converted to the vault's zone), Joplin, Simplenote, Roam, Trilium (a canvas note → one drawing SVG: Trilium's picture with the Excalidraw scene embedded, `drawing::svgWithScene` — a scene Brulion only shows as Trilium stored it, in its order —, the pictures pasted into it in the scene's files while their data URLs fit a scene; else `drawing::toSvg`), Bear, Markdown/TXT, HTML, iCalendar) with auto-detection and safe target paths; exporters (Markdown ZIP, Obsidian, Logseq, HTML site, single HTML, Pandoc Markdown, JSON, iCalendar, OPML, CSV tasks; a tag links to the page it names in HTML and Pandoc and is written `#[[Namespace/Page]]` for Logseq; `HtmlRenderer` and `PandocRenderer` share inline atoms and emphasis pairing in `rendercommon_p.h`); ZIP (vendored `3rdparty/miniz`), TAR, CSV, iCalendar; HTML→Markdown via `QTextDocument` (also pasted HTML: `EditorHost::htmlToMarkdown`); `clipboardHtml` — copied blocks as escaped semantic HTML for the clipboard's text/html (`EditorHost::renderHtml`; pictures as links, never data: URLs; dates of any block; `ClipboardHtmlMarker`: a paste recognises Brulion's own copy and takes its Markdown); pictures from the internet in the HTML and Pandoc exports: `webPictureTargets` (the URLs they show) and `ExportInput::webPictures` (copies the app supplies from its cache: `assets/web/…` files or data: URIs; the exporters never touch the network) |
| `mstodo` (sync, log; QtNetwork) | Microsoft To Do download for the import: `DeviceCodeLogin` (OAuth 2.0 device-code sign-in at login.microsoftonline.com/{authority}/oauth2/v2.0 with the user's own public-client app registration, scope `Tasks.Read`, polling with `authorization_pending` / `slow_down`, AADSTS answers explained, no refresh token), `TodoDownload` (Graph `/me/todo/lists`, every list's tasks with `$expand=checklistItems,linkedResources` — task by task when Graph refuses it or leaves the expansion out —, attachments within a size budget, `@odata.nextLink` paging only on graph.microsoft.com, `Retry-After` on 429/503, a lost connection or an expired sign-in stops it) → the JSON bundle the io importer `mstodo` reads; HTTP only through sync's `HttpTransport`, the token in memory and registered with `log::redact` while in use, nothing stored |

## Platform layer (`src/platform/`, library `brulion_platform`)

Only what needs OS APIs; compiled per target (no `#ifdef`s in core). `platform.h`:
`targetName()` ("linux" | "windows" | "android" | "wasm").

* **Everywhere** (`platform.h`): `logDirectory()` (Linux `$XDG_STATE_HOME/Brulion/Brulion/logs`,
  Android the app's files dir + `/logs`, the browser none: memory only) and `deviceDescription()`
  for the event log; `allowForegroundActivation()` — Windows: `AllowSetForegroundWindow` before a
  second launch hands its files or request to the running Brulion, so its window comes to the
  front (nothing elsewhere); `endStartupNotification(id)` — X11: such a launch ends the
  launcher's startup notification (`StartupNotify=true`) itself, through Qt's xcb plugin, which
  says "remove" once the process shows a window — a 1×1 tooltip window off the screen (nothing
  elsewhere).
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
  "Udostępnij → Brulion"), "Zrobione" taps (`DoneQueue.kt`, persisted while Qt is not running or
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
  settings — the cache's files are dropped and `cacheNotSaved()` stops the keeper), loads them
  before the app opens anything and writes changes back per mount (debounced, on page hide);
  `WasmNotifier` (Notification API, service-worker fallback, clicks → `activated`);
  `WasmShare` (the share target launched in another tab while Brulion runs here: `wasm/index.html`
  posts the launch's query on the BroadcastChannel `brulion-share:<site path>`, the tab holding the
  storage lock answers and emits `received(query)` → `AppController::receiveShareQuery`; the
  listener is installed before the storage asks for its lock, queries before the controller exists
  are kept; docs/WEB.md "Udostępnianie do Brulionu");
  `wasmshell.h` (`setShellStage`/`hideShellLoadingScreen`: the loading screen of
  `wasm/index.html`; `watchVisualViewport`: readings of the browser's viewports and Qt's container
  for the on-screen keyboard — the computation is `ViewportKeyboard` in `ui/keyboardinsets.h`,
  unit-tested); `wasmselftest.h` (`runStorageSelfTest`/`runNotifierSelfTest` for
  `?selftest=storage,notify`, `publishSelfTestResult` — also the share dialog's data for
  `?selftest=share` —, driven by `scripts/wasm-smoke.mjs`). Self-tests that need the running app
  live in the app: `src/app/diag/webselftest.h` (library `brulion_webselftest`, linked into the
  browser build only; `?selftest=webpictures`: pictures from the internet as the editor fetches
  them and an export with copies, `?selftest=pen`: the drawing canvas under a pen), driven by
  the Playwright tests in `tests/web`.

The notifier adapters themselves live in the app (`src/app/notify`, below), because they also
talk to the tray and the settings.

## App (`src/app/`, QML module `Brulion`)

`main.cpp` creates the `QApplication` (desktop: tray) / `QGuiApplication`, the translator,
the fallback fonts for emoji and symbols (`installFallbackFonts()`: always in the browser, whose
Qt has only DejaVu Sans/Mono; on Linux only without a system emoji font, else just the symbol
font for fontconfig's fallbacks; never on Android —
docs/UI.md "Emoji and symbol fonts"), `DevTools` and `AppController`, then loads `Main.qml` (or
a dev tool window). Each area is a
directory with an `area.cmake` (included by `src/app/CMakeLists.txt`) and, for its C++, a static
library that tests link without the app; classes reach QML through `QML_FOREIGN` wrappers
(`qmlforeign.h`, `editorforeign.h`, `shellqml.h`, `contentqml.h`, `planningqml.h`, `ioqml.h`).

| Area | C++ (main classes) | QML |
|---|---|---|
| `ui` — design-system helpers | `DatesHelper` (natural dates, formatting, recurrence, month grids), `TextHelper` (folding, fuzzy ranking, escaping), `DiffHelper` (line/word diffs, conflict hunks), `KeyboardInsets` (the on-screen keyboard's overlap of the window, one source of truth for the shell, the docked editing toolbar, sheets and toasts; its platform sources connected by the app's `keyboardsources.cpp`), `GestureInsets` (the system's gesture zones at the window's edges — Android's gesture navigation — for the drawer's edge swipe), `ThemePalette` (the colours the appearance settings change: surfaces, accent presets and custom accents made readable by WCAG contrast, text colours; the font pickers' family filter) in `brulion_ui_helpers`; `TouchFlick` (touch flicks at the finger's speed on Android) in `brulion_ui_touch`; `RowScroll` (the scroll bar of long lists from the rows at the view's edges, for `BRowScrollBar`) in `brulion_ui_scroll`; the QML engine's local-only network access (`LocalOnlyNetworkFactory`) in `brulion_ui_network`; `installFallbackFonts()` + the bundled emoji/symbol fonts (`assets/fonts`, `fallbackfonts.h`) in `brulion_ui_fonts`; app-only `DeviceInfo` (also the installed font families), `DevTools` (dev/test options) | `qml/theme/Theme.qml`, `qml/components/B*.qml`, `qml/dev/Gallery.qml` |
| `editor` — block editor | `EditorHost` (contract with the shell — `assetUrl`, `drawingUrl` (a drawing's preview in the app's theme), `isDrawing`, … —; header-only, moc'ed once in `brulion_editorhost`, which also has `picturesize.h`: a picture's size from its header or an SVG's root, for the editor and the browser's data: URLs, whether the device decodes its format, and whether a file is a drawing — `localFileIsDrawing`, its name and first 4 KB: an SVG, a `.excalidraw`, an Obsidian Excalidraw plugin `.md` —, the size of a `.excalidraw` or plugin drawing's preview (`localDrawingSize`) and the plugin's export beside one (`drawingExportOf`) — the picture view and the embeds name one it cannot; the clipboard's `renderHtml` / `htmlToMarkdown`, implemented by `Workspace` in `workspaceclipboard.cpp` with core io), `QueryHost`, `PageController` (commands, undo, debounced saves, external-change merge, note mode: outline / document; the block selection in `pagecontroller_selection.cpp`: anchor and focus uids, copy / cut / delete / indent / move / paste over it, clipboard writes with the size limit and read back; `LazyHtmlMimeData`: the copy's text/html rendered only when an application asks for it, at once on Android, in the browser and on Windows), `StreamSelection` (a block selection over several days of the journal stream: paths and rows, each attached day's part, copy with the days' links; the days not attached read from their files), `BlockListModel` (the selection as a uid set with a `selectionEdge` role), `MarkdownHighlighter`, `SuggestionModel`, `QueryResultModel`, `EditorInput`, pure `blocktext.h`, `FakeEditorHost` (playground, tests) | `qml/editor/*` (BlockEditor — keys, the drag-selection tracker, the touch selection mode —, delegates for calc/code/table/query/embeds, BlockMenu — every block's menu with Kopiuj / Wytnij / Zaznacz and "Kopiuj odnośnik" —, SelectionToolbar, TaskBox and TaskStatusMenu — the task checkbox and its status menu, used by every view that shows tasks —, MobileEditToolbar — docked on the keyboard at the bottom of the editor's `toolbarHost`, also the selection's tools —, dialogs), `qml/dev/EditorPlayground.qml`, `qml/dev/SelectionSelfTest.qml`, `qml/dev/PageScript.qml` |
| `shell` — app frame | `AppController` (lifecycle, vault, navigation history, side panel, permissions, dev options; `appshare.cpp`: `receiveShare` / `receiveShareQuery` / `pendingShare` / `saveShare` / `dismissShare` — a share from another app, the web share target or files opened with Brulion (`--open`, a second launch's `share` message; source "open": files without a type typed by `QMimeDatabase`, merged while the dialog waits, one that is a page of the open notebook opens as that page, copies saved and the files never changed) waits for a user notebook, goes to today's journal or new pages through `notebook/share.h`, its copies removed only inside `<dataDir>/share-in`), `AppSettings` (QSettings + owner-only secret file; the Nextcloud switch and server folder per notebook under the id of its folder — `Vault::idForPath`, as its device state —, the settings of 0.3.0 and older handed to the notebook that was current then or whose sync bookkeeping names their account and folder, `notebookOpened`), `Workspace` (owns `Vault` + `VaultIndex`; implements `EditorHost`/`QueryHost`; pages, history, trash, journals, quick capture (`notebook/capture.h`); folders and moves in `workspacefolders.cpp`: `movePath` / `renamePage` / `renameFolder` through one `relocate()` — the index's plan, `Vault::rename`, the planned notes written once each, the special folders of `.brulion/config.json` rebased —, `moveImpact` (kept per target until the
notebook changes) and `requestMoveImpact` (the dragged row's ghost: planned 8 ms per event-loop turn,
`moveImpactReady`), `folderPaths`, `folderInfo`, `subfolders`, `folderRenamed` for routes and the tree) with `WorkspaceReminders` (the scheduler over the index; snooze and a notification's or the missed banner's "Done" written into the note — the occurrence looked up again by id, the edit written compare-and-swap against the indexed content so a stale index never edits another line; notifications carry the notebook's id, one from another notebook is refused; Android's queued "Done" stays in its persisted queue until that notebook is indexed, `doneTarget`) and `WorkspaceSync` (+ `workspacesyncruns.cpp`: the throttled `live` progress, `history`, `runDetails`; a notebook whose server folder another notebook of the device syncs with — `NextcloudSettings::sharedWith` from `AppSettings::nextcloudFolderUser`, left by 0.3.1–0.3.5 — syncs nothing and says why), wired by `workspaceintegration.h`; journals, quick capture, pages, folders and moves delegate to the core module `notebook` (the Workspace keeps the translated messages, notices and signals); `RouteHistory` (`navigation.h`), `CommandModel`, `FolderTreeModel` (the sidebar's tree: lazy, applied as inserted and removed rows, Polish order, the journals folder pinned), the tray calendar's placement and reopen guard (`traypopup.h`, pure; `ShellTrayCalendar`), `DemoVault` ("Przykładowy brulion"; the separate notebook "Dokumentacja Brulionu" is made of Brulion notes in `shell/guide/*.md`, embedded as resources and brought up to date on every open by `updateDocsVault`, which records the hash of every written page in the notebook's LocalStore and recognises 0.3.0–0.3.5 pages by their known hashes, so pages the user changed or deleted stay so; the documentation is never a recent or start notebook (the start drops it from the recent notebooks of 0.3.0–0.3.5) and arms no reminders — `Workspace::setRemindersPaused` keeps the user notebook's armed set; the sidebar button next to the notebook name switches notebooks, `openRecentVault` never recreates a missing folder), `DesktopTray` (`SingleInstance` is in `src/ipc`; `AppController::handleInstanceMessage` takes its messages — unknown verbs change nothing — and `openRoute` shows a route asked for from outside, brulion-cli `app open`, also once a notebook still opening is ready); dev: `ShellSelfTest`, `ShellJourney`, `RemoteControl` (`--remote-control`: AI agents drive the running app over a control socket, docs/CLI.md), all three on the UI driver (`uidriver.h`, `brulion_uidriver`: real input, item finding) | `qml/Main.qml`, `qml/shell/*` (AppShell, the tray calendar `ShellTrayCalendar`, sidebar with the tree's menus and drag and drop, top bar, bottom nav, route view, dialogs — `ShellMoveDialog` for "Przenieś do…", `ShellShareDialog` for shares), `OnboardingView`, `SettingsView` |
| `notify` — notifier adapters (`brulion_notify`) | `Notifier` (a `reminders::NotificationSink` + signals; `createPlatformNotifier()`), `DesktopNotifier` + `DBusNotificationBus` (snooze/done/OK actions — `doneRequested` only for a task's reminder —, ringing alarms, tray fallback, sound hints) or, on Windows, `WindowsToastBus` (C++/WinRT toasts; the XML and the activation arguments in the pure `toastxml.h`, tested everywhere; a bus that `ringsItself()` per message: alarm toasts with a looping sound loop it and get one silent copy when the ring time is over, alarms with a short sound are sent again every ring interval as on Linux; available only while the AppUserModelID is registered and Windows does not turn Brulion's toasts off (an unknown setting before the first toast counts as allowed, `toastsAllowed()`); its own sounds — `ms-winsoundevent` names — and no sound files; toasts cleared at quit), `AndroidNotifier` + `androidpayload` (JSON for the bridge and its queued "Zrobione" requests, tested on the desktop), `WasmNotifierSink`; `ReminderSounds` + `soundchoice.h` (the sounds of reminders and alarms: device-local in AppSettings, per platform — Android ringtones on versioned notification channels, freedesktop theme sounds / files, the browser's tone); dev: the settings self-test (`notifyselftest.h`) | `qml/notify/ReminderSoundSetting.qml` (in `SettingsView`) |
| `content` — reading views | `WorkspaceListModel` base (diffed updates), `JournalListModel`, `BacklinksModel` (links and tags naming the page) / `TagBlocksModel` (`GroupedRefsModel`), `TagListModel`, `PageListModel`, `SearchResultModel`, `HistoryModel`, `OutlineModel`, `PageTools` | `JournalsView`, `PageView`, `AllPagesView`, `TagsView`, `SearchView`, `TrashView`, `HistoryView`, `qml/content/*` (side panel, backlinks, dialogs) |
| `planning` — calendar, tasks, graph | `CalendarModel` (month/week/agenda), `TaskListModel` (sections, filters run by the query engine), `ForceLayout` (Barnes–Hut) + `GraphModel` + `GraphItem` (painted, zoom/pan/pinch), `PlanningActions` (task toggles with undo, journal appends), `PlanningContext` | `CalendarView`, `TasksView`, `GraphView`, `qml/planning/*` |
| `widgets` — Android home-screen widgets (`brulion_widgets`) | `buildSnapshot()` (`widgetsnapshot.h`: TasksView's open tasks with their raw dates and every reminder of their block — filed by `TaskListModel::filingDate`/`nextReminder`, which Kotlin mirrors for a later day —, CalendarModel's agenda, favourite and recent pages, where quick add writes; plain JSON, built and tested on every platform), `widgetactions.h` (the lines of quick-add entries, the queued ones the app writes); Android only: `WidgetPublisher` (created by main.cpp: publishes the snapshot of the notebook whose reminders run — debounced, when the app leaves the front, at midnight, empty on onboarding — and writes queued entries into today's journal, putting back those it cannot write). The only Kotlin write into a notebook is the widgets' quick add with the app closed (`JournalAppend.kt`: the same line and append rules, checked against the same golden files as the C++; atomic, only into an existing notebook folder and inside it, compare-before-rename; `Workspace` likewise never appends to a daily note that is not UTF-8) — waiting for Qt to start would take seconds and show the UI, and the note changes like a Syncthing change would | — (Kotlin: `android/src/…/Widget*.kt`, `QuickAddActivity.kt`) |
| `io` — sync center, import/export (`brulion_ioviews`) | `ImportController`, `MsTodoImport` (the Microsoft To Do card's sign-in: client id and authority in `AppSettings`, core `mstodo`, the bundle handed to `ImportController::setInput`; other input reaching the controller cancels the sign-in), `ExportController`, `FileGatherer` (listing and reading off the GUI thread) / `DocumentProvider` (Android `content://` documents and document trees) / `writeToUrl` (`fileexchange.h`), `NextcloudSetup`, `SyncthingSetup`, `ConflictResolver` (merge3 hunks, base from History; drawings: both versions and their union — `drawing::merge` with no common version — for the previews, "Scal rysunki" through `WorkspaceSync::resolveConflictBytes`; an Obsidian plugin file's notes around the scene must survive — `drawing::keepsMarkdownOf` — or the copy stays); dev: the import self-test (`ioselftest.h`) | `SyncView` (live progress, the history of runs and their details: `SyncLiveProgress`, `SyncHistoryList`, `SyncRunDetails`), `ImportExportView`, `qml/io/*` |
| `drawing` — the drawing editor (`brulion_drawing`) | `DrawingCanvas` (`QQuickPaintedItem`: shows a core `drawing::Drawing`; stylus pressure and its eraser end from the window's tablet events — the canvas takes them when it is the topmost item under the pen —, mouse, fingers — drawing or moving, a pinch —, the wheel and touchpad pinches; pen, highlighter, element eraser, and in `drawingcanvas_tools.cpp` the selection — lasso or tap, moving, `deleteSelection`, `duplicateSelection`, `recolorSelection`, `reorderSelection`, `nudgeSelection` —, the shapes and text — `textEditRequested` / `commitText` with QML's editor —; the paper's pattern, its light or dark (`theme`, `appDark` from QML's `Theme.dark`, `dark`), undo / redo of core `ElementDiff`s, one per gesture or action; `load` / `fileBytes` — the file in its own format, `format` `svg` | `excalidraw` | `excalidrawMd`, core `drawing::writeDrawingFile` — / `svg` / `takeExternal` — a version written elsewhere merged with `drawing::merge`, a gesture in progress kept, a plugin file's Markdown theirs —; `interactive: false` — a preview, the sync center's: no input, the whole drawing in view —; palm rejection while the pen is down; every element prepared once by version, nonce and `updated` (a drawing's hatch lines within `drawing::DrawingHatchLines`), a cached image of the view rendered on the GUI thread, `paint()` drawing a snapshot taken in `updatePaintNode()`), `drawingrender.h` (the paper and the elements in the light or the dark theme — `paintPaper`, `prepare` / `paintElement`, `renderDrawing` —, the fonts of text `setTextFonts` / `textMetrics`, Excalidraw's dark filter per pixel `darkImage`; shared by the canvas and the previews), `DrawingImageProvider` (the QML image provider `drawing`, registered in `main.cpp`: `image://drawing/<path>?v=…&dark=…` — the previews of drawings in notes, the picture view and the editor's notice, `EditorHost::drawingUrl` / `Workspace::drawingUrl`; stateless, in Qt Quick's image reader thread; a drawing in the frame its file names, as the embeds are sized; Qt SVG + `darkSvg`'s colours for files Brulion does not draw; `.excalidraw` and Obsidian Excalidraw plugin files too — one Brulion does not draw by the plugin's export beside it or its elements with dashed boxes) | `qml/drawing/DrawingView.qml` (route `drawing`: the tools, autosave through `Workspace.readAttachment` / `writeAttachment` — the file read before each save, a change not reported yet taken in —, external changes — one it cannot edit shown, the strokes drawn here kept beside it, `Workspace.createAttachment` —, renames, the notices of files it does not edit), `DrawingToolbar`, `DrawingSelectionBar` (the selection's colours, duplicate, front, back, delete), `DrawingTextEditor` (text and labels written on the paper); `drawingqml.h` registers `DrawingCanvas` and the `DrawingFonts` singleton |
| `diag` — the event log (`brulion_diag`) | `LogController` (QML singleton `Logs`: copy, share, save, clear, the export header, the detailed mode), `LogModel` (the Logger's entries filtered by area, level, diacritic-insensitive search and a time window, following new ones), `logexport.h` (header, file name, clipboard cap) | `qml/diag/LogsView.qml` (route `logs`), `LogRow` |

Routes and views: docs/UI.md "Navigation". Area tests: `tests/app/<area>/` (C++ unit tests plus
QML smoke tests and screenshot scripts that run the real app headless).

## Command line (`src/cli`) and IPC (`src/ipc`) — desktop only

* **`brulion-cli`** (docs/CLI.md): headless commands over a notebook folder for scripts and AI
  agents — pages, folders, journals, capture, tasks, reminders, query, search, tags, backlinks,
  calendar, dates, calculations, block links, history —, JSON envelopes and stable exit codes,
  `brulion-cli mcp` (MCP over stdio, tools only, the index kept warm). One registry
  (`registry.cpp`) gives the argv parser, the help, the JSON Schema and the MCP tools. Built on
  `core/notebook` (never on the app's shell): it never creates a `ReminderScheduler`,
  `WorkspaceSync` or `AppSettings`, reads the app's settings file only for the current notebook
  and language, and shares the device state (History, LocalStore) with the app on one computer.
  Writes: the device's lock, compare-and-swap with retries, History reason "cli" (the app's
  history shows "Zastąpiona z wiersza poleceń (agent)"). The app's `main()` hands `brulion cli …`
  over before anything else (the AppImage's one entry point). CMake `BRULION_BUILD_APP=OFF` builds
  only the core and the command line (no Qt Quick/QML/SVG): the Nix package `brulion-cli`.
* **`brulion_ipc`**: `SingleInstance` (one app per session over a `QLocalServer` in
  `$XDG_RUNTIME_DIR`, a lock file decides the primary; only in a folder of the user's own —
  `socketFolderProblem()`, never the shared temp folder: without one the app runs without a
  single instance and the event log says why — and both ends talk only to this user's processes,
  `trustedPeer()` of the peer's `SO_PEERCRED`; on Windows a named pipe with an access list of the
  user, its key and lock file in `%TEMP%`, the peer process's SID compared) and the messages of
  its socket
  (`appmessage.h`: `activate`, `capture`, `today`, `open {"route", "vault"}`, `hidden` (a second
  launch with `--hidden`, e.g. Windows' start at sign-in while Brulion runs: no window), `share
  {"share"}` (a second launch with `--open <file>…`: the files as share JSON, absolute paths; the
  receiver drops its "dir") — one-way lines a second launch or `brulion-cli app open` sends; a
  second launch adds its `activationToken`, the launcher's `XDG_ACTIVATION_TOKEN`, which the app
  presents when it raises its window on Wayland); the **remote control** of the running app
  (`controlprotocol.h`: JSON-line requests and responses, the methods and their parameters;
  `controlserver.h`: a second socket, `<key>-control`, owner-only, other users' connections
  refused; `controlclient.h` for `brulion-cli app …`); `profile.h`: the app's names shared by the
  app and brulion-cli, `BRULION_PROFILE=<name>` for a separate profile (test runs on Windows). Off unless `--remote-control` or the device
  setting `devtools/remoteControl`; the app's side is `src/app/shell/remotecontrol.h`, its input
  and item finding the UI driver `src/app/shell/uidriver.h` (library `brulion_uidriver`), shared
  with the shell self-test and the journey. Nothing nested: gestures play from timers, waits
  poll; see docs/CLI.md "Remote control".
* **Concurrent writers** (the app, the command line, Syncthing, Nextcloud): every write is atomic
  and snapshots what it replaces; the editor's saves and the command line's edits are
  compare-and-swap (`Vault::writeTextIfUnchanged`): a change another program wrote a moment ago is
  merged (editor) or the intent applied again (command line), never overwritten.

## Logging (the event log)

Users see it as **"Rejestr zdarzeń"** (route `logs`; English "Event log" — not "Dziennik",
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

## Data safety

Notes may come from anywhere (imports, sync, other editors) and storage may disappear under the
app; these guards keep data from being lost:

* **Vault storage checks** (`Vault::scanStatus()`, `isStorageAvailable()`): a missing vault
  folder (unmounted SD card), a folder that now lies on another storage than at `open()` (an
  empty mount point) or folders that cannot be listed are never turned into deletions; writes
  fail while the folder is missing or on another storage, so nothing lands on an empty mount
  point. Files changed on disk behind the app are taken in (and kept in History) before an
  overwrite; every overwrite is atomic (`QSaveFile`) and snapshotted into History first.
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
* **Editor**: unsaved edits are merged three-way with external changes; an unresolved conflict
  is saved as a conflict copy, never over the other version. Deleting moves to `.trash/`.
* **Browser**: IndexedDB write-back never starts before the initial load succeeded; private
  mode / blocked storage is reported before the user writes anything.
* **Rendering**: note text reaches QML only through the escaping `InlineRenderer` (or
  `TextHelper`/`DiffHelper` escaping); the calculator never evaluates note text as script.
  Behind that, the QML engine's network access manager serves local URLs only (`file:`,
  `qrc:`, `data:`; `LocalOnlyNetworkFactory` in `src/app/ui/localnetwork.h`; any other URL gets
  a transparent pixel, never a request): nothing in QML loads from the network, so even a
  rich-text `<img>` that slipped through cannot make a synced or imported note contact a server.
  Pictures from the internet (`![x](https://…)`) are fetched by the Workspace itself
  (`workspacepictures.cpp`, core `webpicture`) only when the user allows it — "Pokaż" on the
  picture, the device setting `webPicturesShown` (off by default) or "Zapisz w notatniku" — and
  shown from the device's cache as a local file: http(s) only, no cookies, at most 20 MB, the bytes
  checked to be a picture. The setting never fetches from this device or its private network
  (a synced note must not send requests to the router or printer), and no redirect leads there
  from a host of the internet. Indexing, sync and import never fetch them; an HTML or Pandoc export
does only with "Dołącz kopie" (`ExportController`: the cached copies, read in chunks, the
missing ones fetched through the same Workspace, explicitly asked for like "Zapisz w notatniku" —
but never from this device or its private network, which a note alone must not make it ask; at
most 256 MB of copies in all, the rest keep their addresses). In the browser the
fetches are CORS "simple" requests the browser itself sends (and redirects itself: the manual
redirect check does not apply there); a failed one is told as CORS or mixed content.

## Build, test, packaging

* CMake ≥ 3.24 + Ninja, C++20, `qt_standard_project_setup(REQUIRES 6.8)`, `qt_add_qml_module`;
  `QT_NO_KEYWORDS`, `QT_NO_CAST_FROM_ASCII`. Warnings: `cmake/BrulionWarnings.cmake`
  (`-DBRULION_WERROR=ON` in CI and the dev scripts). Translations: `qt_add_translations`
  (`i18n/brulion_pl.ts`, `brulion_en.ts` for plurals).
* Tests: QtTest via CTest (`brulion_add_test`), core in `tests/core/<module>`, app areas in
  `tests/app/<area>` (`QT_QPA_PLATFORM=offscreen`), the command line in `tests/cli` (the real
  `brulion-cli` on temporary notebooks, also its MCP server) and the socket in `tests/ipc`, plus headless QML checks of the real app:
  `tst_shell_qml` (`--selftest-shell`), `tst_shell_journey*` (`--journey`), `tst_*_qml` smoke
  tests; shell scripts in `tests/scripts` (`tst_scripts_release`: `scripts/release.sh` in
  throwaway git repositories; `tst_scripts_appimage_update_check`). Development builds:
  `scripts/remote-build.sh` (x86_64 laptop) or
  `scripts/dev-build.sh` (locked, `-j2`) — see CLAUDE.md. Sanitizer builds (`-fsanitize=` in
  `CMAKE_CXX_FLAGS`, CI) get `BRULION_TEST_SLOWDOWN=25`: tests multiply their wall-clock limits
  and worker-thread stack sizes by it (1 otherwise), the editor playground's self-tests their
  waits for something asynchronous (`DevTools.testSlowdown`); CI builds them at `-O1`. The GUI
  tests run side by side with the others (`PROCESSORS 2` for a whole app's self-test): their
  checks wait for the state they read, never for a fixed time; only measurements that compare
  wall-clock times run alone (`RUN_SERIAL`: `tst_shell_workspace_perf`, `tst_scroll_jumps_qml`).
  A fresh build directory starts the longest tests first (`tests/ctest-costs.txt` becomes
  ctest's cost data until ctest has its own timings).
* Nix: `nix develop` (Qt 6.11 from nixpkgs, CMake, Ninja, ccache, clang-tools, gdb);
  `nix build .#brulion` (builds without the tests — doCheck = false —, validates the desktop file and AppStream
  metainfo, runs `brulion-cli`); `nix build .#brulion-cli` (the command line alone, `nix/cli.nix`); NixOS module `programs.brulion` (+ autostart hidden in the tray) and
  `services.brulion-web` (the browser version behind nginx) — docs/NIXOS.md; `nix flake check`.
* Windows (test version): `scripts/windows-build.ps1` — MSVC 2022 (`/utf-8`, `/permissive-`,
  `/Zc:__cplusplus`, `/bigobj`, `/W4` without `/WX`), Qt 6.11.2 `win64_msvc2022_64`, `windeployqt`
  + the app-local MSVC runtime → portable ZIP + Inno Setup installer (`packaging/windows/`: the
  `.iss`, the icon, the executables' version resources); the desktop integration
  (`BRULION_DESKTOP`: tray, single instance over a named pipe of this user, brulion-cli, the QML
  self-tests) as on Linux; `BRULION_PROFILE` (src/ipc/profile.h) isolates test runs there; the
  toast bus uses the Windows SDK's C++/WinRT headers and `WindowsApp.lib`; the installer has the
  task "autostart" (the Run value) and the AppUserModelID on its shortcuts — docs/WINDOWS.md.
* Linux: `scripts/appimage-build.sh` (Qt 6.11.2 from aqt, linuxdeploy) → AppImage x86_64 +
  aarch64, each with a `.zsync` file for AppImageUpdate (checked before publishing by
  `scripts/appimage-update-check.sh`); desktop entry and metainfo in `packaging/`.
* Android: `scripts/android-build.sh` — Qt 6.11.2 for Android from aqt with host tools of the
  same version, NDK r27c, JDK 21; ABIs `BRULION_ANDROID_ABIS` (default arm64-v8a; armeabi-v7a,
  x86_64, x86 → one multi-ABI APK); `android/` holds the manifest, resources and Kotlin
  sources (+ Kotlin unit tests); signed APK in GitHub Actions — docs/ANDROID.md.
* WebAssembly: `scripts/wasm-build.sh` — Qt 6.11.2 `wasm_singlethread` + matching emsdk; our
  HTML shell, manifest and service worker in `wasm/` (offline PWA); deployed to GitHub Pages —
  docs/WEB.md. Browser tests of the built site: `scripts/wasm-smoke.mjs` (Chrome over a
  dependency-free DevTools client) and `tests/web` (an npm project CMake ignores: Playwright in
  Chromium and Firefox — pictures from the internet, the pen —, `scripts/web-test.sh`).
* CI (`.github/workflows`): `ci.yml` (Nix build + flake checks on x86_64/aarch64, aqt build
  with ASan/UBSan and `-Werror`, actionlint/shellcheck/clang-format), `android.yml`,
  `appimage.yml`, `windows.yml`, `wasm.yml` (Pages, after the smoke test, the Android emulator's
  browsers and the Playwright tests — job `browsers`), `release.yml` (tag `v*` → draft release with
  APK, AppImages, the Windows installer and ZIP, web ZIP, sources, checksums) — docs/RELEASE.md.
