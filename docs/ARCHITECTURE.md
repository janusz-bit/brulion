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

## Layers

```
┌──── src/app  (executable "brulion", QML module "Brulion") ─────────────────────────────────┐
│ QML: qml/theme, qml/components (B* design system), qml/shell, qml/views, qml/editor,       │
│      qml/content, qml/planning, qml/io, qml/dev (gallery, editor playground)               │
│ C++ area libraries: brulion_ui_helpers · brulion_ui_touch · brulion_editor                 │
│      (+ brulion_editorhost) · brulion_shell · brulion_notify · brulion_content ·           │
│      brulion_planning · brulion_ioviews                                                    │
├──── src/platform  (library brulion_platform) ──────────────────────────────────────────────┤
│ Android: AndroidBridge (JNI ↔ android/src/…/BrulionAndroid.kt)                             │
│ WebAssembly: WasmStorage (IDBFS), WasmNotifier, wasmshell (loading screen), self-tests     │
├──── src/core  (brulion_core_<module> libraries; QtCore + QtNetwork, io also QtGui) ────────┤
│ util · text · dates · markdown · calc · vault · index · sync · reminders · io              │
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
| `util` | `paths.h`: vault paths (normalize/NFC, join, dirname, basename, `pageTitle`), ignored paths and temp files, Syncthing / Brulion conflict copy names, `sanitizeFileName`, unique paths |
| `text` (util) | diacritic folding (`foldDiacritics`, `foldKey`: "ŻÓŁW" → "zolw"), Unicode word boundaries and regex helpers, EOL-preserving `splitLines`/`joinLines`, FNV-1a `contentHash` |
| `dates` (text) | local dates (`DateSpec`, never through UTC); `parseNaturalDate` (Polish + English: "jutro o 9", "w piątek 18:00", "za 2 godziny", "next friday 6pm"); `RecurrenceRule` parse/format/`nextOccurrence`/`occurrencesBetween`, Logseq repeaters; query date expressions (`resolveDateExpr`: `today`, `+7d`, `this-week`, `today..+7d`, `overdue`); human formatting ("dziś 09:00", "pt 17 paź") |
| `markdown` (util, text, dates) | `parseNote` → `ParsedNote` (blocks/outline, links, tags, tasks, reminders, properties, YAML-subset frontmatter, headings, block ids, journal date); metadata tokens; pure edits (`setTaskStatus` incl. the next occurrence of recurring tasks, `renameLinks`, `ensureBlockId`, frontmatter values); journals and templates; note modes (outline / document: `resolveNoteMode` from the `brulion:` frontmatter key, journal / page defaults and a content heuristic; `setNoteModeKey`); **`BlockDocument`** (editable block list with lossless round trip: split/merge/indent/outdent/move subtree/toggle task/list item to paragraph); **`InlineRenderer`** → escaped Qt rich text with `page:`/`tag:`/`block:`/`ext:`/`task:`/`reminder:` links and date/reminder/recurrence chips |
| `calc` (text, dates) | own engine (lexer + Pratt parser + evaluator, bounded work, no `eval`): Polish decimal comma, variables (Unicode names), `ans`, percent sugar, units and conversions, currencies as tags, functions, `sum`/`suma` lines; calc blocks, inline `= expr`, page scope (calc variables + numeric properties); GFM tables: parse/serialize/replace and **table formulas** (A1, ranges, `[Kolumna]`, `above`/`left`…, dependency order, `#CYCLE`) |
| `vault` (util, text) | `Vault` (folder scan, cached listing, atomic `QSaveFile` writes, `QFileSystemWatcher` on the folders and the files open in editors + rescans (when the app returns to the foreground, every 2 minutes) for external changes, echo suppression by content hash, trash `.trash/`, rename/move, `.brulion/config.json`, storage checks — see "Data safety"); `History` (device-local version snapshots, throttled and pruned); `LocalStore` (small JSON key-value store per vault in AppData) |
| `index` (util, text, dates, markdown) | `VaultIndex` (incremental, coalesced `changed()`: notes, link resolution per SYNTAX §2, backlinks incl. `((id))` refs, unlinked references, block ids, tags, tasks, reminders, journals, attachments, page/block suggestions, graph data); `SearchIndex` (BM25+, prefix + fuzzy, diacritic-insensitive, excerpts); the query language (`parseQuery`/`runQuery`, SYNTAX §10) |
| `sync` (util, text, vault; QtNetwork) | `HttpTransport` seam + `NetworkHttpTransport`; `WebDavClient` (PROPFIND/GET/PUT/DELETE/MKCOL, ETags, Basic auth); `SyncEngine` (bidirectional, per-file state + merge bases in LocalStore, conflict copies, deletions, mass-deletion guard); `merge3()` line diff3 with a union of concurrent appends, `mergeJson3()` for the config; `SyncScheduler` (interval, debounce after edits, online/foreground triggers); Syncthing: `SyncthingClient` (REST status), conflict copies, `.stignore` template |
| `reminders` (util, text, dates, markdown, index, vault) | `upcomingNotifications()` (stable ids, recurrence expansion, done tasks skipped); `NotificationSink` (implemented by the app's notifiers); `ReminderScheduler` (in-app timing for platforms without background alarms, missed-reminder detection); `calendarItems()`; `snoozeReminder()` |
| `io` (util, text, dates, markdown, index; QtGui) | importers (Obsidian, Logseq, Notion, Evernote ENEX, Google Keep, Joplin, Simplenote, Roam, Trilium, Bear, Markdown/TXT, HTML, iCalendar) with auto-detection and safe target paths; exporters (Markdown ZIP, Obsidian, Logseq, HTML site, single HTML, JSON, iCalendar, OPML, CSV tasks); ZIP (vendored `3rdparty/miniz`), TAR, CSV, iCalendar; HTML→Markdown via `QTextDocument` |

## Platform layer (`src/platform/`, library `brulion_platform`)

Only what needs OS APIs; compiled per target (no `#ifdef`s in core). `platform.h`:
`targetName()` ("linux" | "android" | "wasm").

* **Android** (`android/androidbridge.h`): `AndroidBridge`, the JNI side of
  `android/src/io/github/januszbit/brulion/android/` (Kotlin: `BrulionAndroid`,
  `AlarmScheduler`, `ReminderNotifier`, `AlarmActivity`, receivers incl. `BOOT_COMPLETED`):
  all-files access, notification permission, exact alarms, full-screen alarm screen,
  `scheduleReminders(json)` (replaces the armed set), notification taps (`activated`,
  `takeActivation()` after a cold start), "Save to Downloads" (MediaStore) and picked documents
  for imports (`documentInfo` / `listDocuments`: DocumentsContract, callable from worker
  threads). `android/selftest` builds an APK exercising the bridge.
* **WebAssembly** (`wasm/`): `WasmStorage` mounts IDBFS on the vault and app-data folders,
  loads them before the app opens anything and writes changes back (debounced, on page hide);
  `WasmNotifier` (Notification API, service-worker fallback, clicks → `activated`);
  `wasmshell.h` (`setShellStage`/`hideShellLoadingScreen`: the loading screen of
  `wasm/index.html`); `wasmselftest.h` (`runStorageSelfTest`/`runNotifierSelfTest` for
  `?selftest=storage,notify`, driven by `scripts/wasm-smoke.mjs`).

The notifier adapters themselves live in the app (`src/app/notify`, below), because they also
talk to the tray and the settings.

## App (`src/app/`, QML module `Brulion`)

`main.cpp` creates the `QApplication` (desktop: tray) / `QGuiApplication`, the translator,
`DevTools` and `AppController`, then loads `Main.qml` (or a dev tool window). Each area is a
directory with an `area.cmake` (included by `src/app/CMakeLists.txt`) and, for its C++, a static
library that tests link without the app; classes reach QML through `QML_FOREIGN` wrappers
(`qmlforeign.h`, `editorforeign.h`, `shellqml.h`, `contentqml.h`, `planningqml.h`, `ioqml.h`).

| Area | C++ (main classes) | QML |
|---|---|---|
| `ui` — design-system helpers | `DatesHelper` (natural dates, formatting, recurrence, month grids), `TextHelper` (folding, fuzzy ranking, escaping), `DiffHelper` (line/word diffs, conflict hunks) in `brulion_ui_helpers`; `TouchFlick` (touch flicks at the finger's speed on Android) in `brulion_ui_touch`; app-only `DeviceInfo`, `DevTools` (dev/test options) | `qml/theme/Theme.qml`, `qml/components/B*.qml`, `qml/dev/Gallery.qml` |
| `editor` — block editor | `EditorHost` (contract with the shell; header-only, moc'ed once in `brulion_editorhost`, which also has `picturesize.h`: a picture's size from its header or an SVG's root, for the editor and the browser's data: URLs), `QueryHost`, `PageController` (commands, undo, debounced saves, external-change merge, note mode: outline / document), `BlockListModel`, `MarkdownHighlighter`, `SuggestionModel`, `QueryResultModel`, `EditorInput`, pure `blocktext.h`, `FakeEditorHost` (playground, tests) | `qml/editor/*` (BlockEditor, delegates for calc/code/table/query/embeds, MobileEditToolbar, dialogs), `qml/dev/EditorPlayground.qml`, `qml/dev/PageScript.qml` |
| `shell` — app frame | `AppController` (lifecycle, vault, navigation history, side panel, permissions, dev options), `AppSettings` (QSettings + owner-only secret file), `Workspace` (owns `Vault` + `VaultIndex`; implements `EditorHost`/`QueryHost`; pages, history, trash, journals, quick capture via `workspacecapture.h`) with `WorkspaceReminders` and `WorkspaceSync`, wired by `workspaceintegration.h`; `RouteHistory` (`navigation.h`), `CommandModel`, `FolderTreeModel`, `DemoVault` ("Przykładowy brulion"), `SingleInstance`, `DesktopTray`; dev: `ShellSelfTest`, `ShellJourney` | `qml/Main.qml`, `qml/shell/*` (AppShell, sidebar, top bar, bottom nav, route view, dialogs), `OnboardingView`, `SettingsView` |
| `notify` — notifier adapters (`brulion_notify`) | `Notifier` (a `reminders::NotificationSink` + signals; `createPlatformNotifier()`), `DesktopNotifier` + `DBusNotificationBus` (snooze/OK actions, ringing alarms, tray fallback, sound hints), `AndroidNotifier` + `androidpayload` (JSON for the bridge, tested on the desktop), `WasmNotifierSink`; `ReminderSounds` + `soundchoice.h` (the sounds of reminders and alarms: device-local in AppSettings, per platform — Android ringtones on versioned notification channels, freedesktop theme sounds / files, the browser's tone); dev: the settings self-test (`notifyselftest.h`) | `qml/notify/ReminderSoundSetting.qml` (in `SettingsView`) |
| `content` — reading views | `WorkspaceListModel` base (diffed updates), `JournalListModel`, `BacklinksModel` / `TagBlocksModel` (`GroupedRefsModel`), `TagListModel`, `PageListModel`, `SearchResultModel`, `HistoryModel`, `OutlineModel`, `PageTools` | `JournalsView`, `PageView`, `AllPagesView`, `TagsView`, `SearchView`, `TrashView`, `HistoryView`, `qml/content/*` (side panel, backlinks, dialogs) |
| `planning` — calendar, tasks, graph | `CalendarModel` (month/week/agenda), `TaskListModel` (sections, filters run by the query engine), `ForceLayout` (Barnes–Hut) + `GraphModel` + `GraphItem` (painted, zoom/pan/pinch), `PlanningActions` (task toggles with undo, journal appends), `PlanningContext` | `CalendarView`, `TasksView`, `GraphView`, `qml/planning/*` |
| `io` — sync center, import/export (`brulion_ioviews`) | `ImportController`, `ExportController`, `FileGatherer` (listing and reading off the GUI thread) / `DocumentProvider` (Android `content://` documents and document trees) / `writeToUrl` (`fileexchange.h`), `NextcloudSetup`, `SyncthingSetup`, `ConflictResolver` (merge3 hunks, base from History); dev: the import self-test (`ioselftest.h`) | `SyncView`, `ImportExportView`, `qml/io/*` |

Routes and views: docs/UI.md "Navigation". Area tests: `tests/app/<area>/` (C++ unit tests plus
QML smoke tests and screenshot scripts that run the real app headless).

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

## Build, test, packaging

* CMake ≥ 3.24 + Ninja, C++20, `qt_standard_project_setup(REQUIRES 6.8)`, `qt_add_qml_module`;
  `QT_NO_KEYWORDS`, `QT_NO_CAST_FROM_ASCII`. Warnings: `cmake/BrulionWarnings.cmake`
  (`-DBRULION_WERROR=ON` in CI and the dev scripts). Translations: `qt_add_translations`
  (`i18n/brulion_pl.ts`, `brulion_en.ts` for plurals).
* Tests: QtTest via CTest (`brulion_add_test`), core in `tests/core/<module>`, app areas in
  `tests/app/<area>` (`QT_QPA_PLATFORM=offscreen`), plus headless QML checks of the real app:
  `tst_shell_qml` (`--selftest-shell`), `tst_shell_journey*` (`--journey`), `tst_*_qml` smoke
  tests. Development builds: `scripts/remote-build.sh` (x86_64 laptop) or
  `scripts/dev-build.sh` (locked, `-j2`) — see CLAUDE.md. Sanitizer builds (`-fsanitize=` in
  `CMAKE_CXX_FLAGS`, CI) get `BRULION_TEST_SLOWDOWN=25`: tests multiply their wall-clock limits
  and worker-thread stack sizes by it (1 otherwise).
* Nix: `nix develop` (Qt 6.11 from nixpkgs, CMake, Ninja, ccache, clang-tools, gdb);
  `nix build .#brulion` (builds without the tests — doCheck = false —, validates the desktop file and AppStream
  metainfo); NixOS module `programs.brulion` (+ autostart hidden in the tray) and
  `services.brulion-web` (the browser version behind nginx) — docs/NIXOS.md; `nix flake check`.
* Linux: `scripts/appimage-build.sh` (Qt 6.11.2 from aqt, linuxdeploy) → AppImage x86_64 +
  aarch64; desktop entry and metainfo in `packaging/`.
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
