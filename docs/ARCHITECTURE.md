# Brulion — architecture (C++20 / Qt 6)

Brulion is an offline-first Markdown notes app: Obsidian-style plain files, a Logseq-style
block outliner with daily journals, calendar, reminders and alarms, calculations and tables,
live queries, sync through Syncthing (plain folder) or Nextcloud (WebDAV), import/export from
other apps.

One C++/QML code base, three targets, all built against **Qt 6.11.2** (CMake accepts ≥ 6.8):

| Target | How | Vault storage | Reminders |
|---|---|---|---|
| **NixOS / Linux desktop** | Qt from nixpkgs: flake package + NixOS module; AppImage (Qt from aqt) for other distributions | a folder (e.g. a Syncthing folder) | `org.freedesktop.Notifications` over D-Bus, tray as fallback; timed in-app (`ReminderScheduler`), the app can live hidden in the tray (`--hidden`, autostart) |
| **Android** (9+, API 28–36) | Qt for Android (aqt) + `androiddeployqt`, signed APK from GitHub Actions | a folder on shared storage (Syncthing-Fork; "All files access") or app storage + Nextcloud | native `AlarmManager` alarms (Kotlin via JNI) — fire while the app is closed and after a reboot |
| **Browser** | Qt for WebAssembly (single-threaded) + emsdk, offline PWA shell (`wasm/`) on GitHub Pages | Emscripten IDBFS (IndexedDB) + import/export, Nextcloud sync | web Notification API (service worker where needed) while the page is open |

On the desktop a command line for scripts and AI agents comes with it: `brulion-cli` (also
`brulion cli …`), with an MCP server (`brulion-cli mcp`) — see "Command line" below and
docs/CLI.md.

## Layers

```
┌──── src/app  (executable "brulion", QML module "Brulion") ─────────────────────────────────┐
│ QML: qml/theme, qml/components (B* design system), qml/shell, qml/views, qml/editor,       │
│      qml/content, qml/planning, qml/io, qml/dev (gallery, editor playground)               │
│ C++ area libraries: brulion_ui_helpers · brulion_ui_touch · brulion_editor                 │
│      (+ brulion_editorhost) · brulion_shell · brulion_notify · brulion_content ·           │
│      brulion_planning · brulion_ioviews · brulion_diag                                     │
├──── src/cli  (executable "brulion-cli", library brulion_cli; desktop) ─────────────────────┤
│ the command registry → argv parser, help, JSON Schema, MCP tools; `brulion cli …` from the │
│ app's binary; built on core/notebook only (never on the app's shell)                       │
├──── src/ipc  (library brulion_ipc; desktop) ───────────────────────────────────────────────┤
│ SingleInstance (one app per session) and the messages of its socket (app open …)           │
├──── src/platform  (library brulion_platform) ──────────────────────────────────────────────┤
│ Android: AndroidBridge (JNI ↔ android/src/…/BrulionAndroid.kt, ImeInsets.kt)               │
│ WebAssembly: WasmStorage (IDBFS), WasmNotifier, wasmshell (loading screen,                 │
│      visualViewport), self-tests                                                           │
├──── src/core  (brulion_core_<module> libraries; QtCore + QtNetwork, io also QtGui) ────────┤
│ log · util · text · dates · markdown · calc · vault · index · sync · reminders · notebook  │
│ · io                                                                                       │
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

## Core modules (`src/core/<module>/`, namespace `brulion`)

Each module is a static library `brulion_core_<module>` (`brulion_add_core_module(… DEPS …)`
in its own `CMakeLists.txt`, picked up automatically). Its public header — named after the
module (`dates/dates.h`, `markdown/markdown.h`, `index/index.h`, …) — starts with an API
overview; `*_p.h` headers are internal. Tests: `tests/core/<module>/tst_<module>*.cpp`, plus
cross-module scenarios in `tests/core/integration`. Shared data model: `src/core/types.h`.

| Module (deps) | Responsibility (main classes / functions) |
|---|---|
| `log` (—) | the event log (UI "Rejestr zdarzeń", see "Logging" below): the `brulion.*` logging categories and their UI groups, `Entry` + the line format, `redact()` + the registry of live secrets (`ScopedSecret`), `Logger` (chained Qt message handler, ring buffer, rotating file, rate limits, verbosity, export), `SlowScope` / `formatBytes` / `formatMs` |
| `util` | `paths.h`: vault paths (normalize/NFC, join, dirname, basename, `pageTitle`), ignored paths and temp files, Syncthing / Brulion conflict copy names, `sanitizeFileName`, unique paths |
| `text` (util) | diacritic folding (`foldDiacritics`, `foldKey`: "ŻÓŁW" → "zolw"), Unicode word boundaries and regex helpers, EOL-preserving `splitLines`/`joinLines`, FNV-1a `contentHash`, `compareNames` (Polish alphabetical order with numbers by value, no ICU: the index, All pages and the sidebar's tree sort alike on every platform) |
| `dates` (text) | local dates (`DateSpec`, never through UTC); `parseNaturalDate` (Polish + English: "jutro o 9", "w piątek 18:00", "za 2 godziny", "next friday 6pm"); `RecurrenceRule` parse/format/`nextOccurrence`/`occurrencesBetween`, Logseq repeaters; query date expressions (`resolveDateExpr`: `today`, `+7d`, `this-week`, `today..+7d`, `overdue`); human formatting ("dziś 09:00", "pt 17 paź") |
| `markdown` (util, text, dates) | `parseNote` → `ParsedNote` (blocks/outline, links, tags, tasks, reminders, properties, YAML-subset frontmatter, headings, block ids, journal date); metadata tokens; pure edits (`setTaskStatus` incl. the next occurrence of recurring tasks, `removeTaskMarker` for "no status", `renameLinks`, `renameTags` (the tags naming a renamed page), `retargetLinks` / `retargetTags` (links by their range, tags by key: what a move writes), `ensureBlockId`, frontmatter values); tag occurrences with their ranges (`findNoteTags`), the tag naming a page (`tagSyntax`); journals and templates; note modes (outline / document: `resolveNoteMode` from the `brulion:` frontmatter key, journal / page defaults and a content heuristic; `setNoteModeKey`); links to headings and blocks (`linkformat.h`: `headingFragment`, the one heading matcher `findHeading` of links, embeds and copying — Obsidian heading paths included —, `uniqueHeadingFragment`, `formatReference` of the copied `((id))`, `[[Page#^id]]`, `[[Page#Heading]]`, embeds); **`BlockDocument`** (editable block list with lossless round trip: split/merge/indent/outdent/move subtree/toggle task/list item to paragraph); **`InlineRenderer`** → escaped Qt rich text with `page:`/`tag:`/`block:`/`ext:`/`task:`/`reminder:` links and date/reminder/recurrence chips |
| `calc` (text, dates) | own engine (lexer + Pratt parser + evaluator, bounded work, no `eval`): Polish decimal comma, variables (Unicode names), `ans`, percent sugar, units and conversions, currencies as tags, functions, `sum`/`suma` lines; calc blocks, inline `= expr`, page scope (calc variables + numeric properties); GFM tables: parse/serialize/replace and **table formulas** (A1, ranges, `[Kolumna]`, `above`/`left`…, dependency order, `#CYCLE`) |
| `vault` (util, text, log) | `Vault` (folder scan, cached listing, atomic `QSaveFile` writes, `QFileSystemWatcher` on the folders and the files open in editors + rescans (when the app returns to the foreground, every 2 minutes) for external changes, echo suppression by content hash, trash `.trash/`, rename/move, `.brulion/config.json`, storage checks — see "Data safety"); `History` (device-local version snapshots, throttled and pruned); `LocalStore` (small JSON key-value store per vault in AppData) |
| `index` (util, text, dates, markdown) | `VaultIndex` (incremental, coalesced `changed()`: notes, link resolution per SYNTAX §2, backlinks incl. `((id))` refs, unlinked references, block ids, tags — also as links to the pages they name, SYNTAX §3: `resolveTag` (notes only), `tagsNaming`, `tagReferences`, `tagsTo`, resolved once per tag and index version —, tasks, reminders, journals, attachments, page/block suggestions, graph data; moves: `linkTarget` / `tagTarget` (the shortest name reaching a file) and `planRelocation` — the notes to rewrite so every link and tag keeps its target, resolved with the lookup tables of before and of after the move, `relocate.cpp`); `SearchIndex` (BM25+, prefix + fuzzy, diacritic-insensitive, excerpts); the query language (`parseQuery`/`runQuery`, SYNTAX §10) |
| `sync` (util, text, vault, log; QtNetwork) | `HttpTransport` seam + `NetworkHttpTransport` (protocol, transfer progress); `WebDavClient` (PROPFIND/GET/PUT/DELETE/MKCOL, ETags, Basic auth; every request observed: `exchanged`, in-flight / idle time, one `brulion.net` line per request in the detailed log); `SyncEngine` (bidirectional, per-file state + merge bases in LocalStore, conflict copies, deletions, mass-deletion guard; numbered runs with a trigger, phase timings, request statistics, per-file error codes, `activity()`); `merge3()` line diff3 with a union of concurrent appends, `mergeJson3()` for the config; `SyncScheduler` (interval, debounce after edits, online/foreground triggers, retry back-off; `runFinished` for every run; stall watchdog while syncing: "stalled" after 20 s without progress, stopped and retried after 180 s); `SyncHistory` (the last 20 runs, quiet ones folded, in its own `sync-history.json`); Syncthing: `SyncthingClient` (REST status), conflict copies, `.stignore` template |
| `reminders` (util, text, dates, markdown, index, vault) | `upcomingNotifications()` (stable ids, recurrence expansion, done tasks skipped); `NotificationSink` (implemented by the app's notifiers); `ReminderScheduler` (in-app timing for platforms without background alarms, missed-reminder detection); `calendarItems()`; `snoozeReminder()` |
| `notebook` (util, text, dates, markdown, vault, index, reminders) | what the app and the command line do to a notebook, so both behave identically (Workspace delegates here, `brulion-cli` is built on it): quick capture lines (`capture.h`: `buildCaptureLine`), journals (`journals.h`: `journalFileFor` under any accepted name, `ensureJournal` from the template — never over a note another program just created —, pure `appendBlock` / `blockFromText`), pages and folders (`pages.h`: `newPagePath`, `createPage` / `plannedPage` with letter-case twins, `createFolder`, `folderInfo`, every rename and move through `relocate()` on `VaultIndex::planRelocation()` with `.brulion/config.json`'s special folders rebased, `deletePath`; problems as `PathProblem` kinds the app turns into its messages), the links the editor copies (`links.h`: `pageLinkTarget`, `blockLinks`), item references (`itemref.h`: `Dom.md#L12@hash`, `^id`; `resolve()` → Ok / Moved / Stale), JSON of the model (`json.h`, also the names of the JSON export), and `Notebook` (`session.h`: a vault opened headless — a missing folder is never created —, a lazy index kept up to date, the device's write lock, `edit()` = intent + compare-and-swap retries + History reason "cli"; `deviceStatePath()` shared with the app) |
| `io` (util, text, dates, markdown, index, notebook; QtGui) | importers (Obsidian, Logseq, Notion, Evernote ENEX, Google Keep, Joplin, Simplenote, Roam, Trilium, Bear, Markdown/TXT, HTML, iCalendar) with auto-detection and safe target paths; exporters (Markdown ZIP, Obsidian, Logseq, HTML site, single HTML, JSON, iCalendar, OPML, CSV tasks; a tag links to the page it names in HTML and is written `#[[Namespace/Page]]` for Logseq); ZIP (vendored `3rdparty/miniz`), TAR, CSV, iCalendar; HTML→Markdown via `QTextDocument` |

## Platform layer (`src/platform/`, library `brulion_platform`)

Only what needs OS APIs; compiled per target (no `#ifdef`s in core). `platform.h`:
`targetName()` ("linux" | "android" | "wasm").

* **Everywhere** (`platform.h`): `logDirectory()` (Linux `$XDG_STATE_HOME/Brulion/Brulion/logs`,
  Android the app's files dir + `/logs`, the browser none: memory only) and `deviceDescription()`
  for the event log.
* **Android** (`android/androidbridge.h`): `AndroidBridge`, the JNI side of
  `android/src/io/github/januszbit/brulion/android/` (Kotlin: `BrulionAndroid`,
  `AlarmScheduler`, `ReminderNotifier`, `AlarmActivity`, receivers incl. `BOOT_COMPLETED`):
  all-files access, notification permission, exact alarms, full-screen alarm screen,
  `scheduleReminders(json)` (replaces the armed set), notification taps (`activated`,
  `takeActivation()` after a cold start), "Save to Downloads" (MediaStore), `shareFile()` (the
  system share sheet for a file below filesDir through Qt's FileProvider), picked documents
  for imports (`documentInfo` / `listDocuments`: DocumentsContract, callable from worker
  threads) and the on-screen keyboard (`imeChanged`: how much of Qt's view the IME covers, from
  `ImeInsets.kt`, frame by frame while it slides on Android 11+, where the window is drawn behind
  the system bars; for `KeyboardInsets`) with the system bars' icons over that window
  (`setSystemBarsLight`). `android/selftest` builds an APK exercising the bridge.
* **WebAssembly** (`wasm/`): `WasmStorage` mounts IDBFS on the vault and app-data folders,
  loads them before the app opens anything and writes changes back (debounced, on page hide);
  `WasmNotifier` (Notification API, service-worker fallback, clicks → `activated`);
  `wasmshell.h` (`setShellStage`/`hideShellLoadingScreen`: the loading screen of
  `wasm/index.html`; `watchVisualViewport`: the part of the page the on-screen keyboard covers,
  for `KeyboardInsets`); `wasmselftest.h` (`runStorageSelfTest`/`runNotifierSelfTest` for
  `?selftest=storage,notify`, driven by `scripts/wasm-smoke.mjs`).

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
| `ui` — design-system helpers | `DatesHelper` (natural dates, formatting, recurrence, month grids), `TextHelper` (folding, fuzzy ranking, escaping), `DiffHelper` (line/word diffs, conflict hunks), `KeyboardInsets` (the on-screen keyboard's overlap of the window, one source of truth for the shell, the docked editing toolbar, sheets and toasts; its platform sources connected by the app's `keyboardsources.cpp`) in `brulion_ui_helpers`; `TouchFlick` (touch flicks at the finger's speed on Android) in `brulion_ui_touch`; the QML engine's local-only network access (`LocalOnlyNetworkFactory`) in `brulion_ui_network`; `installFallbackFonts()` + the bundled emoji/symbol fonts (`assets/fonts`, `fallbackfonts.h`) in `brulion_ui_fonts`; app-only `DeviceInfo`, `DevTools` (dev/test options) | `qml/theme/Theme.qml`, `qml/components/B*.qml`, `qml/dev/Gallery.qml` |
| `editor` — block editor | `EditorHost` (contract with the shell; header-only, moc'ed once in `brulion_editorhost`, which also has `picturesize.h`: a picture's size from its header or an SVG's root, for the editor and the browser's data: URLs, and whether the device decodes its format — the picture view and the embeds name one it cannot), `QueryHost`, `PageController` (commands, undo, debounced saves, external-change merge, note mode: outline / document), `BlockListModel`, `MarkdownHighlighter`, `SuggestionModel`, `QueryResultModel`, `EditorInput`, pure `blocktext.h`, `FakeEditorHost` (playground, tests) | `qml/editor/*` (BlockEditor, delegates for calc/code/table/query/embeds, BlockMenu — every block's menu with "Kopiuj odnośnik" —, TaskBox and TaskStatusMenu — the task checkbox and its status menu, used by every view that shows tasks —, MobileEditToolbar — docked on the keyboard at the bottom of the editor's `toolbarHost` —, dialogs), `qml/dev/EditorPlayground.qml`, `qml/dev/PageScript.qml` |
| `shell` — app frame | `AppController` (lifecycle, vault, navigation history, side panel, permissions, dev options), `AppSettings` (QSettings + owner-only secret file; the Nextcloud switch and server folder per notebook under the id of its folder — `Vault::idForPath`, as its device state —, the settings of 0.3.0 and older handed to the notebook that was current then or whose sync bookkeeping names their account and folder, `notebookOpened`), `Workspace` (owns `Vault` + `VaultIndex`; implements `EditorHost`/`QueryHost`; pages, history, trash, journals, quick capture (`notebook/capture.h`); folders and moves in `workspacefolders.cpp`: `movePath` / `renamePage` / `renameFolder` through one `relocate()` — the index's plan, `Vault::rename`, the planned notes written once each, the special folders of `.brulion/config.json` rebased —, `moveImpact`, `folderPaths`, `folderInfo`, `subfolders`, `folderRenamed` for routes and the tree) with `WorkspaceReminders` and `WorkspaceSync` (+ `workspacesyncruns.cpp`: the throttled `live` progress, `history`, `runDetails`; a notebook whose server folder another notebook of the device syncs with — `NextcloudSettings::sharedWith` from `AppSettings::nextcloudFolderUser`, left by 0.3.1–0.3.5 — syncs nothing and says why), wired by `workspaceintegration.h`; journals, quick capture, pages, folders and moves delegate to the core module `notebook` (the Workspace keeps the translated messages, notices and signals); `RouteHistory` (`navigation.h`), `CommandModel`, `FolderTreeModel` (the sidebar's tree: lazy, applied as inserted and removed rows, Polish order, the journals folder pinned), `DemoVault` ("Przykładowy brulion"; the separate notebook "Dokumentacja Brulionu" is made of Brulion notes in `shell/guide/*.md`, embedded as resources and brought up to date on every open by `updateDocsVault`, which records the hash of every written page in the notebook's LocalStore and recognises 0.3.0–0.3.5 pages by their known hashes, so pages the user changed or deleted stay so; the documentation is never a recent or start notebook (the start drops it from the recent notebooks of 0.3.0–0.3.5) and arms no reminders — `Workspace::setRemindersPaused` keeps the user notebook's armed set; the sidebar button next to the notebook name switches notebooks, `openRecentVault` never recreates a missing folder), `DesktopTray` (`SingleInstance` is in `src/ipc`; `AppController::handleInstanceMessage` takes its messages — unknown verbs change nothing — and `openRoute` shows a route asked for from outside, brulion-cli `app open`, also once a notebook still opening is ready); dev: `ShellSelfTest`, `ShellJourney` | `qml/Main.qml`, `qml/shell/*` (AppShell, sidebar with the tree's menus and drag and drop, top bar, bottom nav, route view, dialogs — `ShellMoveDialog` for "Przenieś do…"), `OnboardingView`, `SettingsView` |
| `notify` — notifier adapters (`brulion_notify`) | `Notifier` (a `reminders::NotificationSink` + signals; `createPlatformNotifier()`), `DesktopNotifier` + `DBusNotificationBus` (snooze/OK actions, ringing alarms, tray fallback, sound hints), `AndroidNotifier` + `androidpayload` (JSON for the bridge, tested on the desktop), `WasmNotifierSink`; `ReminderSounds` + `soundchoice.h` (the sounds of reminders and alarms: device-local in AppSettings, per platform — Android ringtones on versioned notification channels, freedesktop theme sounds / files, the browser's tone); dev: the settings self-test (`notifyselftest.h`) | `qml/notify/ReminderSoundSetting.qml` (in `SettingsView`) |
| `content` — reading views | `WorkspaceListModel` base (diffed updates), `JournalListModel`, `BacklinksModel` (links and tags naming the page) / `TagBlocksModel` (`GroupedRefsModel`), `TagListModel`, `PageListModel`, `SearchResultModel`, `HistoryModel`, `OutlineModel`, `PageTools` | `JournalsView`, `PageView`, `AllPagesView`, `TagsView`, `SearchView`, `TrashView`, `HistoryView`, `qml/content/*` (side panel, backlinks, dialogs) |
| `planning` — calendar, tasks, graph | `CalendarModel` (month/week/agenda), `TaskListModel` (sections, filters run by the query engine), `ForceLayout` (Barnes–Hut) + `GraphModel` + `GraphItem` (painted, zoom/pan/pinch), `PlanningActions` (task toggles with undo, journal appends), `PlanningContext` | `CalendarView`, `TasksView`, `GraphView`, `qml/planning/*` |
| `io` — sync center, import/export (`brulion_ioviews`) | `ImportController`, `ExportController`, `FileGatherer` (listing and reading off the GUI thread) / `DocumentProvider` (Android `content://` documents and document trees) / `writeToUrl` (`fileexchange.h`), `NextcloudSetup`, `SyncthingSetup`, `ConflictResolver` (merge3 hunks, base from History); dev: the import self-test (`ioselftest.h`) | `SyncView` (live progress, the history of runs and their details: `SyncLiveProgress`, `SyncHistoryList`, `SyncRunDetails`), `ImportExportView`, `qml/io/*` |
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
  `$XDG_RUNTIME_DIR`, a lock file decides the primary) and the messages of its socket
  (`appmessage.h`: `activate`, `capture`, `today`, `open {"route", "vault"}` — one-way lines a
  second launch or `brulion-cli app open` sends).
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
  no run while the vault storage is unavailable (`VaultUnavailable`). Concurrent edits are
  merged three-way; when that fails both versions stay (conflict copy) for `ConflictResolver`.
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
  and worker-thread stack sizes by it (1 otherwise).
* Nix: `nix develop` (Qt 6.11 from nixpkgs, CMake, Ninja, ccache, clang-tools, gdb);
  `nix build .#brulion` (builds without the tests — doCheck = false —, validates the desktop file and AppStream
  metainfo, runs `brulion-cli`); `nix build .#brulion-cli` (the command line alone, `nix/cli.nix`); NixOS module `programs.brulion` (+ autostart hidden in the tray) and
  `services.brulion-web` (the browser version behind nginx) — docs/NIXOS.md; `nix flake check`.
* Linux: `scripts/appimage-build.sh` (Qt 6.11.2 from aqt, linuxdeploy) → AppImage x86_64 +
  aarch64, each with a `.zsync` file for AppImageUpdate (checked before publishing by
  `scripts/appimage-update-check.sh`); desktop entry and metainfo in `packaging/`.
* Android: `scripts/android-build.sh` — Qt 6.11.2 for Android from aqt with host tools of the
  same version, NDK r27c, JDK 21; ABIs `BRULION_ANDROID_ABIS` (default arm64-v8a; armeabi-v7a,
  x86_64, x86 → one multi-ABI APK); `android/` holds the manifest, resources and Kotlin
  sources (+ Kotlin unit tests); signed APK in GitHub Actions — docs/ANDROID.md.
* WebAssembly: `scripts/wasm-build.sh` — Qt 6.11.2 `wasm_singlethread` + matching emsdk; our
  HTML shell, manifest and service worker in `wasm/` (offline PWA); deployed to GitHub Pages —
  docs/WEB.md.
* CI (`.github/workflows`): `ci.yml` (Nix build + flake checks on x86_64/aarch64, aqt build
  with ASan/UBSan and `-Werror`, actionlint/shellcheck/clang-format), `android.yml`,
  `appimage.yml`, `wasm.yml` (Pages), `release.yml` (tag `v*` → draft release with APK,
  AppImages, web ZIP, sources, checksums) — docs/RELEASE.md.
