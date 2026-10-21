# Brulion — UI (Qt Quick / QML)

One QML UI for desktop (wide window, keyboard + mouse), Android (phone, touch, soft keyboard)
and the browser (Qt for WebAssembly). Layout adapts to the window width, not the platform.
Qt Quick Controls with the **Basic** style, themed by our own `Theme` singleton and wrapped
components — no Material/Fusion look, so all targets look identical.

## Look & feel

* Calm, paper-like light theme and a deep blue-grey dark theme, matching the logo
  (`assets/logo.svg`): ink `#1f2430`, paper `#fbf7ee`, margin red `#e2574c` (accent /
  destructive), amber `#f2b33d` (reminders, alarms), grid blue `#9fc3e6` (links lighter tint,
  focus). Text contrast ≥ WCAG AA. `Theme.dark` follows `Qt.styleHints.colorScheme` unless the
  user overrides it in the settings.
* Typography: system sans for UI; editor font selectable (serif/sans), comfortable line height,
  configurable max text width and text size.
* Outliner look like Logseq: round bullets, thin vertical guide lines per nesting level, fold
  arrows, clicking a bullet zooms into the block (breadcrumb to zoom out) — in outline mode; in
  document mode a note reads like an Obsidian page (see "Note modes" below).
* Touch (Android, `--touch`): targets ≥ 44 px, bottom navigation, drawer, toolbar above the
  keyboard, dialogs as bottom sheets.

## Window layout (`qml/Main.qml`, `qml/shell/AppShell.qml`)

* **Wide (≥ 900 px):** `SplitView` — left **sidebar** | **top bar** + current view | optional
  right **side panel**.
  * Sidebar (`ShellSidebar`): the notebook's name and a new-page button; navigation Dziennik,
    Kalendarz, Zadania, Wszystkie strony, Tagi, Graf, Szukaj; Ulubione; Ostatnie; Strony (the
    folder tree, lazy, with a context menu: new page / folder here, open in side panel,
    favourite, rename, history, trash; an empty folder opens to "Pusty folder"); a sync status
    footer (opens the sync center).
  * Top bar (`ShellTopBar`): sidebar toggle, back / forward, the title (a breadcrumb for pages
    in folders), Szukaj, Paleta poleceń, Szybka notatka, side panel toggle and **Więcej** (⋮).
  * Side panel (`ShellSidePanel`): a page opened "in the side panel", or the current view's
    context — for pages and journals the tabs **Linki** (linked references and unlinked
    mentions), **Konspekt** (headings), **Kalendarz** (month with journal days).
* **Narrow (< 900 px, phones):** the sidebar becomes a `Drawer` (the ☰ button, the bottom bar's
  Menu, or a swipe from the left edge — on Android with gesture navigation that edge swipe is the
  system's back gesture, so there the buttons open it); the top bar keeps ☰, back (when there is history), title, Szukaj and Więcej;
  a **bottom navigation** bar (Dziennik, Kalendarz, Szukaj, Zadania, Menu → drawer); a floating
  quick-capture button (hidden while editing). The Android back key / gesture walks back
  through the history; the app closes only from the home view. The drawer also lists
  **Importuj notatki** under the navigation (the import tab of `ImportExportView`).
* **Więcej menu** (top bar ⋮): on a page or daily note — Dodaj do ulubionych / Usuń z
  ulubionych, Zmień nazwę… (F2, pages only), Historia strony, Źródło Markdown (Ctrl+E), Przełącz
  tryb notatki, Otwórz w panelu bocznym (wide only), Przenieś do kosza; always — Nowa strona (Ctrl+N), Paleta
  poleceń (Ctrl+K), **Centrum synchronizacji, Importuj notatki (the import tab), Import i eksport,
  Kosz (n), Ustawienia (Ctrl+,), O Brulionie**. These destinations have no button of their own: on
  phones this menu (and the command palette) is the way to them. A new notebook (at most one
  note) also offers **Importuj notatki** under the empty journal's hint, and onboarding has an
  "Importuj notatki" card that opens the chosen notebook on its import tab
  (`AppController.openVaultAt`).
* Shell popups: command palette (`BCommandPalette` over `CommandModel`: navigation, journals,
  new page, capture, theme, language, sync, panels, settings, about), quick switcher
  (`BQuickSwitcher`, fuzzy page search), quick capture (one line → today's journal; dates in
  the text become ⏰ / 📅), new page / folder, rename, delete confirmation, about; toasts.

## Keyboard shortcuts (desktop)

| Keys | Action |
|---|---|
| Ctrl+K | command palette |
| Ctrl+O | quick switcher (go to a page) |
| Ctrl+Shift+F | search |
| Ctrl+J | today's journal |
| Ctrl+N | new page |
| Ctrl+Shift+N | quick capture |
| Alt+← / Alt+→ | back / forward |
| Ctrl+\\ | side panel on/off (wide windows) |
| F2 | rename the current page |
| Ctrl+, | settings |
| Ctrl+E | Markdown source mode of the page / journal |
| Ctrl+Enter | (in dialogs) accept |

Block editor (handled in `qml/editor/BlockEditor.qml`, executed by `PageController`):

| Keys | Editing a block | A block selected (Esc) |
|---|---|---|
| Enter | split the block at the cursor (task checkboxes continue); a new line in multi-line blocks (code, calc, query, quote, callout) | edit the block |
| Shift+Enter | soft line break | — |
| Backspace at the start | remove the task marker, else merge with the previous block | delete the block |
| Delete at the end | merge the next block into this one | delete the block |
| Tab / Shift+Tab | indent / outdent with children (Tab in a multi-line block: a tab character) | indent / outdent |
| Alt+↑ / Alt+↓ | move the block with its children | move |
| ↑ / ↓ | on the first / last line: to the previous / next block | select previous / next |
| Ctrl+Enter | cycle the task status (none → todo → doing → done → none) | same |
| Ctrl+. | fold / unfold | same |
| Ctrl+Z, Ctrl+Shift+Z / Ctrl+Y | undo, redo (per page) | same |
| Ctrl+V | a copied picture or files become attachments `![[…]]` | — |
| Esc | close the suggestions, else stop editing and select the block | deselect |

## Navigation

`AppController` keeps the main view's route history (`navigation.h`: `RouteHistory`, routes are
`QVariantMap`s `{view, path?, line?, blockId?, date?, tag?, query?, section?, …}`);
`ShellRouteView` shows the view for the current route (and for the side panel's route).

| Route `view` | View | Parameters |
|---|---|---|
| `journals` (home) | `JournalsView` — the stream of daily notes, "Przejdź do dnia…", missed reminders banner | `date?` |
| `journal` | `JournalsView`, one day (opened "jako strona") | `date`, `path?` |
| `page` | `PageView` — page header (tags, properties, note mode toggle, favourite, page menu), the block editor (outline or document), linked references, unlinked mentions | `path`, `line?`, `blockId?`, `open?` |
| `calendar` | `CalendarView` — Miesiąc / Tydzień / Agenda, the day panel, add dialog | `date?`, `mode?` |
| `tasks` | `TasksView` — quick add, filters, sections (Zaległe, Dziś, Jutro, W tym tygodniu, Później, Bez daty, Zrobione), "Pokaż jako zapytanie" | `query?` |
| `graph` | `GraphView` — whole / local graph (depth 1–2), toggles for tags, missing pages, journals, templates | `path?`, `depth?`, `select?` |
| `search` | `SearchView` | `query?` |
| `allPages` | `AllPagesView` | `path?` (folder), `sort?` |
| `tags` / `tag` | `TagsView` | `tag?` |
| `sync` | `SyncView` — Przegląd, Syncthing, Nextcloud (live progress, history of runs), Konflikty (merge) | `tab?`, `copy?`, `run?` (open a run's details) |
| `importExport` | `ImportExportView` | `tab?`, … |
| `history` | `HistoryView` — versions of a page, diff, restore | `path` |
| `trash` | `TrashView` | — |
| `settings` | `SettingsView` — Ogólne, Edytor, Notatnik, Przypomnienia, Synchronizacja, Aplikacja | `section?` |
| `logs` | `LogsView` — "Rejestr zdarzeń", the event log (see below) | `group?`, `level?`, `fromMs?`, `toMs?`, `run?` |

`OnboardingView` is shown before a notebook is open (pick or create a folder, or the sample
notebook "Przykładowy brulion").

## The block editor (heart of the app)

A page is edited as a list of **blocks** (Logseq model), backed by `BlockDocument` in the core
(each block keeps its raw Markdown so untouched blocks are written back byte-for-byte):

* `BlockListModel` (C++, `QAbstractListModel`) exposes the blocks with roles such as `raw`,
  `editText`, `html` (rendered by the core `InlineRenderer`), `depth`, `kind`, `view`,
  `headingLevel`, `hasChildren`, `collapsed`, `taskStatus`, `blockId`, `lang`, calc results,
  table models… Zoom (one block's subtree) and folding are model-level; rows survive edits
  (no model resets).
* `qml/editor/BlockEditor.qml`: a `ListView` of `BlockDelegate`s. An unfocused block shows its
  rendered rich text (links: `page:`, `tag:`, `block:`, `ext:`, `task:`, `reminder:` URLs).
  Clicking or tapping it edits it inline (`InlineEditor`: a `TextEdit` with the raw Markdown,
  minus the task marker / fences the delegate draws, and `MarkdownHighlighter`).
* `PageController` executes commands (one undo step each, typing coalesced), saves debounced
  and on focus loss / suspend / quit, and merges external changes three-way.
* Code fences (`blocktext.h`, `bodyWithEditText`): a code block ends at its first closing fence,
  exactly as in the parser. Its editor shows the code only — the fence lines are hidden, and so
  are a block id line (`^id`, written by "Kopiuj odwołanie do bloku" and `((` completion) and
  `key:: value` properties after the closing fence. A ```` ``` ```` line typed inside a closed
  code block stays code: the hidden fences are widened when needed (so a fenced example can be
  written about or pasted without breaking the page). A fence that is not closed (a file written
  elsewhere) is not widened: a closing fence typed into it closes it. A list item with more text
  after its code block edits the closing fence and that text with the code, nothing hidden or
  widened. A fence opened in a text block (```` ``` ```` typed on a new line of a paragraph or list
  item, fenced text pasted without its closing line) gets its closing fence at the end of the
  block, so the code never runs on into the following blocks in the saved file; only a lone
  opening line (```` ```js ```` being typed) stays text until Enter or a line break after it makes
  it a code block. The two lines then act as a pair: a longer opening fence lengthens the closing
  one, Backspace over the opening fence (or deleting its line) removes the closing one, and a
  deleted or changed closing fence is added again at the end of the block.
* **Autocomplete popup** while typing (`SuggestionModel`, `SuggestionPopup`): `[[` pages
  (+ create), `((` blocks (creates the target's block id), `#` tags, `/` slash commands,
  natural-language dates after `📅 ⏳ 🛫 ⏰ ⏰!` or `@` (Polish and English, e.g. "jutro o 9";
  `@` also offers the journal link and a reminder).
* Slash commands (`/`, Polish and English names, diacritic-insensitive filter): Zadanie,
  Przypomnienie…, Alarm…, Termin…, Zaplanuj…, Powtarzaj…, Dziś, Jutro, Data…, Tabela,
  Obliczenia, Zapytanie, Nagłówek 1–3, Cytat, Callout, Kod, Wzór matematyczny, Szablon…,
  Obraz…, Osadź, Odwołanie do bloku, Priorytet wysoki / niski, Linia pozioma, Aktualna
  godzina. Under each name: the syntax it writes ("⏰ data godzina", "- [ ] zadanie", "blok
  ```calc").
* A day without a note (journal stream or a single day) starts from the journal template; the
  note is created by the first change (`PageController.startNewPage`), never by looking at it.
* Special block delegates: code (monospace), `calc` (input lines with right-aligned results),
  table (grid editor with formula results; edits rewrite the Markdown table), `query` (live
  results with checkable tasks), math (`math`/`latex`/`tex` fences, the formula rendered from
  the LaTeX source), callout, embeds/transclusions (`![[…]]`, `!((…))`), images, headings,
  horizontal rule, properties / frontmatter ("Właściwości" panel).
* Chips inside rendered text: 📅 due (red when overdue), ⏳ scheduled, ⏰ reminders (amber,
  bell for alarms), 🔁 recurrence in Polish words. Clicking a ⏰ chip opens the reminder dialog
  (edit / snooze / delete). "dziś"/"jutro" and overdue colours of open pages move on at
  midnight (`DayClock`). Links to daily notes read as dates (`[[2026-10-13]]` → "wt 13 paź").
* Source mode (Ctrl+E, Więcej → Źródło Markdown): one text area with the whole file and
  highlighting.
* Pictures: Ctrl+V pastes a copied picture as an attachment, files dropped on the page are
  stored in the attachments folder and embedded, "/Obraz…" and the toolbar pick files. Opening
  one (a click on the embed, a link to the file) shows the picture itself
  (`ContentPictureView`, `PageView.pictureRoute`): fitted into the view with its aspect ratio
  kept, a click on it or the "Pokaż naturalny rozmiar" button its own pixels (then draggable), the
  button back to the fitted view. The bar's "Pokaż jako tekst", the page menu's "Pokaż obraz"
  and Ctrl+E switch to the alternative view — the file read as text in the block editor (the
  older behaviour, mojibake for a binary picture, kept for text-like assets). While the picture
  shows, the editor stays unloaded (`page.path` ""): a picture's bytes are not a note
  (`paths::isPicturePath`, `Workspace.isPicture`).
* Mobile toolbar (`MobileEditToolbar`, touch UIs, above the keyboard; scrolls sideways):
  Zmniejsz / Zwiększ wcięcie, Przenieś w górę / w dół, Zadanie, Link do strony (`[[`), Tag
  (`#`), Polecenia (`/`), Termin, Przypomnienie, Wstaw obraz, Cofnij, Ponów, Ukryj klawiaturę.

## Scrolling performance

Long pages, the journal stream, Tasks and the calendar agenda are lazy `ListView`s: a row is
created when it scrolls into the list's cache and destroyed when it leaves, on the GUI thread,
while the list moves. Measured with `--scroll-probe` (see "Dev tools"); ctest
`tst_scroll_jumps_qml` fails on jumps, `tst_touch_flick` / `tst_touch_flick_compressed` on Qt's
touch faults.

### Rules for contributors

Any delegate of a scrolling list (a block and everything inside it, a day of the journal, a Tasks
or agenda row) follows these; the reasons are below.

1. **Final height at creation.** The height chain of a delegate is bindings, not Layouts or
   positioners (a `RowLayout`'s, a `Row`'s / `Column`'s / `Flow`'s size arrives with its next
   polish, after the list has placed the row). A sum over a Repeater's items reads `itemAt()` and
   a `revision` the Repeater bumps on `itemAdded` / `itemRemoved`. What a block shows from
   elsewhere is there when it is created: a model's first run in `componentComplete`
   (`QQmlParserStatus`, as `QueryResultModel`), not a `Qt.callLater` / `QTimer` later; a
   picture's size from its file's header (`picturesize.h`), not from the loaded image.
2. **Nothing changes a row's height while the user only scrolls or points.** No room that
   appears on hover or under the resting mouse pointer: hover controls take no room or keep
   theirs (the query header's edit button); room that comes and goes belongs to a user's action
   (the table's "+ Row / + Column" appear when the table is selected or a cell edited).
3. **An inactive `Loader` keeps the implicit size of the item it had** (Qt 6.11): in a
   positioner hide it (`visible: active`), in a sum use `active ? implicitHeight : 0`.
4. **Create only what is shown.** Menus, dialogs, popups and tooltips through a `Loader` that is
   active when they open (a tooltip per table cell made tables slow to create); a row has only
   the parts of its kind (a query row's group label, its checkbox); `BIcon` / `TaskBox` create
   no image for an empty name or a state without an icon. Do not set something in `onLoaded`
   that the Loader's own `active` binding reads (a binding loop; the QML self-tests fail on
   "Binding loop detected").
5. **Cheap per row, on the GUI thread.** No file reads beyond a header, no parsing of whole
   documents, no icon theme lookups: an SVG's size comes from its root element only (Qt's SVG
   reader parses the whole drawing — 0.6 s for a 2.4 MB one on a Raspberry Pi 4). Bound what one
   row creates: a query block makes 20 result rows on touch screens, 50 with a mouse
   ("Show more" adds the rest).
6. **Every list scrolled by touch gets `TouchFlick.enabled: true`** (not for lists with a
   `snapMode` or `StrictlyEnforceRange`, where it does nothing).
7. **Measure, before and after:** `tests/app/perf/scroll-probe.sh` (also `CACHE=0`: every row
   created at the edge of the view), `VAULT=big` for long journals, a Raspberry Pi for ARM times
   (`PLATFORM=offscreen`), `tests/app/perf/android-scroll.sh` on the emulator or a phone; ctest
   `tst_scroll_jumps_qml` and the QML self-tests.

### Why: rows that grow move the content

ListView places a new row with the height it has when its creation completes; a row that grows
afterwards pushes every row below it. Scrolling up, with the row added at the top edge, the text
being read jumps — ListView only grows its first row upwards, and only when that row lies wholly
above the view (`QQuickListViewPrivate::itemGeometryChanged`, Qt 6.11). Whether a row is made
ahead in the cache or only when it reaches the edge depends on timing (the cache is filled between
frames; a slow phone, a loaded machine or a fast flick overtakes it), so such a row makes the
content jump now and then, not every time. Hence:

* bindings instead of Layouts in `ContentJournalDay`, `QueryBlockView`, `TransclusionView`,
  `EmbedView`, `FrontmatterView` (folded), `PlanningItemRow`, `CalendarAgendaList`, `TasksRow`
  (its chips' line breaks), `TableBlockView`'s buttons and `BChip` itself — Tasks and the agenda
  reuse their rows (`reuseItems`), and a reused row whose chips took their new widths a polish
  later wrapped onto another line after it was placed. (In Qt 6.11 a Repeater inside a delegate
  creates its items at once — `incubateObject` gives the delegate's context no incubator — but
  the bindings do not rely on that.)
* a query block runs its first query when its `QueryResultModel` is completed: on the guide page
  its results (773 px) used to arrive after the editor had placed the block;
* pictures: the `embeds` role has each picture's `width` × `height` — a raster picture's header
  (`QImageReader::size()`), an SVG's root `width` / `height` / `viewBox` given to Qt's own SVG
  reader as a document of their own (so units, percentages and the viewBox follow Qt's rules
  exactly), the whole SVG only when the root leaves the size to the drawing and the file is at most
  64 KB (else unknown: sized when loaded); cached per file, modification time and size. In the
  browser the size comes with the picture's `data:` URL (`EditorHost::assetPixelSize`). When the
  loaded picture has another shape than its header said (Qt's pixmap cache still had a picture
  since replaced in the file), the frame takes the picture's shape — never stretched;
* the zoom breadcrumb above the blocks is hidden with its Loader (an inactive Loader kept its
  height: a blank band after zooming out);
* a day of the journal stream is created whole: its editor (`BlockEditor.autoHeight`) creates all
  its blocks with it, at the day's width from the start, so the list places the day with its final
  height. Creating a day is therefore one long step (below); Qt 6.11 creates the rows of a
  ListView inside another list's row synchronously, so it cannot be spread over frames without
  the day growing after it was placed. The journal's cache is about two screens (`dayCache`);
* second line of defence: the block editor's and the journal's lists fill their cache above the
  view also while a touchpad scrolls up (Flickable knows no velocity for touchpad scrolling, and
  ListView then caches only below), so rows usually come into view from the cache.

The regression test turns the cache off (`--probe-cache 0`): every row is then created at the
edge, and a row that is not final at creation fails every run instead of one run in several. It
runs its seven cases two at a time (each run is bounded in time, ~110 s in all).

### Why: creating a row stalls the list

Rows are created on the GUI thread while the list moves; a frame that creates an expensive row is
a visible hitch (on Android's threaded render loop a fling freezes for it). What only some rows
have, or what is used only now and then, is created when needed: block and task menus when they
open, the block editor's dialogs, autocomplete popup, merge dialog, drop hint and touch toolbar on
first use, a block's fold arrow / bullet / number / checkbox only when it has one, a text block's
text without a Loader, a table cell's tooltip when it shows, a table's buttons when they show, a
task box's icon for "done" / "deferred" only; `BIcon` creates no image without a name (an
`IconImage` without a source searches the icon theme on disk). The editor's palette, locale and
clock reach its controller before its rows are created (applied later, they made every row
render twice).

### Touch flicks: `TouchFlick` (`src/app/ui/touchflick.h`)

Qt 6.11 has two faults on touch screens that report no velocities — Android's
(`QInputDevice::Capability::Velocity` unset):

* every fling starts at exactly 0.7x the finger's speed (half the distance): `QGuiApplication`
  copies the platform's velocity (0) into the persistent touch point and then filters
  0.7 · new + 0.3 · that 0 (`QMutableEventPoint::update` / `setTimestamp`, qeventpoint.cpp), and
  Flickable averages those samples;
* a drag that catches a running fling on a row's child (a block's text, a TapHandler, a
  MouseArea) does not move with the first move Flickable gets, then moves by both with the
  second: Flickable's filter clears its "steal" flag on the press, and after a fling the
  drag-start offset is not taken (qquickflickable.cpp, `filterPointerEvent` and `drag()`). On the
  Flickable itself the drag moves at once.

`TouchFlick.enabled: true` on the block editor's list, the journal stream, Tasks, the agenda, the
week grid and day panel, the sidebar tree, All pages, search results, tags, history, trash, the
outline and backlinks panels, source mode, settings, import/export and sync pages and the path
picker works around both from an application event filter that sees every touch stroke before Qt
Quick:

* when the release starts a flick and the samples carry the signature (each move reported at
  exactly 0.7x its own speed), it restarts the flick (`Flickable::flick()`) scaled by the
  finger's speed / Qt's estimate — the finger's speed is the time-weighted mean over the newest
  samples covering at least 3 segments and 40 ms (at most 100 ms). Qt's own decisions stay:
  whether to flick at all, the multi-flick boost, halving at an end, `maximumFlickVelocity`.
  Nothing changes for mouse, wheel, touchpad, stylus, multi-touch, devices that report
  velocities, or a Qt without the fault (no signature); lists that snap are left alone;
* when a press caught a fling, it moves the content with the finger itself — every raw move until
  Flickable has handled one (it is dragging then): with touch compression, Qt Quick's default,
  the moves that arrive within one frame reach Flickable as one, which it skips whole (a slow
  frame while a fling runs into new rows merges several). Flickable's next move puts the content
  where TouchFlick did. Side effects: Flickable emits `flickEnded` at the first move (setting
  `contentY` ends the movement it thinks is running) and again at the release when no new fling
  follows — nothing in the app listens to `flickStarted` / `flickEnded`; and `contentY` changes
  while neither dragging nor flicking, as with Qt's own catch on the Flickable itself (the
  "second line of defence" cache refill above may run once more per upward catch).

A few property reads per press, a list walk per move while a catch lasts; otherwise a type check
per event. `BRULION_TOUCHFLICK=0` turns it off everywhere (A/B measurements; on Android through a
debuggable APK's `extraenvvars`). ctest `tst_touch_flick` delivers touch like the Android QPA
(`QWindowSystemInterface`, explicit timestamps; deterministic): fling / finger 0.700 → 1.000 at
300–2200 px/s, 60–120 Hz and jittery timestamps; a caught fling's moves 0, 40, 20, 20 px → 20,
20, 20, 20 for a finger moving 20 px each; `tst_touch_flick_compressed` the same with Qt Quick's
compression and 1–3 moves per frame (the first version of the catch gave 10, 30 and 7, 33 there).
Its rows without TouchFlick fail when a Qt update changes either fault. Upstream bug report
drafts: both faults are unfixed in qtbase / qtdeclarative dev.

### Measured (before → after this work)

Scroll probe, phone 412×915 touch and desktop 1280×800 touchpad; "big" = 120–365 generated
journal days (`make-vault.py`); before = v0.1.1 with the probe added.

* x86 laptop (Xvfb, llvmpipe, RelWithDebInfo, two runs each): big journal, desktop touchpad —
  longest frame 2.9 / 2.7 s → 82 / 60 ms, p99 1025 / 882 → 16 ms, layout shifts 9 / 10 frames
  (2933 / 4640 px, single jumps up to 1566 px) → none; phone flick — frames over 100 ms 10 → 0,
  longest 144 → 63 ms; guide page "Witaj w Brulionie", desktop touchpad — longest 574 / 311 → 82 /
  84 ms, p99 420 / 104 → 15 ms.
* Raspberry Pi 4 (Cortex-A72, offscreen, Debug): big journal phone flick — longest frame 5.5 s →
  0.29 s, p99 739 → 57 ms; startup of a 2-year journal: first frame 2.0 → 1.3 s. Final tree:
  big journal desktop touchpad — longest 1.6 s → 0.30 s, layout shifts 15 frames (5214 px) →
  none; guide page desktop touchpad — longest 387 → 86 ms, frames over 100 ms 5 → 0; Tasks phone
  flick (3 runs) — longest 113–296 → 46–54 ms.
* Android 16 emulator (x86_64, 60 Hz, real `adb` swipes): a fling starts at 1.02–1.05x the
  finger's speed (0.73x with `BRULION_TOUCHFLICK=0`, Qt's 0.7); a drag that catches a fling moves
  with the finger (0 double steps; 4 per run of up to 31 px without TouchFlick); big journal
  longest frame 129 → 63 ms, none over 100 ms. The calendar agenda's top, reached for the first
  time, cost one frame over 100 ms in 3 of 5 runs before (152–231 ms), now only in the first run
  after installing.

### Still there

* **One still frame right after the lift-off** (Android's threaded render loop). Touch moves are
  applied at the start of a frame (`flushFrameSynchronousEvents` before polish and sync),
  animations advance after the sync (`QSGThreadedRenderLoop::polishAndSync`), so the fling's
  first step reaches the screen a frame later. No public API advances a Flickable's timeline;
  moving the content ahead through `contentY` stops the flick and emits movement signals.
* **The scroll bar's thumb jumps** — the content does not. ListView estimates the height of the
  rows it has not created from the average of those it has (`originPosition` / `lastPosition`),
  and blocks and days differ a lot in height, so `contentHeight` and `originY` change as rows come
  and go (ListView keeps the content in place; only the thumb's position and size move). ListView
  takes no per-row size hints, and the model knows a block's text, not its laid-out height, so a
  better estimate would need a list view of our own; not done.
* **Creating a day of the journal is one step**: ~10 ms plus ~1 ms per block on a laptop, 7–9 ms
  per block on a Raspberry Pi 4 (a day of 27–42 blocks: 0.25–0.28 s; with a query block in it
  more). See "a day of the journal stream is created whole" above for why it is not spread over
  frames.
* **Creating a table or a query block is expensive on ARM**: one or two frames of 60–100 ms each
  on a Raspberry Pi 4 (20–30 ms on a laptop), so a slow phone drag over the guide page is not
  smoother there than before (frames over 50 ms per run 1–7 → 8–9, the longest ~100 ms in both);
  the query block's results used to arrive a frame later and push the content down instead.
* The probe's "view velocity after" a release is still Qt's damped value (Flickable updates
  `verticalVelocity` with the restarted flick's first animation step); its "start velocity from
  its distance" shows what the fling did, and its TouchFlick line counts corrections and caught
  flings. In observe mode it judges touch drags only (a mouse or a stylus moves the content
  through Qt's mouse path).

## Note modes: outline and document

Every page and daily note has a mode — docs/SYNTAX.md §4 "Tryb notatki": the frontmatter key
`brulion: document | outline`, else the notebook's journal / page default and a content
heuristic (`markdown::resolveNoteMode`). Both modes edit the same `BlockDocument` with the same
inline editor; only the layout and a few keys differ.

* **Konspekt** (outline, like Logseq) — the block editor above: a bullet for every block, guide
  lines, folding, zoom into a bullet, Tab indents.
* **Dokument** (document, like Obsidian's live preview) — the note reads as one continuous
  formatted document: no bullets or guide lines for paragraphs and headings; headings large with
  document spacing; paragraphs as normal text; lists as ordinary Markdown lists inside the flow
  (bullets, numbers, checkboxes, indented per nesting level); a comfortable maximum text width.
  Clicking a paragraph, heading or list item edits its raw Markdown in place, exactly like the
  block editor's inline editing (rendered text everywhere else, no layout jump). Tables, calc,
  code, callouts, embeds, queries and the frontmatter properties work as in outline mode. No
  bullet zoom and no folding (folds made in the outline are kept and come back there).

Keys in document mode (the rest as in the block editor table):

| Keys | Paragraph / heading | List item |
|---|---|---|
| Enter | a new paragraph after it (also after a heading) | a new list item (checkboxes and numbering continue); on an empty item: leave the list — the item becomes a paragraph, or moves one level out when nested |
| Shift+Enter | a line break inside the block | same |
| Backspace at the start | merge with the previous block | a task first loses its checkbox; then the list marker goes: the item becomes a paragraph in its place, its children move one level out (Obsidian); a nested item moves one level out instead |
| Tab / Shift+Tab | nothing (no outline indentation) | indent / outdent with children |
| ↑ / ↓ | to the previous / next block, like in continuous text (the caret keeps its x on the page) | same |
| Ctrl+. | nothing (no folding) | same |

Source mode (Ctrl+E) is the same in both modes. A click below the last block adds an empty
paragraph (outline: a list item); typing `- `, `1. ` or `- [ ] ` at the start of a paragraph
makes it a list item, as in Markdown. On touch UIs the editing toolbar leads with the writing
tools — a list button (paragraph ↔ list item), Zadanie, Link do strony, Tag, Polecenia — and
puts indent / outdent (list items only) and moving after them.

How it is built: `PageController.mode` ("outline" | "document", with `modeSource`
"frontmatter" | "journal" | "heuristic" | "default") is resolved when the page is loaded and
kept while it is open (typing a list into a document never flips the layout under the caret);
only a change of the `brulion:` key itself (undo, source mode, another device) changes it —
without the key again the page gets back the mode it had without it in this session (undoing a
switch restores the previous mode) — and a mode taken from the notebook's defaults follows a
change of them (`refreshMode()`; the content is never resolved again while the page is open).
`setMode()` / `toggleMode()` write the
key as one undo step and save at once; `leaveList()` and `toggleList()` are the document's list
commands (`BlockDocument::convertToParagraph`). In the document mode the model ignores folds
(`BlockListModel::setFoldingEnabled(false)`; they come back in the outline) and there is no
zoom. `BlockEditor.documentMode` switches the delegates' layout: `gutter` = `documentInset` (0
on pages), a marker column of `listIndent` per list level (bullets drawn as disc / circle /
square by depth, numbers from the `listMarker` role, task checkboxes in place of the bullet),
heading sizes relative to the text size, and spacing from the `prevKind` role (paragraph gaps,
tight list items, air above headings, a little more above a frame right under a heading).
Rendered text and the inline editor have the same line boxes (the rendered height is counted in
whole fixed-height lines), so starting to edit moves nothing. A frontmatter that
holds nothing but the mode key — one `brulion: …` line, blank lines aside (`noteModeOnly`) — is
not shown: the header's toggle stands for it. A comment or any other text in it keeps the block
visible. The "Właściwości" panel never lists the key.

Choosing and switching the mode:

* **New page dialog** (`ShellNewPageDialog`, Ctrl+N, and `ContentNewPageDialog` in "Wszystkie
  strony", both with `ContentNoteModeChoice`): "Dokument (jak Obsidian)" / "Konspekt (jak
  Logseq)", preselected from the last choice on this device (as long as the notebook's
  `pageMode` is still the one it was made under), else from `pageMode`. The key is written only when the choice differs from
  what the resolution gives for the initial content (`markdown::withNoteModeChoice`). Pages
  created from unresolved `[[links]]` and by the quick switcher's "create" take the default
  without asking.
* **Switching**: a compact toggle in the page header next to the star (its icon shows the current
  mode; tooltip "Tryb dokumentu" / "Tryb konspektu"), the page menu item "Przełącz na dokument" /
  "Przełącz na konspekt" and the command palette's "Przełącz tryb notatki" (in the journal stream:
  the day being written in). A switch always writes the key (`markdown::setNoteModeKey`) and
  changes nothing else in the file; frontmatter that cannot be edited safely is left alone and a
  toast says so.
* **Settings → Notatnik**: "Domyślny tryb nowych stron" and "Tryb dziennika" (both: Dokument |
  Konspekt; defaults dokument and konspekt) — `pageMode` / `journalMode` in
  `.brulion/config.json`, synced.
* The **journal stream** shows each day in its own resolved mode (journals default to outline).
  A day switched to a document among outline days keeps their left edge (the date, the text:
  `BlockEditor.documentInset` = the outline's gutter); with `journalMode` document the days use
  the full width.

## Sync visibility and the event log

The user must always be able to tell what the sync is doing, whether something failed and
why — and be able to send us the log (docs/ARCHITECTURE.md "Logging").

* **Live progress** (`qml/io/SyncLiveProgress.qml` in the Nextcloud tab's status card, from
  `Workspace.sync.live`, at most four updates a second and once a second for the elapsed time;
  no timer while idle): the phase in words ("Sprawdzanie plików na serwerze…", "Wysyłanie i
  pobieranie zmian"), "folder 12 z ok. 40" / "sprawdzono 120 z 3125 plików · zmiany: 3", a bar
  (indeterminate until the files are known), "Trwa 1 min 20 s · 1,4 MB · po zmianie notatki ·
  #42", the files in flight ("Teraz: …") and "Zatrzymaj". When nothing arrives for 20 s an amber
  box says "Serwer nie odpowiada od 25 s…" with what Brulion waits for; after 180 s the run is
  stopped and retried — the panel never says "syncing" forever. `Workspace.sync.statusText`
  (sidebar footer, settings) follows the phases at the same pace.
* **Retry and decisions:** a failed run shows "Kolejna próba za 25 s" (a countdown) with
  "Spróbuj teraz"; held-back deletions and conflicts are "Czeka na Twoją decyzję" / "Konflikt w
  … — scal wersje" (the deletion guard and the conflict list stay as before).
* **History** (`SyncHistoryList.qml`, card "Historia synchronizacji"): the last runs (20 kept,
  5 shown, "Pokaż wszystkie"), quiet ones folded ("Bez zmian ×3, 09:25–09:55"); each row: the
  result in words with a coloured icon (never colour alone), when, why ("ręcznie", "po zmianie
  notatki", "ponowna próba", …), what moved and how long. "Ostatnio: … · Szczegóły" under the
  status. A row opens **`SyncRunDetails.qml`** (a dialog; bottom sheet on phones): the result
  with its explanation, code and technical message, "Na co poszedł czas" (a bar of the phases
  plus a list for screen readers), the changes, the requests to the server (count, average,
  bytes, protocol, timeouts, the slowest), the files that failed with a Polish explanation per
  code, conflicts ("Scal"), "Pokaż w rejestrze" (the log at the run's time) and "Kopiuj
  szczegóły".
* **Rejestr zdarzeń** (`qml/diag/LogsView.qml`, route `logs`; from Settings → Aplikacja (with a
  warning count) and Synchronizacja, the sync center, the palette — "rejestr", "logi",
  "diagnostyka"): a search field (case and diacritics ignored), area chips (Wszystko,
  Synchronizacja, Sieć, Notatnik, Indeks, Przypomnienia, Import i eksport, Aplikacja, System),
  the level (Wszystkie / Ostrzeżenia i błędy / Tylko błędy), chips for a run's time window and
  for the detailed mode; the list (`LogRow`: time, level icon + letter, area, the message as
  plain text, "×37" for repeats; warnings and errors tinted; day headers; earlier runs of the
  app included, "Ukryj wcześniejsze uruchomienia") follows new entries at the end, otherwise a
  "Nowe wpisy (n)" button appears; long press / right click copies a line. Actions: Kopiuj
  (what is shown, with the header; ≤ 256 KB), Udostępnij (Android share sheet), Zapisz w
  Pobranych (Android), Zapisz jako… (desktop, Android SAF), Pobierz (browser), Otwórz folder
  (desktop), Szczegółowy rejestr, Wyczyść… (confirmed). The footer: the detailed switch and what
  the log contains (no passwords or tokens; note names and the server may appear; it never
  leaves the device unless shared). Phones: the meta data above each message, Kopiuj in the
  menu.
* An export starts with a translated header: version, device, Qt, time and detail level, what
  it contains and what it never contains, and the last five sync runs.

## Shared components (`qml/components/`)

`BButton`, `BIconButton`, `BTextField`, `BTextArea`, `BSearchField`, `BSwitch`, `BCheckBox`,
`BChip`, `BBadge`, `BTabBar`/`BTabButton`, `BSegmented`, `BBreadcrumb`, `BCard`, `BListItem`,
`BSectionHeader`, `BKbd`, `BText`, `BDialog` (bottom sheet on narrow windows),
`BConfirmDialog`, `BMenu`/`BMenuItem`/`BMenuSeparator`, `BToast` (+ `BToastHost`),
`BToolTip`, `BEmptyState`, `BProgressBar`, `BSpinner`, `BScrollBar`, `BSplitHandle`,
`BShadow`, `BFocusRing`, `BDatePicker` (month grid, Polish names, natural-language input with
live preview), `BTimePicker`/`BTimePickerDialog` (a round 24-hour clock: tap or drag hours
and minutes, keyboard arrows), `BReminderDialog` (date/time, alarm switch, recurrence picker,
quick picks: za 10 min, za godzinę, dziś 19:00, jutro 8:00, poniedziałek 9:00), `BDiffView`,
`BMergeView` (per-conflict choice: Moja / Ich / Obie / Ręcznie), `BPalette` with `BCommandPalette` and
`BQuickSwitcher`. Icons: SVGs in `assets/icons/` (Lucide, ISC licence) via `BIcon`.

## Strings

`qsTr()` everywhere with English source text; Polish translation in `i18n/brulion_pl.ts`
(default UI language; `brulion_en.ts` only holds English plurals). Plurals with
`qsTr("%n notes", "", n)`. The language can be switched at run time (settings, palette).
Qt's own texts (dialog buttons, the QtQuick.Dialogs file dialog) come from Qt's catalogs
`qtbase_pl.qm` / `qtdeclarative_pl.qm`, bundled as resources (`QtTranslations` in
`src/app/ui`; nix: `qt6.qttranslations`) and switched with the language.

## Design system in code (`src/app/qml/theme`, `src/app/qml/components`, `src/app/ui`)

* **Theme** (`Theme` singleton): every colour, font, size, radius, duration comes from it —
  semantic colours (`bg`, `bgElevated`, `bgSunken`, `fg`, `fgMuted`, `fgFaint`, `border*`,
  `accent`, `link`, `tag*`, `chip*` via `Theme.chip(kind)`, `selection`, `focusRing`,
  `danger*`/`warning*`/`success*`, `diff*`), typography (`fontUi`, `fontEditor`,
  `fontMono`, `text*` sizes), spacing `space1…space16`, radii, `elevation(level)`,
  durations (0 with `reducedMotion`), `touchTarget` and control heights. Paragraph line
  heights are fixed pixel heights so they look the same with every font:
  `lineHeightMode: Text.FixedHeight; lineHeight: Theme.lineHeightPx(font.pixelSize)`
  (a proportional `lineHeight` multiplies the font's own spacing, 1.15–1.4 em depending on the
  font). White text goes on `danger`/`accentSolid`/`primary` fills only — the brand red
  `accent` is for lines, dots and focus marks (3.7:1 with white). Inputs set by the
  shell: `mode` ("system" | "light" | "dark"), `reducedMotion`, `editorFont`, `fontScale`,
  and **the main window must bind its size**:
  `Binding { target: Theme; property: "windowWidth"; value: window.width }` (same for
  `windowHeight`) — `narrow` (< 900 px), `compact` (< 600 px) and `touchUi` derive from it.
* **Components** are built on `QtQuick.Templates` (no style dependency), each file documents
  its API at the top. Conventions: icon-only buttons need a `label` (accessible name +
  tooltip); every interactive control shows `BFocusRing` on keyboard focus; on touch UIs
  hit areas are ≥ 44 px (visuals stay compact via insets) and hover effects are off;
  dialogs/popups are asynchronous (signals, never `exec()`); note text is never rendered
  as rich text unless escaped (`TextHelper.markMatches`, `DiffHelper.segmentsHtml`).
  Context menus open with a right-click and, on touch, a long press (`onPressAndHold` /
  `onLongPressed`); a right-click `TapHandler` also sets `acceptedDevices: PointerDevice.Mouse |
  PointerDevice.TouchPad | PointerDevice.Stylus` — Qt hands finger touches (no buttons) to a
  TapHandler whatever its `acceptedButtons`, so without it every tap opened the menu.
  Touch keyboards: `BTextField` asks for the action key Done (`EnterKey.type: Qt.EnterKeyDone`).
  Qt on Android otherwise shows Next whenever something focusable lies below a field, and Next
  only moves the focus: Enter and `accepted()` never arrive. Forms whose fields lead to the next
  one (Nextcloud, Syncthing) set `EnterKey.type: Qt.EnterKeyNext` on those fields. `BDatePicker`
  applies a typed date or time on Enter, when the field loses focus
  (`applyTypedOnFocusLoss`), when the keyboard is hidden and on Save (`commitPending()`, called
  from `BDialog.acceptRequested`); an invalid time keeps the dialog open with its hint.
* **C++ helpers** (QML singletons): `DatesHelper` (natural-language dates, human formatting,
  recurrence rules, localised names, month grids — all values are strings, no JS `Date`),
  `TextHelper` (diacritic folding, fuzzy ranking for palettes, escaping), `DiffHelper` (Myers
  line diff, word-level highlights, conflict hunks), `DeviceInfo`, `DevTools`. The first three
  are QML-free classes in the static library `brulion_ui_helpers` (`src/app/ui/CMakeLists.txt`,
  QtCore + core modules, unit-tested by `tests/app/tst_uihelpers.cpp`); the app registers them
  through `QML_FOREIGN` wrappers in `src/app/ui/qmlforeign.h`. New helper API: add it to the
  class, it reaches QML and qmllint through the library's extracted metatypes.
* **`TouchFlick`** (attached, `TouchFlick.enabled: true` on a Flickable / ListView): touch flicks
  at the finger's speed on Android ("Scrolling performance"); static library `brulion_ui_touch`
  (Qt Quick, no private Qt headers), tested by `tests/app/touch`. As an attached type it carries
  `QML_ATTACHED` itself (Qt resolves `qmlAttachedProperties` on the registered class), the app's
  wrapper in `qmlforeign.h` gives it its QML name.
* **Icons**: Lucide SVGs in `assets/icons/` (see its LICENSE), `BIcon { name: "bell" }`.
  New icons: copy from `lucide-static`, normalise like the others, re-run CMake.

## Dev tools, screenshots and UI tests

Command-line options of `brulion` (`src/app/ui/devtools.h`, `src/app/shell/appcontroller.h`,
`src/app/main.cpp`); unknown options are ignored. Headless runs:
`QT_QPA_PLATFORM=offscreen QT_QUICK_BACKEND=software`. The developer QML behind `--gallery`,
`--editor` and `--editor-script` (`qml/dev/`, `qml/content/ContentJournalsSelfTest.qml`) is built
unless the CMake option `BRULION_DEV_QML` (`AUTO`, `ON`, `OFF`) turns it off. `AUTO`, the
default, leaves it out of the shipped Android and browser builds (Release / MinSizeRel, about
0.7 MB less) and is decided again on every configure, so a build dir reused for another
`CMAKE_BUILD_TYPE` follows it. Without it `--editor-script` does nothing (stand-ins from
`qml/stubs/`) and `--gallery` / `--editor` end at once (exit code 1); the QML tests that need it
(`tst_editor_qml`, `tst_content_journals_qml`) are left out.

| Option | Effect |
|---|---|
| `--gallery [--section <id>]` | the component gallery (`qml/dev/Gallery.qml`) instead of the app; popups of the section open by themselves |
| `--editor [--editor-script <state>]` | the block editor playground (`qml/dev/EditorPlayground.qml`, in-memory `FakeEditorHost`) in a scripted state: `editing`, `suggest`, `tags`, `slash`, `date`, `zoom`, `source`, `reminder`, `table`, `calc`, `query`, …; the document mode on a sample trip plan: `document`, `document-paragraph`, `document-list`, `document-heading`, `document-task`, `document-outline` |
| `--demo` / `--vault <path>` / `--onboarding` | seed and open the sample notebook (in `--vault`, else "Przykładowy brulion") / open this folder / start on onboarding |
| `--route <view or JSON>` | initial route, e.g. `settings` or `'{"view":"page","path":"Projekty/Remont mieszkania.md","open":"links"}'` |
| `--popup <name[:query]>` | open a shell popup: `palette`, `switcher`, `capture`, `newPage`, `about`, `drawer` |
| `--editor-script <state>` (with the app) | `qml/dev/PageScript.qml`: the editor on the guide page (a document) in a state (`editing`, `slash`, `reminder`, `source`, `sidepanel`, `doc-paragraph`, `doc-list`, `doc-heading`, `doc-outline`; any page given by `--route`: `edit:<text>[@<caret>]`, `select:<text>`, `mode-tooltip`, …) |
| `--screenshot <file> [--delay <ms>]` | save the window after loading and quit |
| `--size <WxH>`, `--scale <f>`, `--touch`, `--theme light\|dark`, `--locale pl\|en`, `--reduced-motion`, `--now "<date> <time>"` | window size, device pixel ratio, touch UI, theme, language, motion, fixed clock |
| `--journey <dir>` (`--journey-theme`, `--journey-empty`) | walk the real app like a first-time user with real input events, a screenshot per step (`src/app/shell/shelljourney.h`; desktop builds) |
| `--selftest-shell` | key routing and focus checks of the real shell, and rows with context menus with real input — taps and long presses (`--touch`: the drawer, All pages, tasks, bullets) or left and right clicks; the event log page (a logged line arrives without its secret, the search ignores diacritics, an area chip filters, the detailed switch) and a sync run's details from the history ("Pokaż w rejestrze") (ctest `tst_shell_qml`, `tst_shell_qml_capture`; desktop builds) |
| `--selftest-import` (with `--onboarding`) | the ways to the import screen (onboarding, empty journal, drawer, Więcej) and an import from a typed path, with real taps / clicks (`src/app/io/ioselftest.h`, ctest `tst_appio_import_qml*`; desktop builds) |
| `--selftest-sounds` (with `--demo`) | Settings → Reminders: the sounds of reminders and alarms (Silent, Ringtone without one → the sound menu, a theme sound, the ringtone back, Play sample), with real taps / clicks (`src/app/notify/notifyselftest.h`, ctest `tst_notify_sounds_qml*`; desktop builds) |
| `--scroll-probe <target>` (`--scroll-script drag\|flick\|wheel\|touchpad\|observe`, `--probe-out <csv>`, `--probe-passes`, `--probe-wait`, `--probe-max`, `--probe-speed`, `--probe-check <px>`, `--probe-cache <px>`) | scroll a view with synthetic input (touch drags / flings, mouse wheel, touchpad) to the end and back — or, with `observe`, record real input (a finger, `adb shell input swipe`) for `--probe-max` ms — record every frame (interval, GUI work, contentY / originY / contentHeight, delegates created (once the view has placed them) — with their kind of block — and resized above / in / below the view, the content's movement on screen against the input, touch moves per frame) and print a summary of stutters, jumps and touch strokes (release speed, fling, its start velocity from its distance; TouchFlick's corrections and caught flings), then quit (`--probe-check <px>`: exit code 1 when the content jumps by more than <px> — at an end of the list only layout shifts count — ctest `tst_scroll_jumps_qml`; `--probe-cache <px>`: the list's cacheBuffer first, 0 = every row created at the edge of the view); the log also goes to `<csv>.log` (and logcat on Android) (`src/app/ui/scrollprobe.h`; target `auto`, `sidebar`, `name:<objectName>`, `type:<QML type>`). Android: Qt passes the activity's `applicationArguments` string extra only to debuggable APKs (`am start -n io.github.januszbit.brulion/.android.BrulionActivity --es applicationArguments "'--scroll-probe auto --scroll-script observe --probe-out /data/user/0/io.github.januszbit.brulion/files/p.csv'"`). Built unless `BRULION_SCROLL_PROBE=OFF`, the default of the shipped Android and browser builds (Release / MinSizeRel): measure with `BRULION_ANDROID_BUILD_TYPE=RelWithDebInfo` |
| `--hidden`, `--capture`, `--today`, `--no-single-instance` | start in the tray / open quick capture / today's journal (a second launch asks the running instance) / allow a second instance |

Scripts (inside `nix develop`; one GUI instance at a time via a lock; a fixed clock `NOW`;
the app scripts give every run a fresh temporary XDG home, so the user's own Brulion is never
touched; `SIZES`, `THEMES`, `LOCALE`, `OUT_DIR` select what to render; output in
`tests/screenshots/`, git-ignored):

* `scripts/screenshots.sh [sections…]` — gallery sections × {desktop 1280×800, phone 412×915
  `--touch`} × {light, dark}; `SIZES="desktop-tall phone-tall"` captures long sections whole.
* `tests/app/editor/editor-screenshots.sh` — the editor playground states.
* `tests/app/shell/screenshots.sh` — the real app: onboarding, journals, the guide page and
  its editor states (`page-editing`, `page-slash`, `page-reminder`, `page-doc-paragraph`,
  `page-doc-list`, `page-doc-heading`, `page-doc-outline`, …), the page menu (`page-menu`), the
  renovation project as an outline (`page-project`), the document mode on a long Obsidian-like
  note (`tests/app/shell/notemode/`: `doc-long`, editing a paragraph / list item / task / heading
  / numbered item, a selected block, the header's mode toggle with its tooltip, the same note as an
  outline: `doc-long-*`) and in the journal stream (`doc-journal`: all days documents,
  `doc-journal-mixed`: one document day among outlines), settings, palette, switcher, new page
  (with the mode choice), capture, drawer.
* `tests/app/content/screenshots.sh`, `tests/app/planning/screenshots.sh`,
  `tests/app/io/screenshots.sh` — content views, calendar/tasks/graph, sync center and
  import/export with prepared notebooks; the io script also seeds a sync history and an earlier
  session of the event log (`fixtures.sh`) for `sync-nextcloud`, `sync-details`,
  `sync-details-failed`, `logs`, `logs-sync`, `settings-app` (and `sync-stalled`, not in the
  default set: a server that never answers, 24 s per shot).
* `tests/app/perf/scroll-probe.sh` — scroll measurements (`--scroll-probe`) of the journal stream,
  pages (document / outline), All pages, Tasks, the calendar agenda and the sidebar × desktop
  (wheel, touchpad) / phone (touch drags, flings), on the sample notebook or `VAULT=big` (365
  synthetic days and long pages, `tests/app/perf/make-vault.py`); a CSV per run and a summary log
  in `tests/screenshots/perf/` (`CACHE=0`: the lists' cache off, runs named `…-cache0`).
  `tests/app/perf/perf-record.sh` records a CPU profile (perf) of one run;
  `tests/app/perf/android-scroll.sh` records real swipes on a phone or the emulator.
  `tests/app/perf/scroll-jumps.sh` (ctest `tst_scroll_jumps_qml`) scrolls the journal (the sample
  notebook plus 40 generated days with every kind of block: queries, calculations, embedded
  sections, blocks and pictures, tables, code), the guide page, Tasks and the calendar agenda
  with touchpad deltas and touch drags, mostly with the cache off (deterministic), and fails on
  jumps (`--probe-check`); the sanitizer build runs two of the cases.
* `tests/app/shell/journey.sh` — the user journey for desktop/phone × light/dark into
  `tests/screenshots/journey/<size>-<theme>/NN-<step>.png` (`JOURNEY=empty`: a new, empty
  notebook); ctest runs it as `tst_shell_journey` / `tst_shell_journey_desktop`.

On the laptop: `scripts/remote-build.sh -n ui brulion --run 'BRULION_BIN=$BRULION_BUILD_DIR/src/app/brulion
tests/app/shell/screenshots.sh' --pull tests/screenshots`. Fast QML iteration: configure a
separate build directory with `-DQT_QML_NO_CACHEGEN=ON`.

**README pictures** (`docs/images/`, Polish locale, the sample notebook) come from these scripts
— `SIZES=<size> THEMES=<theme> tests/app/<area>/screenshots.sh <shot>` — and are optimised with
`pngquant --quality 70-90 --strip` then `oxipng -o 4` (`nix shell nixpkgs#pngquant
nixpkgs#oxipng`; keep each under 150 KB):

| Image | Area, shot | Size, theme |
|---|---|---|
| `dziennik.png` | content `journals` | desktop, light |
| `strona-odnosniki.png` | content `side-links` | desktop, light |
| `kalendarz.png` | planning `calendar` | desktop, light |
| `zadania-ciemny.png` | planning `tasks` | desktop, dark |
| `przypomnienie.png` | shell `page-reminder` | desktop, light |
| `graf-ciemny.png` | planning `graph` | desktop, dark |
| `synchronizacja.png` | io `sync-conflict-merge`, run without the Nextcloud account of `io_nextcloud_home` (so the sidebar footer shows no connection error) | desktop, light |
| `telefon-dziennik.png` | content `journals` | phone, light |
| `telefon-edycja-ciemny.png` | shell `page-editing` | phone, dark |
