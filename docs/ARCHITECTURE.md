# Brulion — architecture (C++20 / Qt 6)

Brulion is an offline-first Markdown notes app: Obsidian-style plain files, a Logseq-style
block outliner with daily journals, calendar, reminders and alarms, calculations and tables,
sync through Syncthing (plain folder) or Nextcloud (WebDAV), import/export from other apps.

One C++/QML code base, three targets, all built against **Qt 6.11.2**:

| Target | How | Vault storage | Reminders |
|---|---|---|---|
| **NixOS / Linux desktop** | Qt from nixpkgs, Nix flake package + NixOS module | a folder (e.g. a Syncthing folder) | tray app + freedesktop notifications (D-Bus) |
| **Android** | Qt for Android (aqt prebuilt, CI), APK via `androiddeployqt` | folder on shared storage (Syncthing-Fork) or app storage + Nextcloud | native `AlarmManager` alarms (Java/Kotlin via JNI) — fire when closed, survive reboot |
| **Browser** | Qt for WebAssembly (single-threaded, aqt prebuilt + emsdk, CI) → GitHub Pages | Emscripten IDBFS (IndexedDB) + import/export of files, Nextcloud sync | browser Notification API while the page is open |

## Layers

```
┌─────────────── src/app  (executable "brulion", QML module "Brulion") ───────────────┐
│ QML: theme, components, block editor, views (journals, page, calendar, tasks, graph,  │
│ search, sync, import/export, history, trash, settings, onboarding)                    │
│ C++ controllers/models exposed to QML (QML_ELEMENT / QML_SINGLETON)                   │
├─────────────── src/platform  (library brulion_platform) ─────────────────────────────┤
│ Notifier (desktop D-Bus+tray / Android JNI / WASM JS), storage permission (Android),  │
│ WASM persistence (IDBFS sync), file pick/save abstraction                             │
├─────────────── src/core  (library brulion_core, QtCore + QtNetwork; io also QtGui) ──┤
│ dates · markdown · calc · vault · index(search, query) · sync(webdav, merge,           │
│ syncthing) · reminders(calendar) · io(import/export, zip) · util                      │
└───────────────────────────────────────────────────────────────────────────────────────┘
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
5. **Polish first.** Source strings in English with `tr()`/`qsTr()`; Polish translation
   (`i18n/brulion_pl.ts`) is the default UI language; English selectable.
6. **Round-trip fidelity.** Editing one block never rewrites other bytes of the file
   (line endings, indentation, unknown syntax preserved).

## Core modules (`src/core/<module>/`, namespace `brulion`)

Each module has a public header named after it (e.g. `src/core/dates/dates.h`) plus internal
headers, and its tests in `tests/core/tst_<module>*.cpp`. Shared types: `src/core/types.h`.

| Module | Responsibility (main classes / functions) |
|---|---|
| `util` | `paths.h` (normalize, join, dirname, basename, isMarkdown, isIgnoredPath, Syncthing conflict names, sanitizeFileName, uniquePath), `text.h` (diacritic folding `foldDiacritics`, Unicode-aware word boundaries, line splitting preserving EOLs), `hash.h` (FNV-1a 64) |
| `dates` | local date helpers; `parseNaturalDate(text, now, Locale)` for Polish + English phrases ("jutro o 9", "w piątek 18:00", "za 2 godziny", "15 października", "next friday 6pm") returning date/time + matched span; `RecurrenceRule` parse (canonical English + Polish) / format(en/pl) / `nextOccurrence` / `occurrencesBetween`; Logseq repeaters; query date expressions (`today`, `+7d`, `this-week`, `overdue`…); human formatting ("dziś 09:00", "pt 17 paź") |
| `markdown` | `parseNote(path, content, config) -> ParsedNote` (blocks/outline, links, tags, tasks, reminders, properties, frontmatter YAML subset, headings, block ids, journal date); inline extraction; **BlockDocument** (editable list of blocks keeping each block's raw text for lossless round-trip: split/merge/indent/outdent/move subtree/toggle task/set reminder/ensure block id) ; pure edit helpers (`setTaskStatus` incl. recurrence next occurrence, `renameLinks`, `applyTemplate`, journal paths); `InlineRenderer` → Qt rich-text HTML subset for QML `Text` (links as `page:`, `tag:`, `block:`, `ext:` URLs, chips for dates/reminders) |
| `calc` | own expression engine (tokenizer + Pratt parser + evaluator; no `eval`): numbers with Polish decimal comma, variables (Unicode names), `ans`, percent sugar (`x + 10%`, `20% z x`), units and conversions (`5 km to mi`, `3 h + 20 min`), currencies as tags (`12 zł + 5 zł`), functions (sum avg min max round abs sqrt pow if …), `sum`/`suma` lines; calc blocks; inline `= expr`; **table formulas** (A1 refs, ranges, `[Kolumna]`, `above`, `left`, dependency order, cycle detection) |
| `vault` | `Vault` (open folder, list/read/write with `QSaveFile` atomic writes, per-path write serialisation, change signals with source, external changes via `QFileSystemWatcher` + rescan diffing, echo suppression by content hash, trash `.trash/`, rename/move, `.brulion/config.json`); `History` (version snapshots per file in AppData with pruning); `LocalStore` (small JSON key-value store per vault in AppData) |
| `index` | `VaultIndex` (incremental: notes, link resolution per SYNTAX §2, backlinks, unlinked references, block ids, tags, tasks, reminders, journals, attachments, graph data); `SearchIndex` (inverted index, prefix + fuzzy, diacritic-insensitive, excerpts); `Query` language (SYNTAX §10: parse + run) |
| `sync` | `WebDavClient` (QNetworkAccessManager: PROPFIND/GET/PUT/DELETE/MKCOL, If-Match, Basic auth); `SyncEngine` (bidirectional, per-file state + merge bases in LocalStore, conflict copies, deletions); `merge3()` line diff3 with union of concurrent appends; `SyncScheduler` (interval, debounce after edits, online detection); Syncthing helpers (conflict copies, REST status client, `.stignore` template) |
| `reminders` | `upcomingNotifications(index, now, horizon)` (stable ids, recurrence expansion, done tasks skipped); `ReminderScheduler` (QTimer based for platforms without background alarms; missed-reminder detection); `calendarItems(index, from, to)`; snooze edits |
| `io` | importers (Obsidian, Logseq, Notion, Evernote ENEX, Google Keep, Joplin, Simplenote, Roam, Bear, Markdown/TXT, HTML, iCalendar) and exporters (Markdown ZIP, Obsidian, Logseq, HTML site, single HTML, JSON, iCalendar, OPML, CSV tasks); ZIP via vendored `3rdparty/miniz`; HTML→Markdown via `QTextDocument` (QtGui) |

## Platform layer (`src/platform/`)

`Notifier` interface: `permission()`, `requestPermission()`, `schedule(QList<ScheduledNotification>)`
(replaces the whole set), `showNow()`, signal `activated(path, line)`, `firesWhenClosed()`.
Implementations selected at compile time:
* `DesktopNotifier` (Linux): `org.freedesktop.Notifications` over QtDBus with actions
  "Drzemka 10 min"/"OK", urgency critical + sound hint for alarms; fallback `QSystemTrayIcon`.
* `AndroidNotifier`: JNI → `android/src/io/github/januszbit/brulion/` (AlarmManager exact
  alarms, receivers incl. BOOT_COMPLETED, notification channels `reminders`/`alarms`, alarm
  activity over the lock screen, auto-stop, snooze) — ported from the v1 Kotlin plugin
  (branch `archive/web-v1`, `src-tauri/plugins/brulion-android`).
* `WasmNotifier`: browser Notification API via Emscripten `EM_JS`.
Also: `StorageAccess` (Android "All files access" check/request), `WasmPersistence` (mount
IDBFS at the vault path, `FS.syncfs` after writes), `FileExchange` (pick/save files — native
dialogs on desktop/Android, `QFileDialog::getOpenFileContent`/`saveFileContent` on WASM).

## App (`src/app/`, QML module `Brulion`)

C++ controllers exposed to QML: `AppController` (vault lifecycle, settings, navigation
stack), `PageController` + `BlockListModel` (block editor model over `BlockDocument`, debounced
saves, external-change merge), `JournalModel`, `CalendarModel`, `TaskListModel`,
`SearchModel`, `GraphModel` (force layout in C++), `SyncController`, `ImportExportController`,
`HistoryModel`, `SettingsController`. QML: `qml/Main.qml`, `qml/theme/Theme.qml` (singleton,
light/dark), `qml/components/*`, `qml/editor/*` (block editor), `qml/views/*`. See docs/UI.md.

## Build, test, packaging

* CMake ≥ 3.24 + Ninja, C++20, `qt_standard_project_setup(REQUIRES 6.8)`, `qt_add_qml_module`.
  Targets: `brulion_core` (static), `brulion_platform` (static), `brulion` (app), tests via
  CTest (`tests/`). Warnings as errors for our code in CI.
* Nix: `nix develop` (Qt 6.11 from nixpkgs, cmake, ninja, ccache, clang-tools, gdb);
  `nix build .#brulion` (wrapQtAppsHook); NixOS module `programs.brulion` (+ autostart in tray).
* Android: Qt 6.11.2 `android_arm64_v8a` (+ armv7, x86_64) from aqt with host tools from the
  same Qt version; `android/` package sources; signed APK in GitHub Actions.
* WebAssembly: Qt 6.11.2 `wasm_singlethread` + matching emsdk; custom HTML shell + service
  worker for offline use; deployed to GitHub Pages.
